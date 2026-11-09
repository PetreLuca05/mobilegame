#include "input.h"

#include <string.h>

void input_begin_frame(MgInput *in)
{
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        MgTouch *t = &in->touches[i];
        if (t->ended) {
            memset(t, 0, sizeof(*t));
            t->owner = MG_TOUCH_FREE;
        }
        t->began = false;
        t->dx = 0.0f;
        t->dy = 0.0f;
    }
    memset(in->key_pressed, 0, sizeof(in->key_pressed));
    in->wheel = 0.0f;
    in->right_click = false;
    in->mouse_dx = 0.0f;
    in->mouse_dy = 0.0f;
    in->text_len = 0;
    in->text[0] = 0;
}

static MgTouch *find_touch(MgInput *in, SDL_FingerID id)
{
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        if ((in->touches[i].active || in->touches[i].ended) && in->touches[i].id == id) {
            return &in->touches[i];
        }
    }
    return NULL;
}

static MgTouch *alloc_touch(MgInput *in)
{
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        MgTouch *t = &in->touches[i];
        if (!t->active && !t->ended) {
            return t;
        }
    }
    return NULL;
}

void input_handle_event(MgInput *in, const SDL_Event *e, float width_pt, float height_pt)
{
    switch (e->type) {
    case SDL_EVENT_FINGER_DOWN: {
        MgTouch *t = alloc_touch(in);
        if (!t) {
            break;
        }
        memset(t, 0, sizeof(*t));
        t->id = e->tfinger.fingerID;
        t->active = true;
        t->began = true;
        t->owner = MG_TOUCH_FREE;
        t->x = t->x0 = e->tfinger.x * width_pt;
        t->y = t->y0 = e->tfinger.y * height_pt;
        break;
    }
    case SDL_EVENT_FINGER_MOTION: {
        MgTouch *t = find_touch(in, e->tfinger.fingerID);
        if (!t || !t->active) {
            break;
        }
        float nx = e->tfinger.x * width_pt;
        float ny = e->tfinger.y * height_pt;
        t->dx += nx - t->x;
        t->dy += ny - t->y;
        t->x = nx;
        t->y = ny;
        break;
    }
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        MgTouch *t = find_touch(in, e->tfinger.fingerID);
        if (!t) {
            break;
        }
        t->active = false;
        t->ended = true;
        break;
    }
    case SDL_EVENT_KEY_DOWN:
        if (e->key.scancode < SDL_SCANCODE_COUNT) {
            if (!e->key.repeat) {
                in->key_pressed[e->key.scancode] = true;
            }
            in->key_down[e->key.scancode] = true;
        }
        break;
    case SDL_EVENT_KEY_UP:
        if (e->key.scancode < SDL_SCANCODE_COUNT) {
            in->key_down[e->key.scancode] = false;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        in->wheel += e->wheel.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.which != SDL_TOUCH_MOUSEID && (e->button.button == SDL_BUTTON_RIGHT || e->button.button == SDL_BUTTON_MIDDLE)) {
            in->mouse_right = true;
            if (e->button.button == SDL_BUTTON_RIGHT) {
                in->right_down_x = e->button.x;
                in->right_down_y = e->button.y;
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.which != SDL_TOUCH_MOUSEID && (e->button.button == SDL_BUTTON_RIGHT || e->button.button == SDL_BUTTON_MIDDLE)) {
            in->mouse_right = false;
            float dx = e->button.x - in->right_down_x, dy = e->button.y - in->right_down_y;
            if (e->button.button == SDL_BUTTON_RIGHT && dx * dx + dy * dy < 36.0f) {
                in->right_click = true;
                in->right_click_x = e->button.x;
                in->right_click_y = e->button.y;
            }
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
#if !defined(SDL_PLATFORM_IOS) && !defined(SDL_PLATFORM_ANDROID)
        if (e->motion.which != SDL_TOUCH_MOUSEID) {
            in->mouse_x = e->motion.x;
            in->mouse_y = e->motion.y;
            in->mouse_seen = true;
        }
#endif
        if (e->motion.which != SDL_TOUCH_MOUSEID && in->mouse_right) {
            in->mouse_dx += e->motion.xrel;
            in->mouse_dy += e->motion.yrel;
        }
        break;
    case SDL_EVENT_TEXT_INPUT: {
        const char *s = e->text.text;
        while (*s && in->text_len < (int)sizeof(in->text) - 1) {
            in->text[in->text_len++] = *s++;
        }
        in->text[in->text_len] = 0;
        break;
    }
    default:
        break;
    }
}
