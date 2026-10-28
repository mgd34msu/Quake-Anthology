/*
 * Port of src/compat/qvm/mod-player-events.ts. Q3SourcePlayerEvent
 * is the landed qa_app_q3_source_player_event (TS owner
 * src/app/bootstrap/simulation/q3/types.ts); the old mirror name
 * qa_qvmy_source_player_event remains as a typedef alias. Checkpoint
 * clients/predictable lists are malloc'd arrays freed by
 * qa_qvm_player_events_checkpoint_free (the read function's result)
 * or owned by the events object (checkpoint method result storage
 * passed in by the caller).
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MOD_PLAYER_EVENTS_H
#define QA_COMPAT_QVM_MOD_PLAYER_EVENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/compat/qvm/memory.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"

typedef struct qa_qvm_predictable_event {
  int32_t sequence;
  int64_t order;
} qa_qvm_predictable_event;

typedef struct qa_qvm_saved_event_cursor {
  qa_saved_actor_id actor;
  int32_t external;
  int32_t external_time;
  int32_t sequence;
  int32_t observed_sequence;
  bool has_external_order;
  int64_t external_order;
  qa_qvm_predictable_event *predictable; /* owned */
  size_t predictable_count;
} qa_qvm_saved_event_cursor;

/* TS QvmPlayerEventsCheckpoint. */
typedef struct qa_qvm_player_events_checkpoint {
  int64_t next_order;
  qa_qvm_saved_event_cursor *clients; /* owned */
  size_t client_count;
} qa_qvm_player_events_checkpoint;

/* False when the reader value is undefined (TS null). */
bool qa_qvm_read_player_events(const qa_save_reader *reader, qa_qvm_player_events_checkpoint *out);
void qa_qvm_player_events_checkpoint_free(qa_qvm_player_events_checkpoint *checkpoint);

/* TS Q3SourcePlayerEvent: landed owner type. sequence_is_predictable
 * selects sequence_number (predictable) vs sequence_time (external);
 * event/parameter are TS numbers (double). */
typedef qa_app_q3_source_player_event qa_qvmy_source_player_event;

typedef struct qa_qvm_mod_player_events_operations {
  qa_qvm_memory *memory;
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  void *ctx;
  qa_actor_id (*reference_saved)(void *ctx, qa_saved_actor_id actor);
  bool (*live)(void *ctx, qa_actor_id actor);
  qa_vec3 (*origin)(void *ctx, qa_actor_id actor);
  double (*time)(void *ctx);
  void (*emit)(void *ctx, const qa_app_q3_source_player_event *event);
} qa_qvm_mod_player_events_operations;

typedef struct qa_qvm_mod_player_events {
  qa_qvm_mod_player_events_operations operations;
  void *entries; /* opaque */
  size_t entry_count;
  size_t entry_capacity;
  int64_t next_order;
} qa_qvm_mod_player_events;

typedef struct qa_qvm_player_address {
  qa_actor_id actor;
  int64_t address;
} qa_qvm_player_address;

void qa_qvm_mod_player_events_init(qa_qvm_mod_player_events *events,
                                   const qa_qvm_mod_player_events_operations *operations);
void qa_qvm_mod_player_events_destroy(qa_qvm_mod_player_events *events);
void qa_qvm_mod_player_events_track(qa_qvm_mod_player_events *events, qa_actor_id actor, int64_t address,
                                    const qa_qvm_saved_event_cursor *saved);
void qa_qvm_mod_player_events_publish(qa_qvm_mod_player_events *events);
void qa_qvm_mod_player_events_discard(qa_qvm_mod_player_events *events);
void qa_qvm_mod_player_events_checkpoint(qa_qvm_mod_player_events *events, qa_qvm_player_events_checkpoint *out);
void qa_qvm_mod_player_events_restore(qa_qvm_mod_player_events *events,
                                      const qa_qvm_player_events_checkpoint *saved,
                                      const qa_qvm_player_address *players, size_t player_count);
void qa_qvm_mod_player_events_release(qa_qvm_mod_player_events *events, qa_actor_id actor);
void qa_qvm_mod_player_events_close(qa_qvm_mod_player_events *events);

#endif
