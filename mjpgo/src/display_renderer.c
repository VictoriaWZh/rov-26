#include "../include/display_renderer.h"
#include <SDL2/SDL.h>
#include <turbojpeg.h>
#include <stdlib.h>
#include <string.h>

struct display_renderer {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    tjhandle tj_instance;
    uint8_t* rgb_buffer;
    uint32_t frame_width;
    uint32_t frame_height;
    bool open;
};

display_renderer_t* display_renderer_create(uint32_t frame_width, uint32_t frame_height,
                                             uint32_t window_width, uint32_t window_height,
                                             const char* title) {
    display_renderer_t* disp = calloc(1, sizeof(*disp));
    if (!disp) return NULL;
    
    disp->frame_width = frame_width;
    disp->frame_height = frame_height;
    
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        free(disp);
        return NULL;
    }
    
    disp->window = SDL_CreateWindow(
        title ? title : "mjpgo",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_width, window_height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!disp->window) {
        SDL_Quit();
        free(disp);
        return NULL;
    }
    
    disp->renderer = SDL_CreateRenderer(
        disp->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    
    if (!disp->renderer) {
        SDL_DestroyWindow(disp->window);
        SDL_Quit();
        free(disp);
        return NULL;
    }
    
    disp->texture = SDL_CreateTexture(
        disp->renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        frame_width, frame_height
    );
    
    if (!disp->texture) {
        SDL_DestroyRenderer(disp->renderer);
        SDL_DestroyWindow(disp->window);
        SDL_Quit();
        free(disp);
        return NULL;
    }
    
    disp->tj_instance = tjInitDecompress();
    if (!disp->tj_instance) {
        SDL_DestroyTexture(disp->texture);
        SDL_DestroyRenderer(disp->renderer);
        SDL_DestroyWindow(disp->window);
        SDL_Quit();
        free(disp);
        return NULL;
    }
    
    disp->rgb_buffer = tjAlloc(frame_width * frame_height * 3);
    if (!disp->rgb_buffer) {
        tjDestroy(disp->tj_instance);
        SDL_DestroyTexture(disp->texture);
        SDL_DestroyRenderer(disp->renderer);
        SDL_DestroyWindow(disp->window);
        SDL_Quit();
        free(disp);
        return NULL;
    }
    
    disp->open = true;
    return disp;
}

bool display_renderer_is_open(display_renderer_t* disp) {
    if (!disp) return false;
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            disp->open = false;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            disp->open = false;
        }
    }
    
    return disp->open;
}

int display_renderer_render(display_renderer_t* disp, const void* jpeg_data, size_t jpeg_len) {
    if (!disp || !jpeg_data || jpeg_len == 0) return -1;
    if (!disp->open) return -1;
    
    int result = tjDecompress2(
        disp->tj_instance,
        (unsigned char*)jpeg_data,
        jpeg_len,
        disp->rgb_buffer,
        disp->frame_width,
        disp->frame_width * 3,
        disp->frame_height,
        TJPF_RGB,
        TJFLAG_FASTDCT
    );
    
    if (result < 0) return -1;
    
    SDL_UpdateTexture(
        disp->texture,
        NULL,
        disp->rgb_buffer,
        disp->frame_width * 3
    );
    
    SDL_RenderClear(disp->renderer);
    SDL_RenderCopy(disp->renderer, disp->texture, NULL, NULL);
    SDL_RenderPresent(disp->renderer);
    
    return 0;
}

void display_renderer_destroy(display_renderer_t* disp) {
    if (!disp) return;
    
    if (disp->rgb_buffer) tjFree(disp->rgb_buffer);
    if (disp->tj_instance) tjDestroy(disp->tj_instance);
    if (disp->texture) SDL_DestroyTexture(disp->texture);
    if (disp->renderer) SDL_DestroyRenderer(disp->renderer);
    if (disp->window) SDL_DestroyWindow(disp->window);
    
    SDL_Quit();
    free(disp);
}
