#include <SDL.h>

#include "port.h"

static SDL_AudioDeviceID sDevice;
static uint32_t sFrequency = 32000;
static bool sInitialized;

static bool open_device(void) {
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    if (sDevice != 0) {
        SDL_CloseAudioDevice(sDevice);
        sDevice = 0;
    }
    SDL_zero(want);
    want.freq = (int) sFrequency;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = NULL;
    sDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (sDevice == 0) {
        SDL_Log("Audio device error: %s", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(sDevice, 0);
    return true;
}

bool port_audio_init(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL audio init failed: %s", SDL_GetError());
        return false;
    }
    sInitialized = true;
    return open_device();
}

void port_audio_shutdown(void) {
    if (sDevice != 0) {
        SDL_CloseAudioDevice(sDevice);
        sDevice = 0;
    }
    if (sInitialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        sInitialized = false;
    }
}

void port_audio_set_frequency(uint32_t freq) {
    if (freq == 0 || freq == sFrequency) {
        return;
    }
    sFrequency = freq;
    if (sInitialized) {
        open_device();
    }
}

uint32_t port_audio_buffered_bytes(void) {
    if (sDevice == 0) {
        return 0;
    }
    return SDL_GetQueuedAudioSize(sDevice);
}

void port_audio_queue(const void *samples, size_t bytes) {
    if (sDevice == 0) {
        return;
    }
    if (SDL_GetQueuedAudioSize(sDevice) > sFrequency * 4 / 4) {
        return;
    }
    SDL_QueueAudio(sDevice, samples, (Uint32) bytes);
}
