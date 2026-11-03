/* Port of src/content/q1/quakec/id1-pickups.ts. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/id1_pickups.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *value = malloc(size != 0 ? size : 1);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
static void *xrealloc(void *ptr, size_t size) {
  void *value = realloc(ptr, size != 0 ? size : 1);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
static noreturn void pickup_error(const char *message) { qa_q1qc_error("progs.dat", "%s", message); }

typedef struct source_actor { qa_owned_actor actor; int64_t slot; } source_actor;
typedef struct pickup_frame {
  bool is_null;
  const qa_q1qc_pickup_stage *stage;
  const qa_q1qc_pickup_descriptor *descriptor;
  source_actor pickup, recipient;
  qa_source_pickup_selection selection;
  qa_q1qc_function_execution execute;
  qa_source_pickup_lifetime lifetime;
  bool new_weapon, source_effect, accepted, consuming, consumed;
} pickup_frame;
typedef struct projection {
  bool active;
  qa_q1qc_inline_region region;
  int64_t reference, word;
  bool has_value;
  double value;
} projection;
struct qa_q1_id1_pickup_binding {
  qa_q1qc_source source;
  qa_original_pickup_admission admission;
  qa_q1qc_machine_fn machine;
  void *machine_ctx;
  qa_q1qc_primary_selected_fn primary_selected;
  void *primary_ctx;
  qa_q1qc_owns_weapon_fn owns_weapon;
  void *owns_ctx;
  qa_q1qc_pickup_policy_fn policy;
  void *policy_ctx;
  qa_q1qc_pickup_stage *pinned; size_t pinned_count;
  const qa_q1qc_pickup_stage **stages; size_t stage_count;
  pickup_frame *active; size_t active_count, active_capacity;
  projection current_projection;
};
static qa_q1qc_machine *pickup_vm(qa_q1_id1_pickup_binding *binding) {
  qa_q1qc_machine *vm = binding->machine(binding->machine_ctx);
  if (vm->program != binding->source.program || vm->entities != binding->source.entities)
    pickup_error("Original pickups belong to another machine");
  return vm;
}
static bool slot_is_free(qa_q1_id1_pickup_binding *binding, int64_t slot) {
  /* TS slots.options.storage.read(slot).free, mirrored by slot_free in
   * qa_q1qc_source; absent mirror reads as not free. */
  return binding->source.slot_free != NULL &&
         binding->source.slot_free(binding->source.slot_free_ctx, slot);
}
static bool pickup_actor_at(qa_q1_id1_pickup_binding *binding, int64_t reference,
                            source_actor *out) {
  int64_t slot = qa_q1qc_entities_slot(binding->source.entities, reference);
  qa_owned_actor actor;
  if (!qa_source_actor_slots_at(binding->source.slots, slot, &actor) || slot_is_free(binding, slot))
    return false;
  out->actor = actor;
  out->slot = slot;
  return true;
}
static bool pickup_live(qa_q1_id1_pickup_binding *binding, source_actor value) {
  qa_owned_actor resolved;
  if (!qa_session_actor_registry_resolve_owned(binding->source.actors, value.actor.id, &resolved) ||
      !qa_q1qc_owned_equal(resolved, value.actor)) return false;
  qa_owned_actor at;
  if (!qa_source_actor_slots_at(binding->source.slots, value.slot, &at) ||
      !qa_q1qc_owned_equal(at, value.actor)) return false;
  return !slot_is_free(binding, value.slot);
}
static void frame_cancel(pickup_frame *frame) {
  static const int32_t ZEROS[3] = {0, 0, 0};
  frame->execute.cancel(frame->execute.ctx, ZEROS);
  /* TS cancel throws; the C contract is that it does not return. */
  qa_range_error("QC function execution cancellation returned");
}
static pickup_frame *push_frame(qa_q1_id1_pickup_binding *binding) {
  if (binding->active_count == binding->active_capacity) {
    binding->active_capacity = binding->active_capacity == 0 ? 4 : binding->active_capacity * 2;
    binding->active = xrealloc(binding->active, binding->active_capacity * sizeof *binding->active);
  }
  pickup_frame *frame = &binding->active[binding->active_count++];
  memset(frame, 0, sizeof *frame);
  return frame;
}
static pickup_frame *top_frame(qa_q1_id1_pickup_binding *binding) {
  return binding->active_count == 0 ? NULL : &binding->active[binding->active_count - 1];
}
/* TS `finally { this.active.pop(); }`: also runs when execute raises (a
 * cancelled caller unwinds through QcFunctionCancellation). */
