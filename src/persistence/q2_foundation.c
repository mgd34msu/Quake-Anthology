/* Port of src/persistence/q2-foundation.ts over the content checkpoint
 * types (content/q2/foundation/checkpoint.h): TS Q2AttackCheckpoint,
 * Q2EntityCheckpoint and Q2FoundationCheckpoint. TS encodes the captured
 * objects as they are, so the builders emit keys in the order those objects
 * are created (the Q2Entity class field order for entity values, the
 * Q2EntityServices.attack() order for attacks); readers follow the TS
 * readers. GPL-2.0-or-later. */
#include "qa/persistence/q2_foundation.h"

#include <stddef.h>
#include <string.h>

#include "q2_codec_internal.h"

/* ---- attack provenance ---- */
static qa_q2_native_cause read_native_cause(const qa_save_reader *reader) {
  static const char *const EDITIONS[] = {"classic", "rerelease"};
  static const char *const GAMES[] = {"base", "xatrix", "rogue", "ctf"};
  qa_q2_native_cause cause;
  memset(&cause, 0, sizeof cause);
  cause.edition = q2c_choice(reader, "edition", EDITIONS, 2);
  if (strcmp(cause.edition, "classic") == 0) {
    cause.game = q2c_choice(reader, "game", GAMES, 4);
    cause.value = q2c_integer(reader, "value", -9007199254740991.0);
  } else {
    cause.id = q2c_integer(reader, "id", 0);
    cause.friendly_fire = q2c_boolean(reader, "friendlyFire");
    cause.no_point_loss = q2c_boolean(reader, "noPointLoss");
  }
  return cause;
}
static qa_save_value *native_cause_value(const qa_q2_native_cause *cause) {
  q2c_object object = q2c_object_new();
  bool rerelease = cause->edition != NULL && strcmp(cause->edition, "rerelease") == 0;
  q2c_set(&object, "edition", qa_save_value_string(rerelease ? "rerelease" : "classic"));
  if (rerelease) {
    q2c_set(&object, "id", qa_save_value_number(cause->id));
    q2c_set(&object, "friendlyFire", qa_save_value_boolean(cause->friendly_fire));
    q2c_set(&object, "noPointLoss", qa_save_value_boolean(cause->no_point_loss));
  } else {
    q2c_set(&object, "game", q2c_string_value(cause->game != NULL ? cause->game : "base"));
    q2c_set(&object, "value", qa_save_value_number(cause->value));
  }
  return q2c_done(&object);
}
static qa_attack_cause read_cause(const qa_save_reader *reader) {
  static const char *const KINDS[] = {"q1", "q2", "q3", "environment"};
  static const char *const ARMOR_EFFECTS[] = {"bypass", "half-effectiveness"};
  static const char *const HAZARDS[] = {"fall", "drown", "lava", "slime", "crush", "trigger"};
  const double integer_min = -9007199254740991.0;
  qa_attack_cause cause;
  memset(&cause, 0, sizeof cause);
  const char *kind = q2c_choice(reader, "kind", KINDS, 4);
  if (strcmp(kind, "q1") == 0) {
    cause.kind = QA_ATTACK_CAUSE_Q1;
    cause.death_type = q2c_string(reader, "deathType");
    if (q2c_present(reader, "armorEffect")) cause.armor_effect = q2c_choice(reader, "armorEffect", ARMOR_EFFECTS, 2);
  } else if (strcmp(kind, "q2") == 0) {
    cause.kind = QA_ATTACK_CAUSE_Q2;
    cause.means_of_death = q2c_integer(reader, "meansOfDeath", integer_min);
    cause.damage_flags = q2c_integer(reader, "damageFlags", integer_min);
    if (q2c_present(reader, "native")) {
      qa_save_reader native = q2c_field(reader, "native");
      cause.has_native = true;
      cause.native = read_native_cause(&native);
      qa_save_reader_destroy(&native);
    }
  } else if (strcmp(kind, "q3") == 0) {
    cause.kind = QA_ATTACK_CAUSE_Q3;
    cause.means_of_death = q2c_integer(reader, "meansOfDeath", integer_min);
    cause.damage_flags = q2c_integer(reader, "damageFlags", integer_min);
  } else {
    cause.kind = QA_ATTACK_CAUSE_ENVIRONMENT;
    cause.hazard = q2c_choice(reader, "hazard", HAZARDS, 6);
  }
  return cause;
}
static qa_save_value *cause_value(const qa_attack_cause *cause) {
  static const char *const NAMES[] = {"q1", "q2", "q3", "environment"};
  q2c_object object = q2c_object_new();
  q2c_set(&object, "kind", qa_save_value_string(NAMES[cause->kind]));
  if (cause->kind == QA_ATTACK_CAUSE_Q1) {
    q2c_set(&object, "deathType", q2c_string_value(cause->death_type));
    if (cause->armor_effect != NULL) q2c_set(&object, "armorEffect", qa_save_value_string(cause->armor_effect));
  } else if (cause->kind == QA_ATTACK_CAUSE_Q2 || cause->kind == QA_ATTACK_CAUSE_Q3) {
    q2c_set(&object, "meansOfDeath", qa_save_value_number(cause->means_of_death));
    q2c_set(&object, "damageFlags", qa_save_value_number(cause->damage_flags));
    if (cause->kind == QA_ATTACK_CAUSE_Q2 && cause->has_native) q2c_set(&object, "native", native_cause_value(&cause->native));
  } else {
    q2c_set(&object, "hazard", q2c_string_value(cause->hazard != NULL ? cause->hazard : "fall"));
  }
  return q2c_done(&object);
}

