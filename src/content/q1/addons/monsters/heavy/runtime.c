/* Port of src/content/q1/addons/monsters/heavy/runtime.ts.
 * MG3 heavy monster source controllers. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/heavy/runtime.h"
#include "qa/foundation/support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/startup.h"
#include "../support.h"

/* One definitions Map entry plus its spawn closure and the source action
 * table TS builds per controller from definition.actions (identical for
 * every controller of a definition, so built once here). */
struct qa_q1am_heavy_registered {
  qa_q1am_mg3_heavy *runtime;
  const char *classname;
  const qa_q1am_heavy_definition *definition;
  qa_q1_monster_action_entry *actions;
  struct qa_q1am_heavy_registered *next;
};
static qa_q1am_mg3_heavy *all_runtimes;

static void heavy_spawn(qa_q1_base_monster *monster);
static void heavy_start(qa_q1_base_monster *monster);
static void heavy_use(qa_q1_base_monster *monster, const qa_actor_id *activator);
static void heavy_play(qa_q1_base_monster *monster, const char *name);
static void heavy_melee_attack(qa_q1_base_monster *monster);
static bool heavy_try_attack(qa_q1_base_monster *monster);
static void heavy_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage);
static void heavy_die(qa_q1_base_monster *monster, const qa_actor_id *attacker);
static void heavy_destroy(qa_q1_base_monster *monster) { free(monster); }
static const qa_q1_base_monster_ops heavy_ops = {
  QA_Q1AM_MG3_MONSTER_OPS,
  .spawn = heavy_spawn, .start = heavy_start, .use = heavy_use, .play = heavy_play,
  .melee_attack = heavy_melee_attack, .try_attack = heavy_try_attack, .pain = heavy_pain, .die = heavy_die,
  .destroy = heavy_destroy,
};
static void heavy_sight_sound(qa_q1am_mg3_monster *monster);
static const qa_q1am_mg3_ops heavy_mg3_ops = {.sight_sound = heavy_sight_sound};

bool qa_q1am_is_heavy_monster(const qa_q1_base_monster *monster) { return monster->ops == &heavy_ops; }
static qa_q1am_heavy_monster *heavy_of(qa_q1_base_monster *monster) { return (qa_q1am_heavy_monster *)monster; }

static void heavy_spawn(qa_q1_base_monster *monster) {
  qa_q1am_heavy_monster *heavy = heavy_of(monster);
  heavy->definition->spawn(heavy);
}
void qa_q1am_heavy_monster_number(qa_q1am_heavy_monster *monster, const char *key, double value) {
  qa_q1ad_set_number(monster->runtime->context, monster->base.base.entity, key, value);
}
void qa_q1am_heavy_monster_install_callbacks(qa_q1am_heavy_monster *monster) {
  qa_q1_services *game = monster->base.base.game;
  qa_q1_actor *entity = monster->base.base.entity;
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, QA_Q1AM_HEAVY_PREFIX ":monster_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, QA_Q1AM_HEAVY_PREFIX ":monster_die");
  entity->path_end = qa_q1_callback_registry_action(game->named, entity, QA_Q1AM_HEAVY_PREFIX ":monster_stand");
}
void qa_q1am_heavy_monster_initialize(qa_q1am_heavy_monster *monster, double size) {
  qa_q1am_heavy_monster_install_callbacks(monster);
  char model[96];
  (void)snprintf(model, sizeof model, "progs/%s.mdl", monster->base.base.spec->model);
  qa_q1am_init_mg3_monster(&monster->base.base, monster->runtime->context, model, 1, size);
}
static void heavy_start(qa_q1_base_monster *monster) {
  qa_q1am_start_mg3_monster(monster, heavy_of(monster)->runtime->context);
}
static void heavy_use(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_actor_id resolved;
  const bool has = qa_q1am_mg3_monster_activator(monster->game, activator, &resolved);
  qa_q1_base_monster_use_super(monster, has ? &resolved : NULL);
}
static qa_q1am_heavy_action definition_action(const qa_q1am_heavy_definition *definition, const char *name) {
  for (size_t i = 0; i < definition->action_count; i++)
    if (strcmp(definition->actions[i].name, name) == 0) return definition->actions[i].fn;
  return NULL;
}
static void heavy_play(qa_q1_base_monster *monster, const char *name) {
  qa_q1am_heavy_monster *heavy = heavy_of(monster);
  const qa_q1am_heavy_action action = definition_action(heavy->definition, name);
  if (!qa_q1_monster_frame_table_has(heavy->definition->frames, name) && action != NULL) {
    action(heavy);
    return;
  }
  qa_q1_base_monster_play_super(monster, name);
}
static void heavy_sight_sound(qa_q1am_mg3_monster *monster) {
  qa_q1am_heavy_monster *heavy = (qa_q1am_heavy_monster *)monster;
  if (heavy->definition->sight == NULL) qa_q1am_mg3_sight_sound_super(&monster->base);
  else heavy->definition->sight(heavy);
}
static void heavy_melee_attack(qa_q1_base_monster *monster) {
  qa_q1am_heavy_monster *heavy = heavy_of(monster);
  if (heavy->definition->melee != NULL) heavy->definition->melee(heavy);
}
static bool heavy_try_attack(qa_q1_base_monster *monster) {
  qa_q1am_heavy_monster *heavy = heavy_of(monster);
  if (heavy->definition->attack != NULL) return heavy->definition->attack(heavy);
  return qa_q1am_mg3_check_attack(monster);
}
static void heavy_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage) {
  qa_q1_base_monster_retaliate(monster, attacker);
  qa_q1am_heavy_monster *heavy = heavy_of(monster);
  heavy->definition->pain(heavy, attacker, damage);
}
static void heavy_die(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  if (monster->counted_death) return;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_base_monster_set_enemy(monster, attacker);
  if (qa_q1_health(game, entity->actor.id) < -99) qa_gameplay_authority_set_health(game->host.combat, entity->actor, -99);
  qa_q1_actor_set_damageable(entity, false);
  memset(&entity->touch, 0, sizeof entity->touch);
  qa_q1_base_monster_count_kill(monster);
  qa_q1am_heavy_monster *heavy = heavy_of(monster);
  heavy->definition->die(heavy);
}