static void pop_frame(void *arg) {
  qa_q1_id1_pickup_binding *binding = arg;
  binding->active_count--;
}
void qa_q1_id1_pickup_binding_validate(qa_q1_id1_pickup_binding *binding) {
  for (size_t i = 0; i < binding->active_count; i++) {
    pickup_frame *frame = &binding->active[i];
    if (frame->is_null) continue;
    if ((!frame->consuming && !frame->consumed && !pickup_live(binding, frame->pickup)) ||
        !pickup_live(binding, frame->recipient) ||
        (!frame->consuming && frame->selection.kind == QA_SOURCE_PICKUP_REPLACEMENT &&
         !frame->selection.current(frame->selection.ctx)))
      frame_cancel(frame);
  }
}
static double pickup_scalar(qa_q1_id1_pickup_binding *binding, qa_qc_pickup_scalar input,
                            int64_t slot) {
  qa_q1qc_machine *vm = pickup_vm(binding);
  if (!input.is_global) {
    qa_q1qc_words words;
    qa_q1qc_entities_at(binding->source.entities, slot, &words);
    return qa_q1qc_words_float(&words, qa_q1qc_machine_field_offset(vm, input.name));
  }
  return qa_q1qc_words_float(&vm->globals, (int64_t)input.word);
}
static bool primary_selected(qa_q1_id1_pickup_binding *binding, qa_owned_actor actor) {
  return binding->primary_selected == NULL ||
         binding->primary_selected(binding->primary_ctx, actor);
}
static const qa_q1qc_pickup_policy *current_policy(qa_q1_id1_pickup_binding *binding) {
  return binding->policy == NULL ? NULL : binding->policy(binding->policy_ctx);
}

