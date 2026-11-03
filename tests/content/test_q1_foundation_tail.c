/* Checks for src/content/q1/foundation: foreignQ3Pickup, regular armor grants,
 * originalPickups routing, damage source effect folding and actor callback
 * binding, over the real shared world tables (q1_world_fixture.h). Every
 * host leaf asserts the ctx it was registered with. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)
#define CHECK_ABORTS(...)                  \
  do {                                     \
    checks++;                              \
    pid_t pid = fork();                    \
    if (pid == 0) {                        \
      (void)prctl(PR_SET_DUMPABLE, 0);     \
      freopen("/dev/null", "w", stderr);   \
      __VA_ARGS__;                         \
      _exit(0);                            \
    }                                      \
    int status = 0;                        \
    waitpid(pid, &status, 0);              \
    if (!WIFSIGNALED(status)) {            \
      failures++;                          \
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__); \
    }                                      \
  } while (0)

#include "qa/content/composition/q1/q3_pickups.h"
#include "q1_world_fixture.h"
#include "qa/content/q1/foundation/pickups.h"

typedef struct host_state {
  bool bad_ctx;
  double gravity;
  int registered;
} host_state;
static host_state H;
static q1_world_fixture FX;

static void set_gravity(void *ctx, qa_actor_id actor, double scale) {
  (void)actor; if (q1_world_fixture_of(ctx) != &FX) H.bad_ctx = true; H.gravity = scale;
}
static void register_entity(void *ctx, qa_q1_actor *entity, qa_q1_services *services) {
  (void)entity; if (q1_world_fixture_of(ctx) != &FX || services != FX.game) H.bad_ctx = true; H.registered++;
}

/* A fresh world; the previous test's world is destroyed first. */
static qa_q1_services *make_game(void) {
  if (FX.game != NULL) q1_world_fixture_destroy(&FX);
  qa_q1_options options;
  memset(&options, 0, sizeof options);
  options.combat_provider = "q1:combat";
  CHECK(q1_world_fixture_create(&FX, options));
  FX.random_value = 0;
  FX.game->host.set_gravity = set_gravity;
  /* TS moved foreignQ3Pickup to composition/q1/q3-pickups.ts: the Q1 foundation
   * reaches Q3-authored items only through the hook the composition installs. */
  qa_comp_q1_register_q3_pickup_hook(FX.game);
  return FX.game;
}
/* attachExisting over an actor another provider admitted to the shared tables. */
static int64_t next_slot = 10;
static qa_q1_actor *spawn(qa_q1_services *game, const char *classname) {
  const qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(FX.actors, "q1:test", next_slot++, "q1:test/actor");
  qa_body_state body;
  memset(&body, 0, sizeof body);
  body.bounds = QA_Q1_POINT;
  qa_shared_body_table_create_body(FX.bodies, actor, body);
  qa_combat_state combat;
  memset(&combat, 0, sizeof combat);
  combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  combat.armor.powered.kind = "none";
  combat.mass = 100;
  qa_gameplay_authority_create_state(FX.combat, actor, combat, NULL, NULL);
  return qa_q1_attach_existing(game, actor, classname, NULL, NULL);
}
static qa_q1_player_state *make_player(qa_q1_services *game) {
  const qa_owned_actor actor = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 50);
  double max_health = 100;
  return qa_q1_attach_player(game, actor, "shotgun", true, &max_health);
}
static int source_armor_writes;
static qa_combat_state source_read(void *ctx) {
  (void)ctx;
  qa_combat_state state;
  memset(&state, 0, sizeof state);
  state.health = 100;
  state.armor.regular.kind = QA_REGULAR_ARMOR_SOURCE;
  state.armor.regular.points = 10;
  state.armor.powered.kind = "none";
  state.mass = 100;
  state.can_take_damage = true;
  return state;
}
static void source_write_health(void *ctx, double health) { (void)ctx; (void)health; }
static void source_write_armor(void *ctx, qa_armor_state armor) { (void)ctx; (void)armor; source_armor_writes++; }
static void source_write_traits(void *ctx, qa_combat_traits traits) { (void)ctx; (void)traits; }
static qa_q1_player_state *make_source_armor_player(qa_q1_services *game) {
  const qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(FX.actors, "q3:character", 9, "q3:sarge");
  qa_body_state body;
  memset(&body, 0, sizeof body);
  body.bounds = QA_Q1_PLAYER_BOUNDS;
  qa_shared_body_table_create_body(FX.bodies, actor, body);
  qa_combat_state_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.read = source_read;
  binding.write_health = source_write_health;
  binding.write_armor = source_write_armor;
  binding.write_traits = source_write_traits;
  qa_gameplay_authority_bind(FX.combat, actor, binding);
  qa_shared_inventory_table_create_entries(FX.inventory, actor, NULL, 0);
  return qa_q1_attach_player(game, actor, "shotgun", true, NULL);
}
static double health_of(qa_q1_player_state *player) { return qa_q1_health(FX.game, player->actor.id); }
static void set_health_of(qa_q1_player_state *player, double health) {
  qa_gameplay_authority_set_health(FX.combat, player->actor, health);
}
static qa_regular_armor_state armor_of(qa_q1_player_state *player) {
  const qa_combat_state_result combat = qa_gameplay_authority_read(FX.combat, player->actor.id);
  CHECK(combat.has_value);
  return combat.value.armor.regular;
}
static bool same_armor(qa_regular_armor_state a, qa_regular_armor_state b) {
  return a.kind == b.kind && a.points == b.points && a.absorption == b.absorption && a.protection == b.protection &&
         ((a.item == NULL && b.item == NULL) || (a.item != NULL && b.item != NULL && !strcmp(a.item, b.item)));
}

