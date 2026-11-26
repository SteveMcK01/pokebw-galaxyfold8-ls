/*
 * host/imgui_overlay.cpp -- Dear ImGui v1.93.0 settings menu & FPS HUD over
 * SDL3 + OpenGL ES 3.0 on Android.
 */

#if defined(HOST_IMGUI)

#include "crash_handler.h"
#include "imgui_overlay.h"
#include "pokebw_settings.h"
#include "pokebw_settings_ids.h"

#include <GLES3/gl3.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

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

const ImVec4 k_type_colors[18] = {
    ImVec4(0.67f, 0.64f, 0.58f, 1.0f), /* Normal */
    ImVec4(0.75f, 0.19f, 0.16f, 1.0f), /* Fighting */
    ImVec4(0.66f, 0.56f, 0.94f, 1.0f), /* Flying */
    ImVec4(0.63f, 0.25f, 0.63f, 1.0f), /* Poison */
    ImVec4(0.88f, 0.75f, 0.41f, 1.0f), /* Ground */
    ImVec4(0.72f, 0.63f, 0.22f, 1.0f), /* Rock */
    ImVec4(0.66f, 0.72f, 0.13f, 1.0f), /* Bug */
    ImVec4(0.44f, 0.35f, 0.60f, 1.0f), /* Ghost */
    ImVec4(0.72f, 0.72f, 0.82f, 1.0f), /* Steel */
    ImVec4(0.96f, 0.32f, 0.19f, 1.0f), /* Fire */
    ImVec4(0.41f, 0.56f, 0.94f, 1.0f), /* Water */
    ImVec4(0.47f, 0.78f, 0.31f, 1.0f), /* Grass */
    ImVec4(0.97f, 0.82f, 0.19f, 1.0f), /* Electric */
    ImVec4(0.97f, 0.35f, 0.53f, 1.0f), /* Psychic */
    ImVec4(0.60f, 0.85f, 0.85f, 1.0f), /* Ice */
    ImVec4(0.44f, 0.22f, 0.97f, 1.0f), /* Dragon */
    ImVec4(0.44f, 0.35f, 0.28f, 1.0f), /* Dark */
    ImVec4(0.32f, 0.32f, 0.32f, 1.0f)  /* ??? */
};

void format_status_badge(uint8_t status, const char **out_label, ImVec4 *out_col)
{
    /* Gen 5 party status bitmask: 0x07 sleep turns, 0x08 PSN, 0x10 BRN, 0x20 FRZ, 0x40 PAR, 0x80 TOX */
    if ((status & 0x07u) != 0u) {
        *out_label = "SLP";
        *out_col = ImVec4(0.65f, 0.65f, 0.72f, 1.0f);
    } else if ((status & 0x80u) != 0u) {
        *out_label = "TOX";
        *out_col = ImVec4(0.78f, 0.30f, 0.78f, 1.0f);
    } else if ((status & 0x08u) != 0u) {
        *out_label = "PSN";
        *out_col = ImVec4(0.72f, 0.34f, 0.72f, 1.0f);
    } else if ((status & 0x10u) != 0u) {
        *out_label = "BRN";
        *out_col = ImVec4(0.94f, 0.42f, 0.22f, 1.0f);
    } else if ((status & 0x20u) != 0u) {
        *out_label = "FRZ";
        *out_col = ImVec4(0.38f, 0.82f, 0.94f, 1.0f);
    } else if ((status & 0x40u) != 0u) {
        *out_label = "PAR";
        *out_col = ImVec4(0.95f, 0.80f, 0.20f, 1.0f);
    } else {
        *out_label = nullptr;
    }
}

static const char *const k_nature_names[25] = {
    "Hardy", "Lonely", "Brave", "Adamant", "Naughty",
    "Bold", "Docile", "Relaxed", "Impish", "Lax",
    "Timid", "Hasty", "Serious", "Jolly", "Naive",
    "Modest", "Mild", "Quiet", "Bashful", "Rash",
    "Calm", "Gentle", "Sassy", "Careful", "Quirky"
};

static const char *daycare_lang_tag(uint8_t lang)
{
    switch (lang) {
    case 1: return "JP";
    case 2: return "EN";
    case 3: return "FR";
    case 4: return "IT";
    case 5: return "DE";
    case 7: return "ES";
    case 8: return "KO";
    default: return "??";
    }
}

static const char *const k_type_names[18] = {
    "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock",
    "Bug", "Ghost", "Steel", "Fire", "Water", "Grass",
    "Electric", "Psychic", "Ice", "Dragon", "Dark", "???"
};

static const char *const k_egg_group_names[16] = {
    "None", "Monster", "Water 1", "Bug", "Flying", "Field", "Fairy", "Grass",
    "Human-Like", "Water 3", "Mineral", "Amorphous", "Water 2", "Ditto",
    "Dragon", "Undiscovered"
};

static bool dex_icontains(const char *haystack, const char *needle)
{
    if (needle[0] == '\0') {
        return true;
    }
    for (const char *h = haystack; *h != '\0'; h++) {
        const char *p = h;
        const char *q = needle;
        while (*p != '\0' && *q != '\0' &&
               std::tolower(static_cast<unsigned char>(*p)) ==
                   std::tolower(static_cast<unsigned char>(*q))) {
            p++;
            q++;
        }
        if (*q == '\0') {
            return true;
        }
    }
    return false;
}

static int dex_icmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        int ca = std::tolower(static_cast<unsigned char>(*a));
        int cb = std::tolower(static_cast<unsigned char>(*b));
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

static void draw_dex_type_badge(uint8_t type_id)
{
    int ti = type_id < 18u ? static_cast<int>(type_id) : 17;
    ImVec4 tc = k_type_colors[ti];
    ImVec4 fill(tc.x * 0.45f, tc.y * 0.45f, tc.z * 0.45f, 0.96f);
    ImVec4 border(tc.x * 0.90f, tc.y * 0.90f, tc.z * 0.90f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, fill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fill);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, fill);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::SmallButton(k_type_names[ti]);
    ImGui::PopStyleColor(4);
}

static void format_gender_ratio(uint8_t ratio, char *out, size_t cap)
{
    if (ratio == 255u) {
        std::snprintf(out, cap, "Genderless");
    } else if (ratio == 0u) {
        std::snprintf(out, cap, "100%% M");
    } else if (ratio == 254u) {
        std::snprintf(out, cap, "100%% F");
    } else if (ratio == 31u) {
        std::snprintf(out, cap, "87.5%% M / 12.5%% F");
    } else if (ratio == 63u) {
        std::snprintf(out, cap, "75%% M / 25%% F");
    } else if (ratio == 127u) {
        std::snprintf(out, cap, "50%% M / 50%% F");
    } else if (ratio == 191u) {
        std::snprintf(out, cap, "25%% M / 75%% F");
    } else if (ratio == 225u) {
        std::snprintf(out, cap, "12.5%% M / 87.5%% F");
    } else {
        double f_pct = (static_cast<double>(ratio) * 100.0) / 255.0;
        std::snprintf(out, cap, "%.0f%% M / %.0f%% F", 100.0 - f_pct, f_pct);
    }
}

