/* Port of src/content/q1/addons/monsters/ordinary/index.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/ordinary/attack.h"
#include "qa/content/q1/addons/monsters/ordinary/rocket_ogre.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/foundation/support.h"

#define FLAGS(x) ((int32_t)(x))

static const qa_bounds small_bounds = {{-16, -16, -24}, {16, 16, 40}};
static const qa_bounds large_bounds = {{-32, -32, -24}, {32, 32, 64}};

static qa_q1am_frame extra_frame_entry;
static qa_q1am_frame_table extra_table;
static bool extra_built;
static const qa_q1am_frame_table *extra_frames(qa_q1am_game *game) {
  if (!extra_built) {
    const qa_q1am_frame *hang = game->base_frame(game->ctx, "zombie_paine1");
    if (hang == NULL) qa_range_error("Missing native zombie hang model frame");
    extra_frame_entry.name = "zombie_hang1";
    extra_frame_entry.frame = hang->frame;
    extra_frame_entry.next = "zombie_hang1";
    extra_frame_entry.operations = NULL;
    extra_frame_entry.operation_count = 0;
    extra_table.frames = &extra_frame_entry;
    extra_table.count = 1;
    extra_built = true;
  }
  return &extra_table;
}

static qa_q1am_species source_spec(qa_q1am_context *context, qa_q1am_actor *entity) {
  const char *field_class = qa_q1am_actor_text(entity, "addon.monsterClass");
  const char *classname = field_class[0] != '\0' ? field_class : entity->classname;
  bool rocket = strcmp(context->program, "mg3") == 0 && strcmp(classname, "monster_ogre_rocket") == 0;
  const char *wanted = rocket ? "monster_ogre" : classname;
  const qa_q1am_species *found = NULL;
  for (size_t i = 0; i < qa_q1am_base_species_count && !found; i++)
    for (size_t c = 0; c < qa_q1am_base_species[i].classname_count; c++)
      if (strcmp(qa_q1am_base_species[i].classnames[c], wanted) == 0) found = &qa_q1am_base_species[i];
  if (found == NULL) qa_range_error("Missing native addon monster %s", classname);
  qa_q1am_species spec = *found;
  spec.bounds = strcmp(spec.species, "demon") == 0 || strcmp(spec.species, "ogre") == 0 ||
                        strcmp(spec.species, "shambler") == 0 || strcmp(spec.species, "shalrath") == 0
                    ? large_bounds : small_bounds;
  if (strcmp(spec.species, "zombie") == 0 && strcmp(context->program, "mg3") == 0 &&
      (FLAGS(entity->spawnflags) & 128) != 0) {
    spec.missile = NULL;
    spec.melee = true;
    return spec;
  }
  if (strcmp(spec.species, "zombie") == 0 && strcmp(context->program, "mg3") == 0) {
    spec.missile = "zombie_missile";
    return spec;
  }
  if (rocket) {
    spec.model = "ogre_rocket";
    spec.sight = "armagon/sight.wav";
  }
  return spec;
}

static void spawn_ordinary(qa_q1am_monster *monster, qa_q1am_context *context) {
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  char callback[220];
  game->combat_set_health(game->ctx, entity->actor, monster->spec.health);
  (void)snprintf(callback, sizeof callback, "%s:ordinary:monster_pain", context->program);
  entity->pain = game->named_pain(game->ctx, entity, callback);
  (void)snprintf(callback, sizeof callback, "%s:ordinary:monster_die", context->program);
  entity->die = game->named_die(game->ctx, entity, callback);
  (void)snprintf(callback, sizeof callback, "%s:ordinary:monster_stand", context->program);
  entity->path_end = game->named_action(game->ctx, entity, callback);
  const char *species = monster->spec.species;
  if (strcmp(species, "zombie") == 0 &&
      ((FLAGS(entity->spawnflags) & 1) != 0 ||
       (strcmp(context->program, "mg3") == 0 && (FLAGS(entity->spawnflags) & 8388608) != 0))) {
    entity->solid = "slidebox";
    entity->movement = "none";
    (void)snprintf(entity->model, sizeof entity->model, "%s", "progs/zombie.mdl");
    game->set_bounds(game->ctx, entity, small_bounds);
    game->link(game->ctx, entity);
    monster->ops.play(monster, strcmp(context->program, "mg3") == 0 &&
                                       (FLAGS(entity->spawnflags) & 8388608) != 0
                                   ? "zombie_hang1" : "zombie_cruc1");
    return;
  }
  if (strcmp(species, "fish") == 0)
    entity->spawnflags = (double)(FLAGS(entity->spawnflags) | 16384);
  if (strcmp(species, "wizard") != 0 && strcmp(species, "fish") != 0)
    context->set_number(context->ctx, entity, "allowPathFind", 1);
  context->set_number(context->ctx, entity, "combat_style",
                      strcmp(species, "knight") == 0 || strcmp(species, "demon") == 0 ||
                              strcmp(species, "tarbaby") == 0 || strcmp(species, "fish") == 0
                          ? 2
                          : strcmp(species, "ogre") == 0 || strcmp(species, "hellknight") == 0 ||
                                    strcmp(species, "shambler") == 0
                                ? 3 : 1);
  char model[192];
  (void)snprintf(model, sizeof model, "progs/%s.mdl", monster->spec.model);
  qa_q1am_init_mg3_monster(monster, context, model,
                           strcmp(species, "wizard") == 0 ? 2 : strcmp(species, "fish") == 0 ? 3 : 1,
                           strcmp(species, "demon") == 0 || strcmp(species, "ogre") == 0 ||
                                   strcmp(species, "shambler") == 0 || strcmp(species, "shalrath") == 0
                               ? 2 : 1);
}

static qa_q1am_ordinary_monster *as_ordinary(qa_q1am_monster *m) { return m->owner; }
static void ordinary_spawn(qa_q1am_monster *m) { spawn_ordinary(m, as_ordinary(m)->context); }
static void ordinary_start(qa_q1am_monster *m) { qa_q1am_start_mg3_monster(m, as_ordinary(m)->context); }
static void ordinary_use(qa_q1am_monster *m, bool has, qa_actor_id activator) {
  qa_q1am_ordinary_monster *self = as_ordinary(m);
  qa_actor_id resolved;
  bool resolved_has = qa_q1am_mg3_monster_activator(m->game, has, activator, &resolved);
  self->super_ops.use(m, resolved_has, resolved);
}
static void zombie_melee(qa_q1am_monster *m, qa_q1am_monster_ops *super_ops) {
  if (strcmp(m->spec.species, "zombie") != 0) {
    super_ops->melee_attack(m);
    return;
  }
  double r = m->game->random(m->game->ctx);
  m->ops.play(m, r < 0.3 ? "zombie_atta1" : r < 0.6 ? "zombie_attb1" : "zombie_attc1");
}
static void ordinary_melee(qa_q1am_monster *m) { zombie_melee(m, &as_ordinary(m)->super_ops); }
static void mg3_ordinary_spawn(qa_q1am_monster *m) {
  if (strcmp(m->spec.model, "ogre_rocket") == 0) {
    (void)snprintf(m->entity->classname, sizeof m->entity->classname, "%s", "monster_ogre");
    qa_q1am_actor_set_field(m->entity, "aflag", "1");
    qa_q1am_actor_set_field(m->entity, "projectiles_max", "2");
    qa_q1am_actor_set_field(m->entity, "projectiles", "2");
  }
  spawn_ordinary(m, as_ordinary(m)->context);
}
static void mg3_ordinary_pain(qa_q1am_monster *m, bool has, qa_actor_id attacker, double damage) {
  qa_q1am_ordinary_monster *self = as_ordinary(m);
  if (strcmp(m->spec.model, "ogre_rocket") != 0) {
    self->super_ops.pain(m, has, attacker, damage);
    return;
  }
  if (m->state->pain_finished > m->game->time || m->game->random(m->game->ctx) * 200 > damage) return;
  m->game->sound(m->game->ctx, m->entity, "armagon/pain.wav", "voice", 1);
  double r = m->game->random(m->game->ctx);
  m->state->pain_finished = m->game->time + (r < 0.75 ? 3 : 4);
  m->ops.play(m, r < 0.25 ? "ogre_pain1" : r < 0.5 ? "ogre_painb1" : r < 0.75 ? "ogre_painc1"
                          : r < 0.88 ? "ogre_paind1" : "ogre_paine1");
}
static void mg3_ordinary_die(qa_q1am_monster *m, bool has, qa_actor_id attacker) {
  qa_q1am_ordinary_monster *self = as_ordinary(m);
  if (strcmp(m->spec.model, "ogre_rocket") != 0 || m->game->health(m->game->ctx, m->entity->actor.id) < -80) {
    self->super_ops.die(m, has, attacker);
    return;
  }
  if (m->counted_death) return;
  m->state->has_enemy = has;
  m->state->enemy = attacker;
  qa_q1am_actor_set_damageable(m->game, m->entity, false);
  m->entity->touch.present = false;
  qa_q1am_monster_count_kill(m);
  m->game->sound(m->game->ctx, m->entity, "armagon/sight2.wav", "voice", 1);
  m->ops.play(m, m->game->random(m->game->ctx) < 0.5 ? "ogre_die1" : "ogre_bdie1");
}
static bool mg3_ordinary_try_attack(qa_q1am_monster *m) { return qa_q1am_mg3_ordinary_attack(m); }
static void mg3_ordinary_play(qa_q1am_monster *m, const char *name) {
  qa_q1am_ordinary_monster *self = as_ordinary(m);
  if (strcmp(name, "zombie_missile") == 0) {
    m->ops.melee_attack(m);
    return;
  }
  if (strcmp(m->spec.model, "ogre_rocket") == 0 && strcmp(name, "ogre_stand5") == 0)
    qa_q1am_rocket_ogre_frame(m, name);
  self->super_ops.play(m, name);
  if (strcmp(m->spec.model, "ogre_rocket") == 0 && strcmp(name, "ogre_stand5") != 0)
    qa_q1am_rocket_ogre_frame(m, name);
}

void qa_q1am_ordinary_monster_init(qa_q1am_ordinary_monster *monster, qa_q1am_context *context,
                                   qa_q1am_actor *entity, bool mg3) {
  memset(monster, 0, sizeof *monster);
  monster->context = context;
  qa_q1am_species spec = source_spec(context, entity);
  char prefix[80];
  (void)snprintf(prefix, sizeof prefix, "%s:ordinary", context->program);
  monster->source.callback_prefix = context->program; /* replaced below */
  const qa_q1am_frame_table *frames = extra_frames(context->game);
  if (mg3 && strcmp(qa_q1am_actor_text(entity, "addon.monsterClass"), "monster_ogre_rocket") == 0) {
    const qa_q1am_frame_table *rocket = qa_q1am_rocket_ogre_frames(context->game);
    size_t total = 0;
    monster->merged_storage[total++] = frames->frames[0];
    for (size_t i = 0; i < rocket->count && total < 16; i++) monster->merged_storage[total++] = rocket->frames[i];
    monster->merged_table.frames = monster->merged_storage;
    monster->merged_table.count = total;
    frames = &monster->merged_table;
  }
  /* callbackPrefix is a per-instance string in TS; keep it stable. */
  static char prefixes[8][80];
  static size_t prefix_count;
  const char *stable = NULL;
  for (size_t i = 0; i < prefix_count; i++)
    if (strcmp(prefixes[i], prefix) == 0) stable = prefixes[i];
  if (stable == NULL) {
    (void)snprintf(prefixes[prefix_count], 80, "%s", prefix);
    stable = prefixes[prefix_count++];
  }
  monster->source.callback_prefix = stable;
  monster->source.frames = frames;
  qa_q1am_monster_init(&monster->base, context->game, entity, &spec, context->base, &monster->source);
  monster->base.owner = monster;
  if (mg3) {
    qa_q1am_mg3_install(&monster->base);
    monster->base.kind = QA_Q1AM_KIND_MG3_ORDINARY;
    monster->super_ops = monster->base.ops;
    monster->base.ops.spawn = mg3_ordinary_spawn;
    monster->base.ops.pain = mg3_ordinary_pain;
    monster->base.ops.die = mg3_ordinary_die;
    monster->base.ops.start = ordinary_start;
    monster->base.ops.use = ordinary_use;
    monster->base.ops.try_attack = mg3_ordinary_try_attack;
    monster->base.ops.play = mg3_ordinary_play;
    monster->base.ops.melee_attack = ordinary_melee;
  } else {
    monster->base.kind = QA_Q1AM_KIND_ORDINARY;
    monster->super_ops = monster->base.ops;
    monster->base.ops.spawn = ordinary_spawn;
    monster->base.ops.start = ordinary_start;
    monster->base.ops.use = ordinary_use;
    monster->base.ops.melee_attack = ordinary_melee;
  }
}

