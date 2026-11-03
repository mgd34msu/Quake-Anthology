/* Port of src/content/q1/addons/monsters/ordinary/index.ts.
 * quakec_{mg1,mg3}/monsters native monster admission. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/index.h"
#include "qa/foundation/support.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/ordinary/army.h"
#include "qa/content/q1/addons/monsters/ordinary/attack.h"
#include "qa/content/q1/addons/monsters/ordinary/chthon.h"
#include "qa/content/q1/addons/monsters/ordinary/classic.h"
#include "qa/content/q1/addons/monsters/ordinary/nightmare.h"
#include "qa/content/q1/addons/monsters/ordinary/rocket_ogre.h"
#include "qa/content/q1/addons/monsters/ordinary/slime.h"
#include "qa/content/q1/addons/monsters/ordinary/source_frames.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/base/provider.h"
#include "../support.h"

#define FLAGS QA_Q1AM_FLAGS

static const qa_bounds small_bounds = {{-16, -16, -24}, {16, 16, 32}};
static const qa_bounds dopa_small = {{-16, -16, -24}, {16, 16, 40}};
static const qa_bounds crucified = {{-16, -16, -24}, {16, 16, 40}};
static const qa_bounds large_bounds = {{-32, -32, -24}, {32, 32, 64}};

static bool same(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static const char *prefix_of(qa_q1ad_program program) {
  switch (program) {
    case QA_Q1AD_DOPA: return "dopa:ordinary";
    case QA_Q1AD_MG1: return "mg1:ordinary";
    case QA_Q1AD_MG3: return "mg3:ordinary";
    case QA_Q1AD_CTF: return "ctf:ordinary";
  }
  return "dopa:ordinary";
}

/* TS ordinarySpecies = [...baseSpecies.filter(not army or dog), armySpecies, dogSpecies]. */
static bool base_army_or_dog(const qa_q1_base_monster_species *spec) {
  return same(spec->species, "army") || same(spec->species, "dog");
}
static size_t ordinary_species_count(void) {
  size_t count = 2;
  for (size_t i = 0; i < QA_Q1_BASE_SPECIES_COUNT; i++)
    if (!base_army_or_dog(&QA_Q1_BASE_SPECIES[i])) count++;
  return count;
}
static const qa_q1_base_monster_species *ordinary_species(size_t index) {
  for (size_t i = 0; i < QA_Q1_BASE_SPECIES_COUNT; i++) {
    if (base_army_or_dog(&QA_Q1_BASE_SPECIES[i])) continue;
    if (index == 0) return &QA_Q1_BASE_SPECIES[i];
    index--;
  }
  return index == 0 ? &qa_q1am_army_species : &qa_q1am_dog_species;
}
/* TS sourceFrames(context, rocket): the generated compositions per `${program}:${rocket}`. */
static const qa_q1_monster_frame_table *source_frames(const qa_q1ad_context *context, bool rocket) {
  if (context->program == QA_Q1AD_MG1) return rocket ? &qa_q1am_ordinary_mg1_rocket_frames : &qa_q1am_ordinary_mg1_frames;
  if (context->program == QA_Q1AD_MG3) return rocket ? &qa_q1am_ordinary_mg3_rocket_frames : &qa_q1am_ordinary_mg3_frames;
  return rocket ? &qa_q1am_ordinary_other_rocket_frames : &qa_q1am_ordinary_other_frames;
}

