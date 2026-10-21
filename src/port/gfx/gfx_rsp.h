#ifndef PORT_GFX_RSP_H
#define PORT_GFX_RSP_H

#include <stdint.h>
#include <stdbool.h>

struct GfxDimensions {
    uint32_t width;
    uint32_t height;
    float aspect_ratio;
};

extern struct GfxDimensions gfx_current_dimensions;

typedef void (*GfxOverlayCallback)(void);

bool gfx_init(void);
void gfx_shutdown(void);
void gfx_set_window_size(uint32_t width, uint32_t height);
void gfx_start_frame(void);
void gfx_run(void *commands);
void gfx_draw_overlay_quad(float x0, float y0, float x1, float y1, const float color[4]);
void gfx_draw_overlay_circle(float cx, float cy, float rx, float ry, float thickness, const float color[4]);
void gfx_set_overlay_callback(GfxOverlayCallback cb);
void gfx_end_frame(void);

#endif