/* TS readQ2AttackCheckpoint. */
qa_q2_attack_checkpoint qa_read_q2_attack_checkpoint(const qa_save_reader *reader) {
  qa_q2_attack_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_attack_provenance *values = &out.values;
  values->sequence = q2c_integer(reader, "sequence", 0);
  qa_save_reader time = q2c_field(reader, "time");
  values->time = qa_read_time(&time);
  qa_save_reader_destroy(&time);
  out.has_attacker = q2c_nullable_actor(reader, "attacker", &out.attacker);
  out.has_inflictor = q2c_nullable_actor(reader, "inflictor", &out.inflictor);
  if (q2c_present(reader, "originatingProjectile")) {
    out.has_originating_projectile = true;
    out.originating_projectile = q2c_actor(reader, "originatingProjectile");
  }
  values->has_originating_projectile = out.has_originating_projectile;
  if (q2c_present(reader, "damagePowerupOwner")) {
    qa_save_reader owner = q2c_field(reader, "damagePowerupOwner");
    values->has_damage_powerup_owner = true;
    values->damage_powerup_owner = qa_namespaced(&owner);
    qa_save_reader_destroy(&owner);
  }
  qa_save_reader weapon = q2c_field(reader, "weapon");
  values->weapon = qa_save_reader_is_null(&weapon) ? NULL : qa_namespaced(&weapon);
  qa_save_reader_destroy(&weapon);
  static const char *const PROVIDERS[] = {"weaponProvider", "combatProvider", "inventoryProvider", "movementProvider"};
  qa_provider_id *targets[] = {&values->weapon_provider, &values->combat_provider, &values->inventory_provider,
                               &values->movement_provider};
  for (size_t i = 0; i < 4; i++) {
    qa_save_reader provider = q2c_field(reader, PROVIDERS[i]);
    *targets[i] = qa_namespaced(&provider);
    qa_save_reader_destroy(&provider);
  }
  qa_save_reader cause = q2c_field(reader, "cause");
  values->cause = read_cause(&cause);
  qa_save_reader_destroy(&cause);
  return out;
}

/* TS saveQ2Attack: {...values, attacker, inflictor, originatingProjectile?},
 * values in Q2EntityServices.attack() order. */
qa_save_value *qa_q2_attack_checkpoint_value(const qa_q2_attack_checkpoint *checkpoint) {
  const qa_attack_provenance *values = &checkpoint->values;
  q2c_object object = q2c_object_new();
  q2c_set(&object, "sequence", qa_save_value_number(values->sequence));
  q2c_set(&object, "time", q2c_time_value(values->time));
  q2c_set(&object, "weapon", values->weapon != NULL ? qa_save_value_string(values->weapon) : qa_save_value_null());
  q2c_set(&object, "weaponProvider", q2c_string_value(values->weapon_provider));
  q2c_set(&object, "combatProvider", q2c_string_value(values->combat_provider));
  q2c_set(&object, "inventoryProvider", q2c_string_value(values->inventory_provider));
  q2c_set(&object, "movementProvider", q2c_string_value(values->movement_provider));
  if (values->has_damage_powerup_owner)
    q2c_set(&object, "damagePowerupOwner", q2c_string_value(values->damage_powerup_owner));
  q2c_set(&object, "cause", cause_value(&values->cause));
  q2c_set(&object, "attacker", q2c_nullable_actor_value(checkpoint->has_attacker, checkpoint->attacker));
  q2c_set(&object, "inflictor", q2c_nullable_actor_value(checkpoint->has_inflictor, checkpoint->inflictor));
  if (checkpoint->has_originating_projectile)
    q2c_set(&object, "originatingProjectile", q2c_actor_value(checkpoint->originating_projectile));
  return q2c_done(&object);
}

