/* Port of src/content/q1/quakec/id1-damage.ts. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/id1_damage.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/quakec/armor_stage.h"
#include "qa/content/q1/quakec/damage_call.h"
#include "qa/content/q1/quakec/damage_scale.h"
#include "qa/content/q1/quakec/id1_program.h"
#include "qa/foundation/support.h"
#include "qa/world/gameplay/armor.h"
#include "qa/world/gameplay/mod_composition.h"

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
static bool str_equal(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static noreturn void damage_error(const char *message) { qa_q1qc_error("progs.dat", "%s", message); }

typedef struct damage_frame {
  qa_damage_request request;
  int64_t target_reference;
  qa_source_damage_observer observer;
  qa_provider_id movement_provider;
  void *cancel_ctx;
  void (*cancel)(void *ctx, const int32_t return_words[3]);
  double regular_scale;
  qa_source_damage_result result;
  int reaction_depth;
  bool health_written;
} damage_frame;
typedef struct protection_owner {
  qa_owned_actor actor;
  qa_armor_stage_intercept_fn intercept;
  void *intercept_ctx;
} protection_owner;
typedef struct owned_alloc { void *ptr; struct owned_alloc *next; } owned_alloc;

struct qa_q1_id1_damage_binding {
  qa_q1qc_source source;
  qa_gameplay_authority *authority;
  qa_q1qc_machine_fn machine;
  void *machine_ctx;
  qa_q1_id1_resolve_request_fn resolve_request;
  void *resolve_ctx;
  qa_q1_id1_damage_projection projection;
  bool has_projection;
  const qa_q1_id1_program_binding *program;
  qa_q1qc_armor_stage *armor_stage;
  qa_q1qc_damage_scale *damage_scale;
  bool has_quad;
  qa_q1qc_inline_region quad_region;
  int64_t quad_result;
  int64_t health, velocity, armor_value, armor_type, items, pain, die;
  damage_frame *active;
  size_t active_count, active_capacity;
  protection_owner *regular; size_t regular_count, regular_capacity;
  protection_owner *powered; size_t powered_count, powered_capacity;
  int64_t *boundary_functions; size_t boundary_function_count;
  qa_q1qc_inline_region *boundary_regions; size_t boundary_region_count;
  qa_q1qc_function_boundary function_boundary;
  qa_q1qc_inline_boundary inline_boundary;
  owned_alloc *allocations;
};
static void *binding_alloc(qa_q1_id1_damage_binding *binding, size_t size) {
  void *ptr = xmalloc(size);
  owned_alloc *node = xmalloc(sizeof *node);
  node->ptr = ptr;
  node->next = binding->allocations;
  binding->allocations = node;
  return ptr;
}
static qa_q1qc_machine *binding_vm(qa_q1_id1_damage_binding *binding) {
  qa_q1qc_machine *vm = binding->machine(binding->machine_ctx);
  if (vm->program != binding->source.program || vm->entities != binding->source.entities)
    damage_error("id1 damage binding belongs to another machine");
  return vm;
}
static damage_frame *top_frame(qa_q1_id1_damage_binding *binding) {
  return binding->active_count == 0 ? NULL : &binding->active[binding->active_count - 1];
}
static void frame_cancel(damage_frame *frame) {
  static const int32_t ZEROS[3] = {0, 0, 0};
  frame->cancel(frame->cancel_ctx, ZEROS);
  /* TS cancel throws QcFunctionCancellation; the C contract (see
   * compat_types.h) is that cancel does not return. */
  qa_range_error("QC function execution cancellation returned");
}
static protection_owner *find_owner(protection_owner *owners, size_t count, qa_owned_actor actor) {
  for (size_t i = 0; i < count; i++)
    if (qa_q1qc_owned_equal(owners[i].actor, actor)) return &owners[i];
  return NULL;
}

