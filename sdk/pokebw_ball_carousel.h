/*
 * sdk/pokebw_ball_carousel.h -- Phase 4.5: Battle Poké Ball Quick-Throw Carousel.
 */

#ifndef POKEBW_BALL_CAROUSEL_H
#define POKEBW_BALL_CAROUSEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PokebwBallEntry {
    uint16_t item_id;
    uint16_t qty;
} PokebwBallEntry;

/*
 * Populate `out[0..max_entries-1]` with all Poké Balls currently in the
 * player's Bag when a Poké Ball can legally be thrown on the active turn
 * (wild battle, non-comm, single living foe, foe not semi-invulnerable,
 * catching allowed, slotIndex == slotsDone).
 * Returns the number of distinct Poké Ball stacks written (0 if unavailable).
 */
int  pokebw_ball_carousel_query(void *core_ptr, PokebwBallEntry *out,
                                int max_entries, uint16_t *out_last_ball);

/* Queue a one-tap Poké Ball throw for the next BtlClient action poll. */
void pokebw_ball_carousel_request_throw(uint16_t item_id);

/* Cancel any queued quick-throw (called if the player picks another menu). */
void pokebw_ball_carousel_clear_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* POKEBW_BALL_CAROUSEL_H */
