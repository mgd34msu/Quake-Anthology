/* Port of src/content/q1/base/creatures.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/creatures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/base/frames.h"
#include "qa/content/q1/base/grunt.h"
#include "qa/content/q1/base/monster_actions.h"
#include "qa/content/q1/base/species.h"
#include "qa/foundation/support.h"

typedef struct monster_entry { qa_owned_actor actor; qa_q1_base_monster *monster; } monster_entry;
typedef struct backpack_entry {
  qa_owned_actor actor;
  qa_q1_backpack_contents contents;
  char *weapon_copy; char *selection_copy;
  qa_q1_backpack_extra *extra_copy; char **extra_items;
} backpack_entry;
typedef struct target_entry { qa_owned_actor actor; qa_actor_id enemy; } target_entry;
typedef struct shot_entry { qa_owned_actor actor; qa_q1_wizard_shot shot; } shot_entry;

struct qa_q1_creatures {
  qa_q1_services *game;
  qa_q1_monster_services services;
  monster_entry *monsters; size_t monster_count; size_t monster_capacity;
  backpack_entry *backpacks; size_t backpack_count; size_t backpack_capacity;
  target_entry *targets; size_t target_count; size_t target_capacity;
  shot_entry *shots; size_t shot_count; size_t shot_capacity;
  double hell_knight_type;
  /* Controllers dropped from the map: TS GC keeps a controller alive
   * while its own callback is still running (die -> remove -> release),
   * so they are freed with the creatures, not at release. */
  qa_q1_base_monster **retired; size_t retired_count; size_t retired_capacity;
  int64_t release_listener;
  /* Registration closures (TS GC-owned): freed with the creatures. */
  void **owned; size_t owned_count; size_t owned_capacity;
};

/* TS WeakMap<Q1EntityServices, Q1Creatures>. */
typedef struct registration { qa_q1_services *game; qa_q1_creatures *creatures; } registration;
static registration REGISTRY[64];
static size_t REGISTRY_COUNT;

static char *dup_or_null(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  return copy;
}
static void *grow(void *array, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return array;
  size_t next = *capacity == 0 ? 8 : *capacity * 2;
  void *grown = realloc(array, next * size);
  if (grown == NULL) qa_range_error("out of memory");
  *capacity = next;
  return grown;
}

qa_q1_creatures *qa_q1_creatures_for(qa_q1_services *game) {
  for (size_t i = 0; i < REGISTRY_COUNT; i++)
    if (REGISTRY[i].game == game) return REGISTRY[i].creatures;
  qa_range_error("Q1 creature services were not registered");
}
qa_q1_services *qa_q1_creatures_game(qa_q1_creatures *creatures) { return creatures->game; }

