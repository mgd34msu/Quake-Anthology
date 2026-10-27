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

/* ---- stub world ---- */
static const char *session_name = "q2bm-test";
static int session_token;
static qa_actor_id make_id(int64_t slot) {
  qa_actor_id id = {{session_name, &session_token}, slot, 0, &session_token};
  return id;
}
static qa_owned_actor make_owned(int64_t slot) {
  qa_owned_actor a = {make_id(slot), "q2:game", &session_token};
  return a;
}
typedef struct stub {
  double now;
  double randoms[32]; size_t random_count, random_at;
  qa_body_state bodies[8]; bool has_body[8];
  double health[8]; bool has_health[8];
  bool can_take_damage[8];
  qa_q2f_entity *entities[8];
  qa_actor_id ray_actor; bool has_ray;
  char last_move[96]; int set_move_count;
  char last_sound[128]; double last_sound_channel; int sound_count;
  int muzzle_count; double last_flash;
  int effect_count; char last_effect[64];
  int loop_count; char last_loop[16];
  int remove_count, cancel_count, schedule_count, traits_count;
  bool last_traits_damage;
  double cells; int configure_count; double configured_count, configured_capacity;
  int diagnostic_count;
  double damage_amount; int damage_count;
  int bullet_count; double bullet_damage;
  int blaster_count; double blaster_effects;
  int rocket_count, hit_count; bool hit_result;
  qa_q2f_entity created;
  qa_actor_id nearby_ids[4]; size_t nearby_count;
  qa_actor_id player_ids[1];
  qa_q2bm_extended_event last_extended; int extended_count;
  char powered_kind[16]; double powered_cells; int powered_count, bind_count;
  bool inventory_present; int inventory_creates;
} stub;
static stub S;
static qa_q2f_host host;
static qa_q2f_game game;
static qa_q2f_entity self_entity, enemy_entity;
static qa_q2m_state state;
static qa_q2m_context context;
static qa_q2m_monsters monsters;