static struct qa_q1am_heavy_registered *registered_for(const qa_q1am_mg3_heavy *runtime, const qa_q1am_heavy_definition *definition) {
  for (struct qa_q1am_heavy_registered *entry = runtime->definitions; entry != NULL; entry = entry->next)
    if (entry->definition == definition) return entry;
  return NULL;
}
/* TS (monster: BaseMonster) => action(runtime.require(monster.entity)); entry ctx = the action. */
static void wrapped_action(void *ctx, qa_q1_base_monster *monster) {
  const qa_q1am_heavy_action_entry *action = ctx;
  action->fn(qa_q1am_mg3_heavy_require(heavy_of(monster)->runtime, monster->entity));
}
static qa_q1_monster_action_entry *wrap_actions(const qa_q1am_heavy_definition *definition) {
  if (definition->action_count == 0) return NULL;
  qa_q1_monster_action_entry *entries = malloc(definition->action_count * sizeof *entries);
  if (entries == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < definition->action_count; i++) {
    entries[i].name = definition->actions[i].name;
    entries[i].fn = wrapped_action;
    entries[i].ctx = (void *)&definition->actions[i];
  }
  return entries;
}
bool qa_q1am_heavy_monster_init(qa_q1am_heavy_monster *monster, qa_q1am_mg3_heavy *runtime, qa_q1_actor *entity,
                                const qa_q1am_heavy_definition *definition) {
  memset(monster, 0, sizeof *monster);
  monster->runtime = runtime;
  monster->definition = definition;
  const struct qa_q1am_heavy_registered *registered = registered_for(runtime, definition);
  qa_q1_monster_action_entry *own = registered == NULL ? wrap_actions(definition) : NULL;
  qa_q1_base_monster_source source;
  memset(&source, 0, sizeof source);
  source.callback_prefix = QA_Q1AM_HEAVY_PREFIX;
  source.frames = definition->frames;
  source.actions = registered != NULL ? registered->actions : own;
  source.action_count = definition->action_count;
  const bool ok = qa_q1am_mg3_monster_init(&monster->base, runtime->context->game, entity, &definition->spec,
                                           qa_q1_base_as_monster_services(runtime->context->base), &source,
                                           &heavy_ops, &heavy_mg3_ops);
  if (own != NULL) {
    /* An unregistered definition keeps its table with the runtime, like the registered ones. */
    struct qa_q1am_heavy_registered *entry = calloc(1, sizeof *entry);
    if (entry == NULL) qa_range_error("out of memory");
    entry->runtime = runtime;
    entry->definition = definition;
    entry->actions = own;
    entry->next = runtime->definitions;
    runtime->definitions = entry;
  }
  return ok;
}

