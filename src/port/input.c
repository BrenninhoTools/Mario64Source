#include <math.h>
#include <string.h>

#include <SDL.h>

#include "port.h"
#include "gfx/gfx_rsp.h"

#define MAX_PADS 4
#define STICK_MAX 80
#define AXIS_DEADZONE 7000
#define C_THRESHOLD 16000
#define MAX_FINGERS 10

enum TouchControl {
    TOUCH_NONE,
    TOUCH_STICK,
    TOUCH_A,
    TOUCH_B,
    TOUCH_Z,
    TOUCH_R,
    TOUCH_START,
    TOUCH_C_UP,
    TOUCH_C_DOWN,
    TOUCH_C_LEFT,
    TOUCH_C_RIGHT,
    TOUCH_COUNT
};

struct TouchButton {
    enum TouchControl id;
    float x;
    float y;
    float r;
    bool fromRight;
    uint16_t mask;
    float color[3];
};

struct Finger {
    bool active;
    SDL_FingerID id;
    enum TouchControl control;
    float x;
    float y;
};

static SDL_GameController *sControllers[MAX_PADS];
static SDL_JoystickID sControllerIds[MAX_PADS];
static struct PortPadState sPads[MAX_PADS];
static struct Finger sFingers[MAX_FINGERS];
static bool sTouchUsed;
static float sStickOriginX;
static float sStickOriginY;
static float sStickX;
static float sStickY;
static uint16_t sTouchButtons;

static const struct TouchButton sTouchLayout[] = {
    { TOUCH_A, 0.13f, 0.78f, 0.075f, true, PORT_N64_BUTTON_A, { 0.25f, 0.45f, 1.0f } },
    { TOUCH_B, 0.30f, 0.86f, 0.065f, true, PORT_N64_BUTTON_B, { 0.15f, 0.8f, 0.3f } },
    { TOUCH_Z, 0.46f, 0.88f, 0.055f, true, PORT_N64_BUTTON_Z, { 0.7f, 0.7f, 0.7f } },
    { TOUCH_R, 0.13f, 0.14f, 0.06f, true, PORT_N64_BUTTON_R, { 0.7f, 0.7f, 0.7f } },
    { TOUCH_START, 0.0f, 0.08f, 0.045f, false, PORT_N64_BUTTON_START, { 0.9f, 0.25f, 0.25f } },
    { TOUCH_C_UP, 0.25f, 0.34f, 0.05f, true, PORT_N64_C_UP, { 1.0f, 0.85f, 0.1f } },
    { TOUCH_C_DOWN, 0.25f, 0.56f, 0.05f, true, PORT_N64_C_DOWN, { 1.0f, 0.85f, 0.1f } },
    { TOUCH_C_LEFT, 0.36f, 0.45f, 0.05f, true, PORT_N64_C_LEFT, { 1.0f, 0.85f, 0.1f } },
    { TOUCH_C_RIGHT, 0.14f, 0.45f, 0.05f, true, PORT_N64_C_RIGHT, { 1.0f, 0.85f, 0.1f } },
};

static float sWindowAspect = 16.0f / 9.0f;

static void layout_center(const struct TouchButton *b, float *x, float *y) {
    float aspect = sWindowAspect;
    if (b->id == TOUCH_START) {
        *x = 0.5f;
    } else if (b->fromRight) {
        *x = 1.0f - b->x / aspect;
    } else {
        *x = b->x / aspect;
    }
    *y = b->y;
}

static enum TouchControl hit_test(float x, float y) {
    size_t i;
    float aspect = sWindowAspect;
    for (i = 0; i < sizeof(sTouchLayout) / sizeof(sTouchLayout[0]); i++) {
        float cx, cy, dx, dy;
        layout_center(&sTouchLayout[i], &cx, &cy);
        dx = (x - cx) * aspect;
        dy = y - cy;
        if (dx * dx + dy * dy <= sTouchLayout[i].r * sTouchLayout[i].r * 1.6f) {
            return sTouchLayout[i].id;
        }
    }
    if (x < 0.45f) {
        return TOUCH_STICK;
    }
    return TOUCH_NONE;
}

