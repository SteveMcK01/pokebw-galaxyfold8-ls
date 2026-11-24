/*
 * host/imgui_overlay.cpp -- Dear ImGui v1.93.0 settings menu & FPS HUD over
 * SDL3 + OpenGL ES 3.0 on Android.
 */

#if defined(HOST_IMGUI)

#include "imgui_overlay.h"
#include "pokebw_settings.h"
#include "pokebw_settings_ids.h"

#include <GLES3/gl3.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

namespace {

int g_initialized = 0;
int g_open = 0;
float g_applied_scale = 0.0f;
ImGuiStyle g_base_style;

Uint64 g_last_ns = 0;
float g_fps = 60.0f;

void apply_theme(ImGuiStyle &s)
{
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 14.0f;
    s.ChildRounding = 10.0f;
    s.FrameRounding = 8.0f;
    s.PopupRounding = 8.0f;
    s.ScrollbarRounding = 10.0f;
    s.GrabRounding = 8.0f;
    s.TabRounding = 8.0f;
    s.WindowBorderSize = 1.5f;
    s.FrameBorderSize = 1.0f;
    s.WindowPadding = ImVec2(14.0f, 12.0f);
    s.FramePadding = ImVec2(12.0f, 9.0f);
    s.ItemSpacing = ImVec2(10.0f, 10.0f);
    s.ScrollbarSize = 24.0f;
    s.GrabMinSize = 22.0f;
    s.TouchExtraPadding = ImVec2(6.0f, 6.0f);

    ImVec4 *c = s.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.08f, 0.10f, 0.14f, 0.94f);
    c[ImGuiCol_Border]           = ImVec4(0.32f, 0.45f, 0.65f, 0.65f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.11f, 0.15f, 0.22f, 1.00f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.14f, 0.22f, 0.34f, 1.00f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.15f, 0.19f, 0.26f, 0.95f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.21f, 0.27f, 0.37f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.25f, 0.35f, 0.48f, 1.00f);
    c[ImGuiCol_CheckMark]        = ImVec4(0.36f, 0.85f, 0.64f, 1.00f);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.35f, 0.68f, 0.98f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.78f, 1.00f, 1.00f);
    c[ImGuiCol_Button]           = ImVec4(0.20f, 0.31f, 0.46f, 0.95f);
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.27f, 0.42f, 0.62f, 1.00f);
    c[ImGuiCol_ButtonActive]     = ImVec4(0.34f, 0.52f, 0.75f, 1.00f);
    c[ImGuiCol_Tab]              = ImVec4(0.13f, 0.18f, 0.26f, 0.95f);
    c[ImGuiCol_TabHovered]       = ImVec4(0.24f, 0.37f, 0.54f, 1.00f);
    c[ImGuiCol_TabSelected]      = ImVec4(0.22f, 0.40f, 0.62f, 1.00f);
    c[ImGuiCol_Header]           = ImVec4(0.20f, 0.32f, 0.48f, 0.90f);
}

void update_scale(SDL_Window *window)
{
    int ww = 0, wh = 0;
    if (!SDL_GetWindowSize(window, &ww, &wh) || ww <= 0 || wh <= 0) {
        return;
    }
    float short_side = static_cast<float>(std::min(ww, wh));
    float scale = std::clamp(short_side / 430.0f, 1.35f, 2.85f);
    if (std::fabs(scale - g_applied_scale) > 0.05f) {
        ImGuiStyle s = g_base_style;
        s.ScaleAllSizes(scale * 0.72f);
        s.FontScaleMain = scale;
        ImGui::GetStyle() = s;
        g_applied_scale = scale;
    }
}

bool ensure_init(SDL_Window *window)
{
    if (g_initialized) {
        return true;
    }
    SDL_GLContext ctx = SDL_GL_GetCurrentContext();
    if (window == nullptr || ctx == nullptr) {
        return false;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr; /* Keep CWD clean on Android */
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;

    apply_theme(g_base_style);
    ImGui::GetStyle() = g_base_style;

    if (!ImGui_ImplSDL3_InitForOpenGL(window, ctx)) {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    g_applied_scale = 0.0f;
    g_initialized = 1;
    return true;
}

void help_marker(const char *help)
{
    if (help != nullptr && help[0] != '\0') {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", help);
        }
    }
}

bool setting_checkbox(const char *label, const char *help, int *field, bool disabled = false)
{
    bool v = (*field != 0);
    if (disabled) {
        ImGui::BeginDisabled();
    }
    bool changed = ImGui::Checkbox(label, &v);
    if (disabled) {
        ImGui::EndDisabled();
    }
    if (changed) {
        *field = v ? 1 : 0;
        host_settings_save();
    }
    help_marker(help);
    return changed;
}

bool eidos_checkbox(const char *label, const char *help, int opt_id)
{
    bool v = (host_eidos_opt_get(opt_id) != 0);
    bool changed = ImGui::Checkbox(label, &v);
    if (changed) {
        host_eidos_opt_set(opt_id, v ? 1 : 0);
    }
    help_marker(help);
    return changed;
}

bool eidos_combo(const char *label, const char *help, int opt_id,
                 const char *const *items, int count)
{
    int v = host_eidos_opt_get(opt_id);
    if (v < 0 || v >= count) {
        v = 0;
    }
    bool changed = ImGui::Combo(label, &v, items, count);
    if (changed) {
        host_eidos_opt_set(opt_id, v);
    }
    help_marker(help);
    return changed;
}

bool eidos_slider(const char *label, const char *help, int opt_id, int min_v, int max_v)
{
    int v = std::clamp(host_eidos_opt_get(opt_id), min_v, max_v);
    bool changed = ImGui::SliderInt(label, &v, min_v, max_v);
    if (changed) {
        host_eidos_opt_set(opt_id, v);
    }
    help_marker(help);
    return changed;
}

void draw_fps_badge(const SDL_Rect &safe)
{
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(safe.x + 12),
                                   static_cast<float>(safe.y + 12)),
                            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##fps_badge", nullptr, flags)) {
        ImGui::Text("%.0f FPS", g_fps);
    }
    ImGui::End();
}