/* TS actionTables: WeakMap<Q1AddonContext, ReadonlyMap<...>>. */
typedef struct action_table {
  const qa_q1ad_context *context;
  qa_q1_monster_action_entry *entries; size_t count, capacity;
  struct action_table *next;
} action_table;
static action_table *all_action_tables;
static void table_set(action_table *table, qa_q1_monster_action_entry entry) {
  for (size_t i = 0; i < table->count; i++)
    if (strcmp(table->entries[i].name, entry.name) == 0) { table->entries[i] = entry; return; } /* Map.set keeps the position */
  if (table->count == table->capacity) {
    const size_t capacity = table->capacity != 0 ? table->capacity * 2 : 16;
    qa_q1_monster_action_entry *grown = realloc(table->entries, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    table->entries = grown;
    table->capacity = capacity;
  }
  table->entries[table->count++] = entry;
}
static void table_add(action_table *table, const qa_q1_monster_action_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) table_set(table, entries[i]);
}
static const action_table *source_actions(qa_q1ad_context *context) {
  for (const action_table *table = all_action_tables; table != NULL; table = table->next)
    if (table->context == context) return table;
  action_table *table = calloc(1, sizeof *table);
  if (table == NULL) qa_range_error("out of memory");
  table->context = context;
  table_add(table, qa_q1am_classic_actions, qa_q1am_classic_action_count);
  if (context->program == QA_Q1AD_MG3) {
    table_add(table, qa_q1am_mg3_no_backpack_actions, qa_q1am_mg3_no_backpack_action_count);
    table_add(table, qa_q1am_mg3_nightmare_actions, qa_q1am_mg3_nightmare_action_count);
    table_set(table, (qa_q1_monster_action_entry){"tbaby_die2", qa_q1am_tarbaby_die2, context});
  } else {
    table_add(table, qa_q1am_mg1_horde_no_backpack_actions, qa_q1am_mg1_horde_no_backpack_action_count);
    table_add(table, qa_q1am_chthon_actions, qa_q1am_chthon_action_count);
  }
  table->next = all_action_tables;
  all_action_tables = table;
  return table;
}

/* TS sourceSpec(context, entity), written into *out. */
static void source_spec(const qa_q1ad_context *context, const qa_q1_actor *entity, qa_q1_base_monster_species *out) {
  const char *field = qa_q1_actor_text(entity, "addon.monsterClass");
  const char *classname = field[0] != '\0' ? field : entity->classname;
  const bool rocket = context->program == QA_Q1AD_MG3 && strcmp(classname, "monster_ogre_rocket") == 0;
  const char *wanted = rocket ? "monster_ogre" : classname;
  const qa_q1_base_monster_species *spec = NULL;
  for (size_t i = 0; i < ordinary_species_count() && spec == NULL; i++) {
    const qa_q1_base_monster_species *candidate = ordinary_species(i);
    for (size_t c = 0; c < candidate->classname_count && spec == NULL; c++)
      if (strcmp(candidate->classnames[c], wanted) == 0) spec = candidate;
  }
  if (spec == NULL) qa_range_error("Missing native addon monster %s", classname);
  const char *species = spec->species;
  const qa_bounds bounds = same(species, "demon") || same(species, "ogre") || same(species, "shambler") ||
                                   same(species, "shalrath") || same(species, "dog")
                               ? large_bounds : context->program == QA_Q1AD_DOPA ? dopa_small : small_bounds;
  *out = *spec;
  out->bounds = bounds;
  if (same(species, "zombie") && context->program == QA_Q1AD_MG3 && (FLAGS(entity->spawnflags) & 128) != 0) {
    out->missile = NULL;
    out->melee = true;
    return;
  }
  if (same(species, "zombie") && context->program != QA_Q1AD_DOPA) {
    out->missile = "zombie_missile";
    return;
  }
  if (rocket) {
    out->model = "ogre_rocket";
    out->sight = "armagon/sight.wav";
  }
}