static qa_q1_base_monster *monster_lookup(void *ctx, qa_q1_actor *entity) {
  qa_q1_creatures *creatures = ctx;
  qa_q1_base_monster *monster = qa_q1_creatures_monsters_get(creatures, entity->actor);
  if (monster == NULL) qa_range_error("Missing Q1 monster controller for %s", entity->classname);
  return monster;
}
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q1_creatures *creatures = ctx;
  qa_q1_creatures_monsters_delete(creatures, actor);
  qa_q1_creatures_backpacks_delete(creatures, actor);
  for (size_t i = 0; i < creatures->target_count; i++) {
    if (qa_same_owned_actor(creatures->targets[i].actor, actor)) {
      memmove(&creatures->targets[i], &creatures->targets[i + 1],
              (creatures->target_count - i - 1) * sizeof *creatures->targets);
      creatures->target_count--;
      break;
    }
  }
  for (size_t i = 0; i < creatures->shot_count; i++) {
    if (qa_same_owned_actor(creatures->shots[i].actor, actor)) {
      memmove(&creatures->shots[i], &creatures->shots[i + 1],
              (creatures->shot_count - i - 1) * sizeof *creatures->shots);
      creatures->shot_count--;
      break;
    }
  }
}
static void wizard_fastfire_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_wizard_fast_fire(game, entity);
}
static void retain(qa_q1_creatures *creatures, void *pointer) {
  creatures->owned = grow(creatures->owned, &creatures->owned_capacity, creatures->owned_count,
                          sizeof *creatures->owned);
  creatures->owned[creatures->owned_count++] = pointer;
}
qa_q1_creatures *qa_q1_creatures_create(qa_q1_services *game, qa_q1_monster_services services) {
  for (size_t i = 0; i < REGISTRY_COUNT; i++)
    if (REGISTRY[i].game == game) qa_range_error("Q1 creature services already registered");
  qa_q1_creatures *creatures = malloc(sizeof *creatures);
  if (creatures == NULL) return NULL;
  memset(creatures, 0, sizeof *creatures);
  creatures->game = game;
  creatures->services = services;
  if (REGISTRY_COUNT < 64) {
    REGISTRY[REGISTRY_COUNT].game = game;
    REGISTRY[REGISTRY_COUNT].creatures = creatures;
    REGISTRY_COUNT++;
  }
  creatures->release_listener = qa_session_actor_registry_on_release(game->host.actors, on_release, creatures);
  retain(creatures, qa_q1_register_monster_callbacks(game, "base", monster_lookup, creatures));
  retain(creatures, qa_q1_register_legacy_monster_callbacks(game, monster_lookup, creatures));
  qa_q1_register_projectile_callbacks(game);
  qa_q1_callback_registry_register(game->named, "base:wizard_fastfire",
                                   (qa_q1_callback_handlers){.action = wizard_fastfire_action});
  return creatures;
}
static void backpack_entry_clear(backpack_entry *entry) {
  free(entry->weapon_copy);
  free(entry->selection_copy);
  if (entry->extra_items != NULL)
    for (size_t i = 0; i < entry->contents.extra_count; i++) free(entry->extra_items[i]);
  free(entry->extra_items);
  free(entry->extra_copy);
  memset(entry, 0, sizeof *entry);
}
void qa_q1_creatures_destroy(qa_q1_creatures *creatures) {
  if (creatures == NULL) return;
  for (size_t i = 0; i < REGISTRY_COUNT; i++) {
    if (REGISTRY[i].creatures == creatures) {
      memmove(&REGISTRY[i], &REGISTRY[i + 1], (REGISTRY_COUNT - i - 1) * sizeof *REGISTRY);
      REGISTRY_COUNT--;
      break;
    }
  }
  qa_session_actor_registry_off_release(creatures->game->host.actors, creatures->release_listener);
  for (size_t i = 0; i < creatures->monster_count; i++) qa_q1_base_monster_destroy(creatures->monsters[i].monster);
  for (size_t i = 0; i < creatures->retired_count; i++) qa_q1_base_monster_destroy(creatures->retired[i]);
  for (size_t i = 0; i < creatures->backpack_count; i++) backpack_entry_clear(&creatures->backpacks[i]);
  free(creatures->retired);
  for (size_t i = 0; i < creatures->owned_count; i++) free(creatures->owned[i]);
  free(creatures->owned);
  free(creatures->monsters);
  free(creatures->backpacks);
  free(creatures->targets);
  free(creatures->shots);
  free(creatures);
}

static bool services_count(void *ctx, qa_q1_base_monster *monster) {
  return qa_q1_creatures_count_monster_kill(ctx, monster);
}
static const char *services_melee(void *ctx) { return qa_q1_creatures_next_hell_knight_melee(ctx); }
static void services_finale(void *ctx, qa_q1_base_monster *monster) {
  qa_q1_creatures_finale(ctx, monster);
}
static void services_finish(void *ctx, qa_q1_base_monster *monster) {
  qa_q1_creatures_finish_finale(ctx, monster);
}
qa_q1_monster_services qa_q1_creatures_as_services(qa_q1_creatures *creatures) {
  qa_q1_monster_services services = {
      .ctx = creatures, .count_monster_kill = services_count,
      .next_hell_knight_melee = services_melee, .finale = services_finale,
      .finish_finale = services_finish};
  return services;
}
bool qa_q1_creatures_count_monster_kill(qa_q1_creatures *creatures, qa_q1_base_monster *monster) {
  return creatures->services.count_monster_kill(creatures->services.ctx, monster);
}
void qa_q1_creatures_finale(qa_q1_creatures *creatures, qa_q1_base_monster *monster) {
  creatures->services.finale(creatures->services.ctx, monster);
}
void qa_q1_creatures_finish_finale(qa_q1_creatures *creatures, qa_q1_base_monster *monster) {
  creatures->services.finish_finale(creatures->services.ctx, monster);
}
const char *qa_q1_creatures_next_hell_knight_melee(qa_q1_creatures *creatures) {
  creatures->hell_knight_type++;
  if (creatures->hell_knight_type == 1) return "hknight_slice1";
  if (creatures->hell_knight_type == 2) return "hknight_smash1";
  creatures->hell_knight_type = 0;
  return "hknight_watk1";
}

