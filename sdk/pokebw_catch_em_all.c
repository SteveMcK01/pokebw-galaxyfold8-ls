/*
 * sdk/pokebw_catch_em_all.c -- Phase 1.4: Gotta Catch 'Em All! Mode.
 *
 * Wraps ov010_FIELD_ENCOUNT_ST_LoadEncountData via -Wl,--wrap so when
 * POKEBW_SET_CATCH_EM_ALL is enabled:
 *   - Black & White version exclusives are merged across Unova habitats.
 *   - Built-in event/mythical encounters (Victini, Zorua, Zoroark, Keldeo,
 *     Meloetta, Genesect, opposite-version cover legendaries/genies) are
 *     catchable in natural Unova locations without Mystery Gift or trading.
 *   - Classic Gens 1-4 National Dex species (including all Kanto/Johto/Hoenn/
 *     Sinnoh starters and pseudo-legendaries) appear in routes, caves,
 *     waters, and phenomena.
 *
 * Only modifies already-populated encounter slots (preserving level ranges
 * and leaving empty encounter tables untouched).
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>

#include "pokebw_settings_ids.h"

/* Matches EncountSlot & EncountData in aletheia/src/game/ov021/field_encount_types.h. */
struct PokebwEncountSlot {
    uint16_t species : 11;
    uint16_t form : 5;
    uint8_t  minLevel;
    uint8_t  maxLevel;
};

struct PokebwEncountData {
    uint8_t                  rate[7];        /* 0x00: 0 grass, 1 dark, 2 spot, 3 surf, 4 ripple, 5 fish, 6 fish-spot */
    uint8_t                  flags;          /* 0x07 */
    struct PokebwEncountSlot ground[3][12];  /* 0x08 */
    struct PokebwEncountSlot water[2][5];    /* 0x98 */
    struct PokebwEncountSlot fishing[2][5];  /* 0xC0 */
};

typedef char pokebw_encount_slot_size_check[
    sizeof(struct PokebwEncountSlot) == 4u ? 1 : -1];
typedef char pokebw_encount_ground_off_check[
    offsetof(struct PokebwEncountData, ground) == 0x08u ? 1 : -1];
typedef char pokebw_encount_water_off_check[
    offsetof(struct PokebwEncountData, water) == 0x98u ? 1 : -1];
typedef char pokebw_encount_fishing_off_check[
    offsetof(struct PokebwEncountData, fishing) == 0xC0u ? 1 : -1];
typedef char pokebw_encount_data_size_check[
    sizeof(struct PokebwEncountData) == 0xE8u ? 1 : -1];

int __real_ov010_FIELD_ENCOUNT_ST_LoadEncountData(
    struct PokebwEncountData *row,
    void *arc,
    uint32_t zoneId,
    int seasonIndex);

static void set_slot(struct PokebwEncountSlot *s, uint16_t species)
{
    if (s == NULL || s->species == 0 || s->minLevel == 0) {
        return;
    }
    s->species = species;
    s->form = 0;
}

