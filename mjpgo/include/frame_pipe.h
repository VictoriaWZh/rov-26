#ifndef FRAME_PIPE_H
#define FRAME_PIPE_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int fd;
    uint32_t chunk_size;
} frame_pipe_t;

frame_pipe_t* frame_pipe_create(int fd, uint32_t chunk_size);

int frame_pipe_write(frame_pipe_t* pipe, uint64_t timestamp_us,
                     const void* data, size_t data_len);

void frame_pipe_destroy(frame_pipe_t* pipe);

#endif
