/* Port of src/content/q1/missionpacks/presentation.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_PRESENTATION_H
#define QA_CONTENT_Q1_MISSIONPACKS_PRESENTATION_H
#include "qa/content/q1/missionpacks/types.h"

/* Mirrors (TS owner: src/content/q1/base/player.ts, unlanded):
 * Q1CharacterDefinition/Q1CharacterSourcePose/Q1CharacterPresentation. */
typedef struct qa_q1mp_character_range { double first; double count; } qa_q1mp_character_range;
typedef struct qa_q1mp_character_definition { const char *model; qa_q1mp_character_range stand, run, pain, death; } qa_q1mp_character_definition;
typedef struct qa_q1mp_character_source_pose { bool has_definition; qa_q1mp_character_definition definition; bool has_frame; double frame; } qa_q1mp_character_source_pose;
typedef struct qa_q1mp_character_presentation { const char *model; double frame; const char *life; } qa_q1mp_character_presentation;
qa_q1mp_character_source_pose qa_q1mp_mission_pack_character_pose(qa_q1mp_game *game, qa_actor_id actor, qa_q1mp_pack pack);
/* TS MissionPackCharacterEffects. footsteps NULL = TS () => false via caller. */
typedef struct qa_q1mp_mission_pack_character_effects {
  qa_q1mp_game *game; qa_q1mp_pack pack; void *footsteps_ctx; bool (*footsteps)(void *ctx);
} qa_q1mp_mission_pack_character_effects;
void qa_q1mp_character_effects_init(qa_q1mp_mission_pack_character_effects *effects, qa_q1mp_game *game, qa_q1mp_pack pack, bool (*footsteps)(void *ctx), void *footsteps_ctx);
void qa_q1mp_character_effects_frame(qa_q1mp_mission_pack_character_effects *effects, qa_actor_id actor, const qa_q1mp_character_presentation *presentation);

#endif
