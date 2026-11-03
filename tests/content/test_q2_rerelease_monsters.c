/*
 * Port of key cases from tests/gameplay/q2-rerelease/monsters/
 * monsters.test.ts for src/content/q2/rerelease/monsters
 * (definitions, tables, shared helpers, and handler behavior
 * against a stubbed host). The full Jorg/Makron, medic revive and
 * reinforcement-spawn integration flows need the Q2Foundation
 * runtime; their definition-level contracts are covered here.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/rerelease/monsters.h"

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

/* ---- world: real shared tables, stubbed source services ---- */
#define WORLD_ACTORS 8
static struct {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_q2_source_callbacks *source_callbacks;
  qa_owned_actor owned[WORLD_ACTORS]; /* index = registry slot */
  bool live;
} R;
static qa_owned_actor make_owned(int64_t slot) {
  if (slot < 0 || slot >= WORLD_ACTORS || R.owned[slot].id.slot != slot) {
    fprintf(stderr, "no actor at slot %lld\n", (long long)slot);
    abort();
  }
  return R.owned[slot];
}
static qa_actor_id make_id(int64_t slot) { return make_owned(slot).id; }
typedef struct stub {
  double now;
  double randoms[64]; size_t random_count, random_at;
  qa_q2_entity *entities[8];
  qa_actor_id ray_actor; bool has_ray;
  double trace_fraction;
  double contents;
  char last_move[96]; int set_move_count;
  char last_sound[128]; double last_sound_channel; int sound_count;
  int muzzle_count; double last_flash;
  int effect_count; char last_effect[64];
  int loop_count; char last_loop[16];
  int remove_count, cancel_count, schedule_count;
  int diagnostic_count;
  double damage_amount; int damage_count;
  int bullet_count; double bullet_damage;
  int blaster_count; double blaster_effects, blaster_damage;
  int rocket_count, hit_count, grenade_count, rail_count, bfg_count;
  bool hit_result;
  double hit_damage, rail_damage;
  qa_q2_entity created[4]; int created_at;
  qa_actor_id nearby_ids[4]; size_t nearby_count;
  qa_actor_id player_ids[1];
  qa_q2_presentation_event last_beam; int beam_count;
  qa_q2_rerelease_random random_stream;
} stub;
static stub S;
/* Sentinels: every host/game/weapon callback checks the ctx it was
 * registered with. */
static char HOST_CTX, GAME_CTX, WEAPONS_CTX;
static int ctx_errors;
#define EXPECT_CTX(ctx, sentinel) do { if ((ctx) != (void *)&(sentinel)) ctx_errors++; } while (0)
static qa_q2_foundation_host host;
static qa_q2_game_services game;
static qa_q2_entity self_entity, enemy_entity;
static qa_q2m_state state;
static qa_q2m_context context;
static qa_q2m_monsters monsters;

static size_t slot_of(qa_actor_id id) { return (size_t)id.slot; }
static qa_body_state body_of(int64_t slot) {
  qa_body_state_result body = qa_shared_body_table_read(R.bodies, make_id(slot));
  if (!body.has_value) { fprintf(stderr, "no body at slot %lld\n", (long long)slot); abort(); }
  return body.value;
}
static void set_body(int64_t slot, qa_body_state body) { qa_shared_body_table_write(R.bodies, make_owned(slot), body); }
/* Shared inventory reads, as TS game.host.inventory.entries(). */
static const qa_inventory_entry *power_entry(int64_t slot) {
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(R.inventory, make_id(slot), &count);
  for (size_t i = 0; i < count; i++) if (strcmp(entries[i].item, "q2:monster-power") == 0) return &entries[i];
  return NULL;
}
static qa_powered_protection_state powered_of(int64_t slot) {
  qa_combat_state_result combat = qa_gameplay_authority_read(R.combat, make_id(slot));
  qa_powered_protection_state none = {"none", 0};
  return combat.has_value ? combat.value.armor.powered : none;
}
static double stub_now(void *ctx) { EXPECT_CTX(ctx, HOST_CTX); return S.now; }
static double stub_frame_seconds(void *ctx) { EXPECT_CTX(ctx, HOST_CTX); return 0.1; }
static double stub_random(void *ctx) {
  EXPECT_CTX(ctx, HOST_CTX);
  if (S.random_at < S.random_count) return S.randoms[S.random_at++];
  return 0.5;
}
static qa_trace_result stub_trace(void *ctx, qa_q2_trace_request request) {
  EXPECT_CTX(ctx, HOST_CTX);
  qa_trace_result r;
  memset(&r, 0, sizeof r);
  r.kind = "q2";
  r.fraction = S.trace_fraction;
  r.end = r.fraction >= 1 ? request.end : request.start;
  r.hit.kind = QA_TRACE_HIT_NONE;
  r.contents = S.contents;
  if (request.bounds == NULL && S.has_ray && request.mask != QA_Q2M_MASK_OPAQUE) {
    r.fraction = 0.8; r.hit.kind = QA_TRACE_HIT_ACTOR; r.hit.actor = S.ray_actor;
    r.end = request.end;
  }
  return r;
}
static const qa_actor_id *stub_nearby(void *ctx, qa_vec3 origin, double radius, size_t *out) {
  EXPECT_CTX(ctx, HOST_CTX); (void)origin; (void)radius;
  *out = S.nearby_count;
  return S.nearby_ids;
}
static const qa_actor_id *stub_players(void *ctx, size_t *out) {
  EXPECT_CTX(ctx, HOST_CTX);
  *out = 1;
  return S.player_ids;
}
static bool stub_is_player(void *ctx, qa_actor_id actor) { EXPECT_CTX(ctx, HOST_CTX); return actor.slot == 7; }
static bool stub_is_monster(void *ctx, qa_actor_id actor) { EXPECT_CTX(ctx, HOST_CTX); (void)actor; return false; }
static void stub_emit(void *ctx, qa_q2_presentation_event event) {
  EXPECT_CTX(ctx, HOST_CTX);
  if (strcmp(event.kind, "monster-muzzleflash") == 0) { S.muzzle_count++; S.last_flash = event.flash; }
  if (strcmp(event.kind, "effect") == 0) {
    S.effect_count++;
    snprintf(S.last_effect, sizeof S.last_effect, "%s", event.effect ? event.effect : "");
  }
  if (strcmp(event.kind, "sound") == 0) {
    S.loop_count++;
    snprintf(S.last_loop, sizeof S.last_loop, "%s", event.loop ? event.loop : "");
  }
  if (strcmp(event.kind, "beam") == 0) { S.beam_count++; S.last_beam = event; }
}
static void stub_diagnostic(void *ctx, const char *message) { EXPECT_CTX(ctx, HOST_CTX); (void)message; S.diagnostic_count++; }
static void stub_touch_triggers(void *ctx, qa_owned_actor actor) { EXPECT_CTX(ctx, HOST_CTX); (void)actor; }
static double stub_point_contents(void *ctx, qa_vec3 point) { EXPECT_CTX(ctx, HOST_CTX); (void)point; return S.contents; }
static bool stub_true2(void *ctx, qa_vec3 a, qa_vec3 b) { EXPECT_CTX(ctx, HOST_CTX); (void)a; (void)b; return true; }
static qa_actor_id stub_world_actor(void *ctx) { EXPECT_CTX(ctx, HOST_CTX); return make_id(0); }
static double stub_gravity(void *ctx) { EXPECT_CTX(ctx, HOST_CTX); return 800; }

