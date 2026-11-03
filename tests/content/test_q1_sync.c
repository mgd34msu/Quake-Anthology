/*
 * Q1 foundation sync with the current TS (weapons, monsters, pickups,
 * spawns, movers, text) and the live skill/teamplay/gravity getters.
 * Expected values come from running the TS under bun over a harness that
 * mirrors q1_world_fixture.h: build/agents/q1-sync/bun/harness.ts with
 * foundation_sync.ts (W*, T*) and foundation_sync2.ts (M*, P*, S*, V*);
 * each block names its label. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

#include "q1_world_fixture.h"
#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/base/species.h"
#include "qa/content/q1/foundation/movers.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/content/q1/foundation/text.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/world/gameplay/pickups.h"

/* Sentinel ctx for every test-installed callback that is not a host double
 * (host doubles check the fixture magic through q1_world_fixture_of). */
typedef struct sentinel {
  unsigned magic;
  int calls, wrong;
  double value;
  qa_actor_id last_shooter;
  qa_vec3 position, normal;
  qa_actor_id hit;
  bool has_hit;
} sentinel;
#define SENTINEL_MAGIC 0x53594e43u
static sentinel *sentinel_of(void *ctx) {
  sentinel *s = ctx;
  if (s == NULL || s->magic != SENTINEL_MAGIC) {
    fprintf(stderr, "sentinel: callback received a foreign ctx\n");
    abort();
  }
  return s;
}

static q1_world_fixture FX;
static sentinel TRACE_HIT; /* what the world-hitting trace double returns */

static bool near(double a, double b) { return fabs(a - b) < 1e-9; }
static bool vec_eq(qa_vec3 a, double x, double y, double z) { return near(a.x, x) && near(a.y, y) && near(a.z, z); }

/* The grunt and the dog are base species; the fixture hosts them the way
 * the selected-monster source does (TS foundation.test.ts gameFor). */
static qa_q1_creatures *CREATURES;
static bool count_kill(void *ctx, qa_q1_base_monster *monster) { (void)ctx; (void)monster; return true; }
static void no_finale(void *ctx, qa_q1_base_monster *monster) { (void)ctx; (void)monster; }
static void world_begin(qa_q1_options options) {
  CHECK(q1_world_fixture_create(&FX, options));
  qa_q1_monster_services services = {NULL, count_kill, NULL, no_finale, no_finale};
  CREATURES = qa_q1_creatures_create(FX.game, services);
  CHECK(CREATURES != NULL);
  /* baseSpecies.filter(army | dog): the first two rows. */
  qa_q1_creatures_register_species(CREATURES, QA_Q1_BASE_SPECIES, 2);
  qa_q1_actor *world = qa_q1_create(FX.game, "worldspawn", NULL, NULL);
  qa_q1_spawn_entity(FX.game, world);
}
static void world_end(void) {
  qa_q1_creatures_destroy(CREATURES);
  CREATURES = NULL;
  q1_world_fixture_destroy(&FX);
}
static qa_q1_actor *spawn(const char *classname) {
  qa_q1_actor *entity = qa_q1_create(FX.game, classname, NULL, NULL);
  qa_q1_spawn_entity(FX.game, entity);
  return entity;
}
static void damageable(qa_q1_actor *entity, double health) {
  qa_gameplay_authority_set_health(FX.combat, entity->actor, health);
  qa_q1_actor_set_damageable(entity, true);
}
static void set_origin_velocity(qa_q1_actor *entity, qa_vec3 origin, bool has_velocity, qa_vec3 velocity) {
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_velocity = has_velocity;
  patch.velocity = velocity;
  qa_q1_set_body(FX.game, entity, &patch);
}
static void run(qa_q1_actor *entity, const char *name) {
  qa_q1_call_action(FX.game, entity, qa_q1_callback_registry_action(FX.game->named, entity, name));
}
static void touch(qa_q1_actor *entity, qa_actor_id other) {
  entity->touch.fn(entity->touch.ctx, FX.game, entity, other, NULL, NULL);
}
static size_t count_sounds(const char *path) {
  size_t count = 0;
  for (size_t i = 0; i < FX.event_count; i++)
    if (!strcmp(FX.events[i].kind, "sound") && FX.events[i].path != NULL && !strcmp(FX.events[i].path, path)) count++;
  return count;
}
static size_t count_events(const char *kind, const char *effect) {
  size_t count = 0;
  for (size_t i = 0; i < FX.event_count; i++)
    if (!strcmp(FX.events[i].kind, kind) &&
        (effect == NULL || (FX.events[i].effect != NULL && !strcmp(FX.events[i].effect, effect))))
      count++;
  return count;
}
static const qa_q1_event *last_event(const char *kind, const char *effect) {
  const qa_q1_event *found = NULL;
  for (size_t i = 0; i < FX.event_count; i++)
    if (!strcmp(FX.events[i].kind, kind) &&
        (effect == NULL || (FX.events[i].effect != NULL && !strcmp(FX.events[i].effect, effect))))
      found = &FX.events[i];
  return found;
}
static const char *think_name(const qa_q1_actor *entity) { return entity->think.fn != NULL ? entity->think.name : NULL; }
static qa_q1_actor *find_class(const char *classname) {
  for (size_t i = 0; i < FX.game->entity_count; i++)
    if (FX.game->entities[i]->classname != NULL && !strcmp(FX.game->entities[i]->classname, classname))
      return FX.game->entities[i];
  return NULL;
}

/* A trace double that strikes TRACE_HIT.hit (or nothing) at TRACE_HIT.position. */
static qa_q1_trace hit_trace(void *ctx, qa_q1_trace_request request) {
  (void)q1_world_fixture_of(ctx);
  sentinel *hit = sentinel_of(&TRACE_HIT);
  hit->calls++;
  if (hit->calls == 1) hit->value = request.start.z;
  qa_q1_trace trace;
  memset(&trace, 0, sizeof trace);
  trace.fraction = 0.5;
  trace.end = hit->position;
  trace.normal = hit->normal;
  trace.in_open = true;
  /* Monster-blocking traces see the target; world-only traces see nothing. */
  if (hit->has_hit && request.monsters) {
    trace.has_actor = true;
    trace.actor = hit->hit;
  }
  return trace;
}
static void use_hit_trace(qa_vec3 end, qa_vec3 normal, const qa_actor_id *actor) {
  memset(&TRACE_HIT, 0, sizeof TRACE_HIT);
  TRACE_HIT.magic = SENTINEL_MAGIC;
  TRACE_HIT.position = end;
  TRACE_HIT.normal = normal;
  TRACE_HIT.has_hit = actor != NULL;
  if (actor != NULL) TRACE_HIT.hit = *actor;
  FX.game->host.trace = hit_trace;
}

