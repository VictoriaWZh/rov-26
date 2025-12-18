#include "../include/udp_common.h"
#include "../include/udp_sender.h"
#include "../include/udp_receiver.h"
#include "../include/video_capturer.h"
#include "../include/frame_pipe.h"
#include "../include/frame_recorder.h"
#include "../include/display_renderer.h"
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OUTPUTS 8
#define OUTPUT_TYPE_SEND 1
#define OUTPUT_TYPE_RECORD 2
#define OUTPUT_TYPE_PIPE 3
#define OUTPUT_TYPE_RENDER 4

typedef struct {
    int type;
    union {
        udp_sender_t* sender;
        frame_recorder_t* recorder;
        frame_pipe_t* pipe;
        display_renderer_t* renderer;
    } handle;
    uint32_t send_rounds;
} output_slot_t;

typedef struct {
    bool enabled;
    uint64_t first_frame_time;
    uint64_t last_frame_time;
    uint64_t frame_count;
    uint64_t total_latency;
    uint64_t min_latency;
    uint64_t max_latency;
} profile_stats_t;

static volatile bool running = true;
static profile_stats_t profile = {0};

static void signal_handler(int sig) {
    (void)sig;
    running = false;
}

static void print_usage(void) {
    printf("mjpgo - Lean MJPEG Streaming\n\n");
    printf("Usage:\n");
    printf("  mjpgo [--profile] [input] [outputs...]\n\n");
    printf("Options:\n");
    printf("  --profile    Enable latency profiling (stats on SIGINT)\n\n");
    printf("Input (exactly one):\n");
    printf("  capture DEVICE WIDTH HEIGHT FPS_NUM FPS_DEN\n");
    printf("  receive IP PORT PACKET_LEN JPEG_LEN WIDTH HEIGHT FPS_NUM FPS_DEN\n\n");
    printf("Output (at least one):\n");
    printf("  send LOCAL_IP LOCAL_PORT REMOTE_IP REMOTE_PORT PACKET_LEN JPEG_LEN ROUNDS\n");
    printf("  record FILENAME\n");
    printf("  pipe FD CHUNK_SIZE\n");
    printf("  render WINDOW_WIDTH WINDOW_HEIGHT\n\n");
    printf("Commands:\n");
    printf("  help         Show this message\n");
    printf("  devices      List V4L2 devices with MJPEG support\n\n");
    printf("Examples:\n");
    printf("  mjpgo capture /dev/video0 640 480 1 30 render 1280 720\n");
    printf("  mjpgo capture /dev/video0 640 480 1 30 send 0.0.0.0 5000 192.168.1.2 5001 1400 500000 1\n");
    printf("  mjpgo receive 0.0.0.0 5001 1400 500000 640 480 1 30 render 1280 720\n");
}

static void print_profile_stats(void) {
    if (!profile.enabled || profile.frame_count == 0) return;
    
    printf("\n--- Profiling Statistics ---\n");
    printf("Frames:     %lu\n", profile.frame_count);
    
    if (profile.first_frame_time != profile.last_frame_time) {
        double duration_s = (profile.last_frame_time - profile.first_frame_time) / 1000000.0;
        double fps = profile.frame_count / duration_s;
        printf("Duration:   %.2f seconds\n", duration_s);
        printf("Average:    %.2f fps\n", fps);
    }
    
    if (profile.total_latency > 0) {
        printf("Latency:\n");
        printf("  Average:  %lu us\n", profile.total_latency / profile.frame_count);
        printf("  Min:      %lu us\n", profile.min_latency);
        printf("  Max:      %lu us\n", profile.max_latency);
    }
}

static void update_profile(uint64_t frame_ts) {
    if (!profile.enabled) return;
    
    uint64_t now = udp_get_time_us();
    
    if (profile.frame_count == 0) {
        profile.first_frame_time = now;
        profile.min_latency = UINT64_MAX;
    }
    
    profile.last_frame_time = now;
    profile.frame_count++;
    
    if (frame_ts > 0 && now > frame_ts) {
        uint64_t latency = now - frame_ts;
        profile.total_latency += latency;
        if (latency < profile.min_latency) profile.min_latency = latency;
        if (latency > profile.max_latency) profile.max_latency = latency;
    }
}

static bool check_renderer_open(output_slot_t* outputs, int count) {
    for (int i = 0; i < count; i++) {
        if (outputs[i].type == OUTPUT_TYPE_RENDER) {
            if (!display_renderer_is_open(outputs[i].handle.renderer)) {
                return false;
            }
        }
    }
    return true;
}

