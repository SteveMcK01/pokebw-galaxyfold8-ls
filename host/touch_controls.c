/*
 * host/touch_controls.c -- Native Android OpenGL ES 3.0 on-screen virtual DS
 * controller for Galaxy Z Fold and Android phones.
 *
 * Maps virtual presses to the default SDL scancodes read by engine/platform
 * (k_default_keymap in engine/platform/platform.c), while leaving non-control
 * touches free to act as the Nintendo DS bottom-screen stylus.
 */

#if defined(__ANDROID__)

#include "touch_controls.h"
#include "imgui_overlay.h"
#include "pokebw_settings.h"

#include <GLES3/gl3.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum {
    VPAD_UP     = 1u << 0,
    VPAD_DOWN   = 1u << 1,
    VPAD_LEFT   = 1u << 2,
    VPAD_RIGHT  = 1u << 3,
    VPAD_A      = 1u << 4,
    VPAD_B      = 1u << 5,
    VPAD_X      = 1u << 6,
    VPAD_Y      = 1u << 7,
    VPAD_L      = 1u << 8,
    VPAD_R      = 1u << 9,
    VPAD_START  = 1u << 10,
    VPAD_SELECT = 1u << 11,
};

enum {
    FINGER_ROLE_STYLUS = 0,
    FINGER_ROLE_VPAD   = 1,
    FINGER_ROLE_TOGGLE = 2,
    FINGER_ROLE_MENU   = 3,
};

typedef struct {
    SDL_FingerID id;
    int active;
    int role;
    float nx;
    float ny;
} TouchFinger;

typedef struct {
    float cx, cy, r;
} CircleGeom;

typedef struct {
    float cx, cy, hw, hh;
} RectGeom;

typedef struct {
    float ww, wh;
    CircleGeom dpad;
    CircleGeom face[4]; /* 0:A, 1:B, 2:X, 3:Y */
    RectGeom l_btn;
    RectGeom r_btn;
    RectGeom select_btn;
    RectGeom start_btn;
    RectGeom toggle_btn;
    RectGeom menu_btn;
} TouchLayout;

typedef struct {
    float x, y;
    float r, g, b, a;
} TouchVertex;

#define MAX_FINGERS 10
#define MAX_VERTS   8192

static TouchFinger g_fingers[MAX_FINGERS];
static unsigned g_pad_mask = 0;
static int g_last_down_claimed = 0;
static int g_last_up_claimed = 0;

static float g_win_w = 1080.0f;
static float g_win_h = 1920.0f;
static SDL_Rect g_safe = { 0, 0, 1080, 1920 };

/* GLES3 rendering state */
static GLuint g_prog = 0;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;
static int g_gl_failed = 0;
static TouchVertex g_verts[MAX_VERTS];
static int g_vert_count = 0;

static void update_window_metrics(SDL_Window *win)
{
    int ww = 0, wh = 0;
    SDL_Rect safe;

    if (win == NULL) {
        return;
    }
    if (SDL_GetWindowSize(win, &ww, &wh) && ww > 0 && wh > 0) {
        g_win_w = (float)ww;
        g_win_h = (float)wh;
        if (SDL_GetWindowSafeArea(win, &safe) && safe.w > 0 && safe.h > 0) {
            g_safe = safe;
        } else {
            g_safe.x = 0;
            g_safe.y = 0;
            g_safe.w = ww;
            g_safe.h = wh;
        }
    }
}

static void compute_layout(TouchLayout *L)
{
    const PokebwSettings *cfg = host_settings_get();
    float sx = (float)g_safe.x;
    float sy = (float)g_safe.y;
    float sw = (float)g_safe.w;
    float sh = (float)g_safe.h;
    float u = sw < sh ? sw : sh;
    float sc = (float)cfg->touch_scale / 100.0f;
    float margin = u * 0.042f;
    float dpad_r;
    float btn_r;
    float face_step;
    float face_cx, face_cy;
    float pill_hw;
    float pill_hh;
    float sh_hw;
    float sh_hh;
    float tog_hw = u * 0.072f;
    float tog_hh = u * 0.028f;
    float top_sep = u * 0.082f;
    float center_x = sx + sw * 0.5f;
    float sys_sep;

    if (sc < 0.70f) sc = 0.70f;
    if (sc > 1.40f) sc = 1.40f;

    dpad_r = u * 0.160f * sc;
    btn_r = u * 0.068f * sc;
    face_step = btn_r * 1.98f;
    pill_hw = u * 0.088f * sc;
    pill_hh = u * 0.036f * sc;
    sh_hw = u * 0.095f * sc;
    sh_hh = u * 0.042f * sc;
    sys_sep = u * 0.125f * sc;

    L->ww = g_win_w;
    L->wh = g_win_h;

    /* D-Pad: bottom-left inside safe area */
    L->dpad.cx = sx + margin + dpad_r;
    L->dpad.cy = sy + sh - margin - dpad_r;
    L->dpad.r = dpad_r;

    /* A/B/X/Y diamond: bottom-right inside safe area (Nintendo DS layout) */
    face_cx = sx + sw - margin - face_step - btn_r;
    face_cy = sy + sh - margin - face_step - btn_r;
    L->face[0] = (CircleGeom){ face_cx + face_step, face_cy, btn_r }; /* A (right)  */
    L->face[1] = (CircleGeom){ face_cx, face_cy + face_step, btn_r }; /* B (bottom) */
    L->face[2] = (CircleGeom){ face_cx, face_cy - face_step, btn_r }; /* X (top)    */
    L->face[3] = (CircleGeom){ face_cx - face_step, face_cy, btn_r }; /* Y (left)   */

    /* L / R shoulders: top-left and top-right inside safe area */
    L->l_btn = (RectGeom){ sx + margin + sh_hw, sy + margin + sh_hh, sh_hw, sh_hh };
    L->r_btn = (RectGeom){ sx + sw - margin - sh_hw, sy + margin + sh_hh, sh_hw, sh_hh };

    /* SELECT / START: bottom-center */
    L->select_btn = (RectGeom){ center_x - sys_sep, sy + sh - margin - pill_hh, pill_hw, pill_hh };
    L->start_btn  = (RectGeom){ center_x + sys_sep, sy + sh - margin - pill_hh, pill_hw, pill_hh };

    /* Top-center Hide/Show toggle + ImGui Settings Menu pill */
    L->toggle_btn = (RectGeom){ center_x - top_sep, sy + margin + tog_hh, tog_hw, tog_hh };
    L->menu_btn   = (RectGeom){ center_x + top_sep, sy + margin + tog_hh, tog_hw, tog_hh };
}