static void spawn_ordinary(qa_q1_base_monster *monster, qa_q1ad_context *context) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  const char *prefix = prefix_of(context->program);
  char name[64];
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, spec->health);
  (void)snprintf(name, sizeof name, "%s:monster_pain", prefix);
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, name);
  (void)snprintf(name, sizeof name, "%s:monster_die", prefix);
  entity->die = qa_q1_callback_registry_die(game->named, entity, name);
  (void)snprintf(name, sizeof name, "%s:monster_stand", prefix);
  entity->path_end = qa_q1_callback_registry_action(game->named, entity, name);
  const char *species = spec->species;
  if (same(species, "zombie") && ((FLAGS(entity->spawnflags) & 1) != 0 ||
                                  (context->program == QA_Q1AD_MG3 && (FLAGS(entity->spawnflags) & 8388608) != 0))) {
    entity->solid = "slidebox";
    entity->movement = "none";
    qa_q1ad_set_text(&entity->model, "progs/zombie.mdl");
    qa_q1_set_bounds(game, entity, crucified);
    qa_q1_link(game, entity);
    qa_q1_base_monster_play(monster, context->program == QA_Q1AD_MG3 && (FLAGS(entity->spawnflags) & 8388608) != 0
                                         ? "zombie_hang1" : "zombie_cruc1");
    return;
  }
  if (same(species, "fish")) entity->spawnflags = FLAGS(entity->spawnflags) | 16384;
  if (same(species, "shambler") && context->program == QA_Q1AD_MG3 && qa_q1_option_skill(game) > 2) entity->count = 0;
  if (!same(species, "wizard") && !same(species, "fish")) qa_q1ad_set_number(context, entity, "allowPathFind", 1);
  qa_q1ad_set_number(context, entity, "combat_style",
                     same(species, "knight") || same(species, "demon") || same(species, "tarbaby") || same(species, "fish") ||
                             same(species, "dog")
                         ? 2
                     : same(species, "ogre") || same(species, "hellknight") || same(species, "shambler") ? 3 : 1);
  char model[96];
  (void)snprintf(model, sizeof model, "progs/%s.mdl", spec->model);
  qa_q1am_init_mg3_monster(monster, context, model, same(species, "wizard") ? 2 : same(species, "fish") ? 3 : 1,
                           same(species, "demon") || same(species, "ogre") || same(species, "shambler") ||
                                   same(species, "shalrath") || same(species, "dog")
                               ? 2 : 1);
}

/* ---- Q1OrdinaryMonster ---- */
static void ordinary_spawn(qa_q1_base_monster *monster);
static void ordinary_awake(qa_q1_base_monster *monster, const qa_actor_id *activator);
static void ordinary_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage);
static void ordinary_die(qa_q1_base_monster *monster, const qa_actor_id *attacker);
static void ordinary_start(qa_q1_base_monster *monster);
static void ordinary_use(qa_q1_base_monster *monster, const qa_actor_id *activator);
static bool ordinary_try_attack(qa_q1_base_monster *monster) { return qa_q1am_mg3_ordinary_attack(monster); }
static void ordinary_play(qa_q1_base_monster *monster, const char *name);
static void ordinary_melee_attack(qa_q1_base_monster *monster);
static void destroy(qa_q1_base_monster *monster) { free(monster); }
static const qa_q1_base_monster_ops ordinary_ops = {
  QA_Q1AM_MG3_MONSTER_OPS,
  .spawn = ordinary_spawn, .awake = ordinary_awake, .pain = ordinary_pain, .die = ordinary_die,
  .start = ordinary_start, .use = ordinary_use, .try_attack = ordinary_try_attack, .play = ordinary_play,
  .melee_attack = ordinary_melee_attack, .destroy = destroy,
};
bool qa_q1am_is_ordinary_monster(const qa_q1_base_monster *monster) { return monster->ops == &ordinary_ops; }
static qa_q1am_ordinary_monster *ordinary_of(qa_q1_base_monster *monster) { return (qa_q1am_ordinary_monster *)monster; }
static bool rocket_model(const qa_q1_base_monster *monster) { return same(monster->spec->model, "ogre_rocket"); }

