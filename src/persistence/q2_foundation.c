/* Port of src/persistence/q2-foundation.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_foundation.h"

#include <stdlib.h>
#include <string.h>

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
/* TS readSavedActor (save-image.ts, not yet ported as a module):
 * {slot: integer(0), generation: integer(0)}. Local copy only. */
static qa_saved_actor_id read_actor(const qa_save_reader *reader) {
  qa_saved_actor_id actor;
  qa_save_reader slot = field(reader, "slot");
  actor.slot = qa_save_reader_integer_min(&slot, 0);
  qa_save_reader_destroy(&slot);
  qa_save_reader generation = field(reader, "generation");
  actor.generation = qa_save_reader_integer_min(&generation, 0);
  qa_save_reader_destroy(&generation);
  return actor;
}
static bool set_own(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
static bool push_own(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) { qa_save_value_free(child); return false; }
  return true;
}
__attribute__((unused)) static qa_save_value *str_value(const char *text) {
  return qa_save_value_string(text != NULL ? text : "");
}
__attribute__((unused)) static qa_save_value *vec_value(const qa_vec3 *vector) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "x", qa_save_value_number(vector->x)) ||
      !set_own(obj, "y", qa_save_value_number(vector->y)) ||
      !set_own(obj, "z", qa_save_value_number(vector->z))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
static qa_save_value *actor_value(const qa_saved_actor_id *actor) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "slot", qa_save_value_number(actor->slot)) ||
      !set_own(obj, "generation", qa_save_value_number(actor->generation))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