static void bullet_hole(void *ctx, qa_owned_actor shooter, qa_vec3 position, qa_vec3 normal) {
  sentinel *s = sentinel_of(ctx);
  s->calls++;
  s->last_shooter = shooter.id;
  s->position = position;
  s->normal = normal;
}
static bool bloody(void *ctx) { (void)sentinel_of(ctx); return false; }

/* W1: fireBullets starts at absmin (SV_LinkEdict padding) and calls the
 * Hipnotic placebullethole once per FireBullets call. */
static void test_fire_bullets(void) {
  world_begin(q1_world_fixture_options());
  const qa_owned_actor p = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  const qa_actor_id world = FX.game->world->actor.id;
  use_hit_trace((qa_vec3){100, 0, 14}, (qa_vec3){-1, 0, 0}, &world);
  sentinel holes = {.magic = SENTINEL_MAGIC};
  FX.game->source_bullet_hole.ctx = &holes;
  FX.game->source_bullet_hole.fn = bullet_hole;
  qa_q1_fire_bullets(FX.game, p, (qa_vec3){1, 0, 0}, QA_Q1_ZERO, 6, 0.04, 0.04, "shotgun");
  CHECK(TRACE_HIT.value == 14.199999809265137); /* W1.start_z */
  CHECK(TRACE_HIT.calls == 6);                  /* W1.traces */
  CHECK(holes.calls == 1);                      /* W1.holes */
  CHECK(holes.last_shooter.slot == 1 && vec_eq(holes.position, 100, 0, 14) && vec_eq(holes.normal, -1, 0, 0));
  CHECK(FX.ctx_errors == 0);
  world_end();
}

/* W2: BloodyNightmare halves a direct rocket on the super shambler. */
static void test_rocket_bloody(void) {
  for (int with = 0; with < 2; with++) {
    world_begin(q1_world_fixture_options());
    const qa_owned_actor p = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
    qa_q1_actor *target = qa_q1_create(FX.game, "monster_super_shambler", NULL, NULL);
    damageable(target, 500);
    set_origin_velocity(target, (qa_vec3){1000, 0, 0}, false, QA_Q1_ZERO);
    sentinel s = {.magic = SENTINEL_MAGIC};
    if (with) { FX.game->source_bloody_nightmare.ctx = &s; FX.game->source_bloody_nightmare.fn = bloody; }
    qa_q1_actor *rocket = qa_q1_create(FX.game, "missile", NULL, NULL);
    rocket->projectile = "rocket";
    rocket->has_owner = true;
    rocket->owner = p.id;
    rocket->projectile_weapon = "rocketlauncher";
    set_origin_velocity(rocket, (qa_vec3){990, 0, 0}, true, (qa_vec3){1000, 0, 0});
    qa_q1_projectile_touch(FX.game, rocket, &target->actor.id, QA_Q1_ZERO, NULL);
    CHECK(qa_q1_health(FX.game, target->actor.id) == (with ? 446 : 392)); /* W2.health */
    CHECK(s.calls == 0); /* TS tests the hook's presence, never calls it */
    world_end();
  }
}

/* W3: MG grenade fix. */
static void test_grenade_fix(void) {
  world_begin(q1_world_fixture_options());
  const qa_owned_actor p = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  const qa_actor_id world = FX.game->world->actor.id;
  FX.game->source_grenade_fix = true;
  qa_q1_player_state *player = qa_q1_attach_player(FX.game, p, "shotgun", true, NULL);
  (void)qa_shared_inventory_table_give(FX.inventory, p, "q1:weapon/grenadelauncher", 1);
  (void)qa_shared_inventory_table_give(FX.inventory, p, "q1:ammo/rockets", 10);
  player->weapon = "grenadelauncher";
  player->view_angles = (qa_vec3){-10, 30, 0};
  qa_q1_services_set_time(FX.game, 1);
  CHECK(qa_q1_fire_weapon(FX.game, player));
  qa_q1_actor *g = find_class("grenade");
  CHECK(g != NULL);
  if (g == NULL) { world_end(); return; }
  CHECK(think_name(g) != NULL && !strcmp(think_name(g), "GrenadeThink")); /* W3.think */
  CHECK(g->next_think == 1.0499999523162842);                              /* W3.next */
  const qa_vec3 angles = qa_q1_get_body(FX.game, g).angles;                 /* W3.angles */
  CHECK(fabs(angles.x - -28.262735143426234) < 1e-9 && fabs(angles.y - 30.205912708108198) < 1e-9 && angles.z == 0);
  qa_q1_projectile_touch(FX.game, g, &world, QA_Q1_ZERO, NULL);
  qa_q1_projectile_touch(FX.game, g, &world, QA_Q1_ZERO, NULL);
  CHECK(count_sounds("weapons/bounce.wav") == 1); /* W3.bounces.same_time */
  qa_q1_services_set_time(FX.game, 1.2);
  qa_q1_projectile_touch(FX.game, g, &world, QA_Q1_ZERO, NULL);
  CHECK(count_sounds("weapons/bounce.wav") == 2); /* W3.bounces.later */
  CHECK(g->attack_finished == 1.300000047683716);
  /* GrenadeThink reflects a grenade stopped on a floor it was falling onto. */
  set_origin_velocity(g, QA_Q1_ZERO, true, QA_Q1_ZERO);
  (void)qa_q1_actor_field_set(g, "oldorigin", "-200 0 -200");
  g->count = 3;
  g->movement_flags = 512 | 1;
  use_hit_trace(QA_Q1_ZERO, (qa_vec3){0, 0, 1}, NULL);
  run(g, "GrenadeThink");
  CHECK(vec_eq(qa_q1_get_body(FX.game, g).velocity, -120, 0, 120)); /* W3.think.velocity */
  CHECK(!strcmp(qa_q1_actor_field_get(g, "oldorigin"), "-120 0 120"));
  CHECK(g->movement_flags == 1 && g->count == 4 && vec_eq(g->angular_velocity, 400, 400, 400));
  CHECK(think_name(g) != NULL && !strcmp(think_name(g), "GrenadeThink"));
  /* A direct grenade hit on the final boss deals 120 and explodes. */
  qa_q1_actor *boss = qa_q1_create(FX.game, "monster_boss", NULL, NULL);
  damageable(boss, 3000);
  boss->aimed_damage = true;
  qa_q1_actor *g2 = qa_q1_create(FX.game, "grenade", NULL, NULL);
  g2->projectile = "grenade";
  g2->has_owner = true;
  g2->owner = p.id;
  g2->projectile_weapon = "grenadelauncher";
  qa_q1_projectile_touch(FX.game, g2, &boss->actor.id, QA_Q1_ZERO, NULL);
  CHECK(qa_q1_health(FX.game, boss->actor.id) == 2880 && !qa_q1_live(FX.game, g2)); /* W3.boss */
  CHECK(FX.ctx_errors == 0);
  world_end();
}

