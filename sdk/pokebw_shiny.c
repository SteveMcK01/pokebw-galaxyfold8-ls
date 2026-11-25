/*
 * sdk/pokebw_shiny.c -- Phase 4.2: Shiny Odds Multiplier.
 *
 * Wraps POKE_TOOL_CreatePersonalRnd and ov021_SODATEYA_ApplyMasudaMethod via
 * -Wl,--wrap in CMakeLists.txt, and provides helper rolls/PID transforms used by
 * sdk/pokebw_wild.c for random/static encounters and overworld spawns.
 *
 * Preserves gender (pid & 0xFF), ability slot ((pid >> 16) & 1), and Gen 5's
 * separate nature field whenever forcing a non-shiny PID to become shiny.
 *
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>

#include "pokebw_shiny.h"
#include "pokebw_settings_ids.h"

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_SHINY_WEAK __attribute__((weak))
#else
#define POKEBW_SHINY_WEAK
#endif

struct PokebwDayCareEgg {
    uint32_t species;            /* 0x00 */
    uint32_t form;               /* 0x04 */
    uint32_t level;              /* 0x08 */
    uint32_t nature;             /* 0x0C */
    uint32_t ability;            /* 0x10 */
    uint32_t baseFriendship;     /* 0x14 */
    uint32_t ivs[6];             /* 0x18 */
    uint32_t otId;               /* 0x30 */
    uint32_t personalRnd;        /* 0x34 */
    uint32_t moveCount;          /* 0x38 */
    uint32_t moves[4];           /* 0x3C */
    uint32_t abilitySlot;        /* 0x4C */
    uint32_t natureFromFather;   /* 0x50 */
    uint32_t natureFromMother;   /* 0x54 */
    uint32_t fatherIvFlags[6];   /* 0x58 */
    uint32_t motherIvFlags[6];   /* 0x70 */
    uint32_t inheritedIvCount;   /* 0x88 */
};

typedef char pokebw_daycare_egg_size_check[
    sizeof(struct PokebwDayCareEgg) == 0x8Cu ? 1 : -1];

extern uint32_t __real_POKE_TOOL_CreatePersonalRnd(
    uint32_t otId, int species, uint16_t form,
    int ratioMode, int shinyWanted, int mode);
extern void __real_ov021_SODATEYA_ApplyMasudaMethod(void **parents, void *egg_ptr);
extern uint32_t GFL_USE_GetRandom(uint32_t bound) POKEBW_SHINY_WEAK;

static uint32_t g_shiny_rng = 0x9E3779B9u;
static int g_in_field_encount = 0;

static uint32_t shiny_rand_u32(void)
{
    uint32_t x = g_shiny_rng;
    if (GFL_USE_GetRandom != NULL) {
        x ^= GFL_USE_GetRandom(0u);
    }
    x ^= 0xA5A55A5Au;
    if (x == 0u) {
        x = 0x6D2B79F5u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_shiny_rng = x;
    return x;
}

static uint32_t shiny_denom(void)
{
    switch (bw_pokebw_setting(POKEBW_SET_SHINY_ODDS)) {
    case 1:  return 1024u;
    case 2:  return 256u;
    case 3:  return 64u;
    case 4:  return 1u;
    default: return 8192u;
    }
}

int pokebw_shiny_is_boosted(void)
{
    int opt = bw_pokebw_setting(POKEBW_SET_SHINY_ODDS);
    return (opt >= 1 && opt <= 4) ? 1 : 0;
}

int pokebw_shiny_roll(void)
{
    uint32_t denom = shiny_denom();
    if (denom <= 1u) {
        return 1;
    }
    return ((shiny_rand_u32() % denom) == 0u) ? 1 : 0;
}

int pokebw_shiny_roll_boosted(void)
{
    uint32_t denom = shiny_denom();
    uint32_t extra;

    if (denom >= 8192u) {
        return 0;
    }
    if (denom <= 1u) {
        return 1;
    }
    /* Condition on a vanilla 1/8192 miss: total P(shiny) = 1/8192 + (8191/8192)*(extra/8191) = 1/denom. */
    extra = (8192u / denom) - 1u;
    return ((shiny_rand_u32() % 8191u) < extra) ? 1 : 0;
}

int pokebw_shiny_is_pid_shiny(uint32_t ot_id, uint32_t pid)
{
    uint32_t x = ((ot_id >> 16) ^ (ot_id & 0xFFFFu) ^
                  (pid >> 16) ^ (pid & 0xFFFFu)) & 0xFFFFu;
    return (x < 8u) ? 1 : 0;
}

uint32_t pokebw_shiny_force_pid(uint32_t ot_id, uint32_t pid)
{
    uint32_t low16    = pid & 0xFFFFu;
    uint32_t abil_bit = (pid >> 16) & 1u;
    uint32_t ot_xor   = ((ot_id >> 16) ^ (ot_id & 0xFFFFu)) & 0xFFFFu;
    uint32_t high16   = ((ot_xor ^ low16) & 0xFFFEu) | abil_bit;
    return (high16 << 16) | low16;
}

void pokebw_shiny_enter_wild(void)
{
    g_in_field_encount++;
}

void pokebw_shiny_leave_wild(void)
{
    if (g_in_field_encount > 0) {
        g_in_field_encount--;
    }
}

uint32_t __wrap_POKE_TOOL_CreatePersonalRnd(
    uint32_t otId, int species, uint16_t form,
    int ratioMode, int shinyWanted, int mode)
{
    uint32_t pid = __real_POKE_TOOL_CreatePersonalRnd(
        otId, species, form, ratioMode, shinyWanted, mode);

    if (g_in_field_encount == 0 && mode == 2 && pokebw_shiny_is_boosted() &&
        !pokebw_shiny_is_pid_shiny(otId, pid) && pokebw_shiny_roll_boosted()) {
        return pokebw_shiny_force_pid(otId, pid);
    }
    return pid;
}

void __wrap_ov021_SODATEYA_ApplyMasudaMethod(void **parents, void *egg_ptr)
{
    struct PokebwDayCareEgg *egg = (struct PokebwDayCareEgg *)egg_ptr;

    __real_ov021_SODATEYA_ApplyMasudaMethod(parents, egg_ptr);
    if (egg != NULL && pokebw_shiny_is_boosted() &&
        !pokebw_shiny_is_pid_shiny(egg->otId, egg->personalRnd) &&
        pokebw_shiny_roll_boosted()) {
        egg->personalRnd = pokebw_shiny_force_pid(egg->otId, egg->personalRnd);
    }
}
