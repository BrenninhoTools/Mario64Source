#include <SDL.h>

#include "port.h"

static SDL_Window *sWindow;
static SDL_GLContext sContext;
static bool sFullscreen;

bool port_window_init(const char *title) {
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    int w = 1280;
    int h = 720;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SDL_Log("SDL video init failed: %s", SDL_GetError());
        return false;
    }

#ifdef PORT_USE_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    w = 0;
    h = 0;
    sFullscreen = true;
#endif

    sWindow = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (sWindow == NULL) {
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
        sWindow = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    }
    if (sWindow == NULL) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        return false;
    }
    sContext = SDL_GL_CreateContext(sWindow);
    if (sContext == NULL) {
        SDL_Log("GL context creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(sWindow, sContext);
    port_window_set_vsync(true);
    return true;
}

void port_window_shutdown(void) {
    if (sContext != NULL) {
        SDL_GL_DeleteContext(sContext);
        sContext = NULL;
    }
    if (sWindow != NULL) {
        SDL_DestroyWindow(sWindow);
        sWindow = NULL;
    }
}

void port_window_set_vsync(bool enabled) {
    if (enabled) {
        if (SDL_GL_SetSwapInterval(-1) != 0) {
            SDL_GL_SetSwapInterval(1);
        }
    } else {
        SDL_GL_SetSwapInterval(0);
    }
}

uint32_t port_window_refresh_rate(void) {
    SDL_DisplayMode mode;
    int index = sWindow != NULL ? SDL_GetWindowDisplayIndex(sWindow) : 0;
    if (SDL_GetCurrentDisplayMode(index < 0 ? 0 : index, &mode) == 0 && mode.refresh_rate > 0) {
        return (uint32_t) mode.refresh_rate;
    }
    return 60;
}

void port_window_toggle_fullscreen(void) {
#ifndef __ANDROID__
    sFullscreen = !sFullscreen;
    SDL_SetWindowFullscreen(sWindow, sFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#endif
}

bool port_window_poll_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                return false;
            case SDL_KEYDOWN:
                if (e.key.repeat == 0) {
                    if (e.key.keysym.scancode == SDL_SCANCODE_F11
                        || (e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                        port_window_toggle_fullscreen();
                        break;
                    }
                }
                port_input_handle_event(&e);
                break;
            default:
                port_input_handle_event(&e);
                break;
        }
    }
    return true;
}

void port_window_swap(void) {
    SDL_GL_SwapWindow(sWindow);
}

void port_window_get_size(int *width, int *height) {
    SDL_GL_GetDrawableSize(sWindow, width, height);
}
