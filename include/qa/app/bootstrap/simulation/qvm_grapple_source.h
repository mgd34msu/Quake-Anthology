/* Port of src/app/bootstrap/simulation/qvm-grapple-source.ts: one
 * initialized source VM (QvmGame) borrows session players and owns only
 * its hook actors. The VM, cvars, command buffer, files, configstrings,
 * userinfo, entity-token cursor and the QvmGrappleProvider are composed
 * here exactly as the TS class composes them. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QVM_GRAPPLE_SOURCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_QVM_GRAPPLE_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/compat/qvm/common_syscalls.h"
#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/grapple_provider.h"
#include "qa/content/mounts.h"
#include "qa/contracts/common.h"
#include "qa/contracts/math.h"
#include "qa/contracts/qvm_grapple.h"
#include "qa/contracts/session.h"
#include "qa/contracts/world.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"
/** TS QvmGrappleTarget: kind "actor" (mover) | "player" (userinfo,
 * team "free"|"red"|"blue"|"spectator", viewHeight). */
typedef struct qa_app_sim_qvm_grapple_target {
  qa_actor_id actor;
  qa_body_state body;
  double health;
  bool is_player;
  bool mover;
  const char *userinfo;
  const char *team;
  double view_height;
} qa_app_sim_qvm_grapple_target;
/** TS QvmGrappleDamage. inflictor/attacker NULL are TS null (point
 * into the struct's storage fields). */
typedef struct qa_app_sim_qvm_grapple_damage {
  qa_actor_id target;
  const qa_actor_id *inflictor, *attacker;
  qa_vec3 direction, point;
  double amount, flags, method;
} qa_app_sim_qvm_grapple_damage;
/** TS QvmGrappleSourceOptions. Borrowed tables must outlive the source;
 * targets returns a borrowed array valid until the next call. binding
 * NULL is the TS undefined. */
typedef struct qa_app_sim_qvm_grapple_source_options {
  const qa_resolved_qvm_artifact *artifact;
  const qa_qvm_grapple_definition *profile;
  qa_provider_id provider;
  qa_content_mounted *mounts;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_shared_scene_queries *scene;
  qa_command_context context;
  double seed;
  const char *entity_text;
  const char *binding;
  void *ctx;
  double (*real_time)(void *ctx, bool has_output, qa_qvm_calendar *out_calendar);
  const qa_app_sim_qvm_grapple_target *(*targets)(void *ctx, size_t *out_count);
  void (*event)(void *ctx, qa_app_q3_source_event event);
  void (*velocity)(void *ctx, qa_actor_id actor, qa_vec3 velocity);
  void (*damage)(void *ctx, qa_app_sim_qvm_grapple_damage damage);
  void (*assert_current)(void *ctx);
} qa_app_sim_qvm_grapple_source_options;
typedef struct qa_app_sim_qvm_grapple_binding { qa_saved_actor_id actor; double pointer; bool client; qa_vec3 origin; } qa_app_sim_qvm_grapple_binding;
typedef struct qa_app_sim_qvm_grapple_tether { qa_saved_actor_id owner, actor; } qa_app_sim_qvm_grapple_tether;
/** TS QvmGrappleSourceCheckpoint. bindings/tethers are owned. A
 * captured grapple owns its module bytes and owners; a read one
 * (read == true) borrows bytes and strings from the reader's tree and
 * owns only its list arrays. */
typedef struct qa_app_sim_qvm_grapple_source_checkpoint {
  double version;
  qa_qvm_grapple_checkpoint grapple;
  qa_app_sim_qvm_grapple_binding *bindings; size_t binding_count;
  qa_app_sim_qvm_grapple_tether *tethers; size_t tether_count;
  bool read;
} qa_app_sim_qvm_grapple_source_checkpoint;
qa_app_sim_qvm_grapple_source_checkpoint qa_app_sim_read_qvm_grapple_source_checkpoint(const qa_save_reader *reader);
void qa_app_sim_qvm_grapple_source_checkpoint_destroy(qa_app_sim_qvm_grapple_source_checkpoint *checkpoint);
/* TS weaponView result (also the GrappleRuntime weaponView shape):
 * the q3 members are set only by the QVM source. */
