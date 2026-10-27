/* Port of src/content/monsters/roster.ts. Slot tables are the TS
 * role records flattened in role order then sorted by classname
 * (TS localeCompare). GPL-2.0-or-later. */
#include "qa/content/monsters/roster.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/monsters/definitions.h"
#include "qa/foundation/support.h"

static const qa_monster_roster_slot q1_slots[] = {
  {"monster_armagon", "boss"},
  {"monster_army", "trooper"},
  {"monster_army_infected", "trooper"},
  {"monster_boss", "boss"},
  {"monster_boss_final", "boss"},
  {"monster_decoy", "special"},
  {"monster_demodog", "leaper"},
  {"monster_demon1", "leaper"},
  {"monster_dog", "melee"},
  {"monster_dragon", "boss"},
  {"monster_dragon_dead", "special"},
  {"monster_eel", "aquatic"},
  {"monster_enforcer", "ranged"},
  {"monster_enforcer_infected", "ranged"},
  {"monster_fish", "aquatic"},
  {"monster_ghost", "special"},
  {"monster_gremlin", "special"},
  {"monster_hell_knight", "hybrid"},
  {"monster_hell_knight_infected", "hybrid"},
  {"monster_knight", "melee"},
  {"monster_knight_infected", "melee"},
  {"monster_lava_man", "special"},
  {"monster_morph", "special"},
  {"monster_mummy", "special"},
  {"monster_ogre", "grenadier"},
  {"monster_ogre_marksman", "grenadier"},
  {"monster_ogre_rocket", "rocket"},
  {"monster_oldone", "boss"},
  {"monster_oldone_new", "boss"},
  {"monster_orb", "special"},
  {"monster_ranged_knight", "hybrid"},
  {"monster_scourge", "artillery"},
  {"monster_shalrath", "artillery"},
  {"monster_shambler", "heavy"},
  {"monster_spikemine", "special"},
  {"monster_super_shambler", "boss"},
  {"monster_super_wrath", "boss"},
  {"monster_sword", "special"},
  {"monster_szombie", "special"},
  {"monster_tarbaby", "special"},
  {"monster_vomit", "special"},
  {"monster_wizard", "flying"},
  {"monster_wrath", "flying"},
  {"monster_zombie", "special"},
};
static const qa_monster_roster_slot q2_slots[] = {
  {"monster_arachnid", "artillery"},
  {"monster_berserk", "melee"},
  {"monster_boss2", "boss"},
  {"monster_boss3_stand", "boss"},
  {"monster_boss5", "boss"},
  {"monster_brain", "hybrid"},
  {"monster_carrier", "boss"},
  {"monster_chick", "rocket"},
  {"monster_chick_heat", "rocket"},
  {"monster_commander_body", "special"},
  {"monster_daedalus", "flying"},
  {"monster_fixbot", "special"},
  {"monster_flipper", "aquatic"},
  {"monster_floater", "flying"},
  {"monster_flyer", "flying"},
  {"monster_gekk", "special"},
  {"monster_gladb", "artillery"},
  {"monster_gladiator", "artillery"},
  {"monster_guardian", "boss"},
  {"monster_guncmdr", "grenadier"},
  {"monster_gunner", "grenadier"},
  {"monster_hover", "flying"},
  {"monster_infantry", "ranged"},
  {"monster_jorg", "boss"},
  {"monster_kamikaze", "special"},
  {"monster_makron", "boss"},
  {"monster_medic", "special"},
  {"monster_medic_commander", "special"},
  {"monster_mutant", "leaper"},
  {"monster_parasite", "special"},
  {"monster_shambler", "heavy"},
  {"monster_soldier", "trooper"},
  {"monster_soldier_hypergun", "ranged"},
  {"monster_soldier_lasergun", "ranged"},
  {"monster_soldier_light", "trooper"},
  {"monster_soldier_ripper", "ranged"},
  {"monster_soldier_ss", "ranged"},
  {"monster_stalker", "special"},
  {"monster_supertank", "boss"},
  {"monster_tank", "heavy"},
  {"monster_tank_commander", "heavy"},
  {"monster_tank_stand", "special"},
  {"monster_turret", "special"},
  {"monster_widow", "boss"},
  {"monster_widow2", "boss"},
};