void draw_settings_modal(const SDL_Rect &safe, PokebwSettings *cfg)
{
    float win_w = std::min(static_cast<float>(safe.w) * 0.92f, 920.0f);
    float win_h = std::min(static_cast<float>(safe.h) * 0.84f, 980.0f);
    ImVec2 center(static_cast<float>(safe.x) + static_cast<float>(safe.w) * 0.5f,
                  static_cast<float>(safe.y) + static_cast<float>(safe.h) * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("PokeBW Fold Settings", nullptr, flags)) {
        float footer_h = ImGui::GetFrameHeightWithSpacing() * 2.2f;
        if (ImGui::BeginChild("##tabs_scroll", ImVec2(0.0f, -footer_h), false)) {
            if (ImGui::BeginTabBar("##pokebw_tabs")) {
                if (ImGui::BeginTabItem("Controls")) {
                    ImGui::Spacing();
                    setting_checkbox("Show On-Screen Controls", nullptr, &cfg->touch_visible);
                    if (ImGui::SliderInt("Opacity (%)", &cfg->touch_opacity, 15, 100)) {
                        host_settings_save();
                    }
                    if (ImGui::SliderInt("Button Scale (%)", &cfg->touch_scale, 70, 140)) {
                        host_settings_save();
                    }
                    ImGui::Separator();
                    setting_checkbox("Show FPS Counter", nullptr, &cfg->show_fps);
                    ImGui::Spacing();
                    if (ImGui::Button("Reset Controls")) {
                        host_settings_reset_controls();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Display & Audio")) {
                    static const char *const k_layouts[] = { "Stacked", "Side-by-Side", "One Screen" };
                    static const char *const k_themes[] = { "Kosmos", "Plasma", "Harmonia" };
                    static const char *const k_resolutions[] = { "Auto", "1x", "2x", "3x", "4x", "5x", "6x" };
                    static const char *const k_backdrops[] = { "Real Map", "Original" };

                    ImGui::Spacing();
                    eidos_combo("Screen Layout",
                                "One Screen uses eidos widescreen 3D + redesigned UI; Stacked/Side-by-Side show both DS screens.",
                                POKEBW_EIDOS_OPT_LAYOUT, k_layouts, 3);
                    int layout = host_eidos_opt_get(POKEBW_EIDOS_OPT_LAYOUT);
                    if (layout != 2) {
                        eidos_checkbox("Swap Screens", nullptr, POKEBW_EIDOS_OPT_SWAP);
                        eidos_slider("Screen Gap (px)", nullptr, POKEBW_EIDOS_OPT_GAP, 0, 64);
                    } else {
                        eidos_combo("Interface Style",
                                    "One-screen interface theme (Kosmos, Plasma, Harmonia).",
                                    POKEBW_EIDOS_OPT_UI_THEME, k_themes, 3);
                    }
                    ImGui::Separator();
                    eidos_combo("3D Resolution", nullptr, POKEBW_EIDOS_OPT_RESOLUTION, k_resolutions, 7);
                    eidos_checkbox("3D Outlines", "Thin crisp edge outlines on 3D models.", POKEBW_EIDOS_OPT_OUTLINES);
                    eidos_combo("Battle Backdrop",
                                "Real Map renders battles in the 3D overworld when using One Screen mode.",
                                POKEBW_EIDOS_OPT_BATTLE_BACKDROP, k_backdrops, 2);
                    eidos_checkbox("Battle Platforms", nullptr, POKEBW_EIDOS_OPT_BATTLE_PLATFORMS);
                    eidos_checkbox("Weather Haze", "Apply overworld fog/weather haze to Pokemon and trainers in battle.", POKEBW_EIDOS_OPT_WEATHER_HAZE);
                    eidos_checkbox("VSync", nullptr, POKEBW_EIDOS_OPT_VSYNC);
                    ImGui::Separator();
                    eidos_slider("Master Volume (%)", nullptr, POKEBW_EIDOS_OPT_VOLUME, 0, 100);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Gameplay")) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Phase 1 hooks (saved automatically):");
                    ImGui::Spacing();
                    setting_checkbox("Trade Evolution Fixes",
                                     "Evolve trade species at Lv. 36+/37+ or via level-up with held item.",
                                     &cfg->trade_evo_fix);
                    setting_checkbox("Modern EXP Share (Party-Wide)",
                                     "100% EXP to battle participants, 50% EXP to bench.",
                                     &cfg->modern_exp_share);
                    setting_checkbox("HM Without Teaching",
                                     "Use field moves from any compatible party member.",
                                     &cfg->hm_freedom);
                    ImGui::Indent();
                    setting_checkbox("HM Badge Bypass",
                                     "Allow field HMs even before obtaining the HM/TM item in your Bag.",
                                     &cfg->hm_no_badges, cfg->hm_freedom == 0);
                    ImGui::Unindent();
                    setting_checkbox("Gotta Catch 'Em All! Mode",
                                     "Unlocks version exclusives, National Dex spawns, and events.",
                                     &cfg->catch_em_all);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("QoL")) {
                    static const char *const k_transitions[] = { "Original", "Fast", "Seamless" };

                    ImGui::Spacing();
                    eidos_checkbox("Instant Text",
                                   "Print each dialogue page at once while keeping page-turn pauses.",
                                   POKEBW_EIDOS_OPT_INSTANT_TEXT);
                    eidos_combo("Map Transitions",
                                "Controls fade speed between doors and maps.",
                                POKEBW_EIDOS_OPT_TRANSITIONS, k_transitions, 3);
                    eidos_checkbox("Type-Colored Move Names",
                                   "Color move names by type inside battle messages.",
                                   POKEBW_EIDOS_OPT_MOVE_COLOURS);
                    ImGui::Separator();
                    ImGui::TextDisabled("Phase 2 QoL toggles:");
                    ImGui::Spacing();
                    setting_checkbox("Universal TM/HM Compatibility", nullptr, &cfg->universal_tm);
                    setting_checkbox("Hard Level Cap", nullptr, &cfg->hard_level_cap);
                    setting_checkbox("No Random Encounters", nullptr, &cfg->no_random_encounters);
                    setting_checkbox("Always Bite When Fishing", nullptr, &cfg->always_bite_fishing);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Reset All")) {
            host_settings_reset_defaults();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(-1.0f, 0.0f))) {
            g_open = 0;
        }
        ImGui::TextDisabled("Saved: %s", host_settings_path());
    }
    ImGui::End();
}

} // namespace