typedef struct armor_original_ctx {
  qa_q1qc_machine *vm;
  const qa_mod_qc_armor_stage *plan;
  qa_q1qc_inline_continuation execute;
  bool *executed;
} armor_original_ctx;
static double armor_original_run(void *ctx) {
  armor_original_ctx *original = ctx;
  original->execute.run(original->execute.ctx);
  *original->executed = true;
  return qa_q1qc_words_float(&original->vm->globals, (int64_t)original->plan->saved);
}
static double armor_zero(void *ctx) { (void)ctx; return 0; }
static void run_armor(qa_q1_id1_damage_binding *binding, qa_q1qc_inline_continuation execute) {
  damage_frame *frame = top_frame(binding);
  qa_q1qc_armor_stage *stage = binding->armor_stage;
  if (frame == NULL || stage == NULL || frame->reaction_depth > 0) {
    execute.run(execute.ctx);
    return;
  }
  const qa_mod_qc_armor_stage *plan = &stage->source;
  /* Intercepts may push damage frames (reallocating the stack), so
   * cancel through a snapshot rather than the frame pointer. */
  damage_frame frame_snapshot = *frame;
  frame = &frame_snapshot;
  qa_q1qc_machine *vm = binding_vm(binding);
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_owned(binding->source.actors, frame->request.target, &actor))
    frame_cancel(frame);
  if (qa_q1qc_words_int(&vm->globals, (int64_t)plan->target) != frame->target_reference)
    damage_error("QC armor stage changed its damage target");
  protection_owner *power = find_owner(binding->powered, binding->powered_count, actor);
  protection_owner *regular = find_owner(binding->regular, binding->regular_count, actor);
  if (power == NULL && regular == NULL) {
    execute.run(execute.ctx);
    return;
  }
  qa_numeric_operations *numeric = &vm->numeric;
  int32_t word = plan->flags.is_bits
                     ? (int32_t)trunc(qa_q1qc_words_float(&vm->globals, (int64_t)plan->flags.word)) : 0;
  qa_attack_damage_flags captured = qa_attack_damage_flags_of(frame->request);
  qa_armor_damage_flags flags = captured.flags;
  double base_scale = flags.has_regular_protection_scale ? flags.regular_protection_scale : 1;
  flags.has_regular_protection_scale = true;
  flags.regular_protection_scale = numeric->multiply(numeric->ctx, base_scale, frame->regular_scale);
  if (plan->flags.is_bits) {
    if ((int64_t)plan->flags.no_armor != 0)
      flags.no_armor = (word & (int32_t)plan->flags.no_armor) != 0;
    if ((int64_t)plan->flags.no_power_armor != 0)
      flags.no_power_armor = (word & (int32_t)plan->flags.no_power_armor) != 0;
    if ((int64_t)plan->flags.no_regular_armor != 0)
      flags.no_regular_armor = (word & (int32_t)plan->flags.no_regular_armor) != 0;
    if ((int64_t)plan->flags.energy != 0)
      flags.energy = (word & (int32_t)plan->flags.energy) != 0;
  }
  qa_armor_stage_input input = {frame->request, frame->request.direction, frame->request.point,
                                frame->request.normal,
                                qa_q1qc_words_float(&vm->globals, (int64_t)plan->damage), flags};
  /* TS live(): owner identity may not change across intercepts. */
  double power_saved = power == NULL ? 0 : power->intercept(power->intercept_ctx, input, armor_zero, NULL);
  qa_owned_actor again;
  if (!qa_session_actor_registry_resolve_owned(binding->source.actors, actor.id, &again) ||
      !qa_q1qc_owned_equal(again, actor))
    frame_cancel(frame);
  if (!isfinite((double)(float)power_saved)) damage_error("Armor savings exceed QC binary32 range");
  int32_t original_bits = qa_q1qc_words_int(&vm->globals, (int64_t)plan->damage);
  double amount = numeric->subtract(numeric->ctx, input.amount, power_saved);
  qa_q1qc_words_set_float(&vm->globals, (int64_t)plan->damage, amount);
  bool executed = false;
  armor_original_ctx original = {vm, plan, execute, &executed};
  protection_owner *current = find_owner(binding->regular, binding->regular_count, actor);
  qa_armor_stage_input regular_input = input;
  regular_input.amount = amount;
  double regular_saved = current == NULL
                             ? armor_original_run(&original)
                             : current->intercept(current->intercept_ctx, regular_input,
                                                  armor_original_run, &original);
  /* TS restores the damage word in a finally around the writes below. */
  if (!qa_session_actor_registry_resolve_owned(binding->source.actors, actor.id, &again) ||
      !qa_q1qc_owned_equal(again, actor)) {
    qa_q1qc_words_set_int(&vm->globals, (int64_t)plan->damage, original_bits);
    frame_cancel(frame);
  }
  if (!isfinite((double)(float)regular_saved)) {
    qa_q1qc_words_set_int(&vm->globals, (int64_t)plan->damage, original_bits);
    damage_error("Armor savings exceed QC binary32 range");
  }
  if (!executed) execute.skip_to_join(execute.ctx);
  qa_q1qc_words_set_float(&vm->globals, (int64_t)plan->saved,
                          numeric->add(numeric->ctx, regular_saved, power_saved));
  qa_q1qc_words_set_int(&vm->globals, (int64_t)plan->damage, original_bits);
}

static void inline_run(void *ctx, qa_q1qc_inline_region region,
                       qa_q1qc_inline_continuation execute) {
  qa_q1_id1_damage_binding *binding = ctx;
  if (binding->damage_scale != NULL && region.entry == binding->damage_scale->region.entry) {
    damage_frame *frame = top_frame(binding);
    bool owner_matches = frame != NULL && frame->request.attack.has_damage_powerup_owner &&
                         str_equal(frame->request.attack.damage_powerup_owner,
                                   binding->source.provider);
    if (owner_matches) execute.skip_to_join(execute.ctx);
    else execute.run(execute.ctx);
    return;
  }
  if (!binding->has_quad || region.entry != binding->quad_region.entry) {
    run_armor(binding, execute);
    return;
  }
  damage_frame *frame = top_frame(binding);
  execute.run(execute.ctx);
  if (frame != NULL && frame->request.attack.has_damage_powerup_owner)
    qa_q1qc_words_set_float(&binding_vm(binding)->globals, binding->quad_result, 0);
}

