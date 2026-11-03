/*
 * Port of src/world/session/mod-presentations.ts (types only).
 * The source context is the compat/qvm QvmPresentationContext; entity
 * state fields are compat/qvm QvmEntityStateFields and the game-state
 * record is network/q3 SourceGameStateRecord. GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_PRESENTATIONS_H
#define QA_WORLD_SESSION_MOD_PRESENTATIONS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/mod_presentation.h"
#include "qa/content/mounts.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/mods.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/protocol.h"
#include "qa/platform/files/writable.h"
#include "qa/world/session/mods.h"

/* TS QvmEntityStateFields / SourceGameStateRecord, borrowed as the TS
 * publication references them. */
typedef const qa_qvm_entity_state_fields *qa_session_qvm_entity_state_fields;
typedef const qa_q3_source_game_state_record *qa_session_source_game_state_record;

typedef struct qa_qvm_scene_client {
  qa_actor_id actor;
  double slot;
  qa_q3_player_state state;
} qa_qvm_scene_client;
typedef struct qa_qvm_scene_entity {
  qa_actor_id actor;
  qa_session_qvm_entity_state_fields state;
  bool owned;
  bool linked;
  double server_flags;
  double single_client;
  qa_bounds bounds;
} qa_qvm_scene_entity;
typedef struct qa_qvm_scene_command {
  double sequence;
  const qa_actor_id *recipient; /* nullable */
  const char *text;
} qa_qvm_scene_command;

/** TS QvmModScenePublication. */
typedef struct qa_qvm_mod_scene_publication {
  double revision;
  double server_time;
  qa_session_source_game_state_record game_state;
  double game_state_revision;
  const qa_qvm_scene_client *clients;
  size_t client_count;
  const qa_qvm_scene_entity *entities;
  size_t entity_count;
  const qa_qvm_scene_command *commands;
  size_t command_count;
} qa_qvm_mod_scene_publication;

typedef struct qa_qvm_scene_pair {
  qa_qvm_mod_scene_publication current;
  const qa_qvm_mod_scene_publication *baseline; /* nullable */
} qa_qvm_scene_pair;
typedef struct qa_qvm_presentation_files {
  qa_content_mounted *mounts;
  qa_user_file_store *writable; /* nullable */
} qa_qvm_presentation_files;
typedef struct qa_qvm_presentation_binding {
  qa_actor_id actor;
  double slot;
  bool owned;
} qa_qvm_presentation_binding;

/** TS ModQvmPresentationSource. Optional members are NULL fn ptrs;
 * nullable results return false. */
struct qa_mod_qvm_presentation_source {
  void *ctx;
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  double generation;
  /* TS Omit<QvmPresentationContext, "frameTimeMilliseconds" |
   * "viewOrigin"> | null: NULL is TS null. The omitted members
   * (frame_time_milliseconds, view_origin) are zero and never read;
   * the consumer supplies them. The result and the player-state slot
   * storage it points into are owned by the source and stay valid
   * until its next context() call. */
  const qa_qvm_presentation_context *(*context)(void *ctx,
                                                qa_actor_id viewer);
  bool (*scene)(void *ctx, qa_qvm_scene_pair *out);
  bool (*files)(void *ctx, qa_qvm_presentation_files *out);
  void (*client_command)(void *ctx, qa_actor_id viewer,
                         const char *const *arguments, size_t count);
  const qa_qvm_presentation_binding *(*bindings)(void *ctx,
                                                 size_t *out_count);
  bool (*actor)(void *ctx, double slot, qa_actor_id *out);
  bool (*live)(void *ctx, qa_actor_id actor);
  void (*assert_current)(void *ctx);
};

/** TS ActiveModPresentation. */
struct qa_active_mod_presentation {
  qa_presentation_owner owner;
  qa_mod_identity identity;
  qa_prepared_mod_qvm_presentation prepared;
  qa_mod_qvm_presentation_source source;
};

#endif
