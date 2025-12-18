#ifndef JPEG_DECODER_H
#define JPEG_DECODER_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    void* tj_instance;
    uint32_t width;
    uint32_t height;
    uint8_t* rgb_buffer;
    size_t rgb_size;
} jpeg_decoder_t;

jpeg_decoder_t* jpeg_decoder_create(uint32_t width, uint32_t height);

int jpeg_decoder_decode(jpeg_decoder_t* dec, const void* jpeg_data, size_t jpeg_len);

void jpeg_decoder_destroy(jpeg_decoder_t* dec);

#endif
