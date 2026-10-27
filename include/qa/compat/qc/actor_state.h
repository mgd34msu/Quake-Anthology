/* Port of src/compat/qc/actor-state.ts. GPL-2.0-or-later.
 * Mirrored types (TS owners unported): SharedSolid/SharedPhysicsFlags
 * from src/app/bootstrap/simulation/physics.ts, unique names below.
 * createQcBodyBinding storage is caller-owned (ctx must outlive binding). */
#ifndef QA_COMPAT_QC_ACTOR_STATE_H
#define QA_COMPAT_QC_ACTOR_STATE_H
#include <stdbool.h>
#include "qa/compat/qc/machine.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/contracts/world.h"
#include "qa/world/actors/body.h"

/* Mirror of TS SharedSolid (src/app/bootstrap/simulation/physics.ts). */
typedef struct qa_compat_qc_shared_solid {
  const char *solid; /* "none"|"trigger"|"box"|"brush" */
  bool has_model; double model;
  const char *family; /* "q1" */
  bool has_owner; qa_actor_id owner;
  bool has_monster, monster, has_dead_monster, dead_monster, q1_corpse, has_item, item;
} qa_compat_qc_shared_solid;
/* Mirror of TS SharedPhysicsFlags (same owner). has_* = TS optional present. */
typedef struct qa_compat_qc_shared_physics_flags {
  bool has_always_touch, always_touch, has_fly, fly, has_swim, swim;
  bool has_partial_ground, partial_ground, has_dead, dead, has_player, player;
  bool has_water_level; double water_level; bool has_water_type; double water_type;
  bool has_enemy, enemy_is_null; qa_actor_id enemy;
  bool has_delta_yaw; double delta_yaw; bool has_team_slave, team_slave;
} qa_compat_qc_shared_physics_flags;

typedef struct qa_compat_qc_actor_state_options {
  qa_compat_qc_machine *machine; bool rerelease; void *ctx;
  bool (*source_slot)(void *ctx, qa_actor_id actor, int64_t *out); /* false = TS null */
  bool (*reference)(void *ctx, int64_t reference, qa_actor_id *out);
  bool (*is_client)(void *ctx, qa_actor_id actor);
} qa_compat_qc_actor_state_options;
typedef struct qa_compat_qc_actor_state { qa_compat_qc_actor_state_options options; } qa_compat_qc_actor_state;
void qa_compat_qc_actor_state_init(qa_compat_qc_actor_state *s, qa_compat_qc_actor_state_options options);
bool qa_compat_qc_actor_state_collision(qa_compat_qc_actor_state *s, qa_owned_actor actor, qa_compat_qc_shared_solid *out);
bool qa_compat_qc_actor_state_motion(qa_compat_qc_actor_state *s, qa_owned_actor actor, qa_body_state body, qa_q2_motion *out);
qa_compat_qc_shared_physics_flags qa_compat_qc_actor_state_flags(qa_compat_qc_actor_state *s, qa_owned_actor actor);
void qa_compat_qc_actor_state_write_flags(qa_compat_qc_actor_state *s, qa_owned_actor actor, qa_compat_qc_shared_physics_flags changes);
void qa_compat_qc_actor_state_write_angular_velocity(qa_compat_qc_actor_state *s, qa_owned_actor actor, qa_vec3 value);

typedef struct qa_compat_qc_body_references {
  void *ctx;
  int64_t (*reference)(void *ctx, qa_actor_id actor);
  bool (*actor)(void *ctx, int64_t reference, qa_actor_id *out);
} qa_compat_qc_body_references;
typedef struct qa_compat_qc_body_binding_state {
  qa_compat_qc_words *fields; qa_compat_qc_program *program;
  int64_t origin, angles, velocity, mins, maxs, flags, ground;
  qa_compat_qc_body_references references; qa_actor_id ground_actor;
} qa_compat_qc_body_binding_state;
qa_body_state_binding qa_compat_qc_create_body_binding(qa_compat_qc_program *program, qa_compat_qc_entity_memory *entities, int64_t slot, qa_compat_qc_body_references references, qa_compat_qc_body_binding_state *storage);
#endif
