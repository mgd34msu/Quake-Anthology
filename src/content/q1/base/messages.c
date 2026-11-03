/* Port of src/content/q1/base/messages.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/messages.h"

#include <stdio.h>
#include <limits.h>
#include <string.h>

typedef struct entry { const char *key; const char *text; } entry;
static const entry OBITUARIES[] = {
  {"$qc_telefragged", "{0} was telefragged by {1}\n"},
  {"$qc_satans_power", "Satan's power deflects {0}'s telefrag\n"},
  {"$qc_discharge_water", "{0} discharges into the water.\n"},
  {"$qc_discharge_slime", "{0} discharges into the water.\n"},
  {"$qc_discharge_lava", "{0} discharges into the water.\n"},
  {"$qc_suicide_pin", "{0} tries to put the pin back in\n"},
  {"$qc_suicide_bored", "{0} becomes bored with life\n"},
  {"$qc_suicide_loaded", "{0} becomes bored with life\n"},
  {"$qc_ff_teammate", "{0} mows down a teammate\n"},
  {"$qc_ff_glasses", "{0} checks his glasses\n"},
  {"$qc_ff_otherteam", "{0} gets a frag for the other team\n"},
  {"$qc_ff_friend", "{0} loses another friend\n"},
  {"$qc_death_ax", "{0} was ax-murdered by {1}\n"},
  {"$qc_death_sg", "{0} chewed on {1}'s boomstick\n"},
  {"$qc_death_dbl", "{0} ate 2 loads of {1}'s buckshot\n"},
  {"$qc_death_nail", "{0} was nailed by {1}\n"},
  {"$qc_death_sng", "{0} was punctured by {1}\n"},
  {"$qc_death_gl1", "{0} was gibbed by {1}'s grenade\n"},
  {"$qc_death_gl2", "{0} eats {1}'s pineapple\n"},
  {"$qc_death_rl2", "{0} was gibbed by {1}'s rocket\n"},
  {"$qc_death_rl3", "{0} rides {1}'s rocket\n"},
  {"$qc_death_lg1", "{0} accepts {1}'s discharge\n"},
  {"$qc_death_lg2", "{0} accepts {1}'s shaft\n"},
  {"$qc_death_drown1", "{0} sleeps with the fishes\n"},
  {"$qc_death_drown2", "{0} sucks it down\n"},
  {"$qc_death_slime1", "{0} gulped a load of slime\n"},
  {"$qc_death_slime2", "{0} can't exist on slime alone\n"},
  {"$qc_death_lava1", "{0} burst into flames\n"},
  {"$qc_death_lava2", "{0} turned into hot slag\n"},
  {"$qc_death_lava3", "{0} visits the Volcano God\n"},
  {"$qc_death_squish", "{0} was squished\n"},
  {"$qc_death_hook1", "{0} was hooked by {1}\n"},
  {"$qc_death_hook2", "{0} was hooked by {1}\n"},
  {"$qc_ks_changed_teams", "{0} changed teams\n"},
  {"$qc_ks_tried_change_teams", "{0} tried to change teams\n"},
  {"$qc_death_fall", "{0} fell to his death\n"},
  {"$qc_death_died", "{0} died\n"},
};
static const entry MONSTER_OBITUARIES[] = {
  {"monster_army", " was shot by a Grunt\n"},
  {"monster_demon1", " was eviscerated by a Fiend\n"},
  {"monster_dog", " was mauled by a Rottweiler\n"},
  {"monster_dragon", " was fried by a Dragon\n"},
  {"monster_enforcer", " was blasted by an Enforcer\n"},
  {"monster_fish", " was fed to the Rotfish\n"},
  {"monster_hell_knight", " was slain by a Death Knight\n"},
  {"monster_knight", " was slashed by a Knight\n"},
  {"monster_ogre", " was destroyed by an Ogre\n"},
  {"monster_oldone", " became one with Shub-Niggurath\n"},
  {"monster_shalrath", " was exploded by a Vore\n"},
  {"monster_shambler", " was smashed by a Shambler\n"},
  {"monster_tarbaby", " was slimed by a Spawn\n"},
  {"monster_vomit", " was vomited on by a Vomitus\n"},
  {"monster_wizard", " was scragged by a Scrag\n"},
  {"monster_zombie", " joins the Zombies\n"},
};

static const char *lookup(const entry *table, size_t count, const char *key) {
  for (size_t i = 0; i < count; i++) if (strcmp(table[i].key, key) == 0) return table[i].text;
  return NULL;
}
const char *qa_q1_classic_monster_obituary(const char *classname) {
  return lookup(MONSTER_OBITUARIES, sizeof MONSTER_OBITUARIES / sizeof *MONSTER_OBITUARIES, classname);
}
char *qa_q1_classic_obituary_text(const char *key, const char *const *args, size_t arg_count,
                                  char *out, size_t out_size) {
  const char *pattern = lookup(OBITUARIES, sizeof OBITUARIES / sizeof *OBITUARIES, key);
  if (pattern == NULL) pattern = key;
  size_t used = 0;
  if (out_size > 0) out[0] = '\0';
  for (const char *p = pattern; *p != '\0'; p++) {
    if (p[0] == '{') {
      const char *q = p + 1; long index = 0; int digits = 0;
      while (*q >= '0' && *q <= '9') {
        /* Number(index) never wraps (and signed overflow is undefined): saturate out of range. */
        index = index <= (LONG_MAX - 9) / 10 ? index * 10 + (*q - '0') : LONG_MAX;
        q++; digits++;
      }
      if (digits > 0 && *q == '}') {
        const char *arg = index >= 0 && (size_t)index < arg_count && args[index] != NULL ? args[index] : "";
        if (used < out_size) used += (size_t)snprintf(out + used, out_size - used, "%s", arg);
        p = q; continue;
      }
    }
    if (used < out_size) used += (size_t)snprintf(out + used, out_size - used, "%c", *p);
  }
  return out;
}
