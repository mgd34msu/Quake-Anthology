/* Port of src/content/q1/base/species.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_SPECIES_H
#define QA_CONTENT_Q1_BASE_SPECIES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/foundation/entity_services.h"

/* TS BaseSpecies is the species string subset; MonsterSpecies.species
 * is Q1Monster["species"] and stays a C string. */
typedef struct qa_q1_base_monster_species {
  const char *species;
  const char *kill_string; /* NULL = absent */
  const char *const *classnames; size_t classname_count;
  const char *model;
  const char *head; /* NULL = TS null */
  double health; double gib_health;
  const char *const *gibs; size_t gib_count;
  qa_bounds bounds;
  const char *stand; const char *walk; const char *run; const char *sight;
  const char *missile; /* NULL = TS null */
  bool melee;
  const char *movement; /* "walk"|"fly"|"swim"|"boss" */
} qa_q1_base_monster_species;

extern const qa_q1_base_monster_species QA_Q1_BASE_SPECIES[];
extern const size_t QA_Q1_BASE_SPECIES_COUNT;

void qa_q1_precache_id1_monster(qa_q1_services *game, const char *species);

#endif