static int hit_rect(const RectGeom *r, float x, float y, float pad)
{
    return x >= r->cx - r->hw - pad && x <= r->cx + r->hw + pad &&
           y >= r->cy - r->hh - pad && y <= r->cy + r->hh + pad;
}

static int hit_circle(const CircleGeom *c, float x, float y, float scale)
{
    float dx = x - c->cx;
    float dy = y - c->cy;
    float rr = c->r * scale;
    return dx * dx + dy * dy <= rr * rr;
}

static unsigned eval_dpad_dir(const CircleGeom *dpad, float x, float y)
{
    float dx = x - dpad->cx;
    float dy = y - dpad->cy;
    float dist = sqrtf(dx * dx + dy * dy);
    unsigned mask = 0;

    if (dist < dpad->r * 0.20f) {
        return 0;
    }
    /* Allow clean 8-way diagonals with slight deadzone bias */
    if (dy < -dist * 0.36f) mask |= VPAD_UP;
    if (dy >  dist * 0.36f) mask |= VPAD_DOWN;
    if (dx < -dist * 0.36f) mask |= VPAD_LEFT;
    if (dx >  dist * 0.36f) mask |= VPAD_RIGHT;
    return mask;
}

static int point_hits_vpad(const TouchLayout *L, float x, float y)
{
    int i;

    if (hit_circle(&L->dpad, x, y, 1.25f)) {
        return 1;
    }
    for (i = 0; i < 4; i++) {
        if (hit_circle(&L->face[i], x, y, 1.20f)) {
            return 1;
        }
    }
    if (hit_rect(&L->l_btn, x, y, 10.0f) || hit_rect(&L->r_btn, x, y, 10.0f) ||
        hit_rect(&L->select_btn, x, y, 10.0f) || hit_rect(&L->start_btn, x, y, 10.0f)) {
        return 1;
    }
    return 0;
}

static void recompute_pad_mask(void)
{
    TouchLayout L;
    unsigned mask = 0;
    int i;

    if (!host_settings_get()->touch_visible || host_imgui_is_open()) {
        g_pad_mask = 0;
        return;
    }
    compute_layout(&L);

    for (i = 0; i < MAX_FINGERS; i++) {
        float x, y;
        int j;
        static const unsigned face_bits[4] = { VPAD_A, VPAD_B, VPAD_X, VPAD_Y };

        if (!g_fingers[i].active || g_fingers[i].role != FINGER_ROLE_VPAD) {
            continue;
        }
        x = g_fingers[i].nx * L.ww;
        y = g_fingers[i].ny * L.wh;

        if (hit_circle(&L.dpad, x, y, 1.35f)) {
            mask |= eval_dpad_dir(&L.dpad, x, y);
        }
        for (j = 0; j < 4; j++) {
            if (hit_circle(&L.face[j], x, y, 1.25f)) {
                mask |= face_bits[j];
            }
        }
        if (hit_rect(&L.l_btn, x, y, 12.0f))      mask |= VPAD_L;
        if (hit_rect(&L.r_btn, x, y, 12.0f))      mask |= VPAD_R;
        if (hit_rect(&L.select_btn, x, y, 12.0f)) mask |= VPAD_SELECT;
        if (hit_rect(&L.start_btn, x, y, 12.0f))  mask |= VPAD_START;
    }
    g_pad_mask = mask;
}