bool qa_q1am_ordinary_monster_init(qa_q1am_ordinary_monster *monster, qa_q1ad_context *context, qa_q1_actor *entity) {
  memset(monster, 0, sizeof *monster);
  monster->context = context;
  const bool rocket = strcmp(qa_q1_actor_text(entity, "addon.monsterClass"), "monster_ogre_rocket") == 0;
  source_spec(context, entity, &monster->spec);
  const action_table *actions = source_actions(context);
  qa_q1_base_monster_source source;
  memset(&source, 0, sizeof source);
  source.callback_prefix = prefix_of(context->program);
  source.frames = source_frames(context, rocket);
  source.actions = actions->entries;
  source.action_count = actions->count;
  source.jump_attack_gate = true;
  return qa_q1am_mg3_monster_init(&monster->base, context->game, entity, &monster->spec,
                                  qa_q1_base_as_monster_services(context->base), &source, &ordinary_ops, NULL);
}
static void ordinary_spawn(qa_q1_base_monster *monster) {
  if (same(monster->spec->species, "boss")) { qa_q1_base_monster_spawn_super(monster); return; }
  if (rocket_model(monster)) {
    qa_q1_actor *entity = monster->entity;
    qa_q1ad_set_text(&entity->classname, "monster_ogre");
    qa_q1am_field_text(entity, "aflag", "1");
    qa_q1am_field_text(entity, "projectiles_max", "2");
    qa_q1am_field_text(entity, "projectiles", "2");
  }
  spawn_ordinary(monster, ordinary_of(monster)->context);
}
static void ordinary_awake(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  if (qa_q1am_killable_chthon(monster)) qa_q1am_awake_killable_chthon(monster, activator);
  else qa_q1_base_monster_awake_super(monster, activator);
}
static void ordinary_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage) {
  qa_q1ad_context *context = ordinary_of(monster)->context;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_monster *state = monster->state;
  if (qa_q1am_killable_chthon(monster)) { qa_q1am_chthon_pain(monster, attacker, damage); return; }
  if (same(monster->spec->species, "army")) { qa_q1am_army_pain(monster, attacker, damage, context->program == QA_Q1AD_MG3); return; }
  if (same(monster->spec->species, "dog")) { qa_q1am_dog_pain(monster, attacker); return; }
  if (same(monster->spec->species, "enforcer") && context->program == QA_Q1AD_MG3) {
    qa_q1_base_monster_retaliate(monster, attacker);
    const double r = qa_q1am_random(game);
    if (state->pain_finished > game->time || (qa_q1_option_skill(game) > 2 && qa_q1am_random(game) * 200 > damage)) return;
    qa_q1_sound(game, entity->actor.id, r < 0.5 ? "enforcer/pain1.wav" : "enforcer/pain2.wav", "voice", 1, 1);
    state->pain_finished = game->time + (r < 0.7 ? 1 : 2);
    qa_q1_base_monster_play(monster, r < 0.2 ? "enf_paina1" : r < 0.4 ? "enf_painb1" : r < 0.7 ? "enf_painc1" : "enf_paind1");
    return;
  }
  if (!rocket_model(monster)) { qa_q1_base_monster_pain_super(monster, attacker, damage); return; }
  if ((FLAGS(entity->movement_flags) & 32) != 0) qa_q1_base_monster_retaliate(monster, attacker);
  if (state->pain_finished > game->time || qa_q1am_random(game) * 200 > damage) return;
  qa_q1_sound_defaults(game, entity->actor.id, "armagon/pain.wav");
  const double r = qa_q1am_random(game);
  state->pain_finished = game->time + (r < 0.75 ? 3 : 4);
  qa_q1_base_monster_play(monster, r < 0.25 ? "ogre_pain1" : r < 0.5 ? "ogre_painb1" : r < 0.75 ? "ogre_painc1"
                                   : r < 0.88 ? "ogre_paind1" : "ogre_paine1");
}
static void ordinary_die(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (qa_q1am_killable_chthon(monster)) {
    qa_q1_base_monster_set_enemy(monster, attacker);
    qa_q1_actor_set_damageable(entity, false);
    memset(&entity->touch, 0, sizeof entity->touch);
    qa_q1_base_monster_play(monster, "boss_death1");
    return;
  }
  if (!rocket_model(monster) || qa_q1_health(game, entity->actor.id) < -80) { qa_q1_base_monster_die_super(monster, attacker); return; }
  if (monster->counted_death) return;
  qa_q1_base_monster_set_enemy(monster, attacker);
  qa_q1_actor_set_damageable(entity, false);
  memset(&entity->touch, 0, sizeof entity->touch);
  qa_q1_base_monster_count_kill(monster);
  qa_q1_sound_defaults(game, entity->actor.id, "armagon/sight2.wav");
  qa_q1_base_monster_play(monster, qa_q1am_random(game) < 0.5 ? "ogre_die1" : "ogre_bdie1");
}
static void ordinary_start(qa_q1_base_monster *monster) { qa_q1am_start_mg3_monster(monster, ordinary_of(monster)->context); }
static void ordinary_use(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_q1_services *game = monster->game;
  if (ordinary_of(monster)->context->program == QA_Q1AD_MG1) {
    if (activator == NULL || !qa_q1_is_player(game, activator)) return;
    qa_q1_base_monster_use_super(monster, activator);
    return;
  }
  /* MG3 monster_use tests the original activator before it looks for a living player. */
  if (activator != NULL && qa_q1_is_player(game, activator)) {
    const qa_q1_monster_target_result observed = qa_q1_monster_target(game, *activator);
    if (observed.has_value && (observed.value.invisible || observed.value.notarget)) return;
  }
  qa_actor_id resolved;
  const bool has = qa_q1am_mg3_monster_activator(game, activator, &resolved);
  qa_q1_base_monster_use_super(monster, has ? &resolved : NULL);
}
static void ordinary_play(qa_q1_base_monster *monster, const char *requested) {
  char name[64];
  qa_q1am_copy_name(name, sizeof name, requested); /* requested may alias next_frame */
  if (strcmp(name, "zombie_missile") == 0) { qa_q1_base_monster_melee_attack(monster); return; }
  const bool rocket = rocket_model(monster);
  if (rocket && strcmp(name, "ogre_stand5") == 0) qa_q1am_rocket_ogre_frame(monster, name);
  qa_q1_base_monster_play_super(monster, name);
  if (rocket && strcmp(name, "ogre_stand5") != 0) qa_q1am_rocket_ogre_frame(monster, name);
}
static void zombie_melee(qa_q1_base_monster *monster) {
  const double r = qa_q1am_random(monster->game);
  qa_q1_base_monster_play(monster, r < 0.3 ? "zombie_atta1" : r < 0.6 ? "zombie_attb1" : "zombie_attc1");
}
static void ordinary_melee_attack(qa_q1_base_monster *monster) {
  if (same(monster->spec->species, "dog")) { qa_q1_base_monster_play(monster, "dog_atta1"); return; }
  if (!same(monster->spec->species, "zombie")) { qa_q1_base_monster_melee_attack_super(monster); return; }
  zombie_melee(monster);
}

