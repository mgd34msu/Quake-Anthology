/* Port of src/persistence/q1-foundation.ts. GPL-2.0-or-later. */
#include "qa/persistence/q1_foundation.h"

#include <stdlib.h>
#include <string.h>

#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"

/* TS Q1_WEAPON_IDS (content/q1/foundation/types.ts). */
static const char *const weapon_ids[] = {
    "axe", "shotgun", "supershotgun", "nailgun", "supernailgun",
    "grenadelauncher", "rocketlauncher", "lightning", "hipnotic:laser",
    "hipnotic:mjolnir", "hipnotic:proximity", "rogue:lava-nailgun",
    "rogue:lava-supernailgun", "rogue:multi-grenade", "rogue:multi-rocket",
    "rogue:plasma", "rogue:grapple", "mg3:laser", "mg3:mjolnir",
    "ctf:grapple"};
#define WEAPON_ID_COUNT (sizeof weapon_ids / sizeof weapon_ids[0])
/* TS Q1_POWERUP_IDS (content/q1/foundation/types.ts). */
static const char *const powerup_ids[] = {
    "quad", "invulnerability", "invisibility", "suit", "hipnotic:wetsuit",
    "hipnotic:empathy", "rogue:shield", "rogue:antigrav", "mg3:lavasuit",
    "q3:haste", "q3:regen", "q3:flight"};
#define POWERUP_ID_COUNT (sizeof powerup_ids / sizeof powerup_ids[0])
/* TS Q1_MONSTER_SPECIES (content/q1/foundation/entity.ts). */
static const char *const monster_species[] = {
    "army", "dog", "knight", "enforcer", "demon", "ogre", "hellknight",
    "shambler", "wizard", "shalrath", "tarbaby", "fish", "zombie", "boss",
    "oldone", "gremlin", "scourge", "armagon", "spikemine", "decoy", "eel",
    "sword", "wrath", "super-wrath", "mummy", "lava-man", "morph", "dragon"};