static int stylus_finger_index(void)
{
    int i;
    for (i = 0; i < MAX_FINGERS; i++) {
        if (g_fingers[i].active && g_fingers[i].role == FINGER_ROLE_STYLUS) {
            return i;
        }
    }
    return -1;
}

static int claimed_finger_count(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_FINGERS; i++) {
        if (g_fingers[i].active && g_fingers[i].role != FINGER_ROLE_STYLUS) {
            n++;
        }
    }
    return n;
}

void host_touch_on_event(const SDL_Event *ev)
{
    TouchLayout L;
    int i, slot = -1;
    float x, y;

    if (ev == NULL) {
        return;
    }
    switch (ev->type) {
    case SDL_EVENT_FINGER_DOWN: {
        SDL_Window *w = SDL_GetWindowFromID(ev->tfinger.windowID);
        if (w != NULL) {
            update_window_metrics(w);
        }
        compute_layout(&L);
        x = ev->tfinger.x * L.ww;
        y = ev->tfinger.y * L.wh;

        for (i = 0; i < MAX_FINGERS; i++) {
            if (g_fingers[i].active && g_fingers[i].id == ev->tfinger.fingerID) {
                slot = i;
                break;
            }
            if (!g_fingers[i].active && slot < 0) {
                slot = i;
            }
        }
        if (slot < 0) {
            return;
        }
        g_fingers[slot].id = ev->tfinger.fingerID;
        g_fingers[slot].active = 1;
        g_fingers[slot].nx = ev->tfinger.x;
        g_fingers[slot].ny = ev->tfinger.y;

        if (hit_rect(&L.menu_btn, x, y, 12.0f)) {
            g_fingers[slot].role = FINGER_ROLE_MENU;
            host_imgui_toggle();
            g_last_down_claimed = 1;
        } else if (host_imgui_is_open()) {
            g_fingers[slot].role = FINGER_ROLE_STYLUS;
            g_last_down_claimed = 0;
        } else if (hit_rect(&L.toggle_btn, x, y, 12.0f)) {
            PokebwSettings *cfg = host_settings_get();
            g_fingers[slot].role = FINGER_ROLE_TOGGLE;
            cfg->touch_visible = !cfg->touch_visible;
            host_settings_save();
            g_last_down_claimed = 1;
        } else if (host_settings_get()->touch_visible && point_hits_vpad(&L, x, y)) {
            g_fingers[slot].role = FINGER_ROLE_VPAD;
            g_last_down_claimed = 1;
        } else {
            g_fingers[slot].role = FINGER_ROLE_STYLUS;
            g_last_down_claimed = 0;
        }
        recompute_pad_mask();
        break;
    }
    case SDL_EVENT_FINGER_MOTION:
        for (i = 0; i < MAX_FINGERS; i++) {
            if (g_fingers[i].active && g_fingers[i].id == ev->tfinger.fingerID) {
                g_fingers[i].nx = ev->tfinger.x;
                g_fingers[i].ny = ev->tfinger.y;
                if (g_fingers[i].role == FINGER_ROLE_VPAD) {
                    recompute_pad_mask();
                }
                break;
            }
        }
        break;
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        for (i = 0; i < MAX_FINGERS; i++) {
            if (g_fingers[i].active && g_fingers[i].id == ev->tfinger.fingerID) {
                g_last_up_claimed = (g_fingers[i].role != FINGER_ROLE_STYLUS);
                g_fingers[i].active = 0;
                recompute_pad_mask();
                break;
            }
        }
        break;
    default:
        break;
    }
}

int host_touch_should_swallow_event(const SDL_Event *ev)
{
    TouchLayout L;

    if (ev == NULL) {
        return 0;
    }
    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev->button.which == SDL_TOUCH_MOUSEID) {
        if (g_last_down_claimed) {
            g_last_down_claimed = 0;
            return 1;
        }
        compute_layout(&L);
        if (hit_rect(&L.menu_btn, ev->button.x, ev->button.y, 12.0f) ||
            hit_rect(&L.toggle_btn, ev->button.x, ev->button.y, 12.0f) ||
            (host_settings_get()->touch_visible && point_hits_vpad(&L, ev->button.x, ev->button.y))) {
            return 1;
        }
    } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP && ev->button.which == SDL_TOUCH_MOUSEID) {
        if (g_last_up_claimed && stylus_finger_index() < 0) {
            g_last_up_claimed = 0;
            return 1;
        }
    } else if (ev->type == SDL_EVENT_MOUSE_MOTION && ev->motion.which == SDL_TOUCH_MOUSEID) {
        if (stylus_finger_index() < 0 && claimed_finger_count() > 0) {
            return 1;
        }
    }
    return 0;
}

int host_touch_override_mouse_state(Uint32 *buttons, float *x, float *y)
{
    int stylus;

    if (host_imgui_is_open()) {
        if (buttons != NULL) *buttons = 0;
        if (x != NULL) *x = 0.0f;
        if (y != NULL) *y = 0.0f;
        return 1;
    }
    if (claimed_finger_count() == 0) {
        return 0;
    }
    stylus = stylus_finger_index();
    if (stylus >= 0) {
        if (buttons != NULL) {
            *buttons = SDL_BUTTON_LMASK;
        }
        if (x != NULL) {
            *x = g_fingers[stylus].nx * g_win_w;
        }
        if (y != NULL) {
            *y = g_fingers[stylus].ny * g_win_h;
        }
        return 1;
    }
    if (buttons != NULL) {
        *buttons = 0;
    }
    if (x != NULL) {
        *x = 0.0f;
    }
    if (y != NULL) {
        *y = 0.0f;
    }
    return 1;
}

