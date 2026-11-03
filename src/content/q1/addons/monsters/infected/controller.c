/* Port of src/content/q1/addons/monsters/infected/controller.ts.
 * quakec_mg3/monsters/mg3_*_infected.qc and monsters.qc. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/infected/controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/infected/frames.h"
#include "qa/content/q1/addons/monsters/infected/species.h"
#include "qa/content/q1/addons/monsters/ordinary/army.h"
#include "qa/content/q1/addons/monsters/ordinary/attack.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/content/q1/base/projectiles.h"
#include "../support.h"

#define PREFIX QA_Q1AM_INFECTED_PREFIX

static void corpse_hold(void *ctx, qa_q1_base_monster *monster) { (void)ctx; qa_q1_base_monster_delay(monster, 9999); }
static void test_rise(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  entity->solid = "slidebox";
  if (!qa_q1am_walk_move(game, entity->actor, 0, 0)) {
    entity->solid = "none";
    qa_q1_link(game, entity);
    qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, monster->current_frame);
    qa_q1_base_monster_delay(monster, 5);
    return;
  }
  qa_q1_link(game, entity);
  qa_q1_sound(game, entity->actor.id, "infected/death1_rev.wav", "voice", 1, 1);
}
static void rise_pain(void *ctx, qa_q1_base_monster *monster) { (void)ctx; monster->state->pain_finished = monster->game->time + 1.5; }
static void resurrect_action(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  qa_q1_call_action(monster->game, monster->entity,
                    qa_q1_callback_registry_action(monster->game->named, monster->entity, PREFIX ":resurrect"));
}
static void no_backpack(void *ctx, qa_q1_base_monster *monster) { (void)ctx; (void)monster; /* mg3NoBackpackActions */ }
/* TS actions = [...armyActions, ...mg3NoBackpackActions, infected_corpse_hold, infected_test_rise,
 * infected_rise_pain, infected_resurrect]. */
static const qa_q1_monster_action_entry infected_actions[] = {
  {"army_fire", qa_q1am_army_fire, NULL}, {"army_refire", qa_q1am_army_refire, NULL},
  {"enf_die3", no_backpack, NULL}, {"enf_fdie3", no_backpack, NULL}, {"ogre_die3", no_backpack, NULL},
  {"ogre_bdie3", no_backpack, NULL},
  {"infected_corpse_hold", corpse_hold, NULL}, {"infected_test_rise", test_rise, NULL},
  {"infected_rise_pain", rise_pain, NULL}, {"infected_resurrect", resurrect_action, NULL},
};