static int find_slot(SDL_JoystickID id) {
    int i;
    for (i = 0; i < MAX_PADS; i++) {
        if (sControllers[i] != NULL && sControllerIds[i] == id) {
            return i;
        }
    }
    return -1;
}

static void open_controller(int deviceIndex) {
    int i;
    SDL_GameController *gc;
    if (!SDL_IsGameController(deviceIndex)) {
        return;
    }
    gc = SDL_GameControllerOpen(deviceIndex);
    if (gc == NULL) {
        return;
    }
    for (i = 0; i < MAX_PADS; i++) {
        if (sControllers[i] == NULL) {
            sControllers[i] = gc;
            sControllerIds[i] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
            return;
        }
    }
    SDL_GameControllerClose(gc);
}

static void touch_draw(void) {
    size_t i;
    float aspect;
    if (!sTouchUsed) {
        return;
    }
    aspect = (float) gfx_current_dimensions.width / (float) gfx_current_dimensions.height;
    sWindowAspect = aspect;
    for (i = 0; i < sizeof(sTouchLayout) / sizeof(sTouchLayout[0]); i++) {
        const struct TouchButton *b = &sTouchLayout[i];
        float cx, cy;
        float color[4];
        bool pressed = (sTouchButtons & b->mask) != 0;
        layout_center(b, &cx, &cy);
        color[0] = b->color[0];
        color[1] = b->color[1];
        color[2] = b->color[2];
        color[3] = pressed ? 0.75f : 0.35f;
        gfx_draw_overlay_circle(cx * 2.0f - 1.0f, 1.0f - cy * 2.0f, b->r * 2.0f / aspect, b->r * 2.0f, 0.0f, color);
    }
    {
        float base[4] = { 1.0f, 1.0f, 1.0f, 0.25f };
        float knob[4] = { 1.0f, 1.0f, 1.0f, 0.5f };
        float r = 0.14f;
        float ox = sStickOriginX;
        float oy = sStickOriginY;
        bool active = false;
        for (i = 0; i < MAX_FINGERS; i++) {
            if (sFingers[i].active && sFingers[i].control == TOUCH_STICK) {
                active = true;
            }
        }
        if (!active) {
            ox = 0.16f / aspect + 0.05f;
            oy = 0.72f;
        }
        gfx_draw_overlay_circle(ox * 2.0f - 1.0f, 1.0f - oy * 2.0f, r * 2.0f / aspect, r * 2.0f, 0.12f, base);
        gfx_draw_overlay_circle((ox + sStickX * r / aspect) * 2.0f - 1.0f, 1.0f - (oy - sStickY * r) * 2.0f,
                                0.06f * 2.0f / aspect, 0.06f * 2.0f, 0.0f, knob);
    }
}

void port_input_init(void) {
    static bool done;
    int i;
    if (done) {
        return;
    }
    done = true;
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC);
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        open_controller(i);
    }
#ifdef __ANDROID__
    sTouchUsed = true;
#endif
    gfx_set_overlay_callback(touch_draw);
}

void port_input_shutdown(void) {
    int i;
    for (i = 0; i < MAX_PADS; i++) {
        if (sControllers[i] != NULL) {
            SDL_GameControllerClose(sControllers[i]);
            sControllers[i] = NULL;
        }
    }
}

static void update_touch_state(void) {
    int i;
    sTouchButtons = 0;
    sStickX = 0.0f;
    sStickY = 0.0f;
    for (i = 0; i < MAX_FINGERS; i++) {
        struct Finger *f = &sFingers[i];
        size_t b;
        if (!f->active) {
            continue;
        }
        if (f->control == TOUCH_STICK) {
            float dx = (f->x - sStickOriginX) * sWindowAspect / 0.14f;
            float dy = (sStickOriginY - f->y) / 0.14f;
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 1.0f) {
                dx /= len;
                dy /= len;
            }
            sStickX = dx;
            sStickY = dy;
            continue;
        }
        for (b = 0; b < sizeof(sTouchLayout) / sizeof(sTouchLayout[0]); b++) {
            if (sTouchLayout[b].id == f->control) {
                sTouchButtons |= sTouchLayout[b].mask;
            }
        }
    }
}