/* ---- entities ---- */
static const char *spawn_value_of(const qa_q2_spawn_fields *spawn, const char *key) {
  for (size_t i = 0; i < spawn->value_count; i++)
    if (strcmp(spawn->values[i].key, key) == 0) return spawn->values[i].value;
  return NULL;
}

static void read_entity_values(const qa_save_reader *v, const qa_q2_spawn_fields *spawn, qa_q2_entity *out) {
  static const char *const SOLIDS[] = {"none", "trigger", "box", "brush"};
  static const char *const MOTIONS[] = {"stationary", "push", "stop", "toss", "new-toss", "bounce", "wall-bounce",
                                        "fly-missile", "fly", "step"};
  out->classname = q2c_string(v, "classname");
  out->target = q2c_string(v, "target");
  out->targetname = q2c_string(v, "targetname");
  out->killtarget = q2c_string(v, "killtarget");
  out->combat_target = q2c_string(v, "combatTarget");
  out->death_target = q2c_string(v, "deathTarget");
  if (q2c_present(v, "healthTarget")) out->health_target = q2c_string(v, "healthTarget");
  else { const char *value = spawn_value_of(spawn, "healthtarget"); out->health_target = value != NULL ? value : ""; }
  if (q2c_present(v, "itemTarget")) out->item_target = q2c_string(v, "itemTarget");
  else { const char *value = spawn_value_of(spawn, "itemtarget"); out->item_target = value != NULL ? value : ""; }
  out->message = q2c_string(v, "message");
  out->model = q2c_string(v, "model");
  out->model2 = q2c_string(v, "model2");
  out->model3 = q2c_string(v, "model3");
  out->model4 = q2c_string(v, "model4");
  out->spawnflags = q2c_number(v, "spawnflags");
  out->delay = q2c_number(v, "delay");
  out->wait = q2c_number(v, "wait");
  out->speed = q2c_number(v, "speed");
  out->accel = q2c_number(v, "accel");
  out->decel = q2c_number(v, "decel");
  out->damage = q2c_number(v, "damage");
  out->damage_radius = q2c_number(v, "damageRadius");
  out->radius_damage = q2c_number(v, "radiusDamage");
  out->count = q2c_number(v, "count");
  out->max_health = q2c_number(v, "maxHealth");
  out->view_height = q2c_number(v, "viewHeight");
  out->frame = q2c_number(v, "frame");
  out->old_frame = q2c_number(v, "oldFrame");
  out->scale = q2c_number(v, "scale");
  out->alpha = q2c_present(v, "alpha") ? q2c_number(v, "alpha") : 1;
  out->skin = q2c_number(v, "skin");
  out->effects = q2c_number(v, "effects");
  out->render_flags = q2c_number(v, "renderFlags");
  out->flags = q2c_number(v, "flags");
  out->server_flags = q2c_number(v, "serverFlags");
  out->light_level = q2c_number(v, "lightLevel");
  out->power_cubes = q2c_number(v, "powerCubes");
  out->timestamp = q2c_number(v, "timestamp");
  out->noise = q2c_string(v, "noise");
  out->sound = q2c_string(v, "sound");
  out->volume = q2c_number(v, "volume");
  out->attenuation = q2c_number(v, "attenuation");
  out->random = q2c_number(v, "random");
  out->map = q2c_string(v, "map");
  out->style = q2c_number(v, "style");
  out->transition_started = q2c_boolean(v, "transitionStarted");
  out->item_dropped = q2c_present(v, "itemDropped") ? q2c_boolean(v, "itemDropped") : false;
  out->clip_mask = q2c_number(v, "clipMask");
  out->projectile = q2c_boolean(v, "projectile");
  out->dodgeable = q2c_boolean(v, "dodgeable");
  out->laser_immune = q2c_boolean(v, "laserImmune");
  out->damageable_target = q2c_boolean(v, "damageableTarget");
  out->visible = q2c_boolean(v, "visible");
  out->solid = q2c_choice(v, "solid", SOLIDS, 4);
  out->motion = q2c_choice(v, "motion", MOTIONS, 10);
  out->gravity = q2c_number(v, "gravity");
  out->gravity_vector = q2c_vector(v, "gravityVector");
  out->angular_velocity = q2c_vector(v, "angularVelocity");
  out->movedir = q2c_vector(v, "movedir");
  out->pos1 = q2c_vector(v, "pos1");
  out->pos2 = q2c_vector(v, "pos2");
  out->has_next_think = !q2c_is_null(v, "nextThink");
  if (out->has_next_think) out->next_think = q2c_number(v, "nextThink");
}