static qa_body_state stub_game_body(void *ctx, qa_owned_actor actor) {
  EXPECT_CTX(ctx, GAME_CTX);
  return body_of(actor.id.slot);
}
static qa_q2_entity *stub_game_entity(void *ctx, const qa_actor_id *actor) {
  EXPECT_CTX(ctx, GAME_CTX);
  if (!actor || actor->slot < 0 || actor->slot >= 8) return NULL;
  return S.entities[slot_of(*actor)];
}
static qa_q2_monster_target_result stub_monster_target(void *ctx, const qa_actor_id *actor) {
  EXPECT_CTX(ctx, GAME_CTX);
  qa_q2_monster_target_result r;
  memset(&r, 0, sizeof r);
  if (actor && qa_shared_body_table_read(R.bodies, *actor).has_value) {
    r.has_value = true; r.value.view_height = 22;
  }
  return r;
}
/* TS Q2EntityServices.move: bodies.write({...body, ...changes}). */
static void stub_game_move(void *ctx, qa_q2_entity *entity, qa_q2_body_changes changes, bool link) {
  EXPECT_CTX(ctx, GAME_CTX); (void)link;
  qa_body_state body = body_of(entity->actor.id.slot);
  if (changes.has_origin) body.origin = changes.origin;
  if (changes.has_angles) body.angles = changes.angles;
  if (changes.has_velocity) body.velocity = changes.velocity;
  if (changes.has_bounds) body.bounds = changes.bounds;
  qa_actor_id ground = changes.ground;
  if (changes.has_ground) body.ground = changes.ground_null ? NULL : &ground;
  set_body(entity->actor.id.slot, body);
}
static void stub_game_remove(void *ctx, qa_q2_entity *entity) { EXPECT_CTX(ctx, GAME_CTX); (void)entity; S.remove_count++; }
static void stub_game_noop_entity(void *ctx, qa_q2_entity *entity) { EXPECT_CTX(ctx, GAME_CTX); (void)entity; }
static void stub_game_cancel(void *ctx, qa_q2_entity *entity) { EXPECT_CTX(ctx, GAME_CTX); (void)entity; S.cancel_count++; }
static void stub_game_schedule(void *ctx, qa_q2_entity *entity, double delay, qa_q2_think think) {
  EXPECT_CTX(ctx, GAME_CTX); (void)entity; (void)delay; (void)think; S.schedule_count++;
}
static void stub_game_sound(void *ctx, qa_owned_actor actor, const char *path, double channel, double volume,
                            double attenuation) {
  EXPECT_CTX(ctx, GAME_CTX); (void)actor; (void)volume; (void)attenuation;
  S.sound_count++;
  snprintf(S.last_sound, sizeof S.last_sound, "%s", path ? path : "");
  S.last_sound_channel = channel;
}
static qa_q2_entity *stub_game_create(void *ctx, const char *classname, const qa_q2_field_pair *values, size_t count) {
  EXPECT_CTX(ctx, GAME_CTX); (void)values; (void)count;
  qa_q2_entity *created = &S.created[S.created_at % 4];
  S.created_at++;
  created->classname = classname;
  return created;
}
static qa_damage_outcome stub_game_damage(void *ctx, qa_actor_id target, qa_q2_entity *inflictor, qa_actor_id inflictor_id,
                                          const qa_actor_id *attacker, double amount, double knockback, qa_vec3 direction,
                                          qa_vec3 point, qa_vec3 normal, double means, double flags, qa_item_id weapon) {
  EXPECT_CTX(ctx, GAME_CTX);
  (void)target; (void)inflictor; (void)inflictor_id; (void)attacker; (void)knockback; (void)direction; (void)point;
  (void)normal; (void)means; (void)flags; (void)weapon;
  S.damage_count++; S.damage_amount = amount;
  qa_damage_outcome outcome;
  memset(&outcome, 0, sizeof outcome);
  return outcome;
}
static bool stub_can_damage(void *ctx, qa_actor_id target, qa_owned_actor attacker) {
  EXPECT_CTX(ctx, GAME_CTX); (void)target; (void)attacker;
  return true;
}
static void stub_use_targets(void *ctx, qa_authored_target entity, const qa_actor_id *activator, bool ignore_delay) {
  EXPECT_CTX(ctx, GAME_CTX); (void)entity; (void)activator; (void)ignore_delay;
}
static void stub_solid(void *ctx, qa_q2_entity *entity, const char *solid) {
  EXPECT_CTX(ctx, GAME_CTX); entity->solid = solid;
}
static void stub_motion(void *ctx, qa_q2_entity *entity, const char *kind) {
  EXPECT_CTX(ctx, GAME_CTX); entity->motion = kind;
}
static void stub_set_move(void *impl, qa_q2m_context *c, const char *name, bool has_immediate, bool immediate) {
  (void)impl; (void)c; (void)has_immediate; (void)immediate;
  S.set_move_count++;
  snprintf(S.last_move, sizeof S.last_move, "%s", name);
}
static void stub_context_handler(void *impl, qa_q2m_context *c) { (void)impl; (void)c; }
static bool stub_context_false(void *impl, qa_q2m_context *c) { (void)impl; (void)c; return false; }
static const char *stub_platform_state(void *impl, qa_q2m_context *c, qa_actor_id actor) {
  (void)impl; (void)c; (void)actor;
  return "bottom";
}
static void stub_schedule_cb(void *impl, qa_q2m_context *c, double delay, const char *name) {
  (void)impl; (void)c; (void)delay; (void)name;
}
static void stub_fire_bullet(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_vec3 start, qa_vec3 dir,
                             double damage, double kick, double hs, double vs, double mod) {
  EXPECT_CTX(ctx, WEAPONS_CTX); (void)self; (void)g; (void)start; (void)dir; (void)kick; (void)hs; (void)vs; (void)mod;
  S.bullet_count++; S.bullet_damage = damage;
}
static qa_q2_entity *stub_fire_blaster(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_vec3 start, qa_vec3 dir,
                                        double damage, double speed, double effects, bool hyper, bool has_mod, double mod) {
  EXPECT_CTX(ctx, WEAPONS_CTX); (void)self; (void)g; (void)start; (void)dir; (void)speed; (void)hyper; (void)has_mod; (void)mod;
  S.blaster_count++; S.blaster_effects = effects; S.blaster_damage = damage;
  return NULL;
}
static bool stub_fire_hit(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_vec3 aim, double damage, double kick) {
  EXPECT_CTX(ctx, WEAPONS_CTX); (void)self; (void)g; (void)aim; (void)kick;
  S.hit_count++; S.hit_damage = damage;
  return S.hit_result;
}
static qa_q2_entity *stub_fire_rocket(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_vec3 start, qa_vec3 dir,
                                       double damage, double speed, double radius, double rd) {
  EXPECT_CTX(ctx, WEAPONS_CTX); (void)self; (void)g; (void)start; (void)dir; (void)damage; (void)speed; (void)radius; (void)rd;
  S.rocket_count++;
  return NULL;
}
static qa_q2_entity *stub_fire_grenade(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_vec3 start, qa_vec3 dir,
                                        double damage, double speed, double time, double radius, bool hand,
                                        bool held, bool adjust, const qa_q2w_grenade_adjustment *adj) {
  EXPECT_CTX(ctx, WEAPONS_CTX); (void)self; (void)g; (void)start; (void)dir; (void)damage; (void)speed; (void)time;
  (void)radius; (void)hand; (void)held; (void)adjust; (void)adj;
  S.grenade_count++;
  return NULL;
}
static void stub_fire_rail(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_vec3 start, qa_vec3 dir,
                           double damage, double kick) {
  EXPECT_CTX(ctx, WEAPONS_CTX); (void)self; (void)g; (void)start; (void)dir; (void)kick;
  S.rail_count++; S.rail_damage = damage;
}
static qa_q2_entity *stub_fire_bfg(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_vec3 start, qa_vec3 dir,
                                    double damage, double speed, double radius) {
  EXPECT_CTX(ctx, WEAPONS_CTX); (void)self; (void)g; (void)start; (void)dir; (void)damage; (void)speed; (void)radius;
  S.bfg_count++;
  return NULL;
}

