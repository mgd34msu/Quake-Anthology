/*
 * Port of src/world/session/mods.ts: mod registrations, host
 * services, prepared mods, and the SessionMods lifecycle owner.
 * TS async lifecycle calls are synchronous here; plain Error throws
 * abort identically, so TS catch-cleanup-restore paths that depend
 * on recoverable throws cannot run (the first failure is fatal).
 * Mirrored from unported modules (named per type, collision-proof
 * qa_session_* names): SimulationPresentation and the engine/physics
 * service fragments (TS owner src/app/bootstrap/simulation/types.ts
 * and siblings), the QVM bytecode artifact (TS owner
 * src/compat/qvm/artifacts.ts). Only the fields mods.ts itself
 * reads are typed; the rest stay opaque borrowed pointers.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MODS_H
#define QA_WORLD_SESSION_MODS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/artifacts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/mods.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/qvm_mod_presentation.h"
#include "qa/contracts/session.h"
#include "qa/contracts/source_items.h"
#include "qa/contracts/source_match.h"
#include "qa/contracts/time.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/gameplay/mod_composition.h"
#include "qa/world/session/mod_client_presentation.h"
#include "qa/world/session/mod_clients.h"
#include "qa/world/session/mod_engine.h"
#include "qa/world/session/resources.h"

typedef struct qa_mod_commands qa_mod_commands;
typedef struct qa_mod_user_files qa_mod_user_files;
typedef struct qa_mod_qvm_presentation_source
    qa_mod_qvm_presentation_source;
typedef struct qa_active_mod_presentation qa_active_mod_presentation;


/** TS ModOperations: borrowed operation pointers from the tables. */
typedef struct qa_mod_operations {
  qa_mod_operation *actor_think;
  qa_mod_operation *actor_touch;
  qa_mod_operation *actor_use;
  qa_mod_operation *actor_pain;
  qa_mod_operation *actor_die;
  qa_mod_operation *damage;
  qa_mod_operation *inventory_give;
  qa_mod_operation *inventory_consume;
  qa_mod_operation *inventory_configure;
  qa_mod_operation *inventory_adjust;
} qa_mod_operations;

/* A TS `() => undefined` removal closure. */
typedef struct qa_mod_registration_removal {
  void *ctx;
  void (*invoke)(void *ctx);
} qa_mod_registration_removal;

/** TS ModRegistrations. Borrowed by runtimes during register(). */
typedef struct qa_mod_registrations {
  qa_provider_id instance;
  qa_mod_operations operations;
  int64_t order;
  qa_resource_scope *resources;
} qa_mod_registrations;

/* TS ModContribution: a registration whose identity provider/order
 * are filled from the registrations. Registers and defers removal. */
qa_mod_registration_removal qa_mod_registrations_register(
    qa_mod_registrations *registrations, qa_mod_operation *operation,
    qa_mod_operation_registration contribution);

/* TS Pick<SimulationEvents, "emit" | "registerResource">: the call
 * arguments travel as one struct each (world/session/mod_engine.h), borrowed
 * for the call. Carried, never called by mods.ts. */
typedef struct qa_session_mod_engine_events {
  void *ctx;
  void (*emit)(void *ctx, const qa_session_mod_emit *event);
  void (*register_resource)(void *ctx, const qa_session_mod_resource_registration *resource);
} qa_session_mod_engine_events;

/* The bindOwner result: a session resource with its owner and the
 * events view installed into a copy of the engine services. */
typedef struct qa_session_mod_bound_events {
  qa_session_resource resource;
  qa_presentation_owner owner;
  qa_session_mod_engine_events events;
} qa_session_mod_bound_events;

/* Mirrored engine service fragment of ModHostServices (TS owners
 * src/compat/qc/presentation-host.ts, src/app/bootstrap/simulation/
 * types.ts, physics.ts, events.ts, src/contracts/scene.ts picks;
 * unported). mods.ts only tests its presence and replaces events. */
