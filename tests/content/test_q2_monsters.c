/*
 * Port of key cases from tests/gameplay/q2-base/monsters/monsters.test.ts
 * for src/content/q2/base/monsters (definitions, tables, shared helpers,
 * and handler behavior against a stubbed host). The full Medic/Brain
 * revive and Jorg/Makron integration flows need the Q2Foundation runtime,
 * which has no C counterpart yet; their definition-level contracts are
 * covered here. GPL-2.0-or-later.
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

#include "qa/content/q2/base/monsters.h"
#include "qa/content/q2/foundation/monsters/ai.h"

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

/* ---- world: real shared tables, stubbed geometry ---- */
/* Index 0 world, 1 self, 2 enemy, 3 created, 7 player; 4-6 unused fillers.
 * Every callback checks its sentinel ctx. */
#define ACTORS 8
typedef struct stub {
  double now;
  double randoms[32]; size_t random_count, random_at;
  qa_identity_owner identities;
  qa_session_actor_registry *registry;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_q2_source_callbacks *source_callbacks;
  qa_owned_actor ids[ACTORS];
  qa_q2_entity *entities[ACTORS];
  qa_actor_id ray_actor; bool has_ray;
  char last_move[96]; int set_move_count;
  char last_sound[128]; double last_sound_channel; int sound_count;
  int muzzle_count; double last_flash;
  int effect_count; char last_effect[64];
  int loop_count; char last_loop[16];
  int remove_count, cancel_count, schedule_count;
  int diagnostic_count;
  double damage_amount; int damage_count;
  int bullet_count; double bullet_damage;
  int blaster_count; double blaster_effects;
  int rocket_count, hit_count; bool hit_result;
  qa_q2_entity created;
  qa_actor_id nearby_ids[4]; size_t nearby_count;
  qa_actor_id player_ids[1];
  qa_q2_presentation_event last_extended; int extended_count;
  qa_q2_named_think boss_think[1];
} stub;
static stub S;
static int host_sentinel, game_sentinel, weapons_sentinel, impl_sentinel, link_sentinel;
static qa_q2_foundation_host host;
static qa_q2_game_services game;
static qa_q2_entity self_entity, enemy_entity;
static qa_q2m_state state;
static qa_q2m_context context;
static qa_q2m_monsters monsters;
static bool world_built;

static size_t slot_of(qa_actor_id id) {
  for (size_t i = 0; i < ACTORS; i++) if (qa_same_actor(S.ids[i].id, id)) return i;
  return ACTORS;
}
static qa_body_state body_of(size_t index) {
  qa_body_state_result r = qa_shared_body_table_read(S.bodies, S.ids[index].id);
  CHECK(r.has_value);
  return r.value;
}
static void set_origin(size_t index, qa_vec3 origin) {
  qa_body_state body = body_of(index);
  body.origin = origin;
  qa_shared_body_table_write(S.bodies, S.ids[index], body);
}
static double stub_now(void *ctx) { CHECK(ctx == &host_sentinel); return S.now; }
static double stub_random(void *ctx) {
  CHECK(ctx == &host_sentinel);
  if (S.random_at < S.random_count) return S.randoms[S.random_at++];
  return 0.5;
}
static qa_trace_result stub_trace(void *ctx, qa_q2_trace_request request) {
  CHECK(ctx == &host_sentinel);
  qa_trace_result r;
  memset(&r, 0, sizeof r);
  r.kind = "q2"; r.fraction = 1; r.end = request.end; r.hit.kind = QA_TRACE_HIT_NONE;
  if (request.bounds == NULL && S.has_ray && request.mask != QA_Q2M_MASK_OPAQUE) {
    r.fraction = 0.8; r.hit.kind = QA_TRACE_HIT_ACTOR; r.hit.actor = S.ray_actor;
  }
  return r;
}
static const qa_actor_id *stub_nearby(void *ctx, qa_vec3 origin, double radius, size_t *out) {
  CHECK(ctx == &host_sentinel); (void)origin; (void)radius;
  *out = S.nearby_count;
  return S.nearby_ids;
}
static const qa_actor_id *stub_players(void *ctx, size_t *out) {
  CHECK(ctx == &host_sentinel);
  *out = 1;
  return S.player_ids;
}
static bool stub_is_player(void *ctx, qa_actor_id actor) { CHECK(ctx == &host_sentinel); return slot_of(actor) == 7; }
static bool stub_is_monster(void *ctx, qa_actor_id actor) { CHECK(ctx == &host_sentinel); (void)actor; return false; }
static void stub_emit(void *ctx, qa_q2_presentation_event event) {
  CHECK(ctx == &host_sentinel);
  if (strcmp(event.kind, "monster-muzzleflash") == 0) { S.muzzle_count++; S.last_flash = event.flash; }
  if (strcmp(event.kind, "effect") == 0) {
    S.effect_count++;
    snprintf(S.last_effect, sizeof S.last_effect, "%s", event.effect ? event.effect : "");
  }
  if (strcmp(event.kind, "sound") == 0) {
    S.loop_count++;
    snprintf(S.last_loop, sizeof S.last_loop, "%s", event.loop ? event.loop : "");
  }
  if (strcmp(event.kind, "monster-beam") == 0 || strcmp(event.kind, "print") == 0) {
    S.extended_count++; S.last_extended = event;
  }
}
static void stub_diagnostic(void *ctx, const char *message) { CHECK(ctx == &host_sentinel); (void)message; S.diagnostic_count++; }
static void stub_touch_triggers(void *ctx, qa_owned_actor actor) { CHECK(ctx == &host_sentinel); (void)actor; }
static double stub_point_contents(void *ctx, qa_vec3 point) { CHECK(ctx == &host_sentinel); return point.z < 0 ? 1 : 0; }
static bool stub_true2(void *ctx, qa_vec3 a, qa_vec3 b) { CHECK(ctx == &host_sentinel); (void)a; (void)b; return true; }
static qa_actor_id stub_world_actor(void *ctx) { CHECK(ctx == &host_sentinel); return S.ids[0].id; }

