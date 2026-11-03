/*
 * Port of key cases from tests/gameplay/q2-missionpacks/weapons.test.ts
 * (native damage causes) plus pure cases derived from the TS sources in
 * src/content/q2/missionpacks (items tables, tag/deathball rules,
 * random item substitution, sphere launch) cross-checked via bun.
 * TypeScript throw cases run in a child process and must abort.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q2/missionpacks.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
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

/* --- weapons.test.ts: native causes --- */
static void test_damage(void) {
  double out = 0;
  qa_q2_native_cause ctf = {"classic", "ctf", 34, 0, false, false};
  CHECK(qa_q2mp_canonical_cause_from_native(ctf, &out) && out == 56);
  qa_q2_native_cause xatrix = {"classic", "xatrix", 34, 0, false, false};
  CHECK(qa_q2mp_canonical_cause_from_native(xatrix, &out) && out == 34);
  qa_q2_native_cause rogue = {"classic", "rogue", 34, 0, false, false};
  CHECK(!qa_q2mp_canonical_cause_from_native(rogue, &out));
  qa_q2_native_cause rr = {"rerelease", NULL, 0, 22, false, true};
  CHECK(qa_q2mp_canonical_cause_from_native(rr, &out) && out == 57);
  for (double id = 0; id <= 58; id++) {
    qa_q2_native_cause native = {"rerelease", NULL, 0, id, true, true};
    double canonical = 0;
    CHECK(qa_q2mp_canonical_cause_from_native(native, &canonical));
    qa_q2_native_cause back = {0};
    qa_q2mp_native_cause_profile profile = {true, NULL, true};
    CHECK(qa_q2mp_native_cause_from_canonical(profile, canonical, &back));
    CHECK(back.id == id && back.friendly_fire && back.no_point_loss && streq(back.edition, "rerelease"));
  }
  /* classic round trip + invalid inputs (from damage.ts guards) */
  qa_q2_native_cause back = {0};
  qa_q2mp_native_cause_profile classic_ctf = {false, "ctf", false};
  CHECK(qa_q2mp_native_cause_from_canonical(classic_ctf, 56, &back) && back.value == 34 && streq(back.game, "ctf"));
  CHECK(!qa_q2mp_native_cause_from_canonical(classic_ctf, 34, &back));
  qa_q2_native_cause bad = {"classic", "base", -1, 0, false, false};
  CHECK(!qa_q2mp_canonical_cause_from_native(bad, &out));
  qa_q2_native_cause frac = {"rerelease", NULL, 0, 1.5, false, false};
  CHECK(!qa_q2mp_canonical_cause_from_native(frac, &out));
  CHECK(QA_Q2MP_MOD_RIPPER == 34 && QA_Q2MP_MOD_TRAP == 39 && QA_Q2MP_MOD_DOPPLE_HUNTER == 55);
}

/* --- weapons/definitions.ts + items.ts tables (bun-verified) --- */
static void test_tables(void) {
  CHECK(qa_q2mp_xatrix_weapon_definition_count == 3);
  CHECK(qa_q2mp_rogue_weapon_definition_count == 6);
  CHECK(streq(qa_q2mp_xatrix_weapon_definitions[1].name, "ionripper"));
  CHECK(streq(qa_q2mp_xatrix_weapon_definitions[1].item, "q2:weapon_boomer"));
  CHECK(qa_q2mp_xatrix_weapon_definitions[2].fires[1] == 8);
  CHECK(qa_q2mp_rogue_weapon_definitions[2].ammo == NULL);
  CHECK(qa_q2mp_rogue_weapon_definitions[5].quantity == 2);
  CHECK(streq(qa_q2mp_mission_weapon_display_name("ionripper"), "Ionripper"));
  CHECK(streq(qa_q2mp_mission_weapon_display_name("tesla"), "Tesla"));
  CHECK(qa_q2mp_mission_weapon_display_name("bogus") == NULL);
  size_t n = 0;
  const qa_q2mp_weapon_inventory_entry *inv = qa_q2mp_mission_weapon_inventory(QA_Q2MP_PACK_XATRIX, &n);
  CHECK(n == 4 && streq(inv[0].item, "q2:ammo_magslug") && inv[0].capacity == 50 && streq(inv[3].item, "q2:weapon_phalanx"));
  inv = qa_q2mp_mission_weapon_inventory(QA_Q2MP_PACK_ROGUE, &n);
  CHECK(n == 9 && streq(inv[4].item, "q2:weapon_proxlauncher") && inv[0].capacity == 200);
  const qa_q2mp_weapon_icon *icons = qa_q2mp_mission_weapon_icons(&n);
  CHECK(n == 13 && streq(icons[0].icon, "a_mslugs") && streq(icons[12].item, "q2:weapon_plasmabeam") && streq(icons[12].icon, "w_heatbeam"));
  qa_q2mp_deathball_rules rules = qa_q2mp_deathball_get_rules(5);
  CHECK(rules.stop_speed == 0 && rules.deathmatch_flags == 917829);
}

