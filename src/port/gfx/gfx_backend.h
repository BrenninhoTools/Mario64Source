#ifndef PORT_GFX_BACKEND_H
#define PORT_GFX_BACKEND_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define GFX_VTX_FLOATS 13

enum GfxFilter {
    GFX_FILTER_POINT,
    GFX_FILTER_LINEAR
};

enum GfxWrap {
    GFX_WRAP_REPEAT,
    GFX_WRAP_MIRROR,
    GFX_WRAP_CLAMP
};

struct GfxShaderKey {
    uint64_t combine;
    uint32_t flags;
};

#define GFX_SHADER_TWO_CYCLE    (1u << 0)
#define GFX_SHADER_FOG          (1u << 1)
#define GFX_SHADER_ALPHA_TEST   (1u << 2)
#define GFX_SHADER_TEXTURE_EDGE (1u << 3)
#define GFX_SHADER_BLEND        (1u << 4)
#define GFX_SHADER_NOISE        (1u << 5)
#define GFX_SHADER_FILL         (1u << 6)

struct GfxShaderProgram;

struct GfxUniforms {
    float prim[4];
    float env[4];
    float fog[4];
    float primLod;
    float alphaRef;
    float noiseSeed;
};

struct GfxBackend {
    bool (*init)(void);
    void (*start_frame)(int width, int height);
    void (*end_frame)(void);
    struct GfxShaderProgram *(*shader_lookup)(const struct GfxShaderKey *key);
    void (*shader_use)(struct GfxShaderProgram *prog, const struct GfxUniforms *u);
    uint32_t (*texture_new)(void);
    void (*texture_delete)(uint32_t id);
    void (*texture_bind)(int unit, uint32_t id);
    void (*texture_upload)(const uint8_t *rgba, int width, int height);
    void (*texture_sampler)(int unit, enum GfxFilter filter, enum GfxWrap wrapS, enum GfxWrap wrapT);
    void (*set_depth)(bool test, bool write, bool decal);
    void (*set_viewport)(int x, int y, int width, int height);
    void (*set_scissor)(int x, int y, int width, int height);
    void (*set_blend)(bool enabled);
    void (*clear_depth)(void);
    void (*clear_color)(float r, float g, float b);
    void (*draw_triangles)(const float *buf, size_t floatCount, size_t triCount);
};

extern const struct GfxBackend gGfxBackendGL;

#endif