static qa_body_state stub_game_body(void *ctx, qa_owned_actor actor) {
  CHECK(ctx == &game_sentinel);
  return body_of(slot_of(actor.id));
}
static qa_q2_entity *stub_game_entity(void *ctx, const qa_actor_id *actor) {
  CHECK(ctx == &game_sentinel);
  if (!actor) return NULL;
  size_t i = slot_of(*actor);
  return i < ACTORS ? S.entities[i] : NULL;
}
static qa_q2_monster_target_result stub_monster_target(void *ctx, const qa_actor_id *actor) {
  CHECK(ctx == &game_sentinel);
  qa_q2_monster_target_result r;
  memset(&r, 0, sizeof r);
  if (actor && qa_shared_body_table_read(S.bodies, *actor).has_value) {
    r.has_value = true; r.value.view_height = 22;
  }
  return r;
}
static void stub_game_move(void *ctx, qa_q2_entity *entity, qa_q2_body_changes changes, bool link) {
  CHECK(ctx == &game_sentinel); (void)link;
  qa_body_state body = body_of(slot_of(entity->actor.id));
  if (changes.has_origin) body.origin = changes.origin;
  if (changes.has_angles) body.angles = changes.angles;
  if (changes.has_velocity) body.velocity = changes.velocity;
  if (changes.has_bounds) body.bounds = changes.bounds;
  qa_actor_id ground = changes.ground;
  if (changes.has_ground) body.ground = changes.ground_null ? NULL : &ground;
  qa_shared_body_table_write(S.bodies, entity->actor, body);
}
static void stub_game_remove(void *ctx, qa_q2_entity *entity) { CHECK(ctx == &game_sentinel); (void)entity; S.remove_count++; }
static void stub_game_noop_entity(void *ctx, qa_q2_entity *entity) { CHECK(ctx == &game_sentinel); (void)entity; }
static void stub_game_cancel(void *ctx, qa_q2_entity *entity) { CHECK(ctx == &game_sentinel); (void)entity; S.cancel_count++; }
static void stub_game_schedule(void *ctx, qa_q2_entity *entity, double delay, qa_q2_think think) {
  CHECK(ctx == &game_sentinel); (void)entity; (void)delay; (void)think; S.schedule_count++;
}
static void stub_game_sound(void *ctx, qa_owned_actor actor, const char *path, double channel, double volume,
                            double attenuation) {
  CHECK(ctx == &game_sentinel); (void)actor; (void)volume; (void)attenuation;
  S.sound_count++;
  snprintf(S.last_sound, sizeof S.last_sound, "%s", path ? path : "");
  S.last_sound_channel = channel;
}
static qa_q2_entity *stub_game_create(void *ctx, const char *classname, const qa_q2_field_pair *values, size_t count) {
  CHECK(ctx == &game_sentinel); (void)classname; (void)values; (void)count;
  return &S.created;
}
static qa_damage_outcome stub_game_damage(void *ctx, qa_actor_id target, qa_q2_entity *inflictor_entity,
                                          qa_actor_id inflictor_id, const qa_actor_id *attacker, double amount,
                                          double knockback, qa_vec3 direction, qa_vec3 point, qa_vec3 normal,
                                          double means_of_death, double flags, qa_item_id weapon) {
  CHECK(ctx == &game_sentinel);
  (void)target; (void)inflictor_entity; (void)inflictor_id; (void)attacker; (void)knockback; (void)direction;
  (void)point; (void)normal; (void)means_of_death; (void)flags; (void)weapon;
  S.damage_count++; S.damage_amount = amount;
  qa_damage_outcome outcome;
  memset(&outcome, 0, sizeof outcome);
  return outcome;
}
static void stub_solid(void *ctx, qa_q2_entity *entity, const char *solid) {
  CHECK(ctx == &game_sentinel); entity->solid = solid;
}
static void stub_motion(void *ctx, qa_q2_entity *entity, const char *kind) {
  CHECK(ctx == &game_sentinel); entity->motion = kind;
}
static void stub_set_move(void *impl, qa_q2m_context *c, const char *name, bool has_immediate, bool immediate) {
  CHECK(impl == &impl_sentinel); (void)c; (void)has_immediate; (void)immediate;
  S.set_move_count++;
  snprintf(S.last_move, sizeof S.last_move, "%s", name);
}
static void stub_walk(void *impl, qa_q2m_context *c) { CHECK(impl == &impl_sentinel); (void)c; }
static void stub_fire_bullet(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_vec3 start, qa_vec3 dir,
                             double damage, double kick, double hs, double vs, double mod) {
  CHECK(ctx == &weapons_sentinel); (void)self; (void)g; (void)start; (void)dir; (void)kick; (void)hs; (void)vs; (void)mod;
  S.bullet_count++; S.bullet_damage = damage;
}
static qa_q2_entity *stub_fire_blaster(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_vec3 start, qa_vec3 dir,
                                        double damage, double speed, double effects, bool hyper, bool has_mod, double mod) {
  CHECK(ctx == &weapons_sentinel); (void)self; (void)g; (void)start; (void)dir; (void)damage; (void)speed; (void)hyper; (void)has_mod; (void)mod;
  S.blaster_count++; S.blaster_effects = effects;
  return NULL;
}
static bool stub_fire_hit(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_vec3 aim, double damage, double kick) {
  CHECK(ctx == &weapons_sentinel); (void)self; (void)g; (void)aim; (void)damage; (void)kick;
  S.hit_count++;
  return S.hit_result;
}
static qa_q2_entity *stub_fire_rocket(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_vec3 start, qa_vec3 dir,
                                       double damage, double speed, double radius, double rd) {
  CHECK(ctx == &weapons_sentinel); (void)self; (void)g; (void)start; (void)dir; (void)damage; (void)speed; (void)radius; (void)rd;
  S.rocket_count++;
  return NULL;
}
static qa_bounds link_bounds(void *ctx, qa_owned_actor actor, qa_body_state body) {
  CHECK(ctx == &link_sentinel); (void)actor;
  return (qa_bounds){qa_add3_double(body.origin, body.bounds.min), qa_add3_double(body.origin, body.bounds.max)};
}
static void link_noop(void *ctx, qa_linked_body body) { CHECK(ctx == &link_sentinel); (void)body; }
static void unlink_noop(void *ctx, qa_actor_id actor) { CHECK(ctx == &link_sentinel); (void)actor; }
static qa_combat_state combat_state(double health) {
  qa_combat_state s;
  memset(&s, 0, sizeof s);
  s.health = health; s.mass = 200; s.can_take_damage = true; s.invulnerable = false; s.team = NULL;
  s.armor.regular.kind = QA_REGULAR_ARMOR_NONE; s.armor.powered.kind = "none";
  return s;
}
static qa_combat_state_result combat_of(size_t index) { return qa_gameplay_authority_read(S.combat, S.ids[index].id); }

