#include "../include/frame_pipe.h"
#include <endian.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

frame_pipe_t* frame_pipe_create(int fd, uint32_t chunk_size) {
    if (fd < 0) return NULL;
    
    frame_pipe_t* pipe = calloc(1, sizeof(*pipe));
    if (!pipe) return NULL;
    
    pipe->fd = fd;
    pipe->chunk_size = chunk_size > 0 ? chunk_size : 4096;
    
    return pipe;
}

static int write_all(int fd, const void* buf, size_t len) {
    const uint8_t* ptr = buf;
    size_t remaining = len;
    
    while (remaining > 0) {
        ssize_t written = write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        ptr += written;
        remaining -= written;
    }
    
    return 0;
}

int frame_pipe_write(frame_pipe_t* pipe, uint64_t timestamp_us,
                     const void* data, size_t data_len) {
    if (!pipe || !data || data_len == 0) return -1;
    
    uint64_t ts_be = htobe64(timestamp_us);
    if (write_all(pipe->fd, &ts_be, sizeof(ts_be)) < 0) return -1;
    
    uint32_t len_be = htobe32((uint32_t)data_len);
    if (write_all(pipe->fd, &len_be, sizeof(len_be)) < 0) return -1;
    
    const uint8_t* src = data;
    size_t remaining = data_len;
    
    while (remaining > 0) {
        size_t chunk = remaining < pipe->chunk_size ? remaining : pipe->chunk_size;
        if (write_all(pipe->fd, src, chunk) < 0) return -1;
        src += chunk;
        remaining -= chunk;
    }
    
    return 0;
}

void frame_pipe_destroy(frame_pipe_t* pipe) {
    free(pipe);
}
