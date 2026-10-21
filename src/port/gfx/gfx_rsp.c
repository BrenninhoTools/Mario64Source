#include <ultra64.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "gfx_rsp.h"
#include "gfx_backend.h"
#include "gfx_texture.h"

#define MAX_VERTICES 64
#define MAX_LIGHTS 8
#define MTX_STACK_SIZE 32
#define MAX_BATCH_TRIS 1024
#define TMEM_SIZE 4096

#define SCREEN_W ((float) SCREEN_WIDTH)
#define SCREEN_H ((float) SCREEN_HEIGHT)

struct GfxDimensions gfx_current_dimensions = { SCREEN_WIDTH, SCREEN_HEIGHT, 4.0f / 3.0f };

struct LoadedVertex {
    float x, y, z, w;
    float s, t;
    float color[4];
    float fog;
    uint8_t clip;
};

struct TileDesc {
    uint8_t fmt;
    uint8_t siz;
    uint8_t cms;
    uint8_t cmt;
    uint8_t masks;
    uint8_t maskt;
    uint8_t shifts;
    uint8_t shiftt;
    uint8_t palette;
    uint16_t line;
    uint16_t tmem;
    uint16_t uls;
    uint16_t ult;
    uint16_t lrs;
    uint16_t lrt;
};

struct LightState {
    float color[3];
    float dir[3];
};

struct DrawState {
    struct GfxShaderKey key;
    uint32_t tex[2];
    uint8_t filter;
    uint8_t wrap[2][2];
    uint8_t depthTest;
    uint8_t depthWrite;
    uint8_t decal;
    uint8_t blend;
    int viewport[4];
    int scissor[4];
    struct GfxUniforms uniforms;
};

static const struct GfxBackend *sBackend = &gGfxBackendGL;

static float sModelview[MTX_STACK_SIZE][4][4];
static int sModelviewTop;
static float sProjection[4][4];
static float sMP[4][4];
static bool sMPDirty = true;

static struct LoadedVertex sVertices[MAX_VERTICES];

static struct LightState sLights[MAX_LIGHTS];
static int sNumLights;
static float sLookAt[2][3];
static float sModelLights[MAX_LIGHTS][3];
static float sModelLookAt[2][3];
static bool sLightsDirty = true;

static uint32_t sGeometryMode;
static uint32_t sOtherModeL;
static uint32_t sOtherModeH;
static uint64_t sCombine;

static uint16_t sTexScaleS;
static uint16_t sTexScaleT;
static uint8_t sTexTile;
static uint8_t sTexOn;

static int16_t sFogMul;
static int16_t sFogOffset;

static float sPrimColor[4];
static float sEnvColor[4];
static float sFogColor[4];
static float sBlendColor[4];
static uint32_t sFillColor;
static float sPrimLod;

static const uint8_t *sTimgAddr;
static uint8_t sTimgFmt;
static uint8_t sTimgSiz;
static uint16_t sTimgWidth;

static const void *sColorImage;
static const void *sDepthImage;

static struct TileDesc sTiles[8];
static uint8_t sTmem[TMEM_SIZE * 2];
static uint32_t sTileTexture[8];
static bool sTileTextureDirty[8];

static Vp_t sViewport;
static int sViewportRect[4];
static int sScissorRect[4];

static struct DrawState sCurState;
static struct DrawState sPendingState;
static bool sStateDirty = true;
static bool sHaveCurState;

static float sBatch[MAX_BATCH_TRIS * 3 * GFX_VTX_FLOATS];
static size_t sBatchTris;

static uint32_t sNoiseFrame;

static GfxOverlayCallback sOverlayCallback;

static uint8_t sDecodeBuffer[1024 * 1024 * 4];

static void mtx_mul(float out[4][4], float a[4][4], float b[4][4]) {
    float tmp[4][4];
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    memcpy(out, tmp, sizeof(tmp));
}

static float aspect_adjust(void) {
    if (gfx_current_dimensions.aspect_ratio <= 0.0f) {
        return 1.0f;
    }
    return (4.0f / 3.0f) / gfx_current_dimensions.aspect_ratio;
}

static void viewport_to_window(const Vp_t *vp, int out[4]) {
    float w = 2.0f * fabsf((float) vp->vscale[0]) / 4.0f;
    float h = 2.0f * fabsf((float) vp->vscale[1]) / 4.0f;
    float x = (float) vp->vtrans[0] / 4.0f - w / 2.0f;
    float y = SCREEN_H - ((float) vp->vtrans[1] / 4.0f + h / 2.0f);
    float rx = (float) gfx_current_dimensions.width / SCREEN_W;
    float ry = (float) gfx_current_dimensions.height / SCREEN_H;
    out[0] = (int) floorf(x * rx);
    out[1] = (int) floorf(y * ry);
    out[2] = (int) ceilf(w * rx);
    out[3] = (int) ceilf(h * ry);
}

static void flush(void) {
    if (sBatchTris == 0) {
        return;
    }
    sBackend->draw_triangles(sBatch, sBatchTris * 3 * GFX_VTX_FLOATS, sBatchTris);
    sBatchTris = 0;
}

static void mark_dirty(void) {
    sStateDirty = true;
}

static void calc_mp(void) {
    if (sMPDirty) {
        mtx_mul(sMP, sModelview[sModelviewTop], sProjection);
        sMPDirty = false;
    }
}

