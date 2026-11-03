/* Port of src/content/q1/missionpacks/runtime.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_RUNTIME_H
#define QA_CONTENT_Q1_MISSIONPACKS_RUNTIME_H
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/missionpacks/arsenal.h"
#include "qa/content/q1/missionpacks/commands.h"
#include "qa/content/q1/missionpacks/obituaries.h"
#include "qa/content/q1/missionpacks/presentation.h"
#include "qa/content/q1/missionpacks/travel.h"
#include "qa/content/q1/missionpacks/types.h"
#include "qa/content/q1/missionpacks/world/index.h"

struct qa_q1mpm_runtime; /* TS Q1MissionPackMonsters (missionpacks/monsters/runtime.h) */

/* TS Q1MissionPackOptions: the world hooks without charmer/charm/becomeDecoy
 * (the runtime supplies those), the command options and footsteps. A host
 * interface, so fn + ctx; NULL fn = TS absent. */
typedef struct qa_q1mp_mission_pack_options {
  qa_q1mp_world_hooks world;
  qa_q1mp_mission_pack_command_options command;
  void *footsteps_ctx; bool (*footsteps)(void *ctx);
} qa_q1mp_mission_pack_options;
/* TS Q1MissionPackRuntime. Must not move after init (the game borrows its
 * members); destroy releases the C-owned parts. */
typedef struct qa_q1mp_mission_pack_runtime {
  qa_q1_services *game; qa_q1_base *base; qa_q1mp_pack pack; qa_q1mp_mission_pack_options options;
  qa_q1mp_mission_pack_arsenal arsenal;
  struct qa_q1mpm_runtime *monsters; /* owned */
  qa_q1mp_missionpack_world world;
  qa_q1mp_mission_pack_character_effects character_effects;
  qa_q1_player_extension postthink, coordinate_dump;
} qa_q1mp_mission_pack_runtime;
void qa_q1mp_mission_pack_runtime_init(qa_q1mp_mission_pack_runtime *runtime, qa_q1_services *game, qa_q1_base *base,
                                       qa_q1mp_pack pack, const qa_q1mp_mission_pack_options *options);
void qa_q1mp_mission_pack_runtime_destroy(qa_q1mp_mission_pack_runtime *runtime);
/* TS registerQ1MissionPack: initializes the caller's storage. options NULL = TS {}. */
qa_q1mp_mission_pack_runtime *qa_q1mp_register_q1_mission_pack(qa_q1_services *game, qa_q1_base *base, qa_q1mp_pack pack,
                                                               const qa_q1mp_mission_pack_options *options,
                                                               qa_q1mp_mission_pack_runtime *out);
bool qa_q1mp_runtime_impulse(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, double impulse);
void qa_q1mp_runtime_player_spawned(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor);
/* attacker NULL = TS null. */
void qa_q1mp_runtime_confirmed_damage(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id target, const qa_actor_id *attacker);
void qa_q1mp_runtime_player_died(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, const qa_actor_id *attacker);
qa_q1_travel_state qa_q1mp_runtime_new_travel(qa_q1mp_mission_pack_runtime *runtime);
qa_q1_travel_state qa_q1mp_runtime_capture_travel(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor);
qa_q1_travel_state qa_q1mp_runtime_decode_travel(qa_q1mp_mission_pack_runtime *runtime, const qa_q1_travel_state *state, double server_flags);
void qa_q1mp_runtime_admit_travel(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor, const qa_q1_travel_state *state);
qa_q1_actor *qa_q1mp_runtime_drop_backpack(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor);
qa_q1_character_source_pose qa_q1mp_runtime_character_pose(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor);
void qa_q1mp_runtime_character_frame(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, const qa_q1_character_presentation *presentation);
/* TS suicide: false refuses the suicide. */
bool qa_q1mp_runtime_suicide(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor);
/* NULL = TS undefined. */
qa_q1_actor *qa_q1mp_runtime_select_spawn(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor);
/* inflictor NULL = TS null. */
qa_q1_obituary qa_q1mp_runtime_obituary(qa_q1mp_mission_pack_runtime *runtime, const qa_q1_obituary_input *input,
                                        const qa_actor_id *inflictor);

#endif
