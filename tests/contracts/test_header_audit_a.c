/*
 * Header-audit slice A: standalone runtime ports that previously
 * existed only as headers (precache registry, shambler damage,
 * weapon names, held weapons, classic text, callback registry).
 * Expectations are taken from the TypeScript sources in
 * src/content/q1/foundation/. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q1/foundation/callbacks.h"
#include "qa/content/q1/foundation/checkpoint.h"
#include "qa/content/q1/foundation/held_weapons.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q1/foundation/precache.h"
#include "qa/content/q1/foundation/shambler_damage.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/content/q1/foundation/text.h"
#include "qa/content/q1/foundation/weapon_names.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/base/travel.h"
#include "../content/q1_world_fixture.h"
#include "qa/content/q2/missionpacks/monsters/rogue_jumpers.h"
#include "qa/content/q2/missionpacks/monsters/rogue_variants.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static bool streq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

static void test_precache(void) {
  qa_q1_precache_registry *registry = qa_q1_precache_registry_create();
  CHECK(registry != NULL);
  CHECK(streq(qa_q1_precache_phase(registry), "loading"));
  size_t count = 0;
  const char *const *models = qa_q1_precache_models(registry, &count);
  CHECK(count == 1 && streq(models[0], ""));
  qa_q1_precache_begin_world(registry, "maps/e1m1.bsp", 2);
  models = qa_q1_precache_models(registry, &count);
  CHECK(count == 4);
  CHECK(streq(models[1], "maps/e1m1.bsp") && streq(models[2], "*1") && streq(models[3], "*2"));
  CHECK(streq(qa_q1_precache_model(registry, "progs/player.mdl"), "progs/player.mdl"));
  (void)qa_q1_precache_model(registry, "progs/player.mdl"); /* dedup */
  models = qa_q1_precache_models(registry, &count);
  CHECK(count == 5);
  CHECK(streq(qa_q1_precache_sound(registry, "weapons/ax1.wav"), "weapons/ax1.wav"));
  size_t sound_count = 0;
  (void)qa_q1_precache_sounds(registry, &sound_count);
  CHECK(sound_count == 2);
  CHECK_ABORTS(qa_q1_precache_begin_world(registry, "maps/e1m2.bsp", 0));
  CHECK_ABORTS((void)qa_q1_precache_model(registry, " leading.wav"));
  CHECK_ABORTS((void)qa_q1_precache_model(registry, ""));
  qa_q1_precache_freeze(registry);
  CHECK(streq(qa_q1_precache_phase(registry), "frozen"));
  CHECK_ABORTS((void)qa_q1_precache_sound(registry, "misc/talk.wav"));

  /* Restore into a fresh registry. */
  qa_q1_precache_tables tables;
  tables.phase = qa_q1_precache_phase(registry);
  tables.models = qa_q1_precache_models(registry, &tables.model_count);
  tables.sounds = qa_q1_precache_sounds(registry, &tables.sound_count);
  qa_q1_precache_registry *restored = qa_q1_precache_registry_create();
  qa_q1_precache_restore(restored, tables);
  CHECK(streq(qa_q1_precache_phase(restored), "frozen"));
  const char *const *restored_models = qa_q1_precache_models(restored, &count);
  CHECK(count == 5 && streq(restored_models[4], "progs/player.mdl"));
  CHECK_ABORTS(qa_q1_precache_restore(restored, tables));
  qa_q1_precache_registry_destroy(restored);
  qa_q1_precache_registry_destroy(registry);

  qa_q1_precache_registry *fresh = qa_q1_precache_registry_create();
  CHECK_ABORTS(qa_q1_precache_begin_world(fresh, "maps/x.bsp", -1));
  CHECK_ABORTS(qa_q1_precache_begin_world(fresh, "maps/x.bsp", 1.5));
  qa_q1_precache_registry_destroy(fresh);
  qa_q1_precache_registry *bad = qa_q1_precache_registry_create();
  const char *bad_models[2] = {"nonempty", "progs/a.mdl"};
  const char *ok_sounds[1] = {""};
  const qa_q1_precache_tables bad_tables = {"loading", bad_models, 2, ok_sounds, 1};
  CHECK_ABORTS(qa_q1_precache_restore(bad, bad_tables));
  const char *dup_models[3] = {"", "progs/a.mdl", "progs/a.mdl"};
  const qa_q1_precache_tables dup_tables = {"loading", dup_models, 3, ok_sounds, 1};
  CHECK_ABORTS(qa_q1_precache_restore(bad, dup_tables));
  qa_q1_precache_registry_destroy(bad);
}

