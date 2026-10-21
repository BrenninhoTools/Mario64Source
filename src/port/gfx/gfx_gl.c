#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <SDL.h>

#ifdef PORT_USE_GLES
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

#include <ultra64.h>

#include "gfx_backend.h"

#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif

#define MAX_PROGRAMS 512
#define MAX_TRACKED_TEXTURES 65536

typedef char pglchar;

typedef GLuint (APIENTRY *PFN_CreateShader)(GLenum);
typedef void (APIENTRY *PFN_ShaderSource)(GLuint, GLsizei, const pglchar *const *, const GLint *);
typedef void (APIENTRY *PFN_CompileShader)(GLuint);
typedef void (APIENTRY *PFN_GetShaderiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *PFN_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, pglchar *);
typedef GLuint (APIENTRY *PFN_CreateProgram)(void);
typedef void (APIENTRY *PFN_AttachShader)(GLuint, GLuint);
typedef void (APIENTRY *PFN_BindAttribLocation)(GLuint, GLuint, const pglchar *);
typedef void (APIENTRY *PFN_LinkProgram)(GLuint);
typedef void (APIENTRY *PFN_GetProgramiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *PFN_GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, pglchar *);
typedef void (APIENTRY *PFN_UseProgram)(GLuint);
typedef GLint (APIENTRY *PFN_GetUniformLocation)(GLuint, const pglchar *);
typedef void (APIENTRY *PFN_Uniform1i)(GLint, GLint);
typedef void (APIENTRY *PFN_Uniform1f)(GLint, GLfloat);
typedef void (APIENTRY *PFN_Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_GenBuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_BindBuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_BufferData)(GLenum, ptrdiff_t, const void *, GLenum);
typedef void (APIENTRY *PFN_VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void (APIENTRY *PFN_EnableVertexAttribArray)(GLuint);
typedef void (APIENTRY *PFN_ActiveTexture)(GLenum);
typedef void (APIENTRY *PFN_GenTextures)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_DeleteTextures)(GLsizei, const GLuint *);
typedef void (APIENTRY *PFN_BindTexture)(GLenum, GLuint);
typedef void (APIENTRY *PFN_TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void (APIENTRY *PFN_TexParameteri)(GLenum, GLenum, GLint);
typedef void (APIENTRY *PFN_PixelStorei)(GLenum, GLint);
typedef void (APIENTRY *PFN_Enable)(GLenum);
typedef void (APIENTRY *PFN_Disable)(GLenum);
typedef void (APIENTRY *PFN_DepthFunc)(GLenum);
typedef void (APIENTRY *PFN_DepthMask)(GLboolean);
typedef void (APIENTRY *PFN_PolygonOffset)(GLfloat, GLfloat);
typedef void (APIENTRY *PFN_BlendFunc)(GLenum, GLenum);
typedef void (APIENTRY *PFN_Viewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (APIENTRY *PFN_Scissor)(GLint, GLint, GLsizei, GLsizei);
typedef void (APIENTRY *PFN_Clear)(GLbitfield);
typedef void (APIENTRY *PFN_ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_DrawArrays)(GLenum, GLint, GLsizei);

static PFN_CreateShader pglCreateShader;
static PFN_ShaderSource pglShaderSource;
static PFN_CompileShader pglCompileShader;
static PFN_GetShaderiv pglGetShaderiv;
static PFN_GetShaderInfoLog pglGetShaderInfoLog;
static PFN_CreateProgram pglCreateProgram;
static PFN_AttachShader pglAttachShader;
static PFN_BindAttribLocation pglBindAttribLocation;
static PFN_LinkProgram pglLinkProgram;
static PFN_GetProgramiv pglGetProgramiv;
static PFN_GetProgramInfoLog pglGetProgramInfoLog;
static PFN_UseProgram pglUseProgram;
static PFN_GetUniformLocation pglGetUniformLocation;
static PFN_Uniform1i pglUniform1i;
static PFN_Uniform1f pglUniform1f;
static PFN_Uniform4f pglUniform4f;
static PFN_GenBuffers pglGenBuffers;
static PFN_BindBuffer pglBindBuffer;
static PFN_BufferData pglBufferData;
static PFN_VertexAttribPointer pglVertexAttribPointer;
static PFN_EnableVertexAttribArray pglEnableVertexAttribArray;
static PFN_ActiveTexture pglActiveTexture;
static PFN_GenTextures pglGenTextures;
static PFN_DeleteTextures pglDeleteTextures;
static PFN_BindTexture pglBindTexture;
static PFN_TexImage2D pglTexImage2D;
static PFN_TexParameteri pglTexParameteri;
static PFN_PixelStorei pglPixelStorei;
static PFN_Enable pglEnable;
static PFN_Disable pglDisable;
static PFN_DepthFunc pglDepthFunc;
static PFN_DepthMask pglDepthMask;
static PFN_PolygonOffset pglPolygonOffset;
static PFN_BlendFunc pglBlendFunc;
static PFN_Viewport pglViewport;
static PFN_Scissor pglScissor;
static PFN_Clear pglClear;
static PFN_ClearColor pglClearColor;
static PFN_DrawArrays pglDrawArrays;

struct GfxShaderProgram {
    struct GfxShaderKey key;
    GLuint id;
    GLint uTex0;
    GLint uTex1;
    GLint uPrim;
    GLint uEnv;
    GLint uFog;
    GLint uPrimLod;
    GLint uAlphaRef;
    GLint uNoise;
};

static struct GfxShaderProgram sPrograms[MAX_PROGRAMS];
static int sProgramCount;
static GLuint sVbo;
static bool sDepthWrite = true;
static uint8_t sTextureNpot[MAX_TRACKED_TEXTURES];
static GLuint sBoundTexture[2];

#define LOAD(name) \
    do { \
        pgl##name = (PFN_##name) SDL_GL_GetProcAddress("gl" #name); \
        if (pgl##name == NULL) { \
            fprintf(stderr, "Missing GL function gl%s\n", #name); \
            return false; \
        } \
    } while (0)

static bool load_functions(void) {
    LOAD(CreateShader);
    LOAD(ShaderSource);
    LOAD(CompileShader);
    LOAD(GetShaderiv);
    LOAD(GetShaderInfoLog);
    LOAD(CreateProgram);
    LOAD(AttachShader);
    LOAD(BindAttribLocation);
    LOAD(LinkProgram);
    LOAD(GetProgramiv);
    LOAD(GetProgramInfoLog);
    LOAD(UseProgram);
    LOAD(GetUniformLocation);
    LOAD(Uniform1i);
    LOAD(Uniform1f);
    LOAD(Uniform4f);
    LOAD(GenBuffers);
    LOAD(BindBuffer);
    LOAD(BufferData);
    LOAD(VertexAttribPointer);
    LOAD(EnableVertexAttribArray);
    LOAD(ActiveTexture);
    LOAD(GenTextures);
    LOAD(DeleteTextures);
    LOAD(BindTexture);
    LOAD(TexImage2D);
    LOAD(TexParameteri);
    LOAD(PixelStorei);
    LOAD(Enable);
    LOAD(Disable);
    LOAD(DepthFunc);
    LOAD(DepthMask);
    LOAD(PolygonOffset);
    LOAD(BlendFunc);
    LOAD(Viewport);
    LOAD(Scissor);
    LOAD(Clear);
    LOAD(ClearColor);
    LOAD(DrawArrays);
    return true;
}

static int sel(uint64_t combine, int cycle, int slot) {
    uint32_t w0 = (uint32_t) (combine >> 32);
    uint32_t w1 = (uint32_t) combine;
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

static const char *color_input(int slot, int v, int cycle) {
    switch (slot) {
        case 0:
            switch (v) {
                case 0: return cycle == 0 ? "vec3(0.0)" : "comb.rgb";
                case 1: return "t0.rgb";
                case 2: return "t1.rgb";
                case 3: return "uPrim.rgb";
                case 4: return "vShade.rgb";
                case 5: return "uEnv.rgb";
                case 6: return "vec3(1.0)";
                case 7: return "vec3(noise)";
                default: return "vec3(0.0)";
            }
        case 1:
            switch (v) {
                case 0: return cycle == 0 ? "vec3(0.0)" : "comb.rgb";
                case 1: return "t0.rgb";
                case 2: return "t1.rgb";
                case 3: return "uPrim.rgb";
                case 4: return "vShade.rgb";
                case 5: return "uEnv.rgb";
                default: return "vec3(0.0)";
            }
        case 2:
            switch (v) {
                case 0: return cycle == 0 ? "vec3(0.0)" : "comb.rgb";
                case 1: return "t0.rgb";
                case 2: return "t1.rgb";
                case 3: return "uPrim.rgb";
                case 4: return "vShade.rgb";
                case 5: return "uEnv.rgb";
                case 6: return "vec3(1.0)";
                case 7: return cycle == 0 ? "vec3(0.0)" : "vec3(comb.a)";
                case 8: return "vec3(t0.a)";
                case 9: return "vec3(t1.a)";
                case 10: return "vec3(uPrim.a)";
                case 11: return "vec3(vShade.a)";
                case 12: return "vec3(uEnv.a)";
                case 13: return "vec3(0.0)";
                case 14: return "vec3(uPrimLod)";
                case 15: return "vec3(1.0)";
                default: return "vec3(0.0)";
            }
        default:
            switch (v) {
                case 0: return cycle == 0 ? "vec3(0.0)" : "comb.rgb";
                case 1: return "t0.rgb";
                case 2: return "t1.rgb";
                case 3: return "uPrim.rgb";
                case 4: return "vShade.rgb";
                case 5: return "uEnv.rgb";
                case 6: return "vec3(1.0)";
                default: return "vec3(0.0)";
            }
    }
}

static const char *alpha_input(int slot, int v, int cycle) {
    if (slot == 2) {
        switch (v) {
            case 0: return "0.0";
            case 1: return "t0.a";
            case 2: return "t1.a";
            case 3: return "uPrim.a";
            case 4: return "vShade.a";
            case 5: return "uEnv.a";
            case 6: return "uPrimLod";
            default: return "0.0";
        }
    }
    switch (v) {
        case 0: return cycle == 0 ? "0.0" : "comb.a";
        case 1: return "t0.a";
        case 2: return "t1.a";
        case 3: return "uPrim.a";
        case 4: return "vShade.a";
        case 5: return "uEnv.a";
        case 6: return "1.0";
        default: return "0.0";
    }
}

static void append(char *buf, size_t cap, size_t *len, const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf + *len, cap - *len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *len += (size_t) n;
        if (*len >= cap) {
            *len = cap - 1;
        }
    }
}

static const char *shader_header(void) {
#ifdef PORT_USE_GLES
    return "#version 100\nprecision mediump float;\n";
#else
    return "#version 110\n";
#endif
}

static GLuint compile(GLenum type, const char *src) {
    GLuint s = pglCreateShader(type);
    GLint ok = 0;
    pglShaderSource(s, 1, &src, NULL);
    pglCompileShader(s);
    pglGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei n = 0;
        pglGetShaderInfoLog(s, sizeof(log), &n, log);
        fprintf(stderr, "Shader compile error:\n%s\n%s\n", log, src);
    }
    return s;
}

static void build_program(struct GfxShaderProgram *p, const struct GfxShaderKey *key) {
    static char vs[2048];
    static char fs[8192];
    size_t vl = 0;
    size_t fl = 0;
    int cycles = (key->flags & GFX_SHADER_TWO_CYCLE) ? 2 : 1;
    int c;
    GLuint v, f, prog;
    GLint ok = 0;

    append(vs, sizeof(vs), &vl, "%s", shader_header());
    append(vs, sizeof(vs), &vl,
           "attribute vec4 aPos;\n"
           "attribute vec2 aUV0;\n"
           "attribute vec2 aUV1;\n"
           "attribute vec4 aShade;\n"
           "attribute float aFog;\n"
           "varying vec2 vUV0;\n"
           "varying vec2 vUV1;\n"
           "varying vec4 vShade;\n"
           "varying float vFog;\n"
           "void main() {\n"
           "    vUV0 = aUV0;\n"
           "    vUV1 = aUV1;\n"
           "    vShade = aShade;\n"
           "    vFog = aFog;\n"
           "    gl_Position = aPos;\n"
           "}\n");

    append(fs, sizeof(fs), &fl, "%s", shader_header());
    append(fs, sizeof(fs), &fl,
           "uniform sampler2D uTex0;\n"
           "uniform sampler2D uTex1;\n"
           "uniform vec4 uPrim;\n"
           "uniform vec4 uEnv;\n"
           "uniform vec4 uFog;\n"
           "uniform float uPrimLod;\n"
           "uniform float uAlphaRef;\n"
           "uniform float uNoise;\n"
           "varying vec2 vUV0;\n"
           "varying vec2 vUV1;\n"
           "varying vec4 vShade;\n"
           "varying float vFog;\n"
           "float hash(vec2 p) {\n"
           "    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);\n"
           "}\n"
           "void main() {\n");

    if (key->flags & GFX_SHADER_FILL) {
        append(fs, sizeof(fs), &fl, "    gl_FragColor = vec4(uPrim.rgb, 1.0);\n}\n");
    } else {
        append(fs, sizeof(fs), &fl,
               "    vec4 t0 = texture2D(uTex0, vUV0);\n"
               "    vec4 t1 = texture2D(uTex1, vUV1);\n"
               "    float noise = hash(gl_FragCoord.xy + vec2(uNoise));\n"
               "    vec4 comb = vec4(0.0);\n"
               "    vec3 rgb;\n"
               "    float a;\n");
        for (c = 0; c < cycles; c++) {
            append(fs, sizeof(fs), &fl, "    rgb = (%s - %s) * %s + %s;\n",
                   color_input(0, sel(key->combine, c, 0), c), color_input(1, sel(key->combine, c, 1), c),
                   color_input(2, sel(key->combine, c, 2), c), color_input(3, sel(key->combine, c, 3), c));
            append(fs, sizeof(fs), &fl, "    a = (%s - %s) * %s + %s;\n",
                   alpha_input(0, sel(key->combine, c, 4), c), alpha_input(1, sel(key->combine, c, 5), c),
                   alpha_input(2, sel(key->combine, c, 6), c), alpha_input(3, sel(key->combine, c, 7), c));
            append(fs, sizeof(fs), &fl, "    comb = clamp(vec4(rgb, a), 0.0, 1.0);\n");
        }
        if (key->flags & GFX_SHADER_TEXTURE_EDGE) {
            append(fs, sizeof(fs), &fl, "    if (comb.a < 0.3) discard;\n");
            if (!(key->flags & GFX_SHADER_BLEND)) {
                append(fs, sizeof(fs), &fl, "    comb.a = 1.0;\n");
            }
        }
        if (key->flags & GFX_SHADER_ALPHA_TEST) {
            append(fs, sizeof(fs), &fl, "    if (comb.a < max(uAlphaRef, 0.004)) discard;\n");
        }
        if (key->flags & GFX_SHADER_NOISE) {
            append(fs, sizeof(fs), &fl, "    if (comb.a < noise) discard;\n");
        }
        if (key->flags & GFX_SHADER_FOG) {
            append(fs, sizeof(fs), &fl, "    comb.rgb = mix(comb.rgb, uFog.rgb, vFog);\n");
        }
        if (key->flags & GFX_SHADER_BLEND) {
            append(fs, sizeof(fs), &fl, "    gl_FragColor = comb;\n}\n");
        } else {
            append(fs, sizeof(fs), &fl, "    gl_FragColor = vec4(comb.rgb, 1.0);\n}\n");
        }
    }

    v = compile(GL_VERTEX_SHADER, vs);
    f = compile(GL_FRAGMENT_SHADER, fs);
    prog = pglCreateProgram();
    pglAttachShader(prog, v);
    pglAttachShader(prog, f);
    pglBindAttribLocation(prog, 0, "aPos");
    pglBindAttribLocation(prog, 1, "aUV0");
    pglBindAttribLocation(prog, 2, "aUV1");
    pglBindAttribLocation(prog, 3, "aShade");
    pglBindAttribLocation(prog, 4, "aFog");
    pglLinkProgram(prog);
    pglGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei n = 0;
        pglGetProgramInfoLog(prog, sizeof(log), &n, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
    }
    p->key = *key;
    p->id = prog;
    pglUseProgram(prog);
    p->uTex0 = pglGetUniformLocation(prog, "uTex0");
    p->uTex1 = pglGetUniformLocation(prog, "uTex1");
    p->uPrim = pglGetUniformLocation(prog, "uPrim");
    p->uEnv = pglGetUniformLocation(prog, "uEnv");
    p->uFog = pglGetUniformLocation(prog, "uFog");
    p->uPrimLod = pglGetUniformLocation(prog, "uPrimLod");
    p->uAlphaRef = pglGetUniformLocation(prog, "uAlphaRef");
    p->uNoise = pglGetUniformLocation(prog, "uNoise");
    if (p->uTex0 >= 0) {
        pglUniform1i(p->uTex0, 0);
    }
    if (p->uTex1 >= 0) {
        pglUniform1i(p->uTex1, 1);
    }
}

static bool gl_init(void) {
    if (!load_functions()) {
        return false;
    }
    pglGenBuffers(1, &sVbo);
    pglBindBuffer(GL_ARRAY_BUFFER, sVbo);
    pglEnableVertexAttribArray(0);
    pglEnableVertexAttribArray(1);
    pglEnableVertexAttribArray(2);
    pglEnableVertexAttribArray(3);
    pglEnableVertexAttribArray(4);
    pglDepthFunc(GL_LEQUAL);
    pglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    pglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    return true;
}

static void gl_start_frame(int width, int height) {
    pglDisable(GL_SCISSOR_TEST);
    pglDepthMask(GL_TRUE);
    sDepthWrite = true;
    pglViewport(0, 0, width, height);
    pglClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    pglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    pglEnable(GL_SCISSOR_TEST);
    pglScissor(0, 0, width, height);
    pglBindBuffer(GL_ARRAY_BUFFER, sVbo);
}

static void gl_end_frame(void) {
}

static struct GfxShaderProgram *gl_shader_lookup(const struct GfxShaderKey *key) {
    int i;
    for (i = 0; i < sProgramCount; i++) {
        if (sPrograms[i].key.combine == key->combine && sPrograms[i].key.flags == key->flags) {
            return &sPrograms[i];
        }
    }
    if (sProgramCount >= MAX_PROGRAMS) {
        return &sPrograms[0];
    }
    build_program(&sPrograms[sProgramCount], key);
    return &sPrograms[sProgramCount++];
}

static void gl_shader_use(struct GfxShaderProgram *p, const struct GfxUniforms *u) {
    pglUseProgram(p->id);
    if (p->uPrim >= 0) {
        pglUniform4f(p->uPrim, u->prim[0], u->prim[1], u->prim[2], u->prim[3]);
    }
    if (p->uEnv >= 0) {
        pglUniform4f(p->uEnv, u->env[0], u->env[1], u->env[2], u->env[3]);
    }
    if (p->uFog >= 0) {
        pglUniform4f(p->uFog, u->fog[0], u->fog[1], u->fog[2], u->fog[3]);
    }
    if (p->uPrimLod >= 0) {
        pglUniform1f(p->uPrimLod, u->primLod);
    }
    if (p->uAlphaRef >= 0) {
        pglUniform1f(p->uAlphaRef, u->alphaRef);
    }
    if (p->uNoise >= 0) {
        pglUniform1f(p->uNoise, u->noiseSeed);
    }
}

static uint32_t gl_texture_new(void) {
    GLuint id = 0;
    pglGenTextures(1, &id);
    return id;
}

static void gl_texture_delete(uint32_t id) {
    GLuint t = id;
    pglDeleteTextures(1, &t);
    if (sBoundTexture[0] == id) {
        sBoundTexture[0] = 0;
    }
    if (sBoundTexture[1] == id) {
        sBoundTexture[1] = 0;
    }
}

static void gl_texture_bind(int unit, uint32_t id) {
    pglActiveTexture(GL_TEXTURE0 + unit);
    pglBindTexture(GL_TEXTURE_2D, id);
    sBoundTexture[unit & 1] = id;
}

static bool is_pot(int v) {
    return v > 0 && (v & (v - 1)) == 0;
}

static void gl_texture_upload(const uint8_t *rgba, int width, int height) {
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (sBoundTexture[0] < MAX_TRACKED_TEXTURES) {
        sTextureNpot[sBoundTexture[0]] = (uint8_t) (!is_pot(width) || !is_pot(height));
    }
}

static GLint gl_wrap(enum GfxWrap w, bool npot) {
#ifdef PORT_USE_GLES
    if (npot) {
        return GL_CLAMP_TO_EDGE;
    }
#else
    (void) npot;
#endif
    switch (w) {
        case GFX_WRAP_CLAMP: return GL_CLAMP_TO_EDGE;
        case GFX_WRAP_MIRROR: return GL_MIRRORED_REPEAT;
        default: return GL_REPEAT;
    }
}

static void gl_texture_sampler(int unit, enum GfxFilter filter, enum GfxWrap wrapS, enum GfxWrap wrapT) {
    GLint f = filter == GFX_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST;
    uint32_t id = sBoundTexture[unit & 1];
    bool npot = id < MAX_TRACKED_TEXTURES ? sTextureNpot[id] != 0 : false;
    pglActiveTexture(GL_TEXTURE0 + unit);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap(wrapS, npot));
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap(wrapT, npot));
}

