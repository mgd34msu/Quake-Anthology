/* Port of src/content/q1/missionpacks/runtime.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_RUNTIME_H
#define QA_CONTENT_Q1_MISSIONPACKS_RUNTIME_H
#include "qa/content/q1/missionpacks/types.h"

#include "qa/content/q1/missionpacks/arsenal.h"
#include "qa/content/q1/missionpacks/commands.h"
#include "qa/content/q1/missionpacks/obituaries.h"
#include "qa/content/q1/missionpacks/presentation.h"
#include "qa/content/q1/missionpacks/travel.h"
#include "qa/content/q1/missionpacks/world/index.h"
/* TS Q1MissionPackOptions = world hooks (minus charmer/charm/becomeDecoy,
 * supplied by runtime) + command options + footsteps. */
typedef struct qa_q1mp_mission_pack_options {
  qa_q1mp_world_hooks world; qa_q1mp_mission_pack_command_options command;
  void *footsteps_ctx; bool (*footsteps)(void *ctx);
  /* Monsters registration (TS owner: missionpacks/monsters/index.ts,
   * landed on the qa_q1mpm_* game mirror): opaque handle + charm. */
  void *monsters;
  void *monsters_ctx;
  void *(*register_monsters)(void *ctx, qa_q1mp_game *game, qa_q1mp_q1base *base, qa_q1mp_pack pack, qa_q1mp_mission_pack_arsenal *arsenal);
  void (*monsters_charm)(void *ctx, void *monsters, qa_q1mp_actor *entity, qa_actor_id charmer);
  qa_q1mp_actor *(*become_decoy)(void *ctx, void *monsters, const char *target, qa_vec3 origin);
} qa_q1mp_mission_pack_options;
/* TS Q1MissionPackRuntime. */
typedef struct qa_q1mp_mission_pack_runtime {
  qa_q1mp_game *game; qa_q1mp_q1base *base; qa_q1mp_pack pack; qa_q1mp_mission_pack_options options;
  qa_q1mp_mission_pack_arsenal arsenal; void *monsters;
  qa_q1mp_missionpack_world world; qa_q1mp_mission_pack_character_effects character_effects;
} qa_q1mp_mission_pack_runtime;
void qa_q1mp_mission_pack_runtime_init(qa_q1mp_mission_pack_runtime *runtime, qa_q1mp_game *game, qa_q1mp_q1base *base, qa_q1mp_pack pack, const qa_q1mp_mission_pack_options *options);
qa_q1mp_mission_pack_runtime *qa_q1mp_register_q1_mission_pack(qa_q1mp_game *game, qa_q1mp_q1base *base, qa_q1mp_pack pack, const qa_q1mp_mission_pack_options *options, qa_q1mp_mission_pack_runtime *out);
bool qa_q1mp_runtime_impulse(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, double impulse);
void qa_q1mp_runtime_player_spawned(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor);
void qa_q1mp_runtime_confirmed_damage(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id target, qa_q1mp_opt_actor attacker);
void qa_q1mp_runtime_player_died(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, qa_q1mp_opt_actor attacker);
qa_q1mp_travel_state qa_q1mp_runtime_new_travel(qa_q1mp_mission_pack_runtime *runtime);
qa_q1mp_travel_state qa_q1mp_runtime_capture_travel(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor);
qa_q1mp_travel_state qa_q1mp_runtime_decode_travel(qa_q1mp_mission_pack_runtime *runtime, qa_q1mp_travel_state state, double server_flags);
void qa_q1mp_runtime_admit_travel(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor, qa_q1mp_travel_state state);
qa_q1mp_actor *qa_q1mp_runtime_drop_backpack(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor);
qa_q1mp_character_source_pose qa_q1mp_runtime_character_pose(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor);
void qa_q1mp_runtime_character_frame(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, const qa_q1mp_character_presentation *presentation);
qa_q1mp_actor *qa_q1mp_runtime_select_spawn(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor);

qa_q1mp_obituary qa_q1mp_runtime_obituary(qa_q1mp_mission_pack_runtime *runtime, const qa_q1mp_obituary_input *input, qa_q1mp_opt_actor inflictor);
#endif