typedef struct qa_session_mod_engine_services {
  void *ctx;
  void (*print)(void *ctx, const char *text);
  bool has_environment;
  double environment_skill;
  const char *environment_mode; /* "singleplayer"|"coop"|"deathmatch" */
  double environment_max_clients;
  double environment_gravity;
  bool has_clients;
  double clients_maximum;
  const void *clients_visibility; /* Q1ClientVisibilityScene */
  const void *(*clients_at)(void *ctx, double slot);
  const char *(*classname)(void *ctx, qa_actor_id actor);
  bool (*inline_model)(void *ctx, double index, qa_bounds *out_bounds,
                       qa_content_id *out_content, const char **out_family);
  const qa_actor_id *(*area_entities)(void *ctx, qa_bounds bounds,
                                      size_t *out_count);
  void (*message)(void *ctx, qa_network_event event,
                  const qa_actor_id *actor);
  const char *presentation_map;    /* TS QuakeCLocalMessageHost.map: string */
  const qa_session_mod_engine_camera *presentation_camera; /* TS presentation.camera; NULL when absent */
  const qa_actor_id *(*presentation_players)(void *ctx, size_t *out_count);
  qa_session_mod_engine_events events;
  /* Typed views of the const void * members: world/session/mod_engine.h. */
  const void *scene;   /* SceneQueries/SharedSceneQueries picks */
  bool (*world)(void *ctx, qa_actor_id *out); /* false is TS null */
  const void *physics; /* SharedPhysics picks */
} qa_session_mod_engine_services;

/** TS ModHostServices. Optional members are NULL pointers / NULL fn
 * ptrs; match/weapons/clients borrow their service tables. */
typedef struct qa_mod_host_services {
  const qa_source_match_services *match;   /* optional */
  const qa_source_weapon_services *weapons; /* optional */
  qa_mod_client_services *clients;         /* optional */
  qa_mod_commands *commands;               /* optional (bind pick) */
  qa_mod_user_files *files;                /* optional (for pick) */
  const void *native;                      /* NativeModHostContext */
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  double seed;
  qa_actor_callback_table *callbacks; /* optional */
  void *ctx;
  /* Pick<AttackProvenance, ...> fields written through out params. */
  void (*damage_context)(void *ctx, qa_provider_id source,
                         double *out_sequence,
                         qa_provider_id *out_weapon_provider,
                         qa_provider_id *out_combat_provider,
                         qa_provider_id *out_inventory_provider,
                         qa_provider_id *out_movement_provider);
  qa_actor_id (*reference_saved)(void *ctx, qa_saved_actor_id actor);
  const qa_session_mod_engine_services *engine; /* optional */
  qa_source_time (*time)(void *ctx);
} qa_mod_host_services;

/** TS ModInitialization. next_frame/assert_current are fn + ctx. */
typedef struct qa_mod_initialization {
  bool restoring;
  const qa_mod_host_services *services; /* nullable */
  qa_provider_id instance;
  qa_resource_scope *resources;
  void *ctx;
  void (*next_frame)(void *ctx);
  void (*assert_current)(void *ctx);
} qa_mod_initialization;

typedef struct qa_mod_runtime qa_mod_runtime;
/** TS ModRuntime. Optional members are NULL fn ptrs. checkpoint and
 * restore are synchronous (TS Promise). presentations writes a
 * borrowed array. */
struct qa_mod_runtime {
  void *ctx;
  void (*close)(void *ctx);
  bool (*client_presentation)(
      void *ctx, qa_mod_client_presentation_source *out);
  bool (*qvm_presentation)(void *ctx,
                           qa_mod_qvm_presentation_source *out);
  void (*activate)(void *ctx);
  void (*register_)(void *ctx, qa_mod_registrations *registrations);
  void (*advance)(void *ctx, qa_frame_context frame);
  const qa_app_simulation_presentation *(*presentations)(
      void *ctx, size_t *out_count);
  const qa_app_simulation_presentation *(*appearance_overrides)(
      void *ctx, size_t *out_count);
  qa_mod_private_checkpoint (*checkpoint)(void *ctx);
  void (*restore)(void *ctx, qa_mod_private_checkpoint state);
};
/* The runtime viewed as a SessionResource. */
qa_session_resource qa_mod_runtime_as_resource(qa_mod_runtime *runtime);

/* TS PreparedModQvmPresentation.artifact: ResolvedQvmArtifact (borrowed). */
typedef const qa_resolved_qvm_artifact *qa_session_qvm_artifact;