typedef struct registry {
  qa_q1am_context *context;
  qa_q1am_ordinary_monster *head, *tail;
  struct registry *next;
  char prefix[80];
} registry;
static registry *all_registries;
static qa_q1am_ordinary_monster *registry_get(registry *r, qa_owned_actor actor) {
  for (qa_q1am_ordinary_monster *m = r->head; m; m = m->next)
    if (qa_q1am_same_owned(m->base.entity->actor, actor)) return m;
  return NULL;
}
static void registry_put(registry *r, qa_q1am_ordinary_monster *m) {
  qa_q1am_ordinary_monster *old = registry_get(r, m->base.entity->actor);
  if (old == m) return;
  if (old != NULL) {
    qa_q1am_ordinary_monster **link = &r->head;
    while (*link && *link != old) link = &(*link)->next;
    *link = old->next;
    if (r->tail == old) r->tail = NULL;
    free(old);
  }
  m->next = NULL;
  if (r->tail) r->tail->next = m;
  else r->head = m;
  r->tail = m;
}
static void registry_delete(registry *r, qa_owned_actor actor) {
  qa_q1am_ordinary_monster **link = &r->head;
  while (*link) {
    if (qa_q1am_same_owned((*link)->base.entity->actor, actor)) {
      qa_q1am_ordinary_monster *dead = *link;
      *link = dead->next;
      free(dead);
      return;
    }
    link = &(*link)->next;
  }
}
static void registry_clear(registry *r) {
  while (r->head) {
    qa_q1am_ordinary_monster *dead = r->head;
    r->head = dead->next;
    free(dead);
  }
  r->tail = NULL;
}
static qa_q1am_ordinary_monster *create_ordinary(registry *r, qa_q1am_actor *entity) {
  qa_q1am_ordinary_monster *m = malloc(sizeof *m);
  if (!m) qa_range_error("Ordinary monster allocation failed");
  qa_q1am_ordinary_monster_init(m, r->context, entity, strcmp(r->context->program, "mg3") == 0);
  registry_put(r, m);
  return m;
}
static qa_q1am_monster *lookup(void *ctx, qa_q1am_actor *entity) {
  qa_q1am_ordinary_monster *m = registry_get(ctx, entity->actor);
  if (m == NULL) qa_range_error("Missing ordinary addon source state");
  return &m->base;
}
static void on_release(void *ctx, qa_owned_actor actor) { registry_delete(ctx, actor); }
typedef struct spawn_ctx { registry *registry; char classname[96]; bool rocket; } spawn_ctx;
static void ordinary_spawn_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game;
  spawn_ctx *state = ctx;
  qa_q1am_actor_set_field(entity, "addon.monsterClass", state->classname);
  qa_q1am_ordinary_monster *value = create_ordinary(state->registry, entity);
  value->base.ops.spawn(&value->base);
}
static uint8_t *capture_all(void *ctx, size_t *out_length) {
  registry *r = ctx;
  qa_save_value *list = qa_save_value_array();
  for (qa_q1am_ordinary_monster *m = r->head; m; m = m->next) {
    qa_save_value *object = qa_save_value_object();
    qa_save_value_set(object, "slot", qa_save_value_number((double)m->base.entity->actor.id.slot));
    qa_save_value_set(object, "generation", qa_save_value_number((double)m->base.entity->actor.id.generation));
    qa_save_value_set(object, "state", qa_q1am_monster_capture(&m->base));
    qa_save_value_push(list, object);
  }
  uint8_t *bytes = qa_encode_checkpoint_value(list, out_length);
  qa_save_value_free(list);
  return bytes;
}
static void restore_all(void *ctx, const uint8_t *bytes, size_t length) {
  registry *r = ctx;
  registry_clear(r);
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
    bool resolved = r->context->game->actors_resolve_saved(
        r->context->game->ctx, (int64_t)qa_save_reader_integer_min(&slot, 0),
        (int64_t)qa_save_reader_integer_min(&generation, 0), &actor);
    qa_save_reader_destroy(&slot);
    qa_save_reader_destroy(&generation);
    qa_q1am_actor *entity = resolved ? r->context->game->entity(r->context->game->ctx, true, actor.id) : NULL;
    if (entity == NULL) qa_save_reader_fail(&reader, "missing ordinary addon monster");
    qa_q1am_ordinary_monster *value = create_ordinary(r, entity);
    qa_save_reader_field(&reader, "state", &state);
    qa_q1am_monster_restore(&value->base, &state);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&root);
  qa_save_value_free(decoded);
}
static void clone_one(void *ctx, qa_q1am_actor *source, qa_q1am_actor *target) {
  registry *r = ctx;
  qa_q1am_ordinary_monster *value = registry_get(r, source->actor);
  if (value == NULL) return;
  qa_q1am_ordinary_monster *copy = create_ordinary(r, target);
  qa_save_value *captured = qa_q1am_monster_capture(&value->base);
  qa_save_reader reader;
  qa_save_reader_init(&reader, captured, r->prefix);
  qa_q1am_monster_restore(&copy->base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
}

qa_q1am_monster *qa_q1am_ordinary_addon_monster(qa_q1am_context *context, qa_q1am_actor *entity) {
  for (registry *r = all_registries; r; r = r->next)
    if (r->context == context) {
      qa_q1am_ordinary_monster *m = registry_get(r, entity->actor);
      if (m) return &m->base;
      break;
    }
  return context->base->monsters_get(context->base->ctx, entity->actor);
}
qa_q1am_ordinary_monster *qa_q1am_register_ordinary_addon_monsters(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  (void)extra_frames(game);
  registry *r = calloc(1, sizeof *r);
  if (!r) qa_range_error("Ordinary monster allocation failed");
  r->context = context;
  (void)snprintf(r->prefix, sizeof r->prefix, "%s:ordinary", context->program);
  r->next = all_registries;
  all_registries = r;
  qa_q1am_monster_lookup lookup_value = {r, lookup};
  game->register_monster_callbacks(game->ctx, game, r->prefix, lookup_value);
  qa_q1am_register_mg3_monster_startup(game, r->prefix, lookup_value);
  for (size_t i = 0; i < qa_q1am_base_species_count; i++) {
    const qa_q1am_species *spec = &qa_q1am_base_species[i];
    if (strcmp(spec->species, "boss") == 0 || strcmp(spec->species, "oldone") == 0) continue;
    for (size_t c = 0; c < spec->classname_count; c++) {
      spawn_ctx *state = malloc(sizeof *state);
      if (!state) qa_range_error("Ordinary monster allocation failed");
      state->registry = r;
      (void)snprintf(state->classname, sizeof state->classname, "%s", spec->classnames[c]);
      game->replace_spawn(game->ctx, spec->classnames[c], ordinary_spawn_handler, state);
    }
  }
  if (strcmp(context->program, "mg3") == 0) {
    qa_q1am_register_rocket_ogre(context);
    spawn_ctx *state = malloc(sizeof *state);
    if (!state) qa_range_error("Ordinary monster allocation failed");
    state->registry = r;
    (void)snprintf(state->classname, sizeof state->classname, "%s", "monster_ogre_rocket");
    game->register_spawn(game->ctx, "monster_ogre_rocket", ordinary_spawn_handler, state);
  }
  game->actors_on_release(game->ctx, on_release, r);
  qa_q1am_state_extension extension = {r->prefix, r, capture_all, restore_all, clone_one};
  game->register_state_extension(game->ctx, extension);
  return r->head;
}
