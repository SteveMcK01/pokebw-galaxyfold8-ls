/*
 * sdk/pokebw_qol.c -- Phase 2.1: QoL & Cheats.
 *
 * Wraps wild/WFBC/fishing encounter checks and season/clock getters via
 * -Wl,--wrap in CMakeLists.txt so players can toggle:
 *   - No Random Encounters (POKEBW_SET_NO_RANDOM_ENCOUNTERS)
 *   - Always Bite When Fishing (POKEBW_SET_ALWAYS_BITE_FISHING)
 *   - Season Switcher (POKEBW_SET_SEASON_OVERRIDE: Auto/Spring/Summer/Autumn/Winter)
 *   - Clock / Time-of-Day Switcher (POKEBW_SET_CLOCK_OVERRIDE: Auto/Morning/Day/Evening/Night)
 *
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>

#include "pokebw_settings_ids.h"

/* ABI-matched view of aletheia/src/game/sdk_rtc.h RTCTime. */
struct PokebwRtcTime {
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
};

typedef char pokebw_rtc_time_size_check[
    sizeof(struct PokebwRtcTime) == 12u ? 1 : -1];

extern uint8_t __real_GAME_DATA_GetSeason(const void *gd);
extern void __real_GF_STANDARD_GetCachedTime(struct PokebwRtcTime *time);
extern void __real_GF_STANDARD_CopyCachedRtc(void *date, struct PokebwRtcTime *time);
extern uint32_t __real_GF_STANDARD_GetSecondsOfDay(void);

extern void *__real_ov021_FIELD_ENCOUNT_CheckWildEncount(void *encount, int triggerMode);
extern void *__real_ov021_FIELD_ENCOUNT_CheckWfbcEncount(void *encount, void *wfbc);
extern void *__real_ov021_FIELD_ENCOUNT_CheckFishingEncount(void *encount, int triggerMode);

static int clock_override_hour(void)
{
    switch (bw_pokebw_setting(POKEBW_SET_CLOCK_OVERRIDE)) {
    case 1:  return 7;   /* Morning */
    case 2:  return 13;  /* Day */
    case 3:  return 18;  /* Evening */
    case 4:  return 22;  /* Night */
    default: return -1;  /* Auto */
    }
}

uint8_t __wrap_GAME_DATA_GetSeason(const void *gd)
{
    int ov = bw_pokebw_setting(POKEBW_SET_SEASON_OVERRIDE);

    if (ov >= 1 && ov <= 4) {
        return (uint8_t)(ov - 1);
    }
    return __real_GAME_DATA_GetSeason(gd);
}

void __wrap_GF_STANDARD_GetCachedTime(struct PokebwRtcTime *time)
{
    int h = clock_override_hour();

    __real_GF_STANDARD_GetCachedTime(time);
    if (time != NULL && h >= 0) {
        time->hour = (uint32_t)h;
        time->minute = 0u;
        time->second = 0u;
    }
}

void __wrap_GF_STANDARD_CopyCachedRtc(void *date, struct PokebwRtcTime *time)
{
    int h = clock_override_hour();

    __real_GF_STANDARD_CopyCachedRtc(date, time);
    if (time != NULL && h >= 0) {
        time->hour = (uint32_t)h;
        time->minute = 0u;
        time->second = 0u;
    }
}

uint32_t __wrap_GF_STANDARD_GetSecondsOfDay(void)
{
    int h = clock_override_hour();

    if (h >= 0) {
        return (uint32_t)h * 3600u;
    }
    return __real_GF_STANDARD_GetSecondsOfDay();
}

void *__wrap_ov021_FIELD_ENCOUNT_CheckWildEncount(void *encount, int triggerMode)
{
    /* triggerMode 0 = normal step/surf roll; 1 = Sweet Scent, 2 = Phenomenon. */
    if (triggerMode == 0 && bw_pokebw_setting(POKEBW_SET_NO_RANDOM_ENCOUNTERS)) {
        return NULL;
    }
    return __real_ov021_FIELD_ENCOUNT_CheckWildEncount(encount, triggerMode);
}

void *__wrap_ov021_FIELD_ENCOUNT_CheckWfbcEncount(void *encount, void *wfbc)
{
    if (bw_pokebw_setting(POKEBW_SET_NO_RANDOM_ENCOUNTERS)) {
        return NULL;
    }
    return __real_ov021_FIELD_ENCOUNT_CheckWfbcEncount(encount, wfbc);
}

void *__wrap_ov021_FIELD_ENCOUNT_CheckFishingEncount(void *encount, int triggerMode)
{
    void *res = __real_ov021_FIELD_ENCOUNT_CheckFishingEncount(encount, triggerMode);

    if (res == NULL && triggerMode == 0 && bw_pokebw_setting(POKEBW_SET_ALWAYS_BITE_FISHING)) {
        int tries;
        for (tries = 0; tries < 15 && res == NULL; tries++) {
            res = __real_ov021_FIELD_ENCOUNT_CheckFishingEncount(encount, triggerMode);
        }
    }
    return res;
}