typedef struct qa_app_sim_grapple_weapon_view {
  const char *path;
  double frame;
  qa_vec3 kick_origin;
  double kick_pitch;
  bool has_q3;
  const qa_qvm_grapple_view_attachment *model_attachments; size_t model_attachment_count;
  const qa_qvm_grapple_view_anchor *model_anchor;
  double q3_time_milliseconds, q3_torso_animation;
  bool q3_firing;
  double q3_horizontal_speed, q3_bob_cycle, q3_weapon;
} qa_app_sim_grapple_weapon_view;
typedef struct qa_app_sim_qvm_grapple_source qa_app_sim_qvm_grapple_source;
/* TS static async create: synchronous; a failure closes the source and
 * rethrows. */
qa_app_sim_qvm_grapple_source *qa_app_sim_qvm_grapple_source_create(qa_app_sim_qvm_grapple_source_options options);
/* Closes (when open) and frees. */
void qa_app_sim_qvm_grapple_source_destroy(qa_app_sim_qvm_grapple_source *source);
/* TS readonly host (actors/bodies) and vm. */
qa_session_actor_registry *qa_app_sim_qvm_grapple_source_actors(const qa_app_sim_qvm_grapple_source *source);
qa_shared_body_table *qa_app_sim_qvm_grapple_source_bodies(const qa_app_sim_qvm_grapple_source *source);
qa_qvm_game *qa_app_sim_qvm_grapple_source_vm(qa_app_sim_qvm_grapple_source *source);
/* TS get core(): raises when not initialized. */
qa_qvm_grapple_provider *qa_app_sim_qvm_grapple_source_core(qa_app_sim_qvm_grapple_source *source);
void qa_app_sim_qvm_grapple_source_begin_frame(qa_app_sim_qvm_grapple_source *source, double milliseconds, double frame);
void qa_app_sim_qvm_grapple_source_fire(qa_app_sim_qvm_grapple_source *source, qa_actor_id actor);
void qa_app_sim_qvm_grapple_source_admit(qa_app_sim_qvm_grapple_source *source, qa_actor_id actor);
void qa_app_sim_qvm_grapple_source_release(qa_app_sim_qvm_grapple_source *source, qa_actor_id actor);
/* False is the TS null. */
bool qa_app_sim_qvm_grapple_source_hook(const qa_app_sim_qvm_grapple_source *source, qa_actor_id actor, qa_actor_id *out);
qa_app_sim_grapple_weapon_view qa_app_sim_qvm_grapple_source_weapon_view(qa_app_sim_qvm_grapple_source *source, qa_actor_id actor);
/* Borrowed array valid until the next call; q3_grapple_cable points at
 * storage owned by the source. */
const qa_app_simulation_presentation *qa_app_sim_qvm_grapple_source_presentations(qa_app_sim_qvm_grapple_source *source,
                                                                                  qa_content_id content, size_t *out_count);
bool qa_app_sim_qvm_grapple_source_pulling(const qa_app_sim_qvm_grapple_source *source, qa_actor_id actor);
void qa_app_sim_qvm_grapple_source_pull(qa_app_sim_qvm_grapple_source *source, qa_actor_id actor);
qa_app_sim_qvm_grapple_source_checkpoint qa_app_sim_qvm_grapple_source_capture(qa_app_sim_qvm_grapple_source *source);
void qa_app_sim_qvm_grapple_source_restore(qa_app_sim_qvm_grapple_source *source,
                                           const qa_app_sim_qvm_grapple_source_checkpoint *checkpoint);
void qa_app_sim_qvm_grapple_source_close(qa_app_sim_qvm_grapple_source *source);
#endif