static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state b) {
  (void)c; (void)a;
  return (qa_bounds){qa_add3_double(b.origin, b.bounds.min), qa_add3_double(b.origin, b.bounds.max)};
}
static void link_noop(void *c, qa_linked_body b) { (void)c; (void)b; }
static void unlink_noop(void *c, qa_actor_id a) { (void)c; (void)a; }
static void world_teardown(void) {
  if (!R.live) return;
  qa_q2_source_callbacks_destroy(R.source_callbacks);
  qa_shared_inventory_table_destroy(R.inventory);
  qa_gameplay_authority_destroy(R.combat);
  qa_actor_callback_table_destroy(R.callbacks);
  qa_shared_body_table_destroy(R.bodies);
  qa_session_actor_registry_close(R.actors);
  qa_session_actor_registry_destroy(R.actors);
  qa_identity_owner_destroy(&R.identities);
  memset(&R, 0, sizeof R);
}
/* Slots 0..7: world, self (1), enemy (2), created (3..6), player (7).
 * Slots 1..6 carry bodies; self and enemy carry combat state (health 100,
 * damageable). Created slots stand for TS game.create actors: a body and
 * no combat state until the spawning code creates one. */
static void world_setup(void) {
  world_teardown();
  qa_identity_owner_create(&R.identities, "q2rm-test");
  R.actors = qa_session_actor_registry_create(&R.identities, 4096);
  qa_body_link_hooks link_hooks = {0};
  link_hooks.absolute_bounds = link_bounds; link_hooks.on_link = link_noop; link_hooks.on_unlink = unlink_noop;
  R.bodies = qa_shared_body_table_create(R.actors, link_hooks);
  R.callbacks = qa_actor_callback_table_create(R.actors);
  qa_gameplay_hooks hooks = {0};
  R.combat = qa_gameplay_authority_create(R.actors, R.callbacks, hooks);
  R.inventory = qa_shared_inventory_table_create(R.actors);
  R.source_callbacks = qa_q2_source_callbacks_create();
  R.live = true;
  for (int i = 0; i < WORLD_ACTORS; i++) {
    qa_owned_actor actor = qa_session_actor_registry_allocate(R.actors, "q2:game", "q2:actor");
    if (actor.id.slot < 0 || actor.id.slot >= WORLD_ACTORS) abort();
    R.owned[actor.id.slot] = actor;
  }
  for (int i = 1; i <= 6; i++) {
    qa_body_state body;
    memset(&body, 0, sizeof body);
    body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
    if (i == 1) body.origin = (qa_vec3){0, 0, 24};
    if (i == 2) body.origin = (qa_vec3){500, 0, 24};
    qa_shared_body_table_create_body(R.bodies, make_owned(i), body);
    if (i > 2) continue;
    qa_combat_state combat;
    memset(&combat, 0, sizeof combat);
    combat.health = 100; combat.can_take_damage = true;
    combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE; combat.armor.powered.kind = "none";
    qa_gameplay_authority_create_state(R.combat, make_owned(i), combat, NULL, NULL);
  }
}

