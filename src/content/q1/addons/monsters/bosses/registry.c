/* Port of src/content/q1/addons/monsters/bosses/registry.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/foundation/support.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/startup.h"
#include "../support.h"

typedef struct boss_entry { qa_owned_actor actor; qa_q1_base_monster *monster; } boss_entry;
struct qa_q1am_boss_registry {
  qa_q1_services *game;
  char *prefix;
  qa_q1am_boss_create_fn create;
  void *create_ctx;
  boss_entry *entries; size_t count, capacity; /* TS Map, insertion order */
  /* Monsters leave the map at release while their own callback may still
   * run (die -> remove -> release); they are freed with the registry. */
  qa_q1_base_monster **retired; size_t retired_count, retired_capacity;
  int64_t release_subscription;
  void *monster_callbacks; /* qa_q1_register_monster_callbacks closure state (owned) */
  qa_q1_state_extension extension;
  qa_q1am_boss_registry *next;
};
static qa_q1am_boss_registry *all_registries;

static void *grow(void *items, size_t *capacity, size_t size) {
  const size_t next = *capacity != 0 ? *capacity * 2 : 8;
  void *grown = realloc(items, next * size);
  if (grown == NULL) qa_range_error("out of memory");
  *capacity = next;
  return grown;
}
static void retire(qa_q1am_boss_registry *r, qa_q1_base_monster *monster) {
  if (r->retired_count == r->retired_capacity) r->retired = grow(r->retired, &r->retired_capacity, sizeof *r->retired);
  r->retired[r->retired_count++] = monster;
}
static boss_entry *entry_get(const qa_q1am_boss_registry *r, qa_owned_actor actor) {
  for (size_t i = 0; i < r->count; i++)
    if (qa_same_owned_actor(r->entries[i].actor, actor)) return &r->entries[i];
  return NULL;
}
/* TS monsters.set(actor, monster). */
static void entry_set(qa_q1am_boss_registry *r, qa_owned_actor actor, qa_q1_base_monster *monster) {
  boss_entry *entry = entry_get(r, actor);
  if (entry != NULL) {
    if (entry->monster != monster) retire(r, entry->monster);
    entry->monster = monster;
    return;
  }
  if (r->count == r->capacity) r->entries = grow(r->entries, &r->capacity, sizeof *r->entries);
  r->entries[r->count].actor = actor;
  r->entries[r->count].monster = monster;
  r->count++;
}
static void entries_clear(qa_q1am_boss_registry *r) {
  for (size_t i = 0; i < r->count; i++) retire(r, r->entries[i].monster);
  r->count = 0;
}
static qa_q1_base_monster *create(qa_q1am_boss_registry *r, qa_q1_actor *entity) {
  qa_q1_base_monster *monster = r->create(r->create_ctx, entity);
  if (monster == NULL) qa_range_error("out of memory");
  return monster;
}

qa_q1_base_monster *qa_q1am_boss_registry_get(const qa_q1am_boss_registry *monsters, qa_owned_actor actor) {
  const boss_entry *entry = entry_get(monsters, actor);
  return entry == NULL ? NULL : entry->monster;
}
qa_q1_base_monster *qa_q1am_require_boss(qa_q1am_boss_registry *monsters, qa_q1_actor *entity) {
  qa_q1_base_monster *monster = qa_q1am_boss_registry_get(monsters, entity->actor);
  if (monster == NULL) qa_range_error("Missing MG3 boss source state for %s", entity->classname);
  return monster;
}
static qa_q1_base_monster *lookup(void *ctx, qa_q1_actor *entity) { return qa_q1am_require_boss(ctx, entity); }
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q1am_boss_registry *r = ctx;
  for (size_t i = 0; i < r->count; i++)
    if (qa_same_owned_actor(r->entries[i].actor, actor)) {
      retire(r, r->entries[i].monster);
      memmove(&r->entries[i], &r->entries[i + 1], (r->count - i - 1) * sizeof *r->entries);
      r->count--;
      return;
    }
}
static void spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* TS (_game, entity) */
  qa_q1am_boss_registry *r = ctx;
  qa_q1_base_monster *monster = create(r, entity);
  entry_set(r, entity->actor, monster);
  qa_q1_base_monster_spawn(monster);
}
static void must(bool ok) { if (!ok) qa_range_error("out of memory"); }
static void set_child(qa_save_value *object, const char *key, qa_save_value *child) {
  must(child != NULL && qa_save_value_set(object, key, child));
}
static bool capture(void *ctx, uint8_t **out, size_t *out_length) {
  qa_q1am_boss_registry *r = ctx;
  qa_save_value *list = qa_save_value_array();
  if (list == NULL) return false;
  for (size_t i = 0; i < r->count; i++) {
    const qa_q1_base_monster *monster = r->entries[i].monster;
    qa_save_value *object = qa_save_value_object();
    must(object != NULL);
    set_child(object, "slot", qa_save_value_number((double)monster->entity->actor.id.slot));
    set_child(object, "generation", qa_save_value_number((double)monster->entity->actor.id.generation));
    set_child(object, "state", qa_q1_base_monster_capture(monster));
    must(qa_save_value_push(list, object));
  }
  *out = qa_encode_checkpoint_value(list, out_length);
  qa_save_value_free(list);
  return *out != NULL;
}
static double field_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader value;
  must(qa_save_reader_field(reader, name, &value));
  const double result = qa_save_reader_integer_min(&value, 0);
  qa_save_reader_destroy(&value);
  return result;
}
static void q1_free_decoded_save(void *value) { qa_save_value_free(value); }
static void restore(void *ctx, const uint8_t *bytes, size_t length) {
  qa_q1am_boss_registry *r = ctx;
  qa_q1_services *game = r->game;
  entries_clear(r);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  qa_save_reader root;
  must(qa_save_reader_init(&root, decoded, r->prefix));
  const size_t count = qa_save_reader_list_count(&root);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader reader, state;
    must(qa_save_reader_item(&root, i, &reader));
    qa_saved_actor_id saved;
    saved.slot = field_integer(&reader, "slot");
    saved.generation = field_integer(&reader, "generation");
    qa_owned_actor actor;
    qa_q1_actor *entity = qa_session_actor_registry_resolve_saved(game->host.actors, saved, &actor)
                              ? qa_q1_get_entity(game, &actor.id) : NULL;
    if (entity == NULL) qa_save_reader_fail(&reader, "missing MG3 boss actor");
    qa_q1_base_monster *monster = create(r, entity);
    must(qa_save_reader_field(&reader, "state", &state));
    qa_q1_base_monster_restore(monster, &state);
    qa_save_reader_destroy(&state);
    entry_set(r, entity->actor, monster);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&root);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}
