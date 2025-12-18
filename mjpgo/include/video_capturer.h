#ifndef VIDEO_CAPTURER_H
#define VIDEO_CAPTURER_H

#include <stdint.h>
#include <stddef.h>

#define CAPTURER_BUFFER_COUNT 4

typedef struct {
    void* data;
    size_t length;
    size_t used;
    uint64_t timestamp_us;
} capture_buffer_t;

typedef struct {
    int device_fd;
    capture_buffer_t buffers[CAPTURER_BUFFER_COUNT];
    int active_index;
    uint64_t epoch_offset_us;
    uint32_t width;
    uint32_t height;
} video_capturer_t;

video_capturer_t* video_capturer_create(const char* device_path,
                                         uint32_t width, uint32_t height,
                                         uint32_t fps_num, uint32_t fps_den);

int video_capturer_grab_frame(video_capturer_t* cap);

void video_capturer_release_frame(video_capturer_t* cap);

void video_capturer_destroy(video_capturer_t* cap);

void video_capturer_list_devices(void);

#endif