static void reset_world(void) {
  memset(&S, 0, sizeof S);
  world_setup();
  S.now = 10; S.hit_result = true; S.trace_fraction = 1;
  S.player_ids[0] = make_id(7);
  qa_q2_rerelease_random_init(&S.random_stream, 1);
  memset(&host, 0, sizeof host);
  host.ctx = &HOST_CTX;
  host.actors = R.actors; host.bodies = R.bodies; host.callbacks = R.callbacks;
  host.combat = R.combat; host.inventory = R.inventory;
  host.now = stub_now; host.random = stub_random; host.frame_seconds = stub_frame_seconds;
  host.gravity = stub_gravity;
  host.trace = stub_trace; host.point_contents = stub_point_contents;
  host.in_phs = stub_true2; host.areas_connected = stub_true2;
  host.nearby = stub_nearby; host.players = stub_players; host.world_actor = stub_world_actor;
  host.is_player = stub_is_player; host.is_monster = stub_is_monster;
  host.touch_triggers = stub_touch_triggers; host.emit = stub_emit; host.diagnostic = stub_diagnostic;
  host.rerelease_random = &S.random_stream;
  memset(&game, 0, sizeof game);
  game.ctx = &GAME_CTX; game.host = &host;
  game.options.edition = QA_Q2_EDITION_RERELEASE; game.options.skill = 1; game.options.map_name = "base1";
  game.options.mode = QA_Q2_MODE_SINGLEPLAYER;
  game.source_callbacks = R.source_callbacks;
  game.body = stub_game_body; game.entity = stub_game_entity; game.monster_target = stub_monster_target;
  game.move = stub_game_move; game.remove = stub_game_remove; game.link = stub_game_noop_entity;
  game.show = stub_game_noop_entity; game.cancel = stub_game_cancel; game.schedule = stub_game_schedule;
  game.sound = stub_game_sound; game.create = stub_game_create; game.damage = stub_game_damage;
  game.can_damage = stub_can_damage; game.use_targets = stub_use_targets;
  game.solid = stub_solid; game.motion = stub_motion;
  memset(&self_entity, 0, sizeof self_entity);
  memset(&enemy_entity, 0, sizeof enemy_entity);
  self_entity.actor = make_owned(1); enemy_entity.actor = make_owned(2);
  self_entity.classname = "monster_test"; enemy_entity.classname = "player";
  self_entity.target = ""; self_entity.targetname = "self"; self_entity.message = "";
  self_entity.max_health = 1000; self_entity.view_height = 22; enemy_entity.view_height = 22;
  self_entity.scale = 1;
  self_entity.has_enemy = true; self_entity.enemy = enemy_entity.actor.id;
  S.entities[1] = &self_entity; S.entities[2] = &enemy_entity;
  for (int i = 0; i < 4; i++) {
    memset(&S.created[i], 0, sizeof S.created[i]);
    S.created[i].actor = make_owned(3 + i);
    S.entities[3 + i] = &S.created[i];
    S.created[i].classname = "created";
    S.created[i].target = ""; S.created[i].targetname = ""; S.created[i].message = "";
    S.created[i].scale = 1;
  }
  S.ray_actor = enemy_entity.actor.id; S.has_ray = true;
  memset(&state, 0, sizeof state);
  state.move = &qa_q2rm_berserk_moves[0];
  state.gib_health = -60;
  memset(&context, 0, sizeof context);
  context.entity = &self_entity; context.game = &game; context.state = &state;
  context.set_move = stub_set_move;
  context.stand = stub_context_handler; context.walk = stub_context_handler;
  context.run = stub_context_handler; context.melee = stub_context_handler;
  context.find_target = stub_context_false;
  context.platform_state = stub_platform_state;
  context.schedule = stub_schedule_cb;
  context.weapons.ctx = &WEAPONS_CTX;
  context.weapons.fire_bullet = stub_fire_bullet;
  context.weapons.fire_blaster = stub_fire_blaster;
  context.weapons.fire_hit = stub_fire_hit;
  context.weapons.fire_rocket = stub_fire_rocket;
  context.weapons.fire_grenade = stub_fire_grenade;
  context.weapons.fire_rail = stub_fire_rail;
  context.weapons.fire_bfg = stub_fire_bfg;
  memset(&monsters, 0, sizeof monsters);
}
static void queue_random(const double *values, size_t count) {
  memcpy(S.randoms, values, count * sizeof *values);
  S.random_count = count; S.random_at = 0;
}
static const qa_q2m_handler *find_callback(const qa_q2m_definition *d, const char *name) {
  for (size_t i = 0; i < d->callback_count; i++)
    if (strcmp(d->callbacks[i].name, name) == 0) return &d->callbacks[i].handler;
  return NULL;
}
static void dispatch(const qa_q2m_definition *d, const char *name) {
  const qa_q2m_handler *h = find_callback(d, name);
  CHECK(h != NULL && h->fn != NULL);
  if (h && h->fn) h->fn(h->ctx, &context);
}
static bool near(double a, double b) { return fabs(a - b) < 1e-6; }