/* --- fake game for random-items / tag / deathball / spheres --- */
static double scripted_random;
static double fake_random(void *ctx) { (void)ctx; return scripted_random; }
static void fake_diagnostic(void *ctx, const char *message) { (void)ctx; (void)message; }
static char printed[128];
static int DEATHBALL_HOST;
static int deathball_host_ctx_errors;
/* TS game.host.emit({ kind: "print", actor: null, level: "high", text }). */
static void deathball_emit(void *ctx, qa_q2_presentation_event event) {
  if (ctx != &DEATHBALL_HOST) deathball_host_ctx_errors++;
  if (strcmp(event.kind, "print") == 0 && !event.has_actor && strcmp(event.level, "high") == 0)
    (void)snprintf(printed, sizeof printed, "%s", event.text);
}
static void noop_effect(void *ctx, qa_q2mp_player_effect effect) { (void)ctx; (void)effect; }
static bool false_fn(void *ctx) { (void)ctx; return false; }

static void test_random_items(void) {
  qa_q2_foundation_host host = {0};
  host.random = fake_random;
  qa_q2_game_services game = {0};
  game.host = &host;
  game.options.edition = QA_Q2_EDITION_CLASSIC;
  game.options.mode = QA_Q2_MODE_DEATHMATCH;
  qa_q2_entity entity = {0};
  entity.classname = "item_health_large";
  qa_q2_item_definition item = {0};
  qa_q2mp_random_item_settings settings = {true, false, false, false};
  item.kind = "health"; item.classname = "item_health_large";
  scripted_random = 0.5;
  CHECK(streq(qa_q2mp_random_item(&entity, &item, &game, settings), "item_health"));
  scripted_random = 0.95;
  CHECK(streq(qa_q2mp_random_item(&entity, &item, &game, settings), "item_adrenaline"));
  entity.classname = "item_health_small";
  CHECK(qa_q2mp_random_item(&entity, &item, &game, settings) == NULL);
  item.kind = "armor"; item.classname = "item_armor_body";
  scripted_random = 0.95;
  CHECK(streq(qa_q2mp_random_item(&entity, &item, &game, settings), "item_armor_body"));
  item.kind = "shard"; item.classname = "item_armor_shard";
  CHECK(qa_q2mp_random_item(&entity, &item, &game, settings) == NULL);
  settings.enabled = false;
  item.kind = "health";
  CHECK(qa_q2mp_random_item(&entity, &item, &game, settings) == NULL);
  settings.enabled = true;
  /* classic category pick: ceil(random * len) - 1 */
  item.kind = "weapon"; item.classname = "weapon_railgun"; entity.classname = "weapon_railgun";
  scripted_random = 0;
  CHECK(qa_q2mp_random_item(&entity, &item, &game, settings) == NULL);
  scripted_random = 0.999;
  CHECK(streq(qa_q2mp_random_item(&entity, &item, &game, settings), "weapon_chainfist"));
  /* rerelease small-health swap without rereleaseRandom uses floor(random*2) */
  game.options.edition = QA_Q2_EDITION_RERELEASE;
  item.classname = "item_health_small";
  scripted_random = 0.75;
  CHECK(streq(qa_q2mp_random_item(&entity, &item, &game, settings), "item_armor_shard"));
}

