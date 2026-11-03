/* Port of src/app/bootstrap/simulation/mod-callbacks.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/mod_callbacks.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/gameplay/inventory.h"
double qa_app_sim_mod_callback_seconds(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}
const qa_mod_runtime_value *qa_app_sim_mod_callback_inputs_get(const qa_app_sim_mod_callback_inputs *inputs,
                                                              const char *name) {
  for (size_t i = 0; i < inputs->count; i++)
    if (strcmp(inputs->names[i], name) == 0) return &inputs->values[i];
  return NULL;
}
static size_t slot_for(qa_app_sim_mod_callback_inputs *inputs, const char *name) {
  for (size_t i = 0; i < inputs->count; i++)
    if (strcmp(inputs->names[i], name) == 0) return i;
  if (inputs->count == QA_APP_SIM_MOD_CALLBACK_INPUT_CAPACITY) qa_range_error("Mod callback input map is full");
  inputs->names[inputs->count] = name;
  return inputs->count++;
}
static void set_scalar(qa_app_sim_mod_callback_inputs *inputs, const char *name, double value) {
  size_t i = slot_for(inputs, name);
  memset(&inputs->values[i], 0, sizeof inputs->values[i]);
  inputs->values[i].kind = "float";
  inputs->values[i].number_value = value;
}
/* TS actor(value): {kind:"actor", value: ActorId | null}. */
static void set_actor(qa_app_sim_mod_callback_inputs *inputs, const char *name, const qa_actor_id *value) {
  size_t i = slot_for(inputs, name);
  memset(&inputs->values[i], 0, sizeof inputs->values[i]);
  inputs->values[i].kind = "actor";
  if (value != NULL) {
    inputs->actors[i] = *value;
    inputs->values[i].actor_value = &inputs->actors[i];
  }
}
static void set_string(qa_app_sim_mod_callback_inputs *inputs, const char *name, const char *value) {
  size_t i = slot_for(inputs, name);
  memset(&inputs->values[i], 0, sizeof inputs->values[i]);
  inputs->values[i].kind = "string";
  inputs->values[i].string_value = value;
}
static void set_vector(qa_app_sim_mod_callback_inputs *inputs, const char *name, qa_vec3 value) {
  size_t i = slot_for(inputs, name);
  memset(&inputs->values[i], 0, sizeof inputs->values[i]);
  inputs->values[i].kind = "vector";
  inputs->values[i].vector_value = value;
}

/* One closure block per callback: the TS `callback` plus the shared
 * time/execute closures. */
typedef struct entry {
  const void *callback;
  const qa_mod_callback_binding *binding;
  qa_app_sim_mod_callback_host host;
} entry;
/* TS inputs(request) builders: they write into a scratch map that
 * invoke then layers after "time". */
typedef void (*input_fn)(qa_app_sim_mod_callback_inputs *values, const void *request);

/* TS invoke: Map([["time", scalar(seconds(time()))], ...values]) then
 * result. values is built before time() runs, as in TS. */
static bool invoke(const entry *e, const qa_app_sim_mod_callback_inputs *values, bool has_result, double result,
                   double *out) {
  qa_app_sim_mod_callback_inputs inputs;
  memset(&inputs, 0, sizeof inputs);
  qa_source_time now = e->host.time(e->host.ctx);
  set_scalar(&inputs, "time", qa_app_sim_mod_callback_seconds(now));
  for (size_t i = 0; i < values->count; i++) {
    size_t at = slot_for(&inputs, values->names[i]);
    inputs.values[at] = values->values[i];
    if (values->values[i].actor_value != NULL) {
      inputs.actors[at] = *values->values[i].actor_value;
      inputs.values[at].actor_value = &inputs.actors[at];
    }
  }
  if (has_result) set_scalar(&inputs, "result", result);
  return e->host.execute(e->host.ctx, e->callback, &inputs, out);
}
static void build(qa_app_sim_mod_callback_inputs *values, input_fn inputs, const void *request) {
  memset(values, 0, sizeof *values);
  inputs(values, request);
}