static void process_outputs(output_slot_t* outputs, int count, uint64_t ts,
                           const void* jpeg, size_t jpeg_len) {
    for (int i = 0; i < count; i++) {
        switch (outputs[i].type) {
            case OUTPUT_TYPE_SEND:
                udp_sender_transmit(outputs[i].handle.sender, ts, jpeg, jpeg_len, outputs[i].send_rounds);
                break;
            case OUTPUT_TYPE_RECORD:
                frame_recorder_write(outputs[i].handle.recorder, ts, jpeg, jpeg_len);
                break;
            case OUTPUT_TYPE_PIPE:
                frame_pipe_write(outputs[i].handle.pipe, ts, jpeg, jpeg_len);
                break;
            case OUTPUT_TYPE_RENDER:
                display_renderer_render(outputs[i].handle.renderer, jpeg, jpeg_len);
                break;
        }
    }
}

static void cleanup_outputs(output_slot_t* outputs, int count) {
    for (int i = 0; i < count; i++) {
        switch (outputs[i].type) {
            case OUTPUT_TYPE_SEND:
                udp_sender_destroy(outputs[i].handle.sender);
                break;
            case OUTPUT_TYPE_RECORD:
                frame_recorder_destroy(outputs[i].handle.recorder);
                break;
            case OUTPUT_TYPE_PIPE:
                frame_pipe_destroy(outputs[i].handle.pipe);
                break;
            case OUTPUT_TYPE_RENDER:
                display_renderer_destroy(outputs[i].handle.renderer);
                break;
        }
    }
}

static int parse_outputs(int argc, char** argv, int start_arg, output_slot_t* outputs,
                        int* out_count, uint32_t width, uint32_t height,
                        uint32_t fps_num, uint32_t fps_den, const char* window_title) {
    int next_arg = start_arg;
    int count = 0;
    
    (void)fps_num;
    (void)fps_den;
    
    while (next_arg < argc && count < MAX_OUTPUTS) {
        if (strcmp(argv[next_arg], "send") == 0) {
            if (argc < next_arg + 8) {
                fprintf(stderr, "send requires: LOCAL_IP LOCAL_PORT REMOTE_IP REMOTE_PORT PACKET_LEN JPEG_LEN ROUNDS\n");
                return -1;
            }
            
            udp_sender_t* sender = udp_sender_create(
                argv[next_arg + 1], atoi(argv[next_arg + 2]),
                argv[next_arg + 3], atoi(argv[next_arg + 4]),
                atoi(argv[next_arg + 5]), atoi(argv[next_arg + 6]));
            
            if (!sender) {
                fprintf(stderr, "Failed to create sender\n");
                return -1;
            }
            
            outputs[count].type = OUTPUT_TYPE_SEND;
            outputs[count].handle.sender = sender;
            outputs[count].send_rounds = atoi(argv[next_arg + 7]);
            count++;
            next_arg += 8;
            
        } else if (strcmp(argv[next_arg], "record") == 0) {
            if (argc < next_arg + 2) {
                fprintf(stderr, "record requires: FILENAME\n");
                return -1;
            }
            
            frame_recorder_t* rec = frame_recorder_create(
                argv[next_arg + 1], width, height, fps_num, fps_den);
            
            if (!rec) {
                fprintf(stderr, "Failed to create recorder: %s\n", argv[next_arg + 1]);
                return -1;
            }
            
            outputs[count].type = OUTPUT_TYPE_RECORD;
            outputs[count].handle.recorder = rec;
            count++;
            next_arg += 2;
            
        } else if (strcmp(argv[next_arg], "pipe") == 0) {
            if (argc < next_arg + 3) {
                fprintf(stderr, "pipe requires: FD CHUNK_SIZE\n");
                return -1;
            }
            
            int fd = atoi(argv[next_arg + 1]);
            uint32_t chunk = atoi(argv[next_arg + 2]);
            
            frame_pipe_t* pipe = frame_pipe_create(fd, chunk);
            if (!pipe) {
                fprintf(stderr, "Failed to create pipe\n");
                return -1;
            }
            
            outputs[count].type = OUTPUT_TYPE_PIPE;
            outputs[count].handle.pipe = pipe;
            count++;
            next_arg += 3;
            
        } else if (strcmp(argv[next_arg], "render") == 0) {
            if (argc < next_arg + 3) {
                fprintf(stderr, "render requires: WINDOW_WIDTH WINDOW_HEIGHT\n");
                return -1;
            }
            
            uint32_t win_w = atoi(argv[next_arg + 1]);
            uint32_t win_h = atoi(argv[next_arg + 2]);
            
            display_renderer_t* disp = display_renderer_create(width, height, win_w, win_h, window_title);
            if (!disp) {
                fprintf(stderr, "Failed to create renderer\n");
                return -1;
            }
            
            outputs[count].type = OUTPUT_TYPE_RENDER;
            outputs[count].handle.renderer = disp;
            count++;
            next_arg += 3;
            
        } else {
            fprintf(stderr, "Unknown output: %s\n", argv[next_arg]);
            return -1;
        }
    }
    
    *out_count = count;
    return next_arg;
}

