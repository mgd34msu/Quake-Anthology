/* Port of src/content/q3/presentation/character-resources.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/character_resources.h"
const qa_q3_pres_character_sounds qa_q3_character_sounds = {
    "sound/weapons/change.wav", "sound/player/gibsplt1.wav",
    "sound/world/telein.wav",   "sound/world/teleout.wav",  "sound/items/respawn1.wav",
    "sound/player/land1.wav",   "sound/player/watr_in.wav", "sound/player/watr_out.wav",
    "sound/player/watr_un.wav", "sound/world/jumppad.wav",
};
const qa_q3_pres_footstep_path qa_q3_footstep_paths[] = {
    {"normal", "step"}, {"boot", "boot"}, {"flesh", "flesh"}, {"mech", "mech"},
    {"energy", "energy"}, {"splash", "splash"}, {"metal", "clank"},
};
const size_t qa_q3_footstep_path_count = sizeof qa_q3_footstep_paths / sizeof qa_q3_footstep_paths[0];
const char *qa_q3_custom_sound_fallback(qa_q3_game_product product, bool team_game) {
  return product == QA_Q3_PRODUCT_MISSIONPACK && team_game ? "james" : "sarge";
}