extern "C" {

void host_imgui_toggle(void)
{
    g_open = !g_open;
}

void host_imgui_set_open(int open)
{
    g_open = open ? 1 : 0;
}

int host_imgui_is_open(void)
{
    return g_open;
}

int host_imgui_process_event(const SDL_Event *ev)
{
    if (!g_open || !g_initialized || ev == nullptr) {
        return 0;
    }
    ImGui_ImplSDL3_ProcessEvent(ev);

    switch (ev->type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_CANCELED:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return 1;
    default:
        return 0;
    }
}

void host_imgui_render(SDL_Window *window)
{
    Uint64 now = SDL_GetTicksNS();
    if (g_last_ns != 0 && now > g_last_ns) {
        float dt = static_cast<float>(now - g_last_ns) * 1e-9f;
        if (dt > 0.0005f && dt < 0.5f) {
            g_fps = g_fps * 0.90f + (1.0f / dt) * 0.10f;
        }
    }
    g_last_ns = now;

    PokebwSettings *cfg = host_settings_get();
    if (!g_open && !cfg->show_fps) {
        return;
    }
    if (!ensure_init(window)) {
        return;
    }

    int ww = 0, wh = 0, pw = 0, ph = 0;
    if (!SDL_GetWindowSize(window, &ww, &wh) || ww <= 0 || wh <= 0) {
        return;
    }
    if (!SDL_GetWindowSizeInPixels(window, &pw, &ph) || pw <= 0 || ph <= 0) {
        return;
    }
    SDL_Rect safe;
    if (!SDL_GetWindowSafeArea(window, &safe) || safe.w <= 0 || safe.h <= 0) {
        safe = { 0, 0, ww, wh };
    }

    update_scale(window);

    GLint prev_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (cfg->show_fps) {
        draw_fps_badge(safe);
    }
    if (g_open) {
        draw_settings_modal(safe, cfg);
    }

    ImGui::Render();
    glViewport(0, 0, pw, ph);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
}

void host_imgui_shutdown(void)
{
    if (!g_initialized) {
        return;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    g_initialized = 0;
    g_applied_scale = 0.0f;
}

} // extern "C"

#endif /* HOST_IMGUI */
