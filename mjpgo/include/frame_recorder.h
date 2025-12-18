#ifndef FRAME_RECORDER_H
#define FRAME_RECORDER_H

#include <stdint.h>
#include <stddef.h>

typedef struct frame_recorder frame_recorder_t;

frame_recorder_t* frame_recorder_create(const char* filename,
                                         uint32_t width, uint32_t height,
                                         uint32_t fps_num, uint32_t fps_den);

int frame_recorder_write(frame_recorder_t* rec, uint64_t timestamp_us,
                         const void* jpeg_data, size_t jpeg_len);

void frame_recorder_destroy(frame_recorder_t* rec);

#endif