/* ---- tests ---- */
static size_t total_frames(const qa_q2m_move *moves, size_t count) {
  size_t total = 0;
  for (size_t i = 0; i < count; i++) total += moves[i].frame_count;
  return total;
}
static void test_tables(void) {
  CHECK(qa_q2rm_actor_move_count == 11 && total_frames(qa_q2rm_actor_moves, qa_q2rm_actor_move_count) == 118);
  CHECK(qa_q2rm_arachnid_move_count == 9 && total_frames(qa_q2rm_arachnid_moves, qa_q2rm_arachnid_move_count) == 103);
  CHECK(qa_q2rm_berserk_move_count == 16 && total_frames(qa_q2rm_berserk_moves, qa_q2rm_berserk_move_count) == 193);
  CHECK(qa_q2rm_boss2_move_count == 12 && total_frames(qa_q2rm_boss2_moves, qa_q2rm_boss2_move_count) == 198);
  CHECK(qa_q2rm_boss31_move_count == 11 && total_frames(qa_q2rm_boss31_moves, qa_q2rm_boss31_move_count) == 191);
  CHECK(qa_q2rm_boss32_move_count == 11 && total_frames(qa_q2rm_boss32_moves, qa_q2rm_boss32_move_count) == 273);
  CHECK(qa_q2rm_brain_move_count == 14 && total_frames(qa_q2rm_brain_moves, qa_q2rm_brain_move_count) == 211);
  CHECK(qa_q2rm_chick_move_count == 17 && total_frames(qa_q2rm_chick_moves, qa_q2rm_chick_move_count) == 211);
  CHECK(qa_q2rm_flipper_move_count == 9 && total_frames(qa_q2rm_flipper_moves, qa_q2rm_flipper_move_count) == 146);
  CHECK(qa_q2rm_float_move_count == 12 && total_frames(qa_q2rm_float_moves, qa_q2rm_float_move_count) == 338);
  CHECK(qa_q2rm_flyer_move_count == 12 && total_frames(qa_q2rm_flyer_moves, qa_q2rm_flyer_move_count) == 212);
  CHECK(qa_q2rm_gladiator_move_count == 9 && total_frames(qa_q2rm_gladiator_moves, qa_q2rm_gladiator_move_count) == 91);
  CHECK(qa_q2rm_guardian_move_count == 12 && total_frames(qa_q2rm_guardian_moves, qa_q2rm_guardian_move_count) == 181);
  CHECK(qa_q2rm_guncmdr_move_count == 33 && total_frames(qa_q2rm_guncmdr_moves, qa_q2rm_guncmdr_move_count) == 511);
  CHECK(qa_q2rm_gunner_move_count == 17 && total_frames(qa_q2rm_gunner_moves, qa_q2rm_gunner_move_count) == 230);
  CHECK(qa_q2rm_hover_move_count == 11 && total_frames(qa_q2rm_hover_moves, qa_q2rm_hover_move_count) == 171);
  CHECK(qa_q2rm_insane_move_count == 18 && total_frames(qa_q2rm_insane_moves, qa_q2rm_insane_move_count) == 331);
  CHECK(qa_q2rm_medic_move_count == 11 && total_frames(qa_q2rm_medic_moves, qa_q2rm_medic_move_count) == 241);
  CHECK(qa_q2rm_mutant_move_count == 14 && total_frames(qa_q2rm_mutant_moves, qa_q2rm_mutant_move_count) == 152);
  CHECK(qa_q2rm_parasite_move_count == 14 && total_frames(qa_q2rm_parasite_moves, qa_q2rm_parasite_move_count) == 137);
  CHECK(qa_q2rm_shambler_move_count == 9 && total_frames(qa_q2rm_shambler_moves, qa_q2rm_shambler_move_count) == 94);
  CHECK(qa_q2rm_supertank_move_count == 11 && total_frames(qa_q2rm_supertank_moves, qa_q2rm_supertank_move_count) == 185);
  CHECK(qa_q2rm_tank_move_count == 17 && total_frames(qa_q2rm_tank_moves, qa_q2rm_tank_move_count) == 275);
  CHECK(qa_q2rm_medic_frames_value.attack42 == 218 && qa_q2rm_medic_frames_value.attack50 == 226);
  CHECK(qa_q2rm_shambler_frames_value.magic01 == 65 && qa_q2rm_shambler_frames_value.magic09 == 73);
  CHECK(qa_q2rm_arachnid_frames_value.rails8 == 7);
  CHECK(qa_q2rm_guncmdr_frames_value.c_attack401 == 419);
  CHECK(qa_q2rm_berserk_frames_value.slam18 == 163 && qa_q2rm_berserk_frames_value.r_att13 == 122);
  CHECK(qa_q2rm_parasite_frames_value.break01 == 0 && qa_q2rm_parasite_frames_value.drain01 == 39);
  CHECK(qa_q2rm_rereleaseFlash_value.GUARDIAN_BLASTER == 227 && qa_q2rm_rereleaseFlash_value.GUNCMDR_CHAINGUN_1 == 240);
  CHECK(qa_q2rm_rereleaseFlash_value.ARACHNID_RAIL1 == 228 && qa_q2rm_rereleaseFlash_value.ARACHNID_RAIL2 == 229);
  CHECK(strcmp(qa_q2rm_shambler_moves[0].name, "shambler_move_stand") == 0);
  CHECK(qa_q2rm_shambler_moves[0].first_frame == 0 && qa_q2rm_shambler_moves[0].last_frame == 16);
  CHECK(qa_q2rm_shambler_moves[0].end == NULL);
  CHECK(qa_q2rm_shambler_moves[0].frames[0].ai == QA_Q2M_AI_STAND
        && qa_q2rm_shambler_moves[0].frames[0].lerp_frame == -1);
}

static void test_definitions(void) {
  reset_world();
  const qa_q2m_definition *arachnid = &qa_q2rm_arachnid_definition;
  CHECK(strcmp(arachnid->classname, "monster_arachnid") == 0 && arachnid->health == 1000);
  CHECK(arachnid->mass == 450 && arachnid->move_count == 9);
  const qa_q2m_definition *shambler = &qa_q2rm_shambler_definition;
  CHECK(strcmp(shambler->kind, "shambler") == 0 && shambler->health == 600 && shambler->gib_health == -60);
  const qa_q2m_definition *gunner = qa_q2rm_get_rerelease_gunner_definition();
  CHECK(gunner->health == 175 && gunner->scale == (float)1.15 && gunner->blind_fire);
  CHECK(gunner->move_count == 17);
  CHECK(gunner->dodge == NULL); /* TS literal omits dodge. */
  const qa_q2m_definition *flipper = qa_q2rm_get_rerelease_flipper_definition();
  CHECK(flipper->bounds.max.z == 20 && flipper->initialize.fn != NULL);
  /* Flipper initialize: alternate-fly values. */
  flipper->initialize.fn(flipper->initialize.ctx, &context);
  CHECK(state.alt.alternate_fly && state.alt.fly_speed == 110 && state.alt.fly_min_distance == 10);
  const qa_q2m_definition *floater = qa_q2rm_get_rerelease_floater_definition();
  CHECK(floater->move_count == 12 && floater->bounds.max.z == 48);
  const qa_q2m_definition *insane = qa_q2rm_get_rerelease_insane_definition();
  CHECK(insane->move_count == 18);
  const qa_q2m_definition *boss2 = qa_q2rm_get_rerelease_boss2_definition();
  CHECK(boss2->has_yaw_speed && boss2->yaw_speed == 50);
  const qa_q2m_definition *makron = qa_q2rm_get_rerelease_makron_definition();
  CHECK(makron->move_count == 11);
  /* Gunner GunnerFire: bullet damage 3. */
  reset_world();
  state.move = &qa_q2rm_gunner_moves[0];
  self_entity.frame = qa_q2rm_gunner_frames_value.attak216;
  dispatch(gunner, "GunnerFire");
  CHECK(S.bullet_count == 1 && S.bullet_damage == 3);
}

