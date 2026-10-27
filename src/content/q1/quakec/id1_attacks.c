/* Port of src/content/q1/quakec/id1-attacks.ts. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/id1_attacks.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/quakec/id1_program.h"
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
static noreturn void attacks_error(const char *message) { qa_q1qc_error("progs.dat", "%s", message); }

typedef struct trace_pair { qa_actor_id actor; qa_vec3 point, normal; } trace_pair;
typedef struct attack_frame {
  qa_actor_id actor;
  int64_t reference;
  qa_item_id weapon;
  double time;
  trace_pair *traces; size_t trace_count, trace_capacity;
} attack_frame;
struct qa_q1_id1_sync_attacks {
  qa_q1qc_source source;
  qa_q1qc_machine_fn machine;
  void *machine_ctx;
  const qa_q1_id1_program_binding *binding;
  attack_frame *active; size_t active_count, active_capacity;
};
qa_q1_id1_sync_attacks *qa_q1_id1_sync_attacks_create(qa_q1qc_source source,
                                                      qa_q1qc_machine_fn machine,
                                                      void *machine_ctx) {
  qa_q1_id1_sync_attacks *attacks = xmalloc(sizeof *attacks);
  memset(attacks, 0, sizeof *attacks);
  attacks->source = source;
  attacks->machine = machine;
  attacks->machine_ctx = machine_ctx;
  attacks->binding = qa_q1_id1_program_binding_get(source.program, NULL);
  return attacks;
}
void qa_q1_id1_sync_attacks_destroy(qa_q1_id1_sync_attacks *attacks) {
  if (attacks == NULL) return;
  for (size_t i = 0; i < attacks->active_count; i++) free(attacks->active[i].traces);
  free(attacks->active);
  free(attacks);
}
void qa_q1_id1_sync_attacks_assert_idle(const qa_q1_id1_sync_attacks *attacks) {
  if (attacks->active_count != 0) attacks_error("Cannot save during a synchronous attack");
}
static qa_q1qc_machine *attacks_vm(qa_q1_id1_sync_attacks *attacks) {
  qa_q1qc_machine *vm = attacks->machine(attacks->machine_ctx);
  if (vm->program != attacks->source.program || vm->entities != attacks->source.entities)
    attacks_error("id1 attacks belong to another machine");
  return vm;
}
static int64_t attacks_field(qa_q1_id1_sync_attacks *attacks, const char *name) {
  const qa_q1qc_definition *field = qa_q1qc_field_by_name(attacks->source.program, name);
  if (field == NULL) qa_q1qc_error("progs.dat", "Missing id1 attack field %s", name);
  return field->offset;
}
static void read_trace(qa_q1qc_machine *vm, qa_vec3 *point, qa_vec3 *normal) {
  *point = qa_q1qc_words_vector(&vm->globals, qa_q1qc_machine_global_offset(vm, "trace_endpos"));
  *normal = qa_q1qc_words_vector(&vm->globals,
                                 qa_q1qc_machine_global_offset(vm, "trace_plane_normal"));
}
static bool slot_actor(qa_q1_id1_sync_attacks *attacks, int64_t reference, qa_owned_actor *out) {
  int64_t slot = qa_q1qc_entities_slot(attacks->source.entities, reference);
  return qa_source_actor_slots_at(attacks->source.slots, slot, out);
}
static void composed_run(void *ctx, qa_q1qc_call_site call, qa_q1qc_function_execution execute) {
  qa_q1_id1_sync_composed *composed = ctx;
  qa_q1_id1_sync_attacks *attacks = composed->owner;
  const qa_q1_id1_attacks *layout = attacks->binding->attacks;
  if (qa_q1qc_boundary_has(&composed->inner, call.function_index)) {
    composed->inner.run(composed->inner.ctx, call, execute);
    return;
  }
  qa_q1qc_machine *vm = attacks_vm(attacks);
  if (call.function_index == layout->add_multi) {
    if (attacks->active_count != 0 && call.caller == layout->trace_attack) {
      attack_frame *attack = &attacks->active[attacks->active_count - 1];
      qa_owned_actor target;
      if (slot_actor(attacks, qa_q1qc_machine_arg_int(vm, 0), &target)) {
        qa_vec3 point, normal;
        read_trace(vm, &point, &normal);
        trace_pair *existing = NULL;
        for (size_t i = 0; i < attack->trace_count; i++)
          if (qa_q1qc_actor_equal(attack->traces[i].actor, target.id)) existing = &attack->traces[i];
        if (existing == NULL) {
          if (attack->trace_count == attack->trace_capacity) {
            attack->trace_capacity = attack->trace_capacity == 0 ? 4 : attack->trace_capacity * 2;
            attack->traces = xrealloc(attack->traces, attack->trace_capacity * sizeof *attack->traces);
          }
          existing = &attack->traces[attack->trace_count++];
          existing->actor = target.id;
        }
        existing->point = point;
        existing->normal = normal;
      }
    }
    qa_q1qc_execution_run(&execute);
    return;
  }
  int64_t reference = qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "self"));
  qa_owned_actor actor;
  if (!slot_actor(attacks, reference, &actor) ||
      !qa_session_actor_registry_is_live(attacks->source.actors, actor.id))
    attacks_error("id1 attack has no live source actor");
  qa_q1qc_words words;
  qa_q1qc_entities_from_reference(attacks->source.entities, reference, &words);
  char classname[128];
  qa_q1qc_machine_string(vm, qa_q1qc_words_int(&words, attacks_field(attacks, "classname")),
                         classname, sizeof classname);
  if (strcmp(classname, "player") != 0)
    attacks_error("id1 player attack requires a source player");
  qa_item_id weapon = call.function_index == layout->axe ? "q1:weapon/axe"
                      : call.function_index == layout->shotgun ? "q1:weapon/shotgun"
                                                                : "q1:weapon/supershotgun";
  if (attacks->active_count == attacks->active_capacity) {
    attacks->active_capacity = attacks->active_capacity == 0 ? 4 : attacks->active_capacity * 2;
    attacks->active = xrealloc(attacks->active, attacks->active_capacity * sizeof *attacks->active);
  }
  attack_frame *frame = &attacks->active[attacks->active_count++];
  *frame = (attack_frame){actor.id, reference, weapon,
                          qa_q1qc_words_float(&vm->globals,
                                              qa_q1qc_machine_global_offset(vm, "time")),
                          NULL, 0, 0};
  qa_q1qc_execution_run(&execute);
  /* TS pops in a finally. */
  attacks->active_count--;
  free(attacks->active[attacks->active_count].traces);
  attacks->active[attacks->active_count].traces = NULL;
  attacks->active[attacks->active_count].trace_count = 0;
  attacks->active[attacks->active_count].trace_capacity = 0;
}
void qa_q1_id1_sync_attacks_compose(qa_q1_id1_sync_composed *composed,
                                    qa_q1_id1_sync_attacks *attacks,
                                    qa_q1qc_function_boundary damage) {
  memset(composed, 0, sizeof *composed);
  composed->owner = attacks;
  composed->inner = damage;
  const qa_q1_id1_attacks *layout = attacks->binding->attacks;
  if (layout == NULL) {
    composed->boundary = damage;
    return;
  }
  composed->functions = xmalloc((damage.function_count + 4) * sizeof *composed->functions);
  for (size_t i = 0; i < damage.function_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < composed->function_count; j++)
      if (composed->functions[j] == damage.functions[i]) seen = true;
    if (!seen) composed->functions[composed->function_count++] = damage.functions[i];
  }
  int64_t extra[4] = {layout->axe, layout->shotgun, layout->super_shotgun, layout->add_multi};
  for (int i = 0; i < 4; i++) {
    bool seen = false;
    for (size_t j = 0; j < composed->function_count; j++)
      if (composed->functions[j] == extra[i]) seen = true;
    if (!seen) composed->functions[composed->function_count++] = extra[i];
  }
  composed->boundary = (qa_q1qc_function_boundary){composed, composed->functions,
                                                   composed->function_count, composed_run};
}
void qa_q1_id1_sync_composed_destroy(qa_q1_id1_sync_composed *composed) {
  free(composed->functions);
  memset(composed, 0, sizeof *composed);
}
bool qa_q1_id1_sync_attacks_resolve(qa_q1_id1_sync_attacks *attacks,
                                    qa_q1_id1_damage_call call,
                                    qa_q1_id1_synchronous_attack *out) {
  const qa_q1_id1_attacks *layout = attacks->binding->attacks;
  if (layout == NULL) return false;
  bool axe = false;
  if (call.call.caller == layout->axe)
    for (size_t i = 0; i < layout->axe_damage_count; i++)
      if (layout->axe_damage[i] == call.call.statement) axe = true;
  bool shotgun = call.call.caller == layout->apply_multi_damage[0] &&
                 call.call.statement == layout->apply_multi_damage[1];
  if (!axe && !shotgun) return false;
  qa_q1qc_machine *vm = attacks_vm(attacks);
  attack_frame *attack = attacks->active_count == 0
                             ? NULL : &attacks->active[attacks->active_count - 1];
  if (attack == NULL || !qa_session_actor_registry_is_live(attacks->source.actors, attack->actor) ||
      !qa_q1qc_actor_equal(attack->actor, call.attacker) ||
      !qa_q1qc_actor_equal(attack->actor, call.inflictor) ||
      qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "self")) != attack->reference ||
      qa_q1qc_words_int(&vm->globals, attacks->binding->damage_global) !=
          attacks->binding->damage_index ||
      axe != (strcmp(attack->weapon, "q1:weapon/axe") == 0))
    attacks_error("Unmatched id1 synchronous damage scope");
  qa_actor_source_result target =
      qa_session_actor_registry_source_of(attacks->source.actors, call.target);
  if (!target.has_value || target.provider == NULL ||
      strcmp(target.provider, attacks->source.provider) != 0)
    attacks_error("id1 attack target has no source projection");
  qa_vec3 point, normal;
  if (axe) {
    read_trace(vm, &point, &normal);
  } else {
    trace_pair *found = NULL;
    for (size_t i = 0; i < attack->trace_count; i++)
      if (qa_q1qc_actor_equal(attack->traces[i].actor, call.target)) found = &attack->traces[i];
    if (found == NULL) attacks_error("id1 shotgun damage has no captured target trace");
    point = found->point;
    normal = found->normal;
  }
  qa_numeric_operations *n = &vm->numeric;
  qa_q1qc_words owner, victim;
  qa_q1qc_entities_from_reference(attacks->source.entities, attack->reference, &owner);
  qa_q1qc_entities_at(attacks->source.entities, target.slot, &victim);
  qa_vec3 min = qa_q1qc_words_vector(&owner, attacks_field(attacks, "absmin"));
  qa_vec3 max = qa_q1qc_words_vector(&owner, attacks_field(attacks, "absmax"));
  qa_vec3 origin = qa_q1qc_words_vector(&victim, attacks_field(attacks, "origin"));
  qa_vec3 delta = {
    n->subtract(n->ctx, origin.x, n->multiply(n->ctx, n->add(n->ctx, min.x, max.x), 0.5)),
    n->subtract(n->ctx, origin.y, n->multiply(n->ctx, n->add(n->ctx, min.y, max.y), 0.5)),
    n->subtract(n->ctx, origin.z, n->multiply(n->ctx, n->add(n->ctx, min.z, max.z), 0.5))};
  double magnitude = n->square_root(
      n->ctx, n->add(n->ctx,
                     n->add(n->ctx, n->multiply(n->ctx, delta.x, delta.x),
                            n->multiply(n->ctx, delta.y, delta.y)),
                     n->multiply(n->ctx, delta.z, delta.z)));
  double inverse = magnitude == 0 ? 0 : n->divide(n->ctx, 1, magnitude);
  out->actor = attack->actor;
  out->weapon = attack->weapon;
  out->time = attack->time;
  out->knockback = n->multiply(n->ctx, call.amount,
                               qa_q1_id1_damage_multiplier(vm, attack->reference, attack->reference));
  out->direction = (qa_vec3){n->multiply(n->ctx, delta.x, inverse),
                             n->multiply(n->ctx, delta.y, inverse),
                             n->multiply(n->ctx, delta.z, inverse)};
  out->point = point;
  out->normal = normal;
  return true;
}
