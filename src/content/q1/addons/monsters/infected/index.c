/* Port of src/content/q1/addons/monsters/infected/index.ts.
 * quakec_mg3/monsters/mg3_*_infected.qc. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/infected/index.h"
#include "qa/foundation/support.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/infected/species.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/content/q1/base/provider.h"
#include "../support.h"

#define PREFIX QA_Q1AM_INFECTED_PREFIX

typedef struct infected_spawn { qa_q1am_infected_registry *registry; const char *kind; } infected_spawn;
struct qa_q1am_infected_registry {
  qa_q1ad_context *context;
  struct { qa_owned_actor actor; qa_q1am_infected *monster; } *entries; size_t count, capacity;
  qa_q1am_infected **retired; size_t retired_count, retired_capacity;
  infected_spawn spawns[4];
  void *monster_callbacks;
  int64_t release_subscription;
  qa_q1_state_extension extension;
  qa_q1am_infected_registry *next;
};
static qa_q1am_infected_registry *all_registries;

static void *grow(void *items, size_t *capacity, size_t size) {
  const size_t next = *capacity != 0 ? *capacity * 2 : 8;
  void *grown = realloc(items, next * size);
  if (grown == NULL) qa_range_error("out of memory");
  *capacity = next;
  return grown;
}
static void retire(qa_q1am_infected_registry *r, qa_q1am_infected *monster) {
  if (r->retired_count == r->retired_capacity) r->retired = grow(r->retired, &r->retired_capacity, sizeof *r->retired);
  r->retired[r->retired_count++] = monster;
}
qa_q1am_infected *qa_q1am_infected_registry_get(const qa_q1am_infected_registry *monsters, qa_owned_actor actor) {
  for (size_t i = 0; i < monsters->count; i++)
    if (qa_same_owned_actor(monsters->entries[i].actor, actor)) return monsters->entries[i].monster;
  return NULL;
}
static void registry_set(qa_q1am_infected_registry *r, qa_owned_actor actor, qa_q1am_infected *monster) {
  for (size_t i = 0; i < r->count; i++)
    if (qa_same_owned_actor(r->entries[i].actor, actor)) {
      if (r->entries[i].monster != monster) retire(r, r->entries[i].monster);
      r->entries[i].monster = monster;
      return;
    }
  if (r->count == r->capacity) r->entries = grow(r->entries, &r->capacity, sizeof *r->entries);
  r->entries[r->count].actor = actor;
  r->entries[r->count].monster = monster;
  r->count++;
}
/* TS replace(entity): a new controller for the entity, set in the map. */
static qa_q1am_infected *replace(void *ctx, qa_q1_actor *entity) {
  qa_q1am_infected_registry *r = ctx;
  qa_q1am_infected *value = malloc(sizeof *value);
  if (value == NULL || !qa_q1am_infected_init(value, r->context, entity, replace, r)) qa_range_error("out of memory");
  registry_set(r, entity->actor, value);
  return value;
}
/* TS monster(entity). */
static qa_q1am_infected *require(qa_q1am_infected_registry *r, qa_q1_actor *entity) {
  qa_q1am_infected *value = qa_q1am_infected_registry_get(r, entity->actor);
  if (value == NULL) qa_range_error("Missing MG3 infected source state");
  return value;
}
static qa_q1_base_monster *lookup(void *ctx, qa_q1_actor *entity) { return &require(ctx, entity)->base.base; }
static void resurrect(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* TS (_game, entity) */
  qa_q1am_infected_resurrect(require(ctx, entity));
}
static bool kill_count_rule(void *ctx, qa_q1_base_monster *value) {
  qa_q1am_infected_registry *r = ctx;
  if (qa_q1am_infected_registry_get(r, value->entity->actor) == NULL || qa_q1_actor_number(value->entity, "infected") != 1)
    return true;
  qa_q1ad_set_number(r->context, value->entity, "infected", 0);
  return false;
}
static void spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* TS (_game, entity) */
  const infected_spawn *closure = ctx;
  qa_q1am_field_text(entity, "infected.kind", closure->kind);
  qa_q1_base_monster_spawn(&replace(closure->registry, entity)->base.base);
}
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q1am_infected_registry *r = ctx;
  for (size_t i = 0; i < r->count; i++)
    if (qa_same_owned_actor(r->entries[i].actor, actor)) {
      retire(r, r->entries[i].monster);
      memmove(&r->entries[i], &r->entries[i + 1], (r->count - i - 1) * sizeof *r->entries);
      r->count--;
      return;
    }
}
static void must(bool ok) { if (!ok) qa_range_error("out of memory"); }
static void set_child(qa_save_value *object, const char *key, qa_save_value *child) {
  must(child != NULL && qa_save_value_set(object, key, child));
}
static bool capture(void *ctx, uint8_t **out, size_t *out_length) {
  qa_q1am_infected_registry *r = ctx;
  qa_save_value *list = qa_save_value_array();
  if (list == NULL) return false;
  for (size_t i = 0; i < r->count; i++) {
    const qa_q1_base_monster *value = &r->entries[i].monster->base.base;
    qa_save_value *object = qa_save_value_object();
    must(object != NULL);
    set_child(object, "slot", qa_save_value_number((double)value->entity->actor.id.slot));
    set_child(object, "generation", qa_save_value_number((double)value->entity->actor.id.generation));
    set_child(object, "state", qa_q1_base_monster_capture(value));
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
  qa_q1am_infected_registry *r = ctx;
  qa_q1_services *game = r->context->game;
  for (size_t i = 0; i < r->count; i++) retire(r, r->entries[i].monster);
  r->count = 0;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  qa_save_reader root;
  must(qa_save_reader_init(&root, decoded, PREFIX));
  const size_t count = qa_save_reader_list_count(&root);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader reader, state;
    must(qa_save_reader_item(&root, i, &reader));
    qa_saved_actor_id saved;
    saved.slot = field_integer(&reader, "slot");
    saved.generation = field_integer(&reader, "generation");
    qa_owned_actor owner;
    qa_q1_actor *entity = qa_session_actor_registry_resolve_saved(game->host.actors, saved, &owner)
                              ? qa_q1_get_entity(game, &owner.id) : NULL;
    if (entity == NULL) qa_save_reader_fail(&reader, "missing infected actor");
    qa_q1am_infected *value = replace(r, entity);
    must(qa_save_reader_field(&reader, "state", &state));
    qa_q1_base_monster_restore(&value->base.base, &state);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&root);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}