#define MONSTER_SPECIES_COUNT (sizeof monster_species / sizeof monster_species[0])

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
static void *alloc(const qa_save_reader *reader, size_t count, size_t size) {
  void *memory = count == 0 ? NULL : calloc(count, size);
  if (count != 0 && memory == NULL)
    qa_save_reader_fail(reader, "out of memory");
  return memory;
}
static const char *f_string(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  const char *value = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double f_number(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  double value = qa_save_reader_number(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static bool f_boolean(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  bool value = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *f_choice(const qa_save_reader *r, const char *name,
                            const char *const *choices, size_t count) {
  qa_save_reader f = field(r, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static double f_choice_number(const qa_save_reader *r, const char *name,
                              const double *choices, size_t count) {
  qa_save_reader f = field(r, name);
  double value = qa_save_reader_choice_number(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *f_namespaced(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  const char *value = qa_namespaced(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_vec3 f_vector(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  qa_vec3 value = qa_read_vector(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_saved_actor_id f_actor(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  qa_saved_actor_id value = qa_read_saved_actor(&f);
  qa_save_reader_destroy(&f);
  return value;
}
/* TS field(name).nullable(readSavedActor) */
static bool f_nullable_actor(const qa_save_reader *r, const char *name,
                             qa_saved_actor_id *out) {
  qa_save_reader f = field(r, name);
  bool present = !qa_save_reader_is_null(&f);
  if (present) *out = qa_read_saved_actor(&f);
  qa_save_reader_destroy(&f);
  return present;
}
static const char *weapon(const qa_save_reader *reader) {
  return qa_save_reader_choice_string(reader, weapon_ids, WEAPON_ID_COUNT);
}
static const char *f_weapon(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  const char *value = weapon(&f);
  qa_save_reader_destroy(&f);
  return value;
}

static qa_q1_entity_source_state read_source_state(
    const qa_save_reader *reader) {
  static const char *const solids[] = {"none", "trigger", "bbox",
                                       "slidebox", "bsp", "corpse"};
  static const char *const movements[] = {"none", "push", "step", "toss",
                                          "bounce", "fly", "flymissile",
                                          "noclip", "gib"};
  static const char *const states[] = {"bottom", "up", "top", "down"};
  static const char *const projectiles[] = {"rocket", "grenade", "spike",
                                            "superspike"};
  static const char *const attack_states[] = {"straight", "melee", "missile",
                                              "dodging"};
  static const double water_types[] = {0, -1, -2, -3, -4, -5, -6};
  qa_q1_entity_source_state state;
  memset(&state, 0, sizeof state);
  state.model = f_string(reader, "model");
  state.frame = f_number(reader, "frame");
  state.skin = f_number(reader, "skin");
  state.effects = f_number(reader, "effects");
  state.solid = f_choice(reader, "solid", solids, 6);
  state.movement = f_choice(reader, "movement", movements, 9);
  state.target = f_string(reader, "target");
  state.targetname = f_string(reader, "targetname");
  state.killtarget = f_string(reader, "killtarget");
  state.message = f_string(reader, "message");
  state.delay = f_number(reader, "delay");
  state.spawnflags = f_number(reader, "spawnflags");
  state.sounds = f_number(reader, "sounds");
  state.wait = f_number(reader, "wait");
  state.speed = f_number(reader, "speed");
  state.damage = f_number(reader, "damage");
  state.max_health = f_number(reader, "maxHealth");
  state.aimed_damage = f_boolean(reader, "aimedDamage");
  state.next_think = f_number(reader, "nextThink");
  state.original_model = f_string(reader, "originalModel");
  state.pos1 = f_vector(reader, "pos1");
  state.pos2 = f_vector(reader, "pos2");
  state.dest1 = f_vector(reader, "dest1");
  state.dest2 = f_vector(reader, "dest2");
  state.movedir = f_vector(reader, "movedir");
  state.mangle = f_vector(reader, "mangle");
  state.state = f_choice(reader, "state", states, 4);
  {
    qa_save_reader bounds = field(reader, "triggerBounds");
    state.has_trigger_bounds = !qa_save_reader_is_null(&bounds);
    if (state.has_trigger_bounds)
      state.trigger_bounds = qa_read_bounds(&bounds);
    qa_save_reader_destroy(&bounds);
  }
  state.attack_finished = f_number(reader, "attackFinished");
  state.count = f_number(reader, "count");
  state.activated = f_boolean(reader, "activated");
  {
    qa_save_reader projectile = field(reader, "projectile");
    if (!qa_save_reader_is_null(&projectile))
      state.projectile =
          qa_save_reader_choice_string(&projectile, projectiles, 4);
    qa_save_reader_destroy(&projectile);
    qa_save_reader projectile_weapon = field(reader, "projectileWeapon");
    if (!qa_save_reader_is_null(&projectile_weapon))
      state.projectile_weapon = weapon(&projectile_weapon);
    qa_save_reader_destroy(&projectile_weapon);
  }
  state.angular_velocity = f_vector(reader, "angularVelocity");
  state.water_level = f_number(reader, "waterLevel");
  state.water_type = f_choice_number(reader, "waterType", water_types, 7);
  state.movement_flags = f_number(reader, "movementFlags");
  state.ideal_yaw = f_number(reader, "idealYaw");
  state.yaw_speed = f_number(reader, "yawSpeed");
  state.attack_state = f_choice(reader, "attackState", attack_states, 4);
  return state;
}

static qa_q1_saved_monster read_monster(const qa_save_reader *reader) {
  static const char *const modes[] = {"stand", "walk", "run", "attack",
                                      "leap", "pain", "death"};
  qa_q1_saved_monster monster;
  memset(&monster, 0, sizeof monster);
  monster.species =
      f_choice(reader, "species", monster_species, MONSTER_SPECIES_COUNT);
  monster.mode = f_choice(reader, "mode", modes, 7);
  monster.frame_index = f_number(reader, "frameIndex");
  {
    qa_save_reader sequence = field(reader, "sequence");
    size_t count = qa_save_reader_list_count(&sequence);
    monster.sequence = alloc(&sequence, count, sizeof *monster.sequence);
    monster.sequence_count = count;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader frame = item(&sequence, i);
      monster.sequence[i] = qa_save_reader_number(&frame);
      qa_save_reader_destroy(&frame);
    }
    qa_save_reader_destroy(&sequence);
  }
  monster.first_frame = f_number(reader, "firstFrame");
  monster.has_enemy = f_nullable_actor(reader, "enemy", &monster.enemy);
  monster.has_old_enemy =
      f_nullable_actor(reader, "oldEnemy", &monster.old_enemy);
  monster.path = f_string(reader, "path");
  monster.pause_until = f_number(reader, "pauseUntil");
  monster.attack_finished = f_number(reader, "attackFinished");
  monster.pain_finished = f_number(reader, "painFinished");
  monster.search_until = f_number(reader, "searchUntil");
  monster.death_drop = f_boolean(reader, "deathDrop");
  monster.refired = f_boolean(reader, "refired");
  return monster;
}

static const char *callback_name(const qa_save_reader *callbacks,
                                 const char *key) {
  qa_save_reader f = field(callbacks, key);
  const char *value =
      qa_save_reader_is_null(&f) ? NULL : qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}

static qa_q1_saved_entity read_entity(const qa_save_reader *reader) {
  qa_q1_saved_entity entity;
  memset(&entity, 0, sizeof entity);
  /* TS entity() binds the callbacks reader before the literal, but
   * only reads through it in literal order below. */
  entity.actor = f_actor(reader, "actor");
  entity.actor_provider = f_namespaced(reader, "actorProvider");
  {
    qa_save_reader slot = field(reader, "sourceSlot");
    entity.has_source_slot = !qa_save_reader_is_null(&slot);
    if (entity.has_source_slot)
      entity.source_slot = qa_save_reader_integer_min(&slot, 0);
    qa_save_reader_destroy(&slot);
  }
  entity.classname = f_string(reader, "classname");
  {
    qa_save_reader ordinal = field(reader, "sourceOrdinal");
    entity.has_source_ordinal = !qa_save_reader_is_null(&ordinal);
    if (entity.has_source_ordinal)
      entity.source_ordinal = qa_save_reader_integer(&ordinal);
    qa_save_reader_destroy(&ordinal);
  }
  {
    qa_save_reader state = field(reader, "state");
    entity.state = read_source_state(&state);
    qa_save_reader_destroy(&state);
  }
  {
    qa_save_reader fields = field(reader, "fields");
    size_t count = qa_save_reader_list_count(&fields);
    entity.fields = alloc(&fields, count, sizeof *entity.fields);
    entity.field_count = count;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&fields, i);
      entity.fields[i].key = f_string(&entry, "key");
      entity.fields[i].value = f_string(&entry, "value");
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&fields);
  }
  {
    qa_save_reader references = field(reader, "references");
    size_t count = qa_save_reader_list_count(&references);
    entity.references = alloc(&references, count, sizeof *entity.references);
    entity.reference_count = count;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&references, i);
      entity.references[i].key = f_string(&entry, "key");
      entity.references[i].has_actor =
          f_nullable_actor(&entry, "actor", &entity.references[i].actor);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&references);
  }
  entity.has_owner = f_nullable_actor(reader, "owner", &entity.owner);
  entity.has_activator =
      f_nullable_actor(reader, "activator", &entity.activator);
  {
    qa_save_reader door_group = field(reader, "doorGroup");
    size_t count = qa_save_reader_list_count(&door_group);
    entity.door_group = alloc(&door_group, count, sizeof *entity.door_group);
    entity.door_group_count = count;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&door_group, i);
      entity.door_group[i] = qa_read_saved_actor(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&door_group);
  }
  {
    qa_save_reader monster = field(reader, "monster");
    entity.has_monster = !qa_save_reader_is_null(&monster);
    if (entity.has_monster) entity.monster = read_monster(&monster);
    qa_save_reader_destroy(&monster);
  }
  {
    qa_save_reader move = field(reader, "move");
    entity.has_move = !qa_save_reader_is_null(&move);
    if (entity.has_move) {
      entity.move.destination = f_vector(&move, "destination");
      entity.move.done = f_string(&move, "done");
    }
    qa_save_reader_destroy(&move);
  }
  {
    qa_save_reader callbacks = field(reader, "callbacks");
    entity.callbacks.think = callback_name(&callbacks, "think");
    entity.callbacks.use = callback_name(&callbacks, "use");
    entity.callbacks.touch = callback_name(&callbacks, "touch");
    entity.callbacks.pain = callback_name(&callbacks, "pain");
    entity.callbacks.die = callback_name(&callbacks, "die");
    entity.callbacks.blocked = callback_name(&callbacks, "blocked");
    entity.callbacks.path_end = callback_name(&callbacks, "pathEnd");
    qa_save_reader_destroy(&callbacks);
  }
  return entity;
}

static qa_q1_saved_player read_player(const qa_save_reader *reader) {
  static const char *const auto_switches[] = {"always", "new", "never"};
  qa_q1_saved_player player;
  memset(&player, 0, sizeof player);
  player.actor = f_actor(reader, "actor");
  player.actor_provider = f_namespaced(reader, "actorProvider");
  {
    qa_save_reader state = field(reader, "state");
    qa_q1_saved_player_state *out = &player.state;
    {
      qa_save_reader alpha = field(&state, "alpha");
      out->alpha =
          qa_save_reader_is_undefined(&alpha) ? 0 : qa_save_reader_number(&alpha);
      qa_save_reader_destroy(&alpha);
      qa_save_reader scale = field(&state, "scale");
      out->scale =
          qa_save_reader_is_undefined(&scale) ? 0 : qa_save_reader_number(&scale);
      qa_save_reader_destroy(&scale);
    }
    out->weapon = f_weapon(&state, "weapon");
    out->primary_holstered = f_boolean(&state, "primaryHolstered");
    out->attack_held = f_boolean(&state, "attackHeld");
    out->jump_held = f_boolean(&state, "jumpHeld");
    out->teleport_until = f_number(&state, "teleportUntil");
    out->attack_finished = f_number(&state, "attackFinished");
    out->weapon_frame = f_number(&state, "weaponFrame");
    out->weapon_animation_at = f_number(&state, "weaponAnimationAt");
    out->weapon_animation_base = f_number(&state, "weaponAnimationBase");
    out->continuous_firing = f_boolean(&state, "continuousFiring");
    out->next_weapon_frame = f_number(&state, "nextWeaponFrame");
    out->lightning_sound_at = f_number(&state, "lightningSoundAt");
    out->punch_angles = f_vector(&state, "punchAngles");
    out->nail_side = f_number(&state, "nailSide");
    out->max_health = f_number(&state, "maxHealth");
    out->mega_rot_at = f_number(&state, "megaRotAt");
    out->hostile_until = f_number(&state, "hostileUntil");
    out->view_angles = f_vector(&state, "viewAngles");
    out->water_level = f_number(&state, "waterLevel");
    out->air_finished = f_number(&state, "airFinished");
    out->drown_damage = f_number(&state, "drownDamage");
    out->drown_at = f_number(&state, "drownAt");
    out->hazard_at = f_number(&state, "hazardAt");
    out->auto_switch = f_choice(&state, "autoSwitch", auto_switches, 3);
    qa_save_reader_destroy(&state);
  }
  {
    qa_save_reader powerups = field(reader, "powerups");
    size_t count = qa_save_reader_list_count(&powerups);
    player.powerups = alloc(&powerups, count, sizeof *player.powerups);
    player.powerup_count = count;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&powerups, i);
      player.powerups[i].kind =
          f_choice(&entry, "kind", powerup_ids, POWERUP_ID_COUNT);
      player.powerups[i].expires = f_number(&entry, "expires");
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&powerups);
  }
  return player;
}

static const char **read_string_list(const qa_save_reader *reader,
                                     size_t *out_count) {
  size_t count = qa_save_reader_list_count(reader);
  const char **values = alloc(reader, count, sizeof *values);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(reader, i);
    values[i] = qa_save_reader_string(&entry);
    qa_save_reader_destroy(&entry);
  }
  *out_count = count;
  return values;
}

qa_q1_foundation_checkpoint qa_read_q1_foundation_checkpoint(
    const qa_save_reader *reader) {
  static const char *const phases[] = {"loading", "frozen"};
  static const char *const editions[] = {"classic", "rerelease"};
  qa_q1_foundation_checkpoint checkpoint;
  memset(&checkpoint, 0, sizeof checkpoint);
  checkpoint.provider = f_namespaced(reader, "provider");
  {
    qa_save_reader format = field(reader, "format");
    checkpoint.format = qa_save_reader_literal_string(&format, "q1-foundation");
    qa_save_reader_destroy(&format);
    qa_save_reader version = field(reader, "version");
    checkpoint.version = qa_save_reader_literal_number(&version, 5);
    qa_save_reader_destroy(&version);
  }
  {
    qa_save_reader precaches = field(reader, "precaches");
    checkpoint.precaches.phase = f_choice(&precaches, "phase", phases, 2);
    qa_save_reader models = field(&precaches, "models");
    checkpoint.precaches.models =
        read_string_list(&models, &checkpoint.precaches.model_count);
    qa_save_reader_destroy(&models);
    qa_save_reader sounds = field(&precaches, "sounds");
    checkpoint.precaches.sounds =
        read_string_list(&sounds, &checkpoint.precaches.sound_count);
    qa_save_reader_destroy(&sounds);
    qa_save_reader_destroy(&precaches);
  }
  checkpoint.edition = f_choice(reader, "edition", editions, 2);
  checkpoint.time = f_number(reader, "time");
  checkpoint.frame_seconds = f_number(reader, "frameSeconds");
  checkpoint.force_retouch = f_number(reader, "forceRetouch");
  {
    qa_save_reader sequence = field(reader, "sequence");
    checkpoint.sequence = qa_save_reader_integer_min(&sequence, 0);
    qa_save_reader_destroy(&sequence);
    qa_save_reader next = field(reader, "nextDynamicSlot");
    checkpoint.next_dynamic_slot = qa_save_reader_integer_min(&next, 0);
    qa_save_reader_destroy(&next);
  }
  checkpoint.total_secrets = f_number(reader, "totalSecrets");
  checkpoint.found_secrets = f_number(reader, "foundSecrets");
  checkpoint.total_monsters = f_number(reader, "totalMonsters");
  checkpoint.killed_monsters = f_number(reader, "killedMonsters");
  checkpoint.world_type = f_number(reader, "worldType");
  checkpoint.map_name = f_string(reader, "mapName");
  {
    qa_save_reader basis = field(reader, "basis");
    checkpoint.basis.forward = f_vector(&basis, "forward");
    checkpoint.basis.right = f_vector(&basis, "right");
    checkpoint.basis.up = f_vector(&basis, "up");
    qa_save_reader_destroy(&basis);
  }
  checkpoint.has_world = f_nullable_actor(reader, "world", &checkpoint.world);
  checkpoint.has_sight_entity =
      f_nullable_actor(reader, "sightEntity", &checkpoint.sight_entity);
  checkpoint.sight_time = f_number(reader, "sightTime");
  {
    qa_save_reader intermission = field(reader, "intermission");
    checkpoint.has_intermission = !qa_save_reader_is_null(&intermission);
    if (checkpoint.has_intermission) {
      checkpoint.intermission.map = f_string(&intermission, "map");
      checkpoint.intermission.has_cause = f_nullable_actor(
          &intermission, "cause", &checkpoint.intermission.cause);
      checkpoint.intermission.exit_after =
          f_number(&intermission, "exitAfter");
    }
    qa_save_reader_destroy(&intermission);
  }
  {
    qa_save_reader entities = field(reader, "entities");
    size_t count = qa_save_reader_list_count(&entities);
    checkpoint.entities = alloc(&entities, count, sizeof *checkpoint.entities);
    checkpoint.entity_count = count;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&entities, i);
      checkpoint.entities[i] = read_entity(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&entities);
  }
  {
    qa_save_reader players = field(reader, "players");
    size_t count = qa_save_reader_list_count(&players);
    checkpoint.players = alloc(&players, count, sizeof *checkpoint.players);
    checkpoint.player_count = count;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&players, i);
      checkpoint.players[i] = read_player(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&players);
  }
  {
    qa_save_reader extensions = field(reader, "extensions");
    size_t count = qa_save_reader_list_count(&extensions);
    checkpoint.extensions =
        alloc(&extensions, count, sizeof *checkpoint.extensions);
    checkpoint.extension_count = count;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&extensions, i);
      checkpoint.extensions[i].id = f_string(&entry, "id");
      qa_save_reader bytes = field(&entry, "bytes");
      size_t length = 0;
      checkpoint.extensions[i].bytes.bytes =
          qa_save_reader_bytes(&bytes, &length);
      checkpoint.extensions[i].bytes.length = length;
      qa_save_reader_destroy(&bytes);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&extensions);
  }
  return checkpoint;
}