static void test_shambler(void) {
  qa_damage_request request;
  memset(&request, 0, sizeof request);
  request.amount = 101;
  request.knockback = 40;
  request.delivery = "direct";
  double amount = 0, knockback = 0;
  /* Not a shambler, and native Q1 / environment causes: no halving. */
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q3;
  request.attack.cause.means_of_death = 6;
  CHECK(!qa_q1_foreign_shambler_damage("ogre", &request, &amount, &knockback));
  CHECK(!qa_q1_foreign_shambler_damage(NULL, &request, &amount, &knockback));
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q1;
  CHECK(!qa_q1_foreign_shambler_damage("shambler", &request, &amount, &knockback));
  request.attack.cause.kind = QA_ATTACK_CAUSE_ENVIRONMENT;
  CHECK(!qa_q1_foreign_shambler_damage("shambler", &request, &amount, &knockback));
  /* Foreign non-rocket direct hits do not halve. */
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q3;
  request.attack.cause.means_of_death = 1;
  CHECK(!qa_q1_foreign_shambler_damage("shambler", &request, &amount, &knockback));
  /* Radius damage and foreign rockets (Q3 MoD 6, Q2 MoD 8) halve. */
  request.delivery = "radius";
  CHECK(qa_q1_foreign_shambler_damage("shambler", &request, &amount, &knockback));
  CHECK(amount == 50.5 && knockback == 20);
  request.delivery = "direct";
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q2;
  request.attack.cause.means_of_death = 8;
  CHECK(qa_q1_foreign_shambler_damage("shambler", &request, &amount, &knockback));
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q3;
  request.attack.cause.means_of_death = 6;
  CHECK(qa_q1_foreign_shambler_damage("shambler", &request, &amount, &knockback));
  /* Math.fround staging: 0.1 * 0.5 in binary32. */
  request.amount = 0.1;
  CHECK(qa_q1_foreign_shambler_damage("shambler", &request, &amount, &knockback));
  CHECK(amount == (double)(float)(0.1 * 0.5));
}

static void test_weapon_names(void) {
  char buffer[128];
  CHECK(streq(qa_q1_weapon_display_name("axe", buffer, sizeof buffer), "Axe"));
  CHECK(streq(qa_q1_weapon_display_name("supershotgun", buffer, sizeof buffer), "Double-barrelled Shotgun"));
  CHECK(streq(qa_q1_weapon_display_name("lightning", buffer, sizeof buffer), "Thunderbolt"));
  CHECK(streq(qa_q1_weapon_display_name("hipnotic:laser", buffer, sizeof buffer), "Laser Cannon"));
  CHECK(streq(qa_q1_weapon_display_name("hipnotic:mjolnir", buffer, sizeof buffer), "Mjolnir"));
  CHECK(streq(qa_q1_weapon_display_name("hipnotic:proximity", buffer, sizeof buffer), "Proximity Gun"));
  CHECK(streq(qa_q1_weapon_display_name("rogue:lava-nailgun", buffer, sizeof buffer), "Lava Nailgun"));
  CHECK(streq(qa_q1_weapon_display_name("rogue:multi-grenade", buffer, sizeof buffer), "Multi Grenade"));
  CHECK(streq(qa_q1_weapon_display_name("ctf:grapple", buffer, sizeof buffer), "Grapple"));
  CHECK(streq(qa_q1_weapon_display_name("custom:plasma_rifle", buffer, sizeof buffer), "Plasma Rifle"));
}

static void test_held_weapons(void) {
  const qa_held_weapon_model *axe = qa_q1_held_weapon("progs/v_axe.mdl");
  const qa_held_weapon_model *gun = qa_q1_held_weapon("progs/v_shot.mdl");
  CHECK(axe != NULL && gun != NULL && axe != gun);
  CHECK(axe->reference_frame == 17 && gun->reference_frame == 12);
  CHECK(streq(axe->path, "progs/player.mdl") && streq(gun->path, "progs/player.mdl"));
  CHECK(axe->part != NULL && axe->part->vertices.count == 16);
  CHECK(gun->part != NULL && gun->part->vertices.count == 38);
  CHECK(gun->part->vertices.values[0] == 40 && gun->part->vertices.values[37] == 202);
  CHECK(axe->part->digests.count == 2);
  CHECK(streq(axe->part->digests.values[0],
              "sha256:10cecfe08d312ff17c63529e280b976c50018f683f541cb03b2048c7a681ebf9"));
  CHECK(gun->grip.origin.x == 3.192499796549479 && gun->grip.scale.z == 1);
  CHECK(qa_q1_held_weapon("progs/player.mdl") == NULL);
  CHECK(qa_q1_held_weapon("progs/v_axe.bsp") == NULL);
  CHECK(qa_q1_held_weapon("v_axe.mdl") == NULL);
  CHECK(qa_q1_held_weapon("progs/v_light.mdl") == gun);
}