/* W4: lightning hits emit particles (MG3 color 246), not blood. */
static void test_lightning_particles(void) {
  for (int mg3 = 0; mg3 < 2; mg3++) {
    world_begin(q1_world_fixture_options());
    const qa_owned_actor p = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
    FX.game->mg3_pickups = mg3 != 0;
    qa_q1_actor *target = qa_q1_create(FX.game, "monster_army", NULL, NULL);
    damageable(target, 100);
    use_hit_trace((qa_vec3){50, 0, 16}, QA_Q1_ZERO, &target->actor.id);
    qa_q1_player_state *player = qa_q1_attach_player(FX.game, p, "shotgun", true, NULL);
    (void)qa_shared_inventory_table_give(FX.inventory, p, "q1:weapon/lightning", 1);
    (void)qa_shared_inventory_table_give(FX.inventory, p, "q1:ammo/cells", 10);
    player->weapon = "lightning";
    qa_q1_services_set_time(FX.game, 1);
    CHECK(qa_q1_fire_weapon(FX.game, player));
    const qa_q1_event *particles = last_event("particles", NULL);
    CHECK(count_events("particles", NULL) == 1 && particles != NULL); /* W4.particles */
    if (particles != NULL)
      CHECK(particles->color == (mg3 ? 246 : 225) && particles->count == 120 && vec_eq(particles->origin, 50, 0, 16) &&
            vec_eq(particles->direction, 0, 0, 100));
    CHECK(count_events("effect", "blood") == 0);             /* W4.blood */
    CHECK(qa_q1_health(FX.game, target->actor.id) == 70);    /* W4.health */
    world_end();
  }
}

/* W6: the axe hit spawns blood four units short and never runs during intermission. */
static void test_axe(void) {
  for (int intermission = 0; intermission < 2; intermission++) {
    world_begin(q1_world_fixture_options());
    const qa_owned_actor p = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
    qa_q1_actor *target = qa_q1_create(FX.game, "monster_army", NULL, NULL);
    damageable(target, 100);
    use_hit_trace((qa_vec3){40, 0, 16}, QA_Q1_ZERO, &target->actor.id);
    qa_q1_player_state *player = qa_q1_attach_player(FX.game, p, "axe", true, NULL);
    qa_q1_services_set_time(FX.game, 1);
    CHECK(qa_q1_fire_weapon(FX.game, player));
    FX.game->has_intermission = intermission != 0;
    qa_q1_actor *strike = find_class("axe_strike");
    CHECK(strike != NULL);
    if (strike != NULL) run(strike, "player_axe3");
    const qa_q1_event *blood = last_event("effect", "blood");
    CHECK(qa_q1_health(FX.game, target->actor.id) == (intermission ? 100 : 80)); /* W6 */
    CHECK(intermission ? blood == NULL : blood != NULL && vec_eq(blood->origin, 36, 0, 16));
    CHECK(strike == NULL || !qa_q1_live(FX.game, strike));
    FX.game->has_intermission = false;
    world_end();
  }
}

/* W7: SuperDamageSound once a second. */
static void test_quad_sound(void) {
  world_begin(q1_world_fixture_options());
  const qa_owned_actor p = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  qa_q1_player_state *player = qa_q1_attach_player(FX.game, p, "shotgun", true, NULL);
  qa_q1_services_set_time(FX.game, 1);
  qa_q1_give_powerup(FX.game, player, "quad", 30);
  (void)qa_q1_fire_weapon(FX.game, player);
  CHECK(count_sounds("items/damage3.wav") == 1); /* W7.first */
  qa_q1_services_set_time(FX.game, 1.6);
  (void)qa_q1_fire_weapon(FX.game, player);
  CHECK(count_sounds("items/damage3.wav") == 1); /* W7.second */
  qa_q1_services_set_time(FX.game, 2.2);
  (void)qa_q1_fire_weapon(FX.game, player);
  CHECK(count_sounds("items/damage3.wav") == 2); /* W7.third */
  world_end();
}

/* W8: bestWeapon / chooseBest with an ammoCount override. */
static double nails(void *ctx, qa_item_id item) {
  sentinel *s = sentinel_of(ctx);
  s->calls++;
  return !strcmp(item, "q1:ammo/nails") ? s->value : 0;
}
static void test_best_weapon_counted(void) {
  world_begin(q1_world_fixture_options());
  const qa_owned_actor p = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(FX.game, p, "shotgun", true, NULL);
  (void)qa_shared_inventory_table_give(FX.inventory, p, "q1:weapon/supernailgun", 1);
  (void)qa_shared_inventory_table_give(FX.inventory, p, "q1:weapon/nailgun", 1);
  sentinel s = {.magic = SENTINEL_MAGIC, .value = 1};
  CHECK(!strcmp(qa_q1_best_weapon(FX.game, p), "shotgun"));                       /* W8.inventory */
  CHECK(!strcmp(qa_q1_best_weapon_counted(FX.game, p, nails, &s), "nailgun"));     /* W8.override */
  s.value = 2;
  CHECK(!strcmp(qa_q1_choose_best_counted(FX.game, p, nails, &s), "supernailgun")); /* W8.override2 */
  CHECK(s.calls > 0);
  world_end();
}