void port_input_handle_event(const void *ev) {
    const SDL_Event *e = (const SDL_Event *) ev;
    int i;
    switch (e->type) {
        case SDL_CONTROLLERDEVICEADDED:
            open_controller(e->cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED: {
            int slot = find_slot(e->cdevice.which);
            if (slot >= 0) {
                SDL_GameControllerClose(sControllers[slot]);
                sControllers[slot] = NULL;
            }
            break;
        }
        case SDL_FINGERDOWN: {
            enum TouchControl c = hit_test(e->tfinger.x, e->tfinger.y);
            sTouchUsed = true;
            for (i = 0; i < MAX_FINGERS; i++) {
                if (!sFingers[i].active) {
                    sFingers[i].active = true;
                    sFingers[i].id = e->tfinger.fingerId;
                    sFingers[i].control = c;
                    sFingers[i].x = e->tfinger.x;
                    sFingers[i].y = e->tfinger.y;
                    if (c == TOUCH_STICK) {
                        sStickOriginX = e->tfinger.x;
                        sStickOriginY = e->tfinger.y;
                    }
                    break;
                }
            }
            update_touch_state();
            break;
        }
        case SDL_FINGERMOTION:
            for (i = 0; i < MAX_FINGERS; i++) {
                if (sFingers[i].active && sFingers[i].id == e->tfinger.fingerId) {
                    sFingers[i].x = e->tfinger.x;
                    sFingers[i].y = e->tfinger.y;
                    if (sFingers[i].control != TOUCH_STICK) {
                        enum TouchControl c = hit_test(e->tfinger.x, e->tfinger.y);
                        if (c != TOUCH_STICK && c != TOUCH_NONE) {
                            sFingers[i].control = c;
                        }
                    }
                }
            }
            update_touch_state();
            break;
        case SDL_FINGERUP:
            for (i = 0; i < MAX_FINGERS; i++) {
                if (sFingers[i].active && sFingers[i].id == e->tfinger.fingerId) {
                    sFingers[i].active = false;
                }
            }
            update_touch_state();
            break;
        default:
            break;
    }
}

static int8_t axis_to_stick(Sint16 v) {
    float f;
    if (v > -AXIS_DEADZONE && v < AXIS_DEADZONE) {
        return 0;
    }
    f = (float) v / 32767.0f * (float) STICK_MAX;
    if (f > STICK_MAX) {
        f = STICK_MAX;
    }
    if (f < -STICK_MAX) {
        f = -STICK_MAX;
    }
    return (int8_t) f;
}

static void read_keyboard(struct PortPadState *p) {
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    int x = 0;
    int y = 0;
    if (k[SDL_SCANCODE_A]) {
        x -= STICK_MAX;
    }
    if (k[SDL_SCANCODE_D]) {
        x += STICK_MAX;
    }
    if (k[SDL_SCANCODE_W]) {
        y += STICK_MAX;
    }
    if (k[SDL_SCANCODE_S]) {
        y -= STICK_MAX;
    }
    if (x != 0 || y != 0) {
        if (k[SDL_SCANCODE_LALT]) {
            x /= 2;
            y /= 2;
        }
        p->stickX = (int8_t) x;
        p->stickY = (int8_t) y;
    }
    if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_J]) {
        p->buttons |= PORT_N64_BUTTON_A;
    }
    if (k[SDL_SCANCODE_K] || k[SDL_SCANCODE_LSHIFT]) {
        p->buttons |= PORT_N64_BUTTON_B;
    }
    if (k[SDL_SCANCODE_L] || k[SDL_SCANCODE_LCTRL]) {
        p->buttons |= PORT_N64_BUTTON_Z;
    }
    if (k[SDL_SCANCODE_RETURN]) {
        p->buttons |= PORT_N64_BUTTON_START;
    }
    if (k[SDL_SCANCODE_R] || k[SDL_SCANCODE_RSHIFT]) {
        p->buttons |= PORT_N64_BUTTON_R;
    }
    if (k[SDL_SCANCODE_Q]) {
        p->buttons |= PORT_N64_BUTTON_L;
    }
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_I]) {
        p->buttons |= PORT_N64_C_UP;
    }
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_COMMA]) {
        p->buttons |= PORT_N64_C_DOWN;
    }
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_U]) {
        p->buttons |= PORT_N64_C_LEFT;
    }
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_O]) {
        p->buttons |= PORT_N64_C_RIGHT;
    }
}