/** TS NonNullable<PreparedMod["presentation"]>. */
typedef struct qa_prepared_mod_qvm_presentation {
  qa_session_qvm_artifact artifact;
  qa_module_identity source;
  qa_qvm_mod_presentation_declaration declaration;
} qa_prepared_mod_qvm_presentation;

/** TS PreparedMod. module_checkpoint: NULL | "guest" | "provider";
 * travel: NULL | "restart" | "retain". */
typedef struct qa_prepared_mod {
  void *ctx;
  bool has_client_presentation;
  qa_mod_client_presentation_admission client_presentation;
  qa_mod_description description;
  qa_mod_identity identity;
  bool has_presentation;
  qa_prepared_mod_qvm_presentation presentation;
  const char *module_checkpoint; /* nullable */
  const char *travel;            /* nullable */
  void (*validate_state)(void *ctx, qa_mod_private_checkpoint state);
  qa_mod_runtime (*initialize)(void *ctx,
                               qa_mod_initialization *context);
} qa_prepared_mod;

/* TS Pick<SimulationEvents, "bindOwner"|"finishOwnerRestore"|
 * "refreshOwner">. */
typedef struct qa_session_mod_presentation_binding {
  void *ctx;
  qa_session_mod_bound_events (*bind_owner)(void *ctx,
                                            qa_provider_id provider,
                                            qa_content_id content,
                                            bool restoring);
  void (*finish_owner_restore)(void *ctx);
  void (*refresh_owner)(void *ctx, qa_provider_id provider);
} qa_session_mods_presentation_option;

/** TS SessionModsOptions. presentation/services may be NULL. */
typedef struct qa_session_mods_options {
  const qa_session_mods_presentation_option *presentation;
  const qa_mod_host_services *services;
  const qa_prepared_mod *prepared;
  size_t prepared_count;
  const qa_mod_selection *enabled;
  size_t enabled_count;
  qa_mod_operations operations;
  void *ctx;
  void (*next_frame)(void *ctx);
} qa_session_mods_options;

typedef struct qa_session_mods qa_session_mods;

/* TS SessionMods.open (synchronous). saved/travel may be NULL, not
 * both. Returns NULL only on allocation failure; TS throws abort. */
qa_session_mods *qa_session_mods_open(
    const qa_session_mods_options *options,
    const qa_mod_session_checkpoint *saved,
    const qa_mod_travel_checkpoint *travel);
void qa_session_mods_destroy(qa_session_mods *mods);
/* Borrowed scratch, invalidated by the next call on this owner. */
const qa_mod_selection *qa_session_mods_enabled(qa_session_mods *mods,
                                                size_t *out_count);
bool qa_session_mods_owns(qa_session_mods *mods, qa_provider_id provider);
const qa_app_simulation_presentation *qa_session_mods_presentations(
    qa_session_mods *mods, size_t *out_count);
/* TS appearanceOverrides(): per-actor groups, later mods winning. */
typedef struct qa_session_mod_appearance_group {
  qa_actor_id actor;
  const qa_app_simulation_presentation *presentations;
  size_t count;
} qa_session_mod_appearance_group;
const qa_session_mod_appearance_group *qa_session_mods_appearance_overrides(
    qa_session_mods *mods, size_t *out_count);
void qa_session_mods_advance(qa_session_mods *mods,
                             qa_frame_context frame);
/* Malloc'd checkpoint mods array (caller frees the array; states are
 * runtime-borrowed). Version is the TS literal 1. */
void qa_session_mods_checkpoint(qa_session_mods *mods,
                                qa_mod_checkpoint **out_mods,
                                size_t *out_count);
const qa_active_mod_client_presentation *
qa_session_mods_client_presentation_sources(qa_session_mods *mods,
                                            size_t *out_count);
const qa_active_mod_presentation *qa_session_mods_presentation_sources(
    qa_session_mods *mods, size_t *out_count);
void qa_session_mods_checkpoint_for_travel(qa_session_mods *mods,
                                           qa_mod_travel_entry **out_mods,
                                           size_t *out_count);
void qa_session_mods_close(qa_session_mods *mods);
/* The owner viewed as a SessionResource. */
qa_session_resource qa_session_mods_as_resource(qa_session_mods *mods);

#endif