static int run_capture_pipeline(int argc, char** argv, int arg_start) {
    if (argc < arg_start + 5) {
        fprintf(stderr, "capture requires: DEVICE WIDTH HEIGHT FPS_NUM FPS_DEN\n");
        return 1;
    }
    
    const char* device = argv[arg_start];
    uint32_t width = atoi(argv[arg_start + 1]);
    uint32_t height = atoi(argv[arg_start + 2]);
    uint32_t fps_num = atoi(argv[arg_start + 3]);
    uint32_t fps_den = atoi(argv[arg_start + 4]);
    
    video_capturer_t* cap = video_capturer_create(device, width, height, fps_num, fps_den);
    if (!cap) {
        fprintf(stderr, "Failed to open capture device: %s\n", device);
        return 1;
    }
    
    char title[256];
    snprintf(title, sizeof(title), "mjpgo - %s %ux%u", device, width, height);
    
    output_slot_t outputs[MAX_OUTPUTS];
    int output_count = 0;
    memset(outputs, 0, sizeof(outputs));
    
    if (parse_outputs(argc, argv, arg_start + 5, outputs, &output_count,
                     width, height, fps_num, fps_den, title) < 0) {
        video_capturer_destroy(cap);
        cleanup_outputs(outputs, output_count);
        return 1;
    }
    
    if (output_count == 0) {
        fprintf(stderr, "At least one output required\n");
        video_capturer_destroy(cap);
        return 1;
    }
    
    printf("Capturing from %s at %ux%u [%u/%u]\n", device, width, height, fps_num, fps_den);
    
    while (running && check_renderer_open(outputs, output_count)) {
        if (video_capturer_grab_frame(cap) < 0) {
            fprintf(stderr, "Frame capture failed\n");
            break;
        }
        
        capture_buffer_t* buf = &cap->buffers[cap->active_index];
        update_profile(buf->timestamp_us);
        process_outputs(outputs, output_count, buf->timestamp_us, buf->data, buf->used);
        video_capturer_release_frame(cap);
    }
    
    cleanup_outputs(outputs, output_count);
    video_capturer_destroy(cap);
    print_profile_stats();
    return 0;
}

static int run_receive_pipeline(int argc, char** argv, int arg_start) {
    if (argc < arg_start + 8) {
        fprintf(stderr, "receive requires: IP PORT PACKET_LEN JPEG_LEN WIDTH HEIGHT FPS_NUM FPS_DEN\n");
        return 1;
    }
    
    const char* ip = argv[arg_start];
    uint16_t port = atoi(argv[arg_start + 1]);
    uint32_t packet_len = atoi(argv[arg_start + 2]);
    uint32_t jpeg_len = atoi(argv[arg_start + 3]);
    uint32_t width = atoi(argv[arg_start + 4]);
    uint32_t height = atoi(argv[arg_start + 5]);
    uint32_t fps_num = atoi(argv[arg_start + 6]);
    uint32_t fps_den = atoi(argv[arg_start + 7]);
    
    udp_receiver_t* recv = udp_receiver_create(ip, port, packet_len, jpeg_len);
    if (!recv) {
        fprintf(stderr, "Failed to create receiver on %s:%u\n", ip, port);
        return 1;
    }
    
    char title[256];
    snprintf(title, sizeof(title), "mjpgo - %s:%u %ux%u", ip, port, width, height);
    
    output_slot_t outputs[MAX_OUTPUTS];
    int output_count = 0;
    memset(outputs, 0, sizeof(outputs));
    
    if (parse_outputs(argc, argv, arg_start + 8, outputs, &output_count,
                     width, height, fps_num, fps_den, title) < 0) {
        udp_receiver_destroy(recv);
        cleanup_outputs(outputs, output_count);
        return 1;
    }
    
    printf("Receiving on %s:%u\n", ip, port);
    
    while (running && check_renderer_open(outputs, output_count)) {
        if (!udp_receiver_get_frame(recv)) break;
        
        update_profile(recv->frame_ts_us);
        process_outputs(outputs, output_count, recv->frame_ts_us, recv->frame_buf, recv->frame_len);
    }
    
    cleanup_outputs(outputs, output_count);
    udp_receiver_destroy(recv);
    print_profile_stats();
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    int arg_idx = 1;
    
    if (strcmp(argv[1], "--profile") == 0) {
        profile.enabled = true;
        arg_idx = 2;
        if (argc < 3) {
            print_usage();
            return 1;
        }
    }
    
    const char* cmd = argv[arg_idx];
    
    if (strcmp(cmd, "help") == 0) {
        print_usage();
        return 0;
    }
    
    if (strcmp(cmd, "devices") == 0) {
        video_capturer_list_devices();
        return 0;
    }
    
    if (strcmp(cmd, "capture") == 0) {
        return run_capture_pipeline(argc, argv, arg_idx + 1);
    }
    
    if (strcmp(cmd, "receive") == 0) {
        return run_receive_pipeline(argc, argv, arg_idx + 1);
    }
    
    fprintf(stderr, "Unknown command: %s\n", cmd);
    print_usage();
    return 1;
}