/* Q2Entity class field order, minus the fields capture() destructures out. */
static qa_save_value *entity_values_value(const qa_q2_entity *e) {
  q2c_object object = q2c_object_new();
  q2c_set(&object, "classname", q2c_string_value(e->classname));
  q2c_set(&object, "target", q2c_string_value(e->target));
  q2c_set(&object, "targetname", q2c_string_value(e->targetname));
  q2c_set(&object, "killtarget", q2c_string_value(e->killtarget));
  q2c_set(&object, "combatTarget", q2c_string_value(e->combat_target));
  q2c_set(&object, "deathTarget", q2c_string_value(e->death_target));
  q2c_set(&object, "healthTarget", q2c_string_value(e->health_target));
  q2c_set(&object, "itemTarget", q2c_string_value(e->item_target));
  q2c_set(&object, "message", q2c_string_value(e->message));
  q2c_set(&object, "model", q2c_string_value(e->model));
  q2c_set(&object, "model2", q2c_string_value(e->model2));
  q2c_set(&object, "model3", q2c_string_value(e->model3));
  q2c_set(&object, "model4", q2c_string_value(e->model4));
  q2c_set(&object, "spawnflags", qa_save_value_number(e->spawnflags));
  q2c_set(&object, "delay", qa_save_value_number(e->delay));
  q2c_set(&object, "wait", qa_save_value_number(e->wait));
  q2c_set(&object, "speed", qa_save_value_number(e->speed));
  q2c_set(&object, "accel", qa_save_value_number(e->accel));
  q2c_set(&object, "decel", qa_save_value_number(e->decel));
  q2c_set(&object, "damage", qa_save_value_number(e->damage));
  q2c_set(&object, "damageRadius", qa_save_value_number(e->damage_radius));
  q2c_set(&object, "radiusDamage", qa_save_value_number(e->radius_damage));
  q2c_set(&object, "count", qa_save_value_number(e->count));
  q2c_set(&object, "maxHealth", qa_save_value_number(e->max_health));
  q2c_set(&object, "viewHeight", qa_save_value_number(e->view_height));
  q2c_set(&object, "frame", qa_save_value_number(e->frame));
  q2c_set(&object, "oldFrame", qa_save_value_number(e->old_frame));
  q2c_set(&object, "scale", qa_save_value_number(e->scale));
  q2c_set(&object, "alpha", qa_save_value_number(e->alpha));
  q2c_set(&object, "skin", qa_save_value_number(e->skin));
  q2c_set(&object, "effects", qa_save_value_number(e->effects));
  q2c_set(&object, "renderFlags", qa_save_value_number(e->render_flags));
  q2c_set(&object, "flags", qa_save_value_number(e->flags));
  q2c_set(&object, "serverFlags", qa_save_value_number(e->server_flags));
  q2c_set(&object, "lightLevel", qa_save_value_number(e->light_level));
  q2c_set(&object, "powerCubes", qa_save_value_number(e->power_cubes));
  q2c_set(&object, "timestamp", qa_save_value_number(e->timestamp));
  q2c_set(&object, "noise", q2c_string_value(e->noise));
  q2c_set(&object, "sound", q2c_string_value(e->sound));
  q2c_set(&object, "volume", qa_save_value_number(e->volume));
  q2c_set(&object, "attenuation", qa_save_value_number(e->attenuation));
  q2c_set(&object, "random", qa_save_value_number(e->random));
  q2c_set(&object, "map", q2c_string_value(e->map));
  q2c_set(&object, "style", qa_save_value_number(e->style));
  q2c_set(&object, "transitionStarted", qa_save_value_boolean(e->transition_started));
  q2c_set(&object, "itemDropped", qa_save_value_boolean(e->item_dropped));
  q2c_set(&object, "clipMask", qa_save_value_number(e->clip_mask));
  q2c_set(&object, "projectile", qa_save_value_boolean(e->projectile));
  q2c_set(&object, "dodgeable", qa_save_value_boolean(e->dodgeable));
  q2c_set(&object, "laserImmune", qa_save_value_boolean(e->laser_immune));
  q2c_set(&object, "damageableTarget", qa_save_value_boolean(e->damageable_target));
  q2c_set(&object, "visible", qa_save_value_boolean(e->visible));
  q2c_set(&object, "solid", q2c_string_value(e->solid));
  q2c_set(&object, "motion", q2c_string_value(e->motion));
  q2c_set(&object, "gravity", qa_save_value_number(e->gravity));
  q2c_set(&object, "gravityVector", q2c_vector_value(e->gravity_vector));
  q2c_set(&object, "angularVelocity", q2c_vector_value(e->angular_velocity));
  q2c_set(&object, "movedir", q2c_vector_value(e->movedir));
  q2c_set(&object, "pos1", q2c_vector_value(e->pos1));
  q2c_set(&object, "pos2", q2c_vector_value(e->pos2));
  q2c_set(&object, "nextThink", e->has_next_think ? qa_save_value_number(e->next_think) : qa_save_value_null());
  return q2c_done(&object);
}

