#ifndef DISPLAY_RENDERER_H
#define DISPLAY_RENDERER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct display_renderer display_renderer_t;

display_renderer_t* display_renderer_create(uint32_t frame_width, uint32_t frame_height,
                                             uint32_t window_width, uint32_t window_height,
                                             const char* title);

bool display_renderer_is_open(display_renderer_t* disp);

int display_renderer_render(display_renderer_t* disp, const void* jpeg_data, size_t jpeg_len);

void display_renderer_destroy(display_renderer_t* disp);

#endif
