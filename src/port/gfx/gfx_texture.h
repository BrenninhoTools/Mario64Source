#ifndef PORT_GFX_TEXTURE_H
#define PORT_GFX_TEXTURE_H

#include <stdint.h>
#include "gfx_backend.h"

enum GfxTlutType {
    GFX_TLUT_RGBA16,
    GFX_TLUT_IA16
};

struct GfxTextureRequest {
    const uint8_t *data;
    const uint8_t *dataLimit;
    const uint8_t *palette;
    uint8_t *decodeBuffer;
    uint8_t fmt;
    uint8_t siz;
    int width;
    int height;
    uint32_t pitch;
    enum GfxTlutType tlutType;
};

void gfx_texture_cache_init(void);
void gfx_texture_cache_frame(void);
void gfx_texture_cache_clear(const struct GfxBackend *backend);
uint32_t gfx_texture_get(const struct GfxBackend *backend, const struct GfxTextureRequest *req);
void gfx_texture_decode(const struct GfxTextureRequest *req, uint8_t *out);

#endif
