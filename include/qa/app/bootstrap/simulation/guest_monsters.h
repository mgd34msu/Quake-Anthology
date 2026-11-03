/* Port of src/app/bootstrap/simulation/guest-monsters.ts: selected monsters
 * on a black-box native world. The guest spawns what the entity string
 * says, so replaced authored monsters are removed from that string and
 * admitted as shared monsters instead. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_GUEST_MONSTERS_H
#define QA_APP_BOOTSTRAP_SIMULATION_GUEST_MONSTERS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q2/foundation/host.h"
#include "qa/contracts/content.h"

/* TS GuestMonsterReplacement: fields borrow the plan's parsed entity. */
typedef struct qa_app_sim_guest_monster_replacement {
  double ordinal; const char *classname;
  const qa_q2_spawn_fields *fields;
  qa_monster_definition_reference definition;
} qa_app_sim_guest_monster_replacement;
typedef struct qa_app_sim_guest_monster_kept { const char *classname; char *reason; /* owned */ } qa_app_sim_guest_monster_kept;
/* TS GuestMonsterPlan. Owns its entity text, arrays and the parsed authored
 * entities the replacements borrow. */
typedef struct qa_app_sim_guest_monster_plan {
  char *entities;
  qa_app_sim_guest_monster_replacement *replacements; size_t replacement_count;
  qa_app_sim_guest_monster_kept *kept; size_t kept_count;
  qa_q2_spawn_fields *authored; size_t authored_count;
} qa_app_sim_guest_monster_plan;
/* TS filterEntityText(text, keep): malloc'd; ordinal 0 always kept. */
char *qa_app_sim_filter_entity_text(const char *text, const double *keep, size_t keep_count);
/* TS planGuestMonsters(entities, edition, authoredFamily, selection, skill,
 * mode, projection). selection must be a "replace" selection; its strings
 * are borrowed. authored_family "q1" | "q2" | "q3"; mode
 * "singleplayer" | "coop" | "deathmatch". Malloc'd; free with _destroy. */
qa_app_sim_guest_monster_plan *qa_app_sim_plan_guest_monsters(const char *entities, qa_q2_edition edition, const char *authored_family,
    const qa_enemy_selection *selection, double skill, const char *mode, bool projection);
void qa_app_sim_guest_monster_plan_destroy(qa_app_sim_guest_monster_plan *plan);
#endif