static void test_foreign_health_and_armor(void) {
  memset(&H, 0, sizeof H);
  qa_q1_services *game = make_game();
  qa_q1_player_state *player = make_player(game);

  /* item_health_small uses the 200 cap and the authored quantity (5). */
  qa_q1_actor *small = spawn(game, "item_health_small");
  CHECK(qa_q1_give_pickup(game, small, player->actor.id));
  CHECK(health_of(player) == 55);
  /* entity.count overrides the authored quantity. */
  qa_q1_actor *counted = spawn(game, "item_health_small");
  counted->count = 200;
  CHECK(qa_q1_give_pickup(game, counted, player->actor.id));
  CHECK(health_of(player) == 200);
  qa_q1_actor *refused = spawn(game, "item_health_small");
  CHECK(qa_q1_give_pickup(game, refused, player->actor.id));
  CHECK(health_of(player) == 200);
  CHECK(refused->solid != NULL && !strcmp(refused->solid, "trigger"));

  /* Shard adds up to 200 with Q3 protection. */
  qa_q1_actor *shard = spawn(game, "item_armor_shard");
  shard->count = 30;
  CHECK(qa_q1_give_pickup(game, shard, player->actor.id));
  CHECK(armor_of(player).kind == QA_REGULAR_ARMOR_Q3 && armor_of(player).points == 30 && armor_of(player).protection == 2.0 / 3.0);

  /* Native armor compares effective protection and grants the q1 record. */
  qa_q1_actor *green = spawn(game, "item_armor1");
  CHECK(qa_q1_give_pickup(game, green, player->actor.id));
  CHECK(armor_of(player).kind == QA_REGULAR_ARMOR_Q1 && armor_of(player).points == 100 && armor_of(player).absorption == 0.3);
  CHECK(!strcmp(armor_of(player).item, "q1:item_armor1"));
  const qa_regular_armor_state granted = armor_of(player);
  qa_q1_actor *second = spawn(game, "item_armor1");
  CHECK(qa_q1_give_pickup(game, second, player->actor.id));
  CHECK(same_armor(armor_of(player), granted));
  /* A source-owned armor formula (a provider's bound combat state) refuses Q1 armor. */
  qa_q1_player_state *sourced = make_source_armor_player(game);
  qa_q1_actor *red = spawn(game, "item_armorInv");
  CHECK(qa_q1_give_pickup(game, red, sourced->actor.id));
  CHECK(source_armor_writes == 0 && armor_of(sourced).kind == QA_REGULAR_ARMOR_SOURCE);
  CHECK(!H.bad_ctx);
}

