/*
 * C checks for src/content/q1/base over the one Q1 game (a real
 * qa_q1_services on the shared world tables, tests/content/q1_world_fixture.h).
 * Ported from ../quake-anthology-ts/tests/gameplay/q1-base/base.test.ts where
 * the case does not need a corpus map (each TS case loads e1m7 only for its
 * scene; here the fixture's open world stands in): base monster mission,
 * zombie recovery and gib death, nightmare pain cooldown, obituaries.
 * Plus the BaseMonster vtable: a derived controller overriding found/run
 * with a sentinel ctx proves TS virtual dispatch and super calls.
 * GPL-2.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q1_world_fixture.h"
#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/base/monsters.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/base/monsters.h"
#include "qa/foundation/js.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int sentinel;

typedef struct base_world {
  q1_world_fixture fx;
  qa_q1_campaign_state campaign;
  qa_q1_base *base;
  qa_owned_actor player;
} base_world;

static void world_create(base_world *w, bool native_precaches) {
  qa_q1_options options = q1_world_fixture_options();
  options.precache_id1 = native_precaches;
  CHECK(q1_world_fixture_create(&w->fx, options));
  qa_q1_campaign_state_init(&w->campaign, 0, 1);
  qa_q1_base_options base_options;
  memset(&base_options, 0, sizeof base_options);
  base_options.campaign = &w->campaign.binding;
  w->base = qa_q1_register_base(w->fx.game, &base_options);
  CHECK(w->base != NULL && qa_q1_base_for(w->fx.game) == w->base);
  w->player = q1_world_fixture_admit_player(&w->fx, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(w->fx.game, w->player, "shotgun", false, NULL);
}
static void world_destroy(base_world *w) {
  CHECK(w->fx.ctx_errors == 0);
  qa_q1_base_destroy(w->base);
  q1_world_fixture_destroy(&w->fx);
}
static qa_q1_base_monster *controller(base_world *w, qa_q1_actor *entity) {
  return qa_q1_creatures_monsters_get(qa_q1_base_creatures(w->base), entity->actor);
}

/* --- base monster mission owns activation, route and one death notification --- */
typedef struct mission_probe {
  int *ctx_check;
  base_world *w;
  qa_q1_actor *entity;
  qa_actor_id player;
  int spawned, started, used, killed;
} mission_probe;
static mission_probe *probe_of(void *ctx) {
  mission_probe *probe = ctx;
  CHECK(probe->ctx_check == &sentinel);
  return probe;
}
static void m_spawned(void *ctx) { probe_of(ctx)->spawned++; }
static void m_started(void *ctx) {
  mission_probe *probe = probe_of(ctx);
  CHECK(qa_q1_actor_damageable(probe->entity));
  CHECK((qa_js_to_int32(probe->entity->movement_flags) & 32) == 32);
  probe->started++;
}
static void m_killed(void *ctx, const qa_actor_id *attacker) { (void)attacker; probe_of(ctx)->killed++; }
static const qa_actor_id *m_route(void *ctx) {
  mission_probe *probe = probe_of(ctx);
  CHECK(probe->started == 1);
  return &probe->player;
}
static bool m_use(void *ctx, const qa_actor_id *activator) { (void)activator; probe_of(ctx)->used++; return true; }
static qa_monster_combat_route m_combat_route(void *ctx) {
  mission_probe *probe = probe_of(ctx);
  qa_monster_combat_route route = {&probe->player, false};
  return route;
}
static void m_found(void *ctx) { (void)probe_of(ctx); }