/* --- encode (TS capture-object key order; see header) --- */
static bool set_own(qa_save_value *object, const char *key,
                    qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) {
    qa_save_value_free(child);
    return false;
  }
  return true;
}
static bool push_own(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) {
    qa_save_value_free(child);
    return false;
  }
  return true;
}
static qa_save_value *str_value(const char *text) {
  return qa_save_value_string(text != NULL ? text : "");
}
static qa_save_value *nullable_str(const char *text) {
  return text != NULL ? qa_save_value_string(text) : qa_save_value_null();
}
static qa_save_value *vec_value(qa_vec3 vector) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = set_own(obj, "x", qa_save_value_number(vector.x)) &&
            set_own(obj, "y", qa_save_value_number(vector.y)) &&
            set_own(obj, "z", qa_save_value_number(vector.z));
  if (!ok) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
static qa_save_value *bounds_value(qa_bounds bounds) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = set_own(obj, "min", vec_value(bounds.min)) &&
            set_own(obj, "max", vec_value(bounds.max));
  if (!ok) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
static qa_save_value *actor_value(qa_saved_actor_id actor) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = set_own(obj, "slot", qa_save_value_number(actor.slot)) &&
            set_own(obj, "generation", qa_save_value_number(actor.generation));
  if (!ok) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