static void *grow(void *items, size_t *capacity, size_t size) {
  const size_t next = *capacity != 0 ? *capacity * 2 : 8;
  void *grown = realloc(items, next * size);
  if (grown == NULL) qa_range_error("out of memory");
  *capacity = next;
  return grown;
}
static void retire(qa_q1am_mg3_heavy *runtime, qa_q1am_heavy_monster *monster) {
  if (runtime->retired_count == runtime->retired_capacity)
    runtime->retired = grow(runtime->retired, &runtime->retired_capacity, sizeof *runtime->retired);
  runtime->retired[runtime->retired_count++] = monster;
}
qa_q1am_heavy_monster *qa_q1am_mg3_heavy_get(const qa_q1am_mg3_heavy *runtime, qa_owned_actor actor) {
  for (size_t i = 0; i < runtime->monster_count; i++)
    if (qa_same_owned_actor(runtime->monsters[i].actor, actor)) return runtime->monsters[i].monster;
  return NULL;
}
qa_q1am_heavy_monster *qa_q1am_mg3_heavy_require(qa_q1am_mg3_heavy *runtime, qa_q1_actor *entity) {
  qa_q1am_heavy_monster *monster = qa_q1am_mg3_heavy_get(runtime, entity->actor);
  if (monster == NULL) qa_range_error("Missing MG3 heavy source controller for %s", entity->classname);
  return monster;
}
/* TS private create(entity, definition): new controller, monsters.set. */
static qa_q1am_heavy_monster *create(qa_q1am_mg3_heavy *runtime, qa_q1_actor *entity, const qa_q1am_heavy_definition *definition) {
  qa_q1am_heavy_monster *monster = malloc(sizeof *monster);
  if (monster == NULL || !qa_q1am_heavy_monster_init(monster, runtime, entity, definition)) qa_range_error("out of memory");
  for (size_t i = 0; i < runtime->monster_count; i++)
    if (qa_same_owned_actor(runtime->monsters[i].actor, entity->actor)) {
      retire(runtime, runtime->monsters[i].monster);
      runtime->monsters[i].monster = monster;
      return monster;
    }
  if (runtime->monster_count == runtime->monster_capacity)
    runtime->monsters = grow(runtime->monsters, &runtime->monster_capacity, sizeof *runtime->monsters);
  runtime->monsters[runtime->monster_count].actor = entity->actor;
  runtime->monsters[runtime->monster_count].monster = monster;
  runtime->monster_count++;
  return monster;
}
static qa_q1_base_monster *lookup(void *ctx, qa_q1_actor *entity) {
  return &qa_q1am_mg3_heavy_require(ctx, entity)->base.base;
}
static void source_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* TS (_game, entity) */
  qa_q1am_heavy_monster *monster = qa_q1am_mg3_heavy_require(ctx, entity);
  monster->definition->die(monster);
}
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q1am_mg3_heavy *runtime = ctx;
  for (size_t i = 0; i < runtime->monster_count; i++)
    if (qa_same_owned_actor(runtime->monsters[i].actor, actor)) {
      retire(runtime, runtime->monsters[i].monster);
      memmove(&runtime->monsters[i], &runtime->monsters[i + 1], (runtime->monster_count - i - 1) * sizeof *runtime->monsters);
      runtime->monster_count--;
      return;
    }
}
static void must(bool ok) { if (!ok) qa_range_error("out of memory"); }
static void set_child(qa_save_value *object, const char *key, qa_save_value *child) {
  must(child != NULL && qa_save_value_set(object, key, child));
}
static bool capture(void *ctx, uint8_t **out, size_t *out_length) {
  qa_q1am_mg3_heavy *runtime = ctx;
  qa_save_value *root = qa_save_value_object(), *list = qa_save_value_array();
  if (root == NULL || list == NULL) { qa_save_value_free(root); qa_save_value_free(list); return false; }
  set_child(root, "runeKnightMeleeCycle", qa_save_value_number(runtime->rune_knight_melee_cycle));
  for (size_t i = 0; i < runtime->monster_count; i++) {
    const qa_q1am_heavy_monster *monster = runtime->monsters[i].monster;
    const qa_q1_actor *entity = monster->base.base.entity;
    qa_save_value *object = qa_save_value_object(), *actor = qa_save_value_object();
    must(object != NULL && actor != NULL);
    set_child(actor, "slot", qa_save_value_number((double)entity->actor.id.slot));
    set_child(actor, "generation", qa_save_value_number((double)entity->actor.id.generation));
    set_child(object, "actor", actor);
    const qa_q1_base_monster_species *spec = monster->base.base.spec;
    set_child(object, "definition", qa_save_value_string(spec->classname_count > 0 ? spec->classnames[0] : entity->classname));
    set_child(object, "state", qa_q1_base_monster_capture(&monster->base.base));
    must(qa_save_value_push(list, object));
  }
  set_child(root, "monsters", list);
  *out = qa_encode_checkpoint_value(root, out_length);
  qa_save_value_free(root);
  return *out != NULL;
}
static void field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  must(qa_save_reader_field(reader, name, out));
}
static double field_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader value;
  field(reader, name, &value);
  const double result = qa_save_reader_integer_min(&value, 0);
  qa_save_reader_destroy(&value);
  return result;
}
static const qa_q1am_heavy_definition *definition_named(const qa_q1am_mg3_heavy *runtime, const char *classname) {
  const qa_q1am_heavy_definition *found = NULL;
  /* TS definitions.set: the last registration of a classname wins. */
  for (const struct qa_q1am_heavy_registered *entry = runtime->definitions; entry != NULL && found == NULL; entry = entry->next)
    if (entry->classname != NULL && strcmp(entry->classname, classname) == 0) found = entry->definition;
  return found;
}
static void q1_free_decoded_save(void *value) { qa_save_value_free(value); }
static void restore(void *ctx, const uint8_t *bytes, size_t length) {
  qa_q1am_mg3_heavy *runtime = ctx;
  qa_q1_services *game = runtime->context->game;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  qa_save_reader root, cycle, list;
  must(qa_save_reader_init(&root, decoded, QA_Q1AM_HEAVY_PREFIX));
  field(&root, "runeKnightMeleeCycle", &cycle);
  runtime->rune_knight_melee_cycle = qa_save_reader_number(&cycle);
  qa_save_reader_destroy(&cycle);
  for (size_t i = 0; i < runtime->monster_count; i++) retire(runtime, runtime->monsters[i].monster);
  runtime->monster_count = 0;
  field(&root, "monsters", &list);
  const size_t count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader reader, saved, definition_reader, state;
    must(qa_save_reader_item(&list, i, &reader));
    field(&reader, "actor", &saved);
    qa_saved_actor_id id;
    id.slot = field_integer(&saved, "slot");
    id.generation = field_integer(&saved, "generation");
    qa_save_reader_destroy(&saved);
    qa_owned_actor owner;
    qa_q1_actor *entity = qa_session_actor_registry_resolve_saved(game->host.actors, id, &owner)
                              ? qa_q1_get_entity(game, &owner.id) : NULL;
    if (entity == NULL) qa_save_reader_fail(&reader, "missing heavy monster actor");
    field(&reader, "definition", &definition_reader);
    const qa_q1am_heavy_definition *definition = definition_named(runtime, qa_save_reader_string(&definition_reader));
    qa_save_reader_destroy(&definition_reader);
    if (definition == NULL) qa_save_reader_fail(&reader, "unknown heavy monster definition");
    qa_q1am_heavy_monster *monster = create(runtime, entity, definition);
    field(&reader, "state", &state);
    qa_q1_base_monster_restore(&monster->base.base, &state);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&list);
  qa_save_reader_destroy(&root);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}