/* --- damage --- */
static void damage_inputs(qa_app_sim_mod_callback_inputs *v, const void *raw) {
  const qa_damage_request *request = raw;
  set_actor(v, "self", &request->target);
  set_actor(v, "attacker", request->attack.attacker);
  set_actor(v, "inflictor", request->attack.inflictor);
  set_scalar(v, "amount", request->amount);
  set_scalar(v, "knockback", request->knockback);
  set_vector(v, "direction", request->direction);
  set_vector(v, "point", request->point);
  set_vector(v, "normal", request->normal);
}
static void damage_transform(void *ctx, const void *raw, void *out_request) {
  const entry *e = ctx;
  qa_damage_request request = *(const qa_damage_request *)raw;
  qa_app_sim_mod_callback_inputs values;
  build(&values, damage_inputs, &request);
  double result = 0;
  bool knockback = strcmp(e->binding->result, "knockback") == 0;
  if (invoke(e, &values, false, 0, &result)) {
    if (knockback) request.knockback = result;
    else request.amount = result;
  }
  *(qa_damage_request *)out_request = request;
}
static void damage_observe(void *ctx, const void *raw, const void *raw_result) {
  const entry *e = ctx;
  const qa_damage_outcome *outcome = raw_result;
  qa_app_sim_mod_callback_inputs values;
  build(&values, damage_inputs, raw);
  double ignored;
  (void)invoke(e, &values, true,
               strcmp(outcome->kind, "committed") == 0 ? outcome->decision.applied_damage : 0, &ignored);
}

/* --- inventory --- */
static void inventory_inputs(qa_app_sim_mod_callback_inputs *v, const void *raw) {
  const qa_inventory_give_request *request = raw;
  set_actor(v, "self", &request->actor.id);
  set_string(v, "item", request->item);
  set_scalar(v, "amount", request->count);
}
static void inventory_transform(void *ctx, const void *raw, void *out_request) {
  const entry *e = ctx;
  qa_inventory_give_request request = *(const qa_inventory_give_request *)raw;
  qa_app_sim_mod_callback_inputs values;
  build(&values, inventory_inputs, &request);
  double result = 0;
  if (invoke(e, &values, false, 0, &result)) request.count = result;
  *(qa_inventory_give_request *)out_request = request;
}
static void give_observe(void *ctx, const void *raw, const void *raw_result) {
  qa_app_sim_mod_callback_inputs values;
  build(&values, inventory_inputs, raw);
  double ignored;
  (void)invoke(ctx, &values, true, *(const double *)raw_result, &ignored);
}
static void consume_observe(void *ctx, const void *raw, const void *raw_result) {
  qa_app_sim_mod_callback_inputs values;
  build(&values, inventory_inputs, raw);
  double ignored;
  (void)invoke(ctx, &values, true, *(const bool *)raw_result ? 1 : 0, &ignored);
}

/* --- actor operations --- */
static void think_inputs(qa_app_sim_mod_callback_inputs *v, const void *raw) {
  const qa_actor_think_request *request = raw;
  set_actor(v, "self", &request->self.id);
  set_scalar(v, "time", qa_app_sim_mod_callback_seconds(request->frame.time));
  set_scalar(v, "elapsed", qa_app_sim_mod_callback_seconds(request->frame.elapsed));
}
static void touch_inputs(qa_app_sim_mod_callback_inputs *v, const void *raw) {
  const qa_actor_touch_request *request = raw;
  set_actor(v, "self", &request->contact.self.id);
  set_actor(v, "other", &request->contact.other);
}
static void use_inputs(qa_app_sim_mod_callback_inputs *v, const void *raw) {
  const qa_actor_use_request *request = raw;
  set_actor(v, "self", &request->self.id);
  set_actor(v, "other", request->has_other ? &request->other : NULL);
  set_actor(v, "activator", request->has_activator ? &request->activator : NULL);
}
static void pain_inputs(qa_app_sim_mod_callback_inputs *v, const void *raw) {
  const qa_actor_pain_request *request = raw;
  set_actor(v, "self", &request->reaction.self.id);
  set_actor(v, "attacker", request->reaction.attacker);
  set_scalar(v, "amount", request->reaction.damage);
  set_scalar(v, "knockback", request->reaction.kick);
}
static void die_inputs(qa_app_sim_mod_callback_inputs *v, const void *raw) {
  const qa_actor_die_request *request = raw;
  set_actor(v, "self", &request->reaction.pain.self.id);
  set_actor(v, "attacker", request->reaction.pain.attacker);
  set_actor(v, "inflictor", request->reaction.inflictor);
  set_scalar(v, "amount", request->reaction.pain.damage);
  set_scalar(v, "knockback", request->reaction.pain.kick);
  set_vector(v, "point", request->reaction.point);
}
typedef struct actor_entry {
  entry base;
  input_fn inputs;
} actor_entry;
static void actor_observe(void *ctx, const void *raw, const void *raw_result) {
  const actor_entry *e = ctx;
  qa_app_sim_mod_callback_inputs values;
  build(&values, e->inputs, raw);
  double ignored;
  (void)invoke(&e->base, &values, true, *(const bool *)raw_result ? 1 : 0, &ignored);
}
static void actor_replace(void *ctx, const void *raw, qa_mod_next next, void *out_result) {
  const actor_entry *e = ctx;
  qa_app_sim_mod_callback_inputs values;
  build(&values, e->inputs, raw);
  double result = 0;
  if (!invoke(&e->base, &values, false, 0, &result)) next.call(next.ctx, raw, out_result);
  else *(bool *)out_result = result != 0;
}

