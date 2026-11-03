/* Port of src/compat/qvm/game-pickups.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/game_pickups.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/interpreter.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

typedef struct pickup_frame pickup_frame;

typedef struct watch_ctx {
  qa_qvm_primary_pickups *pickups;
  uint64_t sequence;
  double item_slot, recipient_slot;
} watch_ctx;

struct pickup_frame {
  uint64_t sequence;
  qa_qvm_module_function_call *call;
  qa_original_pickup_offer offer;
  bool has_supply;
  qa_pickup_supply_offer supply_offer;
  qa_pickup_ammo_grant supply_ammo;
  bool supply_has_quantity;
  qa_qvm_cancellation_scope cancellation;
  qa_owned_actor item, recipient;
  double item_slot, recipient_slot;
  int32_t item_pointer, player_pointer, recipient_pointer;
  const qa_qvm_catalog_record *item_record;
  const qa_qvm_pickup_grant *grant;
  qa_source_pickup_selection selection;
  bool invalid, granted, cancelled;
  char cancelled_error[QA_CAUGHT_MESSAGE_CAPACITY]; /* TS frame.cancelled.error */
  bool has_watch;
  size_t watch_id;
  watch_ctx *watch;
};

typedef struct evaluating {
  uint64_t frame_sequence;
  qa_qvm_region_evaluation region;
  bool entered;
} evaluating;

struct qa_qvm_primary_pickups {
  qa_qvm_primary_pickups_options options;
  pickup_frame *frames;
  size_t frame_count, frame_capacity;
  size_t *removals;
  size_t removal_count, removal_capacity;
  bool has_items;
  qa_qvm_catalog_records items;
  double *grant_returns;
  bool *grant_return_has;
  double *return_sites;
  int64_t *return_pcs;
  size_t return_count, return_capacity;
  bool closed;
  bool disposed;
  evaluating *evaluating_stack;
  size_t evaluating_count, evaluating_capacity;
  uint64_t next_sequence;
  void **grant_ctxs;
  size_t grant_ctx_count;
};

static bool is_integer(double v) { return isfinite(v) && v == trunc(v); }