static qa_save_value *nullable_actor(bool present, qa_saved_actor_id actor) {
  return present ? actor_value(actor) : qa_save_value_null();
}
#define SET(obj, key, child)             \
  do {                                   \
    if (ok) ok = set_own(obj, key, child); \
  } while (0)

static qa_save_value *build_source_state(const qa_q1_entity_source_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = true;
  SET(obj, "model", str_value(in->model));
  SET(obj, "frame", qa_save_value_number(in->frame));
  SET(obj, "skin", qa_save_value_number(in->skin));
  SET(obj, "effects", qa_save_value_number(in->effects));
  SET(obj, "solid", str_value(in->solid));
  SET(obj, "movement", str_value(in->movement));
  SET(obj, "target", str_value(in->target));
  SET(obj, "targetname", str_value(in->targetname));
  SET(obj, "killtarget", str_value(in->killtarget));
  SET(obj, "message", str_value(in->message));
  SET(obj, "delay", qa_save_value_number(in->delay));
  SET(obj, "spawnflags", qa_save_value_number(in->spawnflags));
  SET(obj, "sounds", qa_save_value_number(in->sounds));
  SET(obj, "wait", qa_save_value_number(in->wait));
  SET(obj, "speed", qa_save_value_number(in->speed));
  SET(obj, "damage", qa_save_value_number(in->damage));
  SET(obj, "maxHealth", qa_save_value_number(in->max_health));
  SET(obj, "aimedDamage", qa_save_value_boolean(in->aimed_damage));
  SET(obj, "nextThink", qa_save_value_number(in->next_think));
  SET(obj, "originalModel", str_value(in->original_model));
  SET(obj, "pos1", vec_value(in->pos1));
  SET(obj, "pos2", vec_value(in->pos2));
  SET(obj, "dest1", vec_value(in->dest1));
  SET(obj, "dest2", vec_value(in->dest2));
  SET(obj, "movedir", vec_value(in->movedir));
  SET(obj, "mangle", vec_value(in->mangle));
  SET(obj, "state", str_value(in->state));
  if (ok)
    ok = set_own(obj, "triggerBounds",
                 in->has_trigger_bounds ? bounds_value(in->trigger_bounds)
                                        : qa_save_value_null());
  SET(obj, "attackFinished", qa_save_value_number(in->attack_finished));
  SET(obj, "count", qa_save_value_number(in->count));
  SET(obj, "activated", qa_save_value_boolean(in->activated));
  SET(obj, "projectile", nullable_str(in->projectile));
  SET(obj, "projectileWeapon", nullable_str(in->projectile_weapon));
  SET(obj, "angularVelocity", vec_value(in->angular_velocity));
  SET(obj, "waterLevel", qa_save_value_number(in->water_level));
  SET(obj, "waterType", qa_save_value_number(in->water_type));
  SET(obj, "movementFlags", qa_save_value_number(in->movement_flags));
  SET(obj, "idealYaw", qa_save_value_number(in->ideal_yaw));
  SET(obj, "yawSpeed", qa_save_value_number(in->yaw_speed));
  SET(obj, "attackState", str_value(in->attack_state));
  if (!ok) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}