static void free_block(void *ctx) { free(ctx); }
static void *closure(qa_mod_registrations *registrations, size_t size) {
  void *block = calloc(1, size);
  if (block == NULL) qa_range_error("Out of memory");
  qa_resource_scope_defer(registrations->resources, free_block, block);
  return block;
}
static void register_one(qa_mod_registrations *registrations, qa_mod_operation *operation, const char *id,
                         qa_mod_registration_kind kind, void *ctx,
                         void (*transform)(void *, const void *, void *),
                         void (*observe)(void *, const void *, const void *),
                         void (*replace)(void *, const void *, qa_mod_next, void *)) {
  qa_mod_operation_registration contribution;
  memset(&contribution, 0, sizeof contribution);
  contribution.identity.id = id;
  contribution.kind = kind;
  contribution.ctx = ctx;
  contribution.transform = transform;
  contribution.observe = observe;
  contribution.replace = replace;
  (void)qa_mod_registrations_register(registrations, operation, contribution);
}
/* TS actors(callback, operation, inputs): only observe/replace stages. */
static void actors(qa_mod_registrations *registrations, const entry *base, qa_mod_operation *operation,
                   input_fn inputs) {
  if (strcmp(base->binding->stage, "observe") != 0 && strcmp(base->binding->stage, "replace") != 0) return;
  actor_entry *e = closure(registrations, sizeof *e);
  e->base = *base;
  e->inputs = inputs;
  if (strcmp(base->binding->stage, "observe") == 0)
    register_one(registrations, operation, base->binding->id, QA_MOD_OBSERVE, e, NULL, actor_observe, NULL);
  else register_one(registrations, operation, base->binding->id, QA_MOD_REPLACE, e, NULL, NULL, actor_replace);
}

void qa_app_sim_register_mod_callbacks(const void *callbacks, size_t count, size_t stride, size_t binding_offset,
                                       qa_mod_registrations *registrations, qa_app_sim_mod_callback_host host) {
  for (size_t i = 0; i < count; i++) {
    const unsigned char *element = (const unsigned char *)callbacks + i * stride;
    entry base = {element, (const qa_mod_callback_binding *)(element + binding_offset), host};
    const char *operation = base.binding->operation;
    bool transform = strcmp(base.binding->stage, "transform") == 0;
    if (strcmp(operation, "damage") == 0) {
      entry *e = closure(registrations, sizeof *e);
      *e = base;
      if (transform)
        register_one(registrations, registrations->operations.damage, base.binding->id, QA_MOD_TRANSFORM, e,
                     damage_transform, NULL, NULL);
      else register_one(registrations, registrations->operations.damage, base.binding->id, QA_MOD_OBSERVE, e, NULL,
                        damage_observe, NULL);
    } else if (strcmp(operation, "inventory.give") == 0 || strcmp(operation, "inventory.consume") == 0) {
      bool give = strcmp(operation, "inventory.give") == 0;
      qa_mod_operation *target = give ? registrations->operations.inventory_give
                                      : registrations->operations.inventory_consume;
      entry *e = closure(registrations, sizeof *e);
      *e = base;
      if (transform)
        register_one(registrations, target, base.binding->id, QA_MOD_TRANSFORM, e, inventory_transform, NULL, NULL);
      else register_one(registrations, target, base.binding->id, QA_MOD_OBSERVE, e, NULL,
                        give ? give_observe : consume_observe, NULL);
    } else if (strcmp(operation, "actor.think") == 0) actors(registrations, &base, registrations->operations.actor_think, think_inputs);
    else if (strcmp(operation, "actor.touch") == 0) actors(registrations, &base, registrations->operations.actor_touch, touch_inputs);
    else if (strcmp(operation, "actor.use") == 0) actors(registrations, &base, registrations->operations.actor_use, use_inputs);
    else if (strcmp(operation, "actor.pain") == 0) actors(registrations, &base, registrations->operations.actor_pain, pain_inputs);
    else if (strcmp(operation, "actor.die") == 0) actors(registrations, &base, registrations->operations.actor_die, die_inputs);
  }
}