static void destroy_world(void) {
  if (!world_built) return;
  qa_q2_source_callbacks_destroy(S.source_callbacks);
  qa_shared_inventory_table_destroy(S.inventory);
  qa_gameplay_authority_destroy(S.combat);
  qa_actor_callback_table_destroy(S.callbacks);
  qa_shared_body_table_destroy(S.bodies);
  qa_session_actor_registry_close(S.registry);
  qa_session_actor_registry_destroy(S.registry);
  qa_identity_owner_destroy(&S.identities);
  world_built = false;
}
/* with_combat_at: index of an actor left without shared combat state (ACTORS = none). */
static void build_world(size_t without_combat) {
  destroy_world();
  memset(&S, 0, sizeof S);
  S.now = 10; S.hit_result = true;
  qa_identity_owner_create(&S.identities, "q2bm-test");
  S.registry = qa_session_actor_registry_create(&S.identities, 64);
  qa_body_link_hooks link = {&link_sentinel, link_bounds, link_noop, unlink_noop};
  S.bodies = qa_shared_body_table_create(S.registry, link);
  S.callbacks = qa_actor_callback_table_create(S.registry);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  S.combat = qa_gameplay_authority_create(S.registry, S.callbacks, hooks);
  S.inventory = qa_shared_inventory_table_create(S.registry);
  S.source_callbacks = qa_q2_source_callbacks_create();
  world_built = true;
  for (size_t i = 0; i < ACTORS; i++) {
    S.ids[i] = qa_session_actor_registry_allocate(S.registry, "q2:game", "q2:test");
    qa_body_state body;
    memset(&body, 0, sizeof body);
    body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
    qa_shared_body_table_create_body(S.bodies, S.ids[i], body);
    if (i >= 1 && i <= 3 && i != without_combat)
      qa_gameplay_authority_create_state(S.combat, S.ids[i], combat_state(100), NULL, NULL);
  }
  S.player_ids[0] = S.ids[7].id;
}
static void reset_world_with(size_t without_combat) {
  build_world(without_combat);
  memset(&host, 0, sizeof host);
  host.ctx = &host_sentinel;
  host.actors = S.registry; host.bodies = S.bodies; host.callbacks = S.callbacks;
  host.combat = S.combat; host.inventory = S.inventory;
  host.now = stub_now; host.random = stub_random; host.frame_seconds = stub_now;
  host.trace = stub_trace; host.point_contents = stub_point_contents;
  host.in_phs = stub_true2; host.areas_connected = stub_true2;
  host.nearby = stub_nearby; host.players = stub_players; host.world_actor = stub_world_actor;
  host.is_player = stub_is_player; host.is_monster = stub_is_monster;
  host.touch_triggers = stub_touch_triggers; host.emit = stub_emit; host.diagnostic = stub_diagnostic;
  memset(&game, 0, sizeof game);
  game.ctx = &game_sentinel; game.host = &host;
  game.options.edition = QA_Q2_EDITION_CLASSIC; game.options.skill = 1; game.options.map_name = "base1";
  game.options.mode = QA_Q2_MODE_SINGLEPLAYER;
  game.source_callbacks = S.source_callbacks;
  S.boss_think[0] = (qa_q2_named_think){"q2:BossExplode", qa_q2bm_boss_explosion_think(&monsters)};
  qa_q2_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.think = S.boss_think; defs.think_count = 1;
  qa_q2_source_callbacks_register(S.source_callbacks, defs);
  game.body = stub_game_body; game.entity = stub_game_entity; game.monster_target = stub_monster_target;
  game.move = stub_game_move; game.remove = stub_game_remove; game.link = stub_game_noop_entity;
  game.show = stub_game_noop_entity; game.cancel = stub_game_cancel; game.schedule = stub_game_schedule;
  game.sound = stub_game_sound; game.create = stub_game_create; game.damage = stub_game_damage;
  game.solid = stub_solid; game.motion = stub_motion;
  /* TS new Q2Entity(actor, {classname}) defaults (solid "none", motion "stationary", ...). */
  qa_q2_spawn_fields self_fields = {.ordinal = -1, .classname = "monster_test"};
  qa_q2_spawn_fields enemy_fields = {.ordinal = -1, .classname = "player"};
  qa_q2_spawn_fields created_fields = {.ordinal = -1, .classname = "created"};
  qa_q2_entity_init(&self_entity, S.ids[1], self_fields);
  qa_q2_entity_init(&enemy_entity, S.ids[2], enemy_fields);
  qa_q2_entity_init(&S.created, S.ids[3], created_fields);
  self_entity.targetname = "self";
  self_entity.max_health = 240; self_entity.view_height = 22; enemy_entity.view_height = 22;
  self_entity.has_enemy = true; self_entity.enemy = enemy_entity.actor.id;
  S.entities[1] = &self_entity; S.entities[2] = &enemy_entity;
  S.entities[3] = &S.created;
  set_origin(1, (qa_vec3){0, 0, 24});
  set_origin(2, (qa_vec3){500, 0, 24});
  S.ray_actor = enemy_entity.actor.id; S.has_ray = true;
  memset(&state, 0, sizeof state);
  state.move = &qa_q2bm_berserk_moves[0];
  state.gib_health = -60;
  memset(&context, 0, sizeof context);
  context.entity = &self_entity; context.game = &game; context.state = &state;
  context.impl_ctx = &impl_sentinel;
  context.set_move = stub_set_move; context.walk = stub_walk;
  context.weapons.ctx = &weapons_sentinel;
  context.weapons.fire_bullet = stub_fire_bullet;
  context.weapons.fire_blaster = stub_fire_blaster;
  context.weapons.fire_hit = stub_fire_hit;
  context.weapons.fire_rocket = stub_fire_rocket;
}
static void reset_world(void) { reset_world_with(ACTORS); }
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