/* --- modes/tag.ts + modes/deathball.ts pure rules --- */
static qa_actor_id actor_of(int64_t slot) { qa_actor_id a = {0}; a.slot = slot; a.generation = 1; return a; }
static qa_q2mp_spawn_point stub_select_spawn(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)ctx; (void)e; (void)g;
  qa_q2mp_spawn_point out = {{0, 0, 0}, {0, 0, 0}};
  return out;
}
static qa_q2_entity *stub_farthest(void *ctx, qa_q2_game_services *g) { (void)ctx; (void)g; return NULL; }
static double scored_actor_slot, scored_amount;
static void record_score(void *ctx, qa_actor_id actor, double amount) {
  (void)ctx;
  scored_actor_slot = (double)actor.slot;
  scored_amount = amount;
}
static void test_tag(void) {
  qa_q2mp_tag_hooks hooks = {0};
  qa_q2_item_hooks item_hooks = {0};
  qa_q2_item_module *items = qa_q2_item_module_create(item_hooks);
  CHECK(items != NULL);
  size_t before = 0, after = 0;
  (void)qa_q2_item_module_list(items, &before);
  hooks.items = items;
  hooks.select_spawn = stub_select_spawn;
  hooks.farthest_spawn = stub_farthest;
  hooks.add_score = record_score;
  qa_q2mp_tag tag;
  qa_q2mp_tag_init(&tag, hooks);
  /* The token registers once into the shared item module (TS hooks.items.register). */
  (void)qa_q2_item_module_list(items, &after);
  qa_q2_inventory_item token;
  CHECK(after == before + 1 && qa_q2_item_module_lookup(items, "dm_tag_token", &token) &&
        streq(token.classname, "dm_tag_token") && streq(token.console_give, "forbidden"));
  tag.owner = actor_of(7); tag.has_owner = true;
  CHECK(qa_q2mp_tag_effects(&tag, actor_of(7)) == (double)0x20000000);
  CHECK(qa_q2mp_tag_effects(&tag, actor_of(8)) == 0);
  CHECK(streq(qa_q2mp_tag_dog_tag(&tag, actor_of(7)), "tag3"));
  CHECK(qa_q2mp_tag_change_damage(&tag, actor_of(8), true, actor_of(9), 100) == 75);
  CHECK(qa_q2mp_tag_change_damage(&tag, actor_of(7), true, actor_of(9), 100) == 100);
  CHECK(qa_q2mp_tag_change_damage(&tag, actor_of(8), false, actor_of(0), 101) == 75);
  qa_q2mp_tag_checkpoint cp = qa_q2mp_tag_capture(&tag);
  CHECK(cp.has_owner && cp.owner.slot == 7 && !cp.has_token);
  qa_q2_item_module_destroy(items);
}
static qa_q2mp_deathball_settings db_settings(void *ctx) {
  (void)ctx;
  qa_q2mp_deathball_settings s = {"male/ctf_r", "male/ctf_b", 2};
  return s;
}
static const char *db_skin(void *ctx, qa_actor_id actor) {
  (void)ctx;
  return actor.slot == 1 ? "male/ctf_r" : "male/ctf_b";
}
static void noop_skin(void *ctx, qa_actor_id actor, const char *skin) { (void)ctx; (void)actor; (void)skin; }
static int ended_levels;
static void record_end(void *ctx) { (void)ctx; ended_levels++; }
static double stub_distance(void *ctx, qa_q2_entity *spot, qa_q2_game_services *g) { (void)ctx; (void)spot; (void)g; return 0; }
static void test_deathball(void) {
  qa_q2_foundation_host host = {0};
  host.ctx = &DEATHBALL_HOST;
  host.diagnostic = fake_diagnostic;
  host.emit = deathball_emit;
  qa_q2_game_services game = {0};
  game.host = &host;
  qa_q2mp_deathball_hooks hooks = {0};
  hooks.settings = db_settings;
  hooks.skin = db_skin;
  hooks.set_skin = noop_skin;
  hooks.add_score = record_score;
  hooks.end_level = record_end;
  hooks.select_spawn = stub_select_spawn;
  hooks.spawn_distance = stub_distance;
  qa_q2mp_deathball ball;
  qa_q2mp_deathball_init(&ball, hooks);
  ball.ball = actor_of(3); ball.has_ball = true;
  CHECK(qa_q2mp_deathball_change_damage(&ball, actor_of(3), true, actor_of(1), 50) == 1);
  CHECK(qa_q2mp_deathball_change_damage(&ball, actor_of(1), true, actor_of(2), 51) == 25);
  CHECK(qa_q2mp_deathball_change_damage(&ball, actor_of(1), true, actor_of(3), 51) == 51);
  CHECK(qa_q2mp_deathball_change_knockback(&ball, actor_of(3), 0, 8, &game) == 70);
  CHECK(qa_q2mp_deathball_change_knockback(&ball, actor_of(3), 10, 1, &game) == 30);
  CHECK(qa_q2mp_deathball_change_knockback(&ball, actor_of(3), 16, 2, &game) == 6);
  CHECK(qa_q2mp_deathball_change_knockback(&ball, actor_of(1), 10, 1, &game) == 10);
  ball.team1_score = 2;
  printed[0] = '\0';
  CHECK(qa_q2mp_deathball_check_rules(&ball, &game) && ended_levels == 1 && streq(printed, "Team 1 Wins.\n"));
  CHECK(deathball_host_ctx_errors == 0);
  double scores[2] = {0, 0};
  qa_q2mp_deathball_scores(&ball, scores);
  CHECK(scores[0] == 2 && scores[1] == 0);
}

