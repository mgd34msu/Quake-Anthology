/* Port of src/app/bootstrap/simulation/quakec-weapon-behavior.ts.
 * Executes donor QuakeC bytecode; only its projectile trajectory crosses
 * into the selected launcher's actor. TS `throw new Error(msg)` raises
 * qa_raise("Error", ...) (catchable with qa_try; aborts when uncaught);
 * QuakeC failures raise the machine's "QcRuntimeError".
 *
 * Ownership: the source borrows options.program and every string in
 * options.definition for its lifetime. Instances returned by attach/resume
 * stay valid until the source is destroyed (close only detaches them).
 * Checkpoints own their random words, machine snapshot and lists; free them
 * with qa_app_sim_quakec_weapon_behavior_checkpoint_free. A checkpoint
 * definition borrows the source (or the reader's expected) definition.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_WEAPON_BEHAVIOR_H
#define QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_WEAPON_BEHAVIOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/qc/machine.h"
#include "qa/compat/qc/program.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/session.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/world.h"
#include "qa/persistence/value.h"

/* TS QcWeaponBehaviorTarget. */
typedef struct qa_app_sim_qc_weapon_target {
  qa_actor_id actor;
  qa_body_state body;
  double health;
  const char *classname;
  const char *name;
  bool solid;
} qa_app_sim_qc_weapon_target;

/* TS model() result { index, bounds }. */
typedef struct qa_app_sim_qc_weapon_model {
  double index;
  qa_bounds bounds;
} qa_app_sim_qc_weapon_model;

/* TS QcWeaponBehaviorOptions. scene is TS Pick<SceneQueries, 'trace' |
 * 'pointContents'> and keeps its own ctx; random keeps its own ctx; the
 * remaining callbacks receive ctx. */
typedef struct qa_app_sim_qc_weapon_behavior_options {
  qa_weapon_behavior_definition definition;
  qa_compat_qc_program *program;
  qa_random_source random;
  qa_scene_queries scene;
  const char *mode; /* "singleplayer"|"coop"|"deathmatch" */
  void *ctx;
  const qa_app_sim_qc_weapon_target *(*targets)(void *ctx, size_t *out_count);
  qa_vec3 (*aim)(void *ctx, qa_actor_id shooter, double speed);
  /* False is the TS null. */
  bool (*model)(void *ctx, const char *path, qa_app_sim_qc_weapon_model *out);
  /* recipient NULL is the TS null. */
  void (*print)(void *ctx, const qa_actor_id *recipient, const char *text);
} qa_app_sim_qc_weapon_behavior_options;

typedef struct qa_app_sim_qc_weapon_binding {
  double slot;
  qa_saved_actor_id actor;
  const char *kind; /* "target"|"projectile" (static text) */
} qa_app_sim_qc_weapon_binding;

/* TS QuakeCWeaponBehaviorCheckpoint. */
typedef struct qa_app_sim_quakec_weapon_behavior_checkpoint {
  double version; /* 1 */
  qa_weapon_behavior_definition definition;
  const char *mode;
  double source_time;
  qa_random_state random; /* words/bytes/module owned by the checkpoint */
  qa_compat_qc_machine_snapshot machine;
  double *free_slots; size_t free_count;
  double *retired; size_t retired_count;
  qa_app_sim_qc_weapon_binding *bindings; size_t binding_count;
} qa_app_sim_quakec_weapon_behavior_checkpoint;
void qa_app_sim_quakec_weapon_behavior_checkpoint_free(qa_app_sim_quakec_weapon_behavior_checkpoint *checkpoint);

typedef struct qa_app_sim_quakec_weapon_behavior_source qa_app_sim_quakec_weapon_behavior_source;
/* TS constructor. Raises "Error" for an incapable program or a definition
 * that is not the program's exact QuakeC artifact. NULL only on OOM. */
qa_app_sim_quakec_weapon_behavior_source *qa_app_sim_quakec_weapon_behavior_source_create(
    qa_app_sim_qc_weapon_behavior_options options);
void qa_app_sim_quakec_weapon_behavior_source_destroy(qa_app_sim_quakec_weapon_behavior_source *source);
qa_weapon_behavior_definition qa_app_sim_quakec_weapon_behavior_source_definition(const qa_app_sim_quakec_weapon_behavior_source *source);
/* The private machine (for inspection by owners and tests). */
qa_compat_qc_machine *qa_app_sim_quakec_weapon_behavior_source_machine(qa_app_sim_quakec_weapon_behavior_source *source);
/* TS WeaponBehaviorSource view (attach + resume, ctx = source). */
qa_weapon_behavior_source qa_app_sim_quakec_weapon_behavior_source_as_source(qa_app_sim_quakec_weapon_behavior_source *source);
/* has_value false is the TS null. */
qa_weapon_behavior_instance_result qa_app_sim_quakec_weapon_behavior_source_attach(
    qa_app_sim_quakec_weapon_behavior_source *source, qa_weapon_behavior_launch launch);
qa_weapon_behavior_instance qa_app_sim_quakec_weapon_behavior_source_resume(
    qa_app_sim_quakec_weapon_behavior_source *source, qa_actor_id projectile);
qa_app_sim_quakec_weapon_behavior_checkpoint qa_app_sim_quakec_weapon_behavior_source_checkpoint(
    qa_app_sim_quakec_weapon_behavior_source *source);
/* The source keeps `random` (its ctx must outlive the source). */
void qa_app_sim_quakec_weapon_behavior_source_restore(qa_app_sim_quakec_weapon_behavior_source *source,
    const qa_app_sim_quakec_weapon_behavior_checkpoint *checkpoint,
    qa_actor_id (*actor_reference)(void *ctx, qa_saved_actor_id saved), void *reference_ctx,
    qa_random_source random);

/* TS readQuakeCWeaponBehaviorCheckpoint. Mode and kinds are static text;
 * definition is `expected`. */
qa_app_sim_quakec_weapon_behavior_checkpoint qa_app_sim_read_quakec_weapon_behavior_checkpoint(
    const qa_save_reader *reader, qa_weapon_behavior_definition expected);
#endif