static qa_q2mpm_weapons stub_mission_weapons(void) {
  qa_q2mpm_weapons weapons;
  memset(&weapons, 0, sizeof weapons);
  return weapons;
}

static void test_factory_definitions(void) {
  reset_world();
  qa_q2mpm_arena arena = {0};
  qa_q2mpm_weapons weapons = stub_mission_weapons();
  qa_q2m_definition pair[2];
  CHECK(qa_q2rm_create_rerelease_gladiator_definitions(&arena, weapons, pair) == 2);
  CHECK(strcmp(pair[1].classname, "monster_gladb") == 0 && pair[1].health == 250 && pair[1].mass == 350);
  CHECK(pair[0].bounds.min.x == -32 && pair[0].bounds.max.z == 42);
  CHECK(qa_q2rm_create_rerelease_tank_definitions(&arena, weapons, pair) == 2);
  CHECK(strcmp(pair[1].classname, "monster_tank_commander") == 0 && pair[1].health == 1000 && pair[1].gib_health == -225);
  /* Commander initialize sets skin 2 and count 1. */
  pair[1].initialize.fn(pair[1].initialize.ctx, &context);
  CHECK(self_entity.skin == 2 && self_entity.count == 1);
  CHECK(qa_q2rm_create_rerelease_supertank_definitions(&arena, weapons, true, pair) == 2);
  CHECK(strcmp(pair[1].classname, "monster_boss5") == 0);
  /* boss5 initialize sets skin 2 and (via n64) count 10. */
  self_entity.spawnflags = 0;
  pair[1].initialize.fn(pair[1].initialize.ctx, &context);
  CHECK(self_entity.skin == 2 && self_entity.count == 10);
  /* TS game.host.combat.setPoweredProtection(actor, {kind: "shield", cells: 400}). */
  CHECK(strcmp(powered_of(1).kind, "shield") == 0 && powered_of(1).cells == 400);
  CHECK(qa_q2rm_create_rerelease_chick_definitions(&arena, weapons, pair) == 2);
  CHECK(strcmp(pair[1].classname, "monster_chick_heat") == 0);
  CHECK(pair[0].blind_fire && pair[0].health == 175);
  qa_q2mpm_monster_state source;
  qa_q2mpm_state_init(&source);
  CHECK(qa_q2rm_create_rerelease_medic_definitions(&arena, &monsters, weapons, &source, pair) == 2);
  CHECK(strcmp(pair[0].kind, "medic") == 0 && pair[0].mass == 400 && pair[0].health == 300);
  CHECK(strcmp(pair[1].kind, "medic_commander") == 0 && pair[1].health == 600 && pair[1].mass == 600);
  CHECK(pair[1].has_yaw_speed && pair[1].yaw_speed == 40);
  qa_q2m_definition guncmdr = qa_q2rm_create_gun_commander_definition(&arena, weapons);
  CHECK(guncmdr.health == 325 && guncmdr.gib_health == -175 && guncmdr.move_count == 33);
  /* Gun commander initialize: scale/skin/power armor. */
  reset_world();
  bool had_inventory = qa_shared_inventory_table_has(R.inventory, make_id(1));
  guncmdr.initialize.fn(guncmdr.initialize.ctx, &context);
  CHECK(self_entity.scale == 1.25 && self_entity.skin == 2);
  /* TS inventory.create(actor, []) then configure {q2:monster-power, 200, 200}. */
  CHECK(had_inventory == false && qa_shared_inventory_table_has(R.inventory, make_id(1)) && power_entry(1) != NULL
        && power_entry(1)->count == 200);
  CHECK(power_entry(1) != NULL && power_entry(1)->capacity == 200);
  /* bindPowerArmorCells: powered cells now read through the inventory entry. */
  CHECK(strcmp(powered_of(1).kind, "shield") == 0 && powered_of(1).cells == 200);
  {
    qa_inventory_entry drained;
    memset(&drained, 0, sizeof drained);
    drained.item = "q2:monster-power"; drained.count = 150; drained.capacity = 200;
    qa_shared_inventory_table_configure(R.inventory, make_owned(1), drained, NULL, NULL);
    CHECK(powered_of(1).cells == 150);
  }
  qa_q2rm_mutant_definition mutant;
  qa_q2rm_create_rerelease_mutant_definition(&mutant, &monsters);
  CHECK(mutant.definition.move_count == 14 && mutant.definition.check_attack != NULL);
  CHECK(find_callback(&mutant.definition, "mutant_jump_up") != NULL);
  qa_q2rm_jorg_definition jorg;
  qa_q2rm_healthbar_transfer no_transfer = {NULL, NULL};
  qa_q2rm_create_rerelease_jorg_definition(&arena, &jorg, &monsters, no_transfer);
  CHECK(jorg.definition.health == 8000);
  CHECK(strcmp(jorg.definition.model, "models/monsters/boss3/jorg/tris.md2") == 0);
  qa_q2rm_actor_module actor;
  qa_q2rm_create_rerelease_actor_module(&actor, &monsters);
  CHECK(find_callback(&actor.definition, "actor_fire") != NULL);
  CHECK(actor.definition.move_count == 11);
  qa_q2mpm_arena_destroy(&arena);
}