static void test_text(void) {
  CHECK(streq(qa_q1_classic_message("$qc_found_secret"), "You found a secret area!"));
  CHECK(streq(qa_q1_classic_message("$qc_need_gold_keycard"), "You need the gold keycard"));
  CHECK(streq(qa_q1_classic_message("$qc_need_silver_runekey"), "You need the silver runekey"));
  CHECK(qa_q1_classic_message("$qc_nope") == NULL);
  CHECK(QA_Q1_CLASSIC_MESSAGE_COUNT == 68); /* TS classicQ1Messages.size under bun */
  char out[512];
  const qa_q1_msg_arg name_arg = {false, 0, "Bob"};
  CHECK(streq(qa_q1_classic_text("$qc_has_token", &name_arg, 1, out, sizeof out), "Bob has the tag token!\n"));
  const qa_q1_msg_arg number_arg = {true, 3, NULL};
  CHECK(streq(qa_q1_classic_text("$qc_enemy_killed_bonus", &number_arg, 1, out, sizeof out),
              "Enemy flag carrier killed: 3 bonus frags\n"));
  CHECK(streq(qa_q1_classic_text("$qc_has_token", NULL, 0, out, sizeof out), "{0} has the tag token!\n"));
  const qa_q1_msg_arg key_arg = {false, 0, "$qc_got_quad"};
  CHECK(streq(qa_q1_classic_text("$qc_has_token", &key_arg, 1, out, sizeof out),
              "You got the Quad Damage\n has the tag token!\n"));
  CHECK(streq(qa_q1_entity_string("a\\nb", out, sizeof out), "a\nb"));
  CHECK(streq(qa_q1_entity_string("a\\xb", out, sizeof out), "a\\b"));
  CHECK(streq(qa_q1_entity_string("end\\", out, sizeof out), "end\\"));
  CHECK(streq(qa_q1_entity_string("\\\\n", out, sizeof out), "\\n"));
}

static int action_calls = 0;
static void counting_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; (void)game; (void)entity;
  action_calls++;
}

static void test_callbacks(void) {
  qa_q1_callback_registry *registry = qa_q1_callback_registry_create(NULL);
  CHECK(registry != NULL);
  qa_q1_callback_handlers handlers = {0};
  handlers.action = counting_action;
  qa_q1_callback_registry_register(registry, "my_action", handlers);
  CHECK_ABORTS(qa_q1_callback_registry_register(registry, "my_action", handlers));
  qa_q1_actor entity;
  memset(&entity, 0, sizeof entity);
  const qa_q1_bound_action bound = qa_q1_callback_registry_action(registry, &entity, "my_action");
  CHECK(streq(bound.name, "my_action") && bound.fn == counting_action);
  CHECK(streq(qa_q1_action_name(bound), "my_action"));
  qa_q1_call_action(NULL, &entity, bound);
  CHECK(action_calls == 1);
  const qa_q1_bound_action empty = {0};
  CHECK(qa_q1_action_name(empty) == NULL);
  qa_q1_call_action(NULL, &entity, empty);
  CHECK(action_calls == 1);
  const qa_q1_bound_action unnamed = {NULL, NULL, counting_action};
  CHECK_ABORTS((void)qa_q1_action_name(unnamed));
  CHECK_ABORTS((void)qa_q1_callback_registry_action(registry, &entity, "missing"));
  CHECK_ABORTS((void)qa_q1_callback_registry_use(registry, &entity, "my_action"));
  CHECK_ABORTS((void)qa_q1_callback_registry_touch(registry, &entity, "my_action"));
  qa_q1_callback_registry_destroy(registry);
}

static void test_weapons_and_monsters(void) {
  CHECK(streq(qa_q1_weapon_model("axe"), "progs/v_axe.mdl"));
  CHECK(streq(qa_q1_weapon_model("shotgun"), "progs/v_shot.mdl"));
  CHECK(streq(qa_q1_weapon_model("supershotgun"), "progs/v_shot2.mdl"));
  CHECK(streq(qa_q1_weapon_model("nailgun"), "progs/v_nail.mdl"));
  CHECK(streq(qa_q1_weapon_model("supernailgun"), "progs/v_nail2.mdl"));
  CHECK(streq(qa_q1_weapon_model("grenadelauncher"), "progs/v_rock.mdl"));
  CHECK(streq(qa_q1_weapon_model("rocketlauncher"), "progs/v_rock2.mdl"));
  CHECK(streq(qa_q1_weapon_model("lightning"), "progs/v_light.mdl"));
  CHECK_ABORTS((void)qa_q1_weapon_model("hipnotic:laser"));
  CHECK(qa_q1_path_end_time(0) == 999999);
  CHECK(qa_q1_path_end_time(1.5) == (double)(float)((float)1.5 + 999999));
}

