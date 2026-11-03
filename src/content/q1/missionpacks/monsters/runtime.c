/* Port of src/content/q1/missionpacks/monsters/runtime.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/runtime.h"
#include "qa/foundation/support.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/charm.h"
#include "qa/content/q1/missionpacks/monsters/species_variants.h"
#include "monsters_internal.h"

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  return copy;
}
static bool is_pack(const qa_q1mpm_runtime *runtime, const char *pack) {
  return runtime->pack != NULL && strcmp(runtime->pack, pack) == 0;
}
static qa_q1mpm_mission_monster *mission(qa_q1_base_monster *monster) {
  return (qa_q1mpm_mission_monster *)monster;
}
static bool base_behavior(const qa_q1mpm_mission_monster *monster) {
  return monster->definition->has_base_behavior && monster->definition->base_behavior;
}

/* TS Q1Base as MonsterServices (runtime.base passed to `super`). */
static bool services_count(void *ctx, qa_q1_base_monster *monster) {
  return qa_q1_base_count_monster_kill(ctx, monster);
}
static const char *services_melee(void *ctx) { return qa_q1_base_next_hell_knight_melee(ctx); }
static void services_finale(void *ctx, qa_q1_base_monster *monster) { qa_q1_base_finale(ctx, monster); }
static void services_finish(void *ctx, qa_q1_base_monster *monster) { qa_q1_base_finish_finale(ctx, monster); }
static qa_q1_monster_services base_services(qa_q1_base *base) {
  qa_q1_monster_services services = {.ctx = base, .count_monster_kill = services_count,
                                     .next_hell_knight_melee = services_melee,
                                     .finale = services_finale, .finish_finale = services_finish};
  return services;
}

/* TS BaseMonsterSource actions: `(monster) => action(runtime.require(monster.entity))`.
 * Each entry's own ctx is its definition action (the closure's captured
 * action); the runtime is the controller's. */
static void run_source_action(void *ctx, qa_q1_base_monster *monster) {
  const qa_q1mpm_named_action *entry = ctx;
  qa_q1mpm_mission_monster *mission = qa_q1mpm_mission_monster_of(monster);
  if (mission == NULL) qa_range_error("Mission-pack action on a foreign monster controller");
  entry->action.fn(entry->action.ctx, qa_q1mpm_runtime_require(mission->runtime, monster->entity));
}

static void push_registered(qa_q1mpm_runtime *runtime, qa_q1mpm_registered_definition *registered) {
  if (runtime->registered_count == runtime->registered_capacity) {
    size_t capacity = runtime->registered_capacity == 0 ? 16 : runtime->registered_capacity * 2;
    qa_q1mpm_registered_definition **next = realloc(runtime->registered, capacity * sizeof *next);
    if (next == NULL) qa_range_error("out of memory");
    runtime->registered = next;
    runtime->registered_capacity = capacity;
  }
  runtime->registered[runtime->registered_count++] = registered;
}
static qa_q1mpm_registered_definition *registered_for(qa_q1mpm_runtime *runtime,
                                                      const qa_q1mpm_pack_monster_definition *definition) {
  for (size_t i = 0; i < runtime->registered_count; i++)
    if (runtime->registered[i]->definition == definition) return runtime->registered[i];
  qa_q1mpm_registered_definition *registered = calloc(1, sizeof *registered);
  if (registered == NULL) qa_range_error("out of memory");
  registered->runtime = runtime;
  registered->definition = definition;
  if (definition->action_count > 0) {
    registered->source_actions = calloc(definition->action_count, sizeof *registered->source_actions);
    if (registered->source_actions == NULL) qa_range_error("out of memory");
  }
  for (size_t i = 0; i < definition->action_count; i++) {
    registered->source_actions[i].name = definition->actions[i].name;
    registered->source_actions[i].fn = run_source_action;
    registered->source_actions[i].ctx = (void *)&definition->actions[i];
  }
  registered->source_action_count = definition->action_count;
  push_registered(runtime, registered);
  return registered;
}
static const qa_q1mpm_named_action *definition_action(const qa_q1mpm_pack_monster_definition *definition,
                                                      const char *name) {
  for (size_t i = 0; i < definition->action_count; i++)
    if (strcmp(definition->actions[i].name, name) == 0) return &definition->actions[i];
  return NULL;
}

/* ---- MissionMonster overrides (qa_q1_base_monster_ops) ---- */

