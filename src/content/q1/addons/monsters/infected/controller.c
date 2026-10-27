/* Port of src/content/q1/addons/monsters/infected/controller.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/infected/controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/infected/frames.h"
#include "qa/content/q1/addons/monsters/infected/species.h"
#include "qa/content/q1/addons/monsters/ordinary/army.h"
#include "qa/content/q1/addons/monsters/ordinary/attack.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/foundation/support.h"

static qa_q1am_infected *as_infected(qa_q1am_monster *m) { return m->owner; }
static void army_fire_entry(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  qa_q1am_army_fire_action(monster);
}
static void army_refire_entry(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  qa_q1am_army_refire_action(monster);
}
static void corpse_hold(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  qa_q1am_monster_delay(monster, 9999);
}
static void test_rise(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  entity->solid = "slidebox";
  if (!game->walk_move(game->ctx, entity->actor, 0, 0)) {
    entity->solid = "none";
    game->link(game->ctx, entity);
    qa_q1am_monster_set_frame(monster->next_frame, sizeof monster->next_frame, monster->current_frame);
    qa_q1am_monster_delay(monster, 5);
    return;
  }
  game->link(game->ctx, entity);
  game->sound(game->ctx, entity, "infected/death1_rev.wav", "voice", 1);
}
static void rise_pain(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  monster->state->pain_finished = monster->game->time + 1.5;
}
static void resurrect_action(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  qa_q1am_game *game = monster->game;
  game->call_action(game->ctx, monster->entity,
                    game->named_action(game->ctx, monster->entity, QA_Q1AM_INFECTED_PREFIX ":resurrect"));
}
static const qa_q1am_action_entry infected_actions[] = {
  {"army_fire", NULL, army_fire_entry},
  {"army_refire", NULL, army_refire_entry},
  {"infected_corpse_hold", NULL, corpse_hold},
  {"infected_test_rise", NULL, test_rise},
  {"infected_rise_pain", NULL, rise_pain},
  {"infected_resurrect", NULL, resurrect_action},
};
static const qa_q1am_source infected_source = {
  QA_Q1AM_INFECTED_PREFIX, &qa_q1am_infected_frames, infected_actions,
  sizeof infected_actions / sizeof infected_actions[0],
};

static void infected_spawn(qa_q1am_monster *m) {
  qa_q1am_infected *self = as_infected(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = self->context;
  if (m->spec.classname_count == 0) qa_range_error("Infected source class has no native name");
  (void)snprintf(entity->classname, sizeof entity->classname, "%s", m->spec.classnames[0]);
  context->set_number(context->ctx, entity, "infected", 1);
  game->combat_set_health(game->ctx, entity->actor, m->spec.health);
  if (strncmp(m->spec.stand, "hknight_corpse", strlen("hknight_corpse")) == 0) entity->pain.present = false;
  else entity->pain = game->named_pain(game->ctx, entity, QA_Q1AM_INFECTED_PREFIX ":monster_pain");
  entity->die = game->named_die(game->ctx, entity, QA_Q1AM_INFECTED_PREFIX ":monster_die");
  entity->path_end = game->named_action(game->ctx, entity, QA_Q1AM_INFECTED_PREFIX ":monster_stand");
  if (strcmp(m->spec.species, "knight") == 0 || strcmp(m->spec.species, "hellknight") == 0) {
    context->set_number(context->ctx, entity, "allowPathFind", 1);
    context->set_number(context->ctx, entity, "combat_style",
                        strcmp(m->spec.species, "knight") == 0 ? 2 : 3);
  }
  char model[192];
  (void)snprintf(model, sizeof model, "progs/%s.mdl", m->spec.model);
  qa_q1am_init_mg3_monster(m, context, model, 1,
                           strcmp(m->spec.species, "army") == 0 || strcmp(m->spec.species, "knight") == 0 ? 1 : 2);
}
static void infected_start(qa_q1am_monster *m) { qa_q1am_start_mg3_monster(m, as_infected(m)->context); }
static void infected_use(qa_q1am_monster *m, bool has, qa_actor_id activator) {
  qa_actor_id resolved;
  bool resolved_has = qa_q1am_mg3_monster_activator(m->game, has, activator, &resolved);
  as_infected(m)->super_ops.use(m, resolved_has, resolved);
}
static bool infected_try_attack(qa_q1am_monster *m) { return qa_q1am_mg3_ordinary_attack(m); }
static void infected_melee_attack(qa_q1am_monster *m) {
  if (strcmp(m->spec.species, "zombie") != 0) {
    as_infected(m)->super_ops.melee_attack(m);
    return;
  }
  double r = m->game->random(m->game->ctx);
  m->ops.play(m, r < 0.3 ? "zombie_atta1" : r < 0.6 ? "zombie_attb1" : "zombie_attc1");
}
static void infected_pain(qa_q1am_monster *m, bool has, qa_actor_id attacker, double damage) {
  qa_q1am_infected *self = as_infected(m);
  if (strcmp(m->spec.species, "enforcer") == 0) {
    qa_q1am_monster_retaliate(m, has, attacker);
    qa_q1am_game *game = m->game;
    qa_q1am_actor *entity = m->entity;
    qa_q1am_state *state = m->state;
    double r = game->random(game->ctx);
    if (state->pain_finished > game->time ||
        (game->options.skill > 2 && game->random(game->ctx) * 200 > damage))
      return;
    game->sound(game->ctx, entity, r < 0.5 ? "enforcer/pain1.wav" : "enforcer/pain2.wav", "voice", 1);
    state->pain_finished = game->time + (r < 0.7 ? 1 : 2);
    m->ops.play(m, r < 0.2 ? "enf_paina1" : r < 0.4 ? "enf_painb1" : r < 0.7 ? "enf_painc1" : "enf_paind1");
    return;
  }
  if (strcmp(m->spec.species, "army") == 0) qa_q1am_army_pain(m, has, attacker, damage, true);
  else self->super_ops.pain(m, has, attacker, damage);
}
static void restore_from_capture(qa_q1am_monster *target, qa_q1am_monster *source) {
  qa_save_value *captured = qa_q1am_monster_capture(source);
  qa_save_reader reader;
  qa_save_reader_init(&reader, captured, QA_Q1AM_INFECTED_PREFIX);
  qa_q1am_monster_restore(target, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
}
static void infected_die(qa_q1am_monster *m, bool has, qa_actor_id attacker) {
  qa_q1am_infected *self = as_infected(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = self->context;
  if (m->counted_death) return;
  if (qa_q1am_actor_number(entity, "infected.transformed") != 0 &&
      strcmp(m->spec.species, "zombie") != 0) {
    self->super_ops.die(m, has, attacker);
    return;
  }
  if (game->health(game->ctx, entity->actor.id) < -99)
    game->combat_set_health(game->ctx, entity->actor, -99);
  m->state->has_enemy = has;
  m->state->enemy = attacker;
  qa_q1am_actor_set_damageable(game, entity, false);
  entity->touch.present = false;
  qa_q1am_monster_count_kill(m);
  static const char *const gibs[] = {"gib1", "gib2", "gib3"};
  if (qa_q1am_actor_number(entity, "infected.transformed") != 0) {
    game->sound(game->ctx, entity, "zombie/z_gib.wav", "voice", 1);
    game->throw_head(game->ctx, game, entity, "h_zombie", false, 0);
    for (int i = 0; i < 3; i++)
      game->throw_gib(game->ctx, game, qa_q1am_monster_origin(m), gibs[i],
                      game->health(game->ctx, entity->actor.id));
    return;
  }
  game->sound(game->ctx, entity, "player/udeath.wav", "voice", 1);
  if (strcmp(qa_q1am_infected_kind(entity), "army") == 0)
    game->throw_gib(game->ctx, game, qa_q1am_monster_origin(m), "h_guard",
                    game->health(game->ctx, entity->actor.id));
  for (int i = 0; i < 3; i++)
    game->throw_gib(game->ctx, game, qa_q1am_monster_origin(m), gibs[i],
                    game->health(game->ctx, entity->actor.id));
  context->set_number(context->ctx, entity, "infected.transformed", 1);
  qa_q1am_species spec = qa_q1am_infected_species(entity);
  /* TS: entity.monster = {...this.state, species: spec.species}. The
   * state object is shared here, so only the species word changes. */
  entity->monster.species = spec.species;
  qa_q1am_infected *transformed = self->replace.fn(self->replace.ctx, entity);
  restore_from_capture(&transformed->base, m);
  transformed->base.counted_death = false;
  (void)snprintf(entity->model, sizeof entity->model, "progs/%s.mdl", spec.model);
  qa_q1am_actor_set_field(entity, "noise",
                          strcmp(spec.species, "zombie") == 0 ? "zombie/z_idle.wav" : "demon/sight2.wav");
  game->combat_set_health(game->ctx, entity->actor, spec.health);
  entity->max_health = spec.health;
  entity->pain = game->named_pain(game->ctx, entity, QA_Q1AM_INFECTED_PREFIX ":monster_pain");
  entity->aimed_damage = true;
  qa_q1am_actor_set_damageable(game, entity, true);
  if (strcmp(spec.species, "zombie") == 0) entity->spawnflags = 128;
  else {
    context->set_number(context->ctx, entity, "combat_style", 2);
    transformed->base.state->pain_finished = game->time + 1;
    transformed->base.state->attack_finished = 0;
  }
  (void)snprintf(entity->classname, sizeof entity->classname, "%s",
                 strcmp(spec.species, "zombie") == 0 ? "monster_zombie" : "monster_demon1");
  qa_q1am_infected_retarget(transformed);
  if (game->walk_move(game->ctx, entity->actor, 0, 0)) {
    transformed->base.ops.play(&transformed->base,
                               strcmp(spec.species, "zombie") == 0 ? "zombie_paina1" : "demon1_pain1");
    return;
  }
  game->killed_monsters++;
  qa_q1am_event event = {0};
  event.kind = "monster-killed";
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.total = game->total_monsters;
  event.found = game->killed_monsters;
  game->emit(game->ctx, &event);
  transformed->base.counted_death = true;
  game->combat_set_health(game->ctx, entity->actor, -100);
  game->sound(game->ctx, entity,
              strcmp(spec.species, "zombie") == 0 ? "zombie/z_gib.wav" : "player/udeath.wav", "voice", 1);
  if (strcmp(spec.species, "zombie") == 0) {
    game->throw_head(game->ctx, game, entity, "h_zombie", true, -100);
    for (size_t i = 0; i < spec.gib_count; i++)
      game->throw_gib(game->ctx, game, qa_q1am_monster_origin(m), spec.gibs[i], -100);
    return;
  }
  for (size_t i = 0; i < spec.gib_count; i++)
    game->throw_gib(game->ctx, game, qa_q1am_monster_origin(m), spec.gibs[i], -100);
  game->throw_head(game->ctx, game, entity, "h_demon", true, -100);
}
void qa_q1am_infected_retarget(qa_q1am_infected *infected) {
  qa_q1am_monster *m = &infected->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  if (!m->state->has_enemy) return;
  qa_actor_id enemy = m->state->enemy;
  if (strcmp(game->classname_of(game->ctx, enemy), entity->classname) != 0) return;
  size_t count = 0;
  qa_actor_id *players = game->players(game->ctx, &count);
  bool found = false;
  qa_actor_id player = {0};
  for (size_t i = 0; i < count && !found; i++)
    if (game->health(game->ctx, players[i]) > 0) {
      player = players[i];
      found = true;
    }
  free(players);
  if (!found) return;
  qa_q1am_actor *rival_entity = game->entity(game->ctx, true, enemy);
  if (rival_entity && rival_entity->has_monster && rival_entity->monster.has_enemy &&
      qa_same_actor(rival_entity->monster.enemy, entity->actor.id))
    rival_entity->monster.enemy = player;
  m->state->enemy = player;
}
void qa_q1am_infected_resurrect(qa_q1am_infected *infected) {
  qa_q1am_monster *m = &infected->base;
  infected->context->set_number(infected->context->ctx, m->entity, "aflag", 0);
  infected->context->set_number(infected->context->ctx, m->entity, "infected.risen", 1);
  qa_q1am_infected *risen = infected->replace.fn(infected->replace.ctx, m->entity);
  restore_from_capture(&risen->base, m);
  m->entity->pain = m->game->named_pain(m->game->ctx, m->entity, QA_Q1AM_INFECTED_PREFIX ":monster_pain");
  risen->base.ops.ai(&risen->base, QA_Q1AM_AI_STAND, 0);
}
void qa_q1am_infected_init(qa_q1am_infected *infected, qa_q1am_context *context,
                           qa_q1am_actor *entity, qa_q1am_infected_replace replace) {
  memset(infected, 0, sizeof *infected);
  infected->context = context;
  infected->replace = replace;
  qa_q1am_species spec = qa_q1am_infected_species(entity);
  qa_q1am_monster_init(&infected->base, context->game, entity, &spec, context->base, &infected_source);
  infected->base.owner = infected;
  qa_q1am_mg3_install(&infected->base);
  infected->base.kind = QA_Q1AM_KIND_INFECTED;
  infected->super_ops = infected->base.ops;
  infected->base.ops.spawn = infected_spawn;
  infected->base.ops.start = infected_start;
  infected->base.ops.use = infected_use;
  infected->base.ops.try_attack = infected_try_attack;
  infected->base.ops.melee_attack = infected_melee_attack;
  infected->base.ops.pain = infected_pain;
  infected->base.ops.die = infected_die;
}