static qa_save_value *build_monster(const qa_q1_saved_monster *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = true;
  SET(obj, "species", str_value(in->species));
  SET(obj, "mode", str_value(in->mode));
  SET(obj, "frameIndex", qa_save_value_number(in->frame_index));
  if (ok) {
    qa_save_value *sequence = qa_save_value_array();
    bool built = sequence != NULL;
    for (size_t i = 0; built && i < in->sequence_count; i++)
      built = push_own(sequence, qa_save_value_number(in->sequence[i]));
    if (!built) qa_save_value_free(sequence);
    ok = built && set_own(obj, "sequence", sequence);
  }
  SET(obj, "firstFrame", qa_save_value_number(in->first_frame));
  if (ok) ok = set_own(obj, "enemy", nullable_actor(in->has_enemy, in->enemy));
  if (ok)
    ok = set_own(obj, "oldEnemy",
                 nullable_actor(in->has_old_enemy, in->old_enemy));
  SET(obj, "path", str_value(in->path));
  SET(obj, "pauseUntil", qa_save_value_number(in->pause_until));
  SET(obj, "attackFinished", qa_save_value_number(in->attack_finished));
  SET(obj, "painFinished", qa_save_value_number(in->pain_finished));
  SET(obj, "searchUntil", qa_save_value_number(in->search_until));
  SET(obj, "deathDrop", qa_save_value_boolean(in->death_drop));
  SET(obj, "refired", qa_save_value_boolean(in->refired));
  if (!ok) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}