typedef struct species_spawn { qa_q1_creatures *creatures; const qa_q1_base_monster_species *spec; } species_spawn;
static void spawn_species(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  species_spawn *spawn = ctx;
  qa_q1_creatures *creatures = spawn->creatures;
  qa_q1_base_monster *monster = qa_q1_base_monster_create(
      creatures->game, entity, spawn->spec, qa_q1_creatures_as_services(creatures), NULL);
  if (monster == NULL) qa_range_error("out of memory");
  qa_q1_creatures_monsters_set(creatures, entity->actor, monster);
  qa_q1_base_monster_spawn(monster);
}
void qa_q1_creatures_register_species(qa_q1_creatures *creatures,
                                      const qa_q1_base_monster_species *species, size_t count) {
  for (size_t s = 0; s < count; s++) {
    for (size_t c = 0; c < species[s].classname_count; c++) {
      species_spawn *spawn = malloc(sizeof *spawn);
      if (spawn == NULL) qa_range_error("out of memory");
      spawn->creatures = creatures;
      spawn->spec = &species[s];
      retain(creatures, spawn);
      qa_q1_register_spawn(creatures->game, species[s].classnames[c], spawn_species, spawn);
    }
  }
}

qa_q1_base_monster *qa_q1_creatures_monsters_get(qa_q1_creatures *creatures, qa_owned_actor actor) {
  for (size_t i = 0; i < creatures->monster_count; i++)
    if (qa_same_owned_actor(creatures->monsters[i].actor, actor)) return creatures->monsters[i].monster;
  return NULL;
}
static void retire(qa_q1_creatures *creatures, qa_q1_base_monster *monster) {
  if (monster == NULL) return;
  creatures->retired = grow(creatures->retired, &creatures->retired_capacity, creatures->retired_count,
                            sizeof *creatures->retired);
  creatures->retired[creatures->retired_count++] = monster;
}
void qa_q1_creatures_monsters_set(qa_q1_creatures *creatures, qa_owned_actor actor, qa_q1_base_monster *monster) {
  for (size_t i = 0; i < creatures->monster_count; i++) {
    if (qa_same_owned_actor(creatures->monsters[i].actor, actor)) {
      if (creatures->monsters[i].monster != monster) retire(creatures, creatures->monsters[i].monster);
      creatures->monsters[i].monster = monster;
      return;
    }
  }
  creatures->monsters = grow(creatures->monsters, &creatures->monster_capacity,
                              creatures->monster_count, sizeof *creatures->monsters);
  creatures->monsters[creatures->monster_count].actor = actor;
  creatures->monsters[creatures->monster_count].monster = monster;
  creatures->monster_count++;
}
void qa_q1_creatures_monsters_delete(qa_q1_creatures *creatures, qa_owned_actor actor) {
  for (size_t i = 0; i < creatures->monster_count; i++) {
    if (qa_same_owned_actor(creatures->monsters[i].actor, actor)) {
      retire(creatures, creatures->monsters[i].monster);
      memmove(&creatures->monsters[i], &creatures->monsters[i + 1],
              (creatures->monster_count - i - 1) * sizeof *creatures->monsters);
      creatures->monster_count--;
      return;
    }
  }
}
size_t qa_q1_creatures_monsters_list(qa_q1_creatures *creatures, qa_q1_base_monster **out, size_t capacity) {
  return qa_q1_creatures_monsters_list_from(creatures, 0, out, capacity);
}
size_t qa_q1_creatures_monsters_list_from(qa_q1_creatures *creatures, size_t first,
                                          qa_q1_base_monster **out, size_t capacity) {
  if (first >= creatures->monster_count) return 0;
  size_t available = creatures->monster_count - first;
  size_t count = available < capacity ? available : capacity;
  for (size_t i = 0; i < count; i++) out[i] = creatures->monsters[first + i].monster;
  return count;
}
const qa_q1_backpack_contents *qa_q1_creatures_backpacks_get(qa_q1_creatures *creatures, qa_owned_actor actor) {
  for (size_t i = 0; i < creatures->backpack_count; i++)
    if (qa_same_owned_actor(creatures->backpacks[i].actor, actor)) return &creatures->backpacks[i].contents;
  return NULL;
}
void qa_q1_creatures_backpacks_set(qa_q1_creatures *creatures, qa_owned_actor actor,
                                   const qa_q1_backpack_contents *contents) {
  backpack_entry *entry = NULL;
  for (size_t i = 0; i < creatures->backpack_count; i++) {
    if (qa_same_owned_actor(creatures->backpacks[i].actor, actor)) {
      backpack_entry_clear(&creatures->backpacks[i]);
      creatures->backpacks[i].actor = actor;
      entry = &creatures->backpacks[i];
      break;
    }
  }
  if (entry == NULL) {
    creatures->backpacks = grow(creatures->backpacks, &creatures->backpack_capacity,
                                 creatures->backpack_count, sizeof *creatures->backpacks);
    entry = &creatures->backpacks[creatures->backpack_count++];
    memset(entry, 0, sizeof *entry);
    entry->actor = actor;
  }
  entry->contents = *contents;
  entry->weapon_copy = dup_or_null(contents->weapon);
  entry->contents.weapon = entry->weapon_copy;
  entry->selection_copy = dup_or_null(contents->selection);
  entry->contents.selection = entry->selection_copy;
  if (contents->has_extra && contents->extra_count > 0) {
    entry->extra_copy = malloc(contents->extra_count * sizeof *entry->extra_copy);
    entry->extra_items = malloc(contents->extra_count * sizeof *entry->extra_items);
    if (entry->extra_copy == NULL || entry->extra_items == NULL) qa_range_error("out of memory");
    for (size_t i = 0; i < contents->extra_count; i++) {
      entry->extra_items[i] = dup_or_null(contents->extra[i].item);
      entry->extra_copy[i].item = entry->extra_items[i];
      entry->extra_copy[i].count = contents->extra[i].count;
    }
    entry->contents.extra = entry->extra_copy;
  } else {
    entry->contents.extra = entry->extra_copy;
  }
}
void qa_q1_creatures_backpacks_delete(qa_q1_creatures *creatures, qa_owned_actor actor) {
  for (size_t i = 0; i < creatures->backpack_count; i++) {
    if (qa_same_owned_actor(creatures->backpacks[i].actor, actor)) {
      backpack_entry_clear(&creatures->backpacks[i]);
      memmove(&creatures->backpacks[i], &creatures->backpacks[i + 1],
              (creatures->backpack_count - i - 1) * sizeof *creatures->backpacks);
      creatures->backpack_count--;
      return;
    }
  }
}
bool qa_q1_creatures_targets_get(qa_q1_creatures *creatures, qa_owned_actor actor, qa_actor_id *out) {
  for (size_t i = 0; i < creatures->target_count; i++) {
    if (qa_same_owned_actor(creatures->targets[i].actor, actor)) { *out = creatures->targets[i].enemy; return true; }
  }
  return false;
}
void qa_q1_creatures_targets_set(qa_q1_creatures *creatures, qa_owned_actor actor, qa_actor_id enemy) {
  for (size_t i = 0; i < creatures->target_count; i++) {
    if (qa_same_owned_actor(creatures->targets[i].actor, actor)) { creatures->targets[i].enemy = enemy; return; }
  }
  creatures->targets = grow(creatures->targets, &creatures->target_capacity,
                            creatures->target_count, sizeof *creatures->targets);
  creatures->targets[creatures->target_count].actor = actor;
  creatures->targets[creatures->target_count].enemy = enemy;
  creatures->target_count++;
}
const qa_q1_wizard_shot *qa_q1_creatures_shots_get(qa_q1_creatures *creatures, qa_owned_actor actor) {
  for (size_t i = 0; i < creatures->shot_count; i++)
    if (qa_same_owned_actor(creatures->shots[i].actor, actor)) return &creatures->shots[i].shot;
  return NULL;
}
void qa_q1_creatures_shots_set(qa_q1_creatures *creatures, qa_owned_actor actor, qa_q1_wizard_shot shot) {
  for (size_t i = 0; i < creatures->shot_count; i++) {
    if (qa_same_owned_actor(creatures->shots[i].actor, actor)) { creatures->shots[i].shot = shot; return; }
  }
  creatures->shots = grow(creatures->shots, &creatures->shot_capacity,
                          creatures->shot_count, sizeof *creatures->shots);
  creatures->shots[creatures->shot_count].actor = actor;
  creatures->shots[creatures->shot_count].shot = shot;
  creatures->shot_count++;
}

