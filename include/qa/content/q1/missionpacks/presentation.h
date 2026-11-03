/* Port of src/content/q1/missionpacks/presentation.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_PRESENTATION_H
#define QA_CONTENT_Q1_MISSIONPACKS_PRESENTATION_H
#include "qa/content/q1/base/player.h"
#include "qa/content/q1/missionpacks/types.h"

qa_q1_character_source_pose qa_q1mp_mission_pack_character_pose(qa_q1_services *game, qa_actor_id actor, qa_q1mp_pack pack);
/* TS MissionPackCharacterEffects; footsteps is the TS () => boolean. */
typedef struct qa_q1mp_mission_pack_character_effects {
  qa_q1_services *game; qa_q1mp_pack pack;
  void *footsteps_ctx; bool (*footsteps)(void *ctx);
} qa_q1mp_mission_pack_character_effects;
void qa_q1mp_character_effects_init(qa_q1mp_mission_pack_character_effects *effects, qa_q1_services *game, qa_q1mp_pack pack,
                                    bool (*footsteps)(void *ctx), void *footsteps_ctx);
void qa_q1mp_character_effects_frame(qa_q1mp_mission_pack_character_effects *effects, qa_actor_id actor,
                                     const qa_q1_character_presentation *presentation);

#endif
