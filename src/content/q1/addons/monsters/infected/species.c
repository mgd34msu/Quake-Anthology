/* Port of src/content/q1/addons/monsters/infected/species.ts.
 * quakec_mg3/monsters/mg3_*_infected.qc. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/infected/species.h"

#include <string.h>

#include "qa/content/q1/addons/monsters/ordinary/army.h"
#include "../support.h"

const qa_q1am_infected_classname qa_q1am_infected_classnames[] = {
  {"monster_army_infected", "army"}, {"monster_knight_infected", "knight"},
  {"monster_enforcer_infected", "enforcer"}, {"monster_hell_knight_infected", "hellknight"},
};
const size_t qa_q1am_infected_classname_count = sizeof qa_q1am_infected_classnames / sizeof qa_q1am_infected_classnames[0];
static const qa_bounds large_bounds = {{-32, -32, -24}, {32, 32, 64}};

const char *qa_q1am_infected_kind(const qa_q1_actor *entity) {
  const char *kind = qa_q1_actor_text(entity, "infected.kind");
  static const char *const kinds[] = {"army", "knight", "enforcer", "hellknight"};
  for (size_t i = 0; i < 4; i++)
    if (strcmp(kind, kinds[i]) == 0) return kinds[i];
  qa_range_error("Missing MG3 infected variant on %s", entity->classname);
}
void qa_q1am_infected_species(const qa_q1_actor *entity, qa_q1_base_monster_species *out) {
  const char *kind = qa_q1am_infected_kind(entity);
  const bool transformed = qa_q1_actor_number(entity, "infected.transformed") != 0;
  const char *species = transformed ? (strcmp(kind, "army") == 0 || strcmp(kind, "knight") == 0 ? "zombie" : "demon") : kind;
  if (strcmp(species, "army") == 0) { *out = qa_q1am_army_species; return; }
  const qa_q1_base_monster_species *source = NULL;
  for (size_t i = 0; i < QA_Q1_BASE_SPECIES_COUNT && source == NULL; i++)
    if (strcmp(QA_Q1_BASE_SPECIES[i].species, species) == 0) source = &QA_Q1_BASE_SPECIES[i];
  if (source == NULL) qa_range_error("Missing base species %s", species);
  *out = *source;
  if (strcmp(species, "zombie") == 0) { out->missile = NULL; out->melee = true; return; }
  if (strcmp(species, "enforcer") == 0) { out->bounds = large_bounds; return; }
  if (strcmp(species, "hellknight") != 0 || qa_q1_actor_number(entity, "infected.risen") != 0) return;
  const int32_t flags = QA_Q1AM_FLAGS(entity->spawnflags);
  const int corpse = (flags & 65536) != 0 ? 1 : (flags & 8388608) != 0 ? 2 : 0;
  if (corpse == 0) return;
  out->stand = corpse == 1 ? "hknight_corpse1" : "hknight_corpse2";
  out->run = corpse == 1 ? "hknight_corpse1_rise0" : "hknight_corpse2_rise0";
}