static void clone(void *ctx, qa_q1_actor *source, qa_q1_actor *target) {
  qa_q1am_mg3_heavy *runtime = ctx;
  qa_q1am_heavy_monster *monster = qa_q1am_mg3_heavy_get(runtime, source->actor);
  if (monster == NULL) return;
  qa_q1am_heavy_monster *copy = create(runtime, target, monster->definition);
  qa_save_value *captured = qa_q1_base_monster_capture(&monster->base.base);
  must(captured != NULL);
  qa_save_reader reader;
  must(qa_save_reader_init(&reader, captured, QA_Q1AM_HEAVY_PREFIX));
  qa_q1_base_monster_restore(&copy->base.base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
}
qa_q1am_mg3_heavy *qa_q1am_mg3_heavy_create(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1am_mg3_heavy *runtime = calloc(1, sizeof *runtime);
  if (runtime == NULL) return NULL;
  runtime->context = context;
  runtime->next = all_runtimes;
  all_runtimes = runtime;
  runtime->monster_callbacks = qa_q1_register_monster_callbacks(game, QA_Q1AM_HEAVY_PREFIX, lookup, runtime);
  qa_q1am_register_mg3_monster_startup(game, QA_Q1AM_HEAVY_PREFIX, lookup, runtime);
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = runtime;
  handlers.action = source_die;
  qa_q1_callback_registry_register(game->named, QA_Q1AM_HEAVY_PREFIX ":source_die", handlers);
  runtime->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, runtime);
  runtime->extension.ctx = runtime;
  runtime->extension.id = QA_Q1AM_HEAVY_PREFIX;
  runtime->extension.capture = capture;
  runtime->extension.restore = restore;
  runtime->extension.clone = clone;
  qa_q1_register_state_extension(game, &runtime->extension);
  return runtime;
}
static void spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* TS (_game, entity) */
  const struct qa_q1am_heavy_registered *entry = ctx;
  qa_q1_base_monster_spawn(&create(entry->runtime, entity, entry->definition)->base.base);
}
void qa_q1am_mg3_heavy_register(qa_q1am_mg3_heavy *runtime, const qa_q1am_heavy_definition *definition) {
  qa_q1_services *game = runtime->context->game;
  for (size_t i = 0; i < definition->callback_count; i++) {
    char name[128];
    (void)snprintf(name, sizeof name, QA_Q1AM_HEAVY_PREFIX ":%s", definition->callbacks[i].name);
    qa_q1_callback_registry_register(game->named, name, definition->callbacks[i].handlers);
  }
  qa_q1_monster_action_entry *actions = NULL;
  const struct qa_q1am_heavy_registered *existing = registered_for(runtime, definition);
  for (size_t i = 0; i < definition->spec.classname_count; i++) {
    struct qa_q1am_heavy_registered *entry = calloc(1, sizeof *entry);
    if (entry == NULL) qa_range_error("out of memory");
    entry->runtime = runtime;
    entry->classname = definition->spec.classnames[i];
    entry->definition = definition;
    if (existing == NULL && actions == NULL) actions = wrap_actions(definition);
    entry->actions = existing != NULL ? existing->actions : actions;
    entry->next = runtime->definitions;
    runtime->definitions = entry;
    existing = entry;
    qa_q1_register_spawn(game, entry->classname, spawn, entry);
  }
}
void qa_q1am_release_mg3_heavy(qa_q1_services *game) {
  for (qa_q1am_mg3_heavy **link = &all_runtimes; *link != NULL;) {
    qa_q1am_mg3_heavy *runtime = *link;
    if (runtime->context->game != game) { link = &runtime->next; continue; }
    *link = runtime->next;
    qa_session_actor_registry_off_release(game->host.actors, runtime->release_subscription);
    for (size_t i = 0; i < runtime->monster_count; i++) qa_q1_base_monster_destroy(&runtime->monsters[i].monster->base.base);
    for (size_t i = 0; i < runtime->retired_count; i++) qa_q1_base_monster_destroy(&runtime->retired[i]->base.base);
    /* Entries of one definition share one action table: free each table once. */
    for (struct qa_q1am_heavy_registered *entry = runtime->definitions; entry != NULL;) {
      struct qa_q1am_heavy_registered *next = entry->next;
      bool shared = false;
      for (struct qa_q1am_heavy_registered *later = next; later != NULL && !shared; later = later->next)
        shared = later->actions == entry->actions;
      if (!shared) free(entry->actions);
      free(entry);
      entry = next;
    }
    free(runtime->monster_callbacks);
    free(runtime->monsters);
    free(runtime->retired);
    free(runtime);
  }
}
