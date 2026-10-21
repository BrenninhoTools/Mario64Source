#include <ultra64.h>
#include <string.h>

#include "gfx_texture.h"

#define CACHE_SIZE 8192
#define CACHE_MAX_LOAD 6000
#define CACHE_EVICT_AGE 120

struct CacheEntry {
    uint64_t hash;
    uint32_t id;
    uint32_t lastUsed;
};

static struct CacheEntry sCache[CACHE_SIZE];
static uint32_t sCacheCount;
static uint32_t sFrame;

static uint64_t mix64(uint64_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

static uint64_t hash_bytes(uint64_t h, const uint8_t *p, size_t n) {
    while (n >= 8) {
        uint64_t k;
        memcpy(&k, p, 8);
        h = mix64(h ^ k) + 0x9e3779b97f4a7c15ULL;
        p += 8;
        n -= 8;
    }
    while (n > 0) {
        h = (h ^ *p) * 0x100000001b3ULL;
        p++;
        n--;
    }
    return h;
}

static uint32_t row_bytes(const struct GfxTextureRequest *req) {
    switch (req->siz) {
        case G_IM_SIZ_4b: return (uint32_t) (req->width + 1) / 2;
        case G_IM_SIZ_8b: return (uint32_t) req->width;
        case G_IM_SIZ_16b: return (uint32_t) req->width * 2;
        default: return (uint32_t) req->width * 4;
    }
}

static uint64_t request_hash(const struct GfxTextureRequest *req) {
    uint64_t h = 0xcbf29ce484222325ULL;
    uint32_t params[6];
    uint32_t rb = row_bytes(req);
    int y;
    params[0] = req->fmt;
    params[1] = req->siz;
    params[2] = (uint32_t) req->width;
    params[3] = (uint32_t) req->height;
    params[4] = req->pitch;
    params[5] = (uint32_t) req->tlutType;
    h = hash_bytes(h, (const uint8_t *) params, sizeof(params));
    for (y = 0; y < req->height; y++) {
        const uint8_t *row = req->data + (size_t) y * req->pitch;
        size_t n = rb;
        if (row >= req->dataLimit) {
            break;
        }
        if (row + n > req->dataLimit) {
            n = (size_t) (req->dataLimit - row);
        }
        h = hash_bytes(h, row, n);
    }
    if (req->fmt == G_IM_FMT_CI) {
        size_t pn = req->siz == G_IM_SIZ_4b ? 32 : 512;
        if (req->palette + pn > req->dataLimit) {
            pn = req->palette < req->dataLimit ? (size_t) (req->dataLimit - req->palette) : 0;
        }
        h = hash_bytes(h, req->palette, pn);
    }
    return mix64(h) | 1;
}

static uint8_t fetch(const struct GfxTextureRequest *req, size_t offset) {
    const uint8_t *p = req->data + offset;
    if (p >= req->dataLimit) {
        return 0;
    }
    return *p;
}

static void put_rgba16(uint8_t *out, uint16_t c) {
    out[0] = (uint8_t) (((c >> 11) & 0x1f) * 255 / 31);
    out[1] = (uint8_t) (((c >> 6) & 0x1f) * 255 / 31);
    out[2] = (uint8_t) (((c >> 1) & 0x1f) * 255 / 31);
    out[3] = (c & 1) ? 255 : 0;
}

static void put_palette(const struct GfxTextureRequest *req, uint8_t *out, uint32_t index) {
    const uint8_t *p = req->palette + index * 2;
    uint16_t c;
    if (p + 1 >= req->dataLimit) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    c = (uint16_t) ((p[0] << 8) | p[1]);
    if (req->tlutType == GFX_TLUT_IA16) {
        out[0] = out[1] = out[2] = (uint8_t) (c >> 8);
        out[3] = (uint8_t) (c & 0xff);
    } else {
        put_rgba16(out, c);
    }
}

void gfx_texture_decode(const struct GfxTextureRequest *req, uint8_t *out) {
    int x, y;
    for (y = 0; y < req->height; y++) {
        size_t rowOff = (size_t) y * req->pitch;
        for (x = 0; x < req->width; x++) {
            uint8_t *o = out + ((size_t) y * req->width + x) * 4;
            switch (req->siz) {
                case G_IM_SIZ_4b: {
                    uint8_t b = fetch(req, rowOff + x / 2);
                    uint8_t n = (x & 1) ? (b & 0xf) : (b >> 4);
                    if (req->fmt == G_IM_FMT_IA) {
                        uint8_t i = (uint8_t) (((n >> 1) & 7) * 255 / 7);
                        o[0] = o[1] = o[2] = i;
                        o[3] = (n & 1) ? 255 : 0;
                    } else if (req->fmt == G_IM_FMT_CI) {
                        put_palette(req, o, n);
                    } else {
                        uint8_t i = (uint8_t) (n * 17);
                        o[0] = o[1] = o[2] = o[3] = i;
                    }
                    break;
                }
                case G_IM_SIZ_8b: {
                    uint8_t b = fetch(req, rowOff + x);
                    if (req->fmt == G_IM_FMT_IA) {
                        uint8_t i = (uint8_t) ((b >> 4) * 17);
                        o[0] = o[1] = o[2] = i;
                        o[3] = (uint8_t) ((b & 0xf) * 17);
                    } else if (req->fmt == G_IM_FMT_CI) {
                        put_palette(req, o, b);
                    } else {
                        o[0] = o[1] = o[2] = o[3] = b;
                    }
                    break;
                }
                case G_IM_SIZ_16b: {
                    uint8_t hi = fetch(req, rowOff + x * 2);
                    uint8_t lo = fetch(req, rowOff + x * 2 + 1);
                    if (req->fmt == G_IM_FMT_IA) {
                        o[0] = o[1] = o[2] = hi;
                        o[3] = lo;
                    } else {
                        put_rgba16(o, (uint16_t) ((hi << 8) | lo));
                    }
                    break;
                }
                default:
                    o[0] = fetch(req, rowOff + x * 4);
                    o[1] = fetch(req, rowOff + x * 4 + 1);
                    o[2] = fetch(req, rowOff + x * 4 + 2);
                    o[3] = fetch(req, rowOff + x * 4 + 3);
                    break;
            }
        }
    }
}

void gfx_texture_cache_init(void) {
    memset(sCache, 0, sizeof(sCache));
    sCacheCount = 0;
    sFrame = 0;
}

void gfx_texture_cache_clear(const struct GfxBackend *backend) {
    uint32_t i;
    for (i = 0; i < CACHE_SIZE; i++) {
        if (sCache[i].hash != 0) {
            backend->texture_delete(sCache[i].id);
        }
    }
    memset(sCache, 0, sizeof(sCache));
    sCacheCount = 0;
}

static void evict_old(const struct GfxBackend *backend) {
    uint32_t i;
    for (i = 0; i < CACHE_SIZE; i++) {
        if (sCache[i].hash != 0 && sFrame - sCache[i].lastUsed > CACHE_EVICT_AGE) {
            backend->texture_delete(sCache[i].id);
            sCache[i].hash = 0;
            sCacheCount--;
        }
    }
    if (sCacheCount > CACHE_MAX_LOAD) {
        gfx_texture_cache_clear(backend);
        return;
    }
    {
        static struct CacheEntry rebuilt[CACHE_SIZE];
        memset(rebuilt, 0, sizeof(rebuilt));
        for (i = 0; i < CACHE_SIZE; i++) {
            if (sCache[i].hash != 0) {
                uint32_t slot = (uint32_t) (sCache[i].hash & (CACHE_SIZE - 1));
                while (rebuilt[slot].hash != 0) {
                    slot = (slot + 1) & (CACHE_SIZE - 1);
                }
                rebuilt[slot] = sCache[i];
            }
        }
        memcpy(sCache, rebuilt, sizeof(sCache));
    }
}

void gfx_texture_cache_frame(void) {
    sFrame++;
}

uint32_t gfx_texture_get(const struct GfxBackend *backend, const struct GfxTextureRequest *req) {
    uint64_t h = request_hash(req);
    uint32_t slot = (uint32_t) (h & (CACHE_SIZE - 1));
    uint32_t id;
    while (sCache[slot].hash != 0) {
        if (sCache[slot].hash == h) {
            sCache[slot].lastUsed = sFrame;
            return sCache[slot].id;
        }
        slot = (slot + 1) & (CACHE_SIZE - 1);
    }
    if (sCacheCount >= CACHE_MAX_LOAD) {
        evict_old(backend);
        slot = (uint32_t) (h & (CACHE_SIZE - 1));
        while (sCache[slot].hash != 0) {
            slot = (slot + 1) & (CACHE_SIZE - 1);
        }
    }
    gfx_texture_decode(req, req->decodeBuffer);
    id = backend->texture_new();
    backend->texture_bind(0, id);
    backend->texture_upload(req->decodeBuffer, req->width, req->height);
    sCache[slot].hash = h;
    sCache[slot].id = id;
    sCache[slot].lastUsed = sFrame;
    sCacheCount++;
    return id;
}