void host_touch_apply_keys(bool *keys, int numkeys)
{
    unsigned m = g_pad_mask;

    if (keys == NULL || m == 0 || host_imgui_is_open() || !host_settings_get()->touch_visible) {
        return;
    }
    if ((m & VPAD_UP)     && SDL_SCANCODE_UP        < numkeys) keys[SDL_SCANCODE_UP] = true;
    if ((m & VPAD_DOWN)   && SDL_SCANCODE_DOWN      < numkeys) keys[SDL_SCANCODE_DOWN] = true;
    if ((m & VPAD_LEFT)   && SDL_SCANCODE_LEFT      < numkeys) keys[SDL_SCANCODE_LEFT] = true;
    if ((m & VPAD_RIGHT)  && SDL_SCANCODE_RIGHT     < numkeys) keys[SDL_SCANCODE_RIGHT] = true;
    if ((m & VPAD_A)      && SDL_SCANCODE_X         < numkeys) keys[SDL_SCANCODE_X] = true;
    if ((m & VPAD_B)      && SDL_SCANCODE_Z         < numkeys) keys[SDL_SCANCODE_Z] = true;
    if ((m & VPAD_X)      && SDL_SCANCODE_S         < numkeys) keys[SDL_SCANCODE_S] = true;
    if ((m & VPAD_Y)      && SDL_SCANCODE_A         < numkeys) keys[SDL_SCANCODE_A] = true;
    if ((m & VPAD_L)      && SDL_SCANCODE_Q         < numkeys) keys[SDL_SCANCODE_Q] = true;
    if ((m & VPAD_R)      && SDL_SCANCODE_E         < numkeys) keys[SDL_SCANCODE_E] = true;
    if ((m & VPAD_START)  && SDL_SCANCODE_RETURN    < numkeys) keys[SDL_SCANCODE_RETURN] = true;
    if ((m & VPAD_SELECT) && SDL_SCANCODE_BACKSPACE < numkeys) keys[SDL_SCANCODE_BACKSPACE] = true;
}

/* ------------------------------------------------------------------ */
/* OpenGL ES 3.0 2D geometry batching                                  */
/* ------------------------------------------------------------------ */

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "eidos: touch shader compile error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static int ensure_gl(void)
{
    static const char *vs_src =
        "#version 300 es\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec4 aColor;\n"
        "out vec4 vColor;\n"
        "void main() {\n"
        "    vColor = aColor;\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";
    static const char *fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec4 vColor;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = vColor;\n"
        "}\n";
    GLuint vs, fs;
    GLint ok = 0;

    if (g_gl_failed) {
        return 0;
    }
    if (g_prog != 0) {
        return 1;
    }
    vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        g_gl_failed = 1;
        return 0;
    }
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(g_prog);
        g_prog = 0;
        g_gl_failed = 1;
        return 0;
    }
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TouchVertex),
                          (const void *)offsetof(TouchVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(TouchVertex),
                          (const void *)offsetof(TouchVertex, r));
    return 1;
}

void host_touch_gl_reset(void)
{
    if (g_vbo != 0) { glDeleteBuffers(1, &g_vbo); g_vbo = 0; }
    if (g_vao != 0) { glDeleteVertexArrays(1, &g_vao); g_vao = 0; }
    if (g_prog != 0) { glDeleteProgram(g_prog); g_prog = 0; }
    g_gl_failed = 0;
}

static void push_tri(float x0, float y0, float x1, float y1, float x2, float y2,
                     float r, float g, float b, float a)
{
    if (g_vert_count + 3 > MAX_VERTS) {
        return;
    }
    g_verts[g_vert_count++] = (TouchVertex){ (x0 / g_win_w) * 2.0f - 1.0f, 1.0f - (y0 / g_win_h) * 2.0f, r, g, b, a };
    g_verts[g_vert_count++] = (TouchVertex){ (x1 / g_win_w) * 2.0f - 1.0f, 1.0f - (y1 / g_win_h) * 2.0f, r, g, b, a };
    g_verts[g_vert_count++] = (TouchVertex){ (x2 / g_win_w) * 2.0f - 1.0f, 1.0f - (y2 / g_win_h) * 2.0f, r, g, b, a };
}

static void push_quad(float x0, float y0, float x1, float y1,
                      float x2, float y2, float x3, float y3,
                      float r, float g, float b, float a)
{
    push_tri(x0, y0, x1, y1, x2, y2, r, g, b, a);
    push_tri(x0, y0, x2, y2, x3, y3, r, g, b, a);
}