qa_q1_id1_pickup_binding *qa_q1_id1_pickup_binding_create(
    qa_q1qc_source source, qa_original_pickup_admission admission,
    qa_q1qc_machine_fn machine, void *machine_ctx,
    qa_q1qc_primary_selected_fn primary_selected, void *primary_ctx,
    qa_q1qc_owns_weapon_fn owns_weapon, void *owns_ctx,
    qa_q1qc_pickup_policy_fn policy, void *policy_ctx,
    qa_q1qc_pickup_stage *declared, size_t declared_count) {
  qa_q1_id1_pickup_binding *binding = xmalloc(sizeof *binding);
  memset(binding, 0, sizeof *binding);
  binding->source = source;
  binding->admission = admission;
  binding->machine = machine;
  binding->machine_ctx = machine_ctx;
  binding->primary_selected = primary_selected;
  binding->primary_ctx = primary_ctx;
  binding->owns_weapon = owns_weapon;
  binding->owns_ctx = owns_ctx;
  binding->policy = policy;
  binding->policy_ctx = policy_ctx;
  binding->pinned = qa_q1qc_pickup_stages(source.program, &binding->pinned_count);
  binding->stages = xmalloc((binding->pinned_count + declared_count) * sizeof *binding->stages);
  for (size_t i = 0; i < binding->pinned_count; i++) {
    bool replaced = false;
    for (size_t d = 0; d < declared_count; d++)
      if (declared[d].function_index == binding->pinned[i].function_index) replaced = true;
    if (!replaced) binding->stages[binding->stage_count++] = &binding->pinned[i];
  }
  for (size_t d = 0; d < declared_count; d++)
    binding->stages[binding->stage_count++] = &declared[d];
  return binding;
}
void qa_q1_id1_pickup_binding_destroy(qa_q1_id1_pickup_binding *binding) {
  if (binding == NULL) return;
  qa_q1qc_pickup_stages_destroy(binding->pinned, binding->pinned_count);
  free(binding->stages);
  free(binding->active);
  free(binding);
}
void qa_q1_id1_pickup_binding_assert_idle(const qa_q1_id1_pickup_binding *binding) {
  if (binding->active_count != 0) pickup_error("Cannot save during an original pickup caller");
}
void qa_q1_id1_pickup_binding_observe_store(qa_q1_id1_pickup_binding *binding,
                                            qa_q1qc_entity_store_observation store) {
  projection *projected = &binding->current_projection;
  if (!projected->active) return;
  if (store.reference != projected->reference || store.word != projected->word ||
      store.after_length != 4 || store.function_index != projected->region.function_index ||
      store.statement < projected->region.entry || store.statement >= projected->region.exit)
    pickup_error("Original pickup counter region wrote outside its qualified field");
  if (projected->has_value)
    pickup_error("Original pickup counter region stored more than once");
  uint32_t bits = (uint32_t)store.after[0] | ((uint32_t)store.after[1] << 8) |
                  ((uint32_t)store.after[2] << 16) | ((uint32_t)store.after[3] << 24);
  float value;
  memcpy(&value, &bits, sizeof value);
  projected->value = (double)value;
  projected->has_value = true;
}
bool qa_q1_id1_pickup_binding_selection_deferred(qa_q1_id1_pickup_binding *binding,
                                                 qa_original_pickup_offer offer) {
  pickup_frame *frame = top_frame(binding);
  if (frame == NULL || frame->is_null ||
      !qa_q1qc_actor_equal(frame->pickup.actor.id, offer.pickup) ||
      !qa_q1qc_actor_equal(frame->recipient.actor.id, offer.recipient))
    pickup_error("Pickup selection requires its held source caller");
  qa_q1_id1_pickup_binding_validate(binding);
  return frame->stage->has_source_selection;
}
qa_q1_id1_pickup_supply qa_q1_id1_pickup_binding_supply(qa_q1_id1_pickup_binding *binding,
                                                       qa_original_pickup_offer offer) {
  pickup_frame *frame = top_frame(binding);
  if (frame == NULL || frame->is_null ||
      !qa_q1qc_actor_equal(frame->pickup.actor.id, offer.pickup) ||
      !qa_q1qc_actor_equal(frame->recipient.actor.id, offer.recipient) ||
      !((frame->descriptor->item == offer.item) ||
        (frame->descriptor->item != NULL && offer.item != NULL &&
         strcmp(frame->descriptor->item, offer.item) == 0)) ||
      !frame->descriptor->has_supply)
    pickup_error("Selected supply requires its current original pickup caller");
  qa_q1_id1_pickup_binding_validate(binding);
  qa_q1qc_machine *vm = pickup_vm(binding);
  const qa_q1qc_pickup_supply *supply = &frame->descriptor->supply;
  double amount;
  if (!supply->quantity_is_global) {
    qa_q1qc_words words;
    qa_q1qc_entities_at(binding->source.entities, frame->pickup.slot, &words);
    amount = qa_q1qc_words_float(&words, qa_q1qc_machine_field_offset(vm, supply->quantity_name));
  } else {
    amount = qa_q1qc_words_float(&vm->globals, supply->quantity_word);
  }
  qa_q1_id1_pickup_supply result = {0};
  if (!supply->has_leave) {
    result.offer.kind = QA_PICKUP_SUPPLY_AMMO;
    result.offer.grant = (qa_pickup_ammo_grant){supply->item, amount};
  } else {
    result.offer.kind = QA_PICKUP_SUPPLY_WEAPON;
    result.offer.item = frame->descriptor->item;
    result.ammo_storage = (qa_pickup_ammo_grant){supply->item, amount};
    /* offer.ammo cannot point into the returned value; ammo_storage
     * carries the single grant (see header). */
    result.offer.ammo_count = 1;
  }
  result.leave = supply->has_leave && qa_q1qc_words_float(&vm->globals, supply->leave) != 0;
  return result;
}

