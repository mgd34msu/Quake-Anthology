/*
 * Port of src/compat/qvm/mod-presentation-checkpoint.ts. Entity
 * states are the landed qa_qvm_entity_state_fields with its source
 * read/write (TS owner src/compat/qvm/entity-record.ts,
 * entity_record.h) and snapshots are the landed
 * qa_qvm_source_snapshot (TS owner
 * src/compat/qvm/client-state-record.ts, client_state_record.h).
 * Scene/publication structs mirror QvmSceneContext
 * (src/compat/qvm/mod-presentation.ts) and QvmModScenePublication
 * (src/world/session/mod-presentations.ts). Captures are owned
 * qa_save_value trees. Lists are owned arrays. Snapshots read here
 * own their area mask, entity array and one 64-double player-state
 * slot block (player_state.stats.values is its base); release them
 * with qa_qvm_source_snapshot_free.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MOD_PRESENTATION_CHECKPOINT_H
#define QA_COMPAT_QVM_MOD_PRESENTATION_CHECKPOINT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/client_state_record.h"
#include "qa/compat/qvm/entity_record.h"
#include "qa/compat/qvm/memory_writes.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/session.h"
#include "qa/network/q3/game_state.h"
#include "qa/persistence/value.h"

/* Entity bytes are qa_qvm_entity_state_bytes_for (entity_record.h);
 * snapshot bytes are qa_qvm_snapshot_bytes_for
 * (client_state_record.h). */

/* TS capture result of a presentation game state (owned copies). */
typedef struct qa_qvm_presentation_game_state {
  int32_t *string_offsets; /* owned */
  size_t string_offset_count;
  uint8_t *string_data; /* owned */
  size_t string_data_length;
  int64_t data_count;
} qa_qvm_presentation_game_state;

void qa_qvm_capture_presentation_game_state(const qa_q3_source_game_state_record *state,
                                            qa_qvm_presentation_game_state *out);
void qa_qvm_read_presentation_game_state(const qa_save_reader *reader, qa_q3_source_game_state_record *out);
void qa_qvm_presentation_game_state_free(qa_qvm_presentation_game_state *state);

/* TS capturePresentationSnapshot result. */
typedef struct qa_qvm_presentation_snapshot {
  int64_t number;
  uint8_t *bytes; /* owned */
  size_t byte_length;
} qa_qvm_presentation_snapshot;

void qa_qvm_capture_presentation_snapshot(const qa_qvm_source_snapshot *snapshot, qa_qvm_abi_profile profile,
                                          qa_qvm_presentation_snapshot *out);
void qa_qvm_read_presentation_snapshot(const qa_save_reader *reader, qa_qvm_abi_profile profile,
                                       qa_qvm_source_snapshot *out);
void qa_qvm_presentation_snapshot_free(qa_qvm_presentation_snapshot *snapshot);
/* Frees a snapshot owned as read here (area mask, entities, and the
 * player-state slot block based at player_state.stats.values). */
void qa_qvm_source_snapshot_free(qa_qvm_source_snapshot *snapshot);

typedef struct qa_qvm_scene_actor {
  qa_actor_id actor;
  int64_t slot;
  bool owned;
} qa_qvm_scene_actor;

typedef struct qa_qvm_cp_scene_command {
  int64_t sequence;
  const char *const *arguments; /* borrowed from the save tree on read */
  size_t argument_count;
} qa_qvm_cp_scene_command;

typedef struct qa_qvm_scene_context {
  int64_t revision;
  qa_q3_source_game_state_record game_state;
  int64_t game_state_revision;
  qa_qvm_source_snapshot snapshot;
  qa_qvm_scene_actor *actors; /* owned */
  size_t actor_count;
  qa_qvm_cp_scene_command *commands; /* owned */
  size_t command_count;
  struct qa_qvm_scene_context *baseline; /* owned, nullable */
} qa_qvm_scene_context;

typedef qa_actor_id (*qa_qvm_resolve_saved_actor_fn)(void *ctx, qa_saved_actor_id saved);

qa_save_value *qa_qvm_capture_scene_context(const qa_qvm_scene_context *scene, qa_qvm_abi_profile profile);
void qa_qvm_read_scene_context(const qa_save_reader *reader, qa_qvm_abi_profile profile,
                               qa_qvm_resolve_saved_actor_fn resolve, void *resolve_ctx,
                               qa_qvm_scene_context *out);
void qa_qvm_scene_context_free(qa_qvm_scene_context *scene);

typedef struct qa_qvm_mod_scene_client {
  qa_actor_id actor;
  int64_t slot;
  qa_q3_player_state state;
  double state_slots[64]; /* storage for state views */
} qa_qvm_mod_scene_client;

typedef struct qa_qvm_mod_scene_entity {
  qa_actor_id actor;
  qa_qvm_entity_state_fields state;
  bool owned;
  bool linked;
  int64_t server_flags;
  int64_t single_client;
  qa_bounds bounds;
} qa_qvm_mod_scene_entity;

typedef struct qa_qvm_mod_scene_command {
  int64_t sequence;
  const qa_actor_id *recipient; /* points into recipient_storage */
  qa_actor_id recipient_storage;
  bool has_recipient;
  const char *text; /* borrowed from the save tree on read */
} qa_qvm_mod_scene_command;

typedef struct qa_qvm_cp_mod_scene_publication {
  int64_t revision;
  int64_t server_time;
  qa_q3_source_game_state_record game_state;
  int64_t game_state_revision;
  qa_qvm_mod_scene_client *clients;   /* owned */
  size_t client_count;
  qa_qvm_mod_scene_entity *entities;  /* owned */
  size_t entity_count;
  qa_qvm_mod_scene_command *commands; /* owned */
  size_t command_count;
} qa_qvm_cp_mod_scene_publication;

qa_save_value *qa_qvm_capture_mod_scene_publication(const qa_qvm_cp_mod_scene_publication *scene,
                                                    qa_qvm_abi_profile profile);
void qa_qvm_read_mod_scene_publication(const qa_save_reader *reader, qa_qvm_abi_profile profile,
                                       qa_qvm_resolve_saved_actor_fn resolve, void *resolve_ctx,
                                       qa_qvm_cp_mod_scene_publication *out);
void qa_qvm_mod_scene_publication_free(qa_qvm_cp_mod_scene_publication *scene);

#endif