static void pokebw_apply_catch_em_all(struct PokebwEncountData *row, uint32_t zoneId)
{
    switch (zoneId) {
    case 317: /* Route 1 */
        set_slot(&row->ground[0][6], 495);  /* Snivy */
        set_slot(&row->ground[0][8], 16);   /* Pidgey */
        set_slot(&row->ground[1][6], 133);  /* Eevee */
        set_slot(&row->ground[2][4], 1);    /* Bulbasaur */
        set_slot(&row->ground[2][5], 4);    /* Charmander */
        set_slot(&row->ground[2][6], 7);    /* Squirtle */
        break;

    case 319: /* Route 2 */
        set_slot(&row->ground[0][6], 498);  /* Tepig */
        set_slot(&row->ground[0][7], 25);   /* Pikachu */
        set_slot(&row->ground[0][8], 58);   /* Growlithe */
        set_slot(&row->ground[0][9], 37);   /* Vulpix */
        break;

    case 321: /* Route 3 */
        set_slot(&row->ground[0][6], 501);  /* Oshawott */
        set_slot(&row->ground[0][8], 403);  /* Shinx */
        set_slot(&row->ground[1][6], 447);  /* Riolu */
        set_slot(&row->ground[2][4], 152);  /* Chikorita */
        set_slot(&row->ground[2][5], 155);  /* Cyndaquil */
        set_slot(&row->ground[2][6], 158);  /* Totodile */
        break;

    case 324: /* Wellspring Cave */
    case 325:
        set_slot(&row->ground[0][6], 41);   /* Zubat */
        set_slot(&row->ground[0][7], 74);   /* Geodude */
        set_slot(&row->ground[0][8], 304);  /* Aron */
        set_slot(&row->ground[2][4], 443);  /* Gible (dust cloud) */
        break;

    case 152: /* Dreamyard */
    case 153:
        set_slot(&row->ground[0][4], 511);  /* Pansage */
        set_slot(&row->ground[0][5], 513);  /* Pansear */
        set_slot(&row->ground[0][6], 515);  /* Panpour */
        set_slot(&row->ground[0][7], 63);   /* Abra */
        set_slot(&row->ground[0][8], 280);  /* Ralts */
        set_slot(&row->ground[0][9], 133);  /* Eevee */
        break;

    case 154: /* Pinwheel Forest (outside & inside) */
    case 155:
        set_slot(&row->ground[0][4], 546);  /* Cottonee (Black) */
        set_slot(&row->ground[0][5], 548);  /* Petilil (White) */
        set_slot(&row->ground[0][6], 539);  /* Sawk */
        set_slot(&row->ground[0][7], 538);  /* Throh */
        set_slot(&row->ground[0][8], 123);  /* Scyther */
        set_slot(&row->ground[0][9], 285);  /* Shroomish */
        set_slot(&row->ground[1][6], 214);  /* Heracross */
        set_slot(&row->ground[2][4], 252);  /* Treecko */
        set_slot(&row->ground[2][5], 255);  /* Torchic */
        set_slot(&row->ground[2][6], 258);  /* Mudkip */
        set_slot(&row->ground[2][8], 570);  /* Zorua */
        break;

    case 157: /* Desert Resort */
    case 158:
        set_slot(&row->ground[0][6], 328);  /* Trapinch */
        set_slot(&row->ground[0][7], 207);  /* Gligar */
        set_slot(&row->ground[0][8], 246);  /* Larvitar */
        set_slot(&row->ground[0][9], 564);  /* Tirtouga */
        set_slot(&row->ground[0][10], 566); /* Archen */
        set_slot(&row->ground[2][10], 494); /* Victini (rare spot) */
        break;

    case 160: /* Relic Castle */
    case 161:
    case 162:
    case 163:
        set_slot(&row->ground[0][6], 564);  /* Tirtouga */
        set_slot(&row->ground[0][7], 566);  /* Archen */
        set_slot(&row->ground[0][8], 246);  /* Larvitar */
        set_slot(&row->ground[2][10], 494); /* Victini */
        break;

    case 329: /* Route 5 */
    case 383: /* Route 16 */
        set_slot(&row->ground[0][4], 574);  /* Gothita (Black) */
        set_slot(&row->ground[0][5], 577);  /* Solosis (White) */
        set_slot(&row->ground[0][7], 125);  /* Electabuzz */
        set_slot(&row->ground[0][8], 126);  /* Magmar */
        set_slot(&row->ground[1][6], 143);  /* Snorlax */
        set_slot(&row->ground[2][4], 387);  /* Turtwig */
        set_slot(&row->ground[2][5], 390);  /* Chimchar */
        set_slot(&row->ground[2][6], 393);  /* Piplup */
        break;

    case 385: /* Lostlorn Forest */
        set_slot(&row->ground[0][4], 574);  /* Gothita */
        set_slot(&row->ground[0][5], 577);  /* Solosis */
        set_slot(&row->ground[0][6], 570);  /* Zorua (built-in event) */
        set_slot(&row->ground[1][6], 571);  /* Zoroark (dark grass) */
        set_slot(&row->ground[2][6], 570);  /* Zorua (phenomena) */
        set_slot(&row->ground[2][8], 571);  /* Zoroark (phenomena) */
        break;

    case 195: /* Chargestone Cave */
    case 196:
    case 197:
        set_slot(&row->ground[0][6], 81);   /* Magnemite */
        set_slot(&row->ground[0][7], 239);  /* Elekid */
        set_slot(&row->ground[0][8], 374);  /* Beldum */
        set_slot(&row->ground[2][4], 479);  /* Rotom (dust cloud) */
        break;

    case 339: /* Celestial Tower */
    case 340:
    case 341:
    case 342:
        set_slot(&row->ground[0][6], 92);   /* Gastly */
        set_slot(&row->ground[0][7], 355);  /* Duskull */
        set_slot(&row->ground[0][8], 200);  /* Misdreavus */
        set_slot(&row->ground[0][9], 442);  /* Spiritomb */
        break;

    case 346: /* Moor of Icirrus */
        set_slot(&row->water[0][3], 647);   /* Keldeo (surf) */
        set_slot(&row->water[1][3], 647);   /* Keldeo (rippling water) */
        break;

    case 198: /* Twist Mountain */
    case 199:
    case 200:
    case 201:
    case 202:
    case 203:
        set_slot(&row->ground[0][6], 220);  /* Swinub */
        set_slot(&row->ground[0][7], 215);  /* Sneasel */
        set_slot(&row->ground[0][8], 361);  /* Snorunt */
        set_slot(&row->ground[0][9], 111);  /* Rhyhorn */
        break;

    case 205: /* Dragonspiral Tower */
    case 206:
    case 207:
    case 208:
        set_slot(&row->ground[0][6], 147);  /* Dratini */
        set_slot(&row->ground[0][7], 371);  /* Bagon */
        set_slot(&row->ground[0][8], 443);  /* Gible */
        set_slot(&row->ground[2][8], 643);  /* Reshiram (rare spot) */
        set_slot(&row->ground[2][9], 644);  /* Zekrom (rare spot) */
        break;

    case 348: /* Route 9 */
        set_slot(&row->ground[0][4], 575);  /* Gothorita (Black) */
        set_slot(&row->ground[0][5], 578);  /* Duosion (White) */
        break;

    case 355: /* Route 10 */
    case 358:
        set_slot(&row->ground[0][4], 629);  /* Vullaby (Black) */
        set_slot(&row->ground[0][5], 627);  /* Rufflet (White) */
        set_slot(&row->ground[2][8], 641);  /* Tornadus (rare spot) */
        set_slot(&row->ground[2][9], 642);  /* Thundurus (rare spot) */
        break;

    case 365: /* Route 11 */
        set_slot(&row->ground[0][4], 629);  /* Vullaby */
        set_slot(&row->ground[0][5], 627);  /* Rufflet */
        break;

    case 255: /* Village Bridge */
        set_slot(&row->ground[0][4], 630);  /* Mandibuzz */
        set_slot(&row->ground[0][5], 628);  /* Braviary */
        set_slot(&row->ground[2][10], 648); /* Meloetta (rare spot) */
        break;

    case 374: /* Route 14 */
    case 376: /* Abundant Shrine */
        set_slot(&row->ground[0][4], 200);  /* Misdreavus (White) */
        set_slot(&row->ground[0][5], 198);  /* Murkrow (Black) */
        set_slot(&row->ground[0][6], 228);  /* Houndour (Black) */
        set_slot(&row->ground[0][7], 261);  /* Poochyena (White) */
        set_slot(&row->ground[0][8], 37);   /* Vulpix */
        set_slot(&row->ground[0][9], 58);   /* Growlithe */
        break;

    case 235: /* Liberty Garden */
    case 236:
    case 237:
        set_slot(&row->ground[0][6], 494);  /* Victini */
        set_slot(&row->ground[2][4], 494);  /* Victini */
        break;

    case 238: /* P2 Laboratory */
    case 239:
        set_slot(&row->ground[2][10], 649); /* Genesect (rare spot) */
        break;

    case 240: /* Undella Bay */
    case 423: /* Route 17 */
    case 387: /* Route 18 */
        set_slot(&row->water[0][2], 131);   /* Lapras */
        set_slot(&row->water[0][3], 120);   /* Staryu */
        set_slot(&row->water[1][2], 349);   /* Feebas */
        set_slot(&row->fishing[0][2], 129); /* Magikarp */
        set_slot(&row->fishing[1][2], 116); /* Horsea */
        break;

    case 215: /* Victory Road */
    case 216:
    case 217:
    case 218:
    case 219:
        set_slot(&row->ground[0][6], 375);  /* Metang */
        set_slot(&row->ground[0][7], 247);  /* Pupitar */
        set_slot(&row->ground[0][8], 444);  /* Gabite */
        set_slot(&row->ground[0][9], 372);  /* Shelgon */
        break;

    case 230: /* Giant Chasm */
    case 231:
    case 232:
    case 234:
        set_slot(&row->ground[0][6], 148);  /* Dragonair */
        set_slot(&row->ground[0][7], 375);  /* Metang */
        set_slot(&row->ground[0][8], 448);  /* Lucario */
        set_slot(&row->ground[1][6], 247);  /* Pupitar */
        break;

    default:
        break;
    }
}

int __wrap_ov010_FIELD_ENCOUNT_ST_LoadEncountData(
    struct PokebwEncountData *row,
    void *arc,
    uint32_t zoneId,
    int seasonIndex)
{
    int ok = __real_ov010_FIELD_ENCOUNT_ST_LoadEncountData(row, arc, zoneId, seasonIndex);

    if (ok != 0 && row != NULL && bw_pokebw_setting(POKEBW_SET_CATCH_EM_ALL)) {
        pokebw_apply_catch_em_all(row, zoneId);
    }
    return ok;
}