static bool same_text(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

static bool actor_id_equal(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token &&
         a.session.token == b.session.token;
}

static bool item_id_equal(qa_item_id a, qa_item_id b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

static int32_t proceed(qa_qvm_module_function_call *call) {
  return strcmp(call->execution, "asynchronous") == 0 ? call->proceed_async(call->call_ctx)
                                                      : call->proceed(call->call_ctx);
}

static int32_t call_word(const qa_qvm_module_function_call *call, int64_t byte_offset) {
  if (byte_offset < 0 || byte_offset > (int64_t)call->words_length - 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  const uint8_t *p = call->words + byte_offset;
  uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                  ((uint32_t)p[3] << 24);
  return (int32_t)bits;
}

static void *grow(void *array, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return array;
  size_t next = *capacity == 0 ? 8 : *capacity * 2;
  void *grown = realloc(array, next * size);
  if (grown == NULL) qa_range_error("out of memory");
  *capacity = next;
  return grown;
}

static void entity_view(qa_qvm_primary_pickups *p, double slot, qa_qvm_writable_view *out) {
  qa_qvm_game_data *data = &p->options.game->data;
  if (qa_qvm_game_data_get_entity_stride_bytes(data) != p->options.profile.entity_stride ||
      qa_qvm_game_data_get_client_stride_bytes(data) != p->options.profile.client_stride)
    qa_range_error("Original pickup source records differ from their profile");
  qa_qvm_game_data_entity_bytes(data, slot, out);
}

static int32_t entity_i32(qa_qvm_primary_pickups *p, double slot, int64_t offset) {
  qa_qvm_writable_view view;
  entity_view(p, slot, &view);
  return qa_qvm_writable_view_get_int32(&view, offset, true);
}

static pickup_frame *top_frame(qa_qvm_primary_pickups *p) {
  return p->frame_count == 0 ? NULL : &p->frames[p->frame_count - 1];
}

static pickup_frame *frame_by_sequence(qa_qvm_primary_pickups *p, uint64_t sequence) {
  for (size_t i = 0; i < p->frame_count; i++)
    if (p->frames[i].sequence == sequence) return &p->frames[i];
  return NULL;
}

static void dispose(qa_qvm_primary_pickups *p) {
  if (p->disposed) return;
  p->disposed = true;
  /* TS removals.runAll(): forward order. */
  for (size_t i = 0; i < p->removal_count; i++)
    qa_qvm_module_unbind(&p->options.game->module, p->removals[i]);
  p->removal_count = 0;
}

static void remove_frame(qa_qvm_primary_pickups *p, uint64_t sequence) {
  for (size_t i = 0; i < p->frame_count; i++) {
    if (p->frames[i].sequence != sequence) continue;
    if (p->frames[i].has_watch) {
      qa_qvm_memory_writes_unobserve(&p->options.game->module.memory->writes, p->frames[i].watch_id);
      p->frames[i].has_watch = false;
    }
    free(p->frames[i].watch);
    memmove(&p->frames[i], &p->frames[i + 1], (p->frame_count - i - 1) * sizeof *p->frames);
    p->frame_count--;
    break;
  }
  if (p->closed && p->frame_count == 0) dispose(p);
}

static bool frame_live(qa_qvm_primary_pickups *p, pickup_frame *frame) {
  const qa_qvm_pickup_profile *profile = &p->options.profile;
  return !p->closed && !frame->invalid &&
         p->options.current(p->options.ctx, frame->item, frame->item_slot) &&
         p->options.current(p->options.ctx, frame->recipient, frame->recipient_slot) &&
         entity_i32(p, frame->item_slot, (int64_t)profile->field_inuse) != 0 &&
         entity_i32(p, frame->recipient_slot, (int64_t)profile->field_inuse) != 0 &&
         entity_i32(p, frame->item_slot, (int64_t)profile->field_item) ==
             qa_int32(frame->item_record->address) &&
         entity_i32(p, frame->item_slot, 160) == qa_int32(frame->item_record->index) &&
         entity_i32(p, frame->recipient_slot, (int64_t)profile->field_client) ==
             frame->player_pointer &&
         (frame->selection.kind != QA_SOURCE_PICKUP_REPLACEMENT ||
          frame->selection.current(frame->selection.ctx));
}

typedef struct cancel_job {
  void (*cancel_function)(void *ctx, qa_qvm_cancellation_scope scope);
  void *ctx;
  qa_qvm_cancellation_scope scope;
} cancel_job;

static void cancel_body(void *ctx) {
  cancel_job *job = ctx;
  job->cancel_function(job->ctx, job->scope);
}

/* TS cancel(): try { return call.cancelFunction(frame.cancellation); }
 * catch (error) { frame.cancelled = { error }; throw error; } */
static _Noreturn void cancel_with(pickup_frame *frame,
                                  void (*cancel_function)(void *ctx, qa_qvm_cancellation_scope scope),
                                  void *ctx) {
  cancel_job job = {cancel_function, ctx, frame->cancellation};
  qa_caught_error caught;
  if (qa_try(cancel_body, &job, &caught)) qa_range_error("QVM function scope cancelled");
  frame->cancelled = true;
  snprintf(frame->cancelled_error, sizeof frame->cancelled_error, "%s", caught.message);
  qa_rethrow(&caught);
}

static _Noreturn void cancel_call(qa_qvm_primary_pickups *p, pickup_frame *frame,
                                  qa_qvm_module_function_call *call) {
  (void)p;
  cancel_with(frame, call->cancel_function, call->call_ctx);
}

static _Noreturn void cancel_control(qa_qvm_primary_pickups *p, pickup_frame *frame,
                                     qa_qvm_region_control *control) {
  (void)p;
  cancel_with(frame, control->cancel_function, control->ctx);
}

static bool from_sites(qa_qvm_primary_pickups *p, qa_qvm_module_function_call *call, const double *sites,
                       size_t count) {
  int64_t offset = call->words_offset - 8;
  if (offset < 0) return false;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(call->guest, offset, 4, &view);
  int32_t pc = qa_qvm_writable_view_get_int32(&view, 0, true);
  for (size_t i = 0; i < count; i++)
    for (size_t j = 0; j < p->return_count; j++)
      if (p->return_sites[j] == sites[i] && p->return_pcs[j] == pc) return true;
  return false;
}

typedef struct projection {
  qa_qvm_memory *memory;
  int64_t *addresses;
  int32_t *before;
  size_t count;
  bool active;
} projection;

static int32_t raw_read(qa_qvm_memory *memory, int64_t address) {
  if (address < 0 || address > (int64_t)memory->length - 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  const uint8_t *bytes = memory->bytes + address;
  uint32_t bits = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
                  ((uint32_t)bytes[3] << 24);
  return (int32_t)bits;
}

static void raw_write(qa_qvm_memory *memory, int64_t address, int32_t value) {
  if (address < 0 || address > (int64_t)memory->length - 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  uint8_t *bytes = memory->bytes + address;
  uint32_t bits = (uint32_t)value;
  bytes[0] = (uint8_t)bits;
  bytes[1] = (uint8_t)(bits >> 8);
  bytes[2] = (uint8_t)(bits >> 16);
  bytes[3] = (uint8_t)(bits >> 24);
}

static void project_begin(qa_qvm_primary_pickups *p, const qa_qvm_inventory_word *writes, size_t count,
                          projection *out) {
  memset(out, 0, sizeof *out);
  out->memory = p->options.game->module.memory;
  out->count = count;
  out->active = true;
  out->addresses = malloc((count == 0 ? 1 : count) * sizeof *out->addresses);
  out->before = malloc((count == 0 ? 1 : count) * sizeof *out->before);
  if (out->addresses == NULL || out->before == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    out->addresses[i] = (int64_t)writes[i].address;
    out->before[i] = raw_read(out->memory, out->addresses[i]);
  }
  for (size_t i = 0; i < count; i++) raw_write(out->memory, out->addresses[i], qa_int32(writes[i].value));
}

static void project_restore(projection *proj) {
  if (!proj->active) return;
  proj->active = false;
  for (size_t i = 0; i < proj->count; i++) raw_write(proj->memory, proj->addresses[i], proj->before[i]);
  free(proj->addresses);
  free(proj->before);
  proj->addresses = NULL;
  proj->before = NULL;
}

static void take(qa_qvm_primary_pickups *p, pickup_frame *frame, qa_qvm_module_function_call *call) {
  if (frame->granted) qa_range_error("Original pickup source caller attempted a second grant");
  frame->granted = true;
  uint64_t sequence = frame->sequence;
  bool accepted = frame->selection.kind == QA_SOURCE_PICKUP_REPLACEMENT &&
                  frame->selection.grant(frame->selection.ctx) == QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED;
  frame = frame_by_sequence(p, sequence); /* the grant may have nested another touch */
  if (!accepted || !frame_live(p, frame)) cancel_call(p, frame, call);
}

static void restore_unwind(void *ctx) { project_restore(ctx); }

typedef struct region_run_ctx {
  qa_qvm_primary_pickups *pickups;
  qa_qvm_module_function_call *call;
  uint64_t frame_sequence;
  const qa_qvm_pickup_grant *grant;
  projection *restore;
} region_run_ctx;

static const char *grant_region_run(void *ctx, void *control_ctx) {
  region_run_ctx *run = ctx;
  /* The module binding's control_ctx is the interpreter's region control. */
  qa_qvm_region_control *control = control_ctx;
  qa_qvm_primary_pickups *p = run->pickups;
  pickup_frame *frame = frame_by_sequence(p, run->frame_sequence);
  const qa_qvm_pickup_grant *grant = run->grant;
  project_restore(run->restore);
  if (frame == NULL) qa_range_error("QVM function scope cancelled");
  if (!frame_live(p, frame)) cancel_control(p, frame, control);
  if (p->options.has_supply) {
    qa_item_id ammo = NULL;
    bool has_ammo = p->options.supply_ammo(p->options.supply_ctx, frame->item_record, &ammo);
    double amount = control->local_word(control->ctx, grant->region_quantity);
    if (!grant->has_weapon && !has_ammo)
      qa_range_error("Original ammo pickup has no admitted source counter");
    frame->has_supply = true;
    frame->supply_has_quantity = grant->has_weapon;
    if (!grant->has_weapon && has_ammo) {
      frame->supply_offer.kind = QA_PICKUP_SUPPLY_AMMO;
      frame->supply_offer.grant = (qa_pickup_ammo_grant){ammo, amount};
      frame->supply_offer.ammo = NULL;
      frame->supply_offer.ammo_count = 0;
    } else {
      frame->supply_offer.kind = QA_PICKUP_SUPPLY_WEAPON;
      frame->supply_offer.item = frame->offer.item;
      if (has_ammo) {
        frame->supply_ammo = (qa_pickup_ammo_grant){ammo, amount};
        frame->supply_offer.ammo = &frame->supply_ammo;
        frame->supply_offer.ammo_count = 1;
      } else {
        frame->supply_offer.ammo = NULL;
        frame->supply_offer.ammo_count = 0;
      }
    }
  }
  take(p, frame, run->call);
  frame = frame_by_sequence(p, run->frame_sequence);
  if (frame != NULL) frame->has_supply = false;
  return "skip";
}

typedef struct grant_ctx {
  qa_qvm_primary_pickups *pickups;
  const qa_qvm_pickup_grant *grant;
  size_t grant_index;
} grant_ctx;

static int32_t grant_hook(void *ctx, qa_qvm_module_function_call *call) {
  grant_ctx *gc = ctx;
  qa_qvm_primary_pickups *p = gc->pickups;
  const qa_qvm_pickup_grant *grant = gc->grant;
  if (p->evaluating_count > 0 && !p->evaluating_stack[p->evaluating_count - 1].entered) {
    evaluating *evaluation = &p->evaluating_stack[p->evaluating_count - 1];
    pickup_frame *frame = frame_by_sequence(p, evaluation->frame_sequence);
    if (frame == NULL || frame->grant != grant || !frame_live(p, frame) ||
        call_word(call, 0) != frame->item_pointer || call_word(call, 4) != frame->recipient_pointer)
      qa_range_error("Original pickup evaluation lost its source caller");
    evaluation->entered = true;
    return call->evaluate_region(call->call_ctx, &evaluation->region, NULL, 0);
  }
  pickup_frame *frame = top_frame(p);
  if (frame == NULL || frame->grant != grant || !from_sites(p, call, grant->calls, grant->call_count) ||
      call_word(call, 0) != frame->item_pointer || call_word(call, 4) != frame->recipient_pointer)
    return proceed(call);
  if (!frame_live(p, frame)) cancel_call(p, frame, call);
  if (frame->selection.kind != QA_SOURCE_PICKUP_REPLACEMENT) return proceed(call);
  if (!grant->is_region) {
    take(p, frame, call);
    if (!p->grant_return_has[gc->grant_index])
      qa_range_error("Missing original pickup lifecycle return");
    return (int32_t)p->grant_returns[gc->grant_index];
  }
  qa_qvm_inventory_words writes = {NULL, 0};
  bool has_writes = false;
  qa_qvm_inventory_word single_write;
  if (grant->has_weapon && p->options.has_supply) {
    bool owned = p->options.supply_owns(p->options.supply_ctx, frame->recipient, frame->offer.item);
    if (grant->weapon_storage_inventory) {
      if (p->options.supply_project == NULL)
        qa_range_error("Original pickup requires its declared inventory projection");
      p->options.supply_project(p->options.supply_ctx, frame->recipient, frame->offer.item,
                                owned ? 1 : 0, &writes);
      has_writes = true;
    } else {
      int64_t address = (int64_t)frame->player_pointer + (int64_t)grant->weapon_bits_offset;
      qa_qvm_writable_view view;
      qa_qvm_memory_data_view(p->options.game->module.memory, address, 4, &view);
      int32_t before = qa_qvm_writable_view_get_int32(&view, 0, true);
      int32_t mask = (int32_t)(1u << (uint32_t)qa_int32(frame->item_record->tag));
      single_write = (qa_qvm_inventory_word){(double)address,
                                             (double)(owned ? before | mask : before & ~mask)};
      writes.entries = &single_write;
      writes.count = 1;
    }
  }
  projection restore;
  project_begin(p, writes.entries, writes.count, &restore);
  if (has_writes) qa_qvm_inventory_words_destroy(&writes);
  region_run_ctx run = {p, call, frame->sequence, grant, &restore};
  qa_qvm_module_region_binding binding = {(int64_t)grant->region_entry, (int64_t)grant->region_join, &run,
                                    grant_region_run, NULL};
  call->regions(call->call_ctx, &binding, 1);
  qa_cleanup guard;
  qa_cleanup_push(&guard, restore_unwind, &restore);
  int32_t result = proceed(call);
  qa_cleanup_pop(&guard);
  project_restore(&restore);
  return result;
}

typedef struct live_effect_ctx {
  qa_qvm_primary_pickups *pickups;
  qa_qvm_module_function_call *call;
  uint64_t frame_sequence;
} live_effect_ctx;

static void live_effect_perform(void *ctx) {
  live_effect_ctx *effect = ctx;
  pickup_frame *frame = frame_by_sequence(effect->pickups, effect->frame_sequence);
  if (frame == NULL || !frame_live(effect->pickups, frame)) {
    if (frame != NULL) cancel_call(effect->pickups, frame, effect->call);
  }
}

static int32_t gate_hook(void *ctx, qa_qvm_module_function_call *call) {
  qa_qvm_primary_pickups *p = ctx;
  const qa_qvm_pickup_profile *profile = &p->options.profile;
  pickup_frame *frame = top_frame(p);
  if (frame == NULL || !from_sites(p, call, profile->gate_calls, profile->gate_call_count) ||
      call_word(call, (int64_t)profile->gate_item_argument * 4) != frame->item_pointer ||
      call_word(call, (int64_t)profile->gate_player_argument * 4) != frame->player_pointer)
    return proceed(call);
  if (!frame_live(p, frame)) cancel_call(p, frame, call);
  if (frame->selection.kind != QA_SOURCE_PICKUP_REPLACEMENT || frame->grant == NULL)
    return proceed(call);
  qa_qvm_pickup_eligibility context;
  context.call = call;
  entity_view(p, frame->item_slot, &context.item);
  qa_qvm_memory_data_view(p->options.game->module.memory, frame->player_pointer,
                          (int64_t)profile->client_stride, &context.player);
  uint64_t sequence = frame->sequence;
  bool value = frame->grant->eligible(frame->grant->eligible_ctx, context);
  live_effect_ctx effect = {p, call, sequence};
  call->effect(call->call_ctx, live_effect_perform, &effect);
  return value ? 1 : 0;
}

static int32_t targets_hook(void *ctx, qa_qvm_module_function_call *call) {
  qa_qvm_primary_pickups *p = ctx;
  pickup_frame *frame = top_frame(p);
  if (frame == NULL ||
      !from_sites(p, call, p->options.profile.targets_calls, p->options.profile.targets_call_count))
    return proceed(call);
  if (!frame_live(p, frame)) cancel_call(p, frame, call);
  uint64_t sequence = frame->sequence;
  int32_t result = proceed(call);
  live_effect_ctx effect = {p, call, sequence};
  call->effect(call->call_ctx, live_effect_perform, &effect);
  return result;
}

void qa_qvm_primary_pickups_after_free(qa_qvm_primary_pickups *p, double pointer,
                                       qa_qvm_module_function_call *control) {
  (void)qa_qvm_game_data_number_from_pointer(&p->options.game->data, pointer);
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(p->options.game->module.memory,
                          (int64_t)pointer + (int64_t)p->options.profile.field_inuse, 4, &view);
  if (qa_qvm_writable_view_get_int32(&view, 0, true) != 0) return;
  for (size_t i = p->frame_count; i-- > 0;) {
    if ((double)p->frames[i].item_pointer == pointer ||
        (double)p->frames[i].recipient_pointer == pointer) {
      p->frames[i].invalid = true;
      cancel_call(p, &p->frames[i], control);
    }
  }
}

static int32_t free_hook(void *ctx, qa_qvm_module_function_call *call) {
  qa_qvm_primary_pickups *p = ctx;
  int32_t pointer = call_word(call, 0);
  double slot = qa_qvm_game_data_number_from_pointer(&p->options.game->data, (double)pointer);
  qa_owned_actor actor;
  bool has_actor = p->options.actor(p->options.ctx, slot, &actor);
  int32_t result = proceed(call);
  if (has_actor &&
      entity_i32(p, slot, (int64_t)p->options.profile.field_inuse) == 0 && p->options.own_free_hook)
    p->options.retire(p->options.ctx, actor);
  qa_qvm_primary_pickups_after_free(p, (double)pointer, call);
  return result;
}

static void watch_publish(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  watch_ctx *watch = ctx;
  qa_qvm_primary_pickups *p = watch->pickups;
  pickup_frame *frame = frame_by_sequence(p, watch->sequence);
  if (frame == NULL) return;
  if (entity_i32(p, watch->item_slot, (int64_t)p->options.profile.field_inuse) == 0 ||
      entity_i32(p, watch->recipient_slot, (int64_t)p->options.profile.field_inuse) == 0)
    frame->invalid = true;
}

typedef struct execute_ctx {
  qa_qvm_primary_pickups *pickups;
  qa_qvm_module_function_call *call;
  qa_original_pickup_offer offer;
  qa_qvm_cancellation_scope cancellation;
  qa_owned_actor item, recipient;
  double item_slot, recipient_slot;
  int32_t item_pointer, player_pointer, recipient_pointer;
  const qa_qvm_catalog_record *item_record;
} execute_ctx;

typedef struct proceed_job {
  qa_qvm_module_function_call *call;
  int32_t result;
} proceed_job;

static void proceed_body(void *ctx) {
  proceed_job *job = ctx;
  job->result = proceed(job->call);
}

static int32_t execute_run(void *ctx, qa_source_pickup_selection selection) {
  execute_ctx *execute = ctx;
  qa_qvm_primary_pickups *p = execute->pickups;
  const qa_qvm_pickup_profile *profile = &p->options.profile;
  const qa_qvm_pickup_grant *grant = NULL;
  for (size_t i = 0; i < profile->grant_count; i++)
    if (profile->grants[i].item_type == execute->item_record->type) {
      grant = &profile->grants[i];
      break;
    }
  if (selection.kind == QA_SOURCE_PICKUP_BLOCKED || selection.kind == QA_SOURCE_PICKUP_STALE) return 0;
  if (selection.kind == QA_SOURCE_PICKUP_REPLACEMENT && grant == NULL)
    qa_range_error("Original pickup replacement has no qualified grant boundary");
  pickup_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.sequence = ++p->next_sequence;
  frame.call = execute->call;
  frame.offer = execute->offer;
  frame.cancellation = execute->cancellation;
  frame.item = execute->item;
  frame.recipient = execute->recipient;
  frame.item_slot = execute->item_slot;
  frame.recipient_slot = execute->recipient_slot;
  frame.item_pointer = execute->item_pointer;
  frame.player_pointer = execute->player_pointer;
  frame.recipient_pointer = execute->recipient_pointer;
  frame.item_record = execute->item_record;
  frame.grant = grant;
  frame.selection = selection;
  watch_ctx *watch = malloc(sizeof *watch);
  if (watch == NULL) qa_range_error("out of memory");
  *watch = (watch_ctx){p, frame.sequence, frame.item_slot, frame.recipient_slot};
  frame.watch = watch;
  p->frames = grow(p->frames, &p->frame_capacity, p->frame_count, sizeof *p->frames);
  p->frames[p->frame_count++] = frame;
  qa_qvm_write_range ranges[2] = {
      {(int64_t)frame.item_pointer + (int64_t)profile->field_inuse, 4},
      {(int64_t)frame.recipient_pointer + (int64_t)profile->field_inuse, 4}};
  size_t watch_id = qa_qvm_memory_observe_writes(p->options.game->module.memory, ranges, 2,
                                                 watch_publish, watch, NULL, NULL);
  pickup_frame *stored = frame_by_sequence(p, frame.sequence);
  stored->has_watch = true;
  stored->watch_id = watch_id;
  /* TS try { proceed } catch (error) { finish(); if (frame.cancelled?.error !== error) throw error; return 0; } */
  proceed_job job = {execute->call, 0};
  qa_caught_error caught;
  bool completed = qa_try(proceed_body, &job, &caught);
  if (completed) {
    remove_frame(p, frame.sequence);
    return job.result;
  }
  pickup_frame *failed = frame_by_sequence(p, frame.sequence);
  bool own = failed != NULL && failed->cancelled && strcmp(caught.message, failed->cancelled_error) == 0;
  remove_frame(p, frame.sequence);
  if (!own) qa_rethrow(&caught);
  return 0;
}

static int32_t touch_hook(void *ctx, qa_qvm_module_function_call *call) {
  qa_qvm_primary_pickups *p = ctx;
  if (p->closed) return proceed(call);
  qa_qvm_game *game = p->options.game;
  const qa_qvm_pickup_profile *profile = &p->options.profile;
  int32_t item_pointer = call_word(call, 0);
  int32_t recipient_pointer = call_word(call, 4);
  double item_slot = qa_qvm_game_data_number_from_pointer(&game->data, (double)item_pointer);
  double recipient_slot = qa_qvm_game_data_number_from_pointer(&game->data, (double)recipient_pointer);
  int32_t player_pointer = entity_i32(p, recipient_slot, (int64_t)profile->field_client);
  if (player_pointer == 0 || entity_i32(p, recipient_slot, (int64_t)profile->field_health) < 1)
    return proceed(call);
  int32_t item_address = entity_i32(p, item_slot, (int64_t)profile->field_item);
  const qa_qvm_catalog_records *records = NULL;
  if (p->options.has_catalog) records = p->options.catalog_records(p->options.catalog_ctx);
  else if (p->has_items) records = &p->items;
  const qa_qvm_catalog_record *item_record = NULL;
  if (records != NULL)
    for (size_t i = 0; i < records->count; i++)
      if (qa_int32(records->entries[i].address) == item_address) {
        item_record = &records->entries[i];
        break;
      }
  if (item_record == NULL || qa_int32(item_record->index) != entity_i32(p, item_slot, 160))
    qa_range_error("Original pickup has an undeclared source item descriptor");
  qa_owned_actor item, actor;
  bool has_item = p->options.actor(p->options.ctx, item_slot, &item);
  bool has_actor = p->options.actor(p->options.ctx, recipient_slot, &actor);
  if (!has_item || !has_actor) qa_range_error("Original pickup requires live canonical source actors");
  qa_qvm_pickup_resolved_item resolved = p->options.resolve_item(p->options.ctx, item_record);
  int32_t count = entity_i32(p, item_slot, (int64_t)profile->field_count);
  bool objective = false;
  for (size_t i = 0; i < profile->objective_type_count; i++)
    objective = objective || profile->objective_types[i] == item_record->type;
  qa_original_pickup_offer offer;
  memset(&offer, 0, sizeof offer);
  offer.recipient = actor.id;
  offer.pickup = item.id;
  offer.source = profile->module.id;
  offer.item = resolved.item;
  offer.default_resource = resolved.resource;
  offer.count = (qa_pickup_count){count != 0, (double)count};
  offer.dropped = (entity_i32(p, item_slot, (int64_t)profile->field_flags) &
                   qa_int32(profile->dropped_flag)) != 0;
  offer.time = p->options.time(p->options.ctx);
  offer.grant = objective ? "map-coupled" : NULL;
  execute_ctx execute = {p, call, offer, call->cancellation_scope(call->call_ctx), item, actor,
                         item_slot, recipient_slot, item_pointer, player_pointer, recipient_pointer,
                         item_record};
  return p->options.run_source(p->options.ctx, offer, execute_run, &execute);
}

void qa_qvm_primary_pickups_supply(qa_qvm_pickup_supply *supply, qa_qvm_primary_pickups *p,
                                   qa_original_pickup_offer offer) {
  pickup_frame *frame = top_frame(p);
  if (frame == NULL || !frame->granted || !frame->has_supply ||
      !item_id_equal(frame->offer.item, offer.item) || !actor_id_equal(frame->item.id, offer.pickup) ||
      !actor_id_equal(frame->recipient.id, offer.recipient) || !frame_live(p, frame))
    qa_range_error("Selected QVM supply requires its held original grant");
  memset(supply, 0, sizeof *supply);
  supply->offer = frame->supply_offer;
  supply->ammo_grant = frame->supply_ammo;
  if (supply->offer.ammo_count > 0) supply->offer.ammo = &supply->ammo_grant;
  supply->has_quantity = frame->supply_has_quantity;
  supply->pickups = p;
  supply->frame_sequence = frame->sequence;
}

typedef struct quantity_unwind_ctx {
  qa_qvm_primary_pickups *pickups;
  projection *restore;
} quantity_unwind_ctx;

/* TS finally { restore(); evaluating.pop(); }. */
static void quantity_unwind(void *ctx) {
  quantity_unwind_ctx *unwind = ctx;
  project_restore(unwind->restore);
  unwind->pickups->evaluating_count--;
}

double qa_qvm_pickup_supply_quantity(qa_qvm_pickup_supply *supply, double count) {
  qa_qvm_primary_pickups *p = supply->pickups;
  pickup_frame *frame = frame_by_sequence(p, supply->frame_sequence);
  pickup_frame *top = top_frame(p);
  if (frame == NULL || top == NULL || top->sequence != frame->sequence || !frame->has_supply ||
      !frame_live(p, frame))
    qa_range_error("Original pickup quantity has expired");
  const qa_qvm_pickup_grant *grant = frame->grant;
  double word = trunc(count);
  if (!isfinite(count) || word < -2147483648.0 || word > 2147483647.0)
    qa_range_error("Original QVM pickup counter exceeds its int32 ABI");
  qa_item_id ammo = NULL;
  bool has_ammo = p->options.has_supply &&
                  p->options.supply_ammo(p->options.supply_ctx, frame->item_record, &ammo);
  qa_qvm_inventory_words writes = {NULL, 0};
  bool has_writes = false;
  qa_qvm_inventory_word single_write;
  if (grant->weapon_storage_inventory) {
    if (p->options.supply_project == NULL)
      qa_range_error("Original pickup requires its declared inventory projection");
    if (has_ammo) {
      p->options.supply_project(p->options.supply_ctx, frame->recipient, ammo, word, &writes);
      has_writes = true;
    }
  } else {
    single_write = (qa_qvm_inventory_word){
        (double)((int64_t)frame->player_pointer + (int64_t)grant->weapon_ammo_offset +
                 (int64_t)frame->item_record->tag * 4),
        word};
    writes.entries = &single_write;
    writes.count = 1;
  }
  projection restore;
  project_begin(p, writes.entries, writes.count, &restore);
  if (has_writes) qa_qvm_inventory_words_destroy(&writes);
  p->evaluating_stack = grow(p->evaluating_stack, &p->evaluating_capacity, p->evaluating_count,
                             sizeof *p->evaluating_stack);
  p->evaluating_stack[p->evaluating_count++] =
      (evaluating){frame->sequence, grant->weapon_quantity, false};
  int32_t words[2] = {frame->item_pointer, frame->recipient_pointer};
  quantity_unwind_ctx unwind = {p, &restore};
  qa_cleanup guard;
  qa_cleanup_push(&guard, quantity_unwind, &unwind);
  int32_t result = qa_qvm_module_call(&p->options.game->module, words, 2, (int64_t)grant->entry);
  qa_cleanup_pop(&guard);
  project_restore(&restore);
  p->evaluating_count--;
  return (double)result;
}

static const qa_qvm_instruction *instruction_at(const qa_qvm_image *image, double index) {
  if (!is_integer(index) || index < 0 || index >= (double)image->instruction_count) return NULL;
  return &image->instructions[(size_t)index];
}

static void require_entry(const qa_qvm_image *image, double index) {
  const qa_qvm_instruction *instruction = instruction_at(image, index);
  if (instruction == NULL || instruction->opcode != QA_QVM_OP_ENTER)
    qa_range_error("Original pickup requires a source function entry");
}

static void require_calls(qa_qvm_primary_pickups *p, const qa_qvm_image *image, double target,
                          const double *indices, size_t count) {
  require_entry(image, target);
  for (size_t i = 0; i < count; i++) {
    const qa_qvm_instruction *instruction = instruction_at(image, indices[i]);
    const qa_qvm_instruction *previous = instruction_at(image, indices[i] - 1);
    if (instruction == NULL || instruction->opcode != QA_QVM_OP_CALL || previous == NULL ||
        previous->opcode != QA_QVM_OP_CONST || (double)previous->operand != target)
      qa_range_error("Original pickup call site differs from its qualified target");
    if (p->return_count == p->return_capacity) {
      size_t capacity = p->return_capacity == 0 ? 16 : p->return_capacity * 2;
      p->return_sites = realloc(p->return_sites, capacity * sizeof *p->return_sites);
      p->return_pcs = realloc(p->return_pcs, capacity * sizeof *p->return_pcs);
      if (p->return_sites == NULL || p->return_pcs == NULL) qa_range_error("out of memory");
      p->return_capacity = capacity;
    }
    p->return_sites[p->return_count] = indices[i];
    p->return_pcs[p->return_count] = instruction->byte_offset + 1;
    p->return_count++;
  }
}

static qa_qvm_instruction *region_instructions(const qa_qvm_image *image) {
  qa_qvm_instruction *list =
      malloc((image->instruction_count == 0 ? 1 : image->instruction_count) * sizeof *list);
  if (list == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < image->instruction_count; i++) {
    list[i].opcode = (int32_t)image->instructions[i].opcode;
    list[i].operand_width = image->instructions[i].operand_width;
    list[i].operand = image->instructions[i].operand;
    list[i].byte_offset = image->instructions[i].byte_offset;
  }
  return list;
}

static size_t bind_hook(qa_qvm_primary_pickups *p, double index, qa_qvm_function_hook_fn hook,
                        void *hook_ctx) {
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = qa_qvm_module_get_profile(&p->options.game->module)->module;
  reference.instruction_index = index;
  size_t id = qa_qvm_module_bind_invocation(&p->options.game->module, &reference, hook, hook_ctx);
  p->removals = grow(p->removals, &p->removal_capacity, p->removal_count, sizeof *p->removals);
  p->removals[p->removal_count++] = id;
  return id;
}

qa_qvm_primary_pickups *qa_qvm_primary_pickups_create(qa_qvm_primary_pickups_options options) {
  qa_qvm_game *game = options.game;
  const qa_resolved_qvm_artifact *artifact = options.artifact;
  const qa_qvm_pickup_profile *profile = &options.profile;
  const qa_qvm_image *image = &artifact->image;
  const qa_module_identity *module = &qa_qvm_module_get_profile(&game->module)->module;
  if (!same_text(artifact->module.id, module->id) || !same_text(artifact->module.digest, module->digest) ||
      !same_text(profile->module.id, module->id) || !same_text(profile->module.digest, module->digest) ||
      !same_text(profile->module.revision, module->revision) ||
      !same_text(profile->module.artifact_path, module->artifact_path) ||
      !same_text(profile->abi_profile, qa_qvm_module_get_abi_profile(&game->module)))
    qa_range_error("Original pickup profile differs from its QVM executable");
  double fields[6] = {profile->field_inuse, profile->field_client, profile->field_health,
                      profile->field_item, profile->field_count, profile->field_flags};
  for (size_t i = 0; i < 6; i++)
    if (!is_integer(fields[i]) || fields[i] < 0 || (int64_t)fields[i] % 4 != 0 ||
        fields[i] + 4 > profile->entity_stride)
      qa_range_error("Original pickup field is outside its entity record");
  qa_qvm_primary_pickups *p = calloc(1, sizeof *p);
  if (p == NULL) return NULL;
  p->options = options;
  p->grant_returns = calloc(profile->grant_count == 0 ? 1 : profile->grant_count, sizeof(double));
  p->grant_return_has = calloc(profile->grant_count == 0 ? 1 : profile->grant_count, sizeof(bool));
  if (p->grant_returns == NULL || p->grant_return_has == NULL) {
    free(p->grant_returns);
    free(p->grant_return_has);
    free(p);
    return NULL;
  }
  require_entry(image, profile->touch);
  require_entry(image, profile->free_entry);
  require_calls(p, image, profile->gate_entry, profile->gate_calls, profile->gate_call_count);
  require_calls(p, image, profile->targets_entry, profile->targets_calls, profile->targets_call_count);
  qa_qvm_instruction *list = NULL;
  for (size_t g = 0; g < profile->grant_count; g++) {
    const qa_qvm_pickup_grant *grant = &profile->grants[g];
    for (size_t j = 0; j < g; j++)
      if (profile->grants[j].item_type == grant->item_type)
        qa_range_error("Original pickup has duplicate grant types");
    require_calls(p, image, grant->entry, grant->calls, grant->call_count);
    if (grant->is_region) {
      if (list == NULL) list = region_instructions(image);
      (void)qa_qvm_qualify_region(list, image->instruction_count, (int64_t)grant->entry,
                                   (int64_t)grant->region_entry, (int64_t)grant->region_join);
      if (grant->has_weapon)
        (void)qa_qvm_qualify_region_evaluation(list, image->instruction_count, (int64_t)grant->entry,
                                                &grant->weapon_quantity, "source");
    } else {
      const qa_qvm_instruction *value = instruction_at(image, grant->accepted_return);
      const qa_qvm_instruction *leave = instruction_at(image, grant->accepted_return + 1);
      if (value == NULL || value->opcode != QA_QVM_OP_CONST || leave == NULL ||
          leave->opcode != QA_QVM_OP_LEAVE || value->operand == 0)
        qa_range_error("Original pickup lifecycle return is not a qualified source constant");
      double owner = grant->accepted_return;
      while (owner >= 0) {
        const qa_qvm_instruction *candidate = instruction_at(image, owner);
        if (candidate != NULL && candidate->opcode == QA_QVM_OP_ENTER) break;
        owner--;
      }
      if (owner != grant->entry)
        qa_range_error("Original pickup lifecycle return belongs to another source function");
      p->grant_returns[g] = (double)value->operand;
      p->grant_return_has[g] = true;
    }
  }
  free(list);
  if (profile->items.has_source && same_text(profile->items.source, "live") && !options.has_catalog)
    qa_range_error("Live original pickups require their source catalog owner");
  if (!options.has_catalog) {
    if (!qa_qvm_read_item_records(image->initialized_data, image->initialized_length, profile->items,
                                  NULL, 0, NULL, &p->items)) {
      qa_qvm_primary_pickups_destroy(p);
      return NULL;
    }
    p->has_items = true;
  }
  bind_hook(p, profile->touch, touch_hook, p);
  bind_hook(p, profile->gate_entry, gate_hook, p);
  p->grant_ctxs = calloc(profile->grant_count == 0 ? 1 : profile->grant_count, sizeof(void *));
  if (p->grant_ctxs == NULL) qa_range_error("out of memory");
  for (size_t g = 0; g < profile->grant_count; g++) {
    grant_ctx *gc = malloc(sizeof *gc);
    if (gc == NULL) qa_range_error("out of memory");
    *gc = (grant_ctx){p, &profile->grants[g], g};
    p->grant_ctxs[p->grant_ctx_count++] = gc;
    bind_hook(p, profile->grants[g].entry, grant_hook, gc);
  }
  bind_hook(p, profile->targets_entry, targets_hook, p);
  if (options.own_free_hook) bind_hook(p, profile->free_entry, free_hook, p);
  return p;
}

void qa_qvm_primary_pickups_close(qa_qvm_primary_pickups *p) {
  if (p == NULL) return;
  p->closed = true;
  if (p->frame_count == 0) dispose(p);
}

void qa_qvm_primary_pickups_assert_idle(const qa_qvm_primary_pickups *p) {
  if (p->frame_count != 0) qa_range_error("Cannot save during original QVM pickup execution");
}

void qa_qvm_primary_pickups_destroy(qa_qvm_primary_pickups *p) {
  if (p == NULL) return;
  qa_qvm_primary_pickups_close(p);
  dispose(p);
  for (size_t i = 0; i < p->frame_count; i++) free(p->frames[i].watch);
  free(p->frames);
  free(p->removals);
  if (p->has_items) qa_qvm_catalog_records_destroy(&p->items);
  free(p->grant_returns);
  free(p->grant_return_has);
  free(p->return_sites);
  free(p->return_pcs);
  free(p->evaluating_stack);
  for (size_t i = 0; i < p->grant_ctx_count; i++) free(p->grant_ctxs[i]);
  free(p->grant_ctxs);
  free(p);
}