static qa_save_value *build_entity(const qa_q1_saved_entity *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = true;
  SET(obj, "actor", actor_value(in->actor));
  if (ok)
    ok = set_own(obj, "sourceSlot",
                 in->has_source_slot ? qa_save_value_number(in->source_slot)
                                     : qa_save_value_null());
  SET(obj, "actorProvider", str_value(in->actor_provider));
  SET(obj, "classname", str_value(in->classname));
  if (ok)
    ok = set_own(obj, "sourceOrdinal",
                 in->has_source_ordinal
                     ? qa_save_value_number(in->source_ordinal)
                     : qa_save_value_null());
  SET(obj, "state", build_source_state(&in->state));
  if (ok) {
    qa_save_value *fields = qa_save_value_array();
    bool built = fields != NULL;
    for (size_t i = 0; built && i < in->field_count; i++) {
      qa_save_value *entry = qa_save_value_object();
      built = entry != NULL &&
              set_own(entry, "key", str_value(in->fields[i].key)) &&
              set_own(entry, "value", str_value(in->fields[i].value));
      if (!built) qa_save_value_free(entry);
      else built = push_own(fields, entry);
    }
    if (!built) qa_save_value_free(fields);
    ok = built && set_own(obj, "fields", fields);
  }
  if (ok) {
    qa_save_value *references = qa_save_value_array();
    bool built = references != NULL;
    for (size_t i = 0; built && i < in->reference_count; i++) {
      qa_save_value *entry = qa_save_value_object();
      built = entry != NULL &&
              set_own(entry, "key", str_value(in->references[i].key)) &&
              set_own(entry, "actor",
                      nullable_actor(in->references[i].has_actor,
                                     in->references[i].actor));
      if (!built) qa_save_value_free(entry);
      else built = push_own(references, entry);
    }
    if (!built) qa_save_value_free(references);
    ok = built && set_own(obj, "references", references);
  }
  if (ok) ok = set_own(obj, "owner", nullable_actor(in->has_owner, in->owner));
  if (ok)
    ok = set_own(obj, "activator",
                 nullable_actor(in->has_activator, in->activator));
  if (ok) {
    qa_save_value *door_group = qa_save_value_array();
    bool built = door_group != NULL;
    for (size_t i = 0; built && i < in->door_group_count; i++)
      built = push_own(door_group, actor_value(in->door_group[i]));
    if (!built) qa_save_value_free(door_group);
    ok = built && set_own(obj, "doorGroup", door_group);
  }
  if (ok)
    ok = set_own(obj, "monster",
                 in->has_monster ? build_monster(&in->monster)
                                 : qa_save_value_null());
  if (ok) {
    qa_save_value *move = NULL;
    if (in->has_move) {
      move = qa_save_value_object();
      bool built = move != NULL &&
                   set_own(move, "destination",
                           vec_value(in->move.destination)) &&
                   set_own(move, "done", str_value(in->move.done));
      if (!built) {
        qa_save_value_free(move);
        move = NULL;
        ok = false;
      }
    } else {
      move = qa_save_value_null();
      ok = move != NULL;
    }
    if (ok) ok = set_own(obj, "move", move);
  }
  if (ok) {
    qa_save_value *callbacks = qa_save_value_object();
    bool built = callbacks != NULL &&
                 set_own(callbacks, "think", nullable_str(in->callbacks.think)) &&
                 set_own(callbacks, "use", nullable_str(in->callbacks.use)) &&
                 set_own(callbacks, "touch", nullable_str(in->callbacks.touch)) &&
                 set_own(callbacks, "pain", nullable_str(in->callbacks.pain)) &&
                 set_own(callbacks, "die", nullable_str(in->callbacks.die)) &&
                 set_own(callbacks, "blocked",
                         nullable_str(in->callbacks.blocked)) &&
                 set_own(callbacks, "pathEnd",
                         nullable_str(in->callbacks.path_end));
    if (!built) qa_save_value_free(callbacks);
    ok = built && set_own(obj, "callbacks", callbacks);
  }
  if (!ok) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}