typedef struct link_slot { const char *name; size_t offset; } link_slot;
#define LINK(name, field) {name, offsetof(qa_q2_entity_checkpoint, field)}
static const link_slot LINKS[] = {
  LINK("activator", activator), LINK("enemy", enemy), LINK("owner", owner), LINK("goal", goal),
  LINK("teamMaster", team_master), LINK("teamChain", team_chain), LINK("chain", chain), LINK("beam", beam),
  LINK("beam2", beam2), LINK("proboscus", proboscus)};
#undef LINK
typedef struct callback_slot { const char *name; size_t offset; } callback_slot;
#define CALLBACK(name) {#name, offsetof(qa_q2_entity_checkpoint, name)}
static const callback_slot CALLBACKS[] = {CALLBACK(think), CALLBACK(prethink), CALLBACK(postthink), CALLBACK(use),
                                          CALLBACK(touch), CALLBACK(pain), CALLBACK(die), CALLBACK(blocked)};
#undef CALLBACK

static void read_entity(const qa_save_reader *reader, qa_q2_entity_checkpoint *out, qa_persist_decoded *owner) {
  memset(out, 0, sizeof *out);
  out->actor = q2c_actor(reader, "actor");
  out->has_source_slot = !q2c_is_null(reader, "sourceSlot");
  if (out->has_source_slot) out->source_slot = q2c_integer(reader, "sourceSlot", 0);
  qa_save_reader spawn = q2c_field(reader, "spawn");
  out->spawn.classname = q2c_string(&spawn, "classname");
  out->spawn.ordinal = q2c_integer(&spawn, "ordinal", -9007199254740991.0);
  qa_save_reader values = q2c_field(&spawn, "values");
  out->spawn.value_count = qa_save_reader_list_count(&values);
  out->spawn.value_capacity = out->spawn.value_count;
  out->spawn.values = qa_persist_decoded_array(owner, out->spawn.value_count, sizeof *out->spawn.values);
  for (size_t i = 0; i < out->spawn.value_count; i++) {
    qa_save_reader pair = q2c_item(&values, i);
    /* Borrowed from the decoded tree; restore clones the spawn fields. */
    out->spawn.values[i].key = (char *)q2c_string(&pair, "key");
    out->spawn.values[i].value = (char *)q2c_string(&pair, "value");
    qa_save_reader_destroy(&pair);
  }
  qa_save_reader_destroy(&values);
  qa_save_reader_destroy(&spawn);
  qa_save_reader v = q2c_field(reader, "values");
  read_entity_values(&v, &out->spawn, &out->values);
  qa_save_reader_destroy(&v);
  qa_save_reader links = q2c_field(reader, "links");
  for (size_t i = 0; i < sizeof LINKS / sizeof *LINKS; i++) {
    qa_q2_saved_link *link = (qa_q2_saved_link *)((char *)out + LINKS[i].offset);
    link->has = q2c_nullable_actor(&links, LINKS[i].name, &link->v);
  }
  qa_save_reader_destroy(&links);
  qa_save_reader attack = q2c_field(reader, "lastAttack");
  out->has_last_attack = !qa_save_reader_is_null(&attack);
  if (out->has_last_attack) out->last_attack = qa_read_q2_attack_checkpoint(&attack);
  qa_save_reader_destroy(&attack);
  qa_save_reader callbacks = q2c_field(reader, "callbacks");
  for (size_t i = 0; i < sizeof CALLBACKS / sizeof *CALLBACKS; i++) {
    const char **name = (const char **)((char *)out + CALLBACKS[i].offset);
    *name = q2c_is_null(&callbacks, CALLBACKS[i].name) ? NULL : q2c_string(&callbacks, CALLBACKS[i].name);
  }
  qa_save_reader_destroy(&callbacks);
}