static void monster_pain(qa_q1_actor *entity, qa_actor_id attacker) {
  entity->pain.fn(entity->pain.ctx, FX.game, entity, &attacker, 5);
}
static qa_q1_actor *spawn_monster(const char *classname) {
  qa_q1_actor *entity = spawn(classname);
  run(entity, "walkmonster_start_go");
  return entity;
}

/* M1/M2: retaliate keeps oldEnemy when the attacker is the enemy; death
 * targets fire with attacker ?? enemy. */
static void test_monster_retaliate_and_death(void) {
  world_begin(q1_world_fixture_options());
  const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  const qa_owned_actor b = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  qa_q1_actor *dog = spawn_monster("monster_dog");
  dog->monster->has_enemy = true;
  dog->monster->enemy = a.id;
  qa_q1_services_set_time(FX.game, 2);
  monster_pain(dog, a.id);
  CHECK(!dog->monster->has_old_enemy && dog->monster->enemy.slot == 1); /* M1 */
  monster_pain(dog, b.id);
  CHECK(dog->monster->enemy.slot == 2 && dog->monster->has_old_enemy && dog->monster->old_enemy.slot == 1);
  qa_q1_actor *relay = qa_q1_create(FX.game, "target_delay", NULL, NULL);
  free(relay->targetname);
  relay->targetname = strdup("t1");
  relay->wait = 5;
  qa_q1_spawn_entity(FX.game, relay);
  free(dog->target);
  dog->target = strdup("t1");
  dog->monster->enemy = a.id;
  qa_gameplay_authority_set_health(FX.combat, dog->actor, -10);
  dog->die.fn(dog->die.ctx, FX.game, dog, &b.id);
  CHECK(relay->has_activator && relay->activator.slot == 2); /* M2.activator */
  world_end();
}

/* M3: the nightmare pain cooldown follows every th_pain call, dog included. */
static void test_monster_nightmare_pain(void) {
  for (int skill = 1; skill <= 3; skill += 2) {
    qa_q1_options options = q1_world_fixture_options();
    options.skill = skill;
    world_begin(options);
    const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
    qa_q1_actor *dog = spawn_monster("monster_dog");
    qa_q1_services_set_time(FX.game, 4);
    monster_pain(dog, a.id);
    CHECK(dog->monster->pain_finished == (skill == 3 ? 9 : 0)); /* M3.dog */
    qa_q1_actor *army = spawn_monster("monster_army");
    army->monster->pain_finished = 10;
    monster_pain(army, a.id);
    CHECK(army->monster->pain_finished == (skill == 3 ? 9 : 10)); /* M3.army */
    world_end();
  }
}

/* M4: V101 skips the attack delay on nightmare; sightEntity only for players. */
static void test_monster_found_target(void) {
  for (int rerelease = 0; rerelease < 2; rerelease++) {
    qa_q1_options options = q1_world_fixture_options();
    options.skill = 3;
    options.edition = rerelease ? "rerelease" : "classic";
    world_begin(options);
    const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
    qa_q1_actor *dog = spawn_monster("monster_dog");
    qa_q1_services_set_time(FX.game, 3);
    dog->monster->has_enemy = true;
    dog->monster->enemy = a.id;
    run(dog, "monster_found_target");
    CHECK(dog->monster->attack_finished == (rerelease ? 4 : 0)); /* M4 */
    CHECK(FX.game->sight_entity == dog && dog->monster->search_until == 8);
    qa_q1_actor *other = spawn_monster("monster_army");
    FX.game->sight_entity = NULL;
    dog->monster->enemy = other->actor.id;
    run(dog, "monster_found_target");
    CHECK(FX.game->sight_entity == NULL); /* M4.monster_target */
    world_end();
  }
}

/* M5: Dog_JumpTouch hits at most every 0.5 s under the MG StartItem programs. */
static bool item_start(void *ctx, qa_q1_actor *entity, const char **place, const char **use) {
  sentinel *s = sentinel_of(ctx);
  (void)entity;
  s->calls++;
  *place = "PlaceItem";
  *use = NULL;
  return true;
}
static void test_dog_leap_limit(void) {
  for (int limited = 0; limited < 2; limited++) {
    world_begin(q1_world_fixture_options());
    const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
    sentinel s = {.magic = SENTINEL_MAGIC};
    if (limited) { FX.game->source_item_start.ctx = &s; FX.game->source_item_start.fn = item_start; }
    qa_q1_actor *dog = spawn_monster("monster_dog");
    qa_q1_services_set_time(FX.game, 2);
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_velocity = true;
    patch.velocity = (qa_vec3){400, 0, 0};
    patch.has_ground = true;
    qa_q1_set_body(FX.game, dog, &patch);
    dog->touch = qa_q1_callback_registry_touch(FX.game->named, dog, "Dog_JumpTouch");
    touch(dog, a.id);
    touch(dog, a.id);
    CHECK(qa_q1_health(FX.game, a.id) == (limited ? 86 : 72)); /* M5 */
    CHECK(dog->monster->attack_finished == (limited ? 2.5 : 0));
    world_end();
  }
}