typedef struct admission_ctx {
  qa_q1_id1_pickup_binding *binding;
  const qa_q1qc_pickup_stage *stage;
  const qa_q1qc_pickup_descriptor *descriptor;
  source_actor pickup, recipient;
  qa_q1qc_function_execution execute;
  bool new_weapon, source_effect;
} admission_ctx;
static void *admission_execute(void *ctx, qa_source_pickup_selection selection,
                               qa_source_pickup_lifetime lifetime) {
  admission_ctx *admission = ctx;
  qa_q1_id1_pickup_binding *binding = admission->binding;
  if (selection.kind == QA_SOURCE_PICKUP_STALE) {
    static const int32_t ZEROS[3] = {0, 0, 0};
    admission->execute.skip(admission->execute.ctx, ZEROS);
    return NULL;
  }
  pickup_frame *frame = push_frame(binding);
  frame->stage = admission->stage;
  frame->descriptor = admission->descriptor;
  frame->pickup = admission->pickup;
  frame->recipient = admission->recipient;
  frame->selection = selection;
  frame->execute = admission->execute;
  frame->lifetime = lifetime;
  frame->new_weapon = admission->new_weapon;
  frame->source_effect = admission->source_effect;
  qa_cleanup node;
  qa_cleanup_push(&node, pop_frame, binding);
  qa_q1qc_execution_run(&admission->execute);
  qa_cleanup_pop(&node);
  pop_frame(binding);
  return NULL;
}
static void free_cargo(void *arg) {
  qa_pickup_cargo_entry **cargo = arg;
  free(*cargo);
  *cargo = NULL;
}
static void function_composed_run(void *ctx, qa_q1qc_call_site call,
                                  qa_q1qc_function_execution execute) {
  qa_q1_id1_pickup_function_composed *composed = ctx;
  qa_q1_id1_pickup_binding *binding = composed->owner;
  bool is_selection = false;
  for (size_t i = 0; i < binding->stage_count; i++)
    if (binding->stages[i]->has_source_selection &&
        binding->stages[i]->selection_function == call.function_index) is_selection = true;
  if (is_selection) {
    pickup_frame *frame = top_frame(binding);
    const qa_q1qc_pickup_stage *stage = frame != NULL && !frame->is_null ? frame->stage : NULL;
    bool call_listed = false;
    if (stage != NULL && stage->has_source_selection)
      for (size_t i = 0; i < stage->selection_call_count; i++)
        if (stage->selection_calls[i] == call.statement) call_listed = true;
    if (frame != NULL && !frame->is_null && stage != NULL && stage->has_source_selection &&
        stage->selection_function == call.function_index && call.caller == stage->function_index &&
        call_listed && frame->selection.kind != QA_SOURCE_PICKUP_ORIGINAL &&
        !primary_selected(binding, frame->recipient.actor)) {
      qa_q1_id1_pickup_binding_validate(binding);
      qa_q1qc_machine *vm = pickup_vm(binding);
      qa_item_id item = NULL;
      for (size_t i = 0; i < stage->selection_weapon_count && item == NULL; i++) /* TS find() */
        if (qa_q1qc_words_float(&vm->globals, stage->selection_weapons[i].word) ==
            qa_q1qc_machine_arg_float(vm, 1)) item = stage->selection_weapons[i].item;
      if (!frame->accepted ||
          qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "self")) !=
              qa_q1qc_entities_reference(binding->source.entities, frame->recipient.slot))
        pickup_error("Original pickup selection lost its accepted recipient");
      if (item != NULL) {
        const qa_q1qc_pickup_policy *policy = current_policy(binding);
        if (policy == NULL) pickup_error("Original pickup has no selected weapon continuation");
        policy->select(policy->ctx, frame->recipient.actor, item);
        qa_q1_id1_pickup_binding_validate(binding);
      }
      static const int32_t ZEROS[3] = {0, 0, 0};
      execute.skip(execute.ctx, ZEROS);
      return;
    }
    if (qa_q1qc_boundary_has(&composed->inner, call.function_index))
      composed->inner.run(composed->inner.ctx, call, execute);
    else qa_q1qc_execution_run(&execute);
    return;
  }
  const qa_q1qc_pickup_stage *stage = NULL;
  for (size_t i = 0; i < binding->stage_count && stage == NULL; i++) /* TS find() */
    if (binding->stages[i]->function_index == call.function_index) stage = binding->stages[i];
  if (stage == NULL) {
    composed->inner.run(composed->inner.ctx, call, execute);
    return;
  }
  qa_q1qc_machine *vm = pickup_vm(binding);
  source_actor pickup, recipient;
  bool has_pickup = pickup_actor_at(
      binding, qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "self")), &pickup);
  bool has_recipient = pickup_actor_at(
      binding, qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "other")),
      &recipient);
  if (!has_pickup || !has_recipient) {
    static const int32_t ZEROS[3] = {0, 0, 0};
    execute.skip(execute.ctx, ZEROS);
    return;
  }
  qa_q1qc_words words;
  qa_q1qc_entities_at(binding->source.entities, pickup.slot, &words);
  bool is_cargo = strcmp(stage->descriptor_kind, "cargo") == 0;
  bool is_constant = strcmp(stage->descriptor_kind, "constant") == 0;
  const qa_q1qc_pickup_descriptor *descriptor = NULL;
  if (is_cargo || is_constant) {
    descriptor = stage->value_count != 0 ? &stage->values[0] : NULL;
  } else if (strcmp(stage->descriptor_kind, "string") == 0) {
    char value[256];
    qa_q1qc_machine_string(
        vm, qa_q1qc_words_int(&words, qa_q1qc_machine_field_offset(vm, stage->descriptor_field)),
        value, sizeof value);
    for (size_t i = 0; i < stage->value_count && descriptor == NULL; i++) /* TS find() */
      if (stage->values[i].value_is_string && strcmp(stage->values[i].value_string, value) == 0)
        descriptor = &stage->values[i];
  } else {
    double value = qa_q1qc_words_float(
        &words, qa_q1qc_machine_field_offset(vm, stage->descriptor_field));
    for (size_t i = 0; i < stage->value_count && descriptor == NULL; i++) /* TS find() */
      if (!stage->values[i].value_is_string && stage->values[i].value_number == value)
        descriptor = &stage->values[i];
  }
  if (descriptor == NULL) {
    for (size_t i = 0; i < binding->active_count; i++) {
      pickup_frame *frame = &binding->active[i];
      if (!frame->is_null && qa_q1qc_owned_equal(frame->pickup.actor, pickup.actor)) {
        static const int32_t ZEROS[3] = {0, 0, 0};
        execute.skip(execute.ctx, ZEROS);
        return;
      }
    }
    pickup_frame *frame = push_frame(binding);
    frame->is_null = true;
    qa_cleanup node;
    qa_cleanup_push(&node, pop_frame, binding);
    qa_q1qc_execution_run(&execute);
    qa_cleanup_pop(&node);
    pop_frame(binding);
    return;
  }
  bool source_effect = stage->has_source_effect &&
                       qa_q1qc_words_float(&vm->globals, stage->source_effect_word) ==
                           stage->source_effect_value;
  qa_pickup_cargo_entry *cargo = NULL;
  size_t cargo_count = 0;
  bool new_weapon = false;
  /* The cargo list is owned across the admission call, which unwinds on a
   * cancelled caller; the cleanup frees it there (TS GC). */
  qa_cleanup cargo_node;
  qa_cleanup_push(&cargo_node, free_cargo, &cargo);
  if (is_cargo && !source_effect) {
    cargo = xmalloc((stage->counter_count + 1) * sizeof *cargo);
    for (size_t i = 0; i < stage->counter_count; i++) {
      cargo[cargo_count++] = (qa_pickup_cargo_entry){
          "counter", stage->counters[i].item,
          qa_q1qc_words_float(&words, qa_q1qc_machine_field_offset(vm, stage->counters[i].field))};
    }
    double bits = qa_q1qc_words_float(&words, qa_q1qc_machine_field_offset(vm, "items"));
    if (bits != 0) {
      const qa_q1qc_pickup_word_item *weapon = NULL;
      for (size_t i = 0; i < stage->cargo_weapon_count && weapon == NULL; i++) /* TS find() */
        if (qa_q1qc_words_float(&vm->globals, stage->cargo_weapons[i].word) == bits)
          weapon = &stage->cargo_weapons[i];
      if (weapon == NULL) pickup_error("Original backpack carried weapon is not qualified");
      cargo[cargo_count++] = (qa_pickup_cargo_entry){"weapon", weapon->item, 1};
      if (binding->owns_weapon == NULL) {
        qa_q1qc_words recipient_words;
        qa_q1qc_entities_at(binding->source.entities, recipient.slot, &recipient_words);
        int32_t held = qa_js_to_int32(
            qa_q1qc_words_float(&recipient_words, qa_q1qc_machine_field_offset(vm, "items")));
        new_weapon = (held & qa_js_to_int32(bits)) == 0;
      } else {
        new_weapon = !binding->owns_weapon(binding->owns_ctx, recipient.actor, weapon->item);
      }
    }
  }
  qa_pickup_resource resource_storage;
  qa_original_pickup_offer offer = {0};
  offer.recipient = recipient.actor.id;
  offer.pickup = pickup.actor.id;
  offer.source = binding->source.provider;
  offer.item = descriptor->item;
  if (descriptor->has_resource) {
    resource_storage = descriptor->resource;
    offer.default_resource = &resource_storage;
  }
  if (descriptor->has_count) {
    offer.count.is_override = true;
    offer.count.amount = pickup_scalar(binding, descriptor->count, pickup.slot);
  }
  offer.dropped = is_cargo ||
                  (stage->has_dropped && pickup_scalar(binding, stage->dropped, pickup.slot) != 0);
  offer.time = (qa_source_time){QA_SOURCE_TIME_SECONDS,
                                qa_q1qc_words_float(&vm->globals,
                                                    qa_q1qc_machine_global_offset(vm, "time"))};
  if (source_effect) offer.grant = "source-effect";
  else if (is_cargo) { offer.cargo = cargo; offer.cargo_count = cargo_count; }
  admission_ctx admission = {binding, stage, descriptor, pickup, recipient, execute,
                             new_weapon, source_effect};
  /* TS rejects a Promise result; the C admission is synchronous. */
  (void)binding->admission.run_source(binding->admission.ctx, offer, admission_execute, &admission);
  qa_cleanup_pop(&cargo_node);
  free_cargo(&cargo);
}
void qa_q1_id1_pickup_compose_functions(qa_q1_id1_pickup_function_composed *composed,
                                        qa_q1_id1_pickup_binding *binding,
                                        qa_q1qc_function_boundary inner) {
  for (size_t i = 0; i < binding->stage_count; i++)
    if (qa_q1qc_boundary_has(&inner, binding->stages[i]->function_index))
      pickup_error("Original pickup caller already has a function boundary");
  memset(composed, 0, sizeof *composed);
  composed->owner = binding;
  composed->inner = inner;
  composed->functions = xmalloc((inner.function_count + binding->stage_count * 2) *
                                sizeof *composed->functions);
  for (size_t i = 0; i < inner.function_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < composed->function_count; j++)
      if (composed->functions[j] == inner.functions[i]) seen = true;
    if (!seen) composed->functions[composed->function_count++] = inner.functions[i];
  }
  for (size_t i = 0; i < binding->stage_count; i++) {
    int64_t extra[2] = {binding->stages[i]->function_index,
                        binding->stages[i]->has_source_selection
                            ? binding->stages[i]->selection_function : -1};
    for (int e = 0; e < 2; e++) {
      if (e == 1 && extra[e] < 0) continue;
      bool seen = false;
      for (size_t j = 0; j < composed->function_count; j++)
        if (composed->functions[j] == extra[e]) seen = true;
      if (!seen) composed->functions[composed->function_count++] = extra[e];
    }
  }
  composed->boundary = (qa_q1qc_function_boundary){composed, composed->functions,
                                                   composed->function_count, function_composed_run};
}
void qa_q1_id1_pickup_function_composed_destroy(qa_q1_id1_pickup_function_composed *composed) {
  free(composed->functions);
  memset(composed, 0, sizeof *composed);
}

