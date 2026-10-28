/* Port of src/app/bootstrap/simulation/guest-monsters.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_GUEST_MONSTERS_H
#define QA_APP_BOOTSTRAP_SIMULATION_GUEST_MONSTERS_H
#include <stddef.h>
typedef struct qa_app_sim_guest_monster_field { const char *key; const char *value; } qa_app_sim_guest_monster_field;
typedef struct qa_app_sim_guest_monster_replacement {
  double ordinal; const char *classname;
  const qa_app_sim_guest_monster_field *fields; size_t field_count;
  const char *definition_provider; const char *definition_content; const char *definition_classname;
} qa_app_sim_guest_monster_replacement;
typedef struct qa_app_sim_guest_monster_kept { const char *classname; const char *reason; } qa_app_sim_guest_monster_kept;
typedef struct qa_app_sim_guest_monster_plan {
  char *entities; /* owned */
  qa_app_sim_guest_monster_replacement *replacements; size_t replacement_count;
  qa_app_sim_guest_monster_kept *kept; size_t kept_count;
} qa_app_sim_guest_monster_plan;
/* Malloc'd filtered text; ordinal 0 always kept. */
char *qa_app_sim_filter_entity_text(const char *text, const double *keep, size_t keep_count);
void qa_app_sim_guest_monster_plan_destroy(qa_app_sim_guest_monster_plan *plan);
#endif