static void test_foreign_supply_powerups_holdables(void) {
  memset(&H, 0, sizeof H);
  qa_q1_services *game = make_game();
  qa_q1_player_state *player = make_player(game);

  /* Ammo lands in the first Q1 destination of the supply profile. */
  const double rockets = qa_shared_inventory_table_count(FX.inventory, player->actor.id, "q1:ammo/rockets");
  qa_q1_actor *ammo = spawn(game, "ammo_rockets");
  CHECK(qa_q1_give_pickup(game, ammo, player->actor.id));
  CHECK(qa_shared_inventory_table_count(FX.inventory, player->actor.id, "q1:ammo/rockets") == rockets + 5);
  CHECK(qa_shared_inventory_table_count(FX.inventory, player->actor.id, "q3:ammo/rocketlauncher") == 0);

  /* Railgun maps to the supernailgun through the same profile. */
  qa_q1_actor *rail = spawn(game, "weapon_railgun");
  CHECK(qa_q1_give_pickup(game, rail, player->actor.id));
  CHECK(qa_shared_inventory_table_count(FX.inventory, player->actor.id, "q1:weapon/supernailgun") == 1);

  /* The observation reports the Q3 offer while the pickup is live. */
  qa_q1_actor *spawned = spawn(game, "weapon_railgun");
  CHECK(qa_q1_spawn_pickup(game, spawned));
  CHECK(spawned->model != NULL && !strcmp(spawned->model, "models/weapons2/railgun/railgun.md3"));
  spawned->solid = "trigger";
  qa_pickup_supply_observation observation;
  CHECK(qa_q1_observe_supply(game, spawned->actor.id, player->actor.id, &observation));
  CHECK(observation.offer.kind == QA_PICKUP_SUPPLY_WEAPON && !strcmp(observation.offer.item, "q3:weapon/railgun"));
  CHECK(!strcmp(observation.availability_kind, "ready") && observation.eligible);

  /* Powerups map onto Q1 kinds. */
  qa_q1_actor *quad = spawn(game, "item_quad");
  CHECK(qa_q1_give_pickup(game, quad, player->actor.id));
  CHECK(qa_q1_player_powerup_get(player, "quad") == 30);
  qa_q1_actor *flight = spawn(game, "item_flight");
  CHECK(qa_q1_give_pickup(game, flight, player->actor.id));
  CHECK(qa_q1_player_powerup_get(player, "q3:flight") == 30);
  CHECK(H.gravity == 0.1f || H.gravity == 0.1);

  /* Medkit refills to the player's maximum. */
  set_health_of(player, 40);
  qa_q1_actor *medkit = spawn(game, "holdable_medkit");
  CHECK(qa_q1_give_pickup(game, medkit, player->actor.id));
  CHECK(health_of(player) == 100);

  /* Team objectives stay in place: refused, and still solid. */
  qa_q1_actor *flag = spawn(game, "team_CTF_redflag");
  CHECK(qa_q1_give_pickup(game, flag, player->actor.id));
  CHECK(!strcmp(flag->solid, "trigger"));

  /* A classname outside both tables has no definition. */
  qa_q1_actor *unknown = spawn(game, "misc_unknown");
  CHECK(!qa_q1_give_pickup(game, unknown, player->actor.id));

  /* The teleporter holdable moves the player onto a spawn spot. */
  qa_q1_actor *spot = spawn(game, "info_player_start");
  qa_q1_body patch = qa_q1_get_body(game, spot);
  patch.origin = (qa_vec3){7, 8, 9};
  qa_q1_set_body_patch(game, spot, &patch, true, false, false, false, false);
  FX.random_value = 0.5;
  qa_q1_actor *teleporter = spawn(game, "holdable_teleporter");
  CHECK(qa_q1_give_pickup(game, teleporter, player->actor.id));
  const qa_body_state_result moved = qa_shared_body_table_read(FX.bodies, player->actor.id);
  CHECK(moved.value.origin.x == 7 && moved.value.origin.y == 8 && moved.value.origin.z == 9);
  CHECK(moved.value.ground == NULL);
  CHECK(!H.bad_ctx);
}