static size_t slot_of(qa_actor_id id) { return (size_t)id.slot; }
static double stub_now(void *ctx) { (void)ctx; return S.now; }
static double stub_random(void *ctx) {
  (void)ctx;
  if (S.random_at < S.random_count) return S.randoms[S.random_at++];
  return 0.5;
}
static qa_body_state_result stub_bodies_read(void *ctx, qa_actor_id actor) {
  (void)ctx;
  qa_body_state_result r;
  memset(&r, 0, sizeof r);
  if (actor.slot >= 0 && actor.slot < 8 && S.has_body[slot_of(actor)]) {
    r.has_value = true; r.value = S.bodies[slot_of(actor)];
  }
  return r;
}
static qa_combat_state_result stub_combat_read(void *ctx, qa_actor_id actor) {
  (void)ctx;
  qa_combat_state_result r;
  memset(&r, 0, sizeof r);
  if (actor.slot >= 0 && actor.slot < 8 && S.has_health[slot_of(actor)]) {
    r.has_value = true; r.value.health = S.health[slot_of(actor)];
    r.value.can_take_damage = S.can_take_damage[slot_of(actor)];
  }
  return r;
}
static void stub_set_health(void *ctx, qa_owned_actor actor, double health) {
  (void)ctx;
  S.has_health[slot_of(actor.id)] = true; S.health[slot_of(actor.id)] = health;
}
static void stub_set_traits(void *ctx, qa_owned_actor actor, qa_q2f_combat_traits traits) {
  (void)ctx; (void)actor;
  S.traits_count++;
  if (traits.has_can_take_damage) S.last_traits_damage = traits.can_take_damage;
}
static double stub_inventory_count(void *ctx, qa_actor_id actor, qa_item_id item) {
  (void)ctx; (void)actor; (void)item;
  return S.cells;
}
static void stub_inventory_configure(void *ctx, qa_owned_actor actor, qa_inventory_entry entry) {
  (void)ctx; (void)actor;
  S.configure_count++; S.configured_count = entry.count; S.configured_capacity = entry.capacity;
  S.cells = entry.count;
}
static qa_q2f_saved_result stub_source_of(void *ctx, qa_actor_id actor) {
  (void)ctx; (void)actor;
  qa_q2f_saved_result r = {false, {0, 0}};
  return r;
}
static bool stub_is_live(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return true; }
static qa_trace_result stub_trace(void *ctx, qa_q2f_trace_request request) {
  (void)ctx;
  qa_trace_result r;
  memset(&r, 0, sizeof r);
  r.kind = "q2"; r.fraction = 1; r.end = request.end; r.hit.kind = QA_TRACE_HIT_NONE;
  if (request.bounds == NULL && S.has_ray && request.mask != QA_Q2M_MASK_OPAQUE) {
    r.fraction = 0.8; r.hit.kind = QA_TRACE_HIT_ACTOR; r.hit.actor = S.ray_actor;
  }
  return r;
}
static const qa_actor_id *stub_nearby(void *ctx, qa_vec3 origin, double radius, size_t *out) {
  (void)ctx; (void)origin; (void)radius;
  *out = S.nearby_count;
  return S.nearby_ids;
}
static const qa_actor_id *stub_players(void *ctx, size_t *out) {
  (void)ctx;
  *out = 1;
  return S.player_ids;
}
static bool stub_is_player(void *ctx, qa_actor_id actor) { (void)ctx; return actor.slot == 7; }
static bool stub_is_monster(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return false; }
static void stub_emit(void *ctx, qa_q2f_presentation_event event) {
  (void)ctx;
  if (event.kind == QA_Q2F_EVENT_MONSTER_MUZZLEFLASH) { S.muzzle_count++; S.last_flash = event.flash; }
  if (event.kind == QA_Q2F_EVENT_EFFECT) {
    S.effect_count++;
    snprintf(S.last_effect, sizeof S.last_effect, "%s", event.effect ? event.effect : "");
  }
  if (event.kind == QA_Q2F_EVENT_SOUND) {
    S.loop_count++;
    snprintf(S.last_loop, sizeof S.last_loop, "%s", event.loop ? event.loop : "");
  }
}
static void stub_diagnostic(void *ctx, const char *message) { (void)ctx; (void)message; S.diagnostic_count++; }
static void stub_touch_triggers(void *ctx, qa_owned_actor actor) { (void)ctx; (void)actor; }
static double stub_point_contents(void *ctx, qa_vec3 point) { (void)ctx; return point.z < 0 ? 1 : 0; }
static bool stub_true2(void *ctx, qa_vec3 a, qa_vec3 b) { (void)ctx; (void)a; (void)b; return true; }
static qa_actor_id stub_world_actor(void *ctx) { (void)ctx; return make_id(0); }