static void mm_spawn(qa_q1_base_monster *base) {
  qa_q1mpm_mission_monster *monster = mission(base);
  if (monster->definition->spawn.fn == NULL) qa_q1mpm_monster_spawn_default(monster);
  else monster->definition->spawn.fn(monster->definition->spawn.ctx, monster);
}
void qa_q1mpm_monster_spawn_default(qa_q1mpm_mission_monster *monster) {
  qa_q1_base_monster_spawn_super(&monster->base);
}
static void mm_start(qa_q1_base_monster *base) {
  qa_q1mpm_mission_monster *monster = mission(base);
  if (monster->definition->start.fn == NULL) qa_q1mpm_monster_start_default(monster);
  else monster->definition->start.fn(monster->definition->start.ctx, monster);
}
void qa_q1mpm_monster_start_default(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_actor *found[1];
  const char *path = monster->base.state->path == NULL ? "" : monster->base.state->path;
  const qa_actor_id *path_id = qa_q1_find(game, path, found, 1) > 0 ? &found[0]->actor.id : NULL;
  (void)qa_q1_actor_reference_set(entity, "goalentity", path_id);
  (void)qa_q1_actor_reference_set(entity, "movetarget", path_id);
  qa_q1_base_monster_start_super(&monster->base);
}
void qa_q1mpm_monster_use_default(qa_q1mpm_mission_monster *monster, const qa_actor_id *activator) {
  qa_q1_base_monster_use_super(&monster->base, activator);
}
bool qa_q1mpm_monster_gib_death_default(qa_q1mpm_mission_monster *monster) {
  return qa_q1_base_monster_gib_death_super(&monster->base);
}
/* TS protected override sfx(path): definition.sfx?.(this, path) ?? path. */
const char *qa_q1mpm_monster_sfx(qa_q1mpm_mission_monster *monster, const char *path) {
  const qa_q1mpm_pack_monster_definition *definition = monster->definition;
  return definition->sfx == NULL ? path : definition->sfx(definition->sfx_ctx, monster, path);
}
/* TS protected override gibDeath(). */
bool qa_q1mpm_monster_gib_death(qa_q1mpm_mission_monster *monster) {
  const qa_q1mpm_pack_monster_definition *definition = monster->definition;
  return definition->gib_death == NULL ? qa_q1mpm_monster_gib_death_default(monster)
                                       : definition->gib_death(definition->gib_death_ctx, monster);
}
static const char *mm_sfx(qa_q1_base_monster *base, const char *path) { return qa_q1mpm_monster_sfx(mission(base), path); }
static bool mm_gib_death(qa_q1_base_monster *base) { return qa_q1mpm_monster_gib_death(mission(base)); }
static void mm_play(qa_q1_base_monster *base, const char *requested) {
  qa_q1mpm_mission_monster *monster = mission(base);
  const char *name = monster->definition->redirect == NULL
                         ? requested
                         : monster->definition->redirect(monster->definition->redirect_ctx, monster, requested);
  if (!qa_q1_monster_frame_table_has(&monster->definition->frames, name)) {
    const qa_q1mpm_named_action *action = definition_action(monster->definition, name);
    if (action != NULL) { action->action.fn(action->action.ctx, monster); return; }
  }
  qa_q1_base_monster_play_super(base, name);
}
static void mm_pain(qa_q1_base_monster *base, const qa_actor_id *attacker, double damage) {
  qa_q1mpm_mission_monster *monster = mission(base);
  const qa_q1mpm_pack_monster_definition *definition = monster->definition;
  if (definition->pre_pain != NULL) {
    if ((qa_js_to_int32(base->entity->movement_flags) & 32) != 0) qa_q1_base_monster_retaliate(base, attacker);
    if (definition->pre_pain(definition->pre_pain_ctx, monster, attacker, damage)) return;
  }
  if (base_behavior(monster)) { qa_q1_base_monster_pain_super(base, attacker, damage); return; }
  qa_q1_base_monster_retaliate(base, attacker);
  monster->definition->pain(monster->definition->pain_ctx, monster, attacker, damage);
}
void qa_q1mpm_monster_pain_default(qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  qa_q1_base_monster_pain_super(&monster->base, attacker, damage);
}
void qa_q1mpm_monster_die_default(qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  qa_q1_base_monster_die_super(&monster->base, attacker);
}
static void mm_die(qa_q1_base_monster *base, const qa_actor_id *attacker) {
  qa_q1mpm_mission_monster *monster = mission(base);
  qa_q1_services *game = base->game;
  qa_q1_actor *entity = base->entity;
  if (qa_q1_actor_number(entity, "charmed") != 0) entity->effects = qa_js_bit_and(entity->effects, qa_js_bit_not(8));
  if (base_behavior(monster)) { qa_q1_base_monster_die_super(base, attacker); return; }
  if (base->counted_death) return;
  qa_q1_base_monster_set_enemy(base, attacker);
  if (qa_q1_health(game, entity->actor.id) < -99) qa_gameplay_authority_set_health(game->host.combat, entity->actor, -99);
  qa_q1_actor_set_damageable(entity, false);
  entity->touch = (qa_q1_bound_touch){0};
  qa_q1_base_monster_count_kill(base);
  monster->definition->die(monster->definition->die_ctx, monster, attacker);
}
static void mm_melee_attack(qa_q1_base_monster *base) {
  qa_q1mpm_mission_monster *monster = mission(base);
  if (base_behavior(monster)) qa_q1_base_monster_melee_attack_super(base);
  else if (monster->definition->melee.fn != NULL) monster->definition->melee.fn(monster->definition->melee.ctx, monster);
}
static bool mm_try_attack(qa_q1_base_monster *base) {
  qa_q1mpm_mission_monster *monster = mission(base);
  return monster->definition->check_attack == NULL
             ? qa_q1_base_monster_try_attack_super(base)
             : monster->definition->check_attack(monster->definition->check_attack_ctx, monster);
}
bool qa_q1mpm_monster_check_attack_default(qa_q1mpm_mission_monster *monster) {
  return qa_q1_base_monster_try_attack_super(&monster->base);
}
static void mm_found(qa_q1_base_monster *base, qa_actor_id target) {
  qa_q1mpm_mission_monster *monster = mission(base);
  qa_actor_id owner;
  if (qa_q1mpm_charmer(monster, &owner)) {
    qa_q1_actor *other = qa_q1_get_entity(base->game, &target);
    qa_actor_id other_charmer;
    if (qa_same_actor(target, owner) ||
        (other != NULL && qa_q1_actor_reference_get(other, "charmer", &other_charmer) &&
         qa_same_actor(other_charmer, owner))) {
      qa_q1_base_monster_set_enemy(base, NULL);
      return;
    }
  }
  if (monster->definition->found == NULL) qa_q1mpm_monster_found_default(monster, target);
  else monster->definition->found(monster->definition->found_ctx, monster, target);
}
void qa_q1mpm_monster_found_default(qa_q1mpm_mission_monster *monster, qa_actor_id target) {
  qa_q1_base_monster *base = &monster->base;
  if (!is_pack(monster->runtime, "hipnotic")) { qa_q1_base_monster_found_super(base, target); return; }
  qa_q1_services *game = base->game;
  qa_q1_actor *entity = base->entity;
  const qa_q1_base_monster_species *spec = base->spec;
  qa_q1_base_monster_set_enemy(base, &target);
  if (qa_q1_is_player(game, &target)) { game->sight_entity = entity; game->sight_time = game->time; }
  q1mpm_set_fround_text(entity, "show_hostile", game->time + 1);
  const char *sound = spec->sight;
  char enforcer[32];
  if (strcmp(spec->species, "enforcer") == 0) {
    double r = floor(qa_q1_host_random(game) * 3 + 0.5);
    snprintf(enforcer, sizeof enforcer, "enforcer/sight%d.wav", r == 1 ? 1 : r == 2 ? 2 : r == 0 ? 3 : 4);
    sound = enforcer;
  } else if (strcmp(spec->species, "fish") == 0 ||
             (strcmp(spec->species, "gremlin") == 0 && qa_q1_actor_number(entity, "stoleweapon") != 0)) {
    sound = "";
  }
  if (sound[0] != '\0') qa_q1_sound_defaults(game, entity->actor.id, sound);
  base->state->mode = "run";
  q1mpm_set_next_frame(base, spec->run);
  (void)qa_q1_actor_reference_set(entity, "goalentity", &target);
  qa_q1_body body;
  qa_vec3 origin = q1mpm_read_body(game, target, &body) ? body.origin : QA_Q1_ZERO;
  entity->ideal_yaw = qa_q1_yaw_for(qa_q1_vsub(origin, qa_q1_base_monster_origin(base)));
  qa_q1_base_monster_delay(base, 0.1);
  qa_q1_base_monster_attack_finished(base, 1);
}
static void mm_ai(qa_q1_base_monster *base, qa_q1_monster_ai mode, double distance) {
  qa_q1mpm_mission_monster *monster = mission(base);
  if (mode == QA_Q1_AI_WALK && qa_q1mpm_walk_with_charmer(monster, distance)) return;
  if (monster->definition->ai == NULL) qa_q1_base_monster_ai_super(base, mode, distance);
  else monster->definition->ai(monster->definition->ai_ctx, monster, mode, distance);
}
void qa_q1mpm_monster_ai_default(qa_q1mpm_mission_monster *monster, qa_q1_monster_ai mode, double distance) {
  qa_q1_base_monster_ai_super(&monster->base, mode, distance);
}
static bool mm_fires_through_blockers(qa_q1_base_monster *base) {
  qa_actor_id owner;
  return qa_q1mpm_charmer(mission(base), &owner);
}
static void mm_run(qa_q1_base_monster *base, double distance) {
  qa_q1mpm_mission_monster *monster = mission(base);
  qa_q1_services *game = base->game;
  qa_q1_actor *entity = base->entity;
  qa_actor_id owner;
  if (qa_q1mpm_charmer(monster, &owner)) {
    const qa_actor_id *enemy = qa_q1_base_monster_enemy(base);
    if (enemy == NULL || qa_same_actor(*enemy, owner) || qa_q1_health(game, *enemy) <= 0) {
      qa_q1_base_monster_set_enemy(base, NULL);
      qa_q1mpm_hunt_charmer(monster, false);
      return;
    }
  }
  if (is_pack(monster->runtime, "hipnotic")) q1mpm_set_fround_text(entity, "show_hostile", game->time + 1);
  if (strcmp(entity->attack_state, "dodging") != 0) { qa_q1_base_monster_run_super(base, distance); return; }
  const qa_actor_id *enemy = qa_q1_base_monster_enemy(base);
  if (enemy == NULL || qa_q1_health(game, *enemy) <= 0) {
    entity->attack_state = "straight";
    qa_q1_base_monster_run_super(base, distance);
    return;
  }
  qa_vec3 target;
  if (!qa_q1_base_monster_target(base, &target)) return;
  bool seen = qa_q1_base_monster_visible(base, qa_q1_base_monster_enemy(base));
  if (seen) base->state->search_until = game->time + 5;
  if (qa_q1_base_monster_search_for_coop_target(base)) return;
  (void)qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  if (seen && qa_q1_base_monster_try_attack(base)) return;
  qa_q1_base_monster_delay(base, 0.1);
  double offset = base->lefty ? 40 : -40;
  if (game->time > qa_q1_actor_number(entity, "ltime")) {
    base->lefty = !base->lefty;
    q1mpm_set_fround_text(entity, "ltime", game->time + 0.8);
  }
  entity->ideal_yaw = qa_q1_yaw_for(qa_q1_vsub(target, qa_q1_base_monster_origin(base)));
  if (game->host.walk_move(game->host.ctx, entity->actor, entity->ideal_yaw + offset, distance)) {
    qa_q1_base_monster_change_yaw(base);
    return;
  }
  base->lefty = !base->lefty;
  q1mpm_set_fround_text(entity, "ltime", game->time + 0.8);
  (void)game->host.walk_move(game->host.ctx, entity->actor, entity->ideal_yaw - offset, distance);
  qa_q1_base_monster_change_yaw(base);
}
static void mm_move_to_enemy(qa_q1_base_monster *base, double distance) {
  qa_q1mpm_mission_monster *monster = mission(base);
  qa_q1_services *game = base->game;
  qa_q1_actor *entity = base->entity;
  if (is_pack(monster->runtime, "hipnotic") &&
      (game->world == NULL ? 0 : qa_q1_actor_number(game->world, "RUN_STRAIGHT")) != 0 &&
      game->time > qa_q1_actor_number(entity, "endtime")) {
    if (game->world != NULL) (void)qa_q1_actor_field_set(game->world, "RUN_STRAIGHT", "0");
    if (game->host.walk_move(game->host.ctx, entity->actor, qa_q1_get_body(game, entity).angles.y, distance)) return;
    q1mpm_set_fround_text(entity, "endtime", game->time + 3);
  }
  qa_q1_base_monster_move_to_enemy_super(base, distance);
}
static bool mm_search_for_coop_target(qa_q1_base_monster *base) {
  return qa_q1_actor_number(base->entity, "charmed") == 0 && qa_q1_base_monster_search_for_coop_target_super(base);
}
static qa_q1_basis mm_make_vectors(qa_q1_base_monster *base) {
  return qa_q1_make_vectors(base->game, qa_q1_get_body(base->game, base->entity).angles);
}
static bool mm_find_target(qa_q1_base_monster *base) {
  qa_q1mpm_mission_monster *monster = mission(base);
  if (!is_pack(monster->runtime, "hipnotic")) return qa_q1_base_monster_find_target_super(base);
  bool is_null = false;
  bool found = qa_q1mpm_find_charmed_target(monster, &is_null);
  return is_null ? qa_q1mpm_find_hipnotic_target(monster) : found;
}
static void mm_retaliate(qa_q1_base_monster *base, const qa_actor_id *attacker) {
  qa_actor_id owner;
  if (attacker != NULL && qa_q1mpm_charmer(mission(base), &owner) && qa_same_actor(*attacker, owner)) return;
  qa_q1_base_monster_retaliate_super(base, attacker);
}
static void mm_use(qa_q1_base_monster *base, const qa_actor_id *activator) {
  qa_q1mpm_mission_monster *monster = mission(base);
  if (monster->definition->use == NULL) qa_q1_base_monster_use_super(base, activator);
  else monster->definition->use(monster->definition->use_ctx, monster, activator);
}
static void mm_destroy(qa_q1_base_monster *base) { free(mission(base)); }

