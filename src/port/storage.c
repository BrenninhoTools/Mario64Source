#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "port.h"

static char sUserPath[1024];

const char *port_user_path(void) {
    if (sUserPath[0] == '\0') {
        char *p = SDL_GetPrefPath("BrenninhoTools", "Mario64");
        if (p != NULL) {
            SDL_strlcpy(sUserPath, p, sizeof(sUserPath));
            SDL_free(p);
        } else {
            SDL_strlcpy(sUserPath, "./", sizeof(sUserPath));
        }
    }
    return sUserPath;
}

static void build_path(char *out, size_t size, const char *name) {
    SDL_snprintf(out, size, "%s%s", port_user_path(), name);
}

bool port_read_file(const char *name, void *buf, size_t size) {
    char path[1200];
    SDL_RWops *rw;
    size_t got;
    build_path(path, sizeof(path), name);
    rw = SDL_RWFromFile(path, "rb");
    if (rw == NULL) {
        return false;
    }
    got = SDL_RWread(rw, buf, 1, size);
    SDL_RWclose(rw);
    return got == size;
}

bool port_write_file(const char *name, const void *buf, size_t size) {
    char path[1200];
    char tmp[1220];
    SDL_RWops *rw;
    size_t put;
    build_path(path, sizeof(path), name);
    SDL_snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    rw = SDL_RWFromFile(tmp, "wb");
    if (rw == NULL) {
        return false;
    }
    put = SDL_RWwrite(rw, buf, 1, size);
    SDL_RWclose(rw);
    if (put != size) {
        remove(tmp);
        return false;
    }
    remove(path);
    return rename(tmp, path) == 0;
}