static void test_services_integration(void) {
  q1_world_fixture fx;
  qa_q1_options options;
  memset(&options, 0, sizeof options);
  options.edition = "classic";
  options.precache_id1 = true;
  options.combat_provider = "q1:combat";
  CHECK(q1_world_fixture_create(&fx, options));
  qa_q1_services *game = fx.game;
  const double ordinal0 = 0, ordinal1 = 1, ordinal2 = 2, ordinal3 = 3;
  qa_q1_actor *world = qa_q1_create(game, "worldspawn", NULL, &ordinal0);
  qa_q1_spawn_entity(game, world);
  CHECK(game->world == world);
  size_t sound_count = 0, model_count = 0;
  (void)qa_q1_precache_sounds(game->precaches, &sound_count);
  (void)qa_q1_precache_models(game->precaches, &model_count);
  CHECK(sound_count == 64); /* slot 0 + 63 worldspawn sounds */
  CHECK(model_count == 27); /* slot 0 + 26 worldspawn models */
  qa_q1_actor *shells = qa_q1_create(game, "item_shells", NULL, &ordinal1);
  qa_q1_spawn_entity(game, shells);
  CHECK(streq(shells->model, "maps/b_shell0.bsp"));
  CHECK(streq(shells->solid, "none"));
  CHECK(shells->touch.fn != NULL && streq(shells->touch.name, "item_touch"));
  CHECK(streq(shells->think.name, "PlaceItem"));
  /* Current TS: the grunt is registered by the base/ordinary monster modules,
   * not by the bare foundation (foundation/monsters.ts was removed). */
  CHECK(!qa_q1_spawn_known(game, "monster_army", NULL));
  CHECK_ABORTS(qa_q1_spawn_entity(game, qa_q1_create(game, "monster_army", NULL, &ordinal2)));
  CHECK(game->total_monsters == 0);
  CHECK(qa_q1_spawn_known(game, "worldspawn", NULL));
  CHECK(qa_q1_spawn_known(game, "item_shells", NULL));
  CHECK(!qa_q1_spawn_known(game, "monster_ogre", NULL));
  CHECK(!qa_q1_spawn_known(game, "weapon_axe", NULL));
  CHECK_ABORTS(qa_q1_spawn_entity(game, qa_q1_create(game, "monster_ogre", NULL, &ordinal3)));
  qa_q1_foundation_checkpoint checkpoint;
  CHECK(qa_q1_services_capture(game, &checkpoint));
  CHECK(streq(checkpoint.format, "q1-foundation") && checkpoint.version == 6);
  CHECK(checkpoint.entity_count == 2);
  bool saw_shells = false;
  for (size_t i = 0; i < checkpoint.entity_count; i++)
    if (streq(checkpoint.entities[i].classname, "item_shells")) {
      saw_shells = true;
      CHECK(streq(checkpoint.entities[i].callbacks.touch, "item_touch"));
      CHECK(streq(checkpoint.entities[i].state.model, "maps/b_shell0.bsp"));
    }
  CHECK(saw_shells);
  qa_q1_checkpoint_free(&checkpoint);
  CHECK(fx.ctx_errors == 0);
  q1_world_fixture_destroy(&fx);
}

static qa_weapon_trajectory_update_result fake_launch(void *ctx, qa_weapon_behavior_launch input) {
  qa_weapon_behavior_launch *seen = ctx;
  *seen = input;
  qa_weapon_trajectory_update_result result;
  memset(&result, 0, sizeof result);
  result.has_value = true;
  result.value.origin = (qa_vec3){1, 2, 3};
  result.value.velocity = (qa_vec3){4, 5, 6};
  result.value.angles = (qa_vec3){7, 8, 9};
  return result;
}
static qa_weapon_trajectory_update_result fake_launch_none(void *ctx, qa_weapon_behavior_launch input) {
  (void)ctx; (void)input;
  qa_weapon_trajectory_update_result result;
  memset(&result, 0, sizeof result);
  return result;
}

static void test_launch_projectile_behavior(void) {
  qa_weapon_behavior_launch seen;
  memset(&seen, 0, sizeof seen);
  qa_weapon_behavior_projectile_port port;
  memset(&port, 0, sizeof port);
  port.ctx = &seen;
  port.launch = fake_launch;
  q1_world_fixture fx;
  qa_q1_options options = q1_world_fixture_options();
  options.edition = "classic";
  CHECK(q1_world_fixture_create(&fx, options));
  qa_q1_services *game = fx.game;
  game->host.weapon_behavior = &port;
  const double ordinal = 5;
  qa_q1_actor *rocket = qa_q1_create(game, "rocket", NULL, &ordinal);
  /* A shooter of another provider, not yet a Q1 player. */
  const qa_owned_actor owner = qa_session_actor_registry_allocate_at_source(fx.actors, "q3:character", 1, "q3:sarge");
  const qa_actor_id shooter = owner.id;
  /* Non-player shooter: port never consulted (TS isPlayer gate). */
  qa_q1_launch_projectile_behavior(game, rocket, shooter, "rocketlauncher", "rocket");
  CHECK(seen.weapon == NULL);
  CHECK(qa_q1_attach_player(game, owner, "rocketlauncher", false, NULL) != NULL);
  qa_q1_launch_projectile_behavior(game, rocket, shooter, "rocketlauncher", "rocket");
  CHECK(seen.weapon != NULL && streq(seen.weapon, "q1:weapon/rocketlauncher"));
  CHECK(seen.role != NULL && streq(seen.role, "rocket"));
  qa_q1_body body = qa_q1_get_body(game, rocket);
  CHECK(body.origin.x == 1 && body.velocity.z == 6 && body.angles.y == 8);
  port.launch = fake_launch_none;
  qa_q1_set_origin(game, rocket, (qa_vec3){99, body.origin.y, body.origin.z});
  qa_q1_launch_projectile_behavior(game, rocket, shooter, "rocketlauncher", "rocket");
  CHECK(qa_q1_get_body(game, rocket).origin.x == 99); /* null update leaves body alone */
  q1_world_fixture_destroy(&fx);
}