static bool cause_equal(const qa_attack_cause *a, const qa_attack_cause *b) {
  return a->kind == b->kind && str_equal(a->death_type, b->death_type) &&
         str_equal(a->armor_effect, b->armor_effect) && a->means_of_death == b->means_of_death &&
         a->damage_flags == b->damage_flags && a->has_native == b->has_native &&
         a->native.value == b->native.value && a->native.id == b->native.id &&
         a->native.friendly_fire == b->native.friendly_fire &&
         a->native.no_point_loss == b->native.no_point_loss && str_equal(a->hazard, b->hazard);
}
static bool vec_equal(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
/* TS isDeepStrictEqual over the request with attacker/inflictor nulled. */
static bool metadata_equal(const qa_damage_request *a, const qa_damage_request *b) {
  if (a->knockback != b->knockback || !vec_equal(a->direction, b->direction) ||
      !vec_equal(a->point, b->point) || !vec_equal(a->normal, b->normal) ||
      !str_equal(a->delivery, b->delivery)) return false;
  const qa_attack_provenance *x = &a->attack, *y = &b->attack;
  return x->sequence == y->sequence && x->time.kind == y->time.kind &&
         x->time.value == y->time.value &&
         x->has_originating_projectile == y->has_originating_projectile &&
         (!x->has_originating_projectile ||
          qa_q1qc_actor_equal(x->originating_projectile, y->originating_projectile)) &&
         str_equal(x->weapon, y->weapon) && str_equal(x->weapon_provider, y->weapon_provider) &&
         x->has_damage_powerup_owner == y->has_damage_powerup_owner &&
         (!x->has_damage_powerup_owner ||
          str_equal(x->damage_powerup_owner, y->damage_powerup_owner)) &&
         str_equal(x->combat_provider, y->combat_provider) &&
         str_equal(x->inventory_provider, y->inventory_provider) &&
         str_equal(x->movement_provider, y->movement_provider) && cause_equal(&x->cause, &y->cause);
}
static int64_t reference_for(qa_q1_id1_damage_binding *binding, const qa_actor_id *actor) {
  if (binding->has_projection) return binding->projection.reference(binding->projection.ctx, actor);
  if (actor == NULL) return qa_q1qc_entities_reference(binding->source.entities, 0);
  qa_actor_source_result slot =
      qa_session_actor_registry_source_of(binding->source.actors, *actor);
  if (!slot.has_value || !str_equal(slot.provider, binding->source.provider))
    damage_error("QuakeC damage actor has no source projection");
  return qa_q1qc_entities_reference(binding->source.entities, slot.slot);
}

typedef struct source_damage_ctx {
  qa_q1_id1_damage_binding *binding;
  qa_q1qc_machine *vm;
  qa_q1qc_call_site call;
  qa_q1qc_damage_call_values source_call;
  qa_damage_request request;
  qa_q1qc_function_execution execute;
  bool *executed;
} source_damage_ctx;
static qa_source_damage_result run_source_damage_execute(void *ctx,
                                                         qa_source_damage_observer observer,
                                                         qa_damage_request effective) {
  source_damage_ctx *run = ctx;
  qa_q1_id1_damage_binding *binding = run->binding;
  qa_source_damage_result none = {0, "none"};
  if ((binding->has_projection && binding->projection.admit != NULL &&
       !binding->projection.admit(binding->projection.ctx, effective)) ||
      !qa_session_actor_registry_is_live(binding->source.actors, effective.target))
    return none;
  if (qa_mod_operation_active(qa_gameplay_authority_damage_operation(binding->authority)) &&
      !metadata_equal(&run->request, &effective))
    damage_error("QuakeC damage call accepts actor and amount changes; independent damage metadata requires a replacement");
  if (!isfinite((double)(float)effective.amount))
    damage_error("QuakeC damage amount exceeds binary32 range");
  int64_t target_reference = reference_for(binding, &effective.target);
  int64_t inflictor_reference = reference_for(binding, effective.attack.inflictor);
  int64_t attacker_reference = reference_for(binding, effective.attack.attacker);
  double regular_scale = 1;
  if (binding->armor_stage != NULL) {
    const qa_mod_qc_armor_stage *plan = &binding->armor_stage->source;
    for (size_t i = 0; i < plan->regular_scale_count; i++) {
      const qa_mod_qc_armor_regular_scale *site = &plan->regular_scale[i];
      if ((int64_t)site->statement == run->call.statement &&
          qa_q1qc_function_named(binding->source.program, site->caller)->index == run->call.caller)
        regular_scale = site->scale;
    }
  }
  if (binding->active_count == binding->active_capacity) {
    binding->active_capacity = binding->active_capacity == 0 ? 4 : binding->active_capacity * 2;
    binding->active = xrealloc(binding->active, binding->active_capacity * sizeof *binding->active);
  }
  damage_frame *frame = &binding->active[binding->active_count++];
  *frame = (damage_frame){effective, target_reference, observer,
                          effective.attack.movement_provider, run->execute.ctx,
                          run->execute.cancel, regular_scale, {0, "none"}, 0, false};
  *run->executed = true;
  qa_q1qc_damage_call_values projected = {(double)target_reference, (double)inflictor_reference,
                                          (double)attacker_reference, effective.amount};
  qa_q1qc_project_damage_call(run->vm, binding->program->damage_call, run->source_call, projected,
                              run->execute);
  frame = top_frame(binding);
  if (strcmp(binding->program->damage_kind, "calls") == 0 && frame->health_written &&
      strcmp(frame->result.reaction, "none") == 0 &&
      qa_session_actor_registry_is_live(binding->source.actors, effective.target)) {
    qa_q1qc_words target;
    qa_q1qc_entities_from_reference(binding->source.entities, target_reference, &target);
    if (qa_q1qc_words_float(&target, binding->health) <= 0) {
      frame->result.reaction = "death";
      frame->observer.before_reaction(frame->observer.ctx, frame->result);
    }
  }
  qa_source_damage_result result = frame->result;
  binding->active_count--;
  return result;
}
static qa_damage_outcome apply_source_damage(void *ctx, qa_damage_request composed) {
  source_damage_ctx *run = ctx;
  return qa_gameplay_authority_run_source_damage(run->binding->authority, composed,
                                                 run_source_damage_execute, run);
}
static void observe_native_function(qa_q1_id1_damage_binding *binding, qa_q1qc_call_site call,
                                    qa_q1qc_function_execution execute);
static void function_run(void *ctx, qa_q1qc_call_site call, qa_q1qc_function_execution execute) {
  qa_q1_id1_damage_binding *binding = ctx;
  const qa_q1_id1_program_binding *layout = binding->program;
  if (call.function_index != layout->damage_index) {
    observe_native_function(binding, call, execute);
    return;
  }
  qa_q1qc_machine *vm = binding_vm(binding);
  qa_q1qc_damage_call_values source_call = qa_q1qc_read_damage_call(vm, layout->damage_call);
  qa_actor_id ids[3];
  double refs[3] = {source_call.self, source_call.inflictor, source_call.attacker};
  for (int i = 0; i < 3; i++) {
    if (binding->has_projection) {
      ids[i] = binding->projection.actor(binding->projection.ctx, (int64_t)refs[i]);
    } else {
      int64_t slot = qa_q1qc_entities_slot(binding->source.entities, (int64_t)refs[i]);
      qa_owned_actor owned;
      if (!qa_source_actor_slots_at(binding->source.slots, slot, &owned) ||
          !qa_session_actor_registry_is_live(binding->source.actors, owned.id))
        damage_error("id1 damage references a free source actor");
      ids[i] = owned.id;
    }
  }
  qa_q1_id1_damage_call captured = {call, ids[0], ids[1], ids[2], source_call.amount};
  qa_damage_request request = binding->resolve_request(binding->resolve_ctx, captured);
  bool attacker_same = request.attack.attacker == NULL
                           ? source_call.attacker == 0
                           : qa_q1qc_actor_equal(*request.attack.attacker, captured.attacker);
  bool inflictor_same = request.attack.inflictor == NULL
                            ? source_call.inflictor == 0
                            : qa_q1qc_actor_equal(*request.attack.inflictor, captured.inflictor);
  if (!qa_q1qc_actor_equal(request.target, captured.target) ||
      (double)(float)request.amount != captured.amount || !attacker_same || !inflictor_same)
    damage_error("id1 damage provenance changed source arguments");
  bool executed = false;
  source_damage_ctx run = {binding, vm, call, source_call, request, execute, &executed};
  qa_damage_outcome outcome =
      qa_gameplay_authority_apply(binding->authority, request, apply_source_damage, &run);
  if (binding->has_projection && binding->projection.completed != NULL)
    binding->projection.completed(binding->projection.ctx, request, outcome);
  if (!executed) {
    static const int32_t ZEROS[3] = {0, 0, 0};
    execute.skip(execute.ctx, ZEROS);
  }
}
static void observe_native_function(qa_q1_id1_damage_binding *binding, qa_q1qc_call_site call,
                                    qa_q1qc_function_execution execute) {
  damage_frame *frame = top_frame(binding);
  const qa_q1_id1_program_binding *layout = binding->program;
  if (frame == NULL || frame->reaction_depth > 0) {
    qa_q1qc_execution_run(&execute);
    return;
  }
  if (strcmp(layout->damage_kind, "sites") == 0) {
    if (strcmp(frame->result.reaction, "none") == 0 || call.caller != layout->damage_index ||
        call.statement != (strcmp(frame->result.reaction, "death") == 0 ? layout->death[0]
                                                                         : layout->pain[0])) {
      qa_q1qc_execution_run(&execute);
      return;
    }
    frame->reaction_depth++;
    if (binding->has_projection && binding->projection.reaction != NULL)
      binding->projection.reaction(binding->projection.ctx, frame->request, frame->result, execute);
    else qa_q1qc_execution_run(&execute);
    frame = top_frame(binding);
    frame->reaction_depth--;
    return;
  }
  if (strcmp(frame->result.reaction, "none") != 0) {
    qa_q1qc_execution_run(&execute);
    return;
  }
  const char *reaction = NULL;
  for (size_t i = 0; i < layout->reaction_count; i++)
    if (layout->reactions[i].statement == call.statement) reaction = layout->reactions[i].kind;
  if (reaction == NULL) {
    qa_q1qc_execution_run(&execute);
    return;
  }
  qa_q1qc_machine *vm = binding_vm(binding);
  qa_q1qc_words target;
  qa_q1qc_entities_from_reference(binding->source.entities, frame->target_reference, &target);
  bool death = strcmp(reaction, "death") == 0;
  if (qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "self")) !=
          frame->target_reference ||
      call.function_index != qa_q1qc_words_int(&target, death ? binding->die : binding->pain)) {
    qa_q1qc_execution_run(&execute);
    return;
  }
  if ((qa_q1qc_words_float(&target, binding->health) <= 0) != death)
    damage_error("Native damage reaction disagrees with target health");
  if (!frame->health_written)
    damage_error("Native damage reaction precedes target health mutation");
  frame->result.reaction = reaction;
  frame->observer.before_reaction(frame->observer.ctx, frame->result);
  frame->reaction_depth++;
  if (binding->has_projection && binding->projection.reaction != NULL)
    binding->projection.reaction(binding->projection.ctx, frame->request, frame->result, execute);
  else qa_q1qc_execution_run(&execute);
  frame = top_frame(binding);
  frame->reaction_depth--;
}

