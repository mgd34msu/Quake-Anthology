/* Port of src/content/q1/quakec/id1-environment.ts. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/id1_environment.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/quakec/id1_program.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *value = malloc(size != 0 ? size : 1);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
static noreturn void env_error(const char *message) { qa_q1qc_error("progs.dat", "%s", message); }

struct qa_q1_id1_environment {
  qa_q1qc_source source;
  qa_q1qc_foreign_source foreign;
  qa_q1qc_machine_fn machine;
  void *machine_ctx;
  const qa_q1_id1_program_binding *binding;
};
qa_q1_id1_environment *qa_q1_id1_environment_create(qa_q1qc_source source,
                                                    qa_q1qc_machine_fn machine,
                                                    void *machine_ctx) {
  return qa_q1_id1_environment_create_foreign(source, NULL, machine, machine_ctx);
}
qa_q1_id1_environment *qa_q1_id1_environment_create_foreign(qa_q1qc_source source,
                                                            const qa_q1qc_foreign_source *foreign,
                                                            qa_q1qc_machine_fn machine,
                                                            void *machine_ctx) {
  qa_q1_id1_environment *environment = xmalloc(sizeof *environment);
  memset(environment, 0, sizeof *environment);
  environment->source = source;
  if (foreign != NULL) environment->foreign = *foreign;
  environment->machine = machine;
  environment->machine_ctx = machine_ctx;
  environment->binding = qa_q1_id1_program_binding_get(source.program, NULL);
  for (size_t i = 0; i < environment->binding->environment_count; i++) {
    const qa_q1_environmental_site *site = &environment->binding->environment[i];
    const qa_q1qc_statement *statement =
        site->statement >= 0 && (size_t)site->statement < source.program->statement_count
            ? &source.program->statements[site->statement] : NULL;
    const qa_q1qc_function *caller = qa_q1qc_function_at(source.program, site->caller);
    if (strcmp(caller->name, site->name) != 0 || statement == NULL ||
        statement->opcode != QA_Q1QC_OP_CALL4 ||
        statement->a != environment->binding->damage_global || statement->b != 0 ||
        statement->c != 0) {
      char message[128];
      (void)snprintf(message, sizeof message, "id1 environmental statement %lld mismatch",
                     (long long)site->statement);
      env_error(message);
    }
  }
  return environment;
}
void qa_q1_id1_environment_destroy(qa_q1_id1_environment *environment) { free(environment); }
static int64_t env_field(qa_q1_id1_environment *environment, const char *name) {
  const qa_q1qc_definition *value = qa_q1qc_field_by_name(environment->source.program, name);
  if (value == NULL) qa_q1qc_error("progs.dat", "Missing environmental field %s", name);
  return value->offset;
}
static int64_t env_reference(qa_q1_id1_environment *environment, qa_actor_id actor) {
  qa_actor_source_result owned =
      qa_session_actor_registry_source_of(environment->source.actors, actor);
  if (!qa_session_actor_registry_is_live(environment->source.actors, actor))
    env_error("id1 environment references a stale actor");
  if (owned.has_value && owned.provider != NULL && strcmp(owned.provider, environment->source.provider) == 0)
    return qa_q1qc_entities_reference(environment->source.entities, owned.slot);
  if (environment->foreign.foreign_reference == NULL)
    env_error("id1 damage references an actor without a source projection");
  return environment->foreign.foreign_reference(environment->foreign.ctx, actor);
}
static void entity_text(qa_q1_id1_environment *environment, qa_q1qc_machine *vm, int64_t entity,
                        const char *name, char *buf, size_t size) {
  qa_q1qc_words words;
  qa_q1qc_entities_from_reference(environment->source.entities, entity, &words);
  qa_q1qc_machine_string(vm, qa_q1qc_words_int(&words, env_field(environment, name)), buf, size);
}
bool qa_q1_id1_environment_resolve(qa_q1_id1_environment *environment,
                                   qa_q1_id1_damage_call call,
                                   const qa_q1_id1_physics_callback *callback,
                                   qa_q1_id1_environmental_damage *out) {
  const qa_q1_environmental_site *site = NULL;
  for (size_t i = 0; i < environment->binding->environment_count; i++) {
    const qa_q1_environmental_site *candidate = &environment->binding->environment[i];
    if (candidate->caller == call.call.caller && candidate->statement == call.call.statement) {
      site = candidate; /* TS find(): the first match */
      break;
    }
  }
  if (site == NULL) return false;
  qa_q1qc_machine *vm = environment->machine(environment->machine_ctx);
  if (vm->program != environment->source.program || vm->entities != environment->source.entities ||
      qa_q1qc_words_int(&vm->globals, environment->binding->damage_global) !=
          environment->binding->damage_index ||
      call.call.function_index != environment->binding->damage_index)
    env_error("Unmatched id1 environmental machine or function");
  int64_t target = env_reference(environment, call.target);
  int64_t inflictor = env_reference(environment, call.inflictor);
  int64_t attacker = env_reference(environment, call.attacker);
  int64_t self = qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "self"));
  int64_t other = qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "other"));
  /* TS reads the inflictor row only for a goalentity attacker. */
  int64_t expected_attacker = inflictor;
  if (site->attacker != NULL && strcmp(site->attacker, "goalentity") == 0) {
    qa_q1qc_words inflictor_words;
    qa_q1qc_entities_from_reference(environment->source.entities, inflictor, &inflictor_words);
    expected_attacker = qa_q1qc_words_int(&inflictor_words, env_field(environment, "goalentity"));
  }
  memset(out, 0, sizeof *out);
  out->cause.kind = QA_ATTACK_CAUSE_ENVIRONMENT;
  out->cause.hazard = site->hazard;
  if (site->native != NULL) {
    if (qa_q1qc_machine_arg_int(vm, 0) != target || qa_q1qc_machine_arg_int(vm, 1) != inflictor ||
        qa_q1qc_machine_arg_int(vm, 2) != attacker || qa_q1qc_machine_arg_float(vm, 3) != call.amount)
      env_error("Native map damage arguments changed");
    if (strcmp(site->native, "barrel") == 0) {
      char classname[128];
      entity_text(environment, vm, inflictor, "classname", classname, sizeof classname);
      if (strcmp(classname, "explo_box") != 0) return false;
      qa_q1qc_words words;
      qa_q1qc_entities_from_reference(environment->source.entities, inflictor, &words);
      if (self != inflictor || attacker != inflictor ||
          qa_q1qc_words_int(&words, env_field(environment, "th_die")) !=
              qa_q1qc_function_named(environment->source.program, "barrel_explode")->index)
        env_error("Unmatched native barrel damage");
    } else {
      if (callback == NULL || strcmp(callback->kind, "touch") != 0 ||
          callback->function_index != site->caller ||
          !qa_q1qc_actor_equal(callback->actor, call.inflictor) ||
          env_reference(environment, callback->other) != other || self != inflictor)
        env_error("Unmatched native map touch");
      qa_q1qc_words words;
      qa_q1qc_entities_from_reference(environment->source.entities, inflictor, &words);
      int64_t owner = qa_q1qc_words_int(&words, env_field(environment, "owner"));
      if (strcmp(site->native, "teledeath") == 0) {
        char classname[128];
        entity_text(environment, vm, inflictor, "classname", classname, sizeof classname);
        const char *expected_class = site->statement == 9808 || site->statement == 9817
                                         ? "teledeath3"
                                     : site->statement == 9828 ? "teledeath2" : classname;
        const qa_q1qc_function *caller =
            qa_q1qc_function_at(environment->source.program, site->caller);
        bool victim_matches =
            site->statement == 9828
                ? target == owner
                : site->statement == 9817
                      ? owner == other &&
                            target == qa_q1qc_words_int(&vm->globals, caller->parameter_start) &&
                            target != other && target != inflictor
                      : target == other;
        bool listed = strcmp(classname, "teledeath") == 0 || strcmp(classname, "teledeath2") == 0 ||
                      strcmp(classname, "teledeath3") == 0;
        if (attacker != inflictor || call.amount != 50000 || !victim_matches ||
            strcmp(classname, expected_class) != 0 || !listed)
          env_error("Unmatched native teledeath branch");
        out->cause.kind = QA_ATTACK_CAUSE_Q1;
        (void)snprintf(out->death_type, sizeof out->death_type, "%s", classname);
        out->cause.death_type = out->death_type;
      } else {
        if (target != other) env_error("Unmatched native map victim");
        if (strcmp(site->native, "spike") == 0 || strcmp(site->native, "laser") == 0) {
          char owner_class[128];
          entity_text(environment, vm, owner, "classname", owner_class, sizeof owner_class);
          if (strcmp(owner_class, "trap_spikeshooter") != 0 &&
              strcmp(owner_class, "trap_shooter") != 0) return false;
          double expected = strcmp(site->native, "laser") == 0 ? 15
                            : site->statement == 3900 ? 9 : 18;
          if (attacker != owner || call.amount != expected)
            env_error("Unmatched native trap owner or damage");
        } else if (attacker != inflictor) {
          env_error("Unmatched native map attacker");
        }
        if (strcmp(site->native, "fireball") == 0) {
          char classname[128];
          entity_text(environment, vm, inflictor, "classname", classname, sizeof classname);
          if (strcmp(classname, "fireball") != 0 || call.amount != 20)
            env_error("Unmatched native fireball");
        }
        if (strcmp(site->native, "exit") == 0) {
          char classname[128];
          entity_text(environment, vm, inflictor, "classname", classname, sizeof classname);
          if (strcmp(classname, "trigger_changelevel") != 0 || call.amount != 50000)
            env_error("Unmatched native exit punishment");
        }
        out->cause.kind = QA_ATTACK_CAUSE_Q1;
        entity_text(environment, vm, target, "deathtype", out->death_type, sizeof out->death_type);
        out->cause.death_type = out->death_type;
      }
    }
    if (strcmp(site->native, "barrel") == 0) {
      out->cause.kind = QA_ATTACK_CAUSE_Q1;
      entity_text(environment, vm, target, "deathtype", out->death_type, sizeof out->death_type);
      out->cause.death_type = out->death_type;
    }
  } else if (strcmp(site->context, "world") == 0) {
    if (target != self || inflictor != 0 || attacker != 0 ||
        qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "world")) != 0)
      env_error("Unmatched id1 world hazard arguments");
  } else if (callback == NULL || strcmp(callback->kind, site->context) != 0 ||
             callback->function_index != site->caller ||
             !qa_q1qc_actor_equal(call.target, callback->other) ||
             !qa_q1qc_actor_equal(call.inflictor, callback->actor) || self != inflictor ||
             other != target || attacker != expected_attacker) {
    env_error("Unmatched id1 environmental callback");
  }
  qa_q1qc_words victim;
  qa_q1qc_entities_from_reference(environment->source.entities, target, &victim);
  out->point = qa_q1qc_words_vector(&victim, env_field(environment, "origin"));
  out->direction = (qa_vec3){0, 0, 0};
  out->knockback = 0;
  out->time = qa_q1qc_words_float(&vm->globals, qa_q1qc_machine_global_offset(vm, "time"));
  if (inflictor != 0 && qa_q1qc_words_float(&victim, env_field(environment, "movetype")) == 3) {
    qa_q1qc_words origin_words;
    qa_q1qc_entities_from_reference(environment->source.entities, inflictor, &origin_words);
    qa_mutable_vector_math math;
    qa_create_mutable_vector_math(&math, &vm->numeric, QA_ZERO_LENGTH_PRESERVE);
    qa_vec3 direction = {0, 0, 0};
    qa_vec3 absmin = qa_q1qc_words_vector(&origin_words, env_field(environment, "absmin"));
    qa_vec3 absmax = qa_q1qc_words_vector(&origin_words, env_field(environment, "absmax"));
    qa_vec3 point_copy = out->point;
    /* Each QC vector opcode and the normalize builtin return through
     * float32 VM words (TS store() after every operation). */
    qa_mvm_vector_add(&math, qa_vec3_access_plain(&absmin), qa_vec3_access_plain(&absmax),
                      qa_vec3_access_plain(&direction));
    direction.x = (double)(float)direction.x; direction.y = (double)(float)direction.y;
    direction.z = (double)(float)direction.z;
    qa_mvm_vector_scale(&math, qa_vec3_access_plain(&direction), 0.5,
                        qa_vec3_access_plain(&direction));
    direction.x = (double)(float)direction.x; direction.y = (double)(float)direction.y;
    direction.z = (double)(float)direction.z;
    qa_mvm_vector_subtract(&math, qa_vec3_access_plain(&point_copy),
                           qa_vec3_access_plain(&direction), qa_vec3_access_plain(&direction));
    direction.x = (double)(float)direction.x; direction.y = (double)(float)direction.y;
    direction.z = (double)(float)direction.z;
    (void)qa_mvm_vector_normalize(&math, qa_vec3_access_plain(&direction));
    direction.x = (double)(float)direction.x; direction.y = (double)(float)direction.y;
    direction.z = (double)(float)direction.z;
    out->direction = direction;
    out->knockback = vm->numeric.multiply(
        vm->numeric.ctx, call.amount, qa_q1_id1_damage_multiplier(vm, attacker, inflictor));
  }
  return true;
}
