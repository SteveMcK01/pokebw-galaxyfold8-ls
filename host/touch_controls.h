/*
 * host/touch_controls.h -- Native Android OpenGL ES 3.0 on-screen virtual DS
 * controller (D-pad, A/B/X/Y, L/R, START/SELECT, and a visibility toggle).
 *
 * Compiled on Android only; other targets receive inline no-ops.
 */

#ifndef EIDOS_HOST_TOUCH_CONTROLS_H
#define EIDOS_HOST_TOUCH_CONTROLS_H

#include <stdbool.h>
#include <SDL3/SDL.h>

#if defined(__ANDROID__)

void host_touch_on_event(const SDL_Event *ev);
int  host_touch_should_swallow_event(const SDL_Event *ev);
int  host_touch_override_mouse_state(Uint32 *buttons, float *x, float *y);
void host_touch_apply_keys(bool *keys, int numkeys);
void host_touch_render(SDL_Window *window);
void host_touch_gl_reset(void);

#else

static inline void host_touch_on_event(const SDL_Event *ev) { (void)ev; }
static inline int  host_touch_should_swallow_event(const SDL_Event *ev) { (void)ev; return 0; }
static inline int  host_touch_override_mouse_state(Uint32 *buttons, float *x, float *y)
{
    (void)buttons;
    (void)x;
    (void)y;
    return 0;
}
static inline void host_touch_apply_keys(bool *keys, int numkeys) { (void)keys; (void)numkeys; }
static inline void host_touch_render(SDL_Window *window) { (void)window; }
static inline void host_touch_gl_reset(void) {}

#endif

#endif /* EIDOS_HOST_TOUCH_CONTROLS_H */
