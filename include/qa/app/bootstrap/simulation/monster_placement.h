/* Port of src/app/bootstrap/simulation/monster-placement.ts: authored
 * placement preservation for selected monsters and the shared nearby-fit
 * search. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MONSTER_PLACEMENT_H
#define QA_APP_BOOTSTRAP_SIMULATION_MONSTER_PLACEMENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/world.h"
#include "qa/formats/q1_map/types.h"

typedef struct qa_app_sim_placement_offset { double x; double y; } qa_app_sim_placement_offset;
/* TS offsetsWithin: sorted by dist^2, then y, then x. Malloc'd; caller frees. */
qa_app_sim_placement_offset *qa_app_sim_placement_offsets_within(double radius, size_t *out_count);
/* TS sameVector. */
bool qa_app_sim_monster_same_vector(qa_vec3 left, qa_vec3 right);

/* TS isQ1TeleportStaging. */
bool qa_app_sim_is_q1_teleport_staging(qa_q1_services *game, qa_body_state body);

/* TS preservesAuthoredQ1Placement input. authored NULL = TS undefined. */
typedef struct qa_app_sim_q1_placement_input {
  qa_resolved_map map;
  const qa_q1_entity *authored;
  qa_monster_definition_reference definition;
  qa_q1_services *game;
  const qa_q1_actor *entity;
  qa_body_state body;
} qa_app_sim_q1_placement_input;
bool qa_app_sim_preserves_authored_q1_placement(qa_app_sim_q1_placement_input input);

/* TS preservesAuthoredQ2Placement input. authored NULL = TS undefined,
 * native NULL = TS null. */
typedef struct qa_app_sim_q2_placement_input {
  qa_resolved_map map;
  const qa_q2_spawn_fields *authored;
  qa_monster_definition_reference definition;
  const qa_q2m_definition *native;
  qa_q2_game_services *game;
  const qa_q2_entity *entity;
  qa_body_state body;
} qa_app_sim_q2_placement_input;
bool qa_app_sim_preserves_authored_q2_placement(qa_app_sim_q2_placement_input input);

/* TS nearbyMonsterPlacement input. locomotion strings are "walk" | "fly" |
 * "swim". query is Omit<TraceQuery, "start" | "end" | "shape">: those three
 * members are ignored. world_actor NULL = TS null; blocked_by NULL = TS
 * undefined. The callbacks share ctx. */
typedef struct qa_app_sim_nearby_placement_input {
  qa_body_state body;
  const char *locomotion;
  const qa_actor_id *world_actor;
  struct { qa_vec3 origin; qa_bounds bounds; const char *locomotion; } authored;
  qa_trace_query query;
  void *ctx;
  bool (*same_medium)(void *ctx, qa_vec3 origin);
  qa_trace_result (*trace)(void *ctx, qa_trace_query query);
  void (*blocked_by)(void *ctx, qa_actor_id actor);
} qa_app_sim_nearby_placement_input;
/* TS Pick<BodyState, "origin" | "ground"> | null: found false = null;
 * has_ground false = ground null. */
typedef struct qa_app_sim_nearby_placement {
  bool found;
  qa_vec3 origin;
  bool has_ground;
  qa_actor_id ground;
} qa_app_sim_nearby_placement;
qa_app_sim_nearby_placement qa_app_sim_nearby_monster_placement(const qa_app_sim_nearby_placement_input *input);
#endif