/* ---- Q1DopaOrdinaryMonster ---- */
static void dopa_spawn(qa_q1_base_monster *monster) {
  spawn_ordinary(monster, ((qa_q1am_dopa_ordinary_monster *)monster)->context);
}
static void dopa_start(qa_q1_base_monster *monster) {
  qa_q1am_start_mg3_monster(monster, ((qa_q1am_dopa_ordinary_monster *)monster)->context);
}
static void dopa_use(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_actor_id resolved;
  const bool has = qa_q1am_mg3_monster_activator(monster->game, activator, &resolved);
  qa_q1_base_monster_use_super(monster, has ? &resolved : NULL);
}
static void dopa_melee_attack(qa_q1_base_monster *monster) {
  if (!same(monster->spec->species, "zombie")) { qa_q1_base_monster_melee_attack_super(monster); return; }
  zombie_melee(monster);
}
static const qa_q1_base_monster_ops dopa_ops = {
  .spawn = dopa_spawn, .start = dopa_start, .use = dopa_use, .melee_attack = dopa_melee_attack, .destroy = destroy,
};
static bool dopa_init(qa_q1am_dopa_ordinary_monster *monster, qa_q1ad_context *context, qa_q1_actor *entity) {
  memset(monster, 0, sizeof *monster);
  monster->context = context;
  source_spec(context, entity, &monster->spec);
  qa_q1_base_monster_source source;
  memset(&source, 0, sizeof source);
  source.callback_prefix = prefix_of(context->program);
  source.frames = &qa_q1am_ordinary_hang_frames;
  if (!qa_q1_base_monster_init(&monster->base, context->game, entity, &monster->spec,
                               qa_q1_base_as_monster_services(context->base), &source))
    return false;
  monster->base.ops = &dopa_ops;
  return true;
}
/* TS createOrdinary(context, entity). */
static qa_q1_base_monster *create_ordinary(qa_q1ad_context *context, qa_q1_actor *entity) {
  if (context->program == QA_Q1AD_DOPA) {
    qa_q1am_dopa_ordinary_monster *monster = malloc(sizeof *monster);
    if (monster == NULL || !dopa_init(monster, context, entity)) qa_range_error("out of memory");
    return &monster->base;
  }
  qa_q1am_ordinary_monster *monster = malloc(sizeof *monster);
  if (monster == NULL || !qa_q1am_ordinary_monster_init(monster, context, entity)) qa_range_error("out of memory");
  return &monster->base.base;
}