static qa_save_value *build_player(const qa_q1_saved_player *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = true;
  SET(obj, "actor", actor_value(in->actor));
  SET(obj, "actorProvider", str_value(in->actor_provider));
  if (ok) {
    const qa_q1_saved_player_state *s = &in->state;
    qa_save_value *state = qa_save_value_object();
    bool built = state != NULL;
    /* Q1PlayerState declaration order (see header comment). */
    if (built) built = set_own(state, "alpha", qa_save_value_number(s->alpha));
    if (built) built = set_own(state, "scale", qa_save_value_number(s->scale));
    if (built) built = set_own(state, "weapon", str_value(s->weapon));
    if (built)
      built = set_own(state, "primaryHolstered",
                      qa_save_value_boolean(s->primary_holstered));
    if (built)
      built = set_own(state, "attackFinished",
                      qa_save_value_number(s->attack_finished));
    if (built)
      built = set_own(state, "attackHeld", qa_save_value_boolean(s->attack_held));
    if (built)
      built = set_own(state, "jumpHeld", qa_save_value_boolean(s->jump_held));
    if (built)
      built = set_own(state, "teleportUntil",
                      qa_save_value_number(s->teleport_until));
    if (built)
      built = set_own(state, "weaponFrame",
                      qa_save_value_number(s->weapon_frame));
    if (built)
      built = set_own(state, "weaponAnimationAt",
                      qa_save_value_number(s->weapon_animation_at));
    if (built)
      built = set_own(state, "weaponAnimationBase",
                      qa_save_value_number(s->weapon_animation_base));
    if (built)
      built = set_own(state, "continuousFiring",
                      qa_save_value_boolean(s->continuous_firing));
    if (built)
      built = set_own(state, "nextWeaponFrame",
                      qa_save_value_number(s->next_weapon_frame));
    if (built)
      built = set_own(state, "lightningSoundAt",
                      qa_save_value_number(s->lightning_sound_at));
    if (built) built = set_own(state, "punchAngles", vec_value(s->punch_angles));
    if (built)
      built = set_own(state, "nailSide", qa_save_value_number(s->nail_side));
    if (built)
      built = set_own(state, "maxHealth", qa_save_value_number(s->max_health));
    if (built)
      built = set_own(state, "megaRotAt", qa_save_value_number(s->mega_rot_at));
    if (built)
      built = set_own(state, "hostileUntil",
                      qa_save_value_number(s->hostile_until));
    if (built) built = set_own(state, "viewAngles", vec_value(s->view_angles));
    if (built)
      built = set_own(state, "waterLevel", qa_save_value_number(s->water_level));
    if (built)
      built = set_own(state, "airFinished",
                      qa_save_value_number(s->air_finished));
    if (built)
      built = set_own(state, "drownDamage",
                      qa_save_value_number(s->drown_damage));
    if (built)
      built = set_own(state, "drownAt", qa_save_value_number(s->drown_at));
    if (built)
      built = set_own(state, "hazardAt", qa_save_value_number(s->hazard_at));
    if (built) built = set_own(state, "autoSwitch", str_value(s->auto_switch));
    if (!built) qa_save_value_free(state);
    ok = built && set_own(obj, "state", state);
  }
  if (ok) {
    qa_save_value *powerups = qa_save_value_array();
    bool built = powerups != NULL;
    for (size_t i = 0; built && i < in->powerup_count; i++) {
      qa_save_value *entry = qa_save_value_object();
      built = entry != NULL &&
              set_own(entry, "kind", str_value(in->powerups[i].kind)) &&
              set_own(entry, "expires",
                      qa_save_value_number(in->powerups[i].expires));
      if (!built) qa_save_value_free(entry);
      else built = push_own(powerups, entry);
    }
    if (!built) qa_save_value_free(powerups);
    ok = built && set_own(obj, "powerups", powerups);
  }
  if (!ok) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}

static qa_save_value *build_string_list(const char *const *values, size_t count) {
  qa_save_value *array = qa_save_value_array();
  if (array == NULL) return NULL;
  for (size_t i = 0; i < count; i++) {
    if (!push_own(array, str_value(values[i]))) {
      qa_save_value_free(array);
      return NULL;
    }
  }
  return array;
}