/* --- undef-q1-q2-content slice: services attack/frame/physics --- */
static int frame_pusher_calls;
static double frame_pusher_elapsed;
static bool frame_step_enabled;
static int frame_powerup_calls;
static const char *frame_last_powerup;
static double frame_last_powerup_expires;
static int frame_gravity_calls;
static const char *frame_contents = "empty";
static qa_q1_trace frame_trace(void *ctx, qa_q1_trace_request request) {
  (void)ctx;
  qa_q1_trace trace;
  memset(&trace, 0, sizeof trace);
  trace.fraction = 1;
  trace.end = request.end;
  return trace;
}
static qa_q1_contents frame_contents_fn(void *ctx, qa_vec3 point) {
  (void)ctx; (void)point;
  return frame_contents;
}
/* TS pusherServices(game): the pusher is read with no velocity and its
 * think far away, so stepQ1Pusher only advances its local time by the
 * elapsed interval, which the write records. */
static qa_q1_services *frame_pusher_game;
static bool frame_pusher_read(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out) {
  (void)ctx;
  qa_q1_actor *entity = qa_q1_get_entity(frame_pusher_game, &actor);
  if (entity == NULL) return false;
  memset(out, 0, sizeof *out);
  out->actor = entity->actor;
  out->solid = "bsp";
  out->local_time_seconds = 0;
  out->next_think_seconds = 1000;
  return true;
}
static void frame_pusher_write(void *ctx, qa_q1_physics_entity entity) {
  (void)ctx;
  frame_pusher_elapsed = entity.local_time_seconds;
}
static double frame_n_store(void *ctx, double value) { (void)ctx; return (float)value; }
static double frame_n_add(void *ctx, double a, double b) { (void)ctx; return (float)(a + b); }
static double frame_n_subtract(void *ctx, double a, double b) { (void)ctx; return (float)(a - b); }
static double frame_n_multiply(void *ctx, double a, double b) { (void)ctx; return (float)(a * b); }
static double frame_n_divide(void *ctx, double a, double b) { (void)ctx; return (float)(a / b); }
static qa_q1_pusher_services frame_pusher(void *ctx, qa_q1_services *game) {
  (void)q1_world_fixture_of(ctx);
  frame_pusher_calls++;
  frame_pusher_game = game;
  qa_q1_pusher_services services;
  memset(&services, 0, sizeof services);
  services.numeric.store = frame_n_store;
  services.numeric.add = frame_n_add;
  services.numeric.subtract = frame_n_subtract;
  services.numeric.multiply = frame_n_multiply;
  services.numeric.divide = frame_n_divide;
  services.read = frame_pusher_read;
  services.write = frame_pusher_write;
  return services;
}
static void frame_powerup(void *ctx, qa_owned_actor actor, qa_q1_powerup powerup, double expires) {
  (void)ctx; (void)actor;
  frame_powerup_calls++;
  frame_last_powerup = powerup;
  frame_last_powerup_expires = expires;
}
static void frame_gravity(void *ctx, qa_actor_id actor, double scale) {
  (void)ctx; (void)actor;
  frame_gravity_calls++;
  CHECK(scale == 1);
}
static double frame_random(void *ctx) { (void)ctx; return 0.5; }
static qa_weapon_trajectory_update_result frame_step(void *ctx, qa_owned_actor projectile,
                                                     qa_body_state body, double seconds) {
  (void)ctx; (void)projectile; (void)body; (void)seconds;
  qa_weapon_trajectory_update_result result;
  memset(&result, 0, sizeof result);
  result.has_value = frame_step_enabled;
  result.value.origin = (qa_vec3){9, 9, 9};
  return result;
}