/* ---- registerOrdinaryAddonMonsters ---- */
/* TS stores: WeakMap<Q1AddonContext, Map<OwnedActor, BaseMonster>>, plus the
 * closure state of its registration. */
typedef struct ordinary_store ordinary_store;
typedef struct ordinary_spawn_closure { ordinary_store *store; const char *classname; } ordinary_spawn_closure;
struct ordinary_store {
  qa_q1ad_context *context;
  struct { qa_owned_actor actor; qa_q1_base_monster *monster; } *entries;
  size_t count, capacity;
  qa_q1_base_monster **retired; size_t retired_count, retired_capacity;
  ordinary_spawn_closure *spawns; size_t spawn_count;
  void *monster_callbacks;
  int64_t release_subscription;
  char prefix[24];
  qa_q1_state_extension extension;
  ordinary_store *next;
};
static ordinary_store *all_stores;

static void *grow(void *items, size_t *capacity, size_t size) {
  const size_t next = *capacity != 0 ? *capacity * 2 : 8;
  void *grown = realloc(items, next * size);
  if (grown == NULL) qa_range_error("out of memory");
  *capacity = next;
  return grown;
}
static void retire(ordinary_store *store, qa_q1_base_monster *monster) {
  if (store->retired_count == store->retired_capacity) store->retired = grow(store->retired, &store->retired_capacity, sizeof *store->retired);
  store->retired[store->retired_count++] = monster;
}
static qa_q1_base_monster *store_get(const ordinary_store *store, qa_owned_actor actor) {
  for (size_t i = 0; i < store->count; i++)
    if (qa_same_owned_actor(store->entries[i].actor, actor)) return store->entries[i].monster;
  return NULL;
}
static void store_set(void *ctx, qa_owned_actor actor, qa_q1_base_monster *monster) {
  ordinary_store *store = ctx;
  for (size_t i = 0; i < store->count; i++)
    if (qa_same_owned_actor(store->entries[i].actor, actor)) {
      if (store->entries[i].monster != monster) retire(store, store->entries[i].monster);
      store->entries[i].monster = monster;
      return;
    }
  if (store->count == store->capacity) store->entries = grow(store->entries, &store->capacity, sizeof *store->entries);
  store->entries[store->count].actor = actor;
  store->entries[store->count].monster = monster;
  store->count++;
}
static ordinary_store *store_for(const qa_q1ad_context *context) {
  for (ordinary_store *store = all_stores; store != NULL; store = store->next)
    if (store->context == context) return store; /* newest registration first, like WeakMap.set */
  return NULL;
}
qa_q1_base_monster *qa_q1am_ordinary_addon_monster(qa_q1ad_context *context, qa_q1_actor *entity) {
  const ordinary_store *store = store_for(context);
  qa_q1_base_monster *monster = store == NULL ? NULL : store_get(store, entity->actor);
  return monster != NULL ? monster : qa_q1_creatures_monsters_get(qa_q1_base_creatures(context->base), entity->actor);
}
/* TS monster(entity). */
static qa_q1_base_monster *lookup(void *ctx, qa_q1_actor *entity) {
  qa_q1_base_monster *value = store_get(ctx, entity->actor);
  if (value == NULL) qa_range_error("Missing ordinary addon source state");
  return value;
}
static qa_q1_base_monster *chthon_lookup(void *ctx, qa_q1_actor *entity) { return store_get(ctx, entity->actor); }
static void dog_jump_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                           const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface; /* TS (_game, entity, other) */
  qa_q1am_dog_jump_touch(lookup(ctx, entity), other);
}
static void spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* TS (_game, entity) */
  const ordinary_spawn_closure *closure = ctx;
  qa_q1am_field_text(entity, "addon.monsterClass", closure->classname);
  qa_q1_base_monster *value = create_ordinary(closure->store->context, entity);
  store_set(closure->store, entity->actor, value);
  qa_q1_base_monster_spawn(value);
}
static void on_release(void *ctx, qa_owned_actor actor) {
  ordinary_store *store = ctx;
  for (size_t i = 0; i < store->count; i++)
    if (qa_same_owned_actor(store->entries[i].actor, actor)) {
      retire(store, store->entries[i].monster);
      memmove(&store->entries[i], &store->entries[i + 1], (store->count - i - 1) * sizeof *store->entries);
      store->count--;
      return;
    }
}
static void must(bool ok) { if (!ok) qa_range_error("out of memory"); }
static void set_child(qa_save_value *object, const char *key, qa_save_value *child) {
  must(child != NULL && qa_save_value_set(object, key, child));
}
static bool capture(void *ctx, uint8_t **out, size_t *out_length) {
  ordinary_store *store = ctx;
  qa_save_value *list = qa_save_value_array();
  if (list == NULL) return false;
  for (size_t i = 0; i < store->count; i++) {
    const qa_q1_base_monster *value = store->entries[i].monster;
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
  ordinary_store *store = ctx;
  qa_q1_services *game = store->context->game;
  for (size_t i = 0; i < store->count; i++) retire(store, store->entries[i].monster);
  store->count = 0;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  qa_save_reader root;
  must(qa_save_reader_init(&root, decoded, store->prefix));
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
    if (entity == NULL) qa_save_reader_fail(&reader, "missing ordinary addon monster");
    qa_q1_base_monster *value = create_ordinary(store->context, entity);
    must(qa_save_reader_field(&reader, "state", &state));
    qa_q1_base_monster_restore(value, &state);
    qa_save_reader_destroy(&state);
    store_set(store, entity->actor, value);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&root);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}
