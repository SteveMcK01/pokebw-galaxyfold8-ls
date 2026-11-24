/*
 * host/imgui_overlay.h -- Dear ImGui touch-friendly settings overlay for
 * PokeBW Fold (Android GLES 3.0 + SDL3).
 */

#ifndef EIDOS_HOST_IMGUI_OVERLAY_H
#define EIDOS_HOST_IMGUI_OVERLAY_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(HOST_IMGUI)

void host_imgui_toggle(void);
void host_imgui_set_open(int open);
int  host_imgui_is_open(void);
int  host_imgui_process_event(const SDL_Event *ev);
void host_imgui_render(SDL_Window *window);
void host_imgui_shutdown(void);

#else

static inline void host_imgui_toggle(void) {}
static inline void host_imgui_set_open(int open) { (void)open; }
static inline int  host_imgui_is_open(void) { return 0; }
static inline int  host_imgui_process_event(const SDL_Event *ev) { (void)ev; return 0; }
static inline void host_imgui_render(SDL_Window *window) { (void)window; }
static inline void host_imgui_shutdown(void) {}

#endif

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_HOST_IMGUI_OVERLAY_H */