static void test_services_frame_physics(void) {
  qa_weapon_behavior_projectile_port port;
  memset(&port, 0, sizeof port);
  port.step = frame_step;
  q1_world_fixture fx;
  qa_q1_options options = q1_world_fixture_options();
  options.edition = "classic";
  options.gravity = 800;
  options.has_max_clients = false;
  options.max_clients = 0;
  CHECK(q1_world_fixture_create(&fx, options));
  qa_q1_services *game = fx.game;
  game->host.contents = frame_contents_fn;
  game->host.pusher_services = frame_pusher;
  game->host.powerup = frame_powerup;
  game->host.set_gravity = frame_gravity;
  game->host.random = frame_random;
  game->host.trace = frame_trace;
  game->host.weapon_behavior = &port;
  const double o0 = 0, o1 = 1, o2 = 2, o3 = 3, o4 = 4;
  qa_q1_actor *world = qa_q1_create(game, "worldspawn", NULL, &o0);
  game->world = world;
  qa_q1_actor *body_ent = qa_q1_create(game, "player", NULL, &o1);
  qa_gameplay_authority_set_health(fx.combat, body_ent->actor, 100);
  qa_q1_actor_set_damageable(body_ent, true);
  qa_q1_set_bounds(game, body_ent, (qa_bounds){{-16, -16, -24}, {16, 16, 32}});
  qa_q1_player_state *player = qa_q1_attach_player(game, body_ent->actor, "axe", true, NULL);
  CHECK(player != NULL);
  game->next_dynamic_slot = 100; /* map ordinals occupy the low slots */
  qa_owned_actor bogus;
  memset(&bogus, 0, sizeof bogus);
  bogus.id.slot = 99;
  qa_vec3 zero3 = {0, 0, 0};
  CHECK_ABORTS((void)qa_q1_attack(game, bogus, zero3, 1, 0));
  CHECK_ABORTS((void)qa_q1_weapon_input(game, bogus, true, zero3, 1, 0));
  /* attack: axe fire advances attack/hostile timers from TS time. */
  CHECK(qa_q1_attack(game, body_ent->actor, (qa_vec3){0, 90, 0}, 10, 0));
  CHECK(game->time == 10);
  CHECK(player->view_angles.y == 90);
  CHECK(player->attack_finished == (double)(float)(10 + 0.5));
  CHECK(player->hostile_until == 11);
  /* weapon_input release clears continuous firing state. */
  player->continuous_firing = true;
  player->weapon_frame = 7;
  player->weapon_animation_at = 3;
  CHECK(!qa_q1_weapon_input(game, body_ent->actor, false, (qa_vec3){1, 2, 3}, 11, 1));
  CHECK(!player->continuous_firing && player->weapon_frame == 0 && player->weapon_animation_at == -1);
  CHECK(!player->attack_held && player->water_level == 1 && game->time == 11);
  /* player_frame mega rot: -1 health/sec above max, stops at max. */
  qa_gameplay_authority_set_health(fx.combat, body_ent->actor, 150);
  player->mega_rot_at = 12;
  qa_q1_player_frame(game, body_ent->actor, 12, NULL);
  CHECK(qa_q1_health(game, body_ent->actor.id) == 149 && player->mega_rot_at == 13);
  qa_gameplay_authority_set_health(fx.combat, body_ent->actor, 100);
  qa_q1_player_frame(game, body_ent->actor, 13, NULL);
  CHECK(player->mega_rot_at == -1);
  /* powerup expiry deletes + notifies host; q3:flight restores gravity. */
  qa_q1_player_powerup_set(player, "quad", 13);
  qa_q1_player_powerup_set(player, "q3:flight", 13);
  qa_q1_player_powerup_set(player, "suit", 999);
  frame_powerup_calls = 0;
  frame_gravity_calls = 0;
  qa_q1_player_frame(game, body_ent->actor, 14, NULL);
  CHECK(qa_q1_player_powerup_get(player, "quad") == 0);
  CHECK(qa_q1_player_powerup_get(player, "suit") == 999);
  CHECK(frame_powerup_calls == 2 && frame_last_powerup_expires == 0);
  CHECK(frame_gravity_calls == 1);
  /* q3:regen heals 5 * frameSeconds. */
  qa_q1_player_powerup_delete(player, "suit");
  qa_q1_player_powerup_set(player, "q3:regen", 999);
  game->frame_seconds = 1;
  qa_q1_player_frame(game, body_ent->actor, 15, NULL);
  CHECK(qa_q1_health(game, body_ent->actor.id) == 105);
  /* Drowning at waterLevel 3 once air runs out. */
  qa_q1_player_powerup_delete(player, "q3:regen");
  player->air_finished = 10;
  player->drown_at = 0;
  player->drown_damage = 2;
  const double three = 3;
  qa_q1_player_frame(game, body_ent->actor, 20, &three);
  CHECK(player->drown_damage == 4 && qa_q1_health(game, body_ent->actor.id) == 101 && player->drown_at == 21);
  /* Lava hazard: 10 * waterLevel, 0.2s re-arm without suit. */
  frame_contents = "lava";
  player->hazard_at = 0;
  const double two = 2;
  qa_q1_player_frame(game, body_ent->actor, 22, &two);
  CHECK(qa_q1_health(game, body_ent->actor.id) == 81);
  CHECK(fabs(player->hazard_at - 22.2) < 1e-9);
  frame_contents = "empty";
  /* Toss physics: gravity fround staging, open trace keeps flying. */
  qa_q1_actor *toss = qa_q1_create(game, "grenade", NULL, &o2);
  toss->movement = "toss";
  qa_q1_body velocity_patch;
  memset(&velocity_patch, 0, sizeof velocity_patch);
  velocity_patch.velocity = (qa_vec3){100, 0, 100};
  qa_q1_set_body_patch(game, toss, &velocity_patch, false, false, true, false, false);
  qa_q1_services_physics_entity(game, toss->actor, 30, 0.5);
  CHECK(qa_q1_get_body(game, toss).velocity.z == -300);
  CHECK(qa_q1_get_body(game, toss).origin.x == 50 && qa_q1_get_body(game, toss).origin.z == -150);
  /* Push physics runs stepQ1Pusher over host.pusherServices(game). */
  qa_q1_actor *push = qa_q1_create(game, "func_door", NULL, &o3);
  push->movement = "push";
  frame_pusher_calls = 0;
  qa_q1_services_physics_entity(game, push->actor, 31, 0.25);
  CHECK(frame_pusher_calls == 1 && frame_pusher_elapsed == 0.25);
  /* Gib physics aborts outside the rerelease profile. */
  qa_q1_actor *gib = qa_q1_create(game, "gib", NULL, &o4);
  gib->movement = "gib";
  CHECK_ABORTS(qa_q1_services_physics_entity(game, gib->actor, 32, 0.1));
  gib->movement = "none"; /* keep the physics_step below off the gib path */
  /* Weapon-behavior step update is applied before movement dispatch. */
  const double o5 = 5;
  qa_q1_actor *behaved = qa_q1_create(game, "rocket", NULL, &o5);
  frame_step_enabled = true;
  qa_q1_services_physics_entity(game, behaved->actor, 33, 0.1);
  frame_step_enabled = false;
  CHECK(qa_q1_get_body(game, behaved).origin.x == 9 && qa_q1_get_body(game, behaved).origin.z == 9);
  /* physics_step steps entities then frames players. */
  frame_pusher_calls = 0;
  qa_q1_physics_step(game, 40, 0.1);
  CHECK(game->time == 40 && frame_pusher_calls == 1);
  CHECK(fx.ctx_errors == 0);
  q1_world_fixture_destroy(&fx);
}

