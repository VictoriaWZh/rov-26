#include "../include/jpeg_decoder.h"
#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>

jpeg_decoder_t* jpeg_decoder_create(uint32_t width, uint32_t height) {
    jpeg_decoder_t* dec = calloc(1, sizeof(*dec));
    if (!dec) return NULL;
    
    dec->tj_instance = tjInitDecompress();
    if (!dec->tj_instance) {
        free(dec);
        return NULL;
    }
    
    dec->width = width;
    dec->height = height;
    dec->rgb_size = (size_t)width * height * 3;
    dec->rgb_buffer = tjAlloc(dec->rgb_size);
    
    if (!dec->rgb_buffer) {
        tjDestroy(dec->tj_instance);
        free(dec);
        return NULL;
    }
    
    return dec;
}

int jpeg_decoder_decode(jpeg_decoder_t* dec, const void* jpeg_data, size_t jpeg_len) {
    if (!dec || !jpeg_data || jpeg_len == 0) return -1;
    
    int result = tjDecompress2(
        dec->tj_instance,
        (unsigned char*)jpeg_data,
        jpeg_len,
        dec->rgb_buffer,
        dec->width,
        0,
        dec->height,
        TJPF_RGB,
        TJFLAG_FASTDCT
    );
    
    return (result == 0) ? 0 : -1;
}

void jpeg_decoder_destroy(jpeg_decoder_t* dec) {
    if (!dec) return;
    if (dec->rgb_buffer) tjFree(dec->rgb_buffer);
    if (dec->tj_instance) tjDestroy(dec->tj_instance);
    free(dec);
}
