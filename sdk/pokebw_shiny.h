/*
 * sdk/pokebw_shiny.h -- Phase 4.2: Configurable Shiny Odds Multiplier.
 */

#ifndef POKEBW_SHINY_H
#define POKEBW_SHINY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Non-zero when POKEBW_SET_SHINY_ODDS is boosted above vanilla (1/8192). */
int pokebw_shiny_is_boosted(void);

/* Full shiny roll against configured odds (1/8192, 1/1024, 1/256, 1/64, 100%). */
int pokebw_shiny_roll(void);

/* Conditional bonus roll when a vanilla 1/8192 roll missed. */
int pokebw_shiny_roll_boosted(void);

/* Gen 5 shiny test: (((ot_id >> 16) ^ (ot_id & 0xFFFF) ^ (pid >> 16) ^ (pid & 0xFFFF)) < 8). */
int pokebw_shiny_is_pid_shiny(uint32_t ot_id, uint32_t pid);

/* Re-compute upper 16 bits of pid so it becomes shiny for ot_id while preserving
 * gender (low 8 bits), ability slot (bit 16), and separate Gen 5 nature. */
uint32_t pokebw_shiny_force_pid(uint32_t ot_id, uint32_t pid);

/* Bracket FIELD_ENCOUNT_CreatePersonality so POKE_TOOL_CreatePersonalRnd wrap
 * yields until FIELD_ENCOUNT_CreatePersonality finishes its bit-31 assignment. */
void pokebw_shiny_enter_wild(void);
void pokebw_shiny_leave_wild(void);

#ifdef __cplusplus
}
#endif

#endif /* POKEBW_SHINY_H */