static const qa_q1_base_monster_ops MISSION_MONSTER_OPS = {
    .spawn = mm_spawn,
    .start = mm_start,
    .play = mm_play,
    .pain = mm_pain,
    .die = mm_die,
    .use = mm_use,
    .ai = mm_ai,
    .run = mm_run,
    .move_to_enemy = mm_move_to_enemy,
    .found = mm_found,
    .find_target = mm_find_target,
    .try_attack = mm_try_attack,
    .melee_attack = mm_melee_attack,
    .make_vectors = mm_make_vectors,
    .search_for_coop_target = mm_search_for_coop_target,
    .retaliate = mm_retaliate,
    .fires_through_blockers = mm_fires_through_blockers,
    .sfx = mm_sfx,
    .gib_death = mm_gib_death,
    .destroy = mm_destroy,
};

qa_q1mpm_mission_monster *qa_q1mpm_mission_monster_create(qa_q1_services *game, qa_q1_actor *entity,
                                                          const qa_q1mpm_pack_monster_definition *definition,
                                                          qa_q1mpm_runtime *runtime) {
  qa_q1mpm_registered_definition *registered = registered_for(runtime, definition);
  qa_q1mpm_mission_monster *monster = malloc(sizeof *monster);
  if (monster == NULL) qa_range_error("out of memory");
  const qa_q1_base_monster_source source = {.callback_prefix = runtime->pack,
                                            .frames = &definition->frames,
                                            .actions = registered->source_actions,
                                            .action_count = registered->source_action_count,
                                            .actions_ctx = registered};
  if (!qa_q1_base_monster_init(&monster->base, game, entity, &definition->spec, base_services(runtime->base),
                               &source))
    qa_range_error("out of memory");
  monster->base.ops = &MISSION_MONSTER_OPS;
  monster->definition = definition;
  monster->runtime = runtime;
  return monster;
}
qa_q1mpm_mission_monster *qa_q1mpm_mission_monster_of(qa_q1_base_monster *monster) {
  return monster != NULL && monster->ops == &MISSION_MONSTER_OPS ? mission(monster) : NULL;
}