static void infected_spawn(qa_q1_base_monster *monster);
static void infected_start(qa_q1_base_monster *monster);
static void infected_use(qa_q1_base_monster *monster, const qa_actor_id *activator);
static bool infected_try_attack(qa_q1_base_monster *monster) { return qa_q1am_mg3_ordinary_attack(monster); }
static void infected_melee_attack(qa_q1_base_monster *monster);
static void infected_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage);
static void infected_die(qa_q1_base_monster *monster, const qa_actor_id *attacker);
static void infected_destroy(qa_q1_base_monster *monster) { free(monster); }
static const qa_q1_base_monster_ops infected_ops = {
  QA_Q1AM_MG3_MONSTER_OPS,
  .spawn = infected_spawn, .start = infected_start, .use = infected_use, .try_attack = infected_try_attack,
  .melee_attack = infected_melee_attack, .pain = infected_pain, .die = infected_die, .destroy = infected_destroy,
};
bool qa_q1am_is_infected(const qa_q1_base_monster *monster) { return monster->ops == &infected_ops; }
static qa_q1am_infected *infected_of(qa_q1_base_monster *monster) { return (qa_q1am_infected *)monster; }
static bool same(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static bool corpse_stand(const qa_q1_base_monster *monster) { return strncmp(monster->spec->stand, "hknight_corpse", 14) == 0; }

bool qa_q1am_infected_init(qa_q1am_infected *infected, qa_q1ad_context *context, qa_q1_actor *entity,
                           qa_q1am_infected_replace_fn replace, void *replace_ctx) {
  memset(infected, 0, sizeof *infected);
  infected->context = context;
  infected->replace = replace;
  infected->replace_ctx = replace_ctx;
  qa_q1am_infected_species(entity, &infected->spec);
  qa_q1_base_monster_source source;
  memset(&source, 0, sizeof source);
  source.callback_prefix = PREFIX;
  source.frames = &qa_q1am_infected_frames;
  source.actions = infected_actions;
  source.action_count = sizeof infected_actions / sizeof infected_actions[0];
  return qa_q1am_mg3_monster_init(&infected->base, context->game, entity, &infected->spec,
                                  qa_q1_base_as_monster_services(context->base), &source, &infected_ops, NULL);
}
static void infected_spawn(qa_q1_base_monster *monster) {
  qa_q1ad_context *context = infected_of(monster)->context;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_base_monster_species *spec = monster->spec;
  if (spec->classname_count == 0) qa_range_error("Infected source class has no native name");
  qa_q1ad_set_text(&entity->classname, spec->classnames[0]);
  qa_q1ad_set_number(context, entity, "infected", 1);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, spec->health);
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, PREFIX ":monster_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, PREFIX ":monster_die");
  entity->path_end = qa_q1_callback_registry_action(game->named, entity, PREFIX ":monster_stand");
  if (same(spec->species, "knight") || same(spec->species, "hellknight")) {
    qa_q1ad_set_number(context, entity, "allowPathFind", 1);
    qa_q1ad_set_number(context, entity, "combat_style", same(spec->species, "knight") ? 2 : 3);
  }
  char model[96];
  (void)snprintf(model, sizeof model, "progs/%s.mdl", spec->model);
  qa_q1am_init_mg3_monster(monster, context, model, 1, same(spec->species, "army") || same(spec->species, "knight") ? 1 : 2);
}
static void infected_start(qa_q1_base_monster *monster) { qa_q1am_start_mg3_monster(monster, infected_of(monster)->context); }
static void infected_use(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_q1_services *game = monster->game;
  qa_q1_monster_target_result observed = {0};
  if (activator != NULL && qa_q1_is_player(game, activator)) observed = qa_q1_monster_target(game, *activator);
  if (!monster->state->has_enemy && qa_q1_health(game, monster->entity->actor.id) > 0 && observed.has_value &&
      observed.value.invisible && corpse_stand(monster)) {
    qa_q1_base_monster_play(monster, monster->spec->run);
    return;
  }
  if (observed.has_value && (observed.value.invisible || observed.value.notarget)) return;
  qa_actor_id resolved;
  const bool has = qa_q1am_mg3_monster_activator(game, activator, &resolved);
  qa_q1_base_monster_use_super(monster, has ? &resolved : NULL);
}
static void infected_melee_attack(qa_q1_base_monster *monster) {
  if (!same(monster->spec->species, "zombie")) { qa_q1_base_monster_melee_attack_super(monster); return; }
  const double r = qa_q1am_random(monster->game);
  qa_q1_base_monster_play(monster, r < 0.3 ? "zombie_atta1" : r < 0.6 ? "zombie_attb1" : "zombie_attc1");
}
static void infected_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (corpse_stand(monster) && qa_q1_actor_number(entity, "infected.risen") == 0) {
    qa_q1_base_monster_retaliate(monster, attacker);
    return;
  }
  if (same(monster->spec->species, "enforcer")) {
    qa_q1_base_monster_retaliate(monster, attacker);
    qa_q1_monster *state = monster->state;
    const double r = qa_q1am_random(game);
    if (state->pain_finished > game->time || (qa_q1_option_skill(game) > 2 && qa_q1am_random(game) * 200 > damage)) return;
    qa_q1_sound(game, entity->actor.id, r < 0.5 ? "enforcer/pain1.wav" : "enforcer/pain2.wav", "voice", 1, 1);
    state->pain_finished = game->time + (r < 0.7 ? 1 : 2);
    qa_q1_base_monster_play(monster, r < 0.2 ? "enf_paina1" : r < 0.4 ? "enf_painb1" : r < 0.7 ? "enf_painc1" : "enf_paind1");
    return;
  }
  if (same(monster->spec->species, "army")) qa_q1am_army_pain(monster, attacker, damage, true);
  else qa_q1_base_monster_pain_super(monster, attacker, damage);
}
/* TS for (const model of ["gib1", "gib2", "gib3"]) throwGib(game, this.origin, model, game.health(entity.actor.id)). */
static void gibs(qa_q1_services *game, qa_q1_base_monster *monster) {
  qa_vec3 origin;
  const char * model;
  double damage;
  static const char *const models[] = {"gib1", "gib2", "gib3"};
  for (size_t i = 0; i < 3; i++)
    (void)(origin = qa_q1_base_monster_origin(monster), model = models[i], damage = qa_q1_health(game, monster->entity->actor.id), qa_q1_throw_gib(game, origin, model, damage));
}
static void infected_die(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  qa_vec3 origin;
  double damage;
  qa_q1am_infected *self = infected_of(monster);
  qa_q1ad_context *context = self->context;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (monster->counted_death) return;
  if (qa_q1_actor_number(entity, "infected.transformed") != 0 && !same(monster->spec->species, "zombie")) {
    qa_q1_base_monster_die_super(monster, attacker);
    return;
  }
  if (qa_q1_health(game, entity->actor.id) < -99) qa_gameplay_authority_set_health(game->host.combat, entity->actor, -99);
  qa_q1_base_monster_set_enemy(monster, attacker);
  qa_q1_actor_set_damageable(entity, false);
  memset(&entity->touch, 0, sizeof entity->touch);
  qa_q1_base_monster_count_kill(monster);
  if (qa_q1_actor_number(entity, "infected.transformed") != 0) {
    qa_q1_sound(game, entity->actor.id, "zombie/z_gib.wav", "voice", 1, 1);
    qa_q1_throw_head(game, entity, "h_zombie");
    gibs(game, monster);
    return;
  }
  qa_q1_sound(game, entity->actor.id, "player/udeath.wav", "voice", 1, 1);
  if (strcmp(qa_q1am_infected_kind(entity), "army") == 0)
    (void)(origin = qa_q1_base_monster_origin(monster), damage = qa_q1_health(game, entity->actor.id), qa_q1_throw_gib(game, origin, "h_guard", damage));
  gibs(game, monster);
  qa_q1ad_set_number(context, entity, "infected.transformed", 1);
  qa_q1_base_monster_species spec;
  qa_q1am_infected_species(entity, &spec);
  /* TS entity.monster = { ...this.state, species }: the replaced controller never reads
   * its state again, so the shared state object is updated in place. */
  entity->monster->species = spec.species;
  qa_q1am_infected *transformed = self->replace(self->replace_ctx, entity);
  qa_save_value *captured = qa_q1_base_monster_capture(monster);
  if (captured == NULL) qa_range_error("out of memory");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, captured, PREFIX)) qa_range_error("out of memory");
  qa_q1_base_monster_restore(&transformed->base.base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
  transformed->base.base.counted_death = false;
  char model[96];
  (void)snprintf(model, sizeof model, "progs/%s.mdl", spec.model);
  qa_q1ad_set_text(&entity->model, model);
  const bool zombie = same(spec.species, "zombie");
  qa_q1am_field_text(entity, "noise", zombie ? "zombie/z_idle.wav" : "demon/sight2.wav");
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, spec.health);
  entity->max_health = spec.health;
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, PREFIX ":monster_pain");
  entity->aimed_damage = true;
  qa_q1_actor_set_damageable(entity, true);
  if (zombie) {
    entity->spawnflags = 128;
  } else {
    qa_q1ad_set_number(context, entity, "combat_style", 2);
    transformed->base.base.state->pain_finished = game->time + 1;
    transformed->base.base.state->attack_finished = 0;
  }
  qa_q1ad_set_text(&entity->classname, zombie ? "monster_zombie" : "monster_demon1");
  qa_q1am_infected_retarget(transformed);
  if (qa_q1am_walk_move(game, entity->actor, 0, 0)) {
    qa_q1_base_monster_play(&transformed->base.base, zombie ? "zombie_paina1" : "demon1_pain1");
    return;
  }
  game->killed_monsters++;
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "monster-killed";
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.total = game->total_monsters;
  event.found = game->killed_monsters;
  qa_q1_emit(game, event);
  transformed->base.base.counted_death = true;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, -100);
  qa_q1_sound(game, entity->actor.id, zombie ? "zombie/z_gib.wav" : "player/udeath.wav", "voice", 1, 1);
  if (zombie) {
    qa_q1_throw_head_damage(game, entity, "h_zombie", -100);
    for (size_t i = 0; i < spec.gib_count; i++) (void)qa_q1_throw_gib(game, qa_q1_base_monster_origin(monster), spec.gibs[i], -100);
    return;
  }
  for (size_t i = 0; i < spec.gib_count; i++) (void)qa_q1_throw_gib(game, qa_q1_base_monster_origin(monster), spec.gibs[i], -100);
  qa_q1_throw_head_damage(game, entity, "h_demon", -100);
}
void qa_q1am_infected_retarget(qa_q1am_infected *infected) {
  qa_q1_base_monster *monster = &infected->base.base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (!monster->state->has_enemy) return;
  const qa_actor_id enemy = monster->state->enemy;
  if (strcmp(game->host.classname(game->host.ctx, enemy), entity->classname) != 0) return;
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  const qa_actor_id *player = NULL;
  for (size_t i = 0; i < count && player == NULL; i++)
    if (qa_q1_health(game, players[i]) > 0) player = &players[i];
  if (player == NULL) return;
  const qa_actor_id chosen = *player;
  qa_q1_actor *rival_entity = qa_q1_get_entity(game, &enemy);
  qa_q1_monster *rival = rival_entity == NULL ? NULL : rival_entity->monster;
  if (rival != NULL && rival->has_enemy && qa_same_actor(rival->enemy, entity->actor.id)) rival->enemy = chosen;
  qa_q1_base_monster_set_enemy(monster, &chosen);
}
void qa_q1am_infected_resurrect(qa_q1am_infected *infected) {
  qa_q1_base_monster *monster = &infected->base.base;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_set_number(infected->context, entity, "aflag", 0);
  qa_q1ad_set_number(infected->context, entity, "infected.risen", 1);
  qa_q1am_infected *risen = infected->replace(infected->replace_ctx, entity);
  qa_save_value *captured = qa_q1_base_monster_capture(monster);
  if (captured == NULL) qa_range_error("out of memory");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, captured, PREFIX)) qa_range_error("out of memory");
  qa_q1_base_monster_restore(&risen->base.base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
  entity->pain = qa_q1_callback_registry_pain(monster->game->named, entity, PREFIX ":monster_pain");
  qa_q1_base_monster_ai(&risen->base.base, QA_Q1_AI_STAND, 0);
}