__attribute__((unused)) static qa_save_value *time_value(const qa_source_time *time) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "kind", qa_save_value_string(
          time->kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds")) ||
      !set_own(obj, "value", qa_save_value_number(time->value))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
/* Moves every entry of `child` into `object` (TS object spread). */
__attribute__((unused)) static bool merge_into(qa_save_value *object, qa_save_value *child) {
  for (size_t i = 0; i < child->entry_count; i++) {
    qa_save_value *value = child->entries[i].value;
    child->entries[i].value = NULL;
    if (!qa_save_value_set(object, child->entries[i].key, value)) {
      child->entries[i].value = value;
      return false;
    }
  }
  return true;
}


static const char *choice_field(const qa_save_reader *reader, const char *name,
                                const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static double integer_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double integer0_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  return value;
}
static bool bool_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  bool value = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *string_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_q2_native_cause_checkpoint read_native_cause(const qa_save_reader *reader) {
  static const char *const editions[] = {"classic", "rerelease"};
  static const char *const games[] = {"base", "xatrix", "rogue", "ctf"};
  qa_q2_native_cause_checkpoint cause;
  memset(&cause, 0, sizeof cause);
  cause.edition = choice_field(reader, "edition", editions, 2);
  if (strcmp(cause.edition, "classic") == 0) {
    cause.game = choice_field(reader, "game", games, 4);
    cause.value = integer_field(reader, "value");
  } else {
    cause.id = integer0_field(reader, "id");
    cause.friendly_fire = bool_field(reader, "friendlyFire");
    cause.no_point_loss = bool_field(reader, "noPointLoss");
  }
  return cause;
}
static qa_save_value *build_native_cause(const qa_q2_native_cause_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = set_own(obj, "edition", qa_save_value_string(in->edition != NULL ? in->edition : "classic"));
  if (ok && in->edition != NULL && strcmp(in->edition, "rerelease") == 0)
    ok = set_own(obj, "id", qa_save_value_number(in->id)) &&
         set_own(obj, "friendlyFire", qa_save_value_boolean(in->friendly_fire)) &&
         set_own(obj, "noPointLoss", qa_save_value_boolean(in->no_point_loss));
  else if (ok)
    ok = set_own(obj, "game", qa_save_value_string(in->game != NULL ? in->game : "base")) &&
         set_own(obj, "value", qa_save_value_number(in->value));
  if (!ok) { qa_save_value_free(obj); return NULL; }
  return obj;
}
static qa_q2_attack_cause read_attack_cause_value(const qa_save_reader *reader) {
  static const char *const kinds[] = {"q1", "q2", "q3", "environment"};
  static const char *const armor_effects[] = {"bypass", "half-effectiveness"};
  static const char *const hazards[] = {"fall", "drown", "lava", "slime", "crush", "trigger"};
  qa_q2_attack_cause cause;
  memset(&cause, 0, sizeof cause);
  const char *kind = choice_field(reader, "kind", kinds, 4);
  if (strcmp(kind, "q1") == 0) {
    cause.kind = QA_Q2_ATTACK_CAUSE_Q1;
    cause.death_type = string_field(reader, "deathType");
    qa_save_reader armor = field(reader, "armorEffect");
    if (!qa_save_reader_is_undefined(&armor))
      cause.armor_effect = qa_save_reader_choice_string(&armor, armor_effects, 2);
    qa_save_reader_destroy(&armor);
  } else if (strcmp(kind, "q2") == 0) {
    cause.kind = QA_Q2_ATTACK_CAUSE_Q2;
    cause.means_of_death = integer_field(reader, "meansOfDeath");
    cause.damage_flags = integer_field(reader, "damageFlags");
    qa_save_reader native = field(reader, "native");
    if (!qa_save_reader_is_undefined(&native)) {
      cause.has_native = true;
      cause.native = read_native_cause(&native);
    }
    qa_save_reader_destroy(&native);
  } else if (strcmp(kind, "q3") == 0) {
    cause.kind = QA_Q2_ATTACK_CAUSE_Q3;
    cause.means_of_death = integer_field(reader, "meansOfDeath");
    cause.damage_flags = integer_field(reader, "damageFlags");
  } else {
    cause.kind = QA_Q2_ATTACK_CAUSE_ENVIRONMENT;
    cause.hazard = choice_field(reader, "hazard", hazards, 6);
  }
  return cause;
}
static qa_save_value *build_attack_cause_value(const qa_q2_attack_cause *in) {
  static const char *const names[] = {"q1", "q2", "q3", "environment"};
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = set_own(obj, "kind", qa_save_value_string(names[in->kind]));
  if (ok && in->kind == QA_Q2_ATTACK_CAUSE_Q1) {
    ok = set_own(obj, "deathType", qa_save_value_string(in->death_type != NULL ? in->death_type : ""));
    if (ok && in->armor_effect != NULL)
      ok = set_own(obj, "armorEffect", qa_save_value_string(in->armor_effect));
  } else if (ok && in->kind == QA_Q2_ATTACK_CAUSE_Q2) {
    ok = set_own(obj, "meansOfDeath", qa_save_value_number(in->means_of_death)) &&
         set_own(obj, "damageFlags", qa_save_value_number(in->damage_flags));
    if (ok && in->has_native) ok = set_own(obj, "native", build_native_cause(&in->native));
  } else if (ok && in->kind == QA_Q2_ATTACK_CAUSE_Q3) {
    ok = set_own(obj, "meansOfDeath", qa_save_value_number(in->means_of_death)) &&
         set_own(obj, "damageFlags", qa_save_value_number(in->damage_flags));
  } else if (ok) {
    ok = set_own(obj, "hazard", qa_save_value_string(in->hazard != NULL ? in->hazard : "fall"));
  }
  if (!ok) { qa_save_value_free(obj); return NULL; }
  return obj;
}

static void read_q2_attack_checkpoint(const qa_save_reader *reader, qa_q2_attack_checkpoint *out);
static qa_save_value *build_q2_attack_checkpoint(const qa_q2_attack_checkpoint *in);
static void read_q2_spawn_value(const qa_save_reader *reader, qa_q2_spawn_value *out);
static qa_save_value *build_q2_spawn_value(const qa_q2_spawn_value *in);
static void read_q2_entity_spawn(const qa_save_reader *reader, qa_q2_entity_spawn *out);
static qa_save_value *build_q2_entity_spawn(const qa_q2_entity_spawn *in);
static void read_q2_entity_values(const qa_save_reader *reader, qa_q2_entity_values *out);
static qa_save_value *build_q2_entity_values(const qa_q2_entity_values *in);
static void read_q2_entity_links(const qa_save_reader *reader, qa_q2_entity_links *out);
static qa_save_value *build_q2_entity_links(const qa_q2_entity_links *in);
static void read_q2_entity_callbacks(const qa_save_reader *reader, qa_q2_entity_callbacks *out);
static qa_save_value *build_q2_entity_callbacks(const qa_q2_entity_callbacks *in);
static void read_q2_entity_checkpoint(const qa_save_reader *reader, qa_q2_entity_checkpoint *out);
static qa_save_value *build_q2_entity_checkpoint(const qa_q2_entity_checkpoint *in);
static void read_q2_freed_slot(const qa_save_reader *reader, qa_q2_freed_slot *out);
static qa_save_value *build_q2_freed_slot(const qa_q2_freed_slot *in);
static void read_q2_foundation_counters(const qa_save_reader *reader, qa_q2_foundation_counters *out);
static qa_save_value *build_q2_foundation_counters(const qa_q2_foundation_counters *in);
static void read_q2_foundation_checkpoint(const qa_save_reader *reader, qa_q2_foundation_checkpoint *out);
static qa_save_value *build_q2_foundation_checkpoint(const qa_q2_foundation_checkpoint *in);

static void read_q2_attack_checkpoint(const qa_save_reader *reader, qa_q2_attack_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "sequence");
    (*out).sequence = qa_save_reader_integer_min(&f0, 0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "time");
    (*out).time = qa_read_time(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "attacker");
    if (!qa_save_reader_is_null(&f2)) { (*out).has_attacker = true;
      (*out).attacker = read_actor(&f2);
    }
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "inflictor");
    if (!qa_save_reader_is_null(&f3)) { (*out).has_inflictor = true;
      (*out).inflictor = read_actor(&f3);
    }
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "originatingProjectile");
    if (!qa_save_reader_is_undefined(&f4)) { (*out).has_originating_projectile = true;
      (*out).originating_projectile = read_actor(&f4);
    }
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "damagePowerupOwner");
    if (qa_save_reader_is_undefined(&f5)) (*out).damage_powerup_owner = NULL; else {
      (*out).damage_powerup_owner = qa_namespaced(&f5);
    }
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "weapon");
    if (qa_save_reader_is_null(&f6)) (*out).weapon = NULL; else {
      (*out).weapon = qa_namespaced(&f6);
    }
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "weaponProvider");
    (*out).weapon_provider = qa_namespaced(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "combatProvider");
    (*out).combat_provider = qa_namespaced(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "inventoryProvider");
    (*out).inventory_provider = qa_namespaced(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "movementProvider");
    (*out).movement_provider = qa_namespaced(&f10);
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "cause");
    (*out).cause = read_attack_cause_value(&f11);
    qa_save_reader_destroy(&f11); }
}
static qa_save_value *build_q2_attack_checkpoint(const qa_q2_attack_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "sequence", qa_save_value_number((*in).sequence))) goto fail;
  if (!set_own(obj, "time", time_value(&(*in).time))) goto fail;
  if (!set_own(obj, "attacker", (*in).has_attacker ? actor_value(&(*in).attacker) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "inflictor", (*in).has_inflictor ? actor_value(&(*in).inflictor) : qa_save_value_null())) goto fail;
  if ((*in).has_originating_projectile && !set_own(obj, "originatingProjectile", actor_value(&(*in).originating_projectile))) goto fail;
  if ((*in).damage_powerup_owner != NULL && !set_own(obj, "damagePowerupOwner", str_value((*in).damage_powerup_owner))) goto fail;
  if (!set_own(obj, "weapon", (*in).weapon != NULL ? str_value((*in).weapon) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "weaponProvider", str_value((*in).weapon_provider))) goto fail;
  if (!set_own(obj, "combatProvider", str_value((*in).combat_provider))) goto fail;
  if (!set_own(obj, "inventoryProvider", str_value((*in).inventory_provider))) goto fail;
  if (!set_own(obj, "movementProvider", str_value((*in).movement_provider))) goto fail;
  if (!set_own(obj, "cause", build_attack_cause_value(&(*in).cause))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_spawn_value(const qa_save_reader *reader, qa_q2_spawn_value *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "key");
    (*out).key = qa_save_reader_string(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "value");
    (*out).value = qa_save_reader_string(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_spawn_value(const qa_q2_spawn_value *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "key", str_value((*in).key))) goto fail;
  if (!set_own(obj, "value", str_value((*in).value))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_entity_spawn(const qa_save_reader *reader, qa_q2_entity_spawn *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "classname");
    (*out).classname = qa_save_reader_string(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "ordinal");
    (*out).ordinal = qa_save_reader_integer(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "values");
    (*out).values_count = qa_save_reader_list_count(&f2);
    (*out).values = (qa_q2_spawn_value *)calloc((*out).values_count != 0 ? (*out).values_count : 1, sizeof(qa_q2_spawn_value));
    if ((*out).values == NULL) qa_save_reader_fail(&f2, "out of memory");
    for (size_t i3 = 0; i3 < (*out).values_count; i3++) {
      qa_save_reader it4 = item(&f2, i3);
      read_q2_spawn_value(&it4, &(*out).values[i3]);
      qa_save_reader_destroy(&it4);
    }
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_entity_spawn(const qa_q2_entity_spawn *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "classname", str_value((*in).classname))) goto fail;
  if (!set_own(obj, "ordinal", qa_save_value_number((*in).ordinal))) goto fail;
  { qa_save_value *arr5 = qa_save_value_array();
    if (arr5 == NULL) goto fail;
    for (size_t i6 = 0; i6 < (*in).values_count; i6++) {
      if (!push_own(arr5, build_q2_spawn_value(&(*in).values[i6]))) { qa_save_value_free(arr5); goto fail; }
    }
    if (!set_own(obj, "values", arr5)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_entity_values(const qa_save_reader *reader, qa_q2_entity_values *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "classname");
    (*out).classname = qa_save_reader_string(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "target");
    (*out).target = qa_save_reader_string(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "targetname");
    (*out).targetname = qa_save_reader_string(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "killtarget");
    (*out).killtarget = qa_save_reader_string(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "combatTarget");
    (*out).combat_target = qa_save_reader_string(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "deathTarget");
    (*out).death_target = qa_save_reader_string(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "healthTarget");
    if (qa_save_reader_is_undefined(&f6)) (*out).health_target = NULL; else {
      (*out).health_target = qa_save_reader_string(&f6);
    }
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "itemTarget");
    if (qa_save_reader_is_undefined(&f7)) (*out).item_target = NULL; else {
      (*out).item_target = qa_save_reader_string(&f7);
    }
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "message");
    (*out).message = qa_save_reader_string(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "model");
    (*out).model = qa_save_reader_string(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "model2");
    (*out).model2 = qa_save_reader_string(&f10);
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "model3");
    (*out).model3 = qa_save_reader_string(&f11);
    qa_save_reader_destroy(&f11); }
  { qa_save_reader f12 = field(reader, "model4");
    (*out).model4 = qa_save_reader_string(&f12);
    qa_save_reader_destroy(&f12); }
  { qa_save_reader f13 = field(reader, "spawnflags");
    (*out).spawnflags = qa_save_reader_number(&f13);
    qa_save_reader_destroy(&f13); }
  { qa_save_reader f14 = field(reader, "delay");
    (*out).delay = qa_save_reader_number(&f14);
    qa_save_reader_destroy(&f14); }
  { qa_save_reader f15 = field(reader, "wait");
    (*out).wait = qa_save_reader_number(&f15);
    qa_save_reader_destroy(&f15); }
  { qa_save_reader f16 = field(reader, "speed");
    (*out).speed = qa_save_reader_number(&f16);
    qa_save_reader_destroy(&f16); }
  { qa_save_reader f17 = field(reader, "accel");
    (*out).accel = qa_save_reader_number(&f17);
    qa_save_reader_destroy(&f17); }
  { qa_save_reader f18 = field(reader, "decel");
    (*out).decel = qa_save_reader_number(&f18);
    qa_save_reader_destroy(&f18); }
  { qa_save_reader f19 = field(reader, "damage");
    (*out).damage = qa_save_reader_number(&f19);
    qa_save_reader_destroy(&f19); }
  { qa_save_reader f20 = field(reader, "damageRadius");
    (*out).damage_radius = qa_save_reader_number(&f20);
    qa_save_reader_destroy(&f20); }
  { qa_save_reader f21 = field(reader, "radiusDamage");
    (*out).radius_damage = qa_save_reader_number(&f21);
    qa_save_reader_destroy(&f21); }
  { qa_save_reader f22 = field(reader, "count");
    (*out).count = qa_save_reader_number(&f22);
    qa_save_reader_destroy(&f22); }
  { qa_save_reader f23 = field(reader, "maxHealth");
    (*out).max_health = qa_save_reader_number(&f23);
    qa_save_reader_destroy(&f23); }
  { qa_save_reader f24 = field(reader, "viewHeight");
    (*out).view_height = qa_save_reader_number(&f24);
    qa_save_reader_destroy(&f24); }
  { qa_save_reader f25 = field(reader, "frame");
    (*out).frame = qa_save_reader_number(&f25);
    qa_save_reader_destroy(&f25); }
  { qa_save_reader f26 = field(reader, "oldFrame");
    (*out).old_frame = qa_save_reader_number(&f26);
    qa_save_reader_destroy(&f26); }
  { qa_save_reader f27 = field(reader, "scale");
    (*out).scale = qa_save_reader_number(&f27);
    qa_save_reader_destroy(&f27); }
  { qa_save_reader f28 = field(reader, "alpha");
    (*out).alpha = qa_save_reader_is_undefined(&f28) ? 1 : qa_save_reader_number(&f28);
    qa_save_reader_destroy(&f28); }
  { qa_save_reader f29 = field(reader, "skin");
    (*out).skin = qa_save_reader_number(&f29);
    qa_save_reader_destroy(&f29); }
  { qa_save_reader f30 = field(reader, "effects");
    (*out).effects = qa_save_reader_number(&f30);
    qa_save_reader_destroy(&f30); }
  { qa_save_reader f31 = field(reader, "renderFlags");
    (*out).render_flags = qa_save_reader_number(&f31);
    qa_save_reader_destroy(&f31); }
  { qa_save_reader f32 = field(reader, "flags");
    (*out).flags = qa_save_reader_number(&f32);
    qa_save_reader_destroy(&f32); }
  { qa_save_reader f33 = field(reader, "serverFlags");
    (*out).server_flags = qa_save_reader_number(&f33);
    qa_save_reader_destroy(&f33); }
  { qa_save_reader f34 = field(reader, "lightLevel");
    (*out).light_level = qa_save_reader_number(&f34);
    qa_save_reader_destroy(&f34); }
  { qa_save_reader f35 = field(reader, "powerCubes");
    (*out).power_cubes = qa_save_reader_number(&f35);
    qa_save_reader_destroy(&f35); }
  { qa_save_reader f36 = field(reader, "timestamp");
    (*out).timestamp = qa_save_reader_number(&f36);
    qa_save_reader_destroy(&f36); }
  { qa_save_reader f37 = field(reader, "noise");
    (*out).noise = qa_save_reader_string(&f37);
    qa_save_reader_destroy(&f37); }
  { qa_save_reader f38 = field(reader, "sound");
    (*out).sound = qa_save_reader_string(&f38);
    qa_save_reader_destroy(&f38); }
  { qa_save_reader f39 = field(reader, "volume");
    (*out).volume = qa_save_reader_number(&f39);
    qa_save_reader_destroy(&f39); }
  { qa_save_reader f40 = field(reader, "attenuation");
    (*out).attenuation = qa_save_reader_number(&f40);
    qa_save_reader_destroy(&f40); }
  { qa_save_reader f41 = field(reader, "random");
    (*out).random = qa_save_reader_number(&f41);
    qa_save_reader_destroy(&f41); }
  { qa_save_reader f42 = field(reader, "map");
    (*out).map = qa_save_reader_string(&f42);
    qa_save_reader_destroy(&f42); }
  { qa_save_reader f43 = field(reader, "style");
    (*out).style = qa_save_reader_number(&f43);
    qa_save_reader_destroy(&f43); }
  { qa_save_reader f44 = field(reader, "transitionStarted");
    (*out).transition_started = qa_save_reader_boolean(&f44);
    qa_save_reader_destroy(&f44); }
  { qa_save_reader f45 = field(reader, "clipMask");
    (*out).clip_mask = qa_save_reader_number(&f45);
    qa_save_reader_destroy(&f45); }
  { qa_save_reader f46 = field(reader, "projectile");
    (*out).projectile = qa_save_reader_boolean(&f46);
    qa_save_reader_destroy(&f46); }
  { qa_save_reader f47 = field(reader, "dodgeable");
    (*out).dodgeable = qa_save_reader_boolean(&f47);
    qa_save_reader_destroy(&f47); }
  { qa_save_reader f48 = field(reader, "laserImmune");
    (*out).laser_immune = qa_save_reader_boolean(&f48);
    qa_save_reader_destroy(&f48); }
  { qa_save_reader f49 = field(reader, "damageableTarget");
    (*out).damageable_target = qa_save_reader_boolean(&f49);
    qa_save_reader_destroy(&f49); }
  { qa_save_reader f50 = field(reader, "visible");
    (*out).visible = qa_save_reader_boolean(&f50);
    qa_save_reader_destroy(&f50); }
  { qa_save_reader f51 = field(reader, "solid");
    static const char *const ch9[] = {"none", "trigger", "box", "brush"};
    (*out).solid = qa_save_reader_choice_string(&f51, ch9, 4);
    qa_save_reader_destroy(&f51); }
  { qa_save_reader f52 = field(reader, "motion");
    static const char *const ch10[] = {"stationary", "push", "stop", "toss", "new-toss", "bounce", "wall-bounce", "fly-missile", "fly", "step"};
    (*out).motion = qa_save_reader_choice_string(&f52, ch10, 10);
    qa_save_reader_destroy(&f52); }
  { qa_save_reader f53 = field(reader, "gravity");
    (*out).gravity = qa_save_reader_number(&f53);
    qa_save_reader_destroy(&f53); }
  { qa_save_reader f54 = field(reader, "gravityVector");
    (*out).gravity_vector = qa_read_vector(&f54);
    qa_save_reader_destroy(&f54); }
  { qa_save_reader f55 = field(reader, "angularVelocity");
    (*out).angular_velocity = qa_read_vector(&f55);
    qa_save_reader_destroy(&f55); }
  { qa_save_reader f56 = field(reader, "movedir");
    (*out).movedir = qa_read_vector(&f56);
    qa_save_reader_destroy(&f56); }
  { qa_save_reader f57 = field(reader, "pos1");
    (*out).pos1 = qa_read_vector(&f57);
    qa_save_reader_destroy(&f57); }
  { qa_save_reader f58 = field(reader, "pos2");
    (*out).pos2 = qa_read_vector(&f58);
    qa_save_reader_destroy(&f58); }
  { qa_save_reader f59 = field(reader, "nextThink");
    if (!qa_save_reader_is_null(&f59)) { (*out).has_next_think = true;
      (*out).next_think = qa_save_reader_number(&f59);
    }
    qa_save_reader_destroy(&f59); }
}
static qa_save_value *build_q2_entity_values(const qa_q2_entity_values *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "classname", str_value((*in).classname))) goto fail;
  if (!set_own(obj, "target", str_value((*in).target))) goto fail;
  if (!set_own(obj, "targetname", str_value((*in).targetname))) goto fail;
  if (!set_own(obj, "killtarget", str_value((*in).killtarget))) goto fail;
  if (!set_own(obj, "combatTarget", str_value((*in).combat_target))) goto fail;
  if (!set_own(obj, "deathTarget", str_value((*in).death_target))) goto fail;
  if ((*in).health_target != NULL && !set_own(obj, "healthTarget", str_value((*in).health_target))) goto fail;
  if ((*in).item_target != NULL && !set_own(obj, "itemTarget", str_value((*in).item_target))) goto fail;
  if (!set_own(obj, "message", str_value((*in).message))) goto fail;
  if (!set_own(obj, "model", str_value((*in).model))) goto fail;
  if (!set_own(obj, "model2", str_value((*in).model2))) goto fail;
  if (!set_own(obj, "model3", str_value((*in).model3))) goto fail;
  if (!set_own(obj, "model4", str_value((*in).model4))) goto fail;
  if (!set_own(obj, "spawnflags", qa_save_value_number((*in).spawnflags))) goto fail;
  if (!set_own(obj, "delay", qa_save_value_number((*in).delay))) goto fail;
  if (!set_own(obj, "wait", qa_save_value_number((*in).wait))) goto fail;
  if (!set_own(obj, "speed", qa_save_value_number((*in).speed))) goto fail;
  if (!set_own(obj, "accel", qa_save_value_number((*in).accel))) goto fail;
  if (!set_own(obj, "decel", qa_save_value_number((*in).decel))) goto fail;
  if (!set_own(obj, "damage", qa_save_value_number((*in).damage))) goto fail;
  if (!set_own(obj, "damageRadius", qa_save_value_number((*in).damage_radius))) goto fail;
  if (!set_own(obj, "radiusDamage", qa_save_value_number((*in).radius_damage))) goto fail;
  if (!set_own(obj, "count", qa_save_value_number((*in).count))) goto fail;
  if (!set_own(obj, "maxHealth", qa_save_value_number((*in).max_health))) goto fail;
  if (!set_own(obj, "viewHeight", qa_save_value_number((*in).view_height))) goto fail;
  if (!set_own(obj, "frame", qa_save_value_number((*in).frame))) goto fail;
  if (!set_own(obj, "oldFrame", qa_save_value_number((*in).old_frame))) goto fail;
  if (!set_own(obj, "scale", qa_save_value_number((*in).scale))) goto fail;
  if (!set_own(obj, "alpha", qa_save_value_number((*in).alpha))) goto fail;
  if (!set_own(obj, "skin", qa_save_value_number((*in).skin))) goto fail;
  if (!set_own(obj, "effects", qa_save_value_number((*in).effects))) goto fail;
  if (!set_own(obj, "renderFlags", qa_save_value_number((*in).render_flags))) goto fail;
  if (!set_own(obj, "flags", qa_save_value_number((*in).flags))) goto fail;
  if (!set_own(obj, "serverFlags", qa_save_value_number((*in).server_flags))) goto fail;
  if (!set_own(obj, "lightLevel", qa_save_value_number((*in).light_level))) goto fail;
  if (!set_own(obj, "powerCubes", qa_save_value_number((*in).power_cubes))) goto fail;
  if (!set_own(obj, "timestamp", qa_save_value_number((*in).timestamp))) goto fail;
  if (!set_own(obj, "noise", str_value((*in).noise))) goto fail;
  if (!set_own(obj, "sound", str_value((*in).sound))) goto fail;
  if (!set_own(obj, "volume", qa_save_value_number((*in).volume))) goto fail;
  if (!set_own(obj, "attenuation", qa_save_value_number((*in).attenuation))) goto fail;
  if (!set_own(obj, "random", qa_save_value_number((*in).random))) goto fail;
  if (!set_own(obj, "map", str_value((*in).map))) goto fail;
  if (!set_own(obj, "style", qa_save_value_number((*in).style))) goto fail;
  if (!set_own(obj, "transitionStarted", qa_save_value_boolean((*in).transition_started))) goto fail;
  if (!set_own(obj, "clipMask", qa_save_value_number((*in).clip_mask))) goto fail;
  if (!set_own(obj, "projectile", qa_save_value_boolean((*in).projectile))) goto fail;
  if (!set_own(obj, "dodgeable", qa_save_value_boolean((*in).dodgeable))) goto fail;
  if (!set_own(obj, "laserImmune", qa_save_value_boolean((*in).laser_immune))) goto fail;
  if (!set_own(obj, "damageableTarget", qa_save_value_boolean((*in).damageable_target))) goto fail;
  if (!set_own(obj, "visible", qa_save_value_boolean((*in).visible))) goto fail;
  if (!set_own(obj, "solid", str_value((*in).solid))) goto fail;
  if (!set_own(obj, "motion", str_value((*in).motion))) goto fail;
  if (!set_own(obj, "gravity", qa_save_value_number((*in).gravity))) goto fail;
  if (!set_own(obj, "gravityVector", vec_value(&(*in).gravity_vector))) goto fail;
  if (!set_own(obj, "angularVelocity", vec_value(&(*in).angular_velocity))) goto fail;
  if (!set_own(obj, "movedir", vec_value(&(*in).movedir))) goto fail;
  if (!set_own(obj, "pos1", vec_value(&(*in).pos1))) goto fail;
  if (!set_own(obj, "pos2", vec_value(&(*in).pos2))) goto fail;
  if (!set_own(obj, "nextThink", (*in).has_next_think ? qa_save_value_number((*in).next_think) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_entity_links(const qa_save_reader *reader, qa_q2_entity_links *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "activator");
    if (!qa_save_reader_is_null(&f0)) { (*out).has_activator = true;
      (*out).activator = read_actor(&f0);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "enemy");
    if (!qa_save_reader_is_null(&f1)) { (*out).has_enemy = true;
      (*out).enemy = read_actor(&f1);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "owner");
    if (!qa_save_reader_is_null(&f2)) { (*out).has_owner = true;
      (*out).owner = read_actor(&f2);
    }
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "goal");
    if (!qa_save_reader_is_null(&f3)) { (*out).has_goal = true;
      (*out).goal = read_actor(&f3);
    }
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "teamMaster");
    if (!qa_save_reader_is_null(&f4)) { (*out).has_team_master = true;
      (*out).team_master = read_actor(&f4);
    }
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "teamChain");
    if (!qa_save_reader_is_null(&f5)) { (*out).has_team_chain = true;
      (*out).team_chain = read_actor(&f5);
    }
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "chain");
    if (!qa_save_reader_is_null(&f6)) { (*out).has_chain = true;
      (*out).chain = read_actor(&f6);
    }
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "beam");
    if (!qa_save_reader_is_null(&f7)) { (*out).has_beam = true;
      (*out).beam = read_actor(&f7);
    }
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "beam2");
    if (!qa_save_reader_is_null(&f8)) { (*out).has_beam2 = true;
      (*out).beam2 = read_actor(&f8);
    }
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "proboscus");
    if (!qa_save_reader_is_null(&f9)) { (*out).has_proboscus = true;
      (*out).proboscus = read_actor(&f9);
    }
    qa_save_reader_destroy(&f9); }
}
static qa_save_value *build_q2_entity_links(const qa_q2_entity_links *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "activator", (*in).has_activator ? actor_value(&(*in).activator) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "enemy", (*in).has_enemy ? actor_value(&(*in).enemy) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "owner", (*in).has_owner ? actor_value(&(*in).owner) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "goal", (*in).has_goal ? actor_value(&(*in).goal) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "teamMaster", (*in).has_team_master ? actor_value(&(*in).team_master) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "teamChain", (*in).has_team_chain ? actor_value(&(*in).team_chain) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "chain", (*in).has_chain ? actor_value(&(*in).chain) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "beam", (*in).has_beam ? actor_value(&(*in).beam) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "beam2", (*in).has_beam2 ? actor_value(&(*in).beam2) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "proboscus", (*in).has_proboscus ? actor_value(&(*in).proboscus) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_entity_callbacks(const qa_save_reader *reader, qa_q2_entity_callbacks *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "think");
    if (qa_save_reader_is_null(&f0)) (*out).think = NULL; else {
      (*out).think = qa_save_reader_string(&f0);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "prethink");
    if (qa_save_reader_is_null(&f1)) (*out).prethink = NULL; else {
      (*out).prethink = qa_save_reader_string(&f1);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "postthink");
    if (qa_save_reader_is_null(&f2)) (*out).postthink = NULL; else {
      (*out).postthink = qa_save_reader_string(&f2);
    }
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "use");
    if (qa_save_reader_is_null(&f3)) (*out).use = NULL; else {
      (*out).use = qa_save_reader_string(&f3);
    }
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "touch");
    if (qa_save_reader_is_null(&f4)) (*out).touch = NULL; else {
      (*out).touch = qa_save_reader_string(&f4);
    }
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "pain");
    if (qa_save_reader_is_null(&f5)) (*out).pain = NULL; else {
      (*out).pain = qa_save_reader_string(&f5);
    }
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "die");
    if (qa_save_reader_is_null(&f6)) (*out).die = NULL; else {
      (*out).die = qa_save_reader_string(&f6);
    }
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "blocked");
    if (qa_save_reader_is_null(&f7)) (*out).blocked = NULL; else {
      (*out).blocked = qa_save_reader_string(&f7);
    }
    qa_save_reader_destroy(&f7); }
}
static qa_save_value *build_q2_entity_callbacks(const qa_q2_entity_callbacks *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "think", (*in).think != NULL ? str_value((*in).think) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "prethink", (*in).prethink != NULL ? str_value((*in).prethink) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "postthink", (*in).postthink != NULL ? str_value((*in).postthink) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "use", (*in).use != NULL ? str_value((*in).use) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "touch", (*in).touch != NULL ? str_value((*in).touch) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "pain", (*in).pain != NULL ? str_value((*in).pain) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "die", (*in).die != NULL ? str_value((*in).die) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "blocked", (*in).blocked != NULL ? str_value((*in).blocked) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_entity_checkpoint(const qa_save_reader *reader, qa_q2_entity_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "sourceSlot");
    if (!qa_save_reader_is_null(&f1)) { (*out).has_source_slot = true;
      (*out).source_slot = qa_save_reader_integer_min(&f1, 0);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "spawn");
    read_q2_entity_spawn(&f2, &(*out).spawn);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "values");
    read_q2_entity_values(&f3, &(*out).values);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "links");
    read_q2_entity_links(&f4, &(*out).links);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "lastAttack");
    if (!qa_save_reader_is_null(&f5)) { (*out).has_last_attack = true;
      read_q2_attack_checkpoint(&f5, &(*out).last_attack);
    }
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "callbacks");
    read_q2_entity_callbacks(&f6, &(*out).callbacks);
    qa_save_reader_destroy(&f6); }
  /* TS: absent healthTarget/itemTarget fall back to the spawn values. */
  if (out->values.health_target == NULL) { out->values.health_target = ""; for (size_t i = 0; i < out->spawn.values_count; i++) if (strcmp(out->spawn.values[i].key, "healthtarget") == 0) { out->values.health_target = out->spawn.values[i].value; break; } }
  if (out->values.item_target == NULL) { out->values.item_target = ""; for (size_t i = 0; i < out->spawn.values_count; i++) if (strcmp(out->spawn.values[i].key, "itemtarget") == 0) { out->values.item_target = out->spawn.values[i].value; break; } }
}
static qa_save_value *build_q2_entity_checkpoint(const qa_q2_entity_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "sourceSlot", (*in).has_source_slot ? qa_save_value_number((*in).source_slot) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "spawn", build_q2_entity_spawn(&(*in).spawn))) goto fail;
  if (!set_own(obj, "values", build_q2_entity_values(&(*in).values))) goto fail;
  if (!set_own(obj, "links", build_q2_entity_links(&(*in).links))) goto fail;
  if (!set_own(obj, "lastAttack", (*in).has_last_attack ? build_q2_attack_checkpoint(&(*in).last_attack) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "callbacks", build_q2_entity_callbacks(&(*in).callbacks))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_freed_slot(const qa_save_reader *reader, qa_q2_freed_slot *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "slot");
    (*out).slot = qa_save_reader_integer_min(&f0, 0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "time");
    (*out).time = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_freed_slot(const qa_q2_freed_slot *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "slot", qa_save_value_number((*in).slot))) goto fail;
  if (!set_own(obj, "time", qa_save_value_number((*in).time))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_foundation_counters(const qa_save_reader *reader, qa_q2_foundation_counters *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "totalSecrets");
    (*out).total_secrets = qa_save_reader_number(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "foundSecrets");
    (*out).found_secrets = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "totalGoals");
    (*out).total_goals = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "foundGoals");
    (*out).found_goals = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "totalMonsters");
    (*out).total_monsters = qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "killedMonsters");
    (*out).killed_monsters = qa_save_reader_number(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "serverFlags");
    (*out).server_flags = qa_save_reader_number(&f6);
    qa_save_reader_destroy(&f6); }
}
static qa_save_value *build_q2_foundation_counters(const qa_q2_foundation_counters *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "totalSecrets", qa_save_value_number((*in).total_secrets))) goto fail;
  if (!set_own(obj, "foundSecrets", qa_save_value_number((*in).found_secrets))) goto fail;
  if (!set_own(obj, "totalGoals", qa_save_value_number((*in).total_goals))) goto fail;
  if (!set_own(obj, "foundGoals", qa_save_value_number((*in).found_goals))) goto fail;
  if (!set_own(obj, "totalMonsters", qa_save_value_number((*in).total_monsters))) goto fail;
  if (!set_own(obj, "killedMonsters", qa_save_value_number((*in).killed_monsters))) goto fail;
  if (!set_own(obj, "serverFlags", qa_save_value_number((*in).server_flags))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_foundation_checkpoint(const qa_save_reader *reader, qa_q2_foundation_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "version");
    (*out).version = qa_save_reader_literal_number(&f0, 1);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "nextSourceSlot");
    (*out).next_source_slot = qa_save_reader_integer_min(&f1, 0);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "sequence");
    (*out).sequence = qa_save_reader_integer_min(&f2, 0);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "freedSlots");
    (*out).freed_slots_count = qa_save_reader_list_count(&f3);
    (*out).freed_slots = (qa_q2_freed_slot *)calloc((*out).freed_slots_count != 0 ? (*out).freed_slots_count : 1, sizeof(qa_q2_freed_slot));
    if ((*out).freed_slots == NULL) qa_save_reader_fail(&f3, "out of memory");
    for (size_t i19 = 0; i19 < (*out).freed_slots_count; i19++) {
      qa_save_reader it20 = item(&f3, i19);
      read_q2_freed_slot(&it20, &(*out).freed_slots[i19]);
      qa_save_reader_destroy(&it20);
    }
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "counters");
    read_q2_foundation_counters(&f4, &(*out).counters);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "entities");
    (*out).entities_count = qa_save_reader_list_count(&f5);
    (*out).entities = (qa_q2_entity_checkpoint *)calloc((*out).entities_count != 0 ? (*out).entities_count : 1, sizeof(qa_q2_entity_checkpoint));
    if ((*out).entities == NULL) qa_save_reader_fail(&f5, "out of memory");
    for (size_t i21 = 0; i21 < (*out).entities_count; i21++) {
      qa_save_reader it22 = item(&f5, i21);
      read_q2_entity_checkpoint(&it22, &(*out).entities[i21]);
      qa_save_reader_destroy(&it22);
    }
    qa_save_reader_destroy(&f5); }
}
static qa_save_value *build_q2_foundation_checkpoint(const qa_q2_foundation_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number((*in).version))) goto fail;
  if (!set_own(obj, "nextSourceSlot", qa_save_value_number((*in).next_source_slot))) goto fail;
  if (!set_own(obj, "sequence", qa_save_value_number((*in).sequence))) goto fail;
  { qa_save_value *arr23 = qa_save_value_array();
    if (arr23 == NULL) goto fail;
    for (size_t i24 = 0; i24 < (*in).freed_slots_count; i24++) {
      if (!push_own(arr23, build_q2_freed_slot(&(*in).freed_slots[i24]))) { qa_save_value_free(arr23); goto fail; }
    }
    if (!set_own(obj, "freedSlots", arr23)) goto fail; }
  if (!set_own(obj, "counters", build_q2_foundation_counters(&(*in).counters))) goto fail;
  { qa_save_value *arr25 = qa_save_value_array();
    if (arr25 == NULL) goto fail;
    for (size_t i26 = 0; i26 < (*in).entities_count; i26++) {
      if (!push_own(arr25, build_q2_entity_checkpoint(&(*in).entities[i26]))) { qa_save_value_free(arr25); goto fail; }
    }
    if (!set_own(obj, "entities", arr25)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

qa_q2_attack_checkpoint qa_read_q2_attack_checkpoint(const qa_save_reader *reader) {
  qa_q2_attack_checkpoint out;
  read_q2_attack_checkpoint(reader, &out);
  return out;
}
qa_save_value *qa_q2_attack_checkpoint_value(const qa_q2_attack_checkpoint *checkpoint) {
  return build_q2_attack_checkpoint(checkpoint);
}









qa_q2_foundation_checkpoint qa_read_q2_foundation_checkpoint(const qa_save_reader *reader) {
  qa_q2_foundation_checkpoint out;
  read_q2_foundation_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_foundation_checkpoint(const qa_q2_foundation_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_foundation_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_foundation_checkpoint(const uint8_t *bytes, size_t length, qa_q2_foundation_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-foundation")) { qa_save_value_free(root); return false; }
  read_q2_foundation_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