static void push_line(float x0, float y0, float x1, float y1, float thick,
                      float r, float g, float b, float a)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    float nx, ny;

    if (len < 0.001f) {
        return;
    }
    nx = (-dy / len) * (thick * 0.5f);
    ny = ( dx / len) * (thick * 0.5f);
    push_quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny,
              x1 - nx, y1 - ny, x0 - nx, y0 - ny, r, g, b, a);
}

static void push_circle(float cx, float cy, float rad,
                        float r, float g, float b, float a)
{
    enum { SEGS = 24 };
    int i;

    for (i = 0; i < SEGS; i++) {
        float a0 = (float)i * (6.2831853f / SEGS);
        float a1 = (float)(i + 1) * (6.2831853f / SEGS);
        push_tri(cx, cy,
                 cx + cosf(a0) * rad, cy + sinf(a0) * rad,
                 cx + cosf(a1) * rad, cy + sinf(a1) * rad,
                 r, g, b, a);
    }
}

static void push_ring(float cx, float cy, float rad, float thick,
                      float r, float g, float b, float a)
{
    enum { SEGS = 28 };
    float r_in = rad - thick * 0.5f;
    float r_out = rad + thick * 0.5f;
    int i;

    for (i = 0; i < SEGS; i++) {
        float a0 = (float)i * (6.2831853f / SEGS);
        float a1 = (float)(i + 1) * (6.2831853f / SEGS);
        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);
        push_quad(cx + c0 * r_in,  cy + s0 * r_in,
                  cx + c0 * r_out, cy + s0 * r_out,
                  cx + c1 * r_out, cy + s1 * r_out,
                  cx + c1 * r_in,  cy + s1 * r_in,
                  r, g, b, a);
    }
}

static void push_pill(const RectGeom *rc, float cr, float thick,
                      float fr, float fg, float fb, float fa,
                      float br, float bg, float bb, float ba)
{
    float x0 = rc->cx - rc->hw;
    float y0 = rc->cy - rc->hh;
    float x1 = rc->cx + rc->hw;
    float y1 = rc->cy + rc->hh;
    float rad = cr < rc->hh ? cr : rc->hh;

    /* Fill */
    push_quad(x0 + rad, y0, x1 - rad, y0, x1 - rad, y1, x0 + rad, y1, fr, fg, fb, fa);
    push_quad(x0, y0 + rad, x0 + rad, y0 + rad, x0 + rad, y1 - rad, x0, y1 - rad, fr, fg, fb, fa);
    push_quad(x1 - rad, y0 + rad, x1, y0 + rad, x1, y1 - rad, x1 - rad, y1 - rad, fr, fg, fb, fa);
    push_circle(x0 + rad, y0 + rad, rad, fr, fg, fb, fa);
    push_circle(x1 - rad, y0 + rad, rad, fr, fg, fb, fa);
    push_circle(x0 + rad, y1 - rad, rad, fr, fg, fb, fa);
    push_circle(x1 - rad, y1 - rad, rad, fr, fg, fb, fa);

    /* Outline */
    push_line(x0 + rad, y0, x1 - rad, y0, thick, br, bg, bb, ba);
    push_line(x0 + rad, y1, x1 - rad, y1, thick, br, bg, bb, ba);
    push_line(x0, y0 + rad, x0, y1 - rad, thick, br, bg, bb, ba);
    push_line(x1, y0 + rad, x1, y1 - rad, thick, br, bg, bb, ba);
}