/* TS Q2EntityServices.capture() entity record. */
static qa_save_value *entity_value(const qa_q2_entity_checkpoint *entity) {
  qa_save_value *spawn_values = qa_save_value_array();
  for (size_t i = 0; spawn_values != NULL && i < entity->spawn.value_count; i++) {
    q2c_object pair = q2c_object_new();
    q2c_set(&pair, "key", q2c_string_value(entity->spawn.values[i].key));
    q2c_set(&pair, "value", q2c_string_value(entity->spawn.values[i].value));
    if (!q2c_push(spawn_values, q2c_done(&pair))) { qa_save_value_free(spawn_values); spawn_values = NULL; }
  }
  q2c_object spawn = q2c_object_new();
  q2c_set(&spawn, "classname", q2c_string_value(entity->spawn.classname));
  q2c_set(&spawn, "ordinal", qa_save_value_number(entity->spawn.ordinal));
  q2c_set(&spawn, "values", spawn_values);
  q2c_object links = q2c_object_new();
  for (size_t i = 0; i < sizeof LINKS / sizeof *LINKS; i++) {
    const qa_q2_saved_link *link = (const qa_q2_saved_link *)((const char *)entity + LINKS[i].offset);
    q2c_set(&links, LINKS[i].name, q2c_nullable_actor_value(link->has, link->v));
  }
  q2c_object callbacks = q2c_object_new();
  for (size_t i = 0; i < sizeof CALLBACKS / sizeof *CALLBACKS; i++) {
    const char *name = *(const char *const *)((const char *)entity + CALLBACKS[i].offset);
    q2c_set(&callbacks, CALLBACKS[i].name, name != NULL ? qa_save_value_string(name) : qa_save_value_null());
  }
  q2c_object object = q2c_object_new();
  q2c_set(&object, "actor", q2c_actor_value(entity->actor));
  q2c_set(&object, "sourceSlot", entity->has_source_slot ? qa_save_value_number(entity->source_slot) : qa_save_value_null());
  q2c_set(&object, "spawn", q2c_done(&spawn));
  q2c_set(&object, "values", entity_values_value(&entity->values));
  q2c_set(&object, "links", q2c_done(&links));
  q2c_set(&object, "lastAttack", entity->has_last_attack ? qa_q2_attack_checkpoint_value(&entity->last_attack)
                                                         : qa_save_value_null());
  q2c_set(&object, "callbacks", q2c_done(&callbacks));
  return q2c_done(&object);
}