/* P1/P2: the weapon supply offer carries its ammo list; preview receipts. */
static void test_supply(void) {
  world_begin(q1_world_fixture_options());
  const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(FX.game, a, "shotgun", true, NULL);
  qa_q1_actor *gun = spawn("weapon_nailgun");
  gun->solid = "trigger";
  qa_pickup_supply_observation observation;
  CHECK(qa_q1_observe_supply(FX.game, gun->actor.id, a.id, &observation)); /* P1.observe */
  CHECK(observation.offer.kind == QA_PICKUP_SUPPLY_WEAPON && !strcmp(observation.offer.item, "q1:weapon/nailgun"));
  CHECK(observation.offer.ammo_count == 1 && observation.offer.ammo != NULL &&
        !strcmp(observation.offer.ammo[0].item, "q1:ammo/nails") && observation.offer.ammo[0].amount == 30);
  CHECK(!strcmp(observation.availability_kind, "ready") && observation.eligible);
  qa_pickup_supply_preview preview;
  CHECK(qa_q1_preview_supply(FX.game, gun->actor.id, a.id, &preview)); /* P2.preview */
  CHECK(preview.accepted && preview.weapon_count == 1 && preview.ammo_count == 1);
  if (preview.weapon_count == 1 && preview.ammo_count == 1) {
    CHECK(!strcmp(preview.weapons[0].item, "q1:weapon/nailgun") && preview.weapons[0].before == 0 && preview.weapons[0].given == 1);
    CHECK(!strcmp(preview.ammo[0].item, "q1:ammo/nails") && preview.ammo[0].before == 0 && preview.ammo[0].given == 30);
  }
  qa_free_pickup_supply_preview(preview);
  qa_q1_actor *shells = spawn("item_shells");
  shells->solid = "trigger";
  CHECK(qa_q1_preview_supply(FX.game, shells->actor.id, a.id, &preview)); /* P2.preview.ammo */
  CHECK(preview.accepted && preview.ammo_count == 1 && preview.weapon_count == 0);
  if (preview.ammo_count == 1)
    CHECK(!strcmp(preview.ammo[0].item, "q1:ammo/shells") && preview.ammo[0].before == 25 && preview.ammo[0].given == 20);
  qa_free_pickup_supply_preview(preview);
  world_end();
}

/* Message args live only during the emit: this emit double (fixture ctx)
 * records each message's first argument before forwarding. */
static struct { char text[64]; bool center, is_number; double number; char arg[64]; size_t arg_count; } MESSAGES[8];
static size_t MESSAGE_COUNT;
static void recording_emit(void *ctx, qa_q1_event event) {
  (void)q1_world_fixture_of(ctx);
  if (!strcmp(event.kind, "message") && MESSAGE_COUNT < 8) {
    snprintf(MESSAGES[MESSAGE_COUNT].text, sizeof MESSAGES[0].text, "%s", event.text != NULL ? event.text : "");
    MESSAGES[MESSAGE_COUNT].center = event.center;
    MESSAGES[MESSAGE_COUNT].arg_count = event.arg_count;
    if (event.arg_count > 0) {
      MESSAGES[MESSAGE_COUNT].is_number = event.args[0].is_number;
      MESSAGES[MESSAGE_COUNT].number = event.args[0].number;
      snprintf(MESSAGES[MESSAGE_COUNT].arg, sizeof MESSAGES[0].arg, "%s", event.args[0].text != NULL ? event.args[0].text : "");
    }
    MESSAGE_COUNT++;
  }
  q1_wf_emit(ctx, event);
}

/* P3/P4/P5: MG3 mega cap and cells, pickup messages. */
static void test_mg3_pickups(void) {
  for (int mg3 = 0; mg3 < 2; mg3++) {
    world_begin(q1_world_fixture_options());
    const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
    (void)qa_q1_attach_player(FX.game, a, "shotgun", true, NULL);
    FX.game->mg3_pickups = mg3 != 0;
    FX.game->host.emit = recording_emit;
    MESSAGE_COUNT = 0;
    qa_gameplay_authority_set_health(FX.combat, a, 240);
    qa_q1_actor *mega = qa_q1_create(FX.game, "item_health", NULL, NULL);
    mega->spawnflags = 2;
    qa_q1_spawn_entity(FX.game, mega);
    CHECK(qa_q1_give_pickup(FX.game, mega, a.id));
    CHECK(qa_q1_health(FX.game, a.id) == (mg3 ? 340 : 250)); /* P3.health */
    qa_q1_actor *cells = spawn("item_cells");
    CHECK(qa_q1_give_pickup(FX.game, cells, a.id));
    CHECK(qa_shared_inventory_table_count(FX.inventory, a.id, "q1:ammo/cells") == (mg3 ? 10 : 6)); /* P4.cells */
    CHECK(MESSAGE_COUNT == 2);
    CHECK(!strcmp(MESSAGES[0].text, "$qc_item_health") && !MESSAGES[0].center && MESSAGES[0].arg_count == 1 &&
          MESSAGES[0].is_number && MESSAGES[0].number == 100);
    CHECK(!strcmp(MESSAGES[1].text, "$qc_got_item") && !MESSAGES[1].center && MESSAGES[1].arg_count == 1 &&
          !MESSAGES[1].is_number && !strcmp(MESSAGES[1].arg, "$qc_cells"));
    /* P5.messages */
    world_end();
  }
}

/* P6: backpack cargo through the source admission; unknown weapons ignored. */
static void test_backpack(void) {
  world_begin(q1_world_fixture_options());
  const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  qa_q1_player_state *player = qa_q1_attach_player(FX.game, a, "shotgun", true, NULL);
  qa_q1_actor *pack = qa_q1_create(FX.game, "item_backpack", NULL, NULL);
  (void)qa_q1_actor_field_set(pack, "weapon", "bogus");
  (void)qa_q1_actor_field_set(pack, "shells", "5");
  (void)qa_q1_actor_field_set(pack, "rockets", "3");
  qa_q1_spawn_entity(FX.game, pack);
  CHECK(qa_q1_give_pickup(FX.game, pack, a.id));
  CHECK(qa_shared_inventory_table_count(FX.inventory, a.id, "q1:ammo/shells") == 30); /* P6.bogus */
  CHECK(qa_shared_inventory_table_count(FX.inventory, a.id, "q1:ammo/rockets") == 3);
  CHECK(!qa_q1_live(FX.game, pack) && !strcmp(player->weapon, "shotgun"));
  qa_q1_actor *pack2 = qa_q1_create(FX.game, "item_backpack", NULL, NULL);
  (void)qa_q1_actor_field_set(pack2, "weapon", "rocketlauncher");
  (void)qa_q1_actor_field_set(pack2, "nails", "7");
  qa_q1_spawn_entity(FX.game, pack2);
  CHECK(qa_q1_give_pickup(FX.game, pack2, a.id));
  CHECK(qa_shared_inventory_table_count(FX.inventory, a.id, "q1:weapon/rocketlauncher") == 1); /* P6.rocket */
  CHECK(qa_shared_inventory_table_count(FX.inventory, a.id, "q1:ammo/nails") == 7 && !strcmp(player->weapon, "rocketlauncher"));
  world_end();
}