void qa_q1_creatures_clone(qa_q1_creatures *creatures, qa_q1_actor *source, qa_q1_actor *target) {
  qa_q1_base_monster *original = qa_q1_creatures_monsters_get(creatures, source->actor);
  if (original != NULL) {
    const qa_q1_base_monster_source *source_arg = original->has_source ? &original->source : NULL;
    qa_q1_base_monster *monster = qa_q1_base_monster_create(
        creatures->game, target, original->spec, qa_q1_creatures_as_services(creatures), source_arg);
    if (monster == NULL) qa_range_error("out of memory");
    qa_save_value *captured = qa_q1_base_monster_capture(original);
    qa_save_reader reader;
    if (!qa_save_reader_init(&reader, captured, "monster")) qa_range_error("out of memory");
    qa_q1_base_monster_restore(monster, &reader);
    qa_save_reader_destroy(&reader);
    qa_save_value_free(captured);
    qa_q1_creatures_monsters_set(creatures, target->actor, monster);
  }
  const qa_q1_backpack_contents *backpack = qa_q1_creatures_backpacks_get(creatures, source->actor);
  if (backpack != NULL) qa_q1_creatures_backpacks_set(creatures, target->actor, backpack);
  qa_actor_id enemy;
  if (qa_q1_creatures_targets_get(creatures, source->actor, &enemy))
    qa_q1_creatures_targets_set(creatures, target->actor, enemy);
  const qa_q1_wizard_shot *shot = qa_q1_creatures_shots_get(creatures, source->actor);
  if (shot != NULL) qa_q1_creatures_shots_set(creatures, target->actor, *shot);
}