static void draw_glyph(char ch, float cx, float cy, float h, float t,
                       float r, float g, float b, float a)
{
    float w = h * 0.62f;
    float l = cx - w * 0.5f, ri = cx + w * 0.5f;
    float tp = cy - h * 0.5f, bt = cy + h * 0.5f;

    switch (ch) {
    case 'A':
        push_line(l, bt, cx, tp, t, r, g, b, a);
        push_line(cx, tp, ri, bt, t, r, g, b, a);
        push_line(l + w * 0.20f, cy + h * 0.12f, ri - w * 0.20f, cy + h * 0.12f, t, r, g, b, a);
        break;
    case 'B':
        push_line(l, tp, l, bt, t, r, g, b, a);
        push_line(l, tp, cx + w * 0.15f, tp, t, r, g, b, a);
        push_line(cx + w * 0.15f, tp, ri, tp + h * 0.22f, t, r, g, b, a);
        push_line(ri, tp + h * 0.22f, cx + w * 0.10f, cy, t, r, g, b, a);
        push_line(l, cy, cx + w * 0.15f, cy, t, r, g, b, a);
        push_line(cx + w * 0.15f, cy, ri, cy + h * 0.25f, t, r, g, b, a);
        push_line(ri, cy + h * 0.25f, cx + w * 0.15f, bt, t, r, g, b, a);
        push_line(l, bt, cx + w * 0.15f, bt, t, r, g, b, a);
        break;
    case 'X':
        push_line(l, tp, ri, bt, t, r, g, b, a);
        push_line(ri, tp, l, bt, t, r, g, b, a);
        break;
    case 'Y':
        push_line(l, tp, cx, cy, t, r, g, b, a);
        push_line(ri, tp, cx, cy, t, r, g, b, a);
        push_line(cx, cy, cx, bt, t, r, g, b, a);
        break;
    case 'L':
        push_line(l, tp, l, bt, t, r, g, b, a);
        push_line(l, bt, ri, bt, t, r, g, b, a);
        break;
    case 'R':
        push_line(l, tp, l, bt, t, r, g, b, a);
        push_line(l, tp, ri - w * 0.1f, tp, t, r, g, b, a);
        push_line(ri - w * 0.1f, tp, ri, tp + h * 0.22f, t, r, g, b, a);
        push_line(ri, tp + h * 0.22f, l, cy, t, r, g, b, a);
        push_line(cx, cy, ri, bt, t, r, g, b, a);
        break;
    case 'S':
        push_line(ri, tp, l, tp, t, r, g, b, a);
        push_line(l, tp, l, cy, t, r, g, b, a);
        push_line(l, cy, ri, cy, t, r, g, b, a);
        push_line(ri, cy, ri, bt, t, r, g, b, a);
        push_line(ri, bt, l, bt, t, r, g, b, a);
        break;
    case 'T':
        push_line(l, tp, ri, tp, t, r, g, b, a);
        push_line(cx, tp, cx, bt, t, r, g, b, a);
        break;
    case 'E':
        push_line(l, tp, l, bt, t, r, g, b, a);
        push_line(l, tp, ri, tp, t, r, g, b, a);
        push_line(l, cy, ri - w * 0.15f, cy, t, r, g, b, a);
        push_line(l, bt, ri, bt, t, r, g, b, a);
        break;
    case 'P':
        push_line(l, tp, l, bt, t, r, g, b, a);
        push_line(l, tp, ri, tp, t, r, g, b, a);
        push_line(ri, tp, ri, cy, t, r, g, b, a);
        push_line(ri, cy, l, cy, t, r, g, b, a);
        break;
    case 'D':
        push_line(l, tp, l, bt, t, r, g, b, a);
        push_line(l, tp, cx + w * 0.15f, tp, t, r, g, b, a);
        push_line(cx + w * 0.15f, tp, ri, cy, t, r, g, b, a);
        push_line(ri, cy, cx + w * 0.15f, bt, t, r, g, b, a);
        push_line(cx + w * 0.15f, bt, l, bt, t, r, g, b, a);
        break;
    case 'M':
        push_line(l, bt, l, tp, t, r, g, b, a);
        push_line(l, tp, cx, cy, t, r, g, b, a);
        push_line(cx, cy, ri, tp, t, r, g, b, a);
        push_line(ri, tp, ri, bt, t, r, g, b, a);
        break;
    case 'N':
        push_line(l, bt, l, tp, t, r, g, b, a);
        push_line(l, tp, ri, bt, t, r, g, b, a);
        push_line(ri, bt, ri, tp, t, r, g, b, a);
        break;
    case 'U':
        push_line(l, tp, l, bt, t, r, g, b, a);
        push_line(l, bt, ri, bt, t, r, g, b, a);
        push_line(ri, bt, ri, tp, t, r, g, b, a);
        break;
    case '^':
        push_line(l, cy + h * 0.2f, cx, tp + h * 0.1f, t, r, g, b, a);
        push_line(cx, tp + h * 0.1f, ri, cy + h * 0.2f, t, r, g, b, a);
        break;
    case 'v':
        push_line(l, cy - h * 0.2f, cx, bt - h * 0.1f, t, r, g, b, a);
        push_line(cx, bt - h * 0.1f, ri, cy - h * 0.2f, t, r, g, b, a);
        break;
    case '<':
        push_line(ri - w * 0.1f, tp, l + w * 0.1f, cy, t, r, g, b, a);
        push_line(l + w * 0.1f, cy, ri - w * 0.1f, bt, t, r, g, b, a);
        break;
    case '>':
        push_line(l + w * 0.1f, tp, ri - w * 0.1f, cy, t, r, g, b, a);
        push_line(ri - w * 0.1f, cy, l + w * 0.1f, bt, t, r, g, b, a);
        break;
    default:
        break;
    }
}

static void draw_label(const char *s, float cx, float cy, float h, float t,
                       float r, float g, float b, float a)
{
    size_t n = strlen(s);
    float step = h * 0.85f;
    float start_x = cx - ((float)(n - 1) * step) * 0.5f;
    size_t i;

    for (i = 0; i < n; i++) {
        draw_glyph(s[i], start_x + (float)i * step, cy, h, t, r, g, b, a);
    }
}