static void normalize3(float v[3]) {
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.0f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

static void transform_dir_to_model(const float in[3], float out[3]) {
    float (*m)[4] = sModelview[sModelviewTop];
    out[0] = m[0][0] * in[0] + m[0][1] * in[1] + m[0][2] * in[2];
    out[1] = m[1][0] * in[0] + m[1][1] * in[1] + m[1][2] * in[2];
    out[2] = m[2][0] * in[0] + m[2][1] * in[1] + m[2][2] * in[2];
    normalize3(out);
}

static void calc_lights(void) {
    int i;
    if (!sLightsDirty) {
        return;
    }
    for (i = 0; i < sNumLights; i++) {
        transform_dir_to_model(sLights[i].dir, sModelLights[i]);
    }
    transform_dir_to_model(sLookAt[0], sModelLookAt[0]);
    transform_dir_to_model(sLookAt[1], sModelLookAt[1]);
    sLightsDirty = false;
}

static void sp_matrix(uint8_t param, const void *addr) {
    float m[4][4];
    memcpy(m, addr, sizeof(m));
    if (param & G_MTX_PROJECTION) {
        if (param & G_MTX_LOAD) {
            memcpy(sProjection, m, sizeof(m));
        } else {
            mtx_mul(sProjection, m, sProjection);
        }
    } else {
        if ((param & G_MTX_PUSH) && sModelviewTop < MTX_STACK_SIZE - 1) {
            memcpy(sModelview[sModelviewTop + 1], sModelview[sModelviewTop], sizeof(m));
            sModelviewTop++;
        }
        if (param & G_MTX_LOAD) {
            memcpy(sModelview[sModelviewTop], m, sizeof(m));
        } else {
            mtx_mul(sModelview[sModelviewTop], m, sModelview[sModelviewTop]);
        }
        sLightsDirty = true;
    }
    sMPDirty = true;
}

static void sp_pop_matrix(uint32_t count) {
    while (count-- > 0 && sModelviewTop > 0) {
        sModelviewTop--;
    }
    sMPDirty = true;
    sLightsDirty = true;
}

static void sp_vertex(size_t n, size_t dest, const Vtx *verts) {
    size_t i;
    float adj = aspect_adjust();
    calc_mp();
    if (sGeometryMode & G_LIGHTING) {
        calc_lights();
    }
    for (i = 0; i < n && dest + i < MAX_VERTICES; i++) {
        const Vtx_t *v = &verts[i].v;
        const Vtx_tn *vn = &verts[i].n;
        struct LoadedVertex *d = &sVertices[dest + i];
        float x = v->ob[0];
        float y = v->ob[1];
        float z = v->ob[2];
        float cx = x * sMP[0][0] + y * sMP[1][0] + z * sMP[2][0] + sMP[3][0];
        float cy = x * sMP[0][1] + y * sMP[1][1] + z * sMP[2][1] + sMP[3][1];
        float cz = x * sMP[0][2] + y * sMP[1][2] + z * sMP[2][2] + sMP[3][2];
        float cw = x * sMP[0][3] + y * sMP[1][3] + z * sMP[2][3] + sMP[3][3];
        cx *= adj;
        d->x = cx;
        d->y = cy;
        d->z = cz;
        d->w = cw;

        if (sGeometryMode & G_LIGHTING) {
            float nx = vn->n[0] / 127.0f;
            float ny = vn->n[1] / 127.0f;
            float nz = vn->n[2] / 127.0f;
            float r = sLights[sNumLights].color[0];
            float g = sLights[sNumLights].color[1];
            float b = sLights[sNumLights].color[2];
            int l;
            for (l = 0; l < sNumLights; l++) {
                float intensity = nx * sModelLights[l][0] + ny * sModelLights[l][1] + nz * sModelLights[l][2];
                if (intensity > 0.0f) {
                    r += intensity * sLights[l].color[0];
                    g += intensity * sLights[l].color[1];
                    b += intensity * sLights[l].color[2];
                }
            }
            d->color[0] = r > 1.0f ? 1.0f : r;
            d->color[1] = g > 1.0f ? 1.0f : g;
            d->color[2] = b > 1.0f ? 1.0f : b;
            if (sGeometryMode & G_TEXTURE_GEN) {
                float dx = nx * sModelLookAt[0][0] + ny * sModelLookAt[0][1] + nz * sModelLookAt[0][2];
                float dy = nx * sModelLookAt[1][0] + ny * sModelLookAt[1][1] + nz * sModelLookAt[1][2];
                if (sGeometryMode & G_TEXTURE_GEN_LINEAR) {
                    dx = acosf(-dx) / (float) M_PI;
                    dy = acosf(-dy) / (float) M_PI;
                    d->s = dx * (float) sTexScaleS / 2.0f;
                    d->t = dy * (float) sTexScaleT / 2.0f;
                } else {
                    d->s = (dx + 1.0f) / 4.0f * (float) sTexScaleS;
                    d->t = (dy + 1.0f) / 4.0f * (float) sTexScaleT;
                }
            } else {
                d->s = (float) v->tc[0] * (float) sTexScaleS / 65536.0f;
                d->t = (float) v->tc[1] * (float) sTexScaleT / 65536.0f;
            }
        } else {
            d->color[0] = v->cn[0] / 255.0f;
            d->color[1] = v->cn[1] / 255.0f;
            d->color[2] = v->cn[2] / 255.0f;
            d->s = (float) v->tc[0] * (float) sTexScaleS / 65536.0f;
            d->t = (float) v->tc[1] * (float) sTexScaleT / 65536.0f;
        }
        d->color[3] = v->cn[3] / 255.0f;

        d->clip = 0;
        if (cx < -cw) {
            d->clip |= 1;
        }
        if (cx > cw) {
            d->clip |= 2;
        }
        if (cy < -cw) {
            d->clip |= 4;
        }
        if (cy > cw) {
            d->clip |= 8;
        }
        if (cz < -cw) {
            d->clip |= 16;
        }
        if (cz > cw) {
            d->clip |= 32;
        }

        if (sGeometryMode & G_FOG) {
            float w = fabsf(cw) < 0.001f ? 0.001f : cw;
            float winv = 1.0f / w;
            float f;
            if (winv < 0.0f) {
                winv = 32767.0f;
            }
            f = cz * winv * (float) sFogMul + (float) sFogOffset;
            if (f < 0.0f) {
                f = 0.0f;
            }
            if (f > 255.0f) {
                f = 255.0f;
            }
            d->fog = f / 255.0f;
        } else {
            d->fog = 0.0f;
        }
    }
}

static uint64_t make_combine(uint32_t a0, uint32_t b0, uint32_t c0, uint32_t d0, uint32_t aa0, uint32_t ab0,
                             uint32_t ac0, uint32_t ad0, uint32_t a1, uint32_t b1, uint32_t c1, uint32_t d1,
                             uint32_t aa1, uint32_t ab1, uint32_t ac1, uint32_t ad1) {
    uint32_t w0 = (a0 << 20) | (c0 << 15) | (aa0 << 12) | (ac0 << 9) | (a1 << 5) | c1;
    uint32_t w1 = (b0 << 28) | (b1 << 24) | (aa1 << 21) | (ac1 << 18) | (d0 << 15) | (ab0 << 12) | (ad0 << 9)
                  | (d1 << 6) | (ab1 << 3) | ad1;
    return ((uint64_t) w0 << 32) | w1;
}

static int combine_sel(int cycle, int slot) {
    uint32_t w0 = (uint32_t) (sCombine >> 32);
    uint32_t w1 = (uint32_t) sCombine;
    if (cycle == 0) {
        switch (slot) {
            case 0: return (w0 >> 20) & 0xf;
            case 1: return (w1 >> 28) & 0xf;
            case 2: return (w0 >> 15) & 0x1f;
            case 3: return (w1 >> 15) & 0x7;
            case 4: return (w0 >> 12) & 0x7;
            case 5: return (w1 >> 12) & 0x7;
            case 6: return (w0 >> 9) & 0x7;
            default: return (w1 >> 9) & 0x7;
        }
    }
    switch (slot) {
        case 0: return (w0 >> 5) & 0xf;
        case 1: return (w1 >> 24) & 0xf;
        case 2: return w0 & 0x1f;
        case 3: return (w1 >> 6) & 0x7;
        case 4: return (w1 >> 21) & 0x7;
        case 5: return (w1 >> 3) & 0x7;
        case 6: return (w1 >> 18) & 0x7;
        default: return w1 & 0x7;
    }
}

static bool cycle_uses_texel(int cycle, int texel) {
    int t = texel == 0 ? G_CCMUX_TEXEL0 : G_CCMUX_TEXEL1;
    int ta = texel == 0 ? G_CCMUX_TEXEL0_ALPHA : G_CCMUX_TEXEL1_ALPHA;
    int at = texel == 0 ? G_ACMUX_TEXEL0 : G_ACMUX_TEXEL1;
    int a = combine_sel(cycle, 0);
    int b = combine_sel(cycle, 1);
    int c = combine_sel(cycle, 2);
    int d = combine_sel(cycle, 3);
    if (a == t || b == t || c == t || c == ta || d == t) {
        return true;
    }
    if (combine_sel(cycle, 4) == at || combine_sel(cycle, 5) == at || combine_sel(cycle, 6) == at
        || combine_sel(cycle, 7) == at) {
        return true;
    }
    return false;
}

static bool uses_texel(int texel) {
    uint32_t cycle = sOtherModeH & (3 << G_MDSFT_CYCLETYPE);
    if (cycle == G_CYC_COPY) {
        return texel == 0;
    }
    if (cycle == G_CYC_FILL) {
        return false;
    }
    if (cycle_uses_texel(0, texel)) {
        return true;
    }
    if (cycle == G_CYC_2CYCLE && cycle_uses_texel(1, texel)) {
        return true;
    }
    return false;
}

static uint32_t tile_texture(int tile) {
    struct TileDesc *t = &sTiles[tile & 7];
    uint32_t tlut = sOtherModeH & (3 << G_MDSFT_TEXTLUT);
    int width = ((t->lrs - t->uls) >> 2) + 1;
    int height = ((t->lrt - t->ult) >> 2) + 1;
    uint32_t pitch;
    uint32_t tmemOffset = (uint32_t) t->tmem * 8;
    struct GfxTextureRequest req;

    if (!sTileTextureDirty[tile & 7] && sTileTexture[tile & 7] != 0) {
        return sTileTexture[tile & 7];
    }
    if (t->masks != 0 && (1 << t->masks) < width) {
        width = 1 << t->masks;
    }
    if (t->maskt != 0 && (1 << t->maskt) < height) {
        height = 1 << t->maskt;
    }
    if (width <= 0) {
        width = 1;
    }
    if (height <= 0) {
        height = 1;
    }
    pitch = (uint32_t) t->line * 8;
    if (t->siz == G_IM_SIZ_32b) {
        pitch *= 2;
    }
    if (pitch == 0) {
        switch (t->siz) {
            case G_IM_SIZ_4b: pitch = (width + 1) / 2; break;
            case G_IM_SIZ_8b: pitch = width; break;
            case G_IM_SIZ_16b: pitch = width * 2; break;
            default: pitch = width * 4; break;
        }
    }

    req.data = sTmem + (tmemOffset & (TMEM_SIZE - 1));
    req.dataLimit = sTmem + TMEM_SIZE * 2;
    req.fmt = t->fmt;
    req.siz = t->siz;
    req.width = width;
    req.height = height;
    req.pitch = pitch;
    req.tlutType = tlut == G_TT_IA16 ? GFX_TLUT_IA16 : GFX_TLUT_RGBA16;
    req.palette = sTmem + 2048 + (t->siz == G_IM_SIZ_4b ? t->palette * 32 : 0);
    req.decodeBuffer = sDecodeBuffer;

    sTileTexture[tile & 7] = gfx_texture_get(sBackend, &req);
    sTileTextureDirty[tile & 7] = false;
    return sTileTexture[tile & 7];
}

static enum GfxWrap wrap_mode(uint8_t cm) {
    if (cm & G_TX_CLAMP) {
        return GFX_WRAP_CLAMP;
    }
    if (cm & G_TX_MIRROR) {
        return GFX_WRAP_MIRROR;
    }
    return GFX_WRAP_REPEAT;
}

static void build_state(struct DrawState *s, bool rect) {
    uint32_t cycle = sOtherModeH & (3 << G_MDSFT_CYCLETYPE);
    bool zcmp = (sOtherModeL & Z_CMP) != 0;
    bool zupd = (sOtherModeL & Z_UPD) != 0;
    int i;

    memset(s, 0, sizeof(*s));
    s->key.combine = sCombine;
    if (cycle == G_CYC_2CYCLE) {
        s->key.flags |= GFX_SHADER_TWO_CYCLE;
    }
    if (cycle == G_CYC_FILL) {
        s->key.flags |= GFX_SHADER_FILL;
        s->key.combine = 0;
    }
    if (cycle == G_CYC_COPY) {
        s->key.combine = make_combine(15, 15, 31, G_CCMUX_TEXEL0, 7, 7, 7, G_ACMUX_TEXEL0, 15, 15, 31,
                                      G_CCMUX_TEXEL0, 7, 7, 7, G_ACMUX_TEXEL0);
        s->key.flags |= GFX_SHADER_ALPHA_TEST;
    }
    if ((sOtherModeL >> 30) == G_BL_CLR_FOG && (sGeometryMode & G_FOG)) {
        s->key.flags |= GFX_SHADER_FOG;
    }
    if ((sOtherModeL & (G_BL_A_MEM << 18)) == 0 && cycle != G_CYC_FILL && cycle != G_CYC_COPY) {
        s->key.flags |= GFX_SHADER_BLEND;
        s->blend = 1;
    }
    if ((sOtherModeL & CVG_X_ALPHA) && cycle != G_CYC_FILL) {
        s->key.flags |= GFX_SHADER_TEXTURE_EDGE;
    }
    if ((sOtherModeL & (3 << G_MDSFT_ALPHACOMPARE)) == G_AC_THRESHOLD && cycle != G_CYC_FILL) {
        s->key.flags |= GFX_SHADER_ALPHA_TEST;
    }
    if ((sOtherModeL & (3 << G_MDSFT_ALPHACOMPARE)) == G_AC_DITHER) {
        s->key.flags |= GFX_SHADER_NOISE;
    }

    for (i = 0; i < 2; i++) {
        if (cycle != G_CYC_FILL && uses_texel(i)) {
            int tile = (sTexTile + i) & 7;
            s->tex[i] = tile_texture(tile);
            s->wrap[i][0] = (uint8_t) wrap_mode(sTiles[tile].cms);
            s->wrap[i][1] = (uint8_t) wrap_mode(sTiles[tile].cmt);
        }
    }
    s->filter = (uint8_t) (((sOtherModeH & (3 << G_MDSFT_TEXTFILT)) == G_TF_POINT || cycle == G_CYC_COPY)
                               ? GFX_FILTER_POINT
                               : GFX_FILTER_LINEAR);

    if (!rect) {
        s->depthTest = (sGeometryMode & G_ZBUFFER) && zcmp;
        s->depthWrite = (sGeometryMode & G_ZBUFFER) && zupd;
        s->decal = (sOtherModeL & ZMODE_DEC) == ZMODE_DEC;
        memcpy(s->viewport, sViewportRect, sizeof(s->viewport));
    } else {
        s->viewport[0] = 0;
        s->viewport[1] = 0;
        s->viewport[2] = (int) gfx_current_dimensions.width;
        s->viewport[3] = (int) gfx_current_dimensions.height;
    }
    memcpy(s->scissor, sScissorRect, sizeof(s->scissor));

    memcpy(s->uniforms.prim, sPrimColor, sizeof(sPrimColor));
    memcpy(s->uniforms.env, sEnvColor, sizeof(sEnvColor));
    memcpy(s->uniforms.fog, sFogColor, sizeof(sFogColor));
    s->uniforms.primLod = sPrimLod;
    s->uniforms.alphaRef = (s->key.flags & GFX_SHADER_TEXTURE_EDGE) ? 0.3f : sBlendColor[3];
    if (cycle == G_CYC_COPY) {
        s->uniforms.alphaRef = 0.5f;
    }
    if (cycle == G_CYC_FILL) {
        s->uniforms.prim[0] = (float) ((sFillColor >> 27) & 0x1f) / 31.0f;
        s->uniforms.prim[1] = (float) ((sFillColor >> 22) & 0x1f) / 31.0f;
        s->uniforms.prim[2] = (float) ((sFillColor >> 17) & 0x1f) / 31.0f;
        s->uniforms.prim[3] = 1.0f;
    }
    if (s->key.flags & GFX_SHADER_NOISE) {
        s->uniforms.noiseSeed = (float) (sNoiseFrame % 1024);
    }
}

static void apply_state(const struct DrawState *s) {
    struct GfxShaderProgram *prog;
    int i;
    bool first = !sHaveCurState;

    if (first || memcmp(&s->key, &sCurState.key, sizeof(s->key)) != 0
        || memcmp(&s->uniforms, &sCurState.uniforms, sizeof(s->uniforms)) != 0) {
        prog = sBackend->shader_lookup(&s->key);
        sBackend->shader_use(prog, &s->uniforms);
    }
    for (i = 0; i < 2; i++) {
        if (s->tex[i] != 0) {
            sBackend->texture_bind(i, s->tex[i]);
            sBackend->texture_sampler(i, (enum GfxFilter) s->filter, (enum GfxWrap) s->wrap[i][0],
                                      (enum GfxWrap) s->wrap[i][1]);
        }
    }
    if (first || s->depthTest != sCurState.depthTest || s->depthWrite != sCurState.depthWrite
        || s->decal != sCurState.decal) {
        sBackend->set_depth(s->depthTest, s->depthWrite, s->decal);
    }
    if (first || s->blend != sCurState.blend) {
        sBackend->set_blend(s->blend);
    }
    if (first || memcmp(s->viewport, sCurState.viewport, sizeof(s->viewport)) != 0) {
        sBackend->set_viewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    }
    if (first || memcmp(s->scissor, sCurState.scissor, sizeof(s->scissor)) != 0) {
        sBackend->set_scissor(s->scissor[0], s->scissor[1], s->scissor[2], s->scissor[3]);
    }
    sCurState = *s;
    sHaveCurState = true;
}

static void prepare_draw(bool rect) {
    if (!sStateDirty && !rect && sHaveCurState) {
        return;
    }
    flush();
    build_state(&sPendingState, rect);
    apply_state(&sPendingState);
    sStateDirty = rect;
}

static float shift_coord(float c, uint8_t shift) {
    if (shift == 0) {
        return c;
    }
    if (shift <= 10) {
        return c / (float) (1 << shift);
    }
    return c * (float) (1 << (16 - shift));
}

static void tile_uv(int tile, float s, float t, float *u, float *v) {
    struct TileDesc *td = &sTiles[tile & 7];
    float width = (float) (((td->lrs - td->uls) >> 2) + 1);
    float height = (float) (((td->lrt - td->ult) >> 2) + 1);
    if (td->masks != 0 && (float) (1 << td->masks) < width) {
        width = (float) (1 << td->masks);
    }
    if (td->maskt != 0 && (float) (1 << td->maskt) < height) {
        height = (float) (1 << td->maskt);
    }
    if (width <= 0.0f) {
        width = 1.0f;
    }
    if (height <= 0.0f) {
        height = 1.0f;
    }
    *u = (shift_coord(s / 32.0f, td->shifts) - (float) td->uls / 4.0f) / width;
    *v = (shift_coord(t / 32.0f, td->shiftt) - (float) td->ult / 4.0f) / height;
}

static void emit_vertex(float *out, float x, float y, float z, float w, float s, float t, const float color[4],
                        float fog) {
    tile_uv(sTexTile, s, t, &out[4], &out[5]);
    tile_uv(sTexTile + 1, s, t, &out[6], &out[7]);
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
    out[8] = color[0];
    out[9] = color[1];
    out[10] = color[2];
    out[11] = color[3];
    out[12] = fog;
}

static bool cull_triangle(const struct LoadedVertex *a, const struct LoadedVertex *b, const struct LoadedVertex *c) {
    float dx1, dy1, dx2, dy2, cross;
    uint32_t cull = sGeometryMode & G_CULL_BOTH;
    if (a->clip & b->clip & c->clip) {
        return true;
    }
    if (cull == 0) {
        return false;
    }
    if (a->w == 0.0f || b->w == 0.0f || c->w == 0.0f) {
        return false;
    }
    dx1 = a->x / a->w - b->x / b->w;
    dy1 = a->y / a->w - b->y / b->w;
    dx2 = c->x / c->w - b->x / b->w;
    dy2 = c->y / c->w - b->y / b->w;
    cross = dx1 * dy2 - dy1 * dx2;
    if ((a->w < 0) ^ (b->w < 0) ^ (c->w < 0)) {
        cross = -cross;
    }
    switch (cull) {
        case G_CULL_FRONT:
            return cross <= 0.0f;
        case G_CULL_BACK:
            return cross >= 0.0f;
        default:
            return true;
    }
}

static void sp_tri(uint8_t i0, uint8_t i1, uint8_t i2) {
    struct LoadedVertex *v[3];
    float *out;
    int i;
    if (i0 >= MAX_VERTICES || i1 >= MAX_VERTICES || i2 >= MAX_VERTICES) {
        return;
    }
    v[0] = &sVertices[i0];
    v[1] = &sVertices[i1];
    v[2] = &sVertices[i2];
    if (cull_triangle(v[0], v[1], v[2])) {
        return;
    }
    prepare_draw(false);
    if (sBatchTris >= MAX_BATCH_TRIS) {
        flush();
    }
    out = &sBatch[sBatchTris * 3 * GFX_VTX_FLOATS];
    for (i = 0; i < 3; i++) {
        float color[4];
        memcpy(color, v[i]->color, sizeof(color));
        if (!(sGeometryMode & G_SHADING_SMOOTH)) {
            memcpy(color, v[0]->color, sizeof(float) * 3);
        }
        emit_vertex(out + i * GFX_VTX_FLOATS, v[i]->x, v[i]->y, v[i]->z, v[i]->w, v[i]->s, v[i]->t, color,
                    v[i]->fog);
    }
    sBatchTris++;
}

static void draw_rect(float ulx, float uly, float lrx, float lry, float s0, float t0, float s1, float t1, bool flip) {
    float adj = aspect_adjust();
    float x0 = (ulx / (SCREEN_W * 0.5f) - 1.0f) * adj;
    float x1 = (lrx / (SCREEN_W * 0.5f) - 1.0f) * adj;
    float y0 = 1.0f - uly / (SCREEN_H * 0.5f);
    float y1 = 1.0f - lry / (SCREEN_H * 0.5f);
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float *out;
    float px[4] = { x0, x1, x0, x1 };
    float py[4] = { y0, y0, y1, y1 };
    float ps[4];
    float pt[4];
    int order[6] = { 0, 2, 1, 1, 2, 3 };
    int i;
    uint32_t savedGeom = sGeometryMode;

    if (!flip) {
        ps[0] = s0; pt[0] = t0;
        ps[1] = s1; pt[1] = t0;
        ps[2] = s0; pt[2] = t1;
        ps[3] = s1; pt[3] = t1;
    } else {
        ps[0] = s0; pt[0] = t0;
        ps[1] = s0; pt[1] = t1;
        ps[2] = s1; pt[2] = t0;
        ps[3] = s1; pt[3] = t1;
    }

    sGeometryMode &= ~G_ZBUFFER;
    sStateDirty = true;
    prepare_draw(true);
    if (sBatchTris + 2 > MAX_BATCH_TRIS) {
        flush();
    }
    out = &sBatch[sBatchTris * 3 * GFX_VTX_FLOATS];
    for (i = 0; i < 6; i++) {
        int k = order[i];
        emit_vertex(out + i * GFX_VTX_FLOATS, px[k], py[k], -1.0f, 1.0f, ps[k], pt[k], white, 0.0f);
    }
    sBatchTris += 2;
    flush();
    sGeometryMode = savedGeom;
    sStateDirty = true;
}

static int32_t sign_extend(uint32_t value, int bits) {
    uint32_t m = 1u << (bits - 1);
    value &= (1u << bits) - 1;
    return (int32_t) ((value ^ m) - m);
}

static void dp_fill_rect(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    uint32_t cycle = sOtherModeH & (3 << G_MDSFT_CYCLETYPE);
    float fulx = (float) ulx;
    float fuly = (float) uly;
    float flrx = (float) lrx;
    float flry = (float) lry;
    if (sColorImage != NULL && sColorImage == sDepthImage) {
        flush();
        sBackend->clear_depth();
        return;
    }
    if (cycle == G_CYC_FILL || cycle == G_CYC_COPY) {
        flrx += 1.0f;
        flry += 1.0f;
    }
    draw_rect(fulx, fuly, flrx, flry, 0.0f, 0.0f, 0.0f, 0.0f, false);
}

static void dp_tex_rect(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry, uint8_t tile, int16_t s, int16_t t,
                        int16_t dsdx, int16_t dtdy, bool flip) {
    uint32_t cycle = sOtherModeH & (3 << G_MDSFT_CYCLETYPE);
    float fulx = (float) ulx / 4.0f;
    float fuly = (float) uly / 4.0f;
    float flrx = (float) lrx / 4.0f;
    float flry = (float) lry / 4.0f;
    float fdsdx = (float) dsdx / 1024.0f;
    float fdtdy = (float) dtdy / 1024.0f;
    float s0 = (float) s;
    float t0 = (float) t;
    float s1, t1;
    uint8_t savedTile = sTexTile;
    if (cycle == G_CYC_COPY) {
        fdsdx /= 4.0f;
        flrx += 1.0f;
        flry += 1.0f;
    }
    if (!flip) {
        s1 = s0 + fdsdx * (flrx - fulx) * 32.0f;
        t1 = t0 + fdtdy * (flry - fuly) * 32.0f;
    } else {
        s1 = s0 + fdsdx * (flry - fuly) * 32.0f;
        t1 = t0 + fdtdy * (flrx - fulx) * 32.0f;
    }
    if (sTexTile != tile) {
        sTexTile = tile;
        sStateDirty = true;
    }
    draw_rect(fulx, fuly, flrx, flry, s0, t0, s1, t1, flip);
    if (sTexTile != savedTile) {
        sTexTile = savedTile;
        sStateDirty = true;
    }
}

static void invalidate_tiles(void) {
    int i;
    for (i = 0; i < 8; i++) {
        sTileTextureDirty[i] = true;
    }
    sStateDirty = true;
}

static void dp_load_block(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs) {
    uint32_t texels = lrs - uls + 1;
    uint32_t bytes;
    uint32_t dst = (uint32_t) sTiles[tile & 7].tmem * 8;
    (void) ult;
    switch (sTimgSiz) {
        case G_IM_SIZ_4b: bytes = (texels + 1) / 2; break;
        case G_IM_SIZ_8b: bytes = texels; break;
        case G_IM_SIZ_16b: bytes = texels * 2; break;
        default: bytes = texels * 4; break;
    }
    if (sTimgAddr == NULL) {
        return;
    }
    if (dst >= TMEM_SIZE) {
        dst &= TMEM_SIZE - 1;
    }
    if (dst + bytes > sizeof(sTmem)) {
        bytes = (uint32_t) sizeof(sTmem) - dst;
    }
    memcpy(sTmem + dst, sTimgAddr, bytes);
    invalidate_tiles();
}

static void dp_load_tile(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    struct TileDesc *td = &sTiles[tile & 7];
    uint32_t x0 = uls >> 2;
    uint32_t y0 = ult >> 2;
    uint32_t x1 = lrs >> 2;
    uint32_t y1 = lrt >> 2;
    uint32_t bpp4;
    uint32_t srcPitch;
    uint32_t rowBytes;
    uint32_t dstPitch = (uint32_t) td->line * 8;
    uint32_t dst = (uint32_t) td->tmem * 8;
    uint32_t y;
    if (sTimgAddr == NULL) {
        return;
    }
    switch (sTimgSiz) {
        case G_IM_SIZ_4b: bpp4 = 1; break;
        case G_IM_SIZ_8b: bpp4 = 2; break;
        case G_IM_SIZ_16b: bpp4 = 4; break;
        default: bpp4 = 8; break;
    }
    srcPitch = (uint32_t) sTimgWidth * bpp4 / 2;
    rowBytes = (x1 - x0 + 1) * bpp4 / 2;
    if (sTimgSiz == G_IM_SIZ_32b) {
        dstPitch *= 2;
    }
    if (dstPitch == 0) {
        dstPitch = rowBytes;
    }
    for (y = y0; y <= y1; y++) {
        uint32_t d = dst + (y - y0) * dstPitch;
        if (d + rowBytes > sizeof(sTmem)) {
            break;
        }
        memcpy(sTmem + d, sTimgAddr + y * srcPitch + x0 * bpp4 / 2, rowBytes);
    }
    td->uls = (uint16_t) uls;
    td->ult = (uint16_t) ult;
    td->lrs = (uint16_t) lrs;
    td->lrt = (uint16_t) lrt;
    invalidate_tiles();
}

static void dp_load_tlut(uint8_t tile, uint32_t count) {
    uint32_t dst = (uint32_t) sTiles[tile & 7].tmem * 8;
    uint32_t bytes = (count + 1) * 2;
    if (sTimgAddr == NULL) {
        return;
    }
    if (dst + bytes > sizeof(sTmem)) {
        bytes = (uint32_t) sizeof(sTmem) - dst;
    }
    memcpy(sTmem + dst, sTimgAddr, bytes);
    invalidate_tiles();
}

static void color_from_rgba(float out[4], uint32_t c) {
    out[0] = (float) ((c >> 24) & 0xff) / 255.0f;
    out[1] = (float) ((c >> 16) & 0xff) / 255.0f;
    out[2] = (float) ((c >> 8) & 0xff) / 255.0f;
    out[3] = (float) (c & 0xff) / 255.0f;
}

static void sp_movemem(uint8_t index, uint32_t offset, const void *data) {
    switch (index) {
        case G_MV_VIEWPORT:
            memcpy(&sViewport, data, sizeof(sViewport));
            viewport_to_window(&sViewport, sViewportRect);
            mark_dirty();
            break;
        case G_MV_LIGHT: {
            int slot = (int) (offset / 24);
            const Light_t *l = (const Light_t *) data;
            if (slot < 2) {
                sLookAt[slot][0] = (float) (int8_t) l->dir[0] / 127.0f;
                sLookAt[slot][1] = (float) (int8_t) l->dir[1] / 127.0f;
                sLookAt[slot][2] = (float) (int8_t) l->dir[2] / 127.0f;
            } else {
                int idx = slot - 2;
                if (idx >= 0 && idx < MAX_LIGHTS) {
                    sLights[idx].color[0] = l->col[0] / 255.0f;
                    sLights[idx].color[1] = l->col[1] / 255.0f;
                    sLights[idx].color[2] = l->col[2] / 255.0f;
                    sLights[idx].dir[0] = (float) (int8_t) l->dir[0] / 127.0f;
                    sLights[idx].dir[1] = (float) (int8_t) l->dir[1] / 127.0f;
                    sLights[idx].dir[2] = (float) (int8_t) l->dir[2] / 127.0f;
                }
            }
            sLightsDirty = true;
            break;
        }
        default:
            break;
    }
}

static void sp_moveword(uint8_t index, uint16_t offset, uintptr_t data) {
    (void) offset;
    switch (index) {
        case G_MW_NUMLIGHT:
            sNumLights = (int) ((uint32_t) data / 24);
            if (sNumLights >= MAX_LIGHTS) {
                sNumLights = MAX_LIGHTS - 1;
            }
            sLightsDirty = true;
            break;
        case G_MW_FOG:
            sFogMul = (int16_t) ((uint32_t) data >> 16);
            sFogOffset = (int16_t) ((uint32_t) data & 0xffff);
            break;
        default:
            break;
    }
}

static void run_dl(const Gfx *cmd, int depth) {
    if (depth > 64) {
        return;
    }
    for (;;) {
        uint32_t w0 = (uint32_t) cmd->words.w0;
        uintptr_t w1 = cmd->words.w1;
        uint8_t op = (uint8_t) (w0 >> 24);
        switch (op) {
            case G_NOOP:
            case G_SPNOOP:
            case G_RDPLOADSYNC:
            case G_RDPPIPESYNC:
            case G_RDPTILESYNC:
            case G_RDPFULLSYNC:
            case G_RDPHALF_1:
            case G_RDPHALF_2:
                break;
            case G_MTX:
                flush();
                sp_matrix((uint8_t) ((w0 & 0xff) ^ G_MTX_PUSH), (const void *) w1);
                break;
            case G_POPMTX:
                flush();
                sp_pop_matrix((uint32_t) w1 / 64);
                break;
            case G_VTX: {
                uint32_t n = (w0 >> 12) & 0xff;
                uint32_t end = (w0 >> 1) & 0x7f;
                sp_vertex(n, end - n, (const Vtx *) w1);
                break;
            }
            case G_MODIFYVTX:
                break;
            case G_CULLDL: {
                uint32_t v0 = (w0 & 0xffff) / 2;
                uint32_t vn = ((uint32_t) w1 & 0xffff) / 2;
                uint8_t mask = 0x3f;
                uint32_t i;
                if (v0 < MAX_VERTICES && vn < MAX_VERTICES) {
                    for (i = v0; i <= vn; i++) {
                        mask &= sVertices[i].clip;
                    }
                    if (mask != 0) {
                        return;
                    }
                }
                break;
            }
            case G_TRI1:
                sp_tri((uint8_t) (((w0 >> 16) & 0xff) / 2), (uint8_t) (((w0 >> 8) & 0xff) / 2),
                       (uint8_t) ((w0 & 0xff) / 2));
                break;
            case G_TRI2:
                sp_tri((uint8_t) (((w0 >> 16) & 0xff) / 2), (uint8_t) (((w0 >> 8) & 0xff) / 2),
                       (uint8_t) ((w0 & 0xff) / 2));
                sp_tri((uint8_t) ((((uint32_t) w1 >> 16) & 0xff) / 2), (uint8_t) ((((uint32_t) w1 >> 8) & 0xff) / 2),
                       (uint8_t) (((uint32_t) w1 & 0xff) / 2));
                break;
            case G_QUAD:
                sp_tri((uint8_t) (((w0 >> 16) & 0xff) / 2), (uint8_t) (((w0 >> 8) & 0xff) / 2),
                       (uint8_t) ((w0 & 0xff) / 2));
                sp_tri((uint8_t) ((((uint32_t) w1 >> 16) & 0xff) / 2), (uint8_t) ((((uint32_t) w1 >> 8) & 0xff) / 2),
                       (uint8_t) (((uint32_t) w1 & 0xff) / 2));
                break;
            case G_MOVEMEM:
                flush();
                sp_movemem((uint8_t) (w0 & 0xff), ((w0 >> 8) & 0xff) * 8, (const void *) w1);
                break;
            case G_MOVEWORD:
                flush();
                sp_moveword((uint8_t) ((w0 >> 16) & 0xff), (uint16_t) (w0 & 0xffff), w1);
                break;
            case G_TEXTURE:
                sTexScaleS = (uint16_t) ((uint32_t) w1 >> 16);
                sTexScaleT = (uint16_t) ((uint32_t) w1 & 0xffff);
                sTexTile = (uint8_t) ((w0 >> 8) & 7);
                sTexOn = (uint8_t) ((w0 >> 1) & 0x7f);
                mark_dirty();
                break;
            case G_GEOMETRYMODE:
                sGeometryMode = (sGeometryMode & (w0 & 0xffffff)) | (uint32_t) w1;
                mark_dirty();
                break;
            case G_SETOTHERMODE_L:
            case G_SETOTHERMODE_H: {
                uint32_t len = (w0 & 0xff) + 1;
                uint32_t shift = 32 - ((w0 >> 8) & 0xff) - len;
                uint32_t mask = (len >= 32 ? 0xffffffffu : ((1u << len) - 1)) << shift;
                if (op == G_SETOTHERMODE_L) {
                    sOtherModeL = (sOtherModeL & ~mask) | ((uint32_t) w1 & mask);
                } else {
                    sOtherModeH = (sOtherModeH & ~mask) | ((uint32_t) w1 & mask);
                    invalidate_tiles();
                }
                mark_dirty();
                break;
            }
            case G_RDPSETOTHERMODE:
                sOtherModeH = (w0 & 0xffffff) | (sOtherModeH & 0xff000000);
                sOtherModeL = (uint32_t) w1;
                invalidate_tiles();
                mark_dirty();
                break;
            case G_DL:
                if (((w0 >> 16) & 0xff) == G_DL_PUSH) {
                    run_dl((const Gfx *) w1, depth + 1);
                } else {
                    cmd = (const Gfx *) w1;
                    continue;
                }
                break;
            case G_ENDDL:
                return;
            case G_SETCIMG:
                flush();
                sColorImage = (const void *) w1;
                break;
            case G_SETZIMG:
                flush();
                sDepthImage = (const void *) w1;
                break;
            case G_SETTIMG:
                sTimgAddr = (const uint8_t *) w1;
                sTimgFmt = (uint8_t) ((w0 >> 21) & 7);
                sTimgSiz = (uint8_t) ((w0 >> 19) & 3);
                sTimgWidth = (uint16_t) ((w0 & 0xfff) + 1);
                break;
            case G_SETTILE: {
                struct TileDesc *t = &sTiles[((uint32_t) w1 >> 24) & 7];
                t->fmt = (uint8_t) ((w0 >> 21) & 7);
                t->siz = (uint8_t) ((w0 >> 19) & 3);
                t->line = (uint16_t) ((w0 >> 9) & 0x1ff);
                t->tmem = (uint16_t) (w0 & 0x1ff);
                t->palette = (uint8_t) (((uint32_t) w1 >> 20) & 0xf);
                t->cmt = (uint8_t) (((uint32_t) w1 >> 18) & 3);
                t->maskt = (uint8_t) (((uint32_t) w1 >> 14) & 0xf);
                t->shiftt = (uint8_t) (((uint32_t) w1 >> 10) & 0xf);
                t->cms = (uint8_t) (((uint32_t) w1 >> 8) & 3);
                t->masks = (uint8_t) (((uint32_t) w1 >> 4) & 0xf);
                t->shifts = (uint8_t) ((uint32_t) w1 & 0xf);
                invalidate_tiles();
                break;
            }
            case G_SETTILESIZE: {
                struct TileDesc *t = &sTiles[((uint32_t) w1 >> 24) & 7];
                t->uls = (uint16_t) ((w0 >> 12) & 0xfff);
                t->ult = (uint16_t) (w0 & 0xfff);
                t->lrs = (uint16_t) (((uint32_t) w1 >> 12) & 0xfff);
                t->lrt = (uint16_t) ((uint32_t) w1 & 0xfff);
                invalidate_tiles();
                break;
            }
            case G_LOADBLOCK:
                dp_load_block((uint8_t) (((uint32_t) w1 >> 24) & 7), (w0 >> 12) & 0xfff, w0 & 0xfff,
                              ((uint32_t) w1 >> 12) & 0xfff);
                break;
            case G_LOADTILE:
                dp_load_tile((uint8_t) (((uint32_t) w1 >> 24) & 7), (w0 >> 12) & 0xfff, w0 & 0xfff,
                             ((uint32_t) w1 >> 12) & 0xfff, (uint32_t) w1 & 0xfff);
                break;
            case G_LOADTLUT:
                dp_load_tlut((uint8_t) (((uint32_t) w1 >> 24) & 7), ((uint32_t) w1 >> 14) & 0x3ff);
                break;
            case G_SETCOMBINE:
                sCombine = ((uint64_t) (w0 & 0xffffff) << 32) | ((uint32_t) w1);
                mark_dirty();
                break;
            case G_SETPRIMCOLOR:
                color_from_rgba(sPrimColor, (uint32_t) w1);
                sPrimLod = (float) (w0 & 0xff) / 255.0f;
                mark_dirty();
                break;
            case G_SETENVCOLOR:
                color_from_rgba(sEnvColor, (uint32_t) w1);
                mark_dirty();
                break;
            case G_SETFOGCOLOR:
                color_from_rgba(sFogColor, (uint32_t) w1);
                mark_dirty();
                break;
            case G_SETBLENDCOLOR:
                color_from_rgba(sBlendColor, (uint32_t) w1);
                mark_dirty();
                break;
            case G_SETFILLCOLOR:
                sFillColor = (uint32_t) w1;
                mark_dirty();
                break;
            case G_SETPRIMDEPTH:
            case G_SETKEYGB:
            case G_SETKEYR:
            case G_SETCONVERT:
                break;
            case G_SETSCISSOR: {
                float ulx = (float) ((w0 >> 12) & 0xfff) / 4.0f;
                float uly = (float) (w0 & 0xfff) / 4.0f;
                float lrx = (float) (((uint32_t) w1 >> 12) & 0xfff) / 4.0f;
                float lry = (float) ((uint32_t) w1 & 0xfff) / 4.0f;
                float rx = (float) gfx_current_dimensions.width / SCREEN_W;
                float ry = (float) gfx_current_dimensions.height / SCREEN_H;
                flush();
                sScissorRect[0] = (int) floorf(ulx * rx);
                sScissorRect[1] = (int) floorf((SCREEN_H - lry) * ry);
                sScissorRect[2] = (int) ceilf((lrx - ulx) * rx);
                sScissorRect[3] = (int) ceilf((lry - uly) * ry);
                mark_dirty();
                break;
            }
            case G_FILLRECT: {
                const Gfx *next = cmd + 1;
                int32_t lrx = sign_extend(w0 >> 2, 22);
                int32_t lry = sign_extend((uint32_t) w1 >> 2, 22);
                int32_t ulx = sign_extend((uint32_t) next->words.w0 >> 2, 22);
                int32_t uly = sign_extend((uint32_t) next->words.w1 >> 2, 22);
                dp_fill_rect(ulx, uly, lrx, lry);
                cmd++;
                break;
            }
            case G_TEXRECT:
            case G_TEXRECTFLIP: {
                const Gfx *c1 = cmd + 1;
                const Gfx *c2 = cmd + 2;
                int32_t lrx = sign_extend(w0, 24);
                int32_t lry = sign_extend((uint32_t) w1, 24);
                uint8_t tile = (uint8_t) (((uint32_t) c1->words.w0 >> 24) & 7);
                int32_t ulx = sign_extend((uint32_t) c1->words.w0, 24);
                int32_t uly = sign_extend((uint32_t) c1->words.w1, 24);
                uint32_t st = (uint32_t) c2->words.w0;
                uint32_t dd = (uint32_t) c2->words.w1;
                dp_tex_rect(ulx, uly, lrx, lry, tile, (int16_t) (st >> 16), (int16_t) (st & 0xffff),
                            (int16_t) (dd >> 16), (int16_t) (dd & 0xffff), op == G_TEXRECTFLIP);
                cmd += 2;
                break;
            }
            default:
                break;
        }
        cmd++;
    }
}

bool gfx_init(void) {
    if (!sBackend->init()) {
        return false;
    }
    gfx_texture_cache_init();
    memset(sModelview, 0, sizeof(sModelview));
    sModelview[0][0][0] = sModelview[0][1][1] = sModelview[0][2][2] = sModelview[0][3][3] = 1.0f;
    memset(sProjection, 0, sizeof(sProjection));
    sProjection[0][0] = sProjection[1][1] = sProjection[2][2] = sProjection[3][3] = 1.0f;
    return true;
}

void gfx_shutdown(void) {
    gfx_texture_cache_clear(sBackend);
}

void gfx_set_window_size(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    gfx_current_dimensions.width = width;
    gfx_current_dimensions.height = height;
    gfx_current_dimensions.aspect_ratio = (float) width / (float) height;
}

void gfx_start_frame(void) {
    int full[4];
    sBackend->start_frame((int) gfx_current_dimensions.width, (int) gfx_current_dimensions.height);
    sHaveCurState = false;
    sStateDirty = true;
    sBatchTris = 0;
    sModelviewTop = 0;
    sMPDirty = true;
    sLightsDirty = true;
    full[0] = 0;
    full[1] = 0;
    full[2] = (int) gfx_current_dimensions.width;
    full[3] = (int) gfx_current_dimensions.height;
    memcpy(sViewportRect, full, sizeof(full));
    memcpy(sScissorRect, full, sizeof(full));
    sNoiseFrame++;
    gfx_texture_cache_frame();
}

void gfx_run(void *commands) {
    if (commands == NULL) {
        return;
    }
    run_dl((const Gfx *) commands, 0);
    flush();
}

void gfx_set_overlay_callback(GfxOverlayCallback cb) {
    sOverlayCallback = cb;
}

static void overlay_begin(const float color[4]) {
    struct DrawState s;
    flush();
    memset(&s, 0, sizeof(s));
    s.key.combine = make_combine(15, 15, 31, G_CCMUX_PRIMITIVE, 7, 7, 7, G_ACMUX_PRIMITIVE, 15, 15, 31,
                                 G_CCMUX_PRIMITIVE, 7, 7, 7, G_ACMUX_PRIMITIVE);
    s.key.flags = GFX_SHADER_BLEND;
    s.blend = 1;
    s.viewport[2] = s.scissor[2] = (int) gfx_current_dimensions.width;
    s.viewport[3] = s.scissor[3] = (int) gfx_current_dimensions.height;
    memcpy(s.uniforms.prim, color, sizeof(float) * 4);
    sHaveCurState = false;
    apply_state(&s);
}

static void overlay_emit(float *out, float x, float y) {
    memset(out, 0, sizeof(float) * GFX_VTX_FLOATS);
    out[0] = x;
    out[1] = y;
    out[2] = -1.0f;
    out[3] = 1.0f;
    out[8] = 1.0f;
    out[9] = 1.0f;
    out[10] = 1.0f;
    out[11] = 1.0f;
}

static void overlay_end(void) {
    flush();
    sHaveCurState = false;
    sStateDirty = true;
}

void gfx_draw_overlay_quad(float x0, float y0, float x1, float y1, const float color[4]) {
    float px[4];
    float py[4];
    int order[6] = { 0, 2, 1, 1, 2, 3 };
    int i;
    px[0] = x0; px[1] = x1; px[2] = x0; px[3] = x1;
    py[0] = y0; py[1] = y0; py[2] = y1; py[3] = y1;
    overlay_begin(color);
    for (i = 0; i < 6; i++) {
        overlay_emit(&sBatch[i * GFX_VTX_FLOATS], px[order[i]], py[order[i]]);
    }
    sBatchTris = 2;
    overlay_end();
}

void gfx_draw_overlay_circle(float cx, float cy, float rx, float ry, float thickness, const float color[4]) {
    const int segments = 32;
    int i;
    size_t v = 0;
    overlay_begin(color);
    for (i = 0; i < segments; i++) {
        float a0 = (float) i / (float) segments * 6.2831853f;
        float a1 = (float) (i + 1) / (float) segments * 6.2831853f;
        float c0 = cosf(a0);
        float s0 = sinf(a0);
        float c1 = cosf(a1);
        float s1 = sinf(a1);
        if (thickness <= 0.0f || thickness >= 1.0f) {
            overlay_emit(&sBatch[(v++) * GFX_VTX_FLOATS], cx, cy);
            overlay_emit(&sBatch[(v++) * GFX_VTX_FLOATS], cx + c0 * rx, cy + s0 * ry);
            overlay_emit(&sBatch[(v++) * GFX_VTX_FLOATS], cx + c1 * rx, cy + s1 * ry);
        } else {
            float ir = 1.0f - thickness;
            overlay_emit(&sBatch[(v++) * GFX_VTX_FLOATS], cx + c0 * rx * ir, cy + s0 * ry * ir);
            overlay_emit(&sBatch[(v++) * GFX_VTX_FLOATS], cx + c0 * rx, cy + s0 * ry);
            overlay_emit(&sBatch[(v++) * GFX_VTX_FLOATS], cx + c1 * rx, cy + s1 * ry);
            overlay_emit(&sBatch[(v++) * GFX_VTX_FLOATS], cx + c0 * rx * ir, cy + s0 * ry * ir);
            overlay_emit(&sBatch[(v++) * GFX_VTX_FLOATS], cx + c1 * rx, cy + s1 * ry);
            overlay_emit(&sBatch[(v++) * GFX_VTX_FLOATS], cx + c1 * rx * ir, cy + s1 * ry * ir);
        }
    }
    sBatchTris = v / 3;
    overlay_end();
}

void gfx_end_frame(void) {
    flush();
    if (sOverlayCallback != NULL) {
        sOverlayCallback();
    }
    sBackend->end_frame();
}
