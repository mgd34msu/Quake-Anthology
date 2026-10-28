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
#include "qa/content/q1/foundation/monsters.h"
#include "qa/content/q1/foundation/precache.h"
#include "qa/content/q1/foundation/shambler_damage.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/content/q1/foundation/text.h"
#include "qa/content/q1/foundation/weapon_names.h"
#include "qa/content/q1/foundation/weapons.h"

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
  CHECK(QA_Q1_CLASSIC_MESSAGE_COUNT == 39);
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
  qa_q1_services game;
  qa_q1_host host;
  memset(&host, 0, sizeof host);
  qa_q1_options options;
  memset(&options, 0, sizeof options);
  options.edition = "classic";
  options.precache_id1 = true;
  CHECK(qa_q1_services_init(&game, host, options));
  const double ordinal0 = 0, ordinal1 = 1, ordinal2 = 2, ordinal3 = 3;
  qa_q1_actor *world = qa_q1_create(&game, "worldspawn", NULL, &ordinal0);
  qa_q1_spawn_entity(&game, world);
  CHECK(game.world == world);
  size_t sound_count = 0, model_count = 0;
  (void)qa_q1_precache_sounds(game.precaches, &sound_count);
  (void)qa_q1_precache_models(game.precaches, &model_count);
  CHECK(sound_count == 64); /* slot 0 + 63 worldspawn sounds */
  CHECK(model_count == 27); /* slot 0 + 26 worldspawn models */
  qa_q1_actor *shells = qa_q1_create(&game, "item_shells", NULL, &ordinal1);
  qa_q1_spawn_entity(&game, shells);
  CHECK(streq(shells->model, "maps/b_shell0.bsp"));
  CHECK(streq(shells->solid, "none"));
  CHECK(shells->touch.fn != NULL && streq(shells->touch.name, "item_touch"));
  CHECK(streq(shells->think.name, "PlaceItem"));
  qa_q1_actor *army = qa_q1_create(&game, "monster_army", NULL, &ordinal2);
  qa_q1_spawn_entity(&game, army);
  CHECK(game.total_monsters == 1);
  CHECK(army->monster != NULL && streq(army->monster->species, "army"));
  CHECK(streq(army->model, "progs/soldier.mdl") && army->max_health == 30);
  CHECK(qa_q1_spawn_known(&game, "worldspawn", NULL));
  CHECK(qa_q1_spawn_known(&game, "item_shells", NULL));
  CHECK(!qa_q1_spawn_known(&game, "monster_ogre", NULL));
  CHECK(!qa_q1_spawn_known(&game, "weapon_axe", NULL));
  CHECK_ABORTS(qa_q1_spawn_entity(&game, qa_q1_create(&game, "monster_ogre", NULL, &ordinal3)));
  qa_q1_foundation_checkpoint checkpoint;
  CHECK(qa_q1_services_capture(&game, &checkpoint));
  CHECK(streq(checkpoint.format, "q1-foundation") && checkpoint.version == 5);
  CHECK(checkpoint.entity_count == 3);
  bool saw_shells = false;
  for (size_t i = 0; i < checkpoint.entity_count; i++)
    if (streq(checkpoint.entities[i].classname, "item_shells")) {
      saw_shells = true;
      CHECK(streq(checkpoint.entities[i].callbacks.touch, "item_touch"));
      CHECK(streq(checkpoint.entities[i].state.model, "maps/b_shell0.bsp"));
    }
  CHECK(saw_shells);
  qa_q1_checkpoint_free(&checkpoint);
  qa_q1_services_free(&game);
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
  qa_q1_services game;
  qa_q1_host host;
  memset(&host, 0, sizeof host);
  host.weapon_behavior = &port;
  qa_q1_options options;
  memset(&options, 0, sizeof options);
  options.edition = "classic";
  CHECK(qa_q1_services_init(&game, host, options));
  const double ordinal = 5;
  qa_q1_actor *rocket = qa_q1_create(&game, "rocket", NULL, &ordinal);
  qa_actor_id shooter;
  memset(&shooter, 0, sizeof shooter);
  shooter.slot = 42;
  /* Non-player shooter: port never consulted (TS isPlayer gate). */
  qa_q1_launch_projectile_behavior(&game, rocket, shooter, "rocketlauncher", "rocket");
  CHECK(seen.weapon == NULL);
  qa_owned_actor owner;
  memset(&owner, 0, sizeof owner);
  owner.id = shooter;
  owner.owner = QA_Q1_PROVIDER;
  CHECK(qa_q1_attach_player(&game, owner, "rocketlauncher", false, NULL) != NULL);
  qa_q1_launch_projectile_behavior(&game, rocket, shooter, "rocketlauncher", "rocket");
  CHECK(seen.weapon != NULL && streq(seen.weapon, "q1:weapon/rocketlauncher"));
  CHECK(seen.role != NULL && streq(seen.role, "rocket"));
  CHECK(rocket->body.origin.x == 1 && rocket->body.velocity.z == 6 && rocket->body.angles.y == 8);
  port.launch = fake_launch_none;
  rocket->body.origin.x = 99;
  qa_q1_launch_projectile_behavior(&game, rocket, shooter, "rocketlauncher", "rocket");
  CHECK(rocket->body.origin.x == 99); /* null update leaves body alone */
  qa_q1_services_free(&game);
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
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("%d checks\n", checks);
  return 0;
}