static void test_handlers(void) {
  /* Shambler: floating-point lightning winds up with beam owner. */
  reset_world();
  qa_q2mpm_arena arena = {0};
  state.move = &qa_q2rm_shambler_moves[0];
  { qa_body_state body = body_of(1); body.origin = (qa_vec3){0, 0, 0}; set_body(1, body); }
  self_entity.frame = qa_q2rm_shambler_frames_value.magic01;
  dispatch(&qa_q2rm_shambler_definition, "shambler_windup");
  CHECK(S.created[0].has_owner && strcmp(S.created[0].model, "models/proj/lightning/tris.md2") == 0);
  CHECK(S.beam_count == 1 && near(S.last_beam.start.x, 44) && near(S.last_beam.start.y, -36)
        && near(S.last_beam.start.z, 25));
  CHECK(near(S.last_beam.end.y, 38) && near(S.last_beam.end.x, 28));
  /* Shambler lightning cast: bullet fired, beam from raised hand. */
  S.bullet_count = 0;
  dispatch(&qa_q2rm_shambler_definition, "ShamblerCastLightning");
  CHECK(S.bullet_count == 1 && S.bullet_damage >= 8 && S.bullet_damage < 12);
  CHECK(S.last_beam.start.z == 48);

  /* Berserk spike: random 0 -> damage 5. */
  reset_world();
  state.move = &qa_q2rm_berserk_moves[0];
  const double zeros[4] = {0.0, 0.0, 0.0, 0.0};
  queue_random(zeros, 4);
  qa_q2m_definition berserk = qa_q2rm_create_rerelease_berserk_definition(&arena, &monsters);
  dispatch(&berserk, "berserk_attack_spike");
  CHECK(S.hit_count == 1 && S.hit_damage == 5);
  /* Berserk jump takeoff reaches source vertical velocity 450. */
  dispatch(&berserk, "berserk_jump_takeoff");
  CHECK(near(body_of(1).velocity.z, 450));
  CHECK(self_entity.touch.fn != NULL);
  /* Slam radius damage: no nearby actors -> no damage calls. */
  qa_q2rm_slam_radius_damage(&context, (qa_vec3){0, 0, 0}, 8, 300, 165);
  CHECK(S.damage_count == 0);

  /* Arachnid rail at rails8 -> RAIL2 flash, damage 35. */
  reset_world();
  state.move = &qa_q2rm_arachnid_moves[0];
  self_entity.frame = qa_q2rm_arachnid_frames_value.rails8;
  self_entity.pos1 = body_of(2).origin;
  dispatch(&qa_q2rm_arachnid_definition, "arachnid_rail");
  CHECK(S.rail_count == 1 && S.rail_damage == 35);
  CHECK(S.muzzle_count == 1 && S.last_flash == qa_q2rm_rereleaseFlash_value.ARACHNID_RAIL2);

  /* Guardian blaster: GUARDIAN_BLASTER flash, damage 2. */
  reset_world();
  state.move = &qa_q2rm_guardian_moves[0];
  qa_q2m_definition guardian = qa_q2rm_create_guardian_definition(&arena, &monsters);
  dispatch(&guardian, "guardian_fire_blaster");
  CHECK(S.blaster_count == 1 && S.blaster_damage == 2);
  CHECK(S.last_flash == qa_q2rm_rereleaseFlash_value.GUARDIAN_BLASTER);

  /* Insane: cross frame runs down. */
  reset_world();
  state.move = &qa_q2rm_insane_moves[0];
  self_entity.spawnflags = 16;
  self_entity.frame = qa_q2rm_insane_frames_value.cr_pain10;
  const qa_q2m_definition *insane = qa_q2rm_get_rerelease_insane_definition();
  insane->run.fn(insane->run.ctx, &context);
  CHECK(strcmp(S.last_move, "insane_move_down") == 0);

  /* Brain run turns its power screen on. */
  reset_world();
  state.move = &qa_q2rm_brain_moves[0];
  {
    /* The brain's shared power cells: TS inventory entry q2:monster-power = 100. */
    qa_shared_inventory_table_create_entries(R.inventory, make_owned(1), NULL, 0);
    qa_inventory_entry cells;
    memset(&cells, 0, sizeof cells);
    cells.item = "q2:monster-power"; cells.count = 100; cells.capacity = 100;
    qa_shared_inventory_table_configure(R.inventory, make_owned(1), cells, NULL, NULL);
  }
  qa_q2m_definition brain = qa_q2rm_create_rerelease_brain_definition(&arena, &monsters);
  brain.run.fn(brain.run.ctx, &context);
  CHECK(strcmp(powered_of(1).kind, "screen") == 0 && powered_of(1).cells == 100);

  /* Parasite proboscis fire creates tip (speed 1250) and segment. */
  reset_world();
  state.move = &qa_q2rm_parasite_moves[0];
  self_entity.frame = qa_q2rm_parasite_frames_value.drain01;
  qa_q2m_definition parasite = qa_q2rm_create_rerelease_parasite_definition(&arena, &monsters);
  dispatch(&parasite, "parasite_fire_proboscis");
  CHECK(S.created[0].speed == 1250 && S.created[0].projectile);
  CHECK(strcmp(S.created[1].model, "models/monsters/parasite/segment/tris.md2") == 0);
  CHECK(self_entity.has_proboscus);
  qa_q2mpm_arena_destroy(&arena);
}

static void test_common(void) {
  reset_world();
  /* rereleaseRandom returns the seeded stream; aborts without one. */
  CHECK(qa_q2rm_rerelease_random(&context) == &S.random_stream);
  /* Seed-1 MT19937 sequence matches the TS class: 0, 1. */
  CHECK(qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(&context), 2) == 0);
  CHECK(qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(&context), 2) == 1);
  CHECK_ABORTS(host.rerelease_random = NULL; (void)qa_q2rm_rerelease_random(&context));
  /* Gravity hook default path. */
  CHECK(qa_q2rm_host_gravity(&game) == 800);
  /* calculatePitchToFire with unobstructed traces finds no impact. */
  reset_world();
  state.move = &qa_q2rm_gunner_moves[0];
  S.has_ray = false;
  qa_vec3 out;
  CHECK(!qa_q2rm_calculate_pitch_to_fire_default(&context, body_of(2).origin, body_of(1).origin,
                                                 (qa_vec3){1, 0, 0}, 600, 2.5, false, &out));
  /* monsterJumpFinished raises slow forward velocity to 150. */
  { qa_body_state body = body_of(1); body.velocity = (qa_vec3){10, 0, 20}; set_body(1, body); }
  state.jump_time = 0;
  CHECK(qa_q2rm_monster_jump_finished(&context));
  CHECK(near(body_of(1).velocity.x, 150) && near(body_of(1).velocity.z, 20));
  /* blockedCheckPlatform without enemy body is false. */
  self_entity.has_enemy = false;
  CHECK(!qa_q2rm_blocked_check_platform(&context, 48));
  /* Predict aim without enemy returns null (false). */
  qa_q2rm_predicted_aim aim;
  CHECK(!qa_q2rm_predict_aim_default(&context, body_of(1).origin, 800, false, &aim));
  /* Jump result names. */
  CHECK(strcmp(qa_q2rm_jump_result_name(QA_Q2RM_JUMP_UP), "up") == 0);
  CHECK(strcmp(qa_q2rm_jump_result_name(QA_Q2RM_JUMP_NONE), "none") == 0);
  /* Spawn ground check: empty contents + clear trace -> false. */
  reset_world();
  S.has_ray = false;
  CHECK(!qa_q2rm_check_rerelease_ground_spawn_point(&game, (qa_vec3){0, 0, 24},
                                                    (qa_bounds){{-16, -16, -24}, {16, 16, 32}}, 256, -1));
}