/* ---- Q1MissionPackMonsters ---- */

static void retire(qa_q1mpm_runtime *runtime, qa_q1mpm_mission_monster *monster) {
  if (runtime->retired_count == runtime->retired_capacity) {
    size_t capacity = runtime->retired_capacity == 0 ? 16 : runtime->retired_capacity * 2;
    qa_q1mpm_mission_monster **next = realloc(runtime->retired, capacity * sizeof *next);
    if (next == NULL) qa_range_error("out of memory");
    runtime->retired = next;
    runtime->retired_capacity = capacity;
  }
  runtime->retired[runtime->retired_count++] = monster;
}
qa_q1mpm_mission_monster *qa_q1mpm_runtime_get(qa_q1mpm_runtime *runtime, qa_owned_actor actor) {
  for (size_t i = 0; i < runtime->monster_count; i++)
    if (qa_same_owned_actor(runtime->monsters[i].actor, actor)) return runtime->monsters[i].monster;
  return NULL;
}
static void delete_monster(qa_q1mpm_runtime *runtime, qa_owned_actor actor) {
  for (size_t i = 0; i < runtime->monster_count; i++) {
    if (!qa_same_owned_actor(runtime->monsters[i].actor, actor)) continue;
    retire(runtime, runtime->monsters[i].monster);
    /* TS Map.delete keeps the remaining insertion order (capture order). */
    memmove(&runtime->monsters[i], &runtime->monsters[i + 1],
            (runtime->monster_count - i - 1) * sizeof *runtime->monsters);
    runtime->monster_count--;
    return;
  }
}
void qa_q1mpm_runtime_set_monster(qa_q1mpm_runtime *runtime, qa_owned_actor actor,
                                  qa_q1mpm_mission_monster *monster) {
  for (size_t i = 0; i < runtime->monster_count; i++) {
    if (!qa_same_owned_actor(runtime->monsters[i].actor, actor)) continue;
    if (runtime->monsters[i].monster != monster) retire(runtime, runtime->monsters[i].monster);
    runtime->monsters[i].monster = monster;
    return;
  }
  if (runtime->monster_count == runtime->monster_capacity) {
    size_t capacity = runtime->monster_capacity == 0 ? 16 : runtime->monster_capacity * 2;
    qa_q1mpm_monster_entry *next = realloc(runtime->monsters, capacity * sizeof *next);
    if (next == NULL) qa_range_error("out of memory");
    runtime->monsters = next;
    runtime->monster_capacity = capacity;
  }
  runtime->monsters[runtime->monster_count].actor = actor;
  runtime->monsters[runtime->monster_count].monster = monster;
  runtime->monster_count++;
}
static qa_q1mpm_registered_definition *find_definition(qa_q1mpm_runtime *runtime, const char *classname) {
  for (size_t i = 0; i < runtime->definition_count; i++)
    if (strcmp(runtime->definitions[i].classname, classname) == 0) return runtime->definitions[i].registered;
  return NULL;
}
/* TS definitions.set(classname, definition): a later set replaces in place. */
static void set_definition(qa_q1mpm_runtime *runtime, const char *classname,
                           qa_q1mpm_registered_definition *registered) {
  for (size_t i = 0; i < runtime->definition_count; i++)
    if (strcmp(runtime->definitions[i].classname, classname) == 0) {
      runtime->definitions[i].registered = registered;
      return;
    }
  if (runtime->definition_count == runtime->definition_capacity) {
    size_t capacity = runtime->definition_capacity == 0 ? 16 : runtime->definition_capacity * 2;
    qa_q1mpm_definition_entry *next = realloc(runtime->definitions, capacity * sizeof *next);
    if (next == NULL) qa_range_error("out of memory");
    runtime->definitions = next;
    runtime->definition_capacity = capacity;
  }
  runtime->definitions[runtime->definition_count].classname = dup_text(classname);
  runtime->definitions[runtime->definition_count].registered = registered;
  runtime->definition_count++;
}
static void on_release(void *ctx, qa_owned_actor actor) { delete_monster(ctx, actor); }
static void spawn_handler(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* TS `(_game, entity) => this.spawn(entity)` */
  qa_q1mpm_runtime_spawn(ctx, entity);
}
static qa_q1_base_monster *require_lookup(void *ctx, qa_q1_actor *entity) {
  return &qa_q1mpm_runtime_require(ctx, entity)->base;
}
static bool runtime_capture(void *ctx, uint8_t **out, size_t *out_length);
static void runtime_restore(void *ctx, const uint8_t *bytes, size_t length);
static void runtime_clone(void *ctx, qa_q1_actor *source, qa_q1_actor *target);