/* --- mini fake game over real world tables: spheres launch / owned /
 * disconnect. Every callback checks a sentinel ctx. --- */
static int host_sentinel, game_sentinel;
#define HOST_CTX ((void *)&host_sentinel)
#define GAME_CTX ((void *)&game_sentinel)
#define FAKE_MAX 8
typedef struct sphere_world {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_q2_source_callbacks *source_callbacks;
  qa_q2_entity pool[FAKE_MAX];
  bool alive[FAKE_MAX];
  size_t count;
  qa_q2_entity *list[FAKE_MAX];
  size_t list_count;
  qa_actor_id grounds[FAKE_MAX];
} sphere_world;
static sphere_world W;
static void sphere_world_list(void) {
  W.list_count = 0;
  for (size_t i = 0; i < W.count; i++) if (W.alive[i]) W.list[W.list_count++] = &W.pool[i];
}
static qa_q2_entity *fake_create(void *ctx, const char *classname, const qa_q2_field_pair *values, size_t count) {
  CHECK(ctx == GAME_CTX); (void)values; (void)count;
  size_t index = W.count++;
  qa_owned_actor actor = qa_session_actor_registry_allocate(W.actors, "q2:test", "q2:test");
  qa_body_state body;
  memset(&body, 0, sizeof body);
  qa_shared_body_table_create_body(W.bodies, actor, body);
  qa_q2_spawn_fields fields = {0};
  fields.classname = classname;
  qa_q2_entity_init(&W.pool[index], actor, fields);
  W.alive[index] = true;
  sphere_world_list();
  return &W.pool[index];
}
static qa_q2_entity *fake_lookup(qa_actor_id actor) {
  for (size_t i = 0; i < W.count; i++) if (W.alive[i] && qa_same_actor(W.pool[i].actor.id, actor)) return &W.pool[i];
  return NULL;
}
static void fake_remove(void *ctx, qa_q2_entity *entity) {
  CHECK(ctx == GAME_CTX);
  W.alive[entity - W.pool] = false;
  qa_session_actor_registry_release(W.actors, entity->actor);
  /* A real host's entities snapshot no longer lists removed entities. */
  sphere_world_list();
}
static qa_q2_entity *fake_entity(void *ctx, const qa_actor_id *actor) {
  CHECK(ctx == GAME_CTX);
  return actor == NULL ? NULL : fake_lookup(*actor);
}
static qa_q2_entity *const *fake_entities(void *ctx, size_t *out_count) {
  CHECK(ctx == GAME_CTX);
  *out_count = W.list_count;
  return W.list;
}
static qa_body_state fake_body(void *ctx, qa_owned_actor actor) {
  CHECK(ctx == GAME_CTX);
  return qa_shared_body_table_read(W.bodies, actor.id).value;
}
static void fake_move(void *ctx, qa_q2_entity *entity, qa_q2_body_changes changes, bool link) {
  CHECK(ctx == GAME_CTX); (void)link;
  qa_body_state body = qa_shared_body_table_read(W.bodies, entity->actor.id).value;
  if (changes.has_origin) body.origin = changes.origin;
  if (changes.has_angles) body.angles = changes.angles;
  if (changes.has_velocity) body.velocity = changes.velocity;
  if (changes.has_bounds) body.bounds = changes.bounds;
  if (changes.has_ground) {
    W.grounds[entity - W.pool] = changes.ground;
    body.ground = changes.ground_null ? NULL : &W.grounds[entity - W.pool];
  }
  qa_shared_body_table_write(W.bodies, entity->actor, body);
}
static void fake_noop_entity(void *ctx, qa_q2_entity *entity) { CHECK(ctx == GAME_CTX); (void)entity; }
static void fake_solid(void *ctx, qa_q2_entity *entity, const char *solid) { CHECK(ctx == GAME_CTX); entity->solid = solid; }
static void fake_motion(void *ctx, qa_q2_entity *entity, const char *kind) { CHECK(ctx == GAME_CTX); entity->motion = kind; }
static void fake_schedule(void *ctx, qa_q2_entity *entity, double delay, qa_q2_think think) {
  CHECK(ctx == GAME_CTX); (void)delay;
  entity->think = think;
}
static double host_now(void *ctx) { CHECK(ctx == HOST_CTX); return 10; }
static double host_frame(void *ctx) { CHECK(ctx == HOST_CTX); return 0.1; }
static double host_random(void *ctx) { CHECK(ctx == HOST_CTX); return scripted_random; }
static void host_emit(void *ctx, qa_q2_presentation_event event) { CHECK(ctx == HOST_CTX); (void)event; }
static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state b) {
  (void)c; (void)a;
  return (qa_bounds){qa_add3_double(b.origin, b.bounds.min), qa_add3_double(b.origin, b.bounds.max)};
}
static void link_noop(void *c, qa_linked_body b) { (void)c; (void)b; }
static void unlink_noop(void *c, qa_actor_id a) { (void)c; (void)a; }