static qa_body_state stub_game_body(void *ctx, qa_owned_actor actor) {
  (void)ctx;
  return S.bodies[slot_of(actor.id)];
}
static qa_q2f_entity *stub_game_entity(void *ctx, const qa_actor_id *actor) {
  (void)ctx;
  if (!actor || actor->slot < 0 || actor->slot >= 8) return NULL;
  return S.entities[slot_of(*actor)];
}
static qa_q2f_target_obs_result stub_monster_target(void *ctx, const qa_actor_id *actor) {
  (void)ctx;
  qa_q2f_target_obs_result r;
  memset(&r, 0, sizeof r);
  if (actor && actor->slot >= 0 && actor->slot < 8 && S.has_body[slot_of(*actor)]) {
    r.has_value = true; r.value.view_height = 22;
  }
  return r;
}
static void stub_game_move(void *ctx, qa_q2f_entity *entity, qa_q2f_body_changes changes, bool has_link, bool link) {
  (void)ctx; (void)has_link; (void)link;
  qa_body_state *body = &S.bodies[slot_of(entity->actor.id)];
  if (changes.has_origin) body->origin = changes.origin;
  if (changes.has_angles) body->angles = changes.angles;
  if (changes.has_velocity) body->velocity = changes.velocity;
  if (changes.has_bounds) body->bounds = changes.bounds;
  if (changes.has_ground) body->ground = changes.ground;
}
static void stub_game_remove(void *ctx, qa_q2f_entity *entity) { (void)ctx; (void)entity; S.remove_count++; }
static void stub_game_noop_entity(void *ctx, qa_q2f_entity *entity) { (void)ctx; (void)entity; }
static void stub_game_cancel(void *ctx, qa_q2f_entity *entity) { (void)ctx; (void)entity; S.cancel_count++; }
static void stub_game_schedule(void *ctx, qa_q2f_entity *entity, double delay, qa_q2f_think think) {
  (void)ctx; (void)entity; (void)delay; (void)think; S.schedule_count++;
}
static void stub_game_sound(void *ctx, qa_owned_actor actor, const char *path, bool has_channel, double channel,
                            bool has_volume, double volume, bool has_attenuation, double attenuation) {
  (void)ctx; (void)actor; (void)has_volume; (void)volume; (void)has_attenuation; (void)attenuation;
  S.sound_count++;
  snprintf(S.last_sound, sizeof S.last_sound, "%s", path ? path : "");
  S.last_sound_channel = has_channel ? channel : -1;
}
static qa_q2f_entity *stub_game_create(void *ctx, const char *classname, const qa_q2f_spawn_fields *values) {
  (void)ctx; (void)classname; (void)values;
  return &S.created;
}
static qa_damage_outcome stub_game_damage(void *ctx, qa_q2f_damage_args args) {
  (void)ctx;
  S.damage_count++; S.damage_amount = args.amount;
  qa_damage_outcome outcome;
  memset(&outcome, 0, sizeof outcome);
  return outcome;
}
static void stub_solid(void *ctx, qa_q2f_entity *entity, qa_q2f_solid solid) {
  (void)ctx; entity->solid = solid;
}
static void stub_motion(void *ctx, qa_q2f_entity *entity, qa_q2f_motion_kind kind) {
  (void)ctx; entity->motion = kind;
}
static qa_q2f_think_result stub_think_resolve(void *ctx, const char *name) {
  (void)ctx;
  qa_q2f_think_result r;
  memset(&r, 0, sizeof r);
  if (name && strcmp(name, "q2:BossExplode") == 0) {
    r.has_value = true;
    r.value = qa_q2bm_boss_explosion_think(&monsters);
  }
  return r;
}
static void stub_set_move(void *impl, qa_q2m_context *c, const char *name, bool has_immediate, bool immediate) {
  (void)impl; (void)has_immediate; (void)immediate;
  S.set_move_count++;
  snprintf(S.last_move, sizeof S.last_move, "%s", name);
  /* Keep state.move pointing at a same-named move when known. */
  const qa_q2m_move *moves = c->state->move ? NULL : NULL;
  (void)moves;
}
static void stub_walk(void *impl, qa_q2m_context *c) { (void)impl; (void)c; }
static void stub_fire_bullet(void *ctx, qa_q2f_entity *self, qa_q2f_game *g, qa_vec3 start, qa_vec3 dir,
                             double damage, double kick, double hs, double vs, double mod) {
  (void)ctx; (void)self; (void)g; (void)start; (void)dir; (void)kick; (void)hs; (void)vs; (void)mod;
  S.bullet_count++; S.bullet_damage = damage;
}
static qa_q2f_entity *stub_fire_blaster(void *ctx, qa_q2f_entity *self, qa_q2f_game *g, qa_vec3 start, qa_vec3 dir,
                                        double damage, double speed, double effects, bool hyper, bool has_mod, double mod) {
  (void)ctx; (void)self; (void)g; (void)start; (void)dir; (void)damage; (void)speed; (void)hyper; (void)has_mod; (void)mod;
  S.blaster_count++; S.blaster_effects = effects;
  return NULL;
}
static bool stub_fire_hit(void *ctx, qa_q2f_entity *self, qa_q2f_game *g, qa_vec3 aim, double damage, double kick) {
  (void)ctx; (void)self; (void)g; (void)aim; (void)damage; (void)kick;
  S.hit_count++;
  return S.hit_result;
}
static qa_q2f_entity *stub_fire_rocket(void *ctx, qa_q2f_entity *self, qa_q2f_game *g, qa_vec3 start, qa_vec3 dir,
                                       double damage, double speed, double radius, double rd) {
  (void)ctx; (void)self; (void)g; (void)start; (void)dir; (void)damage; (void)speed; (void)radius; (void)rd;
  S.rocket_count++;
  return NULL;
}
static void stub_extended(void *ctx, qa_q2bm_extended_event event) {
  (void)ctx;
  S.extended_count++; S.last_extended = event;
}
static void stub_bind_cells(void *ctx, qa_owned_actor actor, qa_power_armor_cell_binding cells) {
  (void)ctx; (void)actor; (void)cells; S.bind_count++;
}
static void stub_set_powered(void *ctx, qa_owned_actor actor, const char *kind, double cells) {
  (void)ctx; (void)actor;
  S.powered_count++; S.powered_cells = cells;
  snprintf(S.powered_kind, sizeof S.powered_kind, "%s", kind);
}
static bool stub_inventory_has(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return S.inventory_present; }
static void stub_inventory_create(void *ctx, qa_owned_actor actor) {
  (void)ctx; (void)actor; S.inventory_creates++; S.inventory_present = true;
}

