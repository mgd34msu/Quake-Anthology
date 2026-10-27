/* Port of src/content/q1/addons/monsters/infected/species.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/infected/species.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/ordinary/army.h"
#include "qa/foundation/support.h"

#define FLAGS(x) ((int32_t)(x))

const qa_q1am_infected_classname qa_q1am_infected_classnames[] = {
  {"monster_army_infected", "army"},
  {"monster_knight_infected", "knight"},
  {"monster_enforcer_infected", "enforcer"},
  {"monster_hell_knight_infected", "hellknight"},
};
const size_t qa_q1am_infected_classname_count =
    sizeof qa_q1am_infected_classnames / sizeof qa_q1am_infected_classnames[0];
static const qa_bounds large_bounds = {{-32, -32, -24}, {32, 32, 64}};

const char *qa_q1am_infected_kind(qa_q1am_actor *entity) {
  const char *kind = qa_q1am_actor_text(entity, "infected.kind");
  if (strcmp(kind, "army") == 0 || strcmp(kind, "knight") == 0 ||
      strcmp(kind, "enforcer") == 0 || strcmp(kind, "hellknight") == 0)
    return kind;
  qa_range_error("Missing MG3 infected variant on %s", entity->classname);
}
qa_q1am_species qa_q1am_infected_species(qa_q1am_actor *entity) {
  const char *kind = qa_q1am_infected_kind(entity);
  bool transformed = qa_q1am_actor_number(entity, "infected.transformed") != 0;
  const char *species = transformed
                            ? (strcmp(kind, "army") == 0 || strcmp(kind, "knight") == 0 ? "zombie" : "demon")
                            : kind;
  if (strcmp(species, "army") == 0) return qa_q1am_army_species;
  const qa_q1am_species *source = NULL;
  for (size_t i = 0; i < qa_q1am_base_species_count; i++)
    if (strcmp(qa_q1am_base_species[i].species, species) == 0) source = &qa_q1am_base_species[i];
  if (source == NULL) qa_range_error("Missing base species %s", species);
  qa_q1am_species result = *source;
  if (strcmp(species, "zombie") == 0) {
    result.missile = NULL;
    result.melee = true;
    return result;
  }
  if (strcmp(species, "enforcer") == 0) {
    result.bounds = large_bounds;
    return result;
  }
  if (strcmp(species, "hellknight") != 0 || qa_q1am_actor_number(entity, "infected.risen") != 0) return result;
  int corpse = (FLAGS(entity->spawnflags) & 65536) != 0 ? 1 : (FLAGS(entity->spawnflags) & 8388608) != 0 ? 2 : 0;
  if (corpse == 0) return result;
  /* Stable spellings for the two corpse variants. */
  result.stand = corpse == 1 ? "hknight_corpse1" : "hknight_corpse2";
  result.run = corpse == 1 ? "hknight_corpse1_rise0" : "hknight_corpse2_rise0";
  return result;
}
