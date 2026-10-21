#ifndef PORT_H
#define PORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PORT_N64_BUTTON_A 0x8000
#define PORT_N64_BUTTON_B 0x4000
#define PORT_N64_BUTTON_Z 0x2000
#define PORT_N64_BUTTON_START 0x1000
#define PORT_N64_DPAD_UP 0x0800
#define PORT_N64_DPAD_DOWN 0x0400
#define PORT_N64_DPAD_LEFT 0x0200
#define PORT_N64_DPAD_RIGHT 0x0100
#define PORT_N64_BUTTON_L 0x0020
#define PORT_N64_BUTTON_R 0x0010
#define PORT_N64_C_UP 0x0008
#define PORT_N64_C_DOWN 0x0004
#define PORT_N64_C_LEFT 0x0002
#define PORT_N64_C_RIGHT 0x0001

struct PortPadState {
    uint16_t buttons;
    int8_t stickX;
    int8_t stickY;
};

void game_loop_init(void);
void game_loop_iteration(void);
void sound_thread_init(void);
void sound_thread_tick(void);
void port_handle_vblank(void);
void *port_take_display_list(void);
void setup_mesg_queues(void);
void alloc_pool(void);
int32_t port_audio_enabled(void);

const char *port_user_path(void);
bool port_read_file(const char *name, void *buf, size_t size);
bool port_write_file(const char *name, const void *buf, size_t size);

bool port_audio_init(void);
void port_audio_shutdown(void);
void port_audio_set_frequency(uint32_t freq);
uint32_t port_audio_buffered_bytes(void);
void port_audio_queue(const void *samples, size_t bytes);

bool port_window_init(const char *title);
void port_window_shutdown(void);
bool port_window_poll_events(void);
void port_window_swap(void);
void port_window_get_size(int *width, int *height);
void port_window_toggle_fullscreen(void);
void port_window_set_vsync(bool enabled);
uint32_t port_window_refresh_rate(void);

void port_input_init(void);
void port_input_shutdown(void);
void port_input_update(void);
void port_input_get_pad(int index, struct PortPadState *out);
bool port_input_pad_connected(int index);
void port_input_rumble(int index, bool on);
void port_input_handle_event(const void *sdlEvent);
void port_input_touch_draw(void);

#endif