static bool set_owned(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
static bool push_owned(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) { qa_save_value_free(child); return false; }
  return true;
}
static qa_save_value *saved_actor(qa_actor_id actor) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  bool ok = set_owned(object, "slot", qa_save_value_number((double)actor.slot));
  ok = ok && set_owned(object, "generation", qa_save_value_number((double)actor.generation));
  if (!ok) { qa_save_value_free(object); return NULL; }
  return object;
}
bool qa_q1_creatures_capture_fields(qa_q1_creatures *creatures, qa_save_value *root) {
  bool ok = set_owned(root, "hellKnightType", qa_save_value_number(creatures->hell_knight_type));
  qa_save_value *monsters = qa_save_value_array();
  if (monsters == NULL) return false;
  for (size_t i = 0; ok && i < creatures->monster_count; i++) {
    qa_q1_base_monster *monster = creatures->monsters[i].monster;
    qa_save_value *entry = qa_save_value_object();
    if (entry == NULL) { ok = false; break; }
    ok = set_owned(entry, "actor", saved_actor(monster->entity->actor.id));
    ok = ok && set_owned(entry, "state", qa_q1_base_monster_capture(monster));
    if (ok) ok = push_owned(monsters, entry); else qa_save_value_free(entry);
  }
  ok = ok && set_owned(root, "monsters", monsters);
  if (!ok) return false;
  qa_save_value *backpacks = qa_save_value_array();
  if (backpacks == NULL) return false;
  for (size_t i = 0; ok && i < creatures->backpack_count; i++) {
    const backpack_entry *source = &creatures->backpacks[i];
    const qa_q1_backpack_contents *contents = &source->contents;
    qa_save_value *data = qa_save_value_object();
    if (data == NULL) { ok = false; break; }
    const char *selection = contents->has_selection ? contents->selection : "source-default";
    bool avoid = contents->has_avoid_underwater_lightning ? contents->avoid_underwater_lightning
                 : strcmp(creatures->game->options.edition, "rerelease") == 0;
    double delay = contents->has_owner_pickup_delay ? contents->owner_pickup_delay : 0;
    ok = set_owned(data, "weapon", contents->weapon != NULL ? qa_save_value_string(contents->weapon) : qa_save_value_null());
    ok = ok && set_owned(data, "shells", qa_save_value_number(contents->shells));
    ok = ok && set_owned(data, "nails", qa_save_value_number(contents->nails));
    ok = ok && set_owned(data, "rockets", qa_save_value_number(contents->rockets));
    ok = ok && set_owned(data, "cells", qa_save_value_number(contents->cells));
    qa_save_value *extra = qa_save_value_array();
    if (extra == NULL) ok = false;
    if (ok && contents->has_extra) {
      for (size_t e = 0; ok && e < contents->extra_count; e++) {
        qa_save_value *item = qa_save_value_object();
        if (item == NULL) { ok = false; break; }
        ok = set_owned(item, "item", qa_save_value_string(contents->extra[e].item));
        ok = ok && set_owned(item, "count", qa_save_value_number(contents->extra[e].count));
        if (ok) ok = push_owned(extra, item); else qa_save_value_free(item);
      }
    }
    ok = ok && set_owned(data, "extra", extra);
    ok = ok && set_owned(data, "selection", qa_save_value_string(selection));
    ok = ok && set_owned(data, "avoidUnderwaterLightning", qa_save_value_boolean(avoid));
    ok = ok && set_owned(data, "ownerPickupDelay", qa_save_value_number(delay));
    qa_save_value *entry = qa_save_value_object();
    if (entry == NULL) { qa_save_value_free(data); ok = false; break; }
    ok = ok && set_owned(entry, "actor", saved_actor(source->actor.id));
    ok = ok && set_owned(entry, "contents", data);
    if (!ok) qa_save_value_free(entry);
    else ok = push_owned(backpacks, entry);
  }
  ok = ok && set_owned(root, "backpacks", backpacks);
  if (!ok) return false;
  qa_save_value *targets = qa_save_value_array();
  if (targets == NULL) return false;
  for (size_t i = 0; ok && i < creatures->target_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    if (entry == NULL) { ok = false; break; }
    ok = set_owned(entry, "actor", saved_actor(creatures->targets[i].actor.id));
    ok = ok && set_owned(entry, "enemy", saved_actor(creatures->targets[i].enemy));
    if (ok) ok = push_owned(targets, entry); else qa_save_value_free(entry);
  }
  ok = ok && set_owned(root, "targets", targets);
  if (!ok) return false;
  qa_save_value *shots = qa_save_value_array();
  if (shots == NULL) return false;
  for (size_t i = 0; ok && i < creatures->shot_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    qa_save_value *right = qa_save_value_object();
    if (entry == NULL || right == NULL) { qa_save_value_free(entry); qa_save_value_free(right); ok = false; break; }
    ok = set_owned(right, "x", qa_save_value_number(creatures->shots[i].shot.right.x));
    ok = ok && set_owned(right, "y", qa_save_value_number(creatures->shots[i].shot.right.y));
    ok = ok && set_owned(right, "z", qa_save_value_number(creatures->shots[i].shot.right.z));
    ok = ok && set_owned(entry, "actor", saved_actor(creatures->shots[i].actor.id));
    ok = ok && set_owned(entry, "enemy", saved_actor(creatures->shots[i].shot.enemy));
    ok = ok && set_owned(entry, "right", right);
    if (ok) ok = push_owned(shots, entry); else qa_save_value_free(entry);
  }
  ok = ok && set_owned(root, "shots", shots);
  return ok;
}