/* P7/P8/P9: StartItem health_rot regeneration, DROPTOFLOOR_DISABLE, horde quad. */
static void test_start_item_programs(void) {
  qa_q1_options options = q1_world_fixture_options();
  options.deathmatch = 3;
  world_begin(options);
  const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(FX.game, a, "shotgun", true, NULL);
  sentinel s = {.magic = SENTINEL_MAGIC};
  FX.game->source_item_start.ctx = &s;
  FX.game->source_item_start.fn = item_start;
  qa_q1_actor *mega = qa_q1_create(FX.game, "item_health", NULL, NULL);
  mega->spawnflags = 2;
  qa_q1_spawn_entity(FX.game, mega);
  CHECK(s.calls == 1);
  qa_q1_services_set_time(FX.game, 5);
  CHECK(qa_q1_give_pickup(FX.game, mega, a.id));
  CHECK(think_name(mega) != NULL && !strcmp(think_name(mega), "health_rot") && mega->next_think == 10 &&
        mega->has_owner && mega->owner.slot == 1); /* P7.after_take */
  qa_gameplay_authority_set_health(FX.combat, a, 100);
  run(mega, "health_rot");
  CHECK(think_name(mega) != NULL && !strcmp(think_name(mega), "SUB_regen") && mega->next_think == 25); /* P7.regen */
  qa_q1_actor *floating = qa_q1_create(FX.game, "item_shells", NULL, NULL);
  floating->spawnflags = 65536;
  set_origin_velocity(floating, (qa_vec3){5, 6, 7}, false, QA_Q1_ZERO);
  qa_q1_spawn_entity(FX.game, floating);
  CHECK(think_name(floating) != NULL && !strcmp(think_name(floating), "PlaceItem")); /* P8.spawn */
  run(floating, "PlaceItem");
  CHECK(vec_eq(qa_q1_get_body(FX.game, floating).origin, 5, 6, 13) && !strcmp(floating->solid, "trigger") &&
        !strcmp(floating->movement, "none") && floating->movement_flags == 256); /* P8.place */
  world_end();

  world_begin(q1_world_fixture_options());
  const qa_owned_actor b = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  qa_q1_player_state *player = qa_q1_attach_player(FX.game, b, "shotgun", true, NULL);
  qa_q1_services_set_time(FX.game, 2);
  (void)qa_q1_create(FX.game, "horde_manager", NULL, NULL);
  qa_q1_actor *quad = spawn("item_artifact_super_damage");
  CHECK(qa_q1_give_pickup(FX.game, quad, b.id));
  CHECK(qa_q1_player_powerup_get(player, "quad") == 7); /* P9.quad */
  world_end();
}

/* P10: COOP_RESPAWN_ITEMS_FOR_PLAYERS. */
static void test_coop_shared_items(void) {
  qa_q1_options options = q1_world_fixture_options();
  options.coop = true;
  world_begin(options);
  const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  const qa_owned_actor b = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(FX.game, a, "shotgun", true, NULL);
  (void)qa_q1_attach_player(FX.game, b, "shotgun", true, NULL);
  sentinel s = {.magic = SENTINEL_MAGIC};
  FX.game->source_item_start.ctx = &s;
  FX.game->source_item_start.fn = item_start;
  qa_q1_actor *shells = spawn("item_shells");
  qa_q1_services_set_time(FX.game, 1);
  CHECK(qa_q1_give_pickup(FX.game, shells, a.id));
  qa_actor_id enemy;
  CHECK(shells->count == 1 && think_name(shells) != NULL && !strcmp(think_name(shells), "SUB_regen") &&
        shells->next_think == 2); /* P10.first */
  CHECK(qa_q1_actor_reference_get(shells, "enemy", &enemy) && enemy.slot == 1);
  CHECK(qa_shared_inventory_table_count(FX.inventory, a.id, "q1:ammo/shells") == 45);
  shells->solid = "trigger";
  CHECK(qa_q1_give_pickup(FX.game, shells, a.id));
  CHECK(qa_shared_inventory_table_count(FX.inventory, a.id, "q1:ammo/shells") == 45); /* P10.repeat */
  CHECK(qa_q1_give_pickup(FX.game, shells, b.id));
  CHECK(shells->count == 0 && shells->next_think == 361 && !qa_q1_actor_reference_get(shells, "enemy", &enemy)); /* P10.second */
  CHECK(qa_shared_inventory_table_count(FX.inventory, b.id, "q1:ammo/shells") == 45);
  world_end();
}

/* S1/S2: teleport ignoreTargetname, push_touch fly sound limiter and removal. */
static void test_triggers(void) {
  world_begin(q1_world_fixture_options());
  const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(FX.game, a, "shotgun", true, NULL);
  qa_q1_actor *dest = qa_q1_create(FX.game, "info_teleport_destination", NULL, NULL);
  free(dest->targetname);
  dest->targetname = strdup("dest");
  set_origin_velocity(dest, (qa_vec3){100, 0, 0}, false, QA_Q1_ZERO);
  qa_q1_spawn_entity(FX.game, dest);
  qa_q1_actor *tele = qa_q1_create(FX.game, "trigger_teleport", NULL, NULL);
  free(tele->target);
  tele->target = strdup("dest");
  free(tele->targetname);
  tele->targetname = strdup("gate");
  qa_q1_spawn_entity(FX.game, tele);
  qa_q1_services_set_time(FX.game, 5);
  touch(tele, a.id);
  CHECK(vec_eq(qa_shared_body_table_read(FX.bodies, a.id).value.origin, 0, 0, 0)); /* S1.locked */
  (void)qa_q1_actor_field_set(tele, "teleport.ignoreTargetname", "1");
  touch(tele, a.id);
  CHECK(vec_eq(qa_shared_body_table_read(FX.bodies, a.id).value.origin, 100, 0, 27)); /* S1.open */
  qa_q1_actor *push = qa_q1_create(FX.game, "trigger_push", NULL, NULL);
  push->speed = 50;
  qa_q1_spawn_entity(FX.game, push);
  touch(push, a.id);
  touch(push, a.id);
  CHECK(count_sounds("ambience/windfly.wav") == 1); /* S2.sounds */
  qa_q1_services_set_time(FX.game, 7);
  touch(push, a.id);
  CHECK(count_sounds("ambience/windfly.wav") == 2); /* S2.sounds.later */
  qa_q1_actor *once = qa_q1_create(FX.game, "trigger_push", NULL, NULL);
  once->spawnflags = 1;
  qa_q1_spawn_entity(FX.game, once);
  qa_gameplay_authority_set_health(FX.combat, a, 0);
  touch(once, a.id);
  CHECK(!qa_q1_live(FX.game, once)); /* S2.once_removed */
  world_end();
}