/* TS preferred[selected.family][role]; NULL for boss/special. */
static const char *preferred(const char *family, const char *role) {
  static const struct { const char *role; const char *q1; const char *q2; } table[] = {
      {"trooper", "monster_army", "monster_soldier"},
      {"ranged", "monster_enforcer", "monster_infantry"},
      {"melee", "monster_demon1", "monster_berserk"},
      {"leaper", "monster_demon1", "monster_mutant"},
      {"grenadier", "monster_ogre", "monster_gunner"},
      {"rocket", "monster_shalrath", "monster_chick"},
      {"artillery", "monster_shalrath", "monster_gladiator"},
      {"hybrid", "monster_hell_knight", "monster_brain"},
      {"heavy", "monster_shambler", "monster_tank"},
      {"flying", "monster_wizard", "monster_flyer"},
      {"aquatic", "monster_fish", "monster_flipper"},
      {"boss", NULL, NULL},
      {"special", NULL, NULL},
  };
  for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
    if (strcmp(table[i].role, role) == 0)
      return strcmp(family, "q1") == 0 ? table[i].q1 : table[i].q2;
  return NULL;
}

const qa_monster_roster_slot *qa_campaign_monster_slots(qa_game_family family,
                                                        size_t *out_count) {
  switch (family) {
    case QA_GAME_FAMILY_Q1:
      *out_count = sizeof q1_slots / sizeof q1_slots[0];
      return q1_slots;
    case QA_GAME_FAMILY_Q2:
      *out_count = sizeof q2_slots / sizeof q2_slots[0];
      return q2_slots;
    case QA_GAME_FAMILY_Q3:
      *out_count = 0;
      return NULL;
  }
  *out_count = 0;
  return NULL;
}

bool qa_default_monster_roster(const char *authored_family,
                               qa_provider_reference target,
                               const qa_monster_by_classname *overrides,
                               size_t override_count,
                               qa_enemy_selection *out) {
  const qa_monster_source_definition *selected = NULL;
  for (size_t i = 0; i < qa_monster_source_count; i++)
    if (strcmp(qa_monster_sources[i].provider, target.provider) == 0) {
      selected = &qa_monster_sources[i];
      break;
    }
  if (selected == NULL)
    qa_range_error("Unknown monster source: %s", target.provider);
  qa_game_family family = strcmp(authored_family, "q1") == 0
                              ? QA_GAME_FAMILY_Q1
                              : QA_GAME_FAMILY_Q2;
  size_t slot_count = 0;
  const qa_monster_roster_slot *slots =
      qa_campaign_monster_slots(family, &slot_count);
  size_t capacity = slot_count + override_count;
  qa_monster_by_classname *by_classname = capacity > 0
      ? (qa_monster_by_classname *)malloc(capacity * sizeof *by_classname)
      : NULL;
  if (capacity > 0 && by_classname == NULL) return false;
  size_t count = 0;
  for (size_t i = 0; i < slot_count; i++) {
    const char *classname;
    if (strcmp(authored_family, "q2") == 0 &&
        strcmp(selected->family, "q1") == 0 &&
        strcmp(slots[i].classname, "monster_parasite") == 0)
      classname = "monster_dog";
    else if (strcmp(authored_family, "q1") == 0 &&
             strcmp(selected->family, "q2") == 0 &&
             strcmp(slots[i].classname, "monster_dog") == 0)
      classname = "monster_parasite";
    else if (strcmp(slots[i].role, "boss") == 0 ||
             strcmp(slots[i].role, "special") == 0)
      classname = NULL;
    else if (strcmp(selected->family, authored_family) == 0 &&
             qa_monster_source_creature(selected, slots[i].classname) != NULL)
      classname = slots[i].classname;
    else
      classname = preferred(selected->family, slots[i].role);
    by_classname[count].classname = slots[i].classname;
    if (classname != NULL &&
        qa_monster_source_creature(selected, classname) != NULL) {
      by_classname[count].target.is_map_defined = false;
      by_classname[count].target.reference.source = target;
      by_classname[count].target.reference.classname = classname;
    } else {
      by_classname[count].target.is_map_defined = true;
      by_classname[count].target.reference.source.provider = NULL;
      by_classname[count].target.reference.source.content = NULL;
      by_classname[count].target.reference.classname = NULL;
    }
    count++;
  }
  /* TS { ...byClassname, ...overrides }: later keys replace. */
  for (size_t i = 0; i < override_count; i++) {
    size_t at = count;
    for (size_t j = 0; j < count; j++)
      if (strcmp(by_classname[j].classname, overrides[i].classname) == 0) {
        at = j;
        break;
      }
    by_classname[at] = overrides[i];
    if (at == count) count++;
  }
  memset(out, 0, sizeof *out);
  out->kind = QA_ENEMY_SELECTION_REPLACE;
  out->default_target.is_map_defined = true;
  out->by_classname = by_classname;
  out->by_classname_count = count;
  return true;
}

void qa_default_monster_roster_free(qa_enemy_selection *selection) {
  free((void *)selection->by_classname);
  selection->by_classname = NULL;
  selection->by_classname_count = 0;
}