/* --- undef slice: mission-pack travel/obituary (now the base functions the
 * mission packs call directly: qa_q1_new_travel, qa_q1_capture_travel,
 * qa_q1_admit_travel, qa_q1_obituary_compute) over the real game --- */
static double obituary_roll;
static int obituary_sentinel;
static double obituary_random(void *ctx) { CHECK(ctx == &obituary_sentinel); return obituary_roll; }
static void travel_configure(q1_world_fixture *fx, qa_owned_actor actor, qa_item_id item, double count, double capacity) {
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = item; entry.count = count; entry.capacity = capacity;
  qa_shared_inventory_table_configure(fx->inventory, actor, entry, NULL, NULL);
}

static void test_travel_obituary_adapters(void) {
  q1_world_fixture fx;
  qa_q1_options options = q1_world_fixture_options();
  options.edition = "rerelease";
  options.skill = 3;
  CHECK(q1_world_fixture_create(&fx, options));
  qa_q1_services *game = fx.game;
  /* newQ1Travel: rerelease skill 3 non-deathmatch health is 50. */
  qa_q1_travel_state fresh = qa_q1_new_travel(&game->options);
  CHECK(fresh.health == 50 && fresh.max_health == 50);
  CHECK(streq(fresh.weapon, "shotgun"));
  CHECK(fresh.inventory_count == 14);
  CHECK(streq(fresh.inventory[1].item, "q1:weapon/shotgun") && fresh.inventory[1].count == 1);
  CHECK(streq(fresh.inventory[8].item, "q1:ammo/shells") && fresh.inventory[8].count == 25);
  /* captureQ1Travel: floors shells at 25, zeroes temporary keys. */
  const qa_owned_actor actor = q1_world_fixture_admit_player(&fx, (qa_vec3){0, 0, 0}, 80);
  qa_q1_player_state *player = qa_q1_attach_player(game, actor, "nailgun", false, NULL);
  player->max_health = 100;
  travel_configure(&fx, actor, "q1:ammo/shells", 3, 100);
  travel_configure(&fx, actor, "q1:key/gold", 1, 1);
  const double max_health = 100;
  const qa_q1_travel_policy policy = {true, true};
  qa_q1_travel_state captured = qa_q1_capture_travel(game, actor, "nailgun", &max_health, policy);
  CHECK(captured.health == 80 && captured.max_health == 100);
  CHECK(captured.inventory_count == 2);
  CHECK(captured.inventory[0].count == 25 && captured.inventory[1].count == 0);
  /* admitQ1Travel: sets health, configures entries, selects weapon. */
  qa_gameplay_authority_set_health(fx.combat, actor, 1);
  travel_configure(&fx, actor, qa_q1_weapon_item("nailgun"), 1, 1);
  travel_configure(&fx, actor, "q1:ammo/nails", 10, 200);
  qa_q1_admit_travel(game, actor, &captured);
  CHECK(qa_gameplay_authority_read(fx.combat, actor.id).value.health == 80);
  CHECK(qa_shared_inventory_table_count(fx.inventory, actor.id, "q1:ammo/shells") == 25);
  CHECK(qa_shared_inventory_table_count(fx.inventory, actor.id, "q1:key/gold") == 0);
  CHECK(streq(player->weapon, "nailgun"));
  qa_q1_travel_state_destroy(&fresh);
  qa_q1_travel_state_destroy(&captured);
  CHECK(fx.ctx_errors == 0);
  q1_world_fixture_destroy(&fx);
  /* q1Obituary: shotgun kill credits the attacker (rerelease key). */
  qa_q1_obituary_input input;
  memset(&input, 0, sizeof input);
  input.edition = "rerelease";
  input.death_type = "";
  input.random = obituary_random;
  input.random_ctx = &obituary_sentinel;
  obituary_roll = 0.5;
  input.victim.actor.slot = 1;
  input.victim.name = "Victim";
  input.victim.classname = "player";
  input.victim.is_player = true;
  input.victim.water_type = "empty";
  input.victim.kill_string = "";
  qa_q1_obituary_actor attacker;
  memset(&attacker, 0, sizeof attacker);
  attacker.actor.slot = 2;
  attacker.name = "Killer";
  attacker.classname = "player";
  attacker.is_player = true;
  attacker.water_type = "empty";
  attacker.weapon = "shotgun";
  attacker.kill_string = "";
  input.attacker = &attacker;
  qa_q1_obituary out = qa_q1_obituary_compute(&input);
  CHECK(out.has_message && streq(out.text, "$qc_death_sg"));
  CHECK(out.has_score && out.score_delta == 1 && out.score_actor.slot == 2);
  /* attacker null falls through to the environment death. */
  input.attacker = NULL;
  out = qa_q1_obituary_compute(&input);
  CHECK(out.has_message && streq(out.text, "$qc_death_died"));
  CHECK(out.has_score && out.score_delta == -1 && out.score_actor.slot == 1);
  /* Self lightning discharge in water (classic text path has message). */
  input.attacker = &attacker;
  attacker.actor.slot = 1;
  input.victim.weapon = "lightning";
  input.victim.water_level = 2;
  input.victim.water_type = "water";
  out = qa_q1_obituary_compute(&input);
  CHECK(out.has_message && streq(out.text, "$qc_discharge_water"));
}