/* V1/V2/V3: MG door keys, buttons and the door message talk sound. */
static void test_movers(void) {
  world_begin(q1_world_fixture_options());
  const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(FX.game, a, "shotgun", true, NULL);
  FX.game->source_door_keys = true;
  qa_q1_actor *door = qa_q1_create(FX.game, "func_door", NULL, NULL);
  (void)qa_q1_actor_field_set(door, "movedir", "0 0 64");
  (void)qa_q1_actor_field_set(door, "speed2", "50");
  door->spawnflags = 4096 | 64;
  free(door->message);
  door->message = strdup("hello");
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = (qa_vec3){10, 0, 0};
  patch.has_bounds = true;
  patch.bounds = (qa_bounds){{0, 0, 0}, {64, 8, 96}};
  qa_q1_set_body(FX.game, door, &patch);
  qa_q1_spawn_entity(FX.game, door);
  CHECK(!strcmp(door->solid, "none") && vec_eq(door->pos1, 10, 0, 0) && vec_eq(door->pos2, 10, 0, 64) &&
        vec_eq(door->movedir, 0, 0, 64)); /* V1.door */
  qa_q1_set_origin(FX.game, door, door->pos2);
  door->state = "up";
  run(door, "door_go_down");
  CHECK(door->has_move && vec_eq(door->move.destination, 10, 0, 0) && door->next_think == 1.2799999713897705); /* V1.speed */
  qa_q1_actor *victim = qa_q1_create(FX.game, "monster_army", NULL, NULL);
  damageable(victim, 100);
  door->state = "down";
  door->blocked.fn(door->blocked.ctx, FX.game, door, victim->actor.id);
  CHECK(!strcmp(door->state, "down") && qa_q1_health(FX.game, victim->actor.id) == 98); /* V1.crusher */
  qa_q1_services_set_time(FX.game, 3);
  FX.event_count = 0;
  touch(door, a.id);
  CHECK(count_sounds("misc/talk.wav") == 1); /* V3.sounds */
  qa_q1_actor *button = qa_q1_create(FX.game, "func_button", NULL, NULL);
  button->spawnflags = 8;
  memset(&patch, 0, sizeof patch);
  patch.has_bounds = true;
  patch.bounds = (qa_bounds){{0, 0, 0}, {8, 8, 8}};
  qa_q1_set_body(FX.game, button, &patch);
  qa_q1_spawn_entity(FX.game, button);
  FX.event_count = 0;
  FX.game->host.emit = recording_emit; /* key_message formats its text on the stack */
  MESSAGE_COUNT = 0;
  touch(button, a.id);
  CHECK(!strcmp(button->state, "bottom") && MESSAGE_COUNT == 1 && !strcmp(MESSAGES[0].text, "$qc_need_gold_key")); /* V2.locked */
  (void)qa_shared_inventory_table_give(FX.inventory, a, "q1:key/gold", 1);
  touch(button, a.id);
  CHECK(!strcmp(button->state, "up") && qa_shared_inventory_table_count(FX.inventory, a.id, "q1:key/gold") == 0); /* V2.open */
  CHECK(count_sounds("doors/medtry.wav") == 1 && count_sounds("doors/meduse.wav") == 1 &&
        count_sounds("buttons/airbut1.wav") == 1);
  world_end();
}

/* Live options: qa_q1_options.live getters are read on every access. */
static double live_skill(void *ctx) { sentinel *s = sentinel_of(ctx); s->calls++; return s->value; }
static double live_teamplay(void *ctx) { sentinel *s = sentinel_of(ctx); s->calls++; return s->value + 10; }
static double live_gravity(void *ctx) { sentinel *s = sentinel_of(ctx); s->calls++; return s->value * 100; }
static void test_live_options(void) {
  world_begin(q1_world_fixture_options());
  const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  sentinel s = {.magic = SENTINEL_MAGIC, .value = 1};
  FX.game->options.live.ctx = &s;
  FX.game->options.live.skill = live_skill;
  FX.game->options.live.teamplay = live_teamplay;
  FX.game->options.live.gravity = live_gravity;
  CHECK(qa_q1_option_skill(FX.game) == 1 && qa_q1_option_teamplay(FX.game) == 11 && qa_q1_option_gravity(FX.game) == 100);
  qa_q1_actor *dog = spawn_monster("monster_dog");
  qa_q1_services_set_time(FX.game, 4);
  monster_pain(dog, a.id);
  CHECK(dog->monster->pain_finished == 0); /* M3.dog.1 */
  s.value = 3; /* the cvar changed outside any composition entry point */
  monster_pain(dog, a.id);
  CHECK(dog->monster->pain_finished == 9); /* M3.dog.3 */
  CHECK(qa_q1_option_skill(FX.game) == 3 && qa_q1_option_teamplay(FX.game) == 13 && qa_q1_option_gravity(FX.game) == 300);
  CHECK(s.calls > 0);
  world_end();
}