static void read_controller(SDL_GameController *gc, struct PortPadState *p) {
    int8_t sx = axis_to_stick(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX));
    int8_t sy = axis_to_stick(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY));
    Sint16 rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX);
    Sint16 ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY);
    if (sx != 0 || sy != 0) {
        p->stickX = sx;
        p->stickY = (int8_t) (sy == -128 ? 127 : -sy);
    }
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)) {
        p->buttons |= PORT_N64_BUTTON_A;
    }
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B)
        || SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X)) {
        p->buttons |= PORT_N64_BUTTON_B;
    }
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
        || SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000) {
        p->buttons |= PORT_N64_BUTTON_Z;
    }
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
        || SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) {
        p->buttons |= PORT_N64_BUTTON_R;
    }
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) {
        p->buttons |= PORT_N64_BUTTON_START;
    }
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK)) {
        p->buttons |= PORT_N64_BUTTON_L;
    }
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y)) {
        p->buttons |= PORT_N64_C_DOWN;
    }
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
        p->buttons |= PORT_N64_DPAD_UP;
    }
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
        p->buttons |= PORT_N64_DPAD_DOWN;
    }
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
        p->buttons |= PORT_N64_DPAD_LEFT;
    }
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        p->buttons |= PORT_N64_DPAD_RIGHT;
    }
    if (rx < -C_THRESHOLD) {
        p->buttons |= PORT_N64_C_LEFT;
    }
    if (rx > C_THRESHOLD) {
        p->buttons |= PORT_N64_C_RIGHT;
    }
    if (ry < -C_THRESHOLD) {
        p->buttons |= PORT_N64_C_UP;
    }
    if (ry > C_THRESHOLD) {
        p->buttons |= PORT_N64_C_DOWN;
    }
}

void port_input_update(void) {
    int i;
    memset(sPads, 0, sizeof(sPads));
    read_keyboard(&sPads[0]);
    for (i = 0; i < MAX_PADS; i++) {
        if (sControllers[i] != NULL) {
            read_controller(sControllers[i], &sPads[i]);
        }
    }
    if (sTouchUsed) {
        if (sStickX != 0.0f || sStickY != 0.0f) {
            sPads[0].stickX = (int8_t) (sStickX * STICK_MAX);
            sPads[0].stickY = (int8_t) (sStickY * STICK_MAX);
        }
        sPads[0].buttons |= sTouchButtons;
    }
}

void port_input_get_pad(int index, struct PortPadState *out) {
    if (index < 0 || index >= MAX_PADS) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = sPads[index];
}

bool port_input_pad_connected(int index) {
    if (index == 0) {
        return true;
    }
    return index > 0 && index < MAX_PADS && sControllers[index] != NULL;
}

void port_input_rumble(int index, bool on) {
    if (index < 0 || index >= MAX_PADS || sControllers[index] == NULL) {
        return;
    }
    SDL_GameControllerRumble(sControllers[index], on ? 0xC000 : 0, on ? 0xC000 : 0, on ? 1000 : 0);
}

void port_input_touch_draw(void) {
    touch_draw();
}