static void test_mission(void) {
  base_world w;
  world_create(&w, false);
  qa_q1_services *game = w.fx.game;
  qa_q1_actor *entity = qa_q1_create(game, "monster_knight", NULL, NULL);
  mission_probe probe = {&sentinel, &w, entity, w.player.id, 0, 0, 0, 0};
  qa_monster_mission mission = {&probe, false, m_spawned, m_started, m_killed, m_route, m_use, m_combat_route, m_found};
  qa_q1_set_mission(game, entity->actor.id, &mission);
  const double total = game->total_monsters;
  qa_q1_spawn_entity(game, entity);
  CHECK(probe.spawned == 1);
  CHECK(game->total_monsters == total);
  qa_q1_base_monster *monster = controller(&w, entity);
  CHECK(monster != NULL);
  if (monster != NULL) {
    qa_q1_base_monster_start(monster);
    CHECK(probe.started == 1);
    const qa_actor_id *route = qa_q1_base_monster_route(monster);
    CHECK(route != NULL && qa_same_actor(*route, w.player.id));
    qa_q1_set_monster_route(game, entity, &w.player.id, game->time + 30);
    CHECK(strcmp(monster->state->mode, "stand") == 0);
    CHECK(monster->state->pause_until == game->time + 30);
    qa_q1_base_monster_set_enemy(monster, &w.player.id);
    qa_q1_base_monster_use(monster, NULL);
    CHECK(probe.used == 1);
    const double before = game->killed_monsters;
    qa_q1_base_monster_count_kill(monster);
    qa_q1_base_monster_count_kill(monster);
    CHECK(probe.killed == 1);
    CHECK(game->killed_monsters == before);
  }
  world_destroy(&w);
}

/* --- zombie recovery and gib death use shared combat state --- */
static void test_zombie(void) {
  base_world w;
  world_create(&w, true);
  qa_q1_services *game = w.fx.game;
  qa_q1_actor *entity = qa_q1_create(game, "monster_zombie", NULL, NULL);
  qa_q1_spawn_entity(game, entity);
  qa_q1_base_monster *monster = controller(&w, entity);
  CHECK(monster != NULL);
  if (monster != NULL) {
    qa_q1_actor_set_damageable(entity, true);
    const qa_actor_id player = w.player.id;
    (void)qa_q1_damage(game, entity->actor.id, player, &player, 10, NULL, "direct", "", NULL);
    CHECK(qa_gameplay_authority_read(w.fx.combat, entity->actor.id).value.health == 60);
    (void)qa_q1_damage(game, entity->actor.id, player, &player, 25, NULL, "direct", "", NULL);
    CHECK(strcmp(monster->current_frame, "zombie_paine1") == 0);
    const double before = game->killed_monsters;
    (void)qa_q1_damage(game, entity->actor.id, player, &player, 100, NULL, "direct", "", NULL);
    CHECK(strcmp(entity->model, "progs/h_zombie.mdl") == 0);
    CHECK(!qa_gameplay_authority_read(w.fx.combat, entity->actor.id).value.can_take_damage);
    CHECK(game->killed_monsters == before + 1);
  }
  world_destroy(&w);
}

/* --- nightmare extends every monster's pain cooldown after its pain callback --- */
static void test_nightmare_pain(void) {
  base_world w;
  world_create(&w, false);
  qa_q1_services *game = w.fx.game;
  qa_q1_actor *entity = qa_q1_create(game, "monster_knight", NULL, NULL);
  qa_q1_spawn_entity(game, entity);
  qa_q1_base_monster *monster = controller(&w, entity);
  CHECK(monster != NULL);
  if (monster != NULL) {
    const qa_actor_id player = w.player.id;
    qa_q1_call_pain(game, entity, entity->pain, &player, 5);
    CHECK(monster->state->pain_finished < game->time + 5);
    game->options.skill = 3;
    monster->state->pain_finished = game->time + 100;
    qa_q1_call_pain(game, entity, entity->pain, &player, 5);
    CHECK(monster->state->pain_finished == game->time + 5);
  }
  world_destroy(&w);
}

