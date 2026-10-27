/*
 * Port of src/compat/qvm/raw-entities.ts. Mirrored types (parallel
 * slice): qa_qvmy_raw_game_data mirrors QvmGameData (TS owner
 * src/compat/qvm/game-data.ts: entityBytes(0) offset, stride, count);
 * the guest memory is the landed qa_guest_memory (TS QvmGuestMemory
 * implements it). The table and its capture context are caller-owned
 * storage filled by qa_qvm_raw_entity_table_init; each returned
 * view's ctx is malloc'd and freed by the view consumer.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_RAW_ENTITIES_H
#define QA_COMPAT_QVM_RAW_ENTITIES_H

#include <stdint.h>

#include "qa/compat/qvm/memory.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"

extern const qa_guest_layout qa_qvm_shared_entity_layout;

typedef struct qa_qvmy_raw_game_data {
  void *ctx;
  int64_t (*entity_byte_offset)(void *ctx, int32_t number); /* entityBytes(n) start */
  int64_t entity_stride_bytes;
  int64_t num_entities;
} qa_qvmy_raw_game_data;

typedef qa_actor_id_result (*qa_qvm_current_actor_fn)(void *ctx, int64_t slot, int64_t byte_offset);

typedef struct qa_qvm_raw_entity_table_context {
  const qa_qvmy_raw_game_data *data;
  qa_qvm_memory *memory;
  const qa_guest_memory *guest;
  int64_t capacity;
  int64_t start;
  int64_t stride_bytes;
  qa_guest_address base;
  qa_qvm_current_actor_fn current_actor;
  void *current_actor_ctx;
} qa_qvm_raw_entity_table_context;

void qa_qvm_raw_entity_table_init(qa_raw_entity_table *out, qa_qvm_raw_entity_table_context *context,
                                  const qa_qvmy_raw_game_data *data, qa_qvm_memory *memory,
                                  const qa_guest_memory *guest, int64_t capacity,
                                  qa_qvm_current_actor_fn current_actor, void *current_actor_ctx);

#endif