static void clone(void *ctx, qa_q1_actor *source, qa_q1_actor *target) {
  qa_q1am_boss_registry *r = ctx;
  const boss_entry *entry = entry_get(r, source->actor);
  if (entry == NULL) return;
  qa_q1_base_monster *original = entry->monster;
  qa_q1_base_monster *copy = create(r, target);
  qa_save_value *captured = qa_q1_base_monster_capture(original);
  must(captured != NULL);
  qa_save_reader reader;
  must(qa_save_reader_init(&reader, captured, r->prefix));
  qa_q1_base_monster_restore(copy, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
  entry_set(r, target->actor, copy);
}

qa_q1am_boss_registry *qa_q1am_register_boss_controllers(qa_q1ad_context *context, const char *prefix,
                                                         const char *classname, qa_q1am_boss_create_fn create_fn,
                                                         void *create_ctx) {
  qa_q1_services *game = context->game;
  qa_q1am_boss_registry *r = calloc(1, sizeof *r);
  if (r == NULL) qa_range_error("out of memory");
  const size_t length = strlen(prefix) + 1;
  r->prefix = malloc(length);
  if (r->prefix == NULL) qa_range_error("out of memory");
  memcpy(r->prefix, prefix, length);
  r->game = game;
  r->create = create_fn;
  r->create_ctx = create_ctx;
  r->next = all_registries;
  all_registries = r;
  r->monster_callbacks = qa_q1_register_monster_callbacks(game, prefix, lookup, r);
  qa_q1am_register_mg3_monster_startup(game, prefix, lookup, r);
  qa_q1_register_spawn(game, classname, spawn, r);
  r->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, r);
  r->extension.ctx = r;
  r->extension.id = r->prefix;
  r->extension.capture = capture;
  r->extension.restore = restore;
  r->extension.clone = clone;
  qa_q1_register_state_extension(game, &r->extension);
  return r;
}
void qa_q1am_release_boss_controllers(qa_q1_services *game) {
  qa_q1am_boss_registry **link = &all_registries;
  while (*link != NULL) {
    qa_q1am_boss_registry *r = *link;
    if (r->game != game) { link = &r->next; continue; }
    *link = r->next;
    qa_session_actor_registry_off_release(game->host.actors, r->release_subscription);
    entries_clear(r);
    for (size_t i = 0; i < r->retired_count; i++) qa_q1_base_monster_destroy(r->retired[i]);
    free(r->retired);
    free(r->entries);
    free(r->monster_callbacks);
    free(r->prefix);
    free(r);
  }
}

/* Built action tables, one per (frame table, fn) pair. */
typedef struct frame_actions {
  const qa_q1_monster_frame_table *frames;
  void (*fn)(void *ctx, qa_q1_base_monster *monster);
  qa_q1_monster_action_entry *entries; size_t count;
  struct frame_actions *next;
} frame_actions;
static frame_actions *all_frame_actions;
const qa_q1_monster_action_entry *qa_q1am_frame_actions(const qa_q1_monster_frame_table *frames,
                                                        void (*fn)(void *ctx, qa_q1_base_monster *monster),
                                                        size_t *count) {
  for (const frame_actions *built = all_frame_actions; built != NULL; built = built->next)
    if (built->frames == frames && built->fn == fn) { *count = built->count; return built->entries; }
  frame_actions *built = calloc(1, sizeof *built);
  if (built == NULL) qa_range_error("out of memory");
  size_t capacity = 0;
  for (size_t f = 0; f < frames->count; f++) {
    const qa_q1_monster_frame *frame = &frames->entries[f].frame;
    for (size_t o = 0; o < frame->operation_count; o++) {
      const qa_q1_monster_operation *op = &frame->operations[o];
      if (op->kind != QA_Q1_OP_ACTION) continue;
      bool present = false;
      for (size_t i = 0; i < built->count && !present; i++) present = strcmp(built->entries[i].name, op->name) == 0;
      if (present) continue; /* Map.set keeps the first insertion position */
      if (built->count == capacity) built->entries = grow(built->entries, &capacity, sizeof *built->entries);
      built->entries[built->count].name = op->name;
      built->entries[built->count].fn = fn;
      built->entries[built->count].ctx = (void *)(uintptr_t)op->name; /* the captured operation.name */
      built->count++;
    }
  }
  built->frames = frames;
  built->fn = fn;
  built->next = all_frame_actions;
  all_frame_actions = built;
  *count = built->count;
  return built->entries;
}