/* --- BaseMonster vtable: TS `this.found()` reaches the override; super runs the base body --- */
typedef struct derived_monster {
  qa_q1_base_monster base; /* first: TS `extends BaseMonster` */
  int *ctx_check;
  int found_calls, run_calls;
} derived_monster;
static derived_monster *derived_of(qa_q1_base_monster *monster) {
  derived_monster *derived = (derived_monster *)monster;
  CHECK(derived->ctx_check == &sentinel);
  return derived;
}
static void derived_found(qa_q1_base_monster *monster, qa_actor_id target) {
  derived_of(monster)->found_calls++;
  qa_q1_base_monster_found_super(monster, target);
}
static void derived_run(qa_q1_base_monster *monster, double distance) {
  (void)distance;
  derived_of(monster)->run_calls++;
}
static void derived_destroy(qa_q1_base_monster *monster) { free(derived_of(monster)); }
static const qa_q1_base_monster_ops DERIVED_OPS = {
  .found = derived_found, .run = derived_run, .destroy = derived_destroy,
};
static void test_vtable(void) {
  base_world w;
  world_create(&w, false);
  qa_q1_services *game = w.fx.game;
  qa_q1_actor *entity = qa_q1_create(game, "monster_knight", NULL, NULL);
  const qa_q1_base_monster_species *knight = NULL;
  for (size_t i = 0; i < QA_Q1_BASE_SPECIES_COUNT; i++)
    if (strcmp(QA_Q1_BASE_SPECIES[i].species, "knight") == 0) knight = &QA_Q1_BASE_SPECIES[i];
  CHECK(knight != NULL);
  derived_monster *derived = calloc(1, sizeof *derived);
  CHECK(derived != NULL);
  if (knight != NULL && derived != NULL) {
    CHECK(qa_q1_base_monster_init(&derived->base, game, entity, knight,
                                  qa_q1_creatures_as_services(qa_q1_base_creatures(w.base)), NULL));
    derived->base.ops = &DERIVED_OPS;
    derived->ctx_check = &sentinel;
    /* entity.monster aliases the controller state (TS this.state = entity.monster). */
    CHECK(entity->monster == derived->base.state);
    qa_q1_creatures_monsters_set(qa_q1_base_creatures(w.base), entity->actor, &derived->base);
    qa_q1_base_monster_spawn(&derived->base);
    /* retaliate (base body) calls this.found(): the override, which calls super. */
    qa_q1_base_monster_retaliate(&derived->base, &w.player.id);
    CHECK(derived->found_calls == 1);
    CHECK(derived->base.state->has_enemy && qa_same_actor(derived->base.state->enemy, w.player.id));
    CHECK(strcmp(derived->base.state->mode, "run") == 0);
    /* ai("run") dispatches to the overriding run, not the base body. */
    qa_q1_base_monster_ai(&derived->base, QA_Q1_AI_RUN, 12);
    CHECK(derived->run_calls == 1);
    /* The named monster_found callback (registered for prefix "base") reaches the override too. */
    qa_q1_call_action(game, entity, qa_q1_callback_registry_action(game->named, entity, "base:monster_found"));
    CHECK(derived->found_calls == 2);
  }
  world_destroy(&w);
}

/* --- classic obituaries test water before the monster branch; rerelease reads the kill string --- */
static double fixed_random(void *ctx) { CHECK(ctx == &sentinel); return 0.4; }
static void test_obituaries(void) {
  qa_q1_obituary_actor victim;
  memset(&victim, 0, sizeof victim);
  victim.actor.slot = 1;
  victim.name = "Ranger"; victim.classname = "player"; victim.is_player = true;
  victim.health = -5; victim.water_type = "slime"; victim.water_level = 3; victim.kill_string = "";
  qa_q1_obituary_actor grunt = victim;
  grunt.actor.slot = 2;
  grunt.name = ""; grunt.classname = "monster_army"; grunt.is_player = false; grunt.is_monster = true;
  grunt.water_type = "empty"; grunt.water_level = 0; grunt.kill_string = "$qc_ks_grunt";
  qa_q1_obituary_input input;
  memset(&input, 0, sizeof input);
  input.victim = victim;
  input.attacker = &grunt;
  input.death_type = "";
  input.random = fixed_random;
  input.random_ctx = &sentinel;
  input.edition = "classic";
  qa_q1_obituary classic = qa_q1_obituary_compute(&input);
  CHECK(classic.has_message && strcmp(classic.text, "Ranger gulped a load of slime\n") == 0);
  input.edition = "rerelease";
  input.victim.water_type = "empty";
  qa_q1_obituary rerelease = qa_q1_obituary_compute(&input);
  CHECK(rerelease.has_message && strcmp(rerelease.text, "$qc_ks_grunt") == 0);
}

int main(void) {
  test_mission();
  test_zombie();
  test_nightmare_pain();
  test_vtable();
  test_obituaries();
  printf("test_q1_base: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