static int64_t binding_field(const qa_q1qc_program *program, const char *name) {
  const qa_q1qc_definition *value = qa_q1qc_field_by_name(program, name);
  if (value == NULL) qa_q1qc_error("progs.dat", "missing id1 field %s", name);
  return value->offset;
}
qa_q1_id1_damage_binding *qa_q1_id1_damage_binding_create(
    qa_q1qc_source source, qa_gameplay_authority *authority,
    qa_q1qc_machine_fn machine, void *machine_ctx,
    qa_q1_id1_resolve_request_fn resolve_request, void *resolve_ctx,
    const qa_q1_id1_damage_projection *projection,
    const qa_mod_qc_armor_stage *declared_armor,
    const qa_mod_source_call *declared_scale_call,
    const qa_mod_qc_damage_scale *declared_scale) {
  qa_q1_id1_damage_binding *binding = xmalloc(sizeof *binding);
  memset(binding, 0, sizeof *binding);
  binding->source = source;
  binding->authority = authority;
  binding->machine = machine;
  binding->machine_ctx = machine_ctx;
  binding->resolve_request = resolve_request;
  binding->resolve_ctx = resolve_ctx;
  if (projection != NULL) {
    binding->has_projection = true;
    binding->projection = *projection;
  }
  const qa_q1qc_program *program = source.program;
  binding->program = qa_q1_id1_program_binding_get(program, NULL);
  binding->armor_stage = qa_q1qc_armor_stage_create(program, declared_armor);
  binding->damage_scale = declared_scale == NULL
                              ? NULL
                              : qa_q1qc_damage_scale_create(program, declared_scale_call, declared_scale);
  bool qw = program->digest != NULL &&
            strcmp(program->digest,
                   "sha256:ff51cb5e77360d72b93487d89198dcf94629b92f8bae100fc6ea48a6c12a7830") == 0;
  bool nq = program->digest != NULL &&
            strcmp(program->digest,
                   "sha256:f2619787f9aa0f057246eea1665b622b4691b5c5a800b1a46133d1fe8b771580") == 0;
  if (binding->damage_scale == NULL && qw) {
    binding->has_quad = true;
    binding->quad_region = (qa_q1qc_inline_region){83, 364, 369, false, false, 0, NULL};
    binding->quad_result = 875;
  } else if (binding->damage_scale == NULL && nq) {
    binding->has_quad = true;
    binding->quad_region = (qa_q1qc_inline_region){117, 1426, 1428, false, false, 0, NULL};
    binding->quad_result = 1593;
  }
  if (binding->has_quad) {
    static const qa_q1_id1_statement QW_PREDICATE[6] = {
      {364, QA_Q1QC_OP_LOAD_F, 857, 396, 870}, {365, QA_Q1QC_OP_GT, 870, 31, 871},
      {366, QA_Q1QC_OP_LOAD_S, 856, 124, 872}, {367, QA_Q1QC_OP_NE_S, 872, 873, 874},
      {368, QA_Q1QC_OP_AND, 871, 874, 875}, {369, QA_Q1QC_OP_IF_NOT, 875, 8, 0}};
    static const qa_q1_id1_statement NQ_PREDICATE[3] = {
      {1426, QA_Q1QC_OP_LOAD_F, 1582, 377, 1592}, {1427, QA_Q1QC_OP_GT, 1592, 31, 1593},
      {1428, QA_Q1QC_OP_IF_NOT, 1593, 3, 0}};
    const qa_q1_id1_statement *predicate = qw ? QW_PREDICATE : NQ_PREDICATE;
    size_t predicate_count = qw ? 6 : 3;
    for (size_t i = 0; i < predicate_count; i++) {
      int64_t index = predicate[i].index;
      const qa_q1qc_statement *actual =
          index >= 0 && (size_t)index < program->statement_count ? &program->statements[index] : NULL;
      if (actual == NULL || actual->opcode != predicate[i].opcode || actual->a != predicate[i].a ||
          actual->b != predicate[i].b || actual->c != predicate[i].c)
        damage_error("QC source Quad predicate differs from its original artifact");
    }
  }
  size_t region_capacity = 3;
  binding->boundary_regions = xmalloc(region_capacity * sizeof *binding->boundary_regions);
  if (binding->damage_scale != NULL)
    binding->boundary_regions[binding->boundary_region_count++] = binding->damage_scale->region;
  if (binding->has_quad)
    binding->boundary_regions[binding->boundary_region_count++] = binding->quad_region;
  if (binding->armor_stage != NULL)
    binding->boundary_regions[binding->boundary_region_count++] = binding->armor_stage->region;
  const qa_q1_id1_program_binding *layout = binding->program;
  const qa_q1qc_function *damage = qa_q1qc_function_at(program, layout->damage_index);
  bool sizes_ok = true;
  if (strcmp(layout->damage_kind, "sites") == 0) {
    if (damage->parameter_size_count != 4) sizes_ok = false;
    for (size_t i = 0; i < damage->parameter_size_count; i++)
      if (damage->parameter_sizes[i] != 1) sizes_ok = false;
  }
  if (damage->index != layout->damage_index ||
      damage->first_statement != layout->damage_first_statement ||
      damage->parameter_start != layout->damage_parameter_start ||
      damage->local_words != layout->damage_local_words || !sizes_ok)
    damage_error("id1 damage function layout mismatch");
  if (strcmp(layout->damage_kind, "sites") == 0) {
    for (size_t i = 0; i < layout->statement_count; i++) {
      const qa_q1_id1_statement *expected = &layout->statements[i];
      const qa_q1qc_statement *actual =
          expected->index >= 0 && (size_t)expected->index < program->statement_count
              ? &program->statements[expected->index] : NULL;
      if (actual == NULL || actual->opcode != expected->opcode || actual->a != expected->a ||
          actual->b != expected->b || actual->c != expected->c) {
        char message[128];
        (void)snprintf(message, sizeof message, "id1 damage statement %lld mismatch",
                       (long long)expected->index);
        damage_error(message);
      }
    }
  }
  binding->health = binding_field(program, "health");
  binding->velocity = binding_field(program, "velocity");
  binding->armor_value = binding_field(program, "armorvalue");
  binding->armor_type = binding_field(program, "armortype");
  binding->items = binding_field(program, layout->armor_field);
  binding->pain = binding_field(program, "th_pain");
  binding->die = binding_field(program, "th_die");
  bool sites_only = strcmp(layout->damage_kind, "sites") == 0 &&
                    !(binding->has_projection && binding->projection.reaction != NULL);
  binding->boundary_functions = xmalloc((program->function_count + 1) *
                                        sizeof *binding->boundary_functions);
  if (sites_only) {
    binding->boundary_functions[binding->boundary_function_count++] = layout->damage_index;
  } else {
    for (size_t i = 0; i < program->function_count; i++) {
      const qa_q1qc_function *fn = &program->functions[i];
      if (fn->index > 0 && fn->first_statement > 0 && !fn->named_builtin)
        binding->boundary_functions[binding->boundary_function_count++] = fn->index;
    }
  }
  binding->function_boundary = (qa_q1qc_function_boundary){binding, binding->boundary_functions,
                                                           binding->boundary_function_count,
                                                           function_run};
  binding->inline_boundary = (qa_q1qc_inline_boundary){binding, binding->boundary_regions,
                                                       binding->boundary_region_count, inline_run};
  return binding;
}
void qa_q1_id1_damage_binding_destroy(qa_q1_id1_damage_binding *binding) {
  if (binding == NULL) return;
  qa_q1qc_armor_stage_destroy(binding->armor_stage);
  qa_q1qc_damage_scale_destroy(binding->damage_scale);
  free(binding->active);
  free(binding->regular);
  free(binding->powered);
  free(binding->boundary_functions);
  free(binding->boundary_regions);
  owned_alloc *node = binding->allocations;
  while (node != NULL) {
    owned_alloc *next = node->next;
    free(node->ptr);
    free(node);
    node = next;
  }
  free(binding);
}
const qa_q1qc_function_boundary *qa_q1_id1_damage_function_boundary(
    const qa_q1_id1_damage_binding *binding) {
  return &binding->function_boundary;
}
const qa_q1qc_inline_boundary *qa_q1_id1_damage_inline_boundary(
    const qa_q1_id1_damage_binding *binding) {
  return &binding->inline_boundary;
}
bool qa_q1_id1_damage_multiplier_of(qa_q1_id1_damage_binding *binding, qa_actor_id actor,
                                    int64_t reference, double seconds, double *out) {
  if (binding->damage_scale == NULL) return false;
  *out = qa_q1qc_evaluate_damage_scale(binding_vm(binding), binding->damage_scale, actor,
                                       reference, seconds);
  return true;
}
bool qa_q1_id1_damage_amount_of(qa_q1_id1_damage_binding *binding, qa_actor_id actor,
                                int64_t reference, double seconds, double amount, double *out) {
  if (binding->damage_scale == NULL) return false;
  *out = qa_q1qc_evaluate_damage_amount(binding_vm(binding), binding->damage_scale, actor,
                                        reference, seconds, amount);
  return true;
}