static void format_dex_evo_desc(const PokebwDexEvo &e, bool trade_fix, char *out, size_t cap)
{
    const char *pname = (e.param_name[0] != '\0') ? e.param_name : "Required Item";

    switch (e.method) {
    case 1: std::snprintf(out, cap, "High Friendship"); break;
    case 2: std::snprintf(out, cap, "Friendship (Day)"); break;
    case 3: std::snprintf(out, cap, "Friendship (Night)"); break;
    case 4: std::snprintf(out, cap, "Lv. %u", (unsigned)e.param); break;
    case 5:
        std::snprintf(out, cap, trade_fix ? "Trade or Lv. 36+" : "Trade");
        break;
    case 6:
        if (e.param_name[0] != '\0') {
            std::snprintf(out, cap, trade_fix ? "Lv-Up / Trade w/ %s" : "Trade w/ %s", pname);
        } else {
            std::snprintf(out, cap, trade_fix ? "Lv-Up / Trade w/ Item #%u" : "Trade w/ Item #%u",
                          (unsigned)e.param);
        }
        break;
    case 7:
        if (trade_fix) {
            std::snprintf(out, cap, "Lv. 36+ or w/ counterpart in party");
        } else {
            std::snprintf(out, cap, "Trade w/ Karrablast / Shelmet");
        }
        break;
    case 8:  std::snprintf(out, cap, "Use %s", pname); break;
    case 9:  std::snprintf(out, cap, "Lv. %u (Atk > Def)", (unsigned)e.param); break;
    case 10: std::snprintf(out, cap, "Lv. %u (Atk = Def)", (unsigned)e.param); break;
    case 11: std::snprintf(out, cap, "Lv. %u (Atk < Def)", (unsigned)e.param); break;
    case 12: std::snprintf(out, cap, "Lv. %u (Personality)", (unsigned)e.param); break;
    case 13: std::snprintf(out, cap, "Lv. %u (Personality)", (unsigned)e.param); break;
    case 14: std::snprintf(out, cap, "Lv. %u", (unsigned)e.param); break;
    case 15: std::snprintf(out, cap, "Lv. 20 (Empty Party Slot + Poke Ball)"); break;
    case 16: std::snprintf(out, cap, "High Beauty (%u)", (unsigned)e.param); break;
    case 17: std::snprintf(out, cap, "Use %s (Male)", pname); break;
    case 18: std::snprintf(out, cap, "Use %s (Female)", pname); break;
    case 19: std::snprintf(out, cap, "Lv-Up w/ %s (Day)", pname); break;
    case 20: std::snprintf(out, cap, "Lv-Up w/ %s (Night)", pname); break;
    case 21:
        std::snprintf(out, cap, "Lv-Up knowing %s",
                      e.param_name[0] != '\0' ? e.param_name : "Move");
        break;
    case 22:
        std::snprintf(out, cap, "Lv-Up w/ %s in party",
                      e.param_name[0] != '\0' ? e.param_name : "Partner");
        break;
    case 23: std::snprintf(out, cap, "Lv. %u (Male)", (unsigned)e.param); break;
    case 24: std::snprintf(out, cap, "Lv. %u (Female)", (unsigned)e.param); break;
    case 25: std::snprintf(out, cap, "Lv-Up in Chargestone Cave"); break;
    case 26: std::snprintf(out, cap, "Lv-Up near Moss Rock (Pinwheel)"); break;
    case 27: std::snprintf(out, cap, "Lv-Up near Ice Rock (Twist Mt.)"); break;
    case 28: std::snprintf(out, cap, "Lv-Up in special Unova zone"); break;
    default: std::snprintf(out, cap, "Method %u", (unsigned)e.method); break;
    }
}

static void draw_dex_move_table(const char *table_id, const PokebwDexMove *moves, int count, int mode)
{
    if (count <= 0) {
        ImGui::TextDisabled("None.");
        return;
    }
    static const char *const k_cat_names[3] = { "Stat", "Phys", "Spec" };
    int ncols = (mode == 2) ? 6 : 7;
    if (ImGui::BeginTable(table_id, ncols,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, std::min(280.0f, 34.0f * (count + 1))))) {
        if (mode == 0) {
            ImGui::TableSetupColumn("Lv", ImGuiTableColumnFlags_WidthFixed);
        } else if (mode == 1) {
            ImGui::TableSetupColumn("TM", ImGuiTableColumnFlags_WidthFixed);
        }
        ImGui::TableSetupColumn("Move", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Cat", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Pwr", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Acc", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("PP", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (int i = 0; i < count; i++) {
            const PokebwDexMove &m = moves[i];
            int cat_idx = m.category < 3u ? static_cast<int>(m.category) : 0;
            ImGui::TableNextRow();
            if (mode == 0) {
                ImGui::TableNextColumn();
                if (m.tag <= 1u) {
                    ImGui::TextUnformatted("Start");
                } else {
                    ImGui::Text("Lv.%u", (unsigned)m.tag);
                }
            } else if (mode == 1) {
                ImGui::TableNextColumn();
                if (m.tag > 100u) {
                    ImGui::Text("HM%02u", (unsigned)(m.tag - 100u));
                } else {
                    ImGui::Text("TM%02u", (unsigned)m.tag);
                }
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.name);
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            draw_dex_type_badge(m.type);
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(k_cat_names[cat_idx]);
            ImGui::TableNextColumn();
            if (m.power == 0u) {
                ImGui::TextDisabled("-");
            } else {
                ImGui::Text("%u", (unsigned)m.power);
            }
            ImGui::TableNextColumn();
            if (m.accuracy == 0u) {
                ImGui::TextDisabled("-");
            } else {
                ImGui::Text("%u", (unsigned)m.accuracy);
            }
            ImGui::TableNextColumn();
            ImGui::Text("%u", (unsigned)m.pp);
        }
        ImGui::EndTable();
    }
}