static void test_spheres(void) {
  memset(&W, 0, sizeof W);
  qa_identity_owner_create(&W.identities, "q2-missionpacks");
  W.actors = qa_session_actor_registry_create(&W.identities, 64);
  qa_body_link_hooks link_hooks = {0};
  link_hooks.absolute_bounds = link_bounds;
  link_hooks.on_link = link_noop;
  link_hooks.on_unlink = unlink_noop;
  W.bodies = qa_shared_body_table_create(W.actors, link_hooks);
  W.callbacks = qa_actor_callback_table_create(W.actors);
  qa_gameplay_hooks gameplay = {0};
  W.combat = qa_gameplay_authority_create(W.actors, W.callbacks, gameplay);
  W.inventory = qa_shared_inventory_table_create(W.actors);
  W.source_callbacks = qa_q2_source_callbacks_create();
  qa_q2_foundation_host host = {0};
  host.ctx = HOST_CTX;
  host.now = host_now;
  host.frame_seconds = host_frame;
  host.random = host_random;
  host.emit = host_emit;
  host.actors = W.actors; host.bodies = W.bodies; host.callbacks = W.callbacks;
  host.combat = W.combat; host.inventory = W.inventory;
  qa_q2_game_services game = {0};
  game.ctx = GAME_CTX;
  game.host = &host;
  game.options.edition = QA_Q2_EDITION_CLASSIC;
  game.options.mode = QA_Q2_MODE_DEATHMATCH;
  game.create = fake_create;
  game.remove = fake_remove;
  game.entity = fake_entity;
  game.entities = fake_entities;
  game.body = fake_body;
  game.move = fake_move;
  game.solid = fake_solid;
  game.motion = fake_motion;
  game.schedule = fake_schedule;
  game.show = fake_noop_entity;
  game.link = fake_noop_entity;
  game.cancel = fake_noop_entity;
  game.source_callbacks = W.source_callbacks;
  qa_q2_entity *owner = fake_create(GAME_CTX, "player", NULL, 0);
  qa_body_state owner_body;
  memset(&owner_body, 0, sizeof owner_body);
  owner_body.origin = (qa_vec3){10, 20, 30};
  owner_body.bounds.max = (qa_vec3){16, 16, 32};
  owner_body.angles = (qa_vec3){0, 90, 0};
  qa_shared_body_table_write(W.bodies, owner->actor, owner_body);

  qa_q2mp_sphere_hooks hooks = {false, NULL, false_fn, noop_effect};
  qa_q2mp_spheres spheres;
  qa_q2mp_spheres_init(&spheres, NULL, hooks);
  qa_q2_entity *sphere = qa_q2mp_spheres_launch(&spheres, owner, &game, QA_Q2MP_SPHERE_DEFENDER, false);
  CHECK(streq(sphere->classname, "sphere") && streq(sphere->model2, "models/items/shell/tris.md2"));
  CHECK(sphere->spawnflags == 1 && sphere->has_owner && sphere->pain.fn != NULL);
  CHECK(qa_shared_body_table_read(W.bodies, sphere->actor.id).value.origin.z == 62);
  CHECK(qa_q2mp_spheres_owned_sphere(&spheres, owner->actor.id, &game) == sphere);
  qa_q2mp_spheres_disconnect(&spheres, owner->actor.id, &game);
  CHECK(qa_q2mp_spheres_owned_sphere(&spheres, owner->actor.id, &game) == NULL);
  CHECK(qa_q2mp_projectile_mask(&game) == (double)0x6000003);
  qa_q2_source_callbacks_destroy(W.source_callbacks);
  qa_shared_inventory_table_destroy(W.inventory);
  qa_gameplay_authority_destroy(W.combat);
  qa_actor_callback_table_destroy(W.callbacks);
  qa_shared_body_table_destroy(W.bodies);
  qa_session_actor_registry_close(W.actors);
  qa_session_actor_registry_destroy(W.actors);
  qa_identity_owner_destroy(&W.identities);
}

