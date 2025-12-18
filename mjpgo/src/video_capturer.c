#include "../include/video_capturer.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int safe_ioctl(int fd, unsigned long req, void* arg) {
    int result;
    do {
        result = ioctl(fd, req, arg);
    } while (result < 0 && errno == EINTR);
    return result;
}

static uint64_t compute_epoch_offset(void) {
    struct timeval wall;
    struct timespec mono;
    gettimeofday(&wall, NULL);
    clock_gettime(CLOCK_MONOTONIC, &mono);
    uint64_t wall_us = (uint64_t)wall.tv_sec * 1000000ULL + (uint64_t)wall.tv_usec;
    uint64_t mono_us = (uint64_t)mono.tv_sec * 1000000ULL + (uint64_t)((mono.tv_nsec + 500) / 1000);
    return wall_us - mono_us;
}

video_capturer_t* video_capturer_create(const char* device_path,
                                         uint32_t width, uint32_t height,
                                         uint32_t fps_num, uint32_t fps_den) {
    struct stat st;
    if (stat(device_path, &st) < 0) return NULL;
    if (!S_ISCHR(st.st_mode)) return NULL;
    
    video_capturer_t* cap = calloc(1, sizeof(*cap));
    if (!cap) return NULL;
    
    cap->device_fd = open(device_path, O_RDWR);
    if (cap->device_fd < 0) {
        free(cap);
        return NULL;
    }
    
    cap->width = width;
    cap->height = height;
    cap->epoch_offset_us = compute_epoch_offset();
    cap->active_index = -1;
    
    struct v4l2_capability caps;
    memset(&caps, 0, sizeof(caps));
    if (safe_ioctl(cap->device_fd, VIDIOC_QUERYCAP, &caps) < 0) goto fail;
    if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)) goto fail;
    if (!(caps.capabilities & V4L2_CAP_STREAMING)) goto fail;
    
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    if (safe_ioctl(cap->device_fd, VIDIOC_S_FMT, &fmt) < 0) goto fail;
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) goto fail;
    
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = fps_num;
    parm.parm.capture.timeperframe.denominator = fps_den;
    if (safe_ioctl(cap->device_fd, VIDIOC_S_PARM, &parm) < 0) goto fail;
    
    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.count = CAPTURER_BUFFER_COUNT;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    if (safe_ioctl(cap->device_fd, VIDIOC_REQBUFS, &reqbuf) < 0) goto fail;
    if (reqbuf.count != CAPTURER_BUFFER_COUNT) goto fail;
    
    for (int i = 0; i < CAPTURER_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (safe_ioctl(cap->device_fd, VIDIOC_QUERYBUF, &buf) < 0) goto fail;
        
        cap->buffers[i].length = buf.length;
        cap->buffers[i].data = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, cap->device_fd, buf.m.offset);
        if (cap->buffers[i].data == MAP_FAILED) goto fail;
    }
    
    for (int i = 0; i < CAPTURER_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (safe_ioctl(cap->device_fd, VIDIOC_QBUF, &buf) < 0) goto fail;
    }
    
    enum v4l2_buf_type stream_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (safe_ioctl(cap->device_fd, VIDIOC_STREAMON, &stream_type) < 0) goto fail;
    
    return cap;

fail:
    for (int i = 0; i < CAPTURER_BUFFER_COUNT; i++) {
        if (cap->buffers[i].data && cap->buffers[i].data != MAP_FAILED) {
            munmap(cap->buffers[i].data, cap->buffers[i].length);
        }
    }
    close(cap->device_fd);
    free(cap);
    return NULL;
}

int video_capturer_grab_frame(video_capturer_t* cap) {
    if (!cap || cap->device_fd < 0) return -1;
    
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (safe_ioctl(cap->device_fd, VIDIOC_DQBUF, &buf) < 0) return -1;
    
    cap->active_index = buf.index;
    cap->buffers[buf.index].used = buf.bytesused;
    cap->buffers[buf.index].timestamp_us = 
        (uint64_t)buf.timestamp.tv_sec * 1000000ULL + 
        (uint64_t)buf.timestamp.tv_usec + 
        cap->epoch_offset_us;
    
    return 0;
}

void video_capturer_release_frame(video_capturer_t* cap) {
    if (!cap || cap->active_index < 0) return;
    
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = cap->active_index;
    
    safe_ioctl(cap->device_fd, VIDIOC_QBUF, &buf);
    cap->active_index = -1;
}

void video_capturer_destroy(video_capturer_t* cap) {
    if (!cap) return;
    
    enum v4l2_buf_type stream_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    safe_ioctl(cap->device_fd, VIDIOC_STREAMOFF, &stream_type);
    
    for (int i = 0; i < CAPTURER_BUFFER_COUNT; i++) {
        if (cap->buffers[i].data && cap->buffers[i].data != MAP_FAILED) {
            munmap(cap->buffers[i].data, cap->buffers[i].length);
        }
    }
    
    close(cap->device_fd);
    free(cap);
}

void video_capturer_list_devices(void) {
    DIR* dev = opendir("/dev");
    if (!dev) return;
    
    struct dirent* entry;
    while ((entry = readdir(dev)) != NULL) {
        if (strncmp(entry->d_name, "video", 5) != 0) continue;
        
        char path[256];
        snprintf(path, sizeof(path), "/dev/%s", entry->d_name);
        
        int fd = open(path, O_RDWR);
        if (fd < 0) continue;
        
        struct v4l2_capability caps;
        memset(&caps, 0, sizeof(caps));
        if (safe_ioctl(fd, VIDIOC_QUERYCAP, &caps) < 0) {
            close(fd);
            continue;
        }
        
        printf("%s:\n", path);
        
        struct v4l2_fmtdesc fmtdesc;
        memset(&fmtdesc, 0, sizeof(fmtdesc));
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        
        int has_mjpeg = 0;
        while (safe_ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
            if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG) {
                has_mjpeg = 1;
                break;
            }
            fmtdesc.index++;
        }
        
        if (!has_mjpeg) {
            printf("  No MJPEG support\n");
            close(fd);
            continue;
        }
        
        struct v4l2_frmsizeenum frmsize;
        memset(&frmsize, 0, sizeof(frmsize));
        frmsize.pixel_format = V4L2_PIX_FMT_MJPEG;
        
        while (safe_ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                printf("  %ux%u:", frmsize.discrete.width, frmsize.discrete.height);
                
                struct v4l2_frmivalenum frmival;
                memset(&frmival, 0, sizeof(frmival));
                frmival.pixel_format = V4L2_PIX_FMT_MJPEG;
                frmival.width = frmsize.discrete.width;
                frmival.height = frmsize.discrete.height;
                
                while (safe_ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0) {
                    if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
                        int fps = frmival.discrete.denominator / frmival.discrete.numerator;
                        printf(" %dfps", fps);
                    }
                    frmival.index++;
                }
                printf("\n");
            }
            frmsize.index++;
        }
        
        close(fd);
    }
    
    closedir(dev);
}