typedef struct consume_remove_ctx { qa_q1qc_inline_continuation execute; } consume_remove_ctx;
static void consume_remove(void *ctx) {
  consume_remove_ctx *remove = ctx;
  remove->execute.run(remove->execute.ctx);
}
typedef struct counter_original_ctx {
  qa_q1_id1_pickup_binding *binding;
  qa_q1qc_machine *vm;
  qa_q1qc_inline_region region;
  const qa_q1qc_pickup_operation *operation;
  source_actor recipient;
} counter_original_ctx;
typedef struct counter_restore {
  qa_q1_id1_pickup_binding *binding;
  qa_q1qc_words fields;
  int64_t word;
  int32_t saved;
} counter_restore;
static void restore_counter(void *arg) {
  counter_restore *restore = arg;
  qa_q1qc_words_set_int(&restore->fields, restore->word, restore->saved);
  restore->binding->current_projection.active = false;
}
static bool counter_original(void *ctx, double count, double *out) {
  counter_original_ctx *original = ctx;
  qa_q1_id1_pickup_binding *binding = original->binding;
  qa_q1_id1_pickup_binding_validate(binding);
  if (!isfinite((double)(float)count) || binding->current_projection.active)
    pickup_error("Invalid original pickup counter projection");
  qa_q1qc_words fields;
  qa_q1qc_entities_at(binding->source.entities, original->recipient.slot, &fields);
  int64_t word = qa_q1qc_machine_field_offset(original->vm, original->operation->field);
  int32_t saved = qa_q1qc_words_int(&fields, word);
  binding->current_projection = (projection){
      true, original->region,
      qa_q1qc_entities_reference(binding->source.entities, original->recipient.slot), word,
      false, 0};
  /* TS `finally { fields.setInt(word, saved); this.projection = null; }`. */
  counter_restore restore = {binding, fields, word, saved};
  qa_cleanup node;
  qa_cleanup_push(&node, restore_counter, &restore);
  qa_q1qc_words_set_float(&fields, word, count);
  (void)original->vm->execute_region(original->vm->ctx, original->region, 0);
  bool has = binding->current_projection.has_value;
  double value = binding->current_projection.value;
  qa_cleanup_pop(&node);
  restore_counter(&restore);
  qa_q1_id1_pickup_binding_validate(binding);
  if (has) *out = value;
  return has;
}
typedef struct consuming_scope { qa_q1_id1_pickup_binding *binding; size_t index; } consuming_scope;
static void leave_consuming(void *arg) {
  consuming_scope *scope = arg;
  scope->binding->active[scope->index].consuming = false;
}
static void region_composed_run(void *ctx, qa_q1qc_inline_region region,
                                qa_q1qc_inline_continuation execute) {
  qa_q1_id1_pickup_region_composed *composed = ctx;
  qa_q1_id1_pickup_binding *binding = composed->owner;
  const qa_q1qc_pickup_region *entry = NULL;
  for (size_t s = 0; s < binding->stage_count && entry == NULL; s++)
    for (size_t r = 0; r < binding->stages[s]->region_count; r++) {
      const qa_q1qc_pickup_region *candidate = &binding->stages[s]->regions[r];
      if (candidate->region.entry == region.entry &&
          candidate->region.function_index == region.function_index) entry = candidate;
    }
  if (entry == NULL) {
    composed->inner.run(composed->inner.ctx, region, execute);
    return;
  }
  pickup_frame *frame = top_frame(binding);
  if (frame == NULL || frame->is_null || frame->stage->function_index != region.function_index) {
    execute.run(execute.ctx);
    return;
  }
  if (binding->current_projection.active &&
      binding->current_projection.region.entry == region.entry) {
    execute.run(execute.ctx);
    return;
  }
  qa_q1_id1_pickup_binding_validate(binding);
  qa_q1qc_machine *vm = pickup_vm(binding);
  frame = top_frame(binding);
  int64_t expected_self = qa_q1qc_entities_reference(
      binding->source.entities,
      frame->consumed || entry->self_recipient ? frame->recipient.slot : frame->pickup.slot);
  bool caller_current =
      qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "self")) == expected_self &&
      qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "other")) ==
          qa_q1qc_entities_reference(binding->source.entities, frame->recipient.slot);
  if (!caller_current) frame_cancel(frame);
  const char *kind = entry->operation.kind;
  if (strcmp(kind, "source-effect") == 0) {
    if (!frame->source_effect || frame->selection.kind != QA_SOURCE_PICKUP_ORIGINAL)
      frame_cancel(frame);
    execute.run(execute.ctx);
    qa_q1_id1_pickup_binding_validate(binding);
    top_frame(binding)->accepted = true;
    return;
  }
  if (strcmp(kind, "consume") == 0) {
    if (frame->source_effect && !frame->accepted)
      pickup_error("Original source effect did not reach its grant");
    frame->consuming = true;
    consume_remove_ctx remove = {execute};
    /* TS `finally { frame.consuming = false; }`, by stack index. */
    consuming_scope scope = {binding, (size_t)(frame - binding->active)};
    qa_cleanup consume_node;
    qa_cleanup_push(&consume_node, leave_consuming, &scope);
    frame->lifetime.consume_pickup(frame->lifetime.ctx, consume_remove, &remove);
    frame = top_frame(binding);
    frame->consumed = true;
    qa_cleanup_pop(&consume_node);
    leave_consuming(&scope);
    qa_q1_id1_pickup_binding_validate(binding);
    return;
  }
  const qa_q1qc_pickup_policy *policy = current_policy(binding);
  if (strcmp(kind, "counter") == 0 && !primary_selected(binding, frame->recipient.actor) &&
      policy != NULL) {
    counter_original_ctx original = {binding, vm, region, &entry->operation, frame->recipient};
    policy->counter(policy->ctx, frame->recipient.actor, entry->operation.item,
                    counter_original, &original);
    qa_q1_id1_pickup_binding_validate(binding);
    execute.skip_to_join(execute.ctx);
    return;
  }
  if (frame->selection.kind == QA_SOURCE_PICKUP_ORIGINAL) {
    execute.run(execute.ctx);
    return;
  }
  if (strcmp(kind, "weapon-selection") == 0 && primary_selected(binding, frame->recipient.actor)) {
    execute.run(execute.ctx);
    return;
  }
  if (strcmp(kind, "decision") == 0 || strcmp(kind, "admission") == 0) {
    if (frame->accepted) pickup_error("Original pickup reached more than one recipient decision");
    if (frame->selection.kind != QA_SOURCE_PICKUP_REPLACEMENT ||
        frame->selection.grant(frame->selection.ctx) != QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED)
      frame_cancel(frame);
    qa_q1_id1_pickup_binding_validate(binding);
    frame = top_frame(binding);
    caller_current =
        qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "self")) ==
            qa_q1qc_entities_reference(
                binding->source.entities,
                frame->consumed || entry->self_recipient ? frame->recipient.slot
                                                          : frame->pickup.slot) &&
        qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "other")) ==
            qa_q1qc_entities_reference(binding->source.entities, frame->recipient.slot);
    if (!caller_current) frame_cancel(frame);
    frame->accepted = true;
    if (strcmp(kind, "admission") == 0) {
      execute.run(execute.ctx);
      return;
    }
    qa_q1qc_words_set_float(&vm->globals, entry->operation.word, entry->operation.accepted);
  } else if (!frame->accepted) {
    pickup_error("Original pickup grant has no accepted recipient decision");
  }
  if (strcmp(kind, "cargo-ownership") == 0)
    qa_q1qc_words_set_float(&vm->globals, entry->operation.word, frame->new_weapon ? 1 : 0);
  if (strcmp(kind, "cargo-current") == 0) {
    qa_item_id current = policy == NULL ? NULL : policy->current(policy->ctx, frame->recipient.actor);
    double value = 0;
    if (frame->stage->has_source_selection && current != NULL)
      for (size_t i = 0; i < frame->stage->selection_weapon_count; i++)
        if (strcmp(frame->stage->selection_weapons[i].item, current) == 0) { /* TS find() */
          value = qa_q1qc_words_float(&vm->globals, frame->stage->selection_weapons[i].word);
          break;
        }
    qa_q1qc_words_set_float(&vm->globals, entry->operation.word, value);
  }
  execute.skip_to_join(execute.ctx);
}
void qa_q1_id1_pickup_compose_regions(qa_q1_id1_pickup_region_composed *composed,
                                      qa_q1_id1_pickup_binding *binding,
                                      qa_q1qc_inline_boundary inner) {
  memset(composed, 0, sizeof *composed);
  composed->owner = binding;
  composed->inner = inner;
  size_t total = inner.region_count;
  for (size_t i = 0; i < binding->stage_count; i++) total += binding->stages[i]->region_count;
  composed->regions = xmalloc(total * sizeof *composed->regions);
  for (size_t i = 0; i < inner.region_count; i++)
    composed->regions[composed->region_count++] = inner.regions[i];
  for (size_t s = 0; s < binding->stage_count; s++)
    for (size_t r = 0; r < binding->stages[s]->region_count; r++)
      composed->regions[composed->region_count++] = binding->stages[s]->regions[r].region;
  composed->boundary = (qa_q1qc_inline_boundary){composed, composed->regions,
                                                 composed->region_count, region_composed_run};
}
void qa_q1_id1_pickup_region_composed_destroy(qa_q1_id1_pickup_region_composed *composed) {
  free(composed->regions);
  memset(composed, 0, sizeof *composed);
}