void qa_q1mpm_runtime_init(qa_q1mpm_runtime *runtime, qa_q1_services *game, qa_q1_base *base,
                           const char *pack, const qa_q1mpm_mission_monster_hooks *hooks) {
  memset(runtime, 0, sizeof *runtime);
  runtime->game = game;
  runtime->base = base;
  runtime->pack = pack;
  if (hooks != NULL) runtime->hooks = *hooks;
  runtime->monster_callbacks = qa_q1_register_monster_callbacks(game, pack, require_lookup, runtime);
  if (strcmp(pack, "hipnotic") == 0) {
    /* TS definitions.set(classname, speciesDefinition(spec, variants[classname])): one definition per classname. */
    /* The charmed grunt and dog register their own definitions below. */
    size_t count = 0;
    for (size_t i = 0; i < QA_Q1_BASE_SPECIES_COUNT; i++)
      if (strcmp(QA_Q1_BASE_SPECIES[i].species, "army") != 0 && strcmp(QA_Q1_BASE_SPECIES[i].species, "dog") != 0)
        count += QA_Q1_BASE_SPECIES[i].classname_count;
    runtime->synthetic = calloc(count != 0 ? count : 1, sizeof *runtime->synthetic);
    if (runtime->synthetic == NULL) qa_range_error("out of memory");
    runtime->synthetic_count = count;
    size_t at = 0;
    for (size_t i = 0; i < QA_Q1_BASE_SPECIES_COUNT; i++) {
      const qa_q1_base_monster_species *spec = &QA_Q1_BASE_SPECIES[i];
      if (strcmp(spec->species, "army") == 0 || strcmp(spec->species, "dog") == 0) continue;
      for (size_t j = 0; j < spec->classname_count; j++) {
        qa_q1mpm_pack_monster_definition *definition = &runtime->synthetic[at++];
        qa_q1mpm_species_definition(spec, definition);
        (void)qa_q1mpm_hipnotic_species_variant(spec->classnames[j], definition);
        set_definition(runtime, spec->classnames[j], registered_for(runtime, definition));
        if (strcmp(spec->movement, "boss") != 0)
          qa_q1_replace_spawn(game, spec->classnames[j], spawn_handler, runtime);
      }
    }
  }
  if (strcmp(pack, "rogue") == 0) {
    runtime->synthetic = calloc(QA_Q1MPM_ROGUE_SPECIES_VARIANT_COUNT, sizeof *runtime->synthetic);
    if (runtime->synthetic == NULL) qa_range_error("out of memory");
    runtime->synthetic_count =
        qa_q1mpm_rogue_species_variants(runtime, runtime->synthetic, QA_Q1MPM_ROGUE_SPECIES_VARIANT_COUNT);
    for (size_t i = 0; i < runtime->synthetic_count; i++) {
      const qa_q1mpm_pack_monster_definition *definition = &runtime->synthetic[i];
      for (size_t j = 0; j < definition->spec.classname_count; j++) {
        set_definition(runtime, definition->spec.classnames[j], registered_for(runtime, definition));
        qa_q1_replace_spawn(game, definition->spec.classnames[j], spawn_handler, runtime);
      }
    }
  }
  runtime->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, runtime);
  snprintf(runtime->extension_id, sizeof runtime->extension_id, "q1:%s:monsters", pack);
  runtime->extension = (qa_q1_state_extension){.ctx = runtime, .id = runtime->extension_id,
                                               .capture = runtime_capture, .restore = runtime_restore,
                                               .clone = runtime_clone};
  qa_q1_register_state_extension(game, &runtime->extension);
}
void qa_q1mpm_runtime_destroy(qa_q1mpm_runtime *runtime) {
  if (runtime->game != NULL) qa_session_actor_registry_off_release(runtime->game->host.actors, runtime->release_subscription);
  for (size_t i = 0; i < runtime->monster_count; i++) qa_q1_base_monster_destroy(&runtime->monsters[i].monster->base);
  for (size_t i = 0; i < runtime->retired_count; i++) qa_q1_base_monster_destroy(&runtime->retired[i]->base);
  free(runtime->monsters);
  free(runtime->retired);
  for (size_t i = 0; i < runtime->definition_count; i++) free(runtime->definitions[i].classname);
  free(runtime->definitions);
  for (size_t i = 0; i < runtime->registered_count; i++) {
    free(runtime->registered[i]->source_actions);
    free(runtime->registered[i]);
  }
  free(runtime->registered);
  free(runtime->synthetic);
  free(runtime->owned_defs);
  free(runtime->monster_callbacks);
  for (size_t i = 0; i < runtime->allocation_count; i++) free(runtime->allocations[i]);
  free(runtime->allocations);
  memset(runtime, 0, sizeof *runtime);
}
void *qa_q1mpm_runtime_calloc(qa_q1mpm_runtime *runtime, size_t count, size_t size) {
  if (runtime->allocation_count == runtime->allocation_capacity) {
    size_t capacity = runtime->allocation_capacity == 0 ? 8 : runtime->allocation_capacity * 2;
    void **next = realloc(runtime->allocations, capacity * sizeof *next);
    if (next == NULL) qa_range_error("out of memory");
    runtime->allocations = next;
    runtime->allocation_capacity = capacity;
  }
  void *memory = calloc(count, size);
  if (memory == NULL) qa_range_error("out of memory");
  runtime->allocations[runtime->allocation_count++] = memory;
  return memory;
}
qa_q1mpm_mission_monster *qa_q1mpm_runtime_require(qa_q1mpm_runtime *runtime, qa_q1_actor *entity) {
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_get(runtime, entity->actor);
  if (monster == NULL) qa_range_error("Missing %s monster controller for %s", runtime->pack, entity->classname);
  return monster;
}
qa_q1mpm_mission_monster *qa_q1mpm_runtime_context(qa_q1mpm_runtime *runtime, qa_actor_id actor) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(runtime->game->host.actors, actor, &owner)) return NULL;
  return qa_q1mpm_runtime_get(runtime, owner);
}
void qa_q1mpm_runtime_register(qa_q1mpm_runtime *runtime, const qa_q1mpm_pack_monster_definition *definition) {
  qa_q1_services *game = runtime->game;
  for (size_t i = 0; i < definition->callback_count; i++) {
    char name[192];
    snprintf(name, sizeof name, "%s:%s", runtime->pack, definition->callbacks[i].name);
    qa_q1_callback_registry_register(game->named, name, definition->callbacks[i].handlers);
  }
  for (size_t i = 0; i < definition->spec.classname_count; i++) {
    const char *classname = definition->spec.classnames[i];
    if (find_definition(runtime, classname) != NULL)
      qa_range_error("Duplicate %s monster %s", runtime->pack, classname);
    set_definition(runtime, classname, registered_for(runtime, definition));
    if (qa_q1_has_spawn(game, classname)) qa_q1_replace_spawn(game, classname, spawn_handler, runtime);
    else qa_q1_register_spawn(game, classname, spawn_handler, runtime);
  }
}
void qa_q1mpm_runtime_spawn(qa_q1mpm_runtime *runtime, qa_q1_actor *entity) {
  qa_q1_services *game = runtime->game;
  if (game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
  qa_q1mpm_registered_definition *registered = find_definition(runtime, entity->classname);
  if (registered == NULL) qa_range_error("Unknown %s monster %s", runtime->pack, entity->classname);
  qa_q1mpm_mission_monster *monster = qa_q1mpm_mission_monster_create(game, entity, registered->definition, runtime);
  qa_q1mpm_runtime_set_monster(runtime, entity->actor, monster);
  qa_q1_base_monster_spawn(&monster->base);
}
/* TS charm's name(callback): `base:` callbacks move to the pack prefix. */
static const char *charm_name(qa_q1mpm_runtime *runtime, const char *source, char *buf, size_t size) {
  if (source == NULL) return NULL;
  if (strncmp(source, "base:", 5) == 0) {
    snprintf(buf, size, "%s:%s", runtime->pack, source + 5);
    return buf;
  }
  return source;
}
void qa_q1mpm_runtime_charm(qa_q1mpm_runtime *runtime, qa_q1_actor *entity, qa_actor_id owner) {
  qa_q1_services *game = runtime->game;
  (void)qa_q1_actor_reference_set(entity, "charmer", &owner);
  (void)qa_q1_actor_field_set(entity, "charmed", "1");
  if (qa_q1mpm_runtime_get(runtime, entity->actor) != NULL) return;
  qa_q1_creatures *creatures = qa_q1_base_creatures(runtime->base);
  qa_q1_base_monster *previous = qa_q1_creatures_monsters_get(creatures, entity->actor);
  qa_q1mpm_registered_definition *registered = find_definition(runtime, entity->classname);
  if (previous == NULL || registered == NULL)
    qa_range_error("Missing source charm controller for %s", entity->classname);
  qa_q1mpm_mission_monster *monster = qa_q1mpm_mission_monster_create(game, entity, registered->definition, runtime);
  qa_save_value *captured = qa_q1_base_monster_capture(previous);
  if (captured == NULL) qa_range_error("out of memory");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, captured, "hipnotic:charm")) qa_range_error("out of memory");
  qa_q1_base_monster_restore(&monster->base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
  qa_q1_creatures_monsters_delete(creatures, entity->actor);
  qa_q1mpm_runtime_set_monster(runtime, entity->actor, monster);
  char b1[192], b2[192], b3[192], b4[192], b5[192], b6[192];
  const char *think = charm_name(runtime, qa_q1_action_name(entity->think), b1, sizeof b1);
  const char *use = charm_name(runtime, qa_q1_use_name(entity->use), b2, sizeof b2);
  const char *pain = charm_name(runtime, qa_q1_pain_name(entity->pain), b3, sizeof b3);
  const char *die = charm_name(runtime, qa_q1_die_name(entity->die), b4, sizeof b4);
  const char *touch = charm_name(runtime, qa_q1_touch_name(entity->touch), b5, sizeof b5);
  const char *path_end = charm_name(runtime, qa_q1_action_name(entity->path_end), b6, sizeof b6);
  entity->think = think == NULL ? (qa_q1_bound_action){0} : qa_q1_callback_registry_action(game->named, entity, think);
  entity->use = use == NULL ? (qa_q1_bound_use){0} : qa_q1_callback_registry_use(game->named, entity, use);
  entity->pain = pain == NULL ? (qa_q1_bound_pain){0} : qa_q1_callback_registry_pain(game->named, entity, pain);
  entity->die = die == NULL ? (qa_q1_bound_die){0} : qa_q1_callback_registry_die(game->named, entity, die);
  entity->touch = touch == NULL ? (qa_q1_bound_touch){0} : qa_q1_callback_registry_touch(game->named, entity, touch);
  entity->path_end = path_end == NULL ? (qa_q1_bound_action){0}
                                      : qa_q1_callback_registry_action(game->named, entity, path_end);
}
static void runtime_clone(void *ctx, qa_q1_actor *source, qa_q1_actor *target) {
  qa_q1mpm_runtime *runtime = ctx;
  qa_q1mpm_mission_monster *original = qa_q1mpm_runtime_get(runtime, source->actor);
  if (original == NULL) return;
  qa_q1mpm_mission_monster *monster = qa_q1mpm_mission_monster_create(runtime->game, target, original->definition, runtime);
  qa_save_value *state = qa_q1_base_monster_capture(&original->base);
  if (state == NULL) qa_range_error("out of memory");
  char path[96];
  snprintf(path, sizeof path, "%s:monster-clone", runtime->pack);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, state, path)) qa_range_error("out of memory");
  qa_q1_base_monster_restore(&monster->base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(state);
  qa_q1mpm_runtime_set_monster(runtime, target->actor, monster);
}
static void set_owned(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) qa_range_error("out of memory");
}
static bool runtime_capture(void *ctx, uint8_t **out, size_t *out_length) {
  qa_q1mpm_runtime *runtime = ctx;
  qa_save_value *root = qa_save_value_object();
  if (root == NULL) return false;
  set_owned(root, "version", qa_save_value_number(1));
  set_owned(root, "authoredGremlins", qa_save_value_number(runtime->authored_gremlins));
  set_owned(root, "spawnedGremlins", qa_save_value_number(runtime->spawned_gremlins));
  qa_save_value *list = qa_save_value_array();
  if (list == NULL) { qa_save_value_free(root); return false; }
  for (size_t i = 0; i < runtime->monster_count; i++) {
    qa_q1mpm_mission_monster *monster = runtime->monsters[i].monster;
    qa_save_value *entry = qa_save_value_object();
    qa_save_value *actor = qa_save_value_object();
    if (entry == NULL || actor == NULL) qa_range_error("out of memory");
    set_owned(actor, "slot", qa_save_value_number((double)monster->base.entity->actor.id.slot));
    set_owned(actor, "generation", qa_save_value_number((double)monster->base.entity->actor.id.generation));
    set_owned(entry, "actor", actor);
    const qa_q1_base_monster_species *spec = monster->base.spec;
    set_owned(entry, "definition",
              qa_save_value_string(spec->classname_count > 0 ? spec->classnames[0] : monster->base.entity->classname));
    set_owned(entry, "state", qa_q1_base_monster_capture(&monster->base));
    if (!qa_save_value_push(list, entry)) qa_range_error("out of memory");
  }
  set_owned(root, "monsters", list);
  *out = qa_encode_checkpoint_value(root, out_length);
  qa_save_value_free(root);
  return *out != NULL;
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
static double field_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  must_field(reader, name, &field);
  double value = qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  return value;
}
static void q1_free_decoded_save(void *value) { qa_save_value_free(value); }
static void runtime_restore(void *ctx, const uint8_t *bytes, size_t length) {
  qa_q1mpm_runtime *runtime = ctx;
  qa_q1_services *game = runtime->game;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, runtime->extension_id)) qa_range_error("out of memory");
  qa_save_reader version;
  must_field(&reader, "version", &version);
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  runtime->authored_gremlins = field_number(&reader, "authoredGremlins");
  runtime->spawned_gremlins = field_number(&reader, "spawnedGremlins");
  /* TS monsters.clear(). */
  for (size_t i = 0; i < runtime->monster_count; i++) retire(runtime, runtime->monsters[i].monster);
  runtime->monster_count = 0;
  qa_save_reader monsters;
  must_field(&reader, "monsters", &monsters);
  size_t count = qa_save_reader_list_count(&monsters);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader saved, id;
    if (!qa_save_reader_item(&monsters, i, &saved)) qa_range_error("out of memory");
    must_field(&saved, "actor", &id);
    const double slot = field_integer(&id, "slot");
    const double generation = field_integer(&id, "generation");
    const qa_saved_actor_id saved_id = {slot, generation};
    qa_save_reader_destroy(&id);
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(game->host.actors, saved_id, &actor))
      qa_save_reader_fail(&saved, "missing saved monster actor");
    qa_q1_actor *entity = qa_q1_get_entity(game, &actor.id);
    if (entity == NULL) qa_save_reader_fail(&saved, "missing saved monster source entity");
    qa_save_reader definition_reader;
    must_field(&saved, "definition", &definition_reader);
    qa_q1mpm_registered_definition *registered = find_definition(runtime, qa_save_reader_string(&definition_reader));
    qa_save_reader_destroy(&definition_reader);
    if (registered == NULL) qa_save_reader_fail(&saved, "unknown saved mission pack monster");
    qa_q1mpm_mission_monster *monster = qa_q1mpm_mission_monster_create(game, entity, registered->definition, runtime);
    qa_save_reader state;
    must_field(&saved, "state", &state);
    qa_q1_base_monster_restore(&monster->base, &state);
    qa_save_reader_destroy(&state);
    qa_q1mpm_runtime_set_monster(runtime, actor, monster);
    qa_save_reader_destroy(&saved);
  }
  qa_save_reader_destroy(&monsters);
  qa_save_reader_destroy(&reader);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}
