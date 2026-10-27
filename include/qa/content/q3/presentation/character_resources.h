/* Port of src/content/q3/presentation/character-resources.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_PRESENTATION_CHARACTER_RESOURCES_H
#define QA_CONTENT_Q3_PRESENTATION_CHARACTER_RESOURCES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/state.h" /* qa_q3_game_product (definitions.ts mirror) */
/** CG_RegisterSounds character media, shared by cgame and mixed character playback. */
typedef struct qa_q3_pres_character_sounds {
  const char *select_sound;
  const char *gib_sound;
  const char *tele_in_sound;
  const char *tele_out_sound;
  const char *respawn_sound;
  const char *land_sound;
  const char *watr_in_sound;
  const char *watr_out_sound;
  const char *watr_un_sound;
  const char *jump_pad_sound;
} qa_q3_pres_character_sounds;
extern const qa_q3_pres_character_sounds qa_q3_character_sounds;
/* TS Q3_FOOTSTEP_PATHS: [keyof ClientMedia["footsteps"], path] pairs. */
typedef struct qa_q3_pres_footstep_path { const char *key; const char *path; } qa_q3_pres_footstep_path;
extern const qa_q3_pres_footstep_path qa_q3_footstep_paths[];
extern const size_t qa_q3_footstep_path_count;
/** CG_LoadClientInfo: missionpack changes the team default, never the FFA default.
 * Returns "sarge" | "james". */
const char *qa_q3_custom_sound_fallback(qa_q3_game_product product, bool team_game);
#endif