/* ---- TS readQ2FoundationCheckpoint ---- */
qa_q2_foundation_checkpoint qa_read_q2_foundation_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner) {
  qa_q2_foundation_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader version = q2c_field(reader, "version");
  out.version = qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  out.next_source_slot = q2c_integer(reader, "nextSourceSlot", 0);
  out.sequence = q2c_integer(reader, "sequence", 0);
  qa_save_reader freed = q2c_field(reader, "freedSlots");
  out.freed_slot_count = qa_save_reader_list_count(&freed);
  qa_q2_freed_slot *slots = qa_persist_decoded_array(owner, out.freed_slot_count, sizeof *slots);
  for (size_t i = 0; i < out.freed_slot_count; i++) {
    qa_save_reader value = q2c_item(&freed, i);
    slots[i].slot = q2c_integer(&value, "slot", 0);
    slots[i].time = q2c_number(&value, "time");
    qa_save_reader_destroy(&value);
  }
  out.freed_slots = slots;
  qa_save_reader_destroy(&freed);
  qa_save_reader counters = q2c_field(reader, "counters");
  out.counters.total_secrets = q2c_number(&counters, "totalSecrets");
  out.counters.found_secrets = q2c_number(&counters, "foundSecrets");
  out.counters.total_goals = q2c_number(&counters, "totalGoals");
  out.counters.found_goals = q2c_number(&counters, "foundGoals");
  out.counters.total_monsters = q2c_number(&counters, "totalMonsters");
  out.counters.killed_monsters = q2c_number(&counters, "killedMonsters");
  out.counters.server_flags = q2c_number(&counters, "serverFlags");
  qa_save_reader_destroy(&counters);
  qa_save_reader entities = q2c_field(reader, "entities");
  out.entity_count = qa_save_reader_list_count(&entities);
  qa_q2_entity_checkpoint *records = qa_persist_decoded_array(owner, out.entity_count, sizeof *records);
  for (size_t i = 0; i < out.entity_count; i++) {
    qa_save_reader value = q2c_item(&entities, i);
    read_entity(&value, &records[i], owner);
    qa_save_reader_destroy(&value);
  }
  out.entities = records;
  qa_save_reader_destroy(&entities);
  return out;
}

/* TS Q2EntityServices.capture(): {version, nextSourceSlot, sequence,
 * counters, freedSlots, entities}. */
static qa_save_value *foundation_value(const qa_q2_foundation_checkpoint *checkpoint) {
  const qa_q2_counters *c = &checkpoint->counters;
  q2c_object counters = q2c_object_new();
  q2c_set(&counters, "totalSecrets", qa_save_value_number(c->total_secrets));
  q2c_set(&counters, "foundSecrets", qa_save_value_number(c->found_secrets));
  q2c_set(&counters, "totalGoals", qa_save_value_number(c->total_goals));
  q2c_set(&counters, "foundGoals", qa_save_value_number(c->found_goals));
  q2c_set(&counters, "totalMonsters", qa_save_value_number(c->total_monsters));
  q2c_set(&counters, "killedMonsters", qa_save_value_number(c->killed_monsters));
  q2c_set(&counters, "serverFlags", qa_save_value_number(c->server_flags));
  qa_save_value *freed = qa_save_value_array();
  for (size_t i = 0; freed != NULL && i < checkpoint->freed_slot_count; i++) {
    q2c_object slot = q2c_object_new();
    q2c_set(&slot, "slot", qa_save_value_number(checkpoint->freed_slots[i].slot));
    q2c_set(&slot, "time", qa_save_value_number(checkpoint->freed_slots[i].time));
    if (!q2c_push(freed, q2c_done(&slot))) { qa_save_value_free(freed); freed = NULL; }
  }
  qa_save_value *entities = qa_save_value_array();
  for (size_t i = 0; entities != NULL && i < checkpoint->entity_count; i++)
    if (!q2c_push(entities, entity_value(&checkpoint->entities[i]))) { qa_save_value_free(entities); entities = NULL; }
  q2c_object object = q2c_object_new();
  q2c_set(&object, "version", qa_save_value_number(checkpoint->version));
  q2c_set(&object, "nextSourceSlot", qa_save_value_number(checkpoint->next_source_slot));
  q2c_set(&object, "sequence", qa_save_value_number(checkpoint->sequence));
  q2c_set(&object, "counters", q2c_done(&counters));
  q2c_set(&object, "freedSlots", freed);
  q2c_set(&object, "entities", entities);
  return q2c_done(&object);
}

uint8_t *qa_encode_q2_foundation_checkpoint(const qa_q2_foundation_checkpoint *checkpoint, size_t *out_length) {
  return q2c_encode(foundation_value(checkpoint), out_length);
}

bool qa_decode_q2_foundation_checkpoint(const uint8_t *bytes, size_t length, qa_q2_foundation_checkpoint *out,
                                        qa_persist_decoded *owner) {
  qa_save_reader reader;
  if (!q2c_open(bytes, length, "q2-foundation", owner, &reader)) return false;
  *out = qa_read_q2_foundation_checkpoint(&reader, owner);
  qa_save_reader_destroy(&reader);
  return true;
}
