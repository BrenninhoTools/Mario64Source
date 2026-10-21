#include <SDL.h>
#include <SDL_main.h>

#include "port.h"
#include "gfx/gfx_rsp.h"

#if defined(VERSION_EU)
#define GAME_FPS 25.0
#else
#define GAME_FPS 30.0
#endif

#define MAX_CATCHUP_FRAMES 4

static void render_latest(void *dl) {
    int w = 0;
    int h = 0;
    port_window_get_size(&w, &h);
    gfx_set_window_size((uint32_t) w, (uint32_t) h);
    gfx_start_frame();
    gfx_run(dl);
    gfx_end_frame();
    port_window_swap();
}

int main(int argc, char *argv[]) {
    double frameTime = 1.0 / GAME_FPS;
    double accumulator = 0.0;
    Uint64 freq;
    Uint64 last;
    void *lastDisplayList = NULL;
    (void) argc;
    (void) argv;

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        SDL_Log("SDL init failed: %s", SDL_GetError());
        return 1;
    }
    if (!port_window_init("Super Mario 64")) {
        SDL_Quit();
        return 1;
    }
    if (!gfx_init()) {
        SDL_Log("Renderer init failed");
        port_window_shutdown();
        SDL_Quit();
        return 1;
    }
    port_audio_init();
    port_input_init();

    setup_mesg_queues();
    alloc_pool();
    sound_thread_init();
    game_loop_init();

    freq = SDL_GetPerformanceFrequency();
    last = SDL_GetPerformanceCounter();

    for (;;) {
        Uint64 now;
        int steps = 0;
        void *dl;

        if (!port_window_poll_events()) {
            break;
        }

        now = SDL_GetPerformanceCounter();
        accumulator += (double) (now - last) / (double) freq;
        last = now;
        if (accumulator > frameTime * MAX_CATCHUP_FRAMES) {
            accumulator = frameTime;
        }

        while (accumulator >= frameTime && steps < MAX_CATCHUP_FRAMES) {
            game_loop_iteration();
            port_handle_vblank();
            sound_thread_tick();
            port_handle_vblank();
            sound_thread_tick();
            dl = port_take_display_list();
            if (dl != NULL) {
                lastDisplayList = dl;
            }
            accumulator -= frameTime;
            steps++;
        }

        if (lastDisplayList != NULL) {
            render_latest(lastDisplayList);
        } else {
            SDL_Delay(1);
        }

        if (steps == 0 && port_window_refresh_rate() > 240) {
            SDL_Delay(1);
        }
    }

    gfx_shutdown();
    port_input_shutdown();
    port_audio_shutdown();
    port_window_shutdown();
    SDL_Quit();
    return 0;
}