void draw_gear_tab(void)
{
    static int g_confirm_restore_slot = 0;
    const PokebwGearSnapshot *gear = host_gear_get();
    static const char *const k_season_names[] = { "Spring", "Summer", "Autumn", "Winter" };
    static const ImVec4 k_season_colors[] = {
        ImVec4(0.45f, 0.88f, 0.52f, 1.0f),
        ImVec4(0.96f, 0.78f, 0.26f, 1.0f),
        ImVec4(0.92f, 0.50f, 0.24f, 1.0f),
        ImVec4(0.55f, 0.82f, 0.98f, 1.0f)
    };
    static const char *const k_hab_names[POKEBW_GEAR_HABITATS] = {
        "Tall Grass / Cave",
        "Dark Grass",
        "Phenomena (Shaking / Dust)",
        "Surfing",
        "Rippling Water",
        "Fishing",
        "Rippling Fishing"
    };

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Quick Field Moves (HM/TM)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (gear->valid == 0u) {
            ImGui::TextDisabled("Enter the overworld to use field moves.");
        } else {
            static const char *const k_fm_names[POKEBW_GEAR_FIELD_MOVES] = {
                "Cut", "Fly", "Surf", "Strength", "Waterfall", "Dive", "Flash", "Dig"
            };
            int order[POKEBW_GEAR_FIELD_MOVES];
            int count = 0;

            for (int tier = 0; tier < 3; tier++) {
                for (int i = 0; i < POKEBW_GEAR_FIELD_MOVES; i++) {
                    const PokebwGearFieldMove &fm = gear->field_moves[i];
                    int t = fm.usable ? 0 : (fm.party_slot < 6u ? 1 : 2);
                    if (t == tier) {
                        order[count++] = i;
                    }
                }
            }

            if (ImGui::BeginTable("##hm_grid", 2, ImGuiTableFlags_SizingStretchSame)) {
                for (int k = 0; k < count; k++) {
                    int idx = order[k];
                    const PokebwGearFieldMove &fm = gear->field_moves[idx];
                    const char *mname = k_fm_names[idx];

                    ImGui::TableNextColumn();
                    ImGui::PushID(idx);
                    if (fm.usable && fm.party_slot < gear->party_count) {
                        char btn[64];
                        std::snprintf(btn, sizeof(btn), "%s (%s)",
                                      mname, gear->party[fm.party_slot].name);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.52f, 0.36f, 0.92f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.64f, 0.44f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.42f, 0.28f, 1.0f));
                        if (ImGui::Button(btn, ImVec2(-1.0f, 0.0f))) {
                            host_hm_request_use((int)fm.skill_index,
                                                static_cast<unsigned long long>(SDL_GetTicks()));
                            g_open = 0;
                        }
                        ImGui::PopStyleColor(3);
                    } else {
                        const char *reason = "No user";
                        if (fm.party_slot < 6u) {
                            switch (fm.skill_index) {
                            case 0:  reason = "Face tree";    break;
                            case 4:  reason = "Outdoors only";break;
                            case 1:  reason = "Face water";   break;
                            case 3:  reason = "Face boulder"; break;
                            case 2:  reason = "Face falls";   break;
                            case 10: reason = "Deep water";   break;
                            case 5:  reason = "Dark cave";    break;
                            case 7:  reason = "Caves only";   break;
                            default: reason = "Blocked";      break;
                            }
                        }
                        char btn[64];
                        std::snprintf(btn, sizeof(btn), "%s - %s", mname, reason);
                        ImGui::BeginDisabled();
                        ImGui::Button(btn, ImVec2(-1.0f, 0.0f));
                        ImGui::EndDisabled();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (host_hm_last_status() < 0) {
                ImGui::TextDisabled("Field move unavailable right now (free-roam overworld only).");
            }
        }
    }

    if (ImGui::CollapsingHeader("Party", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (gear->valid == 0u || gear->party_count == 0u) {
            ImGui::TextDisabled("Enter the overworld to sync party data.");
        } else {
            for (int p = 0; p < (int)gear->party_count && p < POKEBW_GEAR_MAX_PARTY; p++) {
                const PokebwGearMon &mon = gear->party[p];
                ImGui::PushID(p);
                ImGui::BeginGroup();

                if (mon.is_egg) {
                    ImGui::Text("Egg");
                    if (mon.egg_hatch_boost != 0u) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.40f, 0.88f, 0.48f, 1.0f),
                                           "[2x Flame Body/Magma Armor]");
                    }
                    if (mon.egg_steps_left == 0u) {
                        ImGui::TextColored(ImVec4(0.32f, 0.94f, 0.44f, 1.0f),
                                           "Ready to hatch on next step!");
                    } else {
                        float efrac = (mon.egg_steps_max > 0u)
                                          ? std::clamp(1.0f - static_cast<float>(mon.egg_steps_left) /
                                                                  static_cast<float>(mon.egg_steps_max),
                                                       0.0f, 1.0f)
                                          : 0.0f;
                        char egg_buf[48];
                        std::snprintf(egg_buf, sizeof(egg_buf), "%u steps left (%u cycles)",
                                      (unsigned)mon.egg_steps_left, (unsigned)mon.egg_cycles_left);
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.92f, 0.72f, 0.24f, 1.0f));
                        ImGui::ProgressBar(efrac, ImVec2(-1.0f, 0.0f), egg_buf);
                        ImGui::PopStyleColor();
                    }
                } else {
                    if (std::strcmp(mon.name, mon.species_name) != 0) {
                        ImGui::Text("%s (%s)  Lv.%u", mon.name, mon.species_name, (unsigned)mon.level);
                    } else {
                        ImGui::Text("%s  Lv.%u", mon.name, (unsigned)mon.level);
                    }
                    const char *st_label = nullptr;
                    ImVec4 st_col(1.0f, 1.0f, 1.0f, 1.0f);
                    if (mon.hp == 0u) {
                        st_label = "FNT";
                        st_col = ImVec4(0.88f, 0.22f, 0.26f, 1.0f);
                    } else {
                        format_status_badge(mon.status, &st_label, &st_col);
                    }
                    if (st_label != nullptr) {
                        ImGui::SameLine();
                        ImGui::TextColored(st_col, "[%s]", st_label);
                    }

                    float frac = mon.max_hp > 0u
                                     ? std::clamp(static_cast<float>(mon.hp) / static_cast<float>(mon.max_hp), 0.0f, 1.0f)
                                     : 0.0f;
                    ImVec4 hp_col = frac > 0.5f
                                        ? ImVec4(0.24f, 0.80f, 0.40f, 1.0f)
                                        : (frac > 0.2f ? ImVec4(0.92f, 0.78f, 0.22f, 1.0f)
                                                       : ImVec4(0.92f, 0.28f, 0.24f, 1.0f));
                    char hp_buf[32];
                    std::snprintf(hp_buf, sizeof(hp_buf), "HP %u/%u", (unsigned)mon.hp, (unsigned)mon.max_hp);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hp_col);
                    ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f), hp_buf);
                    ImGui::PopStyleColor();

                    if (mon.move_count > 0u && ImGui::BeginTable("##moves", 2, ImGuiTableFlags_SizingStretchSame)) {
                        for (int m = 0; m < (int)mon.move_count && m < POKEBW_GEAR_MAX_MOVES; m++) {
                            const PokebwGearMove &mv = mon.moves[m];
                            int ti = mv.type < 18u ? (int)mv.type : 17;
                            ImVec4 tc = k_type_colors[ti];
                            ImVec4 fill(tc.x * 0.42f, tc.y * 0.42f, tc.z * 0.42f, 0.95f);
                            ImVec4 border(tc.x * 0.88f, tc.y * 0.88f, tc.z * 0.88f, 1.0f);

                            ImGui::TableNextColumn();
                            ImGui::PushStyleColor(ImGuiCol_Button, fill);
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fill);
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, fill);
                            ImGui::PushStyleColor(ImGuiCol_Border, border);
                            char mv_buf[48];
                            std::snprintf(mv_buf, sizeof(mv_buf), "%s  %u/%u",
                                          mv.name, (unsigned)mv.pp, (unsigned)mv.max_pp);
                            ImGui::Button(mv_buf, ImVec2(-1.0f, 0.0f));
                            ImGui::PopStyleColor(4);
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndGroup();
                if (p + 1 < (int)gear->party_count) {
                    ImGui::Separator();
                }
                ImGui::PopID();
            }
        }
    }

    if (ImGui::CollapsingHeader("Day Care Preview (Route 3)")) {
        const PokebwGearDaycare &dc = gear->daycare;
        if (gear->valid == 0u || dc.valid == 0u) {
            ImGui::TextDisabled("Enter the overworld to read Route 3 Day Care status.");
        } else {
            if (dc.egg_ready != 0u) {
                ImGui::TextColored(ImVec4(0.32f, 0.96f, 0.48f, 1.0f),
                                   "EGG READY! Talk to the Day Care Man on Route 3.");
                ImGui::Spacing();
            }

            if (dc.parent_count == 0u) {
                ImGui::TextDisabled("No Pokemon currently deposited in Day Care.");
            } else {
                static const char *const k_iv_names[6] = {
                    "HP", "Atk", "Def", "SpA", "SpD", "Spe"
                };
                for (int s = 0; s < 2; s++) {
                    const PokebwGearDaycareParent &p = dc.parents[s];
                    if (!p.present) {
                        continue;
                    }
                    const char *nat = (p.nature < 25u) ? k_nature_names[p.nature] : "Hardy";
                    const char *gstr = (p.gender == 0u) ? "M" : (p.gender == 1u ? "F" : "-");
                    ImGui::Text("Slot %d: %s [%s] Lv.%u", s + 1, p.name, gstr, (unsigned)p.level);
                    if (p.levels_gained > 0u) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.45f, 0.88f, 0.52f, 1.0f), "(+%u)",
                                           (unsigned)p.levels_gained);
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("| %s | %s", nat, daycare_lang_tag(p.language));
                    if (p.held_item != 0u && p.item_name[0] != '\0') {
                        ImGui::Text("  Holding: %s", p.item_name);
                    }
                    char iv_buf[96];
                    int off = 0;
                    for (int k = 0; k < 6; k++) {
                        int n = std::snprintf(iv_buf + off, sizeof(iv_buf) - static_cast<size_t>(off),
                                              "%s%s %u%s", k > 0 ? " | " : "",
                                              k_iv_names[k], (unsigned)p.ivs[k],
                                              (dc.power_stat[s] == k) ? "*" : "");
                        if (n > 0) off += n;
                    }
                    ImGui::TextDisabled("  IVs: %s", iv_buf);
                }

                if (dc.parent_count == 2u) {
                    ImGui::Separator();
                    if (dc.compat_score == 0u) {
                        ImGui::TextColored(ImVec4(0.95f, 0.34f, 0.32f, 1.0f),
                                           "Compatibility: Incompatible (0%% - will not produce an Egg)");
                    } else {
                        ImVec4 ccol = (dc.compat_score >= 70u)
                                          ? ImVec4(0.32f, 0.94f, 0.48f, 1.0f)
                                          : (dc.compat_score >= 50u ? ImVec4(0.95f, 0.82f, 0.24f, 1.0f)
                                                                    : ImVec4(0.96f, 0.56f, 0.22f, 1.0f));
                        const char *clabel = (dc.compat_score >= 70u) ? "High (fast)"
                                             : (dc.compat_score >= 50u ? "Medium" : "Low (slow)");
                        ImGui::TextColored(ccol, "Compatibility: %s (%u/1600 per 16 steps)",
                                           clabel, (unsigned)dc.compat_score);

                        if (dc.egg_species != 0u) {
                            unsigned base_steps = static_cast<unsigned>(dc.egg_cycles) * 257u;
                            if (dc.egg_alt_flag == 1u) {
                                ImGui::Text("Egg Species: Nidoran F / Nidoran M (50/50) (~%u steps)",
                                            base_steps);
                            } else if (dc.egg_alt_flag == 2u) {
                                ImGui::Text("Egg Species: Volbeat / Illumise (50/50) (~%u steps)",
                                            base_steps);
                            } else {
                                ImGui::Text("Egg Species: %s (~%u steps, %u cycles)",
                                            dc.egg_species_name, base_steps, (unsigned)dc.egg_cycles);
                            }
                        }

                        if (dc.egg_move_count > 0u) {
                            char mv_line[128];
                            int off = 0;
                            for (int m = 0; m < (int)dc.egg_move_count && m < 4; m++) {
                                int n = std::snprintf(mv_line + off,
                                                      sizeof(mv_line) - static_cast<size_t>(off),
                                                      "%s%s", m > 0 ? ", " : "", dc.egg_move_names[m]);
                                if (n > 0) off += n;
                            }
                            ImGui::TextWrapped("Egg Moves: %s", mv_line);
                        }

                        if (dc.everstone_mask == 3u) {
                            const char *n0 = k_nature_names[dc.everstone_nature[0] % 25u];
                            const char *n1 = k_nature_names[dc.everstone_nature[1] % 25u];
                            ImGui::TextWrapped("Nature: 25%% %s / 25%% %s / 50%% Random (Dual Everstone)", n0, n1);
                        } else if (dc.everstone_mask == 1u || dc.everstone_mask == 2u) {
                            int es = (dc.everstone_mask == 2u) ? 1 : 0;
                            const char *enat = k_nature_names[dc.everstone_nature[es] % 25u];
                            ImGui::Text("Nature: 50%% %s (Everstone on Slot %d)", enat, es + 1);
                        } else {
                            ImGui::TextDisabled("Nature: Random (1/25 each - equip Everstone for 50%%)");
                        }

                        if (dc.power_stat[0] >= 0 && dc.power_stat[1] >= 0) {
                            int s0 = dc.power_stat[0], s1 = dc.power_stat[1];
                            ImGui::TextWrapped("IVs: 50%% %s (%u) / 50%% %s (%u) + 2 Random from Parents",
                                               k_iv_names[s0], (unsigned)dc.parents[0].ivs[s0],
                                               k_iv_names[s1], (unsigned)dc.parents[1].ivs[s1]);
                        } else if (dc.power_stat[0] >= 0 || dc.power_stat[1] >= 0) {
                            int ps = (dc.power_stat[0] >= 0) ? 0 : 1;
                            int st = dc.power_stat[ps];
                            ImGui::TextWrapped("IVs: Guaranteed %s (%u) from Slot %d + 2 Random from Parents",
                                               k_iv_names[st], (unsigned)dc.parents[ps].ivs[st], ps + 1);
                        } else {
                            ImGui::TextDisabled("IVs: 3 Random IVs inherited from parents (no Power Item)");
                        }

                        static const char *const k_mult_odds[5] = {
                            "1/8192", "1/1024", "1/256", "1/64", "100%"
                        };
                        int ms = static_cast<int>(dc.shiny_odds_setting);
                        if (ms < 0 || ms > 4) ms = 0;
                        if (dc.shiny_odds_setting > 0u) {
                            ImGui::TextColored(ImVec4(0.98f, 0.84f, 0.28f, 1.0f),
                                               "Shiny Odds: %s (Shiny Multiplier active%s)",
                                               k_mult_odds[ms],
                                               dc.masuda_active ? " + Masuda" : "");
                        } else if (dc.masuda_active) {
                            ImGui::TextColored(ImVec4(0.98f, 0.84f, 0.28f, 1.0f),
                                               "Shiny Odds: ~1/1365 (Masuda Method active - 6x rolls)");
                        } else {
                            ImGui::TextDisabled("Shiny Odds: 1/8192 (Masuda inactive - same language)");
                        }
                    }
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Route Radar", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (gear->valid == 0u) {
            ImGui::TextDisabled("Enter the overworld to scan Unova habitats.");
        } else {
            int season_idx = (int)(gear->season & 3u);
            ImGui::Text("%s", gear->zone_name);
            ImGui::SameLine();
            ImGui::TextColored(k_season_colors[season_idx], "[%s]", k_season_names[season_idx]);
            if (gear->catch_em_all_active != 0u) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.40f, 0.88f, 0.96f, 1.0f), "[Catch 'Em All]");
            }

            if (gear->has_encounters == 0u) {
                ImGui::TextDisabled("No wild encounters in this area.");
            } else {
                for (int h = 0; h < POKEBW_GEAR_HABITATS; h++) {
                    const PokebwGearHabitat &hab = gear->habitats[h];
                    if (hab.count == 0u) {
                        continue;
                    }
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.76f, 0.86f, 1.00f, 1.0f), "%s", k_hab_names[h]);
                    if (ImGui::BeginTable(k_hab_names[h], 4,
                                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                        ImGui::TableSetupColumn("Dex", ImGuiTableColumnFlags_WidthFixed);
                        ImGui::TableSetupColumn("Species", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed);
                        ImGui::TableSetupColumn("Rate", ImGuiTableColumnFlags_WidthFixed);
                        for (int e = 0; e < (int)hab.count && e < POKEBW_GEAR_MAX_ENC; e++) {
                            const PokebwGearEncEntry &en = hab.entries[e];
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            if (en.dex_state == 2u) {
                                ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.0f), "[Caught]");
                            } else if (en.dex_state == 1u) {
                                ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.25f, 1.0f), "[Seen]");
                            } else {
                                ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.98f, 1.0f), "[New]");
                            }
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", en.name);
                            ImGui::TableNextColumn();
                            if (en.min_lv == en.max_lv) {
                                ImGui::Text("Lv.%u", (unsigned)en.min_lv);
                            } else {
                                ImGui::Text("Lv.%u-%u", (unsigned)en.min_lv, (unsigned)en.max_lv);
                            }
                            ImGui::TableNextColumn();
                            ImGui::Text("%u%%", (unsigned)en.chance_pct);
                        }
                        ImGui::EndTable();
                    }
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("PC Box & Bag Auto-Sorter")) {
        if (gear->valid == 0u) {
            ImGui::TextDisabled("Enter the overworld to sort PC Boxes or Bag pockets.");
        } else {
            static int s_pc_sel = -1;
            static int s_pc_mode = POKEBW_SORT_PC_DEX;
            static int s_bag_sel = 0;
            static int s_bag_mode = POKEBW_SORT_BAG_GAME;

            static const char *const k_pc_mode_names[] = {
                "Dex # (National)",
                "Name (A-Z)",
                "Level (High -> Low)",
                "Type",
                "Shiny First"
            };
            static const char *const k_bag_mode_names[] = {
                "Game Order (Category)",
                "Name (A-Z)",
                "Quantity (High -> Low)",
                "Item ID"
            };
            static const char *const k_bag_pocket_names[POKEBW_GEAR_BAG_POCKETS] = {
                "Items", "Medicine", "TM & HM", "Berries", "Key Items"
            };

            int usable_boxes = static_cast<int>(gear->box_usable_count);
            if (usable_boxes > POKEBW_GEAR_MAX_BOXES) {
                usable_boxes = POKEBW_GEAR_MAX_BOXES;
            }
            if (s_pc_sel < 0) {
                s_pc_sel = (usable_boxes > 0 && gear->box_current < static_cast<uint8_t>(usable_boxes))
                               ? static_cast<int>(gear->box_current) + 1
                               : 0;
            }
            if (s_pc_sel > usable_boxes) {
                s_pc_sel = 0;
            }

            int total_box_mons = 0;
            for (int b = 0; b < usable_boxes; b++) {
                total_box_mons += static_cast<int>(gear->box_counts[b]);
            }

            ImGui::TextDisabled("PC Storage (%d Pokemon across %d boxes):",
                                total_box_mons, usable_boxes);
            if (usable_boxes > 0) {
                char pc_preview[64];
                if (s_pc_sel == 0) {
                    std::snprintf(pc_preview, sizeof(pc_preview),
                                  "All Usable Boxes (%d Pokemon)", total_box_mons);
                } else {
                    int bi = s_pc_sel - 1;
                    const char *bname = gear->box_names[bi][0] != '\0' ? gear->box_names[bi] : "Box";
                    std::snprintf(pc_preview, sizeof(pc_preview), "%s (%u/30)%s",
                                  bname, static_cast<unsigned>(gear->box_counts[bi]),
                                  (bi == static_cast<int>(gear->box_current)) ? " *" : "");
                }

                if (ImGui::BeginCombo("Target Box", pc_preview)) {
                    char item_lbl[64];
                    std::snprintf(item_lbl, sizeof(item_lbl),
                                  "All Usable Boxes (%d Pokemon)", total_box_mons);
                    if (ImGui::Selectable(item_lbl, s_pc_sel == 0)) {
                        s_pc_sel = 0;
                    }
                    for (int b = 0; b < usable_boxes; b++) {
                        const char *bname = gear->box_names[b][0] != '\0' ? gear->box_names[b] : "Box";
                        std::snprintf(item_lbl, sizeof(item_lbl), "%s (%u/30)%s",
                                      bname, static_cast<unsigned>(gear->box_counts[b]),
                                      (b == static_cast<int>(gear->box_current)) ? " [Current]" : "");
                        ImGui::PushID(b);
                        if (ImGui::Selectable(item_lbl, s_pc_sel == b + 1)) {
                            s_pc_sel = b + 1;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }

                s_pc_mode = std::clamp(s_pc_mode, 0, 4);
                ImGui::Combo("Box Order", &s_pc_mode, k_pc_mode_names, 5);

                const char *pc_btn = (s_pc_sel == 0) ? "Sort All PC Boxes" : "Sort Selected Box";
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.44f, 0.72f, 0.92f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.54f, 0.84f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.34f, 0.58f, 1.0f));
                if (ImGui::Button(pc_btn, ImVec2(-1.0f, 0.0f))) {
                    int kind = (s_pc_sel == 0) ? POKEBW_SORT_KIND_PC_ALL : POKEBW_SORT_KIND_PC_BOX;
                    int target = (s_pc_sel == 0) ? 0 : (s_pc_sel - 1);
                    host_sort_request_submit(kind, s_pc_mode, target,
                                             static_cast<unsigned long long>(SDL_GetTicks()));
                }
                ImGui::PopStyleColor(3);
            } else {
                ImGui::TextDisabled("PC Box data unavailable yet.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            int total_bag = 0;
            for (int pk = 0; pk < POKEBW_GEAR_BAG_POCKETS; pk++) {
                total_bag += static_cast<int>(gear->bag_pocket_counts[pk]);
            }
            ImGui::TextDisabled("Bag Pockets (%d total item stacks):", total_bag);

            s_bag_sel = std::clamp(s_bag_sel, 0, POKEBW_GEAR_BAG_POCKETS);
            char bag_preview[64];
            if (s_bag_sel == 0) {
                std::snprintf(bag_preview, sizeof(bag_preview), "All Pockets (%d stacks)", total_bag);
            } else {
                int pk = s_bag_sel - 1;
                std::snprintf(bag_preview, sizeof(bag_preview), "%s (%u stacks)",
                              k_bag_pocket_names[pk],
                              static_cast<unsigned>(gear->bag_pocket_counts[pk]));
            }

            if (ImGui::BeginCombo("Target Pocket", bag_preview)) {
                char pk_lbl[64];
                std::snprintf(pk_lbl, sizeof(pk_lbl), "All Pockets (%d stacks)", total_bag);
                if (ImGui::Selectable(pk_lbl, s_bag_sel == 0)) {
                    s_bag_sel = 0;
                }
                for (int pk = 0; pk < POKEBW_GEAR_BAG_POCKETS; pk++) {
                    std::snprintf(pk_lbl, sizeof(pk_lbl), "%s (%u stacks)",
                                  k_bag_pocket_names[pk],
                                  static_cast<unsigned>(gear->bag_pocket_counts[pk]));
                    if (ImGui::Selectable(pk_lbl, s_bag_sel == pk + 1)) {
                        s_bag_sel = pk + 1;
                    }
                }
                ImGui::EndCombo();
            }

            s_bag_mode = std::clamp(s_bag_mode, 0, 3);
            ImGui::Combo("Bag Order", &s_bag_mode, k_bag_mode_names, 4);

            const char *bag_btn = (s_bag_sel == 0) ? "Sort All Bag Pockets" : "Sort Selected Pocket";
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.56f, 0.42f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.68f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.44f, 0.32f, 1.0f));
            if (ImGui::Button(bag_btn, ImVec2(-1.0f, 0.0f))) {
                int kind = (s_bag_sel == 0) ? POKEBW_SORT_KIND_BAG_ALL : POKEBW_SORT_KIND_BAG_POCKET;
                int target = (s_bag_sel == 0) ? 0 : (s_bag_sel - 1);
                host_sort_request_submit(kind, s_bag_mode, target,
                                         static_cast<unsigned long long>(SDL_GetTicks()));
            }
            ImGui::PopStyleColor(3);

            if (host_sort_status()[0] != '\0') {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", host_sort_status());
            }
        }
    }

    if (ImGui::CollapsingHeader("Pokedex Lookup (A-Z / #001-649)")) {
        static bool s_dex_auto_req = false;
        static char s_dex_search[40] = "";
        static int s_dex_sort_az = 1;
        static int s_dex_letter = 0; /* 0 = All, 1..26 = A..Z */
        static int s_dex_owned_filter = 0; /* 0=All, 1=Seen+, 2=Caught */
        static uint16_t s_selected_species = 0u;
        static uint8_t s_selected_form = 0u;

        const PokebwDexIndex *dex_idx = host_dex_index_get();
        const PokebwDexDetail *dex_det = host_dex_detail_get();

        if (gear->valid == 0u) {
            s_dex_auto_req = false;
            ImGui::TextDisabled("Enter the overworld to browse ROM Pokedex data.");
        } else {
            uint64_t now_ms = SDL_GetTicks();
            uint16_t default_sp = (gear->party_count > 0u && gear->party[0].species >= 1u &&
                                   gear->party[0].species <= POKEBW_DEX_MAX_SPECIES)
                                      ? gear->party[0].species
                                      : 1u;
            if (s_selected_species == 0u) {
                s_selected_species = default_sp;
            }
            if ((dex_idx->valid == 0u || dex_det->valid == 0u) && !s_dex_auto_req) {
                s_dex_auto_req = true;
                host_dex_request_submit(1, s_selected_species, s_selected_form, now_ms);
            }

            if (gear->party_count > 0u) {
                ImGui::TextDisabled("Party Jump:");
                for (int p = 0; p < (int)gear->party_count && p < POKEBW_GEAR_MAX_PARTY; p++) {
                    const PokebwGearMon &pm = gear->party[p];
                    if (pm.is_egg || pm.species == 0u || pm.species > POKEBW_DEX_MAX_SPECIES) {
                        continue;
                    }
                    ImGui::SameLine();
                    ImGui::PushID(2000 + p);
                    if (ImGui::SmallButton(pm.species_name[0] != '\0' ? pm.species_name : pm.name)) {
                        s_selected_species = pm.species;
                        s_selected_form = 0u;
                        host_dex_request_submit(dex_idx->valid == 0u ? 1 : 0,
                                                s_selected_species, 0u, now_ms);
                    }
                    ImGui::PopID();
                }
            }

            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##dex_search", "Search Pokemon name or # (e.g. Excadrill, 530)",
                                     s_dex_search, sizeof(s_dex_search));

            if (ImGui::RadioButton("Sort A-Z", s_dex_sort_az == 1)) {
                s_dex_sort_az = 1;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Sort #001-649", s_dex_sort_az == 0)) {
                s_dex_sort_az = 0;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh")) {
                host_dex_request_submit(1, s_selected_species, s_selected_form, now_ms);
            }

            static const char *const k_letter_items =
                "All\0A\0B\0C\0D\0E\0F\0G\0H\0I\0J\0K\0L\0M\0N\0O\0P\0Q\0R\0S\0T\0U\0V\0W\0X\0Y\0Z\0";
            static const char *const k_owned_items = "All\0Seen+\0Caught\0";
            ImGui::SetNextItemWidth(110.0f);
            ImGui::Combo("Letter", &s_dex_letter, k_letter_items);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::Combo("Status", &s_dex_owned_filter, k_owned_items);

            if (host_dex_status()[0] != '\0') {
                ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.25f, 1.0f), "%s", host_dex_status());
            }

            if (dex_idx->valid != 0u) {
                static uint16_t s_filtered[POKEBW_DEX_MAX_SPECIES];
                int fcount = 0;
                int num_query = 0;
                bool is_num_query = true;
                for (const char *p = s_dex_search; *p != '\0'; p++) {
                    if (*p == '#') {
                        continue;
                    }
                    if (!std::isdigit(static_cast<unsigned char>(*p))) {
                        is_num_query = false;
                        break;
                    }
                    num_query = num_query * 10 + (*p - '0');
                }
                if (s_dex_search[0] == '\0' || num_query <= 0) {
                    is_num_query = false;
                }

                for (int sp = 1; sp <= (int)dex_idx->count && sp <= POKEBW_DEX_MAX_SPECIES; sp++) {
                    const char *nm = dex_idx->names[sp];
                    uint8_t ds = dex_idx->dex_state[sp];
                    if (s_dex_owned_filter == 1 && ds < 1u) {
                        continue;
                    }
                    if (s_dex_owned_filter == 2 && ds < 2u) {
                        continue;
                    }
                    if (s_dex_letter > 0 && nm[0] != '\0') {
                        char want = static_cast<char>('A' + (s_dex_letter - 1));
                        char got = static_cast<char>(std::toupper(static_cast<unsigned char>(nm[0])));
                        if (got != want) {
                            continue;
                        }
                    }
                    if (s_dex_search[0] != '\0') {
                        if (is_num_query) {
                            if (sp != num_query) {
                                continue;
                            }
                        } else if (!dex_icontains(nm, s_dex_search)) {
                            continue;
                        }
                    }
                    s_filtered[fcount++] = static_cast<uint16_t>(sp);
                }

                if (s_dex_sort_az != 0 && fcount > 1) {
                    std::sort(s_filtered, s_filtered + fcount, [dex_idx](uint16_t a, uint16_t b) {
                        int c = dex_icmp(dex_idx->names[a], dex_idx->names[b]);
                        return c != 0 ? (c < 0) : (a < b);
                    });
                }

                ImGui::TextDisabled("%d matching species", fcount);
                if (ImGui::BeginChild("##dex_list", ImVec2(-1.0f, 175.0f), ImGuiChildFlags_Borders)) {
                    ImGuiListClipper clipper;
                    clipper.Begin(fcount);
                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                            uint16_t sp = s_filtered[i];
                            uint8_t ds = dex_idx->dex_state[sp];
                            const char *tag = (ds == 2u) ? "[C]" : (ds == 1u ? "[S]" : "   ");
                            char row_label[64];
                            std::snprintf(row_label, sizeof(row_label), "%s #%03u  %s##drow_%u",
                                          tag, (unsigned)sp, dex_idx->names[sp], (unsigned)sp);
                            bool is_sel = (sp == s_selected_species);
                            if (ImGui::Selectable(row_label, is_sel)) {
                                s_selected_species = sp;
                                s_selected_form = 0u;
                                host_dex_request_submit(0, sp, 0u, now_ms);
                            }
                        }
                    }
                    ImGui::EndChild();
                }
            }

            if (dex_det->valid != 0u) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                const char *st_label = dex_det->dex_state == 2u
                                           ? "[Caught]"
                                           : (dex_det->dex_state == 1u ? "[Seen]" : "[Unseen]");
                ImVec4 st_col = dex_det->dex_state == 2u
                                    ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f)
                                    : (dex_det->dex_state == 1u
                                           ? ImVec4(0.95f, 0.78f, 0.25f, 1.0f)
                                           : ImVec4(0.65f, 0.65f, 0.70f, 1.0f));
                ImGui::Text("#%03u  %s", (unsigned)dex_det->species, dex_det->name);
                ImGui::SameLine();
                ImGui::TextColored(st_col, "%s", st_label);
                if (dex_det->category[0] != '\0') {
                    ImGui::TextDisabled("%s Pokemon", dex_det->category);
                }

                draw_dex_type_badge(dex_det->type1);
                if (dex_det->type2 != dex_det->type1) {
                    ImGui::SameLine();
                    draw_dex_type_badge(dex_det->type2);
                }

                if (dex_det->form_count > 1u) {
                    ImGui::SameLine();
                    char flab[24];
                    std::snprintf(flab, sizeof(flab), "Form %u/%u >",
                                  (unsigned)dex_det->form + 1u, (unsigned)dex_det->form_count);
                    if (ImGui::SmallButton(flab)) {
                        uint8_t next_form = static_cast<uint8_t>((dex_det->form + 1u) % dex_det->form_count);
                        s_selected_form = next_form;
                        host_dex_request_submit(0, dex_det->species, next_form, now_ms);
                    }
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Abilities:");
                bool any_ab = false;
                for (int a = 0; a < 3; a++) {
                    if (dex_det->ability_ids[a] == 0u || dex_det->abilities[a][0] == '\0') {
                        continue;
                    }
                    if (a > 0 && dex_det->ability_ids[a] == dex_det->ability_ids[0]) {
                        continue;
                    }
                    if (any_ab) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("|");
                        ImGui::SameLine();
                    }
                    if (a == 2) {
                        ImGui::TextColored(ImVec4(0.96f, 0.78f, 0.30f, 1.0f),
                                           "%s [HA]", dex_det->abilities[a]);
                    } else {
                        ImGui::Text("%s", dex_det->abilities[a]);
                    }
                    any_ab = true;
                }

                /* Egg Groups, Gender Ratio, Catch Rate */
                {
                    char gbuf[40];
                    format_gender_ratio(dex_det->gender_ratio, gbuf, sizeof(gbuf));
                    int eg1 = dex_det->egg_group1 < 16u ? (int)dex_det->egg_group1 : 0;
                    int eg2 = dex_det->egg_group2 < 16u ? (int)dex_det->egg_group2 : 0;
                    if (eg2 != 0 && eg2 != eg1) {
                        ImGui::TextDisabled("Egg: %s / %s  |  Cycles: %u  |  Catch: %u  |  %s",
                                            k_egg_group_names[eg1], k_egg_group_names[eg2],
                                            (unsigned)dex_det->egg_cycles,
                                            (unsigned)dex_det->catch_rate, gbuf);
                    } else {
                        ImGui::TextDisabled("Egg: %s  |  Cycles: %u  |  Catch: %u  |  %s",
                                            k_egg_group_names[eg1],
                                            (unsigned)dex_det->egg_cycles,
                                            (unsigned)dex_det->catch_rate, gbuf);
                    }
                }

                if (ImGui::BeginTabBar("##dex_detail_tabs")) {
                    if (ImGui::BeginTabItem("Stats & Evo")) {
                        static const char *const k_stat_names[6] = {
                            "HP", "Atk", "Def", "SpA", "SpD", "Spe"
                        };
                        ImGui::Text("Base Stats  (BST: %u)", (unsigned)dex_det->bst);
                        for (int s = 0; s < 6; s++) {
                            uint8_t val = dex_det->base_stats[s];
                            float frac = std::clamp(static_cast<float>(val) / 200.0f, 0.0f, 1.0f);
                            ImVec4 col = val >= 110u
                                             ? ImVec4(0.24f, 0.84f, 0.48f, 1.0f)
                                             : (val >= 75u
                                                    ? ImVec4(0.45f, 0.78f, 0.92f, 1.0f)
                                                    : (val >= 50u
                                                           ? ImVec4(0.95f, 0.78f, 0.25f, 1.0f)
                                                           : ImVec4(0.92f, 0.34f, 0.28f, 1.0f)));
                            ImGui::Text("%-3s %3u", k_stat_names[s], (unsigned)val);
                            ImGui::SameLine(110.0f);
                            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
                            ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f), "");
                            ImGui::PopStyleColor();
                        }

                        ImGui::Spacing();
                        ImGui::Text("Evolution Family");
                        if (dex_det->pre_evo_count == 0u && dex_det->evo_count == 0u) {
                            ImGui::TextDisabled("Does not evolve.");
                        } else {
                            bool tfix = dex_det->trade_evo_fix_active != 0u;
                            for (int i = 0; i < (int)dex_det->pre_evo_count; i++) {
                                const PokebwDexEvo &e = dex_det->pre_evos[i];
                                char desc[64];
                                format_dex_evo_desc(e, tfix, desc, sizeof(desc));
                                ImGui::TextDisabled("From:");
                                ImGui::SameLine();
                                ImGui::PushID(3000 + i);
                                if (ImGui::SmallButton(e.target_name)) {
                                    s_selected_species = e.target_species;
                                    s_selected_form = 0u;
                                    host_dex_request_submit(0, e.target_species, 0u, now_ms);
                                }
                                ImGui::PopID();
                                ImGui::SameLine();
                                ImGui::TextWrapped("(%s)", desc);
                            }
                            for (int i = 0; i < (int)dex_det->evo_count; i++) {
                                const PokebwDexEvo &e = dex_det->evos[i];
                                char desc[64];
                                format_dex_evo_desc(e, tfix, desc, sizeof(desc));
                                ImGui::TextDisabled("To:");
                                ImGui::SameLine();
                                ImGui::PushID(3100 + i);
                                if (ImGui::SmallButton(e.target_name)) {
                                    s_selected_species = e.target_species;
                                    s_selected_form = 0u;
                                    host_dex_request_submit(0, e.target_species, 0u, now_ms);
                                }
                                ImGui::PopID();
                                ImGui::SameLine();
                                ImGui::TextWrapped("(%s)", desc);
                            }
                        }
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Moves")) {
                        if (ImGui::BeginTabBar("##dex_move_tabs")) {
                            char lbl[32];
                            std::snprintf(lbl, sizeof(lbl), "Level (%u)###dlvl", (unsigned)dex_det->lvl_count);
                            if (ImGui::BeginTabItem(lbl)) {
                                draw_dex_move_table("##tbl_lvl", dex_det->lvl_moves,
                                                    (int)dex_det->lvl_count, 0);
                                ImGui::EndTabItem();
                            }
                            std::snprintf(lbl, sizeof(lbl), "TM/HM (%u)###dtm", (unsigned)dex_det->tm_count);
                            if (ImGui::BeginTabItem(lbl)) {
                                draw_dex_move_table("##tbl_tm", dex_det->tm_moves,
                                                    (int)dex_det->tm_count, 1);
                                ImGui::EndTabItem();
                            }
                            std::snprintf(lbl, sizeof(lbl), "Egg (%u)###degg", (unsigned)dex_det->egg_count);
                            if (ImGui::BeginTabItem(lbl)) {
                                draw_dex_move_table("##tbl_egg", dex_det->egg_moves,
                                                    (int)dex_det->egg_count, 2);
                                ImGui::EndTabItem();
                            }
                            std::snprintf(lbl, sizeof(lbl), "Tutor (%u)###dtut", (unsigned)dex_det->tutor_count);
                            if (ImGui::BeginTabItem(lbl)) {
                                draw_dex_move_table("##tbl_tut", dex_det->tutor_moves,
                                                    (int)dex_det->tutor_count, 2);
                                ImGui::EndTabItem();
                            }
                            ImGui::EndTabBar();
                        }
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Locations")) {
                        if (dex_det->loc_catch_em_all_active != 0u) {
                            ImGui::TextColored(ImVec4(0.40f, 0.88f, 0.96f, 1.0f),
                                               "Includes Gotta Catch 'Em All! spawns");
                        }
                        if (dex_det->loc_count == 0u) {
                            ImGui::TextDisabled("No wild Unova encounters (evolve, gift, static, or breed).");
                        } else if (ImGui::BeginTable("##dex_locs", 4,
                                                     ImGuiTableFlags_RowBg |
                                                         ImGuiTableFlags_SizingFixedFit |
                                                         ImGuiTableFlags_ScrollY,
                                                     ImVec2(0.0f, std::min(280.0f, 36.0f * (dex_det->loc_count + 1))))) {
                            static const char *const k_short_hab[POKEBW_DEX_HABITATS] = {
                                "Grass/Cave", "Dark Grass", "Phenomena",
                                "Surf", "Ripple Surf", "Fish", "Ripple Fish"
                            };
                            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableSetupColumn("Habitat", ImGuiTableColumnFlags_WidthFixed);
                            ImGui::TableSetupColumn("Lv / Rate", ImGuiTableColumnFlags_WidthFixed);
                            ImGui::TableSetupColumn("Season", ImGuiTableColumnFlags_WidthFixed);
                            ImGui::TableHeadersRow();
                            for (int l = 0; l < (int)dex_det->loc_count; l++) {
                                const PokebwDexLocation &loc = dex_det->locations[l];
                                int hi = loc.habitat < POKEBW_DEX_HABITATS ? (int)loc.habitat : 0;
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                ImGui::TextUnformatted(loc.place_name);
                                ImGui::TableNextColumn();
                                ImGui::TextUnformatted(k_short_hab[hi]);
                                ImGui::TableNextColumn();
                                if (loc.min_lv == loc.max_lv) {
                                    ImGui::Text("Lv.%u (%u%%)", (unsigned)loc.min_lv, (unsigned)loc.chance_pct);
                                } else {
                                    ImGui::Text("Lv.%u-%u (%u%%)", (unsigned)loc.min_lv,
                                                (unsigned)loc.max_lv, (unsigned)loc.chance_pct);
                                }
                                ImGui::TableNextColumn();
                                if ((loc.seasons_mask & 0x0Fu) == 0x0Fu) {
                                    ImGui::TextDisabled("All");
                                } else {
                                    char sbuf[16];
                                    int at = 0;
                                    if (loc.seasons_mask & 1u) { sbuf[at++] = 'S'; sbuf[at++] = 'p'; }
                                    if (loc.seasons_mask & 2u) { if (at) sbuf[at++] = '/'; sbuf[at++] = 'S'; sbuf[at++] = 'u'; }
                                    if (loc.seasons_mask & 4u) { if (at) sbuf[at++] = '/'; sbuf[at++] = 'A'; sbuf[at++] = 'u'; }
                                    if (loc.seasons_mask & 8u) { if (at) sbuf[at++] = '/'; sbuf[at++] = 'W'; sbuf[at++] = 'i'; }
                                    sbuf[at] = '\0';
                                    ImGui::TextUnformatted(sbuf);
                                }
                            }
                            ImGui::EndTable();
                        }
                        if (dex_det->loc_truncated != 0u) {
                            ImGui::TextDisabled("(Showing first %u locations)",
                                                (unsigned)POKEBW_DEX_MAX_LOCATIONS);
                        }
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Save Backup Slots")) {
        int active_slot = host_gear_active_save_slot();
        ImGui::Text("Active eidos Save: Slot %d / 8", active_slot);
        ImGui::TextDisabled("Use Title Screen Continue (< Left / Right >) to switch slots 1-8.");
        ImGui::Spacing();

        for (int s = 1; s <= 3; s++) {
            PokebwBackupSlotInfo info;
            host_backup_slot_info(s, &info);
            ImGui::PushID(100 + s);
            if (info.exists) {
                ImGui::Text("Backup %d: %s (%.0f KiB)", s, info.timestamp,
                            static_cast<double>(info.size_bytes) / 1024.0);
            } else {
                ImGui::TextDisabled("Backup %d: Empty", s);
            }
            if (ImGui::Button("Backup")) {
                host_backup_slot_save(s);
                g_confirm_restore_slot = 0;
            }
            ImGui::SameLine();
            if (!info.exists) {
                ImGui::BeginDisabled();
            }
            if (g_confirm_restore_slot == s) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
                if (ImGui::Button("Confirm Restore?")) {
                    host_backup_slot_restore(s);
                    g_confirm_restore_slot = 0;
                }
                ImGui::PopStyleColor();
            } else {
                if (ImGui::Button("Restore")) {
                    g_confirm_restore_slot = s;
                }
            }
            if (!info.exists) {
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        if (host_backup_status()[0] != '\0') {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", host_backup_status());
        }
    }

    if (gear->valid != 0u && gear->ow_sprites_total > 0u) {
        unsigned synth = (gear->ow_sprites_total >= gear->ow_sprites_custom)
                             ? (unsigned)(gear->ow_sprites_total - gear->ow_sprites_custom)
                             : 0u;
        ImGui::Spacing();
        ImGui::TextDisabled("Overworld Sprite Archive: %u ready (%u custom .bwsp, %u ROM synth)",
                            (unsigned)gear->ow_sprites_total,
                            (unsigned)gear->ow_sprites_custom,
                            synth);
    }
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
                if (ImGui::BeginTabItem("Gear")) {
                    draw_gear_tab();
                    ImGui::EndTabItem();
                }

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
                    setting_checkbox("Following Pokemon",
                                     "Your first conscious party Pokemon walks behind you on the overworld. Press A facing it to hear its cry.",
                                     &cfg->following_pokemon);
                    setting_checkbox("Overworld Wild Pokemon",
                                     "Visible wild Pokemon roam tall grass, caves, and water using live Unova season tables. Touch or press A to battle.",
                                     &cfg->overworld_wilds);
                    ImGui::Indent();
                    if (cfg->overworld_wilds == 0) {
                        ImGui::BeginDisabled();
                    }
                    if (ImGui::SliderInt("Overworld Wild Density", &cfg->overworld_wild_density, 1, 6)) {
                        host_settings_save();
                    }
                    help_marker("Maximum simultaneous roaming wild Pokemon on the current map (1-6).");
                    if (cfg->overworld_wilds == 0) {
                        ImGui::EndDisabled();
                    }
                    ImGui::Unindent();
                    ImGui::Separator();
                    setting_checkbox("Portable PC Box Access",
                                     "Open Unova PC Boxes anywhere on the overworld (blocked during battles, cutscenes, or restricted areas).",
                                     &cfg->portable_pc);
                    if (cfg->portable_pc != 0) {
                        if (ImGui::Button("Open PC Box (Organize)")) {
                            host_pc_request_open(2, static_cast<unsigned long long>(SDL_GetTicks()));
                            g_open = 0;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Move Items")) {
                            host_pc_request_open(3, static_cast<unsigned long long>(SDL_GetTicks()));
                            g_open = 0;
                        }
                        if (host_pc_last_status() < 0) {
                            ImGui::TextDisabled("Last PC request was blocked (only available while free-roaming on the overworld).");
                        }
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("QoL")) {
                    static const char *const k_transitions[] = { "Original", "Fast", "Seamless" };
                    static const char *const k_ff_speeds[]   = { "2x", "3x", "4x", "Max (Uncapped)" };
                    static const char *const k_shiny_odds[]  = {
                        "1/8192 (Vanilla)",
                        "1/1024 (8x)",
                        "1/256 (32x)",
                        "1/64 (128x)",
                        "100% (Always Shiny)"
                    };

                    ImGui::Spacing();
                    ImGui::TextDisabled("Fast-Forward & Shiny Odds:");
                    ImGui::Spacing();
                    setting_checkbox("Fast-Forward Active",
                                     "Run overworld and battles at boosted speed. You can also tap the top-center FF pill anytime.",
                                     &cfg->fast_forward);
                    {
                        int ff_sp = std::clamp(cfg->fast_forward_speed, 0, 3);
                        if (ImGui::Combo("Fast-Forward Speed", &ff_sp, k_ff_speeds, 4)) {
                            cfg->fast_forward_speed = ff_sp;
                            host_settings_save();
                        }
                        int sh = std::clamp(cfg->shiny_odds, 0, 4);
                        if (ImGui::Combo("Shiny Odds", &sh, k_shiny_odds, 5)) {
                            cfg->shiny_odds = sh;
                            host_settings_save();
                        }
                        help_marker("Applies to wild Pokemon, overworld wilds, static legendaries, starters/gifts, and hatched eggs while preserving nature, gender, and ability.");
                    }
                    ImGui::Separator();
                    setting_checkbox("\"Use Another Repel?\" Prompt",
                                     "When Repel wears off, prompts to immediately apply another Repel from your Bag (Black 2 / White 2 style).",
                                     &cfg->repel_prompt);
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
                    setting_checkbox("Hard Level Cap",
                                     "Caps battle EXP at the next Unova Gym/League ace level (14/20/23/27/31/35/39/43/54). Turn off post-game for Lv. 100.",
                                     &cfg->hard_level_cap);
                    setting_checkbox("No Random Encounters",
                                     "Disables wild grass/cave/water step encounters (Sweet Scent and shaking-grass phenomena still work).",
                                     &cfg->no_random_encounters);
                    setting_checkbox("Always Bite When Fishing",
                                     "Guarantees a bite whenever fishing in valid water.",
                                     &cfg->always_bite_fishing);
                    setting_checkbox("Battle Effectiveness Pills",
                                     "Show 4x/2x/1/2/1/4/0x badges on move cards (Teravolt, Turboblaze, Mold Breaker, and ability-aware).",
                                     &cfg->type_effectiveness);
                    setting_checkbox("Poke Ball Quick-Throw Carousel",
                                     "Show a < [Poke Ball xQty] > quick-throw bar directly on the wild battle command screen for one-tap catching.",
                                     &cfg->ball_carousel);
                    {
                        static const char *const k_seasons[] = { "Auto", "Spring", "Summer", "Autumn", "Winter" };
                        static const char *const k_clocks[] = { "Auto", "Morning (07:00)", "Day (13:00)", "Evening (18:00)", "Night (22:00)" };
                        int season = std::clamp(cfg->season_override, 0, 4);
                        if (ImGui::Combo("Season Override", &season, k_seasons, 5)) {
                            cfg->season_override = season;
                            host_settings_save();
                        }
                        help_marker("Overrides Unova's season (takes full visual effect on next map/door transition).");
                        int clock_ov = std::clamp(cfg->clock_override, 0, 4);
                        if (ImGui::Combo("Time of Day Override", &clock_ov, k_clocks, 5)) {
                            cfg->clock_override = clock_ov;
                            host_settings_save();
                        }
                        help_marker("Overrides lighting, battle sky, and time-of-day evolutions without changing your save's calendar date.");
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("Save Data (Standard 512 KiB DS .sav):");
                    ImGui::Spacing();
                    ImGui::TextWrapped("Save Folder: %s", host_settings_effective_save_dir());
                    if (ImGui::Button("Choose Save Folder...")) {
                        host_pick_save_dir_begin();
                    }
                    ImGui::SameLine();
                    if (cfg->save_dir[0] == '\0') {
                        ImGui::BeginDisabled();
                    }
                    if (ImGui::Button("Reset to Default")) {
                        host_settings_set_save_dir("");
                    }
                    if (cfg->save_dir[0] == '\0') {
                        ImGui::EndDisabled();
                    }
                    if (host_save_dir_status()[0] != '\0') {
                        ImGui::TextWrapped("%s", host_save_dir_status());
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("Diagnostics & Crash Logs (Termux / Shizuku / ADB):");
                    ImGui::Spacing();
                    {
                        static char s_diag_status[640] = "";
                        ImGui::TextWrapped("Crash log: %s", host_crash_primary_path());
                        if (host_crash_has_log()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.35f, 1.0f),
                                               "Previous crash log found on disk.");
                        } else {
                            ImGui::TextDisabled("No prior crash log recorded.");
                        }
                        if (ImGui::Button("Export Debug & Crash Log")) {
                            host_crash_export_diag(s_diag_status, sizeof(s_diag_status));
                        }
                        help_marker("Bundles the latest crash log and stderr.txt into pokebw_diag.log in external app storage and your active save folder.");
                        if (s_diag_status[0] != '\0') {
                            ImGui::TextWrapped("%s", s_diag_status);
                        }
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (cfg->portable_pc != 0) {
            if (ImGui::Button("PC Box")) {
                host_pc_request_open(2, static_cast<unsigned long long>(SDL_GetTicks()));
                g_open = 0;
            }
            ImGui::SameLine();
        }
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
    host_pick_save_dir_pump();

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