/* --- weapons/player.ts: unknown fire callback throws (aborts) --- */
static void wemit(void *ctx, qa_q2w_event event) { (void)ctx; (void)event; }
static void wnoise(void *ctx, qa_actor_id actor, qa_vec3 origin, bool secondary) {
  (void)ctx; (void)actor; (void)origin; (void)secondary;
}
static void wdodge(void *ctx, qa_actor_id monster, qa_actor_id attacker, double eta, qa_trace_result trace) {
  (void)ctx; (void)monster; (void)attacker; (void)eta; (void)trace;
}
static bool wtarget(void *ctx, const qa_actor_id *attacker, qa_actor_id target) {
  (void)ctx; (void)attacker; (void)target;
  return true;
}
static struct qa_q2m_context *no_monster(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return NULL; }
static void test_weapons_abort(void) {
  qa_q2w_hooks hooks = {0};
  hooks.emit = wemit;
  hooks.noise = wnoise;
  hooks.dodge = wdodge;
  hooks.can_target = wtarget;
  qa_q2w_weapons weapons;
  CHECK(qa_q2w_weapons_init(&weapons, hooks));
  qa_q2mp_projectile_hooks phooks = {0};
  phooks.base = &weapons.base;
  phooks.monster = no_monster;
  phooks.player_effect = noop_effect;
  qa_q2mp_projectiles projectiles;
  qa_q2mp_projectiles_init(&projectiles, phooks);
  qa_q2mp_weapons mission;
  qa_q2mp_weapons_init(&mission, &projectiles);
  qa_q2mp_weapons_register(&mission, &weapons, QA_Q2MP_PACK_XATRIX, QA_Q2_EDITION_CLASSIC);
  CHECK(weapons.extension_count == 3);
  const qa_q2w_definition *ion = qa_q2w_weapons_definition(&weapons, "ionripper");
  CHECK(ion != NULL && ion->quantity == 2);
  /* TS fire() default arm: throw new Error("No mission-pack fire callback...") */
  qa_q2w_definition bogus = *ion;
  bogus.name = "bogus";
  qa_q2w_state state;
  qa_q2w_state_init_default(&state);
  qa_q2w_context context = {0};
  context.definition = &bogus;
  context.state = &state;
  qa_q2w_extension ext = weapons.extensions[0];
  CHECK_ABORTS(ext.fire(ext.ctx, &context, &weapons));
  qa_q2w_weapons_destroy(&weapons);
}

int main(void) {
  test_damage();
  test_tables();
  test_random_items();
  test_tag();
  test_deathball();
  test_spheres();
  test_weapons_abort();
  if (failures != 0) {
    fprintf(stderr, "%d/%d checks failed\n", failures, checks);
    return 1;
  }
  printf("q2 missionpacks: %d checks passed\n", checks);
  return 0;
}