static void clone(void *ctx, qa_q1_actor *source, qa_q1_actor *target) {
  qa_q1am_infected_registry *r = ctx;
  qa_q1am_infected *value = qa_q1am_infected_registry_get(r, source->actor);
  if (value == NULL) return;
  qa_save_value *captured = qa_q1_base_monster_capture(&value->base.base);
  must(captured != NULL);
  qa_q1am_infected *copy = replace(r, target);
  qa_save_reader reader;
  must(qa_save_reader_init(&reader, captured, PREFIX));
  qa_q1_base_monster_restore(&copy->base.base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
}
qa_q1am_infected_registry *qa_q1am_register_mg3_infected(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1am_infected_registry *r = calloc(1, sizeof *r);
  if (r == NULL) qa_range_error("out of memory");
  r->context = context;
  r->next = all_registries;
  all_registries = r;
  r->monster_callbacks = qa_q1_register_monster_callbacks(game, PREFIX, lookup, r);
  qa_q1am_register_mg3_monster_startup(game, PREFIX, lookup, r);
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = r;
  handlers.action = resurrect;
  qa_q1_callback_registry_register(game->named, PREFIX ":resurrect", handlers);
  qa_q1_base_register_kill_count_rule(context->base, PREFIX, kill_count_rule, r);
  for (size_t i = 0; i < qa_q1am_infected_classname_count && i < 4; i++) {
    r->spawns[i].registry = r;
    r->spawns[i].kind = qa_q1am_infected_classnames[i].kind;
    qa_q1_register_spawn(game, qa_q1am_infected_classnames[i].classname, spawn, &r->spawns[i]);
  }
  r->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, r);
  r->extension.ctx = r;
  r->extension.id = PREFIX;
  r->extension.capture = capture;
  r->extension.restore = restore;
  r->extension.clone = clone;
  qa_q1_register_state_extension(game, &r->extension);
  return r;
}
void qa_q1am_release_mg3_infected(qa_q1_services *game) {
  for (qa_q1am_infected_registry **link = &all_registries; *link != NULL;) {
    qa_q1am_infected_registry *r = *link;
    if (r->context->game != game) { link = &r->next; continue; }
    *link = r->next;
    qa_session_actor_registry_off_release(game->host.actors, r->release_subscription);
    for (size_t i = 0; i < r->count; i++) qa_q1_base_monster_destroy(&r->entries[i].monster->base.base);
    for (size_t i = 0; i < r->retired_count; i++) qa_q1_base_monster_destroy(&r->retired[i]->base.base);
    free(r->entries);
    free(r->retired);
    free(r->monster_callbacks);
    free(r);
  }
}