static void clone(void *ctx, qa_q1_actor *source, qa_q1_actor *target) {
  ordinary_store *store = ctx;
  qa_q1_base_monster *value = store_get(store, source->actor);
  if (value == NULL) return;
  qa_q1_base_monster *copy = create_ordinary(store->context, target);
  qa_save_value *captured = qa_q1_base_monster_capture(value);
  must(captured != NULL);
  qa_save_reader reader;
  must(qa_save_reader_init(&reader, captured, store->prefix));
  qa_q1_base_monster_restore(copy, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
  store_set(store, target->actor, copy);
}
static bool admitted(const qa_q1ad_context *context, const qa_q1_base_monster_species *spec) {
  return !(same(spec->species, "oldone") || (same(spec->species, "boss") && context->program != QA_Q1AD_MG1) ||
           (context->program == QA_Q1AD_DOPA && (same(spec->species, "army") || same(spec->species, "dog"))));
}
void qa_q1am_register_ordinary_addon_monsters(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  ordinary_store *store = calloc(1, sizeof *store);
  if (store == NULL) qa_range_error("out of memory");
  store->context = context;
  (void)snprintf(store->prefix, sizeof store->prefix, "%s", prefix_of(context->program));
  store->next = all_stores;
  all_stores = store;
  store->monster_callbacks = qa_q1_register_monster_callbacks(game, store->prefix, lookup, store);
  qa_q1am_register_mg3_monster_startup(game, store->prefix, lookup, store);
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = store;
  handlers.touch = dog_jump_touch;
  char name[64];
  (void)snprintf(name, sizeof name, "%s:dog_jump_touch", store->prefix);
  qa_q1_callback_registry_register(game->named, name, handlers);
  size_t total = 1; /* + monster_ogre_rocket */
  for (size_t i = 0; i < ordinary_species_count(); i++) total += ordinary_species(i)->classname_count;
  store->spawns = calloc(total, sizeof *store->spawns);
  if (store->spawns == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < ordinary_species_count(); i++) {
    const qa_q1_base_monster_species *spec = ordinary_species(i);
    if (!admitted(context, spec)) continue;
    for (size_t c = 0; c < spec->classname_count; c++) {
      ordinary_spawn_closure *closure = &store->spawns[store->spawn_count++];
      closure->store = store;
      closure->classname = spec->classnames[c];
      if (qa_q1_has_spawn(game, closure->classname)) qa_q1_replace_spawn(game, closure->classname, spawn, closure);
      else qa_q1_register_spawn(game, closure->classname, spawn, closure);
    }
  }
  if (context->program == QA_Q1AD_MG1) qa_q1am_register_chthon(context, chthon_lookup, store);
  if (context->program == QA_Q1AD_MG3) {
    qa_q1am_register_slime(context, (qa_q1am_monster_store){store, store_set}, create_ordinary);
    qa_q1am_register_rocket_ogre(context);
    qa_q1am_register_mg3_nightmare(context);
    ordinary_spawn_closure *closure = &store->spawns[store->spawn_count++];
    closure->store = store;
    closure->classname = "monster_ogre_rocket";
    qa_q1_register_spawn(game, closure->classname, spawn, closure);
  }
  store->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, store);
  store->extension.ctx = store;
  store->extension.id = store->prefix;
  store->extension.capture = capture;
  store->extension.restore = restore;
  store->extension.clone = clone;
  qa_q1_register_state_extension(game, &store->extension);
}
void qa_q1am_release_ordinary_addon_monsters(qa_q1_services *game) {
  for (ordinary_store **link = &all_stores; *link != NULL;) {
    ordinary_store *store = *link;
    if (store->context->game != game) { link = &store->next; continue; }
    *link = store->next;
    qa_session_actor_registry_off_release(game->host.actors, store->release_subscription);
    for (size_t i = 0; i < store->count; i++) qa_q1_base_monster_destroy(store->entries[i].monster);
    for (size_t i = 0; i < store->retired_count; i++) qa_q1_base_monster_destroy(store->retired[i]);
    free(store->entries);
    free(store->retired);
    free(store->spawns);
    free(store->monster_callbacks);
    free(store);
  }
  for (action_table **link = &all_action_tables; *link != NULL;) {
    action_table *table = *link;
    if (table->context->game != game) { link = &table->next; continue; }
    *link = table->next;
    free(table->entries);
    free(table);
  }
  qa_q1am_release_chthon(game);
  qa_q1am_release_slime(game);
}