static void test_medic_reinforcements(void) {
  qa_q2_spawn_fields empty;
  memset(&empty, 0, sizeof empty);
  qa_q2rm_medic_reinforcement_choice *entries = NULL;
  size_t count = qa_q2rm_rerelease_medic_reinforcements(&empty, &entries);
  CHECK(count == 7u);
  CHECK(strcmp(entries[0].classname, "monster_soldier_light") == 0 && entries[0].strength == 1);
  CHECK(strcmp(entries[6].classname, "monster_gladiator") == 0 && entries[6].strength == 6);
  qa_q2rm_medic_reinforcements_free(entries, count);
  qa_q2_spawn_fields custom;
  memset(&custom, 0, sizeof custom);
  CHECK(qa_q2_spawn_set(&custom, "reinforcements", "monster_test 3;monster_other"));
  count = qa_q2rm_rerelease_medic_reinforcements(&custom, &entries);
  CHECK(count == 2u && entries[0].strength == 3 && isnan(entries[1].strength));
  qa_q2rm_medic_reinforcements_free(entries, count);
  qa_q2_spawn_fields blank;
  memset(&blank, 0, sizeof blank);
  CHECK(qa_q2_spawn_set(&blank, "reinforcements", ""));
  count = qa_q2rm_rerelease_medic_reinforcements(&blank, &entries);
  CHECK(count == 0u && entries == NULL);
  qa_q2_spawn_fields_free(&custom);
  qa_q2_spawn_fields_free(&blank);
}

static void test_spawn_modules(void) {
  reset_world();
  /* Tank stand spawn: scale/skin/frame. */
  qa_q2rm_tank_stand_module stand;
  qa_q2rm_create_tank_stand_module(&stand);
  qa_q2_spawn_module stand_spawn = qa_q2rm_tank_stand_as_spawn(&stand);
  qa_q2_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.actor = make_owned(1);
  entity.classname = "monster_tank_stand";
  entity.target = ""; entity.targetname = ""; entity.message = "";
  CHECK(stand_spawn.spawn(stand_spawn.ctx, &entity, &game));
  CHECK(entity.scale == 1.5 && entity.skin == 2 && entity.frame == qa_q2rm_tank_frames_value.stand01);
  CHECK(entity.use.fn != NULL);
  /* Target actor spawn: jump target defaults. */
  qa_q2rm_actor_module actor;
  qa_q2rm_create_rerelease_actor_module(&actor, &monsters);
  qa_q2_spawn_module targets = qa_q2rm_actor_targets_as_spawn(&actor);
  memset(&entity, 0, sizeof entity);
  entity.actor = make_owned(1);
  entity.classname = "target_actor";
  entity.targetname = "door_actor";
  entity.target = ""; entity.message = "";
  entity.spawnflags = 1;
  CHECK(targets.spawn(targets.ctx, &entity, &game));
  CHECK(entity.speed == 200 && entity.movedir.z == 200 && entity.touch.fn != NULL);
}

static void test_registration(void) {
  reset_world();
  qa_q2m_weapons foundation_weapons;
  memset(&foundation_weapons, 0, sizeof foundation_weapons);
  foundation_weapons.ctx = &S;
  foundation_weapons.fire_bullet = stub_fire_bullet;
  foundation_weapons.fire_blaster = stub_fire_blaster;
  foundation_weapons.fire_hit = stub_fire_hit;
  foundation_weapons.fire_rocket = stub_fire_rocket;
  foundation_weapons.fire_grenade = stub_fire_grenade;
  foundation_weapons.fire_rail = stub_fire_rail;
  foundation_weapons.fire_bfg = stub_fire_bfg;
  qa_q2m_monsters registry;
  memset(&registry, 0, sizeof registry);
  CHECK(qa_q2m_monsters_init(&registry, foundation_weapons, (qa_q2m_monster_hooks){0}));
  qa_q2rm_ordinary_module ordinary;
  qa_q2mpm_weapons weapons = stub_mission_weapons();
  qa_q2rm_register_rerelease_ordinary_monsters(&ordinary, &registry, weapons);
  const qa_q2m_definition *berserk = qa_q2m_monsters_definition(&registry, "monster_berserk", &game);
  CHECK(berserk != NULL && strcmp(berserk->kind, "berserk") == 0);
  /* Heat/plasma variants excluded from ordinary registration. */
  CHECK(qa_q2m_monsters_definition(&registry, "monster_chick_heat", &game) == NULL);
  CHECK(qa_q2m_monsters_definition(&registry, "monster_gladb", &game) == NULL);
  CHECK(qa_q2m_monsters_definition(&registry, "monster_chick", &game) != NULL);
  qa_q2m_monsters_destroy(&registry);
  qa_q2rm_ordinary_module_destroy(&ordinary);
}

int main(void) {
  test_tables();
  test_definitions();
  test_factory_definitions();
  test_handlers();
  test_common();
  test_medic_reinforcements();
  test_spawn_modules();
  test_registration();
  world_teardown();
  CHECK(ctx_errors == 0);
  if (failures) {
    fprintf(stderr, "%d/%d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d checks passed\n", checks);
  return 0;
}