typedef struct touch_probe { int sentinel; int touches; qa_original_pickup_offer offer; bool had_resource; qa_pickup_resource resource; bool accept; bool taken; } touch_probe;
static touch_probe P;
static qa_original_pickup_outcome admission_touch(void *ctx, qa_original_pickup_offer offer, qa_original_pickup_continuation continuation) {
  if (ctx != &P.sentinel) H.bad_ctx = true;
  P.touches++;
  P.offer = offer;
  P.had_resource = offer.default_resource != NULL;
  if (P.had_resource) P.resource = *offer.default_resource;
  P.taken = continuation.original(continuation.ctx);
  continuation.complete(continuation.ctx, P.taken);
  return QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED;
}
static void test_original_pickup_routing(void) {
  memset(&H, 0, sizeof H);
  memset(&P, 0, sizeof P);
  qa_q1_services *game = make_game();
  static qa_original_pickup_admission admission;
  admission.ctx = &P.sentinel;
  admission.touch = admission_touch;
  game->host.original_pickups = &admission;
  qa_q1_player_state *player = make_player(game);
  qa_q1_actor *armor = spawn(game, "item_armor2");
  armor->count = 3;
  CHECK(qa_q1_give_pickup(game, armor, player->actor.id));
  CHECK(P.touches == 1 && P.taken);
  CHECK(P.had_resource && P.resource.kind == QA_PICKUP_RESOURCE_PROTECTION && P.resource.channel == QA_PROTECTION_REGULAR);
  CHECK(!strcmp(P.offer.item, "q1:item_armor2"));
  CHECK(P.offer.count.is_override && P.offer.count.amount == 3);
  CHECK(!P.offer.dropped && P.offer.time.kind == QA_SOURCE_TIME_SECONDS);
  CHECK(armor_of(player).kind == QA_REGULAR_ARMOR_Q1 && armor_of(player).points == 150);
  CHECK(!strcmp(armor->solid, "none"));
  CHECK(!H.bad_ctx);
}

/* Each table's ctx is its own sentinel, so a mixed-up fold fails. */
static int E1, E2;
static int fold_calls;
static qa_damage_request request_value;
static qa_combat_state combat_value;
static bool e1_before(void *ctx, qa_damage_request r, double d, qa_combat_state t, const qa_combat_state *a, qa_damage_preparation *out) {
  (void)r; (void)t; (void)a; CHECK(ctx == &E1); fold_calls++; out->is_continue = true; out->amount = d * 2; return true;
}
static bool e2_before(void *ctx, qa_damage_request r, double d, qa_combat_state t, const qa_combat_state *a, qa_damage_preparation *out) {
  (void)r; (void)t; (void)a; CHECK(ctx == &E2); fold_calls++; out->is_continue = true; out->amount = d + 1; return true;
}
static bool e2_cancel(void *ctx, qa_damage_request r, double d, qa_combat_state t, const qa_combat_state *a, qa_damage_preparation *out) {
  (void)r; (void)d; (void)t; (void)a; CHECK(ctx == &E2); out->is_continue = false; out->amount = 0; return true;
}
static bool e1_undefined(void *ctx, qa_damage_request r, double d, qa_combat_state t, const qa_combat_state *a, qa_damage_preparation *out) {
  (void)r; (void)d; (void)t; (void)a; (void)out; CHECK(ctx == &E1); return false;
}
static bool e1_deny(void *ctx, qa_damage_request r, qa_combat_state t, const qa_combat_state *a) {
  (void)r; (void)t; (void)a; CHECK(ctx == &E1); return false;
}
static double e1_after(void *ctx, qa_damage_request r, double take, qa_combat_state t, const qa_combat_state *a) {
  (void)r; (void)t; (void)a; CHECK(ctx == &E1); return take * 3;
}
static double e2_after(void *ctx, qa_damage_request r, double take, qa_combat_state t, const qa_combat_state *a) {
  (void)r; (void)t; (void)a; CHECK(ctx == &E2); return take + 1;
}
static qa_q1_lethal_health e1_lethal(void *ctx, qa_damage_request r, double health, qa_combat_state t, const qa_combat_state *a) {
  (void)r; (void)t; (void)a; CHECK(ctx == &E1);
  qa_q1_lethal_health result = {true, health + 5, "none"};
  return result;
}
static qa_q1_lethal_health e2_lethal(void *ctx, qa_damage_request r, double health, qa_combat_state t, const qa_combat_state *a) {
  (void)r; (void)health; (void)t; (void)a; (void)ctx; fold_calls = -1000;
  qa_q1_lethal_health result = {true, 0, "death"};
  return result;
}
static void test_damage_effects(void) {
  memset(&H, 0, sizeof H);
  qa_q1_services *game = make_game();
  qa_q1_damage_source_effects first, second;
  memset(&first, 0, sizeof first);
  memset(&second, 0, sizeof second);
  first.ctx = &E1;
  second.ctx = &E2;
  first.before_quad = e1_before;
  second.before_quad = e2_before;
  first.after_quad = e1_undefined;
  second.after_quad = e2_cancel;
  first.protection_applies = e1_deny;
  first.after_armor = e1_after;
  second.after_armor = e2_after;
  first.lethal_health = e1_lethal;
  second.lethal_health = e2_lethal;
  qa_q1_register_damage_source_effects(game, "first", first);
  qa_q1_register_damage_source_effects(game, "second", second);
  CHECK_ABORTS(qa_q1_register_damage_source_effects(game, "first", first));

  qa_q1_damage_source_effects folded = qa_q1_damage_source_effects_of(game);
  CHECK(folded.ctx == game);
  qa_damage_preparation out;
  CHECK(folded.before_quad(folded.ctx, request_value, 10, combat_value, NULL, &out));
  CHECK(out.is_continue && out.amount == 21 && fold_calls == 2); /* registration order: 10*2+1 */
  CHECK(folded.after_quad(folded.ctx, request_value, 10, combat_value, NULL, &out));
  CHECK(!out.is_continue);
  CHECK(!folded.protection_applies(folded.ctx, request_value, combat_value, NULL));
  CHECK(folded.armor_allowed(folded.ctx, request_value, 1, combat_value, NULL)); /* absent = true */
  CHECK(folded.before_health(folded.ctx, request_value, 1, combat_value, NULL));
  CHECK(folded.after_armor(folded.ctx, request_value, 2, combat_value, NULL) == 7);
  qa_q1_lethal_health lethal = folded.lethal_health(folded.ctx, request_value, -4, combat_value, NULL);
  CHECK(lethal.health == 1 && !strcmp(lethal.reaction, "none"));
  CHECK(fold_calls == 2); /* "none" stops the fold before the second table */
  CHECK(!H.bad_ctx);
}