typedef struct protection_bind_ctx {
  qa_q1_id1_damage_binding *binding;
  qa_owned_actor actor;
  qa_protection_channel channel;
  qa_armor_stage_intercept_fn intercept;
  void *intercept_ctx;
} protection_bind_ctx;
static void protection_remove(void *ctx) {
  protection_bind_ctx *bind = ctx;
  qa_q1_id1_damage_binding *binding = bind->binding;
  protection_owner **owners = bind->channel == QA_PROTECTION_REGULAR ? &binding->regular
                                                                     : &binding->powered;
  size_t *count = bind->channel == QA_PROTECTION_REGULAR ? &binding->regular_count
                                                         : &binding->powered_count;
  protection_owner *owner = find_owner(*owners, *count, bind->actor);
  if (owner != NULL && owner->intercept == bind->intercept &&
      owner->intercept_ctx == bind->intercept_ctx) {
    size_t at = (size_t)(owner - *owners);
    memmove(&(*owners)[at], &(*owners)[at + 1], (*count - at - 1) * sizeof **owners);
    (*count)--;
  }
}
static qa_armor_stage_removal protection_bind(void *ctx,
                                              qa_armor_stage_intercept_fn intercept,
                                              void *intercept_ctx) {
  protection_bind_ctx *stage = ctx;
  qa_q1_id1_damage_binding *binding = stage->binding;
  qa_session_actor_registry_assert_owned(binding->source.actors, stage->actor);
  protection_owner **owners = stage->channel == QA_PROTECTION_REGULAR ? &binding->regular
                                                                      : &binding->powered;
  size_t *count = stage->channel == QA_PROTECTION_REGULAR ? &binding->regular_count
                                                          : &binding->powered_count;
  size_t *capacity = stage->channel == QA_PROTECTION_REGULAR ? &binding->regular_capacity
                                                             : &binding->powered_capacity;
  if (find_owner(*owners, *count, stage->actor) != NULL)
    qa_q1qc_error("progs.dat", "QC %s armor stage already has an owner",
                  stage->channel == QA_PROTECTION_REGULAR ? "regular" : "powered");
  if (*count == *capacity) {
    *capacity = *capacity == 0 ? 4 : *capacity * 2;
    *owners = xrealloc(*owners, *capacity * sizeof **owners);
  }
  (*owners)[(*count)++] = (protection_owner){stage->actor, intercept, intercept_ctx};
  protection_bind_ctx *removal = binding_alloc(binding, sizeof *removal);
  *removal = (protection_bind_ctx){binding, stage->actor, stage->channel, intercept, intercept_ctx};
  qa_armor_stage_removal result = {removal, protection_remove};
  return result;
}
bool qa_q1_id1_damage_protection_stage(qa_q1_id1_damage_binding *binding,
                                       qa_owned_actor actor, qa_protection_channel channel,
                                       qa_source_armor_stage *out) {
  if (binding->armor_stage == NULL ||
      (channel == QA_PROTECTION_REGULAR && !binding->armor_stage->region.replaceable))
    return false;
  protection_bind_ctx *stage = binding_alloc(binding, sizeof *stage);
  *stage = (protection_bind_ctx){binding, actor, channel, NULL, NULL};
  out->ctx = stage;
  out->bind = protection_bind;
  return true;
}