static void gl_set_depth(bool test, bool write, bool decal) {
    if (test || write) {
        pglEnable(GL_DEPTH_TEST);
        pglDepthFunc(test ? GL_LEQUAL : GL_ALWAYS);
    } else {
        pglDisable(GL_DEPTH_TEST);
    }
    pglDepthMask(write ? GL_TRUE : GL_FALSE);
    sDepthWrite = write;
    if (decal) {
        pglEnable(GL_POLYGON_OFFSET_FILL);
        pglPolygonOffset(-2.0f, -2.0f);
    } else {
        pglDisable(GL_POLYGON_OFFSET_FILL);
    }
}

static void gl_set_viewport(int x, int y, int width, int height) {
    pglViewport(x, y, width, height);
}

static void gl_set_scissor(int x, int y, int width, int height) {
    pglScissor(x, y, width, height);
}

static void gl_set_blend(bool enabled) {
    if (enabled) {
        pglEnable(GL_BLEND);
    } else {
        pglDisable(GL_BLEND);
    }
}

static void gl_clear_depth(void) {
    pglDisable(GL_SCISSOR_TEST);
    pglDepthMask(GL_TRUE);
    pglClear(GL_DEPTH_BUFFER_BIT);
    pglDepthMask(sDepthWrite ? GL_TRUE : GL_FALSE);
    pglEnable(GL_SCISSOR_TEST);
}