static void build_geometry(const TouchLayout *L)
{
    const PokebwSettings *cfg = host_settings_get();
    float thick = fmaxf(2.0f, L->ww < L->wh ? L->ww * 0.0028f : L->wh * 0.0028f);
    unsigned m = g_pad_mask;
    float op = (float)cfg->touch_opacity / 100.0f;
    int menu_open = host_imgui_is_open();

    if (op < 0.15f) op = 0.15f;
    if (op > 1.00f) op = 1.00f;

    g_vert_count = 0;

    /* Always draw the small top-center PAD and MENU pills */
    {
        float fa = cfg->touch_visible ? 0.30f : 0.18f;
        float ba = cfg->touch_visible ? 0.65f : 0.45f;
        float th = L->toggle_btn.hh * 0.95f;
        push_pill(&L->toggle_btn, L->toggle_btn.hh, thick * 0.85f,
                  0.10f, 0.12f, 0.16f, fa, 0.78f, 0.84f, 0.94f, ba);
        draw_label("PAD", L->toggle_btn.cx, L->toggle_btn.cy, th, thick * 0.9f,
                   0.92f, 0.95f, 1.0f, cfg->touch_visible ? 0.85f : 0.55f);

        push_pill(&L->menu_btn, L->menu_btn.hh, thick * 0.85f,
                  menu_open ? 0.28f : 0.10f, menu_open ? 0.50f : 0.12f, menu_open ? 0.80f : 0.16f,
                  menu_open ? 0.72f : 0.32f,
                  0.78f, 0.84f, 0.94f, menu_open ? 0.95f : 0.65f);
        draw_label("MENU", L->menu_btn.cx, L->menu_btn.cy, th * 0.88f, thick * 0.85f,
                   0.92f, 0.95f, 1.0f, menu_open ? 0.98f : 0.85f);
    }

    if (!cfg->touch_visible || menu_open) {
        return;
    }

    /* D-Pad base + 4 arms */
    {
        float cx = L->dpad.cx;
        float cy = L->dpad.cy;
        float r = L->dpad.r;
        float arm_w = r * 0.36f;
        float arm_len = r * 0.92f;
        float inner = r * 0.28f;
        float ch_sz = r * 0.22f;
        RectGeom up    = { cx, cy - (inner + arm_len) * 0.5f, arm_w, (arm_len - inner) * 0.5f };
        RectGeom down  = { cx, cy + (inner + arm_len) * 0.5f, arm_w, (arm_len - inner) * 0.5f };
        RectGeom left  = { cx - (inner + arm_len) * 0.5f, cy, (arm_len - inner) * 0.5f, arm_w };
        RectGeom right = { cx + (inner + arm_len) * 0.5f, cy, (arm_len - inner) * 0.5f, arm_w };
        RectGeom hub   = { cx, cy, arm_w, arm_w };

        push_circle(cx, cy, r * 1.04f, 0.06f, 0.08f, 0.11f, 0.24f * op);
        push_ring(cx, cy, r * 1.04f, thick, 0.65f, 0.72f, 0.84f, 0.35f * op);

        push_pill(&hub, 4.0f, 0.0f, 0.14f, 0.17f, 0.22f, 0.36f * op, 0, 0, 0, 0);

        push_pill(&up, arm_w * 0.35f, thick,
                  (m & VPAD_UP) ? 0.35f : 0.14f, (m & VPAD_UP) ? 0.65f : 0.17f, (m & VPAD_UP) ? 0.95f : 0.22f,
                  ((m & VPAD_UP) ? 0.68f : 0.38f) * op, 0.75f, 0.82f, 0.94f, ((m & VPAD_UP) ? 0.90f : 0.52f) * op);
        push_pill(&down, arm_w * 0.35f, thick,
                  (m & VPAD_DOWN) ? 0.35f : 0.14f, (m & VPAD_DOWN) ? 0.65f : 0.17f, (m & VPAD_DOWN) ? 0.95f : 0.22f,
                  ((m & VPAD_DOWN) ? 0.68f : 0.38f) * op, 0.75f, 0.82f, 0.94f, ((m & VPAD_DOWN) ? 0.90f : 0.52f) * op);
        push_pill(&left, arm_w * 0.35f, thick,
                  (m & VPAD_LEFT) ? 0.35f : 0.14f, (m & VPAD_LEFT) ? 0.65f : 0.17f, (m & VPAD_LEFT) ? 0.95f : 0.22f,
                  ((m & VPAD_LEFT) ? 0.68f : 0.38f) * op, 0.75f, 0.82f, 0.94f, ((m & VPAD_LEFT) ? 0.90f : 0.52f) * op);
        push_pill(&right, arm_w * 0.35f, thick,
                  (m & VPAD_RIGHT) ? 0.35f : 0.14f, (m & VPAD_RIGHT) ? 0.65f : 0.17f, (m & VPAD_RIGHT) ? 0.95f : 0.22f,
                  ((m & VPAD_RIGHT) ? 0.68f : 0.38f) * op, 0.75f, 0.82f, 0.94f, ((m & VPAD_RIGHT) ? 0.90f : 0.52f) * op);

        draw_glyph('^', up.cx,    up.cy,    ch_sz, thick * 1.1f, 0.92f, 0.96f, 1.0f, ((m & VPAD_UP)    ? 0.95f : 0.68f) * op);
        draw_glyph('v', down.cx,  down.cy,  ch_sz, thick * 1.1f, 0.92f, 0.96f, 1.0f, ((m & VPAD_DOWN)  ? 0.95f : 0.68f) * op);
        draw_glyph('<', left.cx,  left.cy,  ch_sz, thick * 1.1f, 0.92f, 0.96f, 1.0f, ((m & VPAD_LEFT)  ? 0.95f : 0.68f) * op);
        draw_glyph('>', right.cx, right.cy, ch_sz, thick * 1.1f, 0.92f, 0.96f, 1.0f, ((m & VPAD_RIGHT) ? 0.95f : 0.68f) * op);
    }

    /* Face buttons: A, B, X, Y */
    {
        static const unsigned bits[4] = { VPAD_A, VPAD_B, VPAD_X, VPAD_Y };
        static const char labels[4] = { 'A', 'B', 'X', 'Y' };
        static const float cols[4][3] = {
            { 0.24f, 0.82f, 0.52f }, /* A: green */
            { 0.92f, 0.32f, 0.36f }, /* B: red   */
            { 0.32f, 0.62f, 0.96f }, /* X: blue  */
            { 0.92f, 0.74f, 0.24f }, /* Y: gold  */
        };
        int i;

        for (i = 0; i < 4; i++) {
            int pressed = (m & bits[i]) != 0;
            const CircleGeom *c = &L->face[i];
            float fa = (pressed ? 0.65f : 0.28f) * op;
            float ba = (pressed ? 0.95f : 0.58f) * op;
            push_circle(c->cx, c->cy, c->r, cols[i][0], cols[i][1], cols[i][2], fa);
            push_ring(c->cx, c->cy, c->r, thick * 1.15f, cols[i][0], cols[i][1], cols[i][2], ba);
            draw_glyph(labels[i], c->cx, c->cy, c->r * 0.72f, thick * 1.15f,
                       0.96f, 0.98f, 1.0f, (pressed ? 0.98f : 0.80f) * op);
        }
    }

    /* Shoulder & System pills: L, R, SEL, STA */
    {
        struct {
            const RectGeom *rc;
            unsigned bit;
            const char *txt;
        } pills[4] = {
            { &L->l_btn,      VPAD_L,      "L"   },
            { &L->r_btn,      VPAD_R,      "R"   },
            { &L->select_btn, VPAD_SELECT, "SEL" },
            { &L->start_btn,  VPAD_START,  "STA" },
        };
        int i;

        for (i = 0; i < 4; i++) {
            int pressed = (m & pills[i].bit) != 0;
            const RectGeom *rc = pills[i].rc;
            push_pill(rc, rc->hh, thick,
                      pressed ? 0.35f : 0.12f, pressed ? 0.62f : 0.15f, pressed ? 0.95f : 0.20f,
                      (pressed ? 0.68f : 0.34f) * op,
                      0.78f, 0.85f, 0.96f, (pressed ? 0.95f : 0.56f) * op);
            draw_label(pills[i].txt, rc->cx, rc->cy, rc->hh * 0.92f, thick,
                       0.94f, 0.96f, 1.0f, (pressed ? 0.98f : 0.78f) * op);
        }
    }
}