/* Late TS changes (foundation_sync3.ts L1-L4). */
static sentinel TRACE_LOG;
static qa_q1_trace logging_trace(void *ctx, qa_q1_trace_request request) {
  (void)q1_world_fixture_of(ctx);
  sentinel *log = sentinel_of(&TRACE_LOG);
  log->calls++;
  log->position = request.start;
  log->normal = request.end; /* reused: the request end */
  log->has_hit = request.monsters;
  qa_q1_trace trace;
  memset(&trace, 0, sizeof trace);
  trace.fraction = 1;
  trace.end = request.end;
  trace.in_open = true;
  return trace;
}
static void test_late_changes(void) {
  /* L1: a gold door ring of three; the touched door and the next lose touch,
   * doorUse plays the key-use sound once. */
  world_begin(q1_world_fixture_options());
  const qa_owned_actor a = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(FX.game, a, "shotgun", true, NULL);
  qa_q1_actor *doors[3];
  for (int i = 0; i < 3; i++) {
    doors[i] = qa_q1_create(FX.game, "func_door", NULL, NULL);
    doors[i]->spawnflags = 8;
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_origin = true;
    patch.origin = (qa_vec3){i * 10, 0, 0};
    patch.has_bounds = true;
    patch.bounds = (qa_bounds){{0, 0, 0}, {12, 8, 8}};
    qa_q1_set_body(FX.game, doors[i], &patch);
    qa_q1_spawn_entity(FX.game, doors[i]);
  }
  qa_q1_link_doors(FX.game);
  (void)qa_shared_inventory_table_give(FX.inventory, a, "q1:key/gold", 1);
  qa_q1_services_set_time(FX.game, 3);
  FX.event_count = 0;
  touch(doors[1], a.id);
  CHECK(doors[0]->touch.fn != NULL && doors[1]->touch.fn == NULL && doors[2]->touch.fn == NULL); /* L1.touch */
  CHECK(count_sounds("doors/meduse.wav") == 1 && count_sounds("misc/null.wav") == 3);           /* L1.sounds */
  for (int i = 0; i < 3; i++) CHECK(!strcmp(doors[i]->state, "up"));                            /* L1.states */
  world_end();
  /* L2: rerelease buttons travel along the absolute movedir; buttonFire
   * records the activator even when already up. */
  for (int rerelease = 0; rerelease < 2; rerelease++) {
    qa_q1_options options = q1_world_fixture_options();
    options.edition = rerelease ? "rerelease" : "classic";
    world_begin(options);
    const qa_owned_actor p = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
    qa_q1_actor *button = qa_q1_create(FX.game, "func_button", NULL, NULL);
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_angles = true;
    patch.angles = (qa_vec3){0, 135, 0};
    patch.has_bounds = true;
    patch.bounds = (qa_bounds){{0, 0, 0}, {32, 16, 8}};
    qa_q1_set_body(FX.game, button, &patch);
    qa_q1_spawn_entity(FX.game, button);
    CHECK(rerelease ? vec_eq(button->pos2, -21.171571731567383, 21.171571731567383, 0)
                    : vec_eq(button->pos2, -5.171572685241699, 5.171572685241699, 0)); /* L2.pos2 */
    button->state = "top";
    button->use.fn(button->use.ctx, FX.game, button, NULL, &p.id);
    CHECK(button->has_activator && button->activator.slot == 1); /* L2.activator */
    world_end();
  }
  /* L3: the rerelease dopefish achievement on e2m3. */
  world_begin(q1_world_fixture_options());
  const qa_owned_actor b = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(FX.game, b, "shotgun", true, NULL);
  free(FX.game->map_name);
  FX.game->map_name = strdup("e2m3");
  qa_q1_actor *trigger = qa_q1_create(FX.game, "trigger_once", NULL, NULL);
  free(trigger->message);
  trigger->message = strdup("$map_dopefish");
  qa_q1_spawn_entity(FX.game, trigger);
  FX.event_count = 0;
  trigger->use.fn(trigger->use.ctx, FX.game, trigger, NULL, &b.id);
  const qa_q1_event *achievement = last_event("achievement", NULL);
  CHECK(count_events("achievement", NULL) == 1 && achievement != NULL && !strcmp(achievement->id, "ACH_FIND_DOPEFISH") &&
        achievement->player.slot == 1); /* L3 */
  world_end();
  /* L4: GrenadeThink probes from 4 units above to 8 below. */
  world_begin(q1_world_fixture_options());
  FX.game->source_grenade_fix = true;
  qa_q1_actor *g = qa_q1_create(FX.game, "grenade", NULL, NULL);
  set_origin_velocity(g, (qa_vec3){1, 2, 3}, false, QA_Q1_ZERO);
  (void)qa_q1_actor_field_set(g, "oldorigin", "0 0 -200");
  memset(&TRACE_LOG, 0, sizeof TRACE_LOG);
  TRACE_LOG.magic = SENTINEL_MAGIC;
  FX.game->host.trace = logging_trace;
  run(g, "GrenadeThink");
  CHECK(TRACE_LOG.calls == 1 && vec_eq(TRACE_LOG.position, 1, 2, 7) && vec_eq(TRACE_LOG.normal, 1, 2, -5) &&
        !TRACE_LOG.has_hit); /* L4 */
  world_end();
}

/* T1: the classic message table. */
static void test_text(void) {
  CHECK(QA_Q1_CLASSIC_MESSAGE_COUNT == 68); /* T1.count */
  CHECK(!strcmp(qa_q1_classic_message("$qc_suicided_too_much"), "You have suicided too much already.\n"));
  CHECK(!strcmp(qa_q1_classic_message("$qc_out_lava_nails"), "Out of Lava Nails\n"));
  CHECK(!strcmp(qa_q1_classic_message("$qc_shells"), "the shells")); /* T1.shells */
}

int main(void) {
  test_fire_bullets();
  test_rocket_bloody();
  test_grenade_fix();
  test_lightning_particles();
  test_axe();
  test_quad_sound();
  test_best_weapon_counted();
  test_monster_retaliate_and_death();
  test_monster_nightmare_pain();
  test_monster_found_target();
  test_dog_leap_limit();
  test_supply();
  test_mg3_pickups();
  test_backpack();
  test_start_item_programs();
  test_coop_shared_items();
  test_triggers();
  test_movers();
  test_live_options();
  test_text();
  test_late_changes();
  printf("test_q1_sync: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