static void gl_clear_color(float r, float g, float b) {
    pglClearColor(r, g, b, 1.0f);
    pglClear(GL_COLOR_BUFFER_BIT);
}

static void gl_draw_triangles(const float *buf, size_t floatCount, size_t triCount) {
    GLsizei stride = GFX_VTX_FLOATS * sizeof(float);
    pglBufferData(GL_ARRAY_BUFFER, (ptrdiff_t) (floatCount * sizeof(float)), buf, GL_STREAM_DRAW);
    pglVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (const void *) 0);
    pglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (const void *) (4 * sizeof(float)));
    pglVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void *) (6 * sizeof(float)));
    pglVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (const void *) (8 * sizeof(float)));
    pglVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (const void *) (12 * sizeof(float)));
    pglDrawArrays(GL_TRIANGLES, 0, (GLsizei) (triCount * 3));
}

const struct GfxBackend gGfxBackendGL = {
    gl_init,
    gl_start_frame,
    gl_end_frame,
    gl_shader_lookup,
    gl_shader_use,
    gl_texture_new,
    gl_texture_delete,
    gl_texture_bind,
    gl_texture_upload,
    gl_texture_sampler,
    gl_set_depth,
    gl_set_viewport,
    gl_set_scissor,
    gl_set_blend,
    gl_clear_depth,
    gl_clear_color,
    gl_draw_triangles,
};