void host_touch_render(SDL_Window *window)
{
    TouchLayout L;
    int pw = 0, ph = 0;
    GLint prev_fbo = 0, prev_prog = 0, prev_vao = 0, prev_vbo = 0, prev_vp[4] = { 0 };
    GLint prev_src_rgb = 0, prev_dst_rgb = 0, prev_src_a = 0, prev_dst_a = 0;
    GLboolean prev_blend, prev_depth, prev_cull, prev_scissor;

    if (window == NULL || SDL_GL_GetCurrentContext() == NULL) {
        return;
    }
    update_window_metrics(window);
    if (!SDL_GetWindowSizeInPixels(window, &pw, &ph) || pw <= 0 || ph <= 0) {
        return;
    }
    if (!ensure_gl()) {
        return;
    }

    compute_layout(&L);
    build_geometry(&L);
    if (g_vert_count <= 0) {
        return;
    }

    /* Save GLES3 state */
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_vbo);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prev_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &prev_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prev_src_a);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prev_dst_a);
    prev_blend   = glIsEnabled(GL_BLEND);
    prev_depth   = glIsEnabled(GL_DEPTH_TEST);
    prev_cull    = glIsEnabled(GL_CULL_FACE);
    prev_scissor = glIsEnabled(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, pw, ph);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)g_vert_count * sizeof(TouchVertex)),
                 g_verts, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, g_vert_count);

    /* Restore GLES3 state */
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev_vbo);
    glBindVertexArray((GLuint)prev_vao);
    glUseProgram((GLuint)prev_prog);
    glBlendFuncSeparate((GLenum)prev_src_rgb, (GLenum)prev_dst_rgb,
                        (GLenum)prev_src_a, (GLenum)prev_dst_a);
    if (!prev_blend)  glDisable(GL_BLEND);
    if (prev_depth)   glEnable(GL_DEPTH_TEST);
    if (prev_cull)    glEnable(GL_CULL_FACE);
    if (prev_scissor) glEnable(GL_SCISSOR_TEST);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
}

#endif /* __ANDROID__ */