qa_armor_state qa_q1_id1_damage_read_armor(const qa_q1_id1_damage_binding *binding,
                                          const qa_q1qc_words *words) {
  uint32_t items = (uint32_t)(int64_t)trunc(qa_q1qc_words_float(words, binding->items));
  uint32_t green = (uint32_t)(int64_t)binding->program->armor_masks[0];
  uint32_t yellow = (uint32_t)(int64_t)binding->program->armor_masks[1];
  uint32_t red = (uint32_t)(int64_t)binding->program->armor_masks[2];
  qa_item_id item = (items & red) != 0 ? "q1:item_armorInv"
                    : (items & yellow) != 0 ? "q1:item_armor2"
                    : (items & green) != 0 ? "q1:item_armor1" : NULL;
  qa_armor_state state = {0};
  state.powered.kind = "none";
  if (item == NULL) {
    state.regular.kind = QA_REGULAR_ARMOR_NONE;
  } else {
    state.regular.kind = QA_REGULAR_ARMOR_Q1;
    state.regular.points = qa_q1qc_words_float(words, binding->armor_value);
    state.regular.absorption = qa_q1qc_words_float(words, binding->armor_type);
    state.regular.item = item;
  }
  return state;
}
void qa_q1_id1_damage_observe_call(qa_q1_id1_damage_binding *binding, qa_q1qc_call_site call) {
  damage_frame *frame = top_frame(binding);
  const qa_q1_id1_program_binding *layout = binding->program;
  if (strcmp(layout->damage_kind, "calls") == 0) return;
  if (frame == NULL || call.caller != layout->damage_index ||
      (call.statement != layout->death[0] && call.statement != layout->pain[0])) return;
  qa_q1qc_machine *vm = binding_vm(binding);
  if (strcmp(layout->attribution, "native") == 0) {
    qa_q1qc_words target;
    qa_q1qc_entities_from_reference(binding->source.entities, frame->target_reference, &target);
    bool death = call.statement == layout->death[0];
    bool bad = death ? qa_q1qc_machine_arg_int(vm, 0) != frame->target_reference ||
                           qa_q1qc_words_float(&target, binding->health) > 0
                     : qa_q1qc_words_int(&vm->globals,
                                         qa_q1qc_machine_global_offset(vm, "self")) !=
                               frame->target_reference ||
                           qa_q1qc_words_float(&target, binding->health) <= 0 ||
                           qa_q1qc_machine_arg_float(vm, 1) != frame->result.applied_damage;
    if (bad) damage_error("Unsupported native damage reaction context");
  }
  int64_t expected = qa_q1qc_words_int(
      &vm->globals, call.statement == layout->death[0] ? layout->death[1] : layout->pain[1]);
  if (call.function_index != expected) return;
  qa_source_damage_result result = {
      strcmp(layout->attribution, "native") == 0
          ? frame->result.applied_damage
          : qa_q1qc_words_float(&vm->globals, layout->take),
      call.statement == layout->death[0] ? "death" : "pain"};
  frame->result = result;
  frame->observer.before_reaction(frame->observer.ctx, result);
}
static float read_le_float(const uint8_t *bytes, size_t offset) {
  uint32_t bits = (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) |
                  ((uint32_t)bytes[offset + 2] << 16) | ((uint32_t)bytes[offset + 3] << 24);
  float value;
  memcpy(&value, &bits, sizeof value);
  return value;
}
void qa_q1_id1_damage_observe_entity_store(qa_q1_id1_damage_binding *binding,
                                           qa_q1qc_entity_store_observation store) {
  damage_frame *frame = top_frame(binding);
  if (frame == NULL) return;
  const qa_q1_id1_program_binding *layout = binding->program;
  bool native = strcmp(layout->damage_kind, "calls") == 0;
  int64_t combat_words[5] = {binding->health, binding->velocity, binding->armor_value,
                             binding->armor_type, binding->items};
  bool combat_store = false;
  for (int i = 0; i < 5; i++) if (combat_words[i] == store.word) combat_store = true;
  if (native && frame->reaction_depth > 0) return;
  if (native && combat_store && store.reference != frame->target_reference)
    damage_error("Unsupported native damage redirects a combat store to another actor");
  if (store.reference != frame->target_reference) return;
  if (native && combat_store && strcmp(frame->result.reaction, "none") != 0)
    damage_error("Native damage combat store follows its reaction continuation");
  if (!native && store.function_index != layout->damage_index) return;
  qa_q1qc_machine *vm = binding_vm(binding);
  (void)vm;
  if (store.word == binding->health) {
    if (strcmp(layout->damage_kind, "sites") == 0 && store.statement != layout->health_store)
      damage_error("Unsupported native damage health store");
    double before = (double)read_le_float(store.before, 0);
    double after = (double)read_le_float(store.after, 0);
    qa_damage_mutation mutation = {0};
    mutation.kind = QA_DAMAGE_MUTATION_HEALTH;
    mutation.health_before = before;
    mutation.health_after = after;
    frame->observer.stored(frame->observer.ctx, mutation);
    frame->health_written = true;
    frame->result = (qa_source_damage_result){
        strcmp(layout->damage_kind, "calls") == 0
            ? frame->result.applied_damage + before - after
            : qa_q1qc_words_float(&vm->globals, layout->take),
        "none"};
  } else if (store.word == binding->armor_value || store.word == binding->armor_type ||
             store.word == binding->items) {
    qa_q1qc_words current;
    qa_q1qc_entities_from_reference(binding->source.entities, store.reference, &current);
    size_t length = current.byte_length;
    uint8_t *copy = xmalloc(length);
    memcpy(copy, current.bytes, length);
    size_t offset = (size_t)store.word * 4;
    size_t copied = store.before_length;
    if (offset + copied > length) copied = offset < length ? length - offset : 0;
    if (copied != 0) memcpy(copy + offset, store.before, copied);
    qa_q1qc_words previous = {copy, length};
    qa_damage_mutation mutation = {0};
    mutation.kind = QA_DAMAGE_MUTATION_ARMOR;
    mutation.armor_before = qa_q1_id1_damage_read_armor(binding, &previous);
    mutation.armor_after = qa_q1_id1_damage_read_armor(binding, &current);
    free(copy);
    frame->observer.stored(frame->observer.ctx, mutation);
  } else if (store.word == binding->velocity) {
    qa_damage_mutation mutation = {0};
    mutation.kind = QA_DAMAGE_MUTATION_SOURCE_VELOCITY;
    mutation.movement_provider = frame->movement_provider;
    mutation.velocity_before = (qa_vec3){(double)read_le_float(store.before, 0),
                                          (double)read_le_float(store.before, 4),
                                          (double)read_le_float(store.before, 8)};
    mutation.velocity_after = (qa_vec3){(double)read_le_float(store.after, 0),
                                         (double)read_le_float(store.after, 4),
                                         (double)read_le_float(store.after, 8)};
    frame->observer.stored(frame->observer.ctx, mutation);
  }
}