/* ---- tests ---- */
static size_t total_frames(const qa_q2m_move *moves, size_t count) {
  size_t total = 0;
  for (size_t i = 0; i < count; i++) total += moves[i].frame_count;
  return total;
}
static void test_tables(void) {
  CHECK(qa_q2bm_actor_move_count == 11 && total_frames(qa_q2bm_actor_moves, qa_q2bm_actor_move_count) == 127);
  CHECK(qa_q2bm_berserk_move_count == 11 && total_frames(qa_q2bm_berserk_moves, qa_q2bm_berserk_move_count) == 122);
  CHECK(qa_q2bm_boss2_move_count == 11 && total_frames(qa_q2bm_boss2_moves, qa_q2bm_boss2_move_count) == 202);
  CHECK(qa_q2bm_boss31_move_count == 13 && total_frames(qa_q2bm_boss31_moves, qa_q2bm_boss31_move_count) == 202);
  CHECK(qa_q2bm_boss32_move_count == 12 && total_frames(qa_q2bm_boss32_moves, qa_q2bm_boss32_move_count) == 293);
  CHECK(qa_q2bm_brain_move_count == 13 && total_frames(qa_q2bm_brain_moves, qa_q2bm_brain_move_count) == 192);
  CHECK(qa_q2bm_chick_move_count == 17 && total_frames(qa_q2bm_chick_moves, qa_q2bm_chick_move_count) == 211);
  CHECK(qa_q2bm_flipper_move_count == 9 && total_frames(qa_q2bm_flipper_moves, qa_q2bm_flipper_move_count) == 146);
  CHECK(qa_q2bm_float_move_count == 12 && total_frames(qa_q2bm_float_moves, qa_q2bm_float_move_count) == 351);
  CHECK(qa_q2bm_flyer_move_count == 17 && total_frames(qa_q2bm_flyer_moves, qa_q2bm_flyer_move_count) == 241);
  CHECK(qa_q2bm_gladiator_move_count == 8 && total_frames(qa_q2bm_gladiator_moves, qa_q2bm_gladiator_move_count) == 90);
  CHECK(qa_q2bm_gunner_move_count == 14 && total_frames(qa_q2bm_gunner_moves, qa_q2bm_gunner_move_count) == 199);
  CHECK(qa_q2bm_hover_move_count == 16 && total_frames(qa_q2bm_hover_moves, qa_q2bm_hover_move_count) == 275);
  CHECK(qa_q2bm_insane_move_count == 18 && total_frames(qa_q2bm_insane_moves, qa_q2bm_insane_move_count) == 331);
  CHECK(qa_q2bm_medic_move_count == 10 && total_frames(qa_q2bm_medic_moves, qa_q2bm_medic_move_count) == 235);
  CHECK(qa_q2bm_mutant_move_count == 12 && total_frames(qa_q2bm_mutant_moves, qa_q2bm_mutant_move_count) == 142);
  CHECK(qa_q2bm_parasite_move_count == 14 && total_frames(qa_q2bm_parasite_moves, qa_q2bm_parasite_move_count) == 133);
  CHECK(qa_q2bm_supertank_move_count == 15 && total_frames(qa_q2bm_supertank_moves, qa_q2bm_supertank_move_count) == 266);
  CHECK(qa_q2bm_tank_move_count == 19 && total_frames(qa_q2bm_tank_moves, qa_q2bm_tank_move_count) == 285);
  CHECK(qa_q2bm_medic_frames_value.attack42 == 218 && qa_q2bm_medic_frames_value.attack50 == 226);
  CHECK(qa_q2bm_gunner_frames_value.attak216 == 144 && qa_q2bm_gunner_frames_value.attak105 == 112);
  CHECK(qa_q2bm_boss32_frames_value.stand201 == 414 && qa_q2bm_boss32_frames_value.stand260 == 473);
  CHECK(qa_q2bm_tank_frames_value.attak406 == 173 && qa_q2bm_tank_frames_value.attak110 == 64);
  CHECK(qa_q2bm_gladiator_frames_value.stand1 == 0 && qa_q2bm_gladiator_frames_value.death22 == 82);
  CHECK(strcmp(qa_q2bm_berserk_moves[0].name, "berserk_move_stand") == 0);
  /* Floater drops floater_move_activate (30 rows for 31 frames). */
  size_t active_count = 0;
  const qa_q2m_move *active = qa_q2bm_floater_active_moves(&active_count);
  CHECK(active_count == qa_q2bm_float_move_count - 1);
  bool has_activate = false;
  for (size_t i = 0; i < active_count; i++)
    if (strcmp(active[i].name, "floater_move_activate") == 0) has_activate = true;
  CHECK(!has_activate);
}
static void test_definitions(void) {
  reset_world();
  qa_q2m_weapons weapons;
  memset(&weapons, 0, sizeof weapons);
  weapons.ctx = &weapons_sentinel; weapons.fire_bullet = stub_fire_bullet; weapons.fire_blaster = stub_fire_blaster;
  weapons.fire_hit = stub_fire_hit; weapons.fire_rocket = stub_fire_rocket;
  qa_q2m_monster_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  CHECK(qa_q2m_monsters_init(&monsters, weapons, hooks));
  qa_q2bm_classic_base_definitions *defs = malloc(sizeof *defs);
  CHECK(defs != NULL);
  if (!defs) return;
  qa_q2bm_classic_base_monster_definitions(defs, &monsters);
  static const char *const classnames[20] = {
    "monster_berserk", "monster_brain", "monster_chick", "monster_flipper", "monster_floater",
    "monster_flyer", "monster_gladiator", "monster_gunner", "monster_hover", "monster_medic",
    "monster_mutant", "monster_parasite", "monster_supertank", "monster_tank", "monster_tank_commander",
    "monster_boss2", "monster_jorg", "monster_makron", "misc_actor", "misc_insane"};
  static const double healths[20] = {240, 300, 175, 50, 200, 50, 400, 175, 240, 300,
                                     300, 175, 1500, 750, 1000, 2000, 3000, 3000, 100, 100};
  for (size_t i = 0; i < 20; i++) {
    const qa_q2m_definition *d = &defs->definitions[i];
    CHECK(strcmp(d->classname, classnames[i]) == 0);
    CHECK(d->health == healths[i]);
    CHECK(d->has_source_callbacks && d->source_callbacks.think_count >= 1);
    if (d->source_callbacks.think_count >= 1)
      CHECK(strcmp(d->source_callbacks.think[d->source_callbacks.think_count - 1].name, "q2:BossExplode") == 0);
    const qa_q2m_move *initial = NULL;
    for (size_t m = 0; m < d->move_count; m++)
      if (strcmp(d->moves[m].name, d->initial_move) == 0) initial = &d->moves[m];
    CHECK(initial != NULL);
    if (initial) CHECK((double)initial->frame_count >= initial->last_frame - initial->first_frame + 1);
    CHECK(d->reactions.die != NULL);
  }
  CHECK(defs->definitions[17].source_callbacks.think_count == 3);
  CHECK(strcmp(defs->definitions[17].source_callbacks.think[0].name, "q2:base/MakronSpawn") == 0);
  CHECK(strcmp(defs->definitions[17].source_callbacks.think[1].name, "q2:base/makron_torso_think") == 0);
  CHECK(defs->definitions[10].source_callbacks.touch_count == 1);
  CHECK(strcmp(defs->definitions[10].source_callbacks.touch[0].name, "classic_mutant_jump_touch") == 0);
  free(defs);
  /* Registration fills the provider table. */
  qa_q2bm_classic_base_module module;
  CHECK(qa_q2bm_register_classic_base_monsters(&module, &monsters));
  CHECK(monsters.definition_count == 20);
  qa_q2_spawn_module spawn = qa_q2bm_classic_base_as_spawn(&module);
  qa_q2_callback_definitions merged = *spawn.callbacks;
  bool has_boss_think = false, has_actor_use = false;
  for (size_t i = 0; i < merged.think_count; i++)
    if (strcmp(merged.think[i].name, "q2:base/Think_Boss3Stand") == 0) has_boss_think = true;
  for (size_t i = 0; i < merged.use_count; i++)
    if (strcmp(merged.use[i].name, "actor_use") == 0) has_actor_use = true;
  CHECK(has_boss_think);
  CHECK(has_actor_use);
  qa_q2bm_classic_base_module_destroy(&module);
  qa_q2m_monsters_destroy(&monsters);
}
static void test_parasite_reachable(void) {
  /* Oracle values from the TypeScript parasiteDrainReachable. */
  CHECK(qa_q2bm_parasite_drain_reachable((qa_vec3){0, 0, 0}, (qa_vec3){100, 0, 0}) == true);
  CHECK(qa_q2bm_parasite_drain_reachable((qa_vec3){0, 0, 0}, (qa_vec3){256, 0, 0}) == true);
  CHECK(qa_q2bm_parasite_drain_reachable((qa_vec3){0, 0, 0}, (qa_vec3){257, 0, 0}) == false);
  CHECK(qa_q2bm_parasite_drain_reachable((qa_vec3){0, 0, 100}, (qa_vec3){0, 0, 0}) == false);
  CHECK(qa_q2bm_parasite_drain_reachable((qa_vec3){0, 0, 0}, (qa_vec3){100, 0, 50}) == true);
  CHECK(qa_q2bm_parasite_drain_reachable((qa_vec3){0, 0, 0}, (qa_vec3){100, 0, 100}) == false);
  CHECK(qa_q2bm_parasite_drain_reachable((qa_vec3){24, 0, 6}, (qa_vec3){200, 0, 24}) == true);
}
static qa_pain_reaction pain_reaction(double damage) {
  qa_pain_reaction r;
  memset(&r, 0, sizeof r);
  r.damage = damage;
  return r;
}
static qa_death_reaction death_reaction(double damage) {
  qa_death_reaction r;
  memset(&r, 0, sizeof r);
  r.pain = pain_reaction(damage);
  return r;
}
static void test_handlers(void) {
  reset_world();
  /* Berserk pain: gate, sound, move choice. */
  queue_random((double[]){0.4}, 1);
  qa_q2bm_berserk_definition.reactions.pain(NULL, &context, pain_reaction(30));
  CHECK(strcmp(S.last_move, "berserk_move_pain1") == 0);
  CHECK(strcmp(S.last_sound, "berserk/berpain2.wav") == 0);
  CHECK(state.pain_time == 13);
  S.set_move_count = 0; S.now = 11;
  qa_q2bm_berserk_definition.reactions.pain(NULL, &context, pain_reaction(30));
  CHECK(S.set_move_count == 0);
  /* Berserk die without gib: the corpse becomes damageable through the real authority. */
  qa_combat_trait_changes undamageable;
  memset(&undamageable, 0, sizeof undamageable);
  undamageable.has_can_take_damage = true; undamageable.can_take_damage = false;
  qa_gameplay_authority_set_traits(S.combat, S.ids[1], undamageable);
  qa_q2bm_berserk_definition.reactions.die(NULL, &context, death_reaction(60));
  CHECK(strcmp(S.last_move, "berserk_move_death1") == 0);
  CHECK(state.dead == true && combat_of(1).has_value && combat_of(1).value.can_take_damage == true);

  reset_world();
  state.move = &qa_q2bm_insane_moves[0];
  self_entity.spawnflags = 4;
  qa_q2bm_insane_definition.walk.fn(NULL, &context);
  CHECK(strcmp(S.last_move, "insane_move_crawl") == 0);
  self_entity.spawnflags = 8;
  qa_q2bm_insane_definition.initialize.fn(NULL, &context);
  CHECK(state.locomotion == QA_Q2M_LOCO_FLY);
  CHECK(((uint32_t)self_entity.flags & 2048u) != 0);
  CHECK(body_of(1).bounds.min.y == 0 && body_of(1).bounds.max.y == 8);

  reset_world();
  set_origin(2, (qa_vec3){50, 0, 24});
  state.has_melee = true; state.has_ranged_attack = true;
  CHECK(qa_q2bm_boss_check_attack_default(&context) == true);
  CHECK(state.attack_state == QA_Q2M_ATTACK_MELEE);
  state.has_melee = false;
  CHECK(qa_q2bm_boss_check_attack_default(&context) == true);
  CHECK(state.attack_state == QA_Q2M_ATTACK_MISSILE);

  reset_world();
  state.move = &qa_q2bm_gunner_moves[0];
  self_entity.frame = qa_q2bm_gunner_frames_value.attak216;
  dispatch(&qa_q2bm_gunner_definition, "GunnerFire");
  CHECK(S.bullet_count == 1 && S.bullet_damage == 3);
  CHECK(S.muzzle_count == 1 && S.last_flash == 45);

  reset_world();
  state.move = &qa_q2bm_flyer_moves[0];
  dispatch(&qa_q2bm_flyer_definition, "flyer_setstart");
  CHECK(strcmp(S.last_move, "flyer_move_start") == 0);
  dispatch(&qa_q2bm_flyer_definition, "flyer_nextmove");
  CHECK(strcmp(S.last_move, "flyer_move_run") == 0);

  reset_world();
  state.move = &qa_q2bm_boss32_moves[0];
  qa_q2_spawn_module boss3 = qa_q2bm_boss3_stand_module();
  self_entity.classname = "monster_boss3_stand";
  CHECK(boss3.spawn(boss3.ctx, &self_entity, &game) == true);
  CHECK(strcmp(self_entity.model, "models/monsters/boss3/rider/tris.md2") == 0);
  CHECK(self_entity.frame == 414);
  CHECK(S.schedule_count == 1);
}
static void test_brain_power(void) {
  reset_world();
  state.move = &qa_q2bm_brain_moves[0];
  /* Without shared combat state, game.host.combat.setPoweredProtection throws (TS GameplayAuthority). */
  reset_world_with(1);
  state.move = &qa_q2bm_brain_moves[0];
  CHECK_ABORTS(qa_q2bm_brain_definition.initialize.fn(NULL, &context));
  reset_world();
  state.move = &qa_q2bm_brain_moves[0];
  CHECK(!qa_shared_inventory_table_has(S.inventory, S.ids[1].id));
  qa_q2bm_brain_definition.initialize.fn(NULL, &context);
  /* inventory.create(actor, []) then configure(q2:monster-power 100/100). */
  CHECK(qa_shared_inventory_table_has(S.inventory, S.ids[1].id));
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(S.inventory, S.ids[1].id, &entry_count);
  CHECK(entry_count == 1 && strcmp(entries[0].item, "q2:monster-power") == 0 && entries[0].count == 100
        && entries[0].capacity == 100);
  /* bindPowerArmorCells ran once: a second binding for the actor throws. */
  qa_power_armor_cell_binding probe;
  memset(&probe, 0, sizeof probe);
  CHECK_ABORTS(qa_gameplay_authority_bind_power_armor_cells(S.combat, S.ids[1], probe));
  qa_combat_state_result powered = combat_of(1);
  CHECK(powered.has_value && strcmp(powered.value.armor.powered.kind, "screen") == 0
        && powered.value.armor.powered.cells == 100);
  dispatch(&qa_q2bm_brain_definition, "brain_chest_open");
  CHECK(strcmp(combat_of(1).value.armor.powered.kind, "none") == 0);
  CHECK(strcmp(S.last_sound, "brain/brnatck1.wav") == 0);
  /* Second initialize does not rebind the same actor (a rebind would throw). */
  qa_q2bm_brain_definition.initialize.fn(NULL, &context);
  CHECK(strcmp(combat_of(1).value.armor.powered.kind, "screen") == 0);
  /* Boss explosion without a restored monster context aborts. */
  reset_world();
  qa_q2m_weapons weapons;
  memset(&weapons, 0, sizeof weapons);
  qa_q2m_monster_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  CHECK(qa_q2m_monsters_init(&monsters, weapons, hooks));
  CHECK_ABORTS(qa_q2bm_boss_explosion_think_fn(&monsters, &self_entity, &game));
  qa_q2m_monsters_destroy(&monsters);
  destroy_world();
}

int main(void) {
  test_tables();
  test_definitions();
  test_parasite_reachable();
  test_handlers();
  test_brain_power();
  printf("test_q2_monsters: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