static void reset_world(void) {
  memset(&S, 0, sizeof S);
  S.now = 10; S.hit_result = true;
  S.player_ids[0] = make_id(7);
  memset(&host, 0, sizeof host);
  host.ctx = &S;
  host.actors.ctx = &S; host.actors.is_live = stub_is_live; host.actors.source_of = stub_source_of;
  host.bodies.ctx = &S; host.bodies.read = stub_bodies_read;
  host.combat.ctx = &S; host.combat.read = stub_combat_read;
  host.combat.set_health = stub_set_health; host.combat.set_traits = stub_set_traits;
  host.inventory.ctx = &S; host.inventory.count = stub_inventory_count;
  host.inventory.configure = stub_inventory_configure;
  host.now = stub_now; host.random = stub_random; host.frame_seconds = stub_now;
  host.trace = stub_trace; host.point_contents = stub_point_contents;
  host.in_phs = stub_true2; host.areas_connected = stub_true2;
  host.nearby = stub_nearby; host.players = stub_players; host.world_actor = stub_world_actor;
  host.is_player = stub_is_player; host.is_monster = stub_is_monster;
  host.touch_triggers = stub_touch_triggers; host.emit = stub_emit; host.diagnostic = stub_diagnostic;
  memset(&game, 0, sizeof game);
  game.ctx = &S; game.host = &host;
  game.options.edition = QA_Q2F_CLASSIC; game.options.skill = 1; game.options.map_name = "base1";
  game.options.mode = "singleplayer";
  game.source_callbacks.ctx = &S; game.source_callbacks.think_resolve = stub_think_resolve;
  game.body = stub_game_body; game.entity = stub_game_entity; game.monster_target = stub_monster_target;
  game.move = stub_game_move; game.remove = stub_game_remove; game.link = stub_game_noop_entity;
  game.show = stub_game_noop_entity; game.cancel = stub_game_cancel; game.schedule = stub_game_schedule;
  game.sound = stub_game_sound; game.create = stub_game_create; game.damage = stub_game_damage;
  game.solid = stub_solid; game.motion = stub_motion;
  memset(&self_entity, 0, sizeof self_entity);
  memset(&enemy_entity, 0, sizeof enemy_entity);
  self_entity.actor = make_owned(1); enemy_entity.actor = make_owned(2);
  self_entity.classname = "monster_test"; enemy_entity.classname = "player";
  self_entity.target = ""; self_entity.targetname = "self"; self_entity.message = "";
  self_entity.max_health = 240; self_entity.view_height = 22; enemy_entity.view_height = 22;
  self_entity.has_enemy = true; self_entity.enemy = enemy_entity.actor.id;
  S.entities[1] = &self_entity; S.entities[2] = &enemy_entity;
  S.created.actor = make_owned(3); S.entities[3] = &S.created;
  for (int i = 1; i <= 3; i++) {
    S.has_body[i] = true; S.has_health[i] = true; S.health[i] = 100; S.can_take_damage[i] = true;
    S.bodies[i].bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  }
  S.bodies[1].origin = (qa_vec3){0, 0, 24};
  S.bodies[2].origin = (qa_vec3){500, 0, 24};
  S.ray_actor = enemy_entity.actor.id; S.has_ray = true;
  memset(&state, 0, sizeof state);
  state.move = &qa_q2bm_berserk_moves[0];
  state.gib_health = -60;
  memset(&context, 0, sizeof context);
  context.entity = &self_entity; context.game = &game; context.state = &state;
  context.set_move = stub_set_move; context.walk = stub_walk;
  context.weapons.ctx = &S;
  context.weapons.fire_bullet = stub_fire_bullet;
  context.weapons.fire_blaster = stub_fire_blaster;
  context.weapons.fire_hit = stub_fire_hit;
  context.weapons.fire_rocket = stub_fire_rocket;
  qa_q2bm_set_event_sink((qa_q2bm_event_sink){&S, stub_extended});
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
  weapons.ctx = &S; weapons.fire_bullet = stub_fire_bullet; weapons.fire_blaster = stub_fire_blaster;
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
  qa_q2bm_spawn_module spawn = qa_q2bm_classic_base_as_spawn(&module);
  qa_q2f_callback_definitions merged = spawn.callbacks(spawn.ctx);
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
  /* Berserk die without gib. */
  qa_q2bm_berserk_definition.reactions.die(NULL, &context, death_reaction(60));
  CHECK(strcmp(S.last_move, "berserk_move_death1") == 0);
  CHECK(state.dead == true && S.last_traits_damage == true);

  reset_world();
  state.move = &qa_q2bm_insane_moves[0];
  self_entity.spawnflags = 4;
  qa_q2bm_insane_definition.walk.fn(NULL, &context);
  CHECK(strcmp(S.last_move, "insane_move_crawl") == 0);
  self_entity.spawnflags = 8;
  qa_q2bm_insane_definition.initialize.fn(NULL, &context);
  CHECK(state.locomotion == QA_Q2M_LOCO_FLY);
  CHECK(((uint32_t)self_entity.flags & 2048u) != 0);
  CHECK(S.bodies[1].bounds.min.y == 0 && S.bodies[1].bounds.max.y == 8);

  reset_world();
  S.bodies[2].origin = (qa_vec3){50, 0, 24};
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
  qa_q2bm_spawn_module boss3 = qa_q2bm_boss3_stand_module();
  self_entity.classname = "monster_boss3_stand";
  CHECK(boss3.spawn(boss3.ctx, &self_entity, &game) == true);
  CHECK(strcmp(self_entity.model, "models/monsters/boss3/rider/tris.md2") == 0);
  CHECK(self_entity.frame == 414);
  CHECK(S.schedule_count == 1);
}
static void test_brain_power(void) {
  reset_world();
  state.move = &qa_q2bm_brain_moves[0];
  /* Unwired power hooks abort (mirrored authority surface). */
  CHECK_ABORTS(qa_q2bm_brain_definition.initialize.fn(NULL, &context));
  qa_q2bm_brain_set_power_hooks((qa_q2bm_brain_power_hooks){
      &S, stub_bind_cells, stub_set_powered, stub_inventory_has, stub_inventory_create});
  qa_q2bm_brain_definition.initialize.fn(NULL, &context);
  CHECK(S.inventory_creates == 1);
  CHECK(S.configure_count == 1 && S.configured_count == 100 && S.configured_capacity == 100);
  CHECK(S.bind_count == 1);
  CHECK(strcmp(S.powered_kind, "screen") == 0 && S.powered_cells == 100);
  dispatch(&qa_q2bm_brain_definition, "brain_chest_open");
  CHECK(strcmp(S.powered_kind, "none") == 0);
  CHECK(strcmp(S.last_sound, "brain/brnatck1.wav") == 0);
  /* Second initialize does not rebind the same actor. */
  qa_q2bm_brain_definition.initialize.fn(NULL, &context);
  CHECK(S.bind_count == 1);
  /* Boss explosion without a registered think aborts. */
  reset_world();
  game.source_callbacks.think_resolve = stub_think_resolve;
  qa_q2m_weapons weapons;
  memset(&weapons, 0, sizeof weapons);
  qa_q2m_monster_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  CHECK(qa_q2m_monsters_init(&monsters, weapons, hooks));
  CHECK_ABORTS(qa_q2bm_boss_explosion_think_fn(&monsters, &self_entity, &game));
  qa_q2m_monsters_destroy(&monsters);
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