static void must_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}
static double field_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  must_field(reader, name, &field);
  double value = qa_save_reader_number(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static bool field_boolean(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  must_field(reader, name, &field);
  bool value = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static double saved_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  must_field(reader, name, &field);
  double value = qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  return value;
}
static qa_owned_actor restore_actor(qa_q1_creatures *creatures, const qa_save_reader *reader) {
  int64_t slot = (int64_t)saved_integer(reader, "slot");
  int64_t generation = (int64_t)saved_integer(reader, "generation");
  qa_owned_actor actor;
  const qa_saved_actor_id saved = {(double)slot, (double)generation};
  if (!qa_session_actor_registry_resolve_saved(creatures->game->host.actors, saved, &actor))
    qa_save_reader_fail(reader, "missing saved actor");
  return actor;
}
static qa_actor_id restore_reference(qa_q1_creatures *creatures, const qa_save_reader *reader) {
  int64_t slot = (int64_t)saved_integer(reader, "slot");
  int64_t generation = (int64_t)saved_integer(reader, "generation");
  const qa_saved_actor_id saved = {(double)slot, (double)generation};
  return qa_session_actor_registry_reference_saved(creatures->game->host.actors, saved, "checkpoint");
}
static const qa_q1_base_monster_species *species_for_classname(const char *classname) {
  for (size_t s = 0; s < QA_Q1_BASE_SPECIES_COUNT; s++)
    for (size_t c = 0; c < QA_Q1_BASE_SPECIES[s].classname_count; c++)
      if (strcmp(QA_Q1_BASE_SPECIES[s].classnames[c], classname) == 0) return &QA_Q1_BASE_SPECIES[s];
  return NULL;
}
/* Takes over a version 5 grunt or dog: its sequence cursor becomes the equivalent source frame. */
static void adopt_legacy_monsters(qa_q1_creatures *creatures, const qa_owned_actor *restored, size_t restored_count) {
  qa_q1_services *game = creatures->game;
  for (size_t i = 0; i < game->legacy_animation_count; i++) {
    const qa_owned_actor owner = game->legacy_animations[i].owner;
    const qa_q1_legacy_animation animation = game->legacy_animations[i].animation;
    bool seen = false;
    for (size_t r = 0; r < restored_count && !seen; r++) seen = qa_same_owned_actor(restored[r], owner);
    if (seen) continue;
    qa_q1_actor *entity = qa_q1_get_entity(game, &owner.id);
    const qa_q1_base_monster_species *spec = entity == NULL ? NULL : species_for_classname(entity->classname);
    if (entity == NULL || spec == NULL || (strcmp(spec->species, "army") != 0 && strcmp(spec->species, "dog") != 0))
      qa_range_error("Saved version 5 monster is not a grunt or dog");
    char frame[64];
    if (!qa_q1_legacy_grunt_frame(spec->species, animation.mode, animation.first_frame, animation.frame_index, frame,
                                  sizeof frame) ||
        !qa_q1_monster_frames_has(frame))
      qa_range_error("Saved %s has an unknown animation cursor", entity->classname);
    qa_q1_base_monster *monster = qa_q1_base_monster_create(game, entity, spec, qa_q1_creatures_as_services(creatures), NULL);
    if (monster == NULL) qa_range_error("out of memory");
    (void)snprintf(monster->current_frame, sizeof monster->current_frame, "%s", frame);
    (void)snprintf(monster->next_frame, sizeof monster->next_frame, "%s", frame);
    monster->counted_death = strcmp(animation.mode, "death") == 0;
    qa_q1_creatures_monsters_set(creatures, owner, monster);
  }
  game->legacy_animation_count = 0;
}
void qa_q1_creatures_restore_fields(qa_q1_creatures *creatures, const qa_save_reader *root) {
  qa_q1_services *game = creatures->game;
  creatures->hell_knight_type = field_number(root, "hellKnightType");
  for (size_t i = 0; i < creatures->monster_count; i++) retire(creatures, creatures->monsters[i].monster);
  creatures->monster_count = 0;
  for (size_t i = 0; i < creatures->backpack_count; i++) backpack_entry_clear(&creatures->backpacks[i]);
  creatures->backpack_count = 0;
  creatures->target_count = 0;
  creatures->shot_count = 0;
  qa_save_reader list;
  must_field(root, "monsters", &list);
  size_t count = qa_save_reader_list_count(&list);
  qa_owned_actor *restored = malloc((count ? count : 1) * sizeof *restored);
  if (restored == NULL) qa_range_error("out of memory");
  qa_cleanup restored_cleanup; /* a bad saved monster raises out of the load */
  qa_cleanup_push(&restored_cleanup, free, restored);
  size_t restored_count = 0;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, actor_reader, state;
    if (!qa_save_reader_item(&list, i, &entry)) qa_range_error("out of memory");
    must_field(&entry, "actor", &actor_reader);
    qa_owned_actor owner = restore_actor(creatures, &actor_reader);
    qa_save_reader_destroy(&actor_reader);
    qa_q1_actor *entity = qa_q1_get_entity(game, &owner.id);
    if (entity == NULL) qa_save_reader_fail(&entry, "missing source monster entity");
    const qa_q1_base_monster_species *spec = species_for_classname(entity->classname);
    if (spec == NULL) qa_save_reader_fail(&entry, "unknown base monster");
    qa_q1_base_monster *monster = qa_q1_base_monster_create(game, entity, spec,
                                                            qa_q1_creatures_as_services(creatures), NULL);
    if (monster == NULL) qa_range_error("out of memory");
    must_field(&entry, "state", &state);
    qa_q1_base_monster_restore(monster, &state);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&entry);
    qa_q1_creatures_monsters_set(creatures, owner, monster);
    restored[restored_count++] = owner;
  }
  qa_save_reader_destroy(&list);
  adopt_legacy_monsters(creatures, restored, restored_count);
  qa_cleanup_pop(&restored_cleanup);
  free(restored);
  must_field(root, "backpacks", &list);
  count = qa_save_reader_list_count(&list);
  /* Weapon choice list: WEAPONS then registered weapon keys. */
  const char *choices[72];
  size_t choice_count = 0;
  for (size_t i = 0; i < 8; i++) choices[choice_count++] = QA_Q1_WEAPONS[i];
  for (size_t i = 0; i < game->registered_weapon_count && choice_count < 72; i++)
    choices[choice_count++] = game->registered_weapons[i].def->id;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, actor_reader, data, field, extra_list;
    if (!qa_save_reader_item(&list, i, &entry)) qa_range_error("out of memory");
    must_field(&entry, "actor", &actor_reader);
    qa_owned_actor owner = restore_actor(creatures, &actor_reader);
    qa_save_reader_destroy(&actor_reader);
    must_field(&entry, "contents", &data);
    qa_q1_backpack_contents contents = {0};
    must_field(&data, "weapon", &field);
    contents.weapon = qa_save_reader_is_null(&field)
                          ? NULL : qa_save_reader_choice_string(&field, choices, choice_count);
    qa_save_reader_destroy(&field);
    contents.shells = field_number(&data, "shells");
    contents.nails = field_number(&data, "nails");
    contents.rockets = field_number(&data, "rockets");
    contents.cells = field_number(&data, "cells");
    must_field(&data, "extra", &extra_list);
    size_t extra_count = qa_save_reader_list_count(&extra_list);
    /* TS keeps every saved extra entry (a fixed 32 dropped the rest). */
    qa_q1_backpack_extra *extras = malloc((extra_count != 0 ? extra_count : 1) * sizeof *extras);
    if (extras == NULL) qa_range_error("out of memory");
    qa_cleanup extras_cleanup;
    qa_cleanup_push(&extras_cleanup, free, extras);
    const size_t stored_extra = extra_count;
    for (size_t e = 0; e < stored_extra; e++) {
      qa_save_reader item, item_field;
      if (!qa_save_reader_item(&extra_list, e, &item)) qa_range_error("out of memory");
      must_field(&item, "item", &item_field);
      extras[e].item = qa_namespaced(&item_field);
      qa_save_reader_destroy(&item_field);
      extras[e].count = field_number(&item, "count");
      qa_save_reader_destroy(&item);
    }
    contents.extra = extras;
    contents.extra_count = stored_extra;
    contents.has_extra = true;
    qa_save_reader_destroy(&extra_list);
    must_field(&data, "selection", &field);
    static const char *const SELECTIONS[] = {"source-default", "rank"};
    contents.selection = qa_save_reader_choice_string(&field, SELECTIONS, 2);
    contents.has_selection = true;
    qa_save_reader_destroy(&field);
    contents.avoid_underwater_lightning = field_boolean(&data, "avoidUnderwaterLightning");
    contents.has_avoid_underwater_lightning = true;
    contents.owner_pickup_delay = field_number(&data, "ownerPickupDelay");
    contents.has_owner_pickup_delay = true;
    qa_q1_creatures_backpacks_set(creatures, owner, &contents);
    qa_cleanup_pop(&extras_cleanup);
    free(extras);
    qa_save_reader_destroy(&data);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  must_field(root, "targets", &list);
  count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, actor_reader, enemy_reader;
    if (!qa_save_reader_item(&list, i, &entry)) qa_range_error("out of memory");
    must_field(&entry, "actor", &actor_reader);
    qa_owned_actor owner = restore_actor(creatures, &actor_reader);
    qa_save_reader_destroy(&actor_reader);
    must_field(&entry, "enemy", &enemy_reader);
    qa_actor_id enemy = restore_reference(creatures, &enemy_reader);
    qa_save_reader_destroy(&enemy_reader);
    qa_q1_creatures_targets_set(creatures, owner, enemy);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  must_field(root, "shots", &list);
  count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, actor_reader, enemy_reader, right;
    if (!qa_save_reader_item(&list, i, &entry)) qa_range_error("out of memory");
    must_field(&entry, "actor", &actor_reader);
    qa_owned_actor owner = restore_actor(creatures, &actor_reader);
    qa_save_reader_destroy(&actor_reader);
    must_field(&entry, "enemy", &enemy_reader);
    qa_actor_id enemy = restore_reference(creatures, &enemy_reader);
    qa_save_reader_destroy(&enemy_reader);
    must_field(&entry, "right", &right);
    /* TS reads x, y, z in order; a C initializer list does not sequence them. */
    const double right_x = field_number(&right, "x");
    const double right_y = field_number(&right, "y");
    const double right_z = field_number(&right, "z");
    qa_q1_wizard_shot shot = {enemy, {right_x, right_y, right_z}};
    qa_save_reader_destroy(&right);
    qa_q1_creatures_shots_set(creatures, owner, shot);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
}