/* --- undef slice: rogue monster base definitions --- */
static void test_rogue_base_definitions(void) {
  qa_q2m_definition jorg = qa_q2mpm_base_create_jorg_definition(NULL, NULL);
  CHECK(streq(jorg.classname, "monster_jorg") && streq(jorg.kind, "jorg"));
  CHECK(jorg.health == 3000 && jorg.mass == 1000);
  CHECK(streq(jorg.initial_move, "jorg_move_stand"));
  CHECK(jorg.callback_count == 12 && streq(jorg.callbacks[8].name, "MakronToss"));
  qa_q2m_definition mutant = qa_q2mpm_base_create_mutant_definition(NULL, NULL, "rogue");
  CHECK(streq(mutant.classname, "monster_mutant") && mutant.health == 300);
  CHECK(mutant.has_source_callbacks && mutant.source_callbacks.touch_count == 1);
  CHECK(streq(mutant.source_callbacks.touch[0].name, "rogue_mutant_jump_touch"));
  qa_q2m_definition classic = qa_q2mpm_base_create_mutant_definition(NULL, NULL, NULL);
  CHECK(streq(classic.source_callbacks.touch[0].name, "classic_mutant_jump_touch"));
  /* withMakronSpawnCallbacks appends the two think callbacks. */
  qa_q2m_definition makron = qa_q2mpm_base_makron_definition;
  CHECK(streq(makron.classname, "monster_makron") && makron.health == 3000);
  size_t thinks = makron.has_source_callbacks ? makron.source_callbacks.think_count : 0;
  qa_q2_named_think think_storage[8];
  qa_q2_named_use use_storage[8];
  qa_q2m_definition wrapped;
  qa_q2mpm_with_makron_spawn_callbacks(&wrapped, &makron, NULL, think_storage, use_storage);
  CHECK(wrapped.has_source_callbacks && wrapped.source_callbacks.think_count == thinks + 2);
  CHECK(streq(wrapped.source_callbacks.think[thinks].name, "q2:base/MakronSpawn"));
  CHECK(streq(wrapped.source_callbacks.think[thinks + 1].name, "q2:base/makron_torso_think"));
}

int main(void) {
  test_precache();
  test_shambler();
  test_weapon_names();
  test_held_weapons();
  test_text();
  test_callbacks();
  test_weapons_and_monsters();
  test_services_integration();
  test_launch_projectile_behavior();
  test_services_frame_physics();
  test_travel_obituary_adapters();
  test_rogue_base_definitions();
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("%d checks\n", checks);
  return 0;
}