static int think_runs, die_runs, touch_runs;
static qa_actor_id last_attacker;
static void on_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)game; (void)entity; CHECK(ctx == &E1); think_runs++; }
static void on_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)game; (void)entity; CHECK(ctx == &E2); die_runs++; if (attacker) last_attacker = *attacker;
}
static void on_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)game; (void)entity; (void)other; (void)surface; CHECK(ctx == &E1); CHECK(normal != NULL && normal->z == 1); touch_runs++;
}
static void test_actor_callbacks(void) {
  memset(&H, 0, sizeof H);
  qa_q1_services *game = make_game();
  game->host.register_entity = register_entity;
  qa_q1_actor *entity = spawn(game, "monster_test");
  CHECK(H.registered == 1);
  entity->think.fn = on_think;
  entity->think.ctx = &E1;
  entity->next_think = 4;
  qa_frame_context frame;
  memset(&frame, 0, sizeof frame);
  frame.time.kind = QA_SOURCE_TIME_MILLISECONDS;
  frame.time.value = 2500;
  frame.elapsed.kind = QA_SOURCE_TIME_SECONDS;
  frame.elapsed.value = 0.25;
  CHECK(qa_actor_callback_table_think(FX.callbacks, entity->actor, frame));
  CHECK(think_runs == 1 && game->time == 2.5 && game->frame_seconds == 0.25);
  CHECK(entity->think.fn == NULL && entity->next_think == -1);

  entity->touch.fn = on_touch;
  entity->touch.ctx = &E1;
  qa_plane plane = {{0, 0, 1}, 0};
  qa_touch_contact contact;
  memset(&contact, 0, sizeof contact);
  contact.self = entity->actor;
  contact.other = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100).id;
  contact.plane = &plane;
  (void)qa_actor_callback_table_touch(FX.callbacks, contact);
  CHECK(touch_runs == 1);

  /* Killed clamps health and records the attacker on walking monsters. */
  entity->monster = calloc(1, sizeof *entity->monster);
  entity->movement = "step";
  qa_gameplay_authority_set_health(FX.combat, entity->actor, -300);
  entity->die.fn = on_die;
  entity->die.ctx = &E2;
  const qa_actor_id attacker = q1_world_fixture_admit_player(&FX, QA_Q1_ZERO, 100).id;
  qa_death_reaction death;
  memset(&death, 0, sizeof death);
  death.pain.self = entity->actor;
  death.pain.attacker = &attacker;
  (void)qa_actor_callback_table_die(FX.callbacks, death);
  CHECK(qa_q1_health(game, entity->actor.id) == -99);
  CHECK(entity->monster->has_enemy && qa_same_actor(entity->monster->enemy, attacker));
  CHECK(die_runs == 1 && qa_same_actor(last_attacker, attacker));
  CHECK(!H.bad_ctx);
}

int main(void) {
  test_foreign_health_and_armor();
  test_foreign_supply_powerups_holdables();
  test_original_pickup_routing();
  test_damage_effects();
  test_actor_callbacks();
  CHECK(FX.ctx_errors == 0);
  q1_world_fixture_destroy(&FX);
  printf("test_q1_foundation_tail: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
