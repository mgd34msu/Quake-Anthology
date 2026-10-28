/* See include/qa/compat/qvm/raw_entities.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/raw_entities.h"

#include <stdlib.h>

#include "qa/foundation/support.h"

static const qa_guest_field_layout shared_fields[] = {
    {"s", 0, "uint8", 208}, {"r.s", 208, "uint8", 208},
    {"r.linked", 416, "int32", 1}, {"r.linkcount", 420, "int32", 1},
    {"r.svFlags", 424, "int32", 1}, {"r.singleClient", 428, "int32", 1},
    {"r.bmodel", 432, "int32", 1}, {"r.mins", 436, "float32", 3},
    {"r.maxs", 448, "float32", 3}, {"r.contents", 460, "int32", 1},
    {"r.absmin", 464, "float32", 3}, {"r.absmax", 476, "float32", 3},
    {"r.currentOrigin", 488, "float32", 3}, {"r.currentAngles", 500, "float32", 3},
    {"r.ownerNum", 512, "int32", 1},
};

const qa_guest_layout qa_qvm_shared_entity_layout = {
    "q3:shared-entity-qvm32", 516, 4, 4, shared_fields,
    sizeof shared_fields / sizeof shared_fields[0]};

typedef struct view_context {
  qa_qvm_raw_entity_table_context *table;
  int64_t slot;
  int64_t byte_offset;
} view_context;

static qa_actor_id_result view_current_actor(void *ctx) {
  view_context *view = ctx;
  return view->table->current_actor(view->table->current_actor_ctx, view->slot, view->byte_offset);
}

static qa_raw_entity_view at_slot(void *ctx, double slot_value) {
  qa_qvm_raw_entity_table_context *context = ctx;
  if (slot_value < 0.0 || slot_value >= 9007199254740991.0)
    qa_range_error("QVM entity slot exceeds table capacity");
  int64_t slot = (int64_t)slot_value;
  if (slot_value != (double)slot || slot < 0 || slot >= context->capacity)
    qa_range_error("QVM entity slot exceeds table capacity");
  qa_guest_address address = context->guest->offset(context->guest->ctx, context->base,
                                                     slot * context->stride_bytes);
  view_context *view = malloc(sizeof *view);
  if (view == NULL) qa_range_error("QVM memory allocation failed");
  view->table = context;
  view->slot = slot;
  view->byte_offset = address.byte_offset;
  qa_raw_entity_view result;
  result.ctx = view;
  result.module = context->guest->module;
  result.slot = (double)slot;
  result.address = address;
  result.stride_bytes = (double)context->stride_bytes;
  result.public_layout = qa_qvm_shared_entity_layout;
  result.bytes = context->guest->borrow(context->guest->ctx, address, (double)context->stride_bytes);
  result.current_actor = view_current_actor;
  return result;
}

static qa_raw_entity_view from_pointer(void *ctx, qa_guest_address address) {
  qa_qvm_raw_entity_table_context *context = ctx;
  (void)context->guest->borrow(context->guest->ctx, address, 0);
  int64_t displacement = address.byte_offset - context->base.byte_offset;
  if (displacement % context->stride_bytes != 0)
    qa_range_error("QVM pointer is not an entity record boundary");
  return at_slot(ctx, (double)(displacement / context->stride_bytes));
}

void qa_qvm_raw_entity_table_init(qa_raw_entity_table *out, qa_qvm_raw_entity_table_context *context,
                                  const qa_qvmy_raw_game_data *data, qa_qvm_memory *memory,
                                  const qa_guest_memory *guest, int64_t capacity,
                                  qa_qvm_current_actor_fn current_actor, void *current_actor_ctx) {
  int64_t start = data->entity_byte_offset(data->ctx, 0);
  int64_t stride_bytes = data->entity_stride_bytes;
  qa_guest_address_result base = guest->pointer(guest->ctx, start == 0 ? (int64_t)memory->length : start);
  if (!base.has_value) qa_range_error("Located QVM entity table has a null base");
  int64_t count = data->num_entities;
  if (capacity < count || count < 0 || stride_bytes < 516 ||
      start + capacity * stride_bytes > (int64_t)memory->length)
    qa_range_error("Invalid QVM entity table capacity or stride");
  context->data = data;
  context->memory = memory;
  context->guest = guest;
  context->capacity = capacity;
  context->start = start;
  context->stride_bytes = stride_bytes;
  context->base = base.value;
  context->current_actor = current_actor;
  context->current_actor_ctx = current_actor_ctx;
  out->ctx = context;
  out->module = guest->module;
  out->base = base.value;
  out->stride_bytes = (double)stride_bytes;
  out->count = (double)count;
  out->capacity = (double)capacity;
  out->layout = qa_qvm_shared_entity_layout;
  out->at_slot = at_slot;
  out->from_pointer = from_pointer;
}
