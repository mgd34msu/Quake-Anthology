/* Port of src/settings/server/lmctf.ts. GPL-2.0-or-later. */
#include "qa/settings/server/lmctf.h"

#include <stdbool.h>

#include "qa/settings/server/common.h"

/* LMCTF_RUNES (kind, bit, name) in source order; createLmctfRules
 * defaults runes to 15, so every bit except vampire (16) is on. */
static const struct {
  const char *id;
  const char *name;
  const char *description;
  int mask;
} runes[] = {
    {"server:lmctf.rune.damage", "Damage Artifact",
     "Spawn the Damage Artifact on the next map.", 1},
    {"server:lmctf.rune.haste", "Haste Artifact",
     "Spawn the Haste Artifact on the next map.", 4},
    {"server:lmctf.rune.resist", "Resist Artifact",
     "Spawn the Resist Artifact on the next map.", 2},
    {"server:lmctf.rune.regen", "Regen Artifact",
     "Spawn the Regen Artifact on the next map.", 8},
    {"server:lmctf.rune.vampire", "Vampire Artifact",
     "Spawn the Vampire Artifact on the next map.", 16},
};
#define RUNE_COUNT (sizeof runes / sizeof *runes)

static qa_server_setting_definition limit_definitions[2];
static qa_server_setting_definition rune_definitions[RUNE_COUNT];
static qa_server_setting_definition weapon_definitions[1];
static bool built = false;

static void build(void) {
  if (built) return;
  built = true;
  qa_server_limit(&limit_definitions[0], 
      "server:time-limit", "timelimit", "Match time (minutes)", 0,
      "Duration used when the next LMCTF match starts. Does not reset an "
      "active countdown.",
      QA_SERVER_APPLY_NEXT_MATCH, true);
  qa_server_limit(&limit_definitions[1], 
      "server:frag-limit", "fraglimit", "Frag limit", 0,
      "Start the final countdown when a player reaches this score. Zero "
      "disables the limit.",
      QA_SERVER_APPLY_LIVE, true);
  for (size_t i = 0; i < RUNE_COUNT; i++) {
    qa_server_setting_target target = {
        .is_bit = true, .name = "runes", .mask = runes[i].mask, .inverted = false};
    rune_definitions[i] = qa_server_toggle(runes[i].id, target, runes[i].name,
                                           (15 & runes[i].mask) != 0,
                                           runes[i].description,
                                           QA_SERVER_APPLY_NEXT_MAP);
  }
  qa_server_setting_target fast_switch = {.is_bit = false, .name = "fastswitch"};
  weapon_definitions[0] = qa_server_toggle(
      "server:lmctf.fast-switch", fast_switch, "Fast weapon switching", false,
      "Use the selected LMCTF weapon owner's fast switch behavior.",
      QA_SERVER_APPLY_LIVE);
}

qa_server_setting_collection qa_lmctf_limit_settings(void) {
  build();
  qa_server_setting_collection result = {"q2:lmctf-limits", limit_definitions, 2};
  return result;
}
qa_server_setting_collection qa_lmctf_rune_settings(void) {
  build();
  qa_server_setting_collection result = {"q2:lmctf-runes", rune_definitions,
                                         RUNE_COUNT};
  return result;
}
qa_server_setting_collection qa_lmctf_weapon_settings(void) {
  build();
  qa_server_setting_collection result = {"q2:lmctf-weapons", weapon_definitions, 1};
  return result;
}
