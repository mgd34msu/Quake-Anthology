/*
 * Port of src/content/monsters/roster.ts: campaign monster role
 * slots (sorted by classname with TS localeCompare order) and the
 * default cross-family replacement roster. defaultMonsterRoster
 * returns a malloc'd by_classname list the caller frees with
 * qa_default_monster_roster_free; NULL out / false return is OOM
 * only (an unknown target provider aborts with the TS RangeError
 * message). GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MONSTERS_ROSTER_H
#define QA_CONTENT_MONSTERS_ROSTER_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"

/* TS MonsterRole (exact strings): "trooper"|"ranged"|"melee"|
 * "leaper"|"grenadier"|"rocket"|"artillery"|"hybrid"|"heavy"|
 * "flying"|"aquatic"|"boss"|"special". */
typedef const char *qa_monster_role;

typedef struct qa_monster_roster_slot {
  const char *classname;
  qa_monster_role role;
} qa_monster_roster_slot;

/* TS campaignMonsterSlots; *out_count set (q3 yields count 0). */
const qa_monster_roster_slot *qa_campaign_monster_slots(qa_game_family family,
                                                        size_t *out_count);

/* TS defaultMonsterRoster. `authored_family` is "q1" or "q2".
 * `overrides` may be NULL when override_count is 0. On success
 * *out is a "replace" selection whose by_classname list is
 * malloc'd; returns false only on OOM. */
bool qa_default_monster_roster(const char *authored_family,
                               qa_provider_reference target,
                               const qa_monster_by_classname *overrides,
                               size_t override_count,
                               qa_enemy_selection *out);
void qa_default_monster_roster_free(qa_enemy_selection *selection);

#endif