static qa_save_value *build_checkpoint(
    const qa_q1_foundation_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = true;
  SET(obj, "format", str_value(in->format));
  SET(obj, "provider", str_value(in->provider));
  SET(obj, "version", qa_save_value_number(in->version));
  if (ok) {
    qa_save_value *precaches = qa_save_value_object();
    bool built = precaches != NULL &&
                 set_own(precaches, "phase", str_value(in->precaches.phase)) &&
                 set_own(precaches, "models",
                         build_string_list(in->precaches.models,
                                           in->precaches.model_count)) &&
                 set_own(precaches, "sounds",
                         build_string_list(in->precaches.sounds,
                                           in->precaches.sound_count));
    if (!built) qa_save_value_free(precaches);
    ok = built && set_own(obj, "precaches", precaches);
  }
  SET(obj, "edition", str_value(in->edition));
  SET(obj, "time", qa_save_value_number(in->time));
  SET(obj, "frameSeconds", qa_save_value_number(in->frame_seconds));
  SET(obj, "forceRetouch", qa_save_value_number(in->force_retouch));
  if (ok) {
    qa_save_value *basis = qa_save_value_object();
    bool built = basis != NULL &&
                 set_own(basis, "forward", vec_value(in->basis.forward)) &&
                 set_own(basis, "right", vec_value(in->basis.right)) &&
                 set_own(basis, "up", vec_value(in->basis.up));
    if (!built) qa_save_value_free(basis);
    ok = built && set_own(obj, "basis", basis);
  }
  SET(obj, "sequence", qa_save_value_number(in->sequence));
  SET(obj, "nextDynamicSlot", qa_save_value_number(in->next_dynamic_slot));
  SET(obj, "totalSecrets", qa_save_value_number(in->total_secrets));
  SET(obj, "foundSecrets", qa_save_value_number(in->found_secrets));
  SET(obj, "totalMonsters", qa_save_value_number(in->total_monsters));
  SET(obj, "killedMonsters", qa_save_value_number(in->killed_monsters));
  SET(obj, "worldType", qa_save_value_number(in->world_type));
  SET(obj, "mapName", str_value(in->map_name));
  if (ok) ok = set_own(obj, "world", nullable_actor(in->has_world, in->world));
  if (ok)
    ok = set_own(obj, "sightEntity",
                 nullable_actor(in->has_sight_entity, in->sight_entity));
  SET(obj, "sightTime", qa_save_value_number(in->sight_time));
  if (ok) {
    qa_save_value *intermission = NULL;
    if (in->has_intermission) {
      intermission = qa_save_value_object();
      bool built =
          intermission != NULL &&
          set_own(intermission, "map", str_value(in->intermission.map)) &&
          set_own(intermission, "cause",
                  nullable_actor(in->intermission.has_cause,
                                 in->intermission.cause)) &&
          set_own(intermission, "exitAfter",
                  qa_save_value_number(in->intermission.exit_after));
      if (!built) {
        qa_save_value_free(intermission);
        intermission = NULL;
        ok = false;
      }
    } else {
      intermission = qa_save_value_null();
      ok = intermission != NULL;
    }
    if (ok) ok = set_own(obj, "intermission", intermission);
  }
  if (ok) {
    qa_save_value *entities = qa_save_value_array();
    bool built = entities != NULL;
    for (size_t i = 0; built && i < in->entity_count; i++)
      built = push_own(entities, build_entity(&in->entities[i]));
    if (!built) qa_save_value_free(entities);
    ok = built && set_own(obj, "entities", entities);
  }
  if (ok) {
    qa_save_value *players = qa_save_value_array();
    bool built = players != NULL;
    for (size_t i = 0; built && i < in->player_count; i++)
      built = push_own(players, build_player(&in->players[i]));
    if (!built) qa_save_value_free(players);
    ok = built && set_own(obj, "players", players);
  }
  if (ok) {
    qa_save_value *extensions = qa_save_value_array();
    bool built = extensions != NULL;
    for (size_t i = 0; built && i < in->extension_count; i++) {
      qa_save_value *entry = qa_save_value_object();
      built = entry != NULL &&
              set_own(entry, "id", str_value(in->extensions[i].id)) &&
              set_own(entry, "bytes",
                      qa_save_value_bytes(in->extensions[i].bytes.bytes,
                                          in->extensions[i].bytes.length));
      if (!built) qa_save_value_free(entry);
      else built = push_own(extensions, entry);
    }
    if (!built) qa_save_value_free(extensions);
    ok = built && set_own(obj, "extensions", extensions);
  }
  if (!ok) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
#undef SET

uint8_t *qa_encode_q1_foundation_checkpoint(
    const qa_q1_foundation_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}

bool qa_decode_q1_foundation_checkpoint(const uint8_t *bytes, size_t length,
                                        qa_q1_foundation_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q1-foundation")) {
    qa_save_value_free(root);
    return false;
  }
  *out = qa_read_q1_foundation_checkpoint(&reader);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

void qa_q1_foundation_checkpoint_free(qa_q1_foundation_checkpoint *checkpoint) {
  if (checkpoint == NULL) return;
  free(checkpoint->precaches.models);
  free(checkpoint->precaches.sounds);
  for (size_t i = 0; i < checkpoint->entity_count; i++) {
    qa_q1_saved_entity *entity = &checkpoint->entities[i];
    free(entity->fields);
    free(entity->references);
    free(entity->door_group);
    free(entity->monster.sequence);
  }
  free(checkpoint->entities);
  for (size_t i = 0; i < checkpoint->player_count; i++)
    free(checkpoint->players[i].powerups);
  free(checkpoint->players);
  free(checkpoint->extensions);
  qa_save_value_free(checkpoint->root);
  memset(checkpoint, 0, sizeof *checkpoint);
}
