/* Port of src/app/bootstrap/simulation/qvm-weapon-behavior.ts.
 * Executes source QVM callbacks, never a second game frame or selected
 * projectile impact. TS `throw new Error(msg)` raises qa_raise("Error", ...);
 * AggregateError rollbacks raise "AggregateError" with the first message.
 * The TS async create is synchronous (QvmGame.initializeAsync is synchronous
 * in C).
 *
 * Ownership: the source borrows options.artifact, options.mounts,
 * options.scene and every string reachable from options; it keeps its own
 * validated copy of the profile (definition strings live as long as the
 * source). Instances stay valid until the source is destroyed. Checkpoints
 * from a source own their module bytes; checkpoints from the reader borrow
 * the reader tree. Free both with qa_app_sim_qvm_weapon_behavior_checkpoint_free.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QVM_WEAPON_BEHAVIOR_H
#define QA_APP_BOOTSTRAP_SIMULATION_QVM_WEAPON_BEHAVIOR_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/common_syscalls.h"
#include "qa/compat/qvm/weapon_behavior_profile.h"
#include "qa/content/mounts.h"
#include "qa/contracts/common.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/world.h"
#include "qa/persistence/value.h"
#include "qa/world/collision/index.h"

/* TS QvmWeaponTarget: {actor, body, health} & ({kind:'actor'} |
 * {kind:'player', userinfo, team:'free'|'red'|'blue'|'spectator'}). */
typedef struct qa_app_sim_qvm_weapon_target {
  qa_actor_id actor; qa_body_state body; double health;
  const char *kind;     /* "actor"|"player" */
  const char *userinfo; /* player */
  const char *team;     /* player */
} qa_app_sim_qvm_weapon_target;

/* TS QvmWeaponBehaviorOptions. realTime is TS QvmCommonServices['realTime']
 * and receives ctx like the other callbacks. */
typedef struct qa_app_sim_qvm_weapon_behavior_options {
  const qa_resolved_qvm_artifact *artifact;
  const qa_qvm_weapon_profile *profile;
  qa_content_mounted *mounts;
  qa_shared_scene_queries *scene;
  qa_command_context context;
  double seed;
  const char *entity_text;
  const char *mode; /* "singleplayer"|"coop"|"deathmatch" */
  bool team_mode;
  void *ctx;
  double (*real_time)(void *ctx, bool has_output, qa_qvm_calendar *out_calendar);
  const qa_app_sim_qvm_weapon_target *(*targets)(void *ctx, size_t *out_count);
  void (*print)(void *ctx, const char *text);
  void (*assert_current)(void *ctx);
} qa_app_sim_qvm_weapon_behavior_options;

typedef struct qa_app_sim_qvm_weapon_binding { qa_saved_actor_id actor; double pointer; const char *kind; /* "target"|"client"|"projectile" */ } qa_app_sim_qvm_weapon_binding;
typedef struct qa_app_sim_qvm_weapon_retired { qa_saved_actor_id actor; qa_weapon_trajectory_update trajectory; } qa_app_sim_qvm_weapon_retired;
/* TS QvmWeaponBehaviorCheckpoint. */
typedef struct qa_app_sim_qvm_weapon_behavior_checkpoint {
  double version;
  qa_weapon_behavior_definition definition;
  qa_guest_checkpoint module;
  char profile_digest[65];
  qa_saved_actor_id *activated; size_t activated_count;
  qa_app_sim_qvm_weapon_binding *bindings; size_t binding_count;
  qa_app_sim_qvm_weapon_retired *retired; size_t retired_count;
  bool owns_module; /* module data and host bytes are malloc'd */
} qa_app_sim_qvm_weapon_behavior_checkpoint;
void qa_app_sim_qvm_weapon_behavior_checkpoint_free(qa_app_sim_qvm_weapon_behavior_checkpoint *checkpoint);

typedef struct qa_app_sim_qvm_weapon_behavior_source qa_app_sim_qvm_weapon_behavior_source;
/* TS static create: constructs, runs GAME_INIT and validates the located
 * layout; closes and rethrows on failure. NULL only on OOM. */
qa_app_sim_qvm_weapon_behavior_source *qa_app_sim_qvm_weapon_behavior_source_create(qa_app_sim_qvm_weapon_behavior_options options);
/* TS static restore: a new source over the saved module (never initialized). */
qa_app_sim_qvm_weapon_behavior_source *qa_app_sim_qvm_weapon_behavior_source_create_restored(
    qa_app_sim_qvm_weapon_behavior_options options, const qa_app_sim_qvm_weapon_behavior_checkpoint *checkpoint,
    qa_actor_id (*resolve_actor)(void *ctx, qa_saved_actor_id saved), void *resolve_ctx);
/* Frees memory; the TS close is separate (and runs here when still open). */
void qa_app_sim_qvm_weapon_behavior_source_destroy(qa_app_sim_qvm_weapon_behavior_source *source);
qa_weapon_behavior_definition qa_app_sim_qvm_weapon_behavior_source_definition(const qa_app_sim_qvm_weapon_behavior_source *source);
/* The private QvmGame (for owners and tests). */
struct qa_qvm_game *qa_app_sim_qvm_weapon_behavior_source_game(qa_app_sim_qvm_weapon_behavior_source *source);
qa_weapon_behavior_source qa_app_sim_qvm_weapon_behavior_source_as_source(qa_app_sim_qvm_weapon_behavior_source *source);
/* TS attach never returns null. */
qa_weapon_behavior_instance qa_app_sim_qvm_weapon_behavior_source_attach(qa_app_sim_qvm_weapon_behavior_source *source, qa_weapon_behavior_launch launch);
qa_weapon_behavior_instance qa_app_sim_qvm_weapon_behavior_source_resume(qa_app_sim_qvm_weapon_behavior_source *source, qa_actor_id actor);
qa_app_sim_qvm_weapon_behavior_checkpoint qa_app_sim_qvm_weapon_behavior_source_checkpoint(qa_app_sim_qvm_weapon_behavior_source *source);
void qa_app_sim_qvm_weapon_behavior_source_restore(qa_app_sim_qvm_weapon_behavior_source *source,
    const qa_app_sim_qvm_weapon_behavior_checkpoint *checkpoint,
    qa_actor_id (*resolve_actor)(void *ctx, qa_saved_actor_id saved), void *resolve_ctx);
void qa_app_sim_qvm_weapon_behavior_source_close(qa_app_sim_qvm_weapon_behavior_source *source);
/* TS readQvmWeaponBehaviorCheckpoint. */
qa_app_sim_qvm_weapon_behavior_checkpoint qa_app_sim_read_qvm_weapon_behavior_checkpoint(
    const qa_save_reader *reader, qa_weapon_behavior_definition expected);
/* TS qvmWeaponInitializationEntities. Malloc'd entity text; caller frees. */
char *qa_app_sim_qvm_weapon_initialization_entities(const char *text);
#endif
