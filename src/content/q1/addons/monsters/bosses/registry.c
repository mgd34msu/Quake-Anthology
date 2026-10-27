/* Port of src/content/q1/addons/monsters/bosses/registry.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/foundation/support.h"

typedef struct boss_entry {
  qa_owned_actor actor;
  qa_q1am_monster *monster;
  struct boss_entry *next;
} boss_entry;
struct qa_q1am_boss_registry {
  qa_q1am_context *context;
  qa_q1am_boss_create create;
  char prefix[80];
  boss_entry *head, *tail;
};
static boss_entry *entry_get(qa_q1am_boss_registry *r, qa_owned_actor actor) {
  for (boss_entry *e = r->head; e; e = e->next)
    if (qa_q1am_same_owned(e->actor, actor)) return e;
  return NULL;
}
static void entry_put(qa_q1am_boss_registry *r, qa_q1am_monster *monster) {
  boss_entry *old = entry_get(r, monster->entity->actor);
  if (old != NULL) {
    if (old->monster != monster) r->create.destroy(r->create.ctx, old->monster);
    old->monster = monster;
    return;
  }
  boss_entry *entry = malloc(sizeof *entry);
  if (!entry) qa_range_error("Boss registry allocation failed");
  entry->actor = monster->entity->actor;
  entry->monster = monster;
  entry->next = NULL;
  if (r->tail) r->tail->next = entry;
  else r->head = entry;
  r->tail = entry;
}
static void entry_delete(qa_q1am_boss_registry *r, qa_owned_actor actor) {
  boss_entry **link = &r->head;
  while (*link) {
    if (qa_q1am_same_owned((*link)->actor, actor)) {
      boss_entry *dead = *link;
      *link = dead->next;
      r->create.destroy(r->create.ctx, dead->monster);
      free(dead);
      return;
    }
    link = &(*link)->next;
  }
}
static void entries_clear(qa_q1am_boss_registry *r) {
  while (r->head) {
    boss_entry *dead = r->head;
    r->head = dead->next;
    r->create.destroy(r->create.ctx, dead->monster);
    free(dead);
  }
  r->tail = NULL;
}
qa_q1am_monster *qa_q1am_require_boss(qa_q1am_boss_registry *registry, qa_q1am_actor *entity) {
  boss_entry *entry = entry_get(registry, entity->actor);
  if (entry == NULL) qa_range_error("Missing MG3 boss source state for %s", entity->classname);
  return entry->monster;
}
bool qa_q1am_boss_registry_has(qa_q1am_boss_registry *registry, qa_owned_actor actor) {
  return entry_get(registry, actor) != NULL;
}
static qa_q1am_monster *lookup(void *ctx, qa_q1am_actor *entity) {
  return qa_q1am_require_boss(ctx, entity);
}
static void on_release(void *ctx, qa_owned_actor actor) { entry_delete(ctx, actor); }
static void spawn_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game;
  qa_q1am_boss_registry *r = ctx;
  qa_q1am_monster *monster = r->create.fn(r->create.ctx, entity);
  entry_put(r, monster);
  monster->ops.spawn(monster);
}
static uint8_t *capture_all(void *ctx, size_t *out_length) {
  qa_q1am_boss_registry *r = ctx;
  qa_save_value *list = qa_save_value_array();
  for (boss_entry *e = r->head; e; e = e->next) {
    qa_save_value *object = qa_save_value_object();
    qa_save_value_set(object, "slot", qa_save_value_number((double)e->monster->entity->actor.id.slot));
    qa_save_value_set(object, "generation", qa_save_value_number((double)e->monster->entity->actor.id.generation));
    qa_save_value_set(object, "state", qa_q1am_monster_capture(e->monster));
    qa_save_value_push(list, object);
  }
  uint8_t *bytes = qa_encode_checkpoint_value(list, out_length);
  qa_save_value_free(list);
  return bytes;
}
static void restore_all(void *ctx, const uint8_t *bytes, size_t length) {
  qa_q1am_boss_registry *r = ctx;
  entries_clear(r);
  qa_q1am_game *game = r->context->game;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader root;
  qa_save_reader_init(&root, decoded, r->prefix);
  size_t count = qa_save_reader_list_count(&root);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader reader, slot, generation, state;
    qa_save_reader_item(&root, i, &reader);
    qa_save_reader_field(&reader, "slot", &slot);
    qa_save_reader_field(&reader, "generation", &generation);
    qa_owned_actor actor;
    bool resolved = game->actors_resolve_saved(game->ctx, (int64_t)qa_save_reader_integer_min(&slot, 0),
                                                (int64_t)qa_save_reader_integer_min(&generation, 0), &actor);
    qa_save_reader_destroy(&slot);
    qa_save_reader_destroy(&generation);
    qa_q1am_actor *entity = resolved ? game->entity(game->ctx, true, actor.id) : NULL;
    if (entity == NULL) qa_save_reader_fail(&reader, "missing MG3 boss actor");
    qa_q1am_monster *monster = r->create.fn(r->create.ctx, entity);
    entry_put(r, monster);
    qa_save_reader_field(&reader, "state", &state);
    qa_q1am_monster_restore(monster, &state);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&root);
  qa_save_value_free(decoded);
}
static void clone_one(void *ctx, qa_q1am_actor *source, qa_q1am_actor *target) {
  qa_q1am_boss_registry *r = ctx;
  boss_entry *entry = entry_get(r, source->actor);
  if (entry == NULL) return;
  qa_q1am_monster *clone = r->create.fn(r->create.ctx, target);
  entry_put(r, clone);
  qa_save_value *captured = qa_q1am_monster_capture(entry->monster);
  qa_save_reader reader;
  qa_save_reader_init(&reader, captured, r->prefix);
  qa_q1am_monster_restore(clone, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
}
typedef struct boss_action_ctx {
  qa_q1am_kind kind;
  const char *kind_error;
  const char *name;
  void (*dispatch)(qa_q1am_monster *monster, const char *name);
} boss_action_ctx;
static void boss_action_trampoline(void *ctx, qa_q1am_monster *monster) {
  boss_action_ctx *state = ctx;
  if (monster->kind != state->kind) qa_range_error("%s", state->kind_error);
  state->dispatch(monster, state->name);
}
const qa_q1am_source *qa_q1am_boss_build_source(
    const char *prefix, const qa_q1am_frame_table *frames, qa_q1am_kind kind,
    const char *kind_error, void (*dispatch)(qa_q1am_monster *monster, const char *name),
    const qa_q1am_action_entry *extra, size_t extra_count) {
  qa_q1am_source *source = malloc(sizeof *source);
  if (!source) qa_range_error("Boss registry allocation failed");
  size_t cap = 16, count = 0;
  qa_q1am_action_entry *actions = calloc(cap, sizeof *actions);
  if (!actions) qa_range_error("Boss registry allocation failed");
  for (size_t f = 0; f < frames->count; f++)
    for (size_t o = 0; o < frames->frames[f].operation_count; o++) {
      const qa_q1am_operation *op = &frames->frames[f].operations[o];
      if (op->kind != QA_Q1AM_OP_ACTION) continue;
      bool exists = false;
      for (size_t i = 0; i < count; i++)
        if (strcmp(actions[i].name, op->name) == 0) exists = true;
      if (exists) continue;
      if (count == cap) {
        cap *= 2;
        actions = realloc(actions, cap * sizeof *actions);
        if (!actions) qa_range_error("Boss registry allocation failed");
      }
      boss_action_ctx *state = malloc(sizeof *state);
      if (!state) qa_range_error("Boss registry allocation failed");
      state->kind = kind;
      state->kind_error = kind_error;
      state->name = op->name;
      state->dispatch = dispatch;
      actions[count].name = op->name;
      actions[count].ctx = state;
      actions[count].fn = boss_action_trampoline;
      count++;
    }
  for (size_t i = 0; i < extra_count; i++) {
    bool replaced = false;
    for (size_t j = 0; j < count; j++)
      if (strcmp(actions[j].name, extra[i].name) == 0) {
        actions[j] = extra[i];
        replaced = true;
      }
    if (replaced) continue;
    if (count == cap) {
      cap *= 2;
      actions = realloc(actions, cap * sizeof *actions);
      if (!actions) qa_range_error("Boss registry allocation failed");
    }
    actions[count++] = extra[i];
  }
  source->callback_prefix = prefix;
  source->frames = frames;
  source->actions = actions;
  source->action_count = count;
  return source;
}
qa_q1am_boss_registry *qa_q1am_register_boss_controllers(qa_q1am_context *context, const char *prefix,
                                                         const char *classname, qa_q1am_boss_create create) {
  qa_q1am_game *game = context->game;
  qa_q1am_boss_registry *r = calloc(1, sizeof *r);
  if (!r) qa_range_error("Boss registry allocation failed");
  r->context = context;
  r->create = create;
  (void)snprintf(r->prefix, sizeof r->prefix, "%s", prefix);
  qa_q1am_monster_lookup lookup_value = {r, lookup};
  game->register_monster_callbacks(game->ctx, game, prefix, lookup_value);
  qa_q1am_register_mg3_monster_startup(game, prefix, lookup_value);
  game->register_spawn(game->ctx, classname, spawn_handler, r);
  game->actors_on_release(game->ctx, on_release, r);
  qa_q1am_state_extension extension = {r->prefix, r, capture_all, restore_all, clone_one};
  game->register_state_extension(game->ctx, extension);
  return r;
}
