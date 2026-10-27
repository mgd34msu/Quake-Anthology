/*
 * Port of key cases from tests/gameplay/q1-addons/monsters/heavy/
 * source.test.ts and tests/gameplay/q1-addons/common.test.ts (the TS
 * originals are retail-asset integration tests; the cases here keep
 * the same observable counts/values against a stub host), plus
 * direct checks of the ported frame tables, MG3 AI helpers,
 * navigation, startup, path targets and registries.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q1/addons/monsters.h"

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
      (void)prctl(PR_SET_DUMPABLE, 0);                                  \
      (void)freopen("/dev/null", "w", stderr);                          \
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

/* ---- stub host ---- */
#define MAX_ACTORS 512
#define MAX_HANDLERS 256
static const int token_cell = 0;
typedef struct stub {
  qa_q1am_game game;
  qa_q1am_base base;
  qa_q1am_context context;
  qa_q1am_actor actors[MAX_ACTORS];
  bool alive[MAX_ACTORS];
  qa_q1am_body bodies[MAX_ACTORS];
  double health[MAX_ACTORS];
  bool can_damage[MAX_ACTORS];
  qa_q1am_player player_states[8];
  qa_actor_id player_ids[8];
  size_t player_count;
  size_t next_slot;
  double random_queue[64];
  size_t random_head, random_tail;
  double random_fallback;
  qa_q1am_trace trace_result;
  struct { char name[160]; qa_q1am_handlers handlers; } named[MAX_HANDLERS];
  size_t named_count;
  struct {
    char classname[96];
    void (*fn)(void *, qa_q1am_game *, qa_q1am_actor *);
    void *ctx;
  } spawns[64];
  size_t spawn_count;
  int sound_count, damage_count, emit_count, services_emit_count, create_count;
  char last_sound[128];
  double last_damage;
  qa_actor_id last_damage_target;
  double campaign_flags;
  int walk_move_result;
  char played[64][80];
  size_t played_count;
} stub;
static stub S;
static qa_q1am_actor *world_actor;
static qa_actor_id make_id(int64_t slot) {
  qa_actor_id id = {{ "test", &token_cell}, slot, 0, &token_cell};
  return id;
}
static qa_owned_actor make_owned(int64_t slot) {
  qa_owned_actor owned = {make_id(slot), "q1:official", &token_cell};
  return owned;
}
static qa_q1am_actor *stub_create(void *ctx, const char *classname) {
  (void)ctx;
  size_t slot = S.next_slot++;
  qa_q1am_actor *actor = &S.actors[slot];
  qa_q1am_actor_init(actor, make_owned((int64_t)slot), classname);
  S.alive[slot] = true;
  S.bodies[slot] = (qa_q1am_body){0};
  S.create_count++;
  return actor;
}
static size_t stub_entity_count(void *ctx) { (void)ctx; return S.next_slot; }
static qa_q1am_actor *stub_entity_at(void *ctx, size_t index) {
  (void)ctx;
  return S.alive[index] ? &S.actors[index] : NULL;
}
static const qa_monster_mission *stub_mission(void *ctx, qa_actor_id actor) {
  (void)ctx; (void)actor; return NULL;
}
static qa_q1am_handlers *find_handlers(const char *name) {
  for (size_t i = 0; i < S.named_count; i++)
    if (strcmp(S.named[i].name, name) == 0) return &S.named[i].handlers;
  return NULL;
}
static void stub_named_register(void *ctx, const char *name, qa_q1am_handlers handlers) {
  (void)ctx;
  (void)snprintf(S.named[S.named_count].name, 160, "%s", name);
  S.named[S.named_count].handlers = handlers;
  S.named_count++;
}
static qa_q1am_callback named_of(const char *name) {
  qa_q1am_callback callback = {true, ""};
  (void)snprintf(callback.name, sizeof callback.name, "%s", name);
  return callback;
}
static qa_q1am_callback stub_named_action(void *c, qa_q1am_actor *e, const char *n) { (void)c; (void)e; return named_of(n); }
static qa_q1am_callback stub_named_use(void *c, qa_q1am_actor *e, const char *n) { (void)c; (void)e; return named_of(n); }
static qa_q1am_callback stub_named_touch(void *c, qa_q1am_actor *e, const char *n) { (void)c; (void)e; return named_of(n); }
static qa_q1am_callback stub_named_pain(void *c, qa_q1am_actor *e, const char *n) { (void)c; (void)e; return named_of(n); }
static qa_q1am_callback stub_named_die(void *c, qa_q1am_actor *e, const char *n) { (void)c; (void)e; return named_of(n); }
static void stub_call_action(void *ctx, qa_q1am_actor *entity, qa_q1am_callback callback) {
  (void)ctx;
  qa_q1am_handlers *h = find_handlers(callback.name);
  if (h && h->action) h->action(h->ctx, &S.game, entity);
}
static const char *stub_callback_name(void *ctx, bool present, const qa_q1am_callback *callback) {
  (void)ctx;
  return present ? callback->name : NULL;
}
static void stub_register_spawn(void *ctx, const char *classname,
                                void (*fn)(void *, qa_q1am_game *, qa_q1am_actor *), void *fn_ctx) {
  (void)ctx;
  (void)snprintf(S.spawns[S.spawn_count].classname, 96, "%s", classname);
  S.spawns[S.spawn_count].fn = fn;
  S.spawns[S.spawn_count].ctx = fn_ctx;
  S.spawn_count++;
}
static void stub_noop_ext(void *ctx, qa_q1am_state_extension ext) { (void)ctx; (void)ext; }
static void stub_noop_effects(void *ctx, const char *id, qa_q1am_damage_effects e) { (void)ctx; (void)id; (void)e; }
static void stub_spawn_entity(void *ctx, qa_q1am_actor *entity) {
  (void)ctx;
  for (size_t i = 0; i < S.spawn_count; i++)
    if (strcmp(S.spawns[i].classname, entity->classname) == 0)
      S.spawns[i].fn(S.spawns[i].ctx, &S.game, entity);
}
static void stub_remove(void *ctx, qa_q1am_actor *entity) { (void)ctx; S.alive[entity->actor.id.slot] = false; }
static bool stub_live(void *ctx, qa_q1am_actor *entity) { (void)ctx; return S.alive[entity->actor.id.slot]; }
static qa_q1am_actor *stub_entity(void *ctx, bool has, qa_actor_id actor) {
  (void)ctx;
  if (!has || actor.slot < 0 || actor.slot >= MAX_ACTORS || !S.alive[actor.slot]) return NULL;
  return &S.actors[actor.slot];
}
static bool stub_is_player(void *ctx, bool has, qa_actor_id actor) {
  (void)ctx;
  if (!has) return false;
  for (size_t i = 0; i < S.player_count; i++)
    if (qa_same_actor(S.player_ids[i], actor)) return true;
  return false;
}
static double stub_health(void *ctx, qa_actor_id actor) { (void)ctx; return S.health[actor.slot]; }
static qa_q1am_body stub_body(void *ctx, qa_q1am_actor *entity) { (void)ctx; return S.bodies[entity->actor.id.slot]; }
static void stub_set_body(void *ctx, qa_q1am_actor *entity, const qa_q1am_body_patch *patch) {
  (void)ctx;
  qa_q1am_body *body = &S.bodies[entity->actor.id.slot];
  if (patch->has_origin) body->origin = patch->origin;
  if (patch->has_angles) body->angles = patch->angles;
  if (patch->has_velocity) body->velocity = patch->velocity;
  if (patch->has_bounds) body->bounds = patch->bounds;
  if (patch->has_ground) {
    body->has_ground = !patch->ground_null;
    body->ground = patch->ground;
  }
}
static void stub_set_origin(void *ctx, qa_q1am_actor *entity, qa_vec3 origin) {
  (void)ctx; S.bodies[entity->actor.id.slot].origin = origin;
}
static void stub_set_bounds(void *ctx, qa_q1am_actor *entity, qa_bounds bounds) {
  (void)ctx; S.bodies[entity->actor.id.slot].bounds = bounds;
}
static void stub_link(void *ctx, qa_q1am_actor *entity) { (void)ctx; (void)entity; }
static void stub_schedule(void *ctx, qa_q1am_actor *entity, double delay, qa_q1am_callback cb) {
  (void)ctx;
  entity->think = cb;
  entity->next_think = S.game.time + delay;
}
static void stub_sound(void *ctx, qa_q1am_actor *entity, const char *path, const char *channel, double att) {
  (void)ctx; (void)entity; (void)channel; (void)att;
  S.sound_count++;
  (void)snprintf(S.last_sound, sizeof S.last_sound, "%s", path);
}
static void stub_effect(void *ctx, const char *e, qa_vec3 o, bool h, qa_actor_id a, double amt) {
  (void)ctx; (void)e; (void)o; (void)h; (void)a; (void)amt;
}
static qa_q1am_actor **stub_find(void *ctx, const char *targetname, size_t *out_count) {
  (void)ctx;
  qa_q1am_actor **out = calloc(MAX_ACTORS, sizeof *out);
  size_t n = 0;
  if (targetname[0] != '\0')
    for (size_t i = 0; i < S.next_slot; i++)
      if (S.alive[i] && strcmp(S.actors[i].targetname, targetname) == 0) out[n++] = &S.actors[i];
  *out_count = n;
  return out;
}
static void stub_use_targets(void *ctx, qa_q1am_actor *e, bool h, qa_actor_id a) { (void)ctx; (void)e; (void)h; (void)a; }
static void stub_damage(void *ctx, qa_actor_id target, qa_actor_id inf, bool h, qa_actor_id att, double amount) {
  (void)ctx; (void)inf; (void)h; (void)att;
  S.damage_count++;
  S.last_damage = amount;
  S.last_damage_target = target;
  S.health[target.slot] -= amount;
}
static bool stub_can_damage(void *ctx, qa_actor_id t, qa_actor_id i) { (void)ctx; (void)t; (void)i; return true; }
static void stub_radius_damage(void *ctx, qa_actor_id i, bool h, qa_actor_id a, double amt, bool hi, qa_actor_id ig, const char *w) {
  (void)ctx; (void)i; (void)h; (void)a; (void)amt; (void)hi; (void)ig; (void)w;
}
static qa_q1am_basis stub_make_vectors(void *ctx, qa_vec3 angles) {
  (void)ctx;
  double pitch = angles.x * M_PI / 180, yaw = angles.y * M_PI / 180;
  qa_q1am_basis basis;
  basis.forward = (qa_vec3){cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), -sin(pitch)};
  basis.right = qa_q1am_normalize(qa_cross3_double(basis.forward, (qa_vec3){0, 0, 1}));
  if (qa_length3(basis.right) == 0) basis.right = (qa_vec3){0, -1, 0};
  basis.up = qa_cross3_double(basis.right, basis.forward);
  return basis;
}
static qa_q1am_player *stub_player(void *ctx, qa_actor_id actor) {
  (void)ctx;
  for (size_t i = 0; i < S.player_count; i++)
    if (qa_same_actor(S.player_ids[i], actor)) return &S.player_states[i];
  return NULL;
}
static bool stub_monster_target(void *ctx, qa_actor_id actor, qa_monster_target_observation *out) {
  (void)ctx; (void)actor;
  *out = (qa_monster_target_observation){25, false, false, false, 0, false, 0};
  return true;
}
static const qa_q1am_frame stub_base_frames[] = {
  {"zombie_paine1", 174, "zombie_paine2", NULL, 0},
  {"ogre_stand5", 4, "ogre_stand6", NULL, 0},
  {"ogre_walk3", 20, "ogre_walk4", NULL, 0},
  {"ogre_run1", 40, "ogre_run2", NULL, 0},
  {"ogre_nail1", 50, "ogre_nail2", NULL, 0},
  {"ogre_nail4", 53, "ogre_nail5", NULL, 0},
  {"ogre_nail5", 54, "ogre_nail6", NULL, 0},
  {"ogre_die3", 60, "ogre_die4", NULL, 0},
  {"ogre_bdie3", 70, "ogre_bdie4", NULL, 0},
};
static const qa_q1am_frame *stub_base_frame(void *ctx, const char *name) {
  (void)ctx;
  for (size_t i = 0; i < sizeof stub_base_frames / sizeof stub_base_frames[0]; i++)
    if (strcmp(stub_base_frames[i].name, name) == 0) return &stub_base_frames[i];
  return NULL;
}
static double stub_random(void *ctx) {
  (void)ctx;
  if (S.random_head < S.random_tail) return S.random_queue[S.random_head++];
  return S.random_fallback;
}
static qa_q1am_trace stub_trace(void *ctx, const qa_q1am_trace_request *request) {
  (void)ctx; (void)request; return S.trace_result;
}
static const char *stub_contents(void *ctx, qa_vec3 p) { (void)ctx; (void)p; return "empty"; }
static const char *stub_classname(void *ctx, qa_actor_id actor) {
  (void)ctx;
  qa_q1am_actor *entity = stub_entity(NULL, true, actor);
  return entity ? entity->classname : "player";
}
static bool stub_walk_move(void *ctx, qa_owned_actor a, double y, double d) {
  (void)ctx; (void)a; (void)y; (void)d; return S.walk_move_result != 0;
}
static void stub_change_yaw(void *ctx, qa_owned_actor a) { (void)ctx; (void)a; }
static void stub_move_to_goal(void *ctx, qa_owned_actor a, qa_actor_id g, double d) { (void)ctx; (void)a; (void)g; (void)d; }
static bool stub_check_bottom(void *ctx, qa_actor_id a) { (void)ctx; (void)a; return true; }
static bool stub_check_client(void *ctx, qa_owned_actor o, qa_actor_id *out) { (void)ctx; (void)o; (void)out; return false; }
static qa_actor_id *stub_players(void *ctx, size_t *out_count) {
  (void)ctx;
  qa_actor_id *out = calloc(S.player_count ? S.player_count : 1, sizeof *out);
  memcpy(out, S.player_ids, S.player_count * sizeof *out);
  *out_count = S.player_count;
  return out;
}
static void stub_emit(void *ctx, const qa_q1am_event *e) { (void)ctx; (void)e; S.emit_count++; }
static bool stub_body_read(void *ctx, qa_actor_id actor, qa_q1am_body *out) {
  (void)ctx;
  if (actor.slot < 0 || actor.slot >= MAX_ACTORS || !S.alive[actor.slot]) return false;
  *out = S.bodies[actor.slot];
  return true;
}
static void stub_body_write(void *ctx, qa_owned_actor actor, const qa_q1am_body *body) {
  (void)ctx; S.bodies[actor.id.slot] = *body;
}
static void stub_body_link(void *ctx, qa_owned_actor a) { (void)ctx; (void)a; }
static void stub_on_release_reg(void *ctx, void (*fn)(void *, qa_owned_actor), void *fn_ctx) {
  (void)ctx; (void)fn; (void)fn_ctx;
}
static bool stub_resolve_owned(void *ctx, qa_actor_id actor, qa_owned_actor *out) {
  (void)ctx;
  if (actor.slot < 0 || actor.slot >= MAX_ACTORS || !S.alive[actor.slot]) return false;
  *out = S.actors[actor.slot].actor;
  return true;
}
static bool stub_resolve_saved(void *ctx, int64_t slot, int64_t generation, qa_owned_actor *out) {
  (void)ctx; (void)generation;
  if (slot < 0 || slot >= MAX_ACTORS || !S.alive[slot]) return false;
  *out = S.actors[slot].actor;
  return true;
}
static qa_actor_id *stub_observations(void *ctx, size_t *out_count) {
  (void)ctx;
  qa_actor_id *out = calloc(MAX_ACTORS, sizeof *out);
  size_t n = 0;
  for (size_t i = 0; i < S.next_slot; i++)
    if (S.alive[i]) out[n++] = S.actors[i].actor.id;
  *out_count = n;
  return out;
}
static void stub_set_health(void *ctx, qa_owned_actor a, double h) { (void)ctx; S.health[a.id.slot] = h; }
static void stub_set_team(void *ctx, qa_owned_actor a, const char *t) { (void)ctx; (void)a; (void)t; }
static void stub_set_can_damage(void *ctx, qa_owned_actor a, bool v) { (void)ctx; S.can_damage[a.id.slot] = v; }
static bool stub_can_take_damage(void *ctx, qa_actor_id a) { (void)ctx; return S.can_damage[a.slot]; }
static void stub_armor_none(void *ctx, qa_owned_actor a) { (void)ctx; (void)a; }
static size_t stub_inv_count(void *ctx, qa_actor_id a) { (void)ctx; (void)a; return 0; }
static const char *stub_inv_item(void *ctx, qa_actor_id a, size_t i) { (void)ctx; (void)a; (void)i; return ""; }
static void stub_inv_set(void *ctx, qa_owned_actor a, size_t i, double c) { (void)ctx; (void)a; (void)i; (void)c; }
static qa_q1am_actor *stub_launch_spike(void *ctx, qa_q1am_game *g, bool has, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity) {
  (void)ctx; (void)g;
  qa_q1am_actor *shot = stub_create(NULL, "spike");
  shot->has_owner = has;
  shot->owner = owner;
  S.bodies[shot->actor.id.slot].origin = origin;
  S.bodies[shot->actor.id.slot].velocity = velocity;
  return shot;
}
static qa_q1am_actor *stub_throw_gib(void *ctx, qa_q1am_game *g, qa_vec3 o, const char *m, double d) {
  (void)ctx; (void)g; (void)o; (void)m; (void)d; return stub_create(NULL, "gib");
}
static void stub_throw_head(void *ctx, qa_q1am_game *g, qa_q1am_actor *e, const char *m, bool h, double d) {
  (void)ctx; (void)g; (void)e; (void)m; (void)h; (void)d;
}
static qa_q1am_actor *stub_meat(void *ctx, qa_q1am_game *g, qa_q1am_actor *o, qa_vec3 a, qa_vec3 b) {
  (void)ctx; (void)g; (void)o; (void)a; (void)b; return stub_create(NULL, "meat");
}
static qa_q1am_actor *stub_create_missile(void *ctx, qa_q1am_game *g, bool h, qa_actor_id o, const char *c, const char *m, qa_vec3 a, qa_vec3 b) {
  (void)ctx; (void)g; (void)h; (void)o; (void)m; (void)a; (void)b; return stub_create(NULL, c);
}
static void stub_sprite_explosion(void *ctx, qa_q1am_game *g, qa_q1am_actor *m) { (void)ctx; (void)g; (void)m; }
static qa_q1am_actor *stub_fog(void *ctx, qa_q1am_game *g, qa_vec3 o) { (void)ctx; (void)g; (void)o; return stub_create(NULL, "fog"); }
static qa_q1am_actor *stub_bubble(void *ctx, qa_q1am_game *g, qa_vec3 o) { (void)ctx; (void)g; (void)o; return stub_create(NULL, "bubble"); }
static void stub_fire_bullets(void *ctx, qa_q1am_game *g, qa_owned_actor s, qa_vec3 d, qa_vec3 v, double c, double x, double y, const char *w) {
  (void)ctx; (void)g; (void)s; (void)d; (void)v; (void)c; (void)x; (void)y; (void)w;
}
static void stub_register_monster_callbacks(void *ctx, qa_q1am_game *g, const char *p, qa_q1am_monster_lookup l) {
  (void)ctx; (void)g; (void)p; (void)l;
}
/* BaseMonster mirror ops: play executes source frame operations. */
static void base_play(qa_q1am_monster *m, const char *name) {
  if (S.played_count < 64) (void)snprintf(S.played[S.played_count++], 80, "%s", name);
  const qa_q1am_frame *frame = m->source ? qa_q1am_frame_table_get(m->source->frames, name) : NULL;
  if (frame == NULL) frame = stub_base_frame(NULL, name);
  qa_q1am_monster_set_frame(m->current_frame, sizeof m->current_frame, name);
  if (frame == NULL) return;
  qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, frame->next);
  m->entity->frame = frame->frame;
  for (size_t i = 0; i < frame->operation_count; i++) {
    const qa_q1am_operation *op = &frame->operations[i];
    if (op->kind == QA_Q1AM_OP_AI) m->ops.ai(m, op->ai_mode, op->distance);
    else if (op->kind == QA_Q1AM_OP_ACTION && m->source) {
      for (size_t a = 0; a < m->source->action_count; a++)
        if (strcmp(m->source->actions[a].name, op->name) == 0)
          m->source->actions[a].fn(m->source->actions[a].ctx, m);
    } else if (op->kind == QA_Q1AM_OP_SOUND) {
      S.game.sound(S.game.ctx, m->entity, op->path, op->channel, op->attenuation);
    } else if (op->kind == QA_Q1AM_OP_SOLID) {
      m->entity->solid = op->name;
    }
  }
}
static void base_noop_m(qa_q1am_monster *m) { (void)m; }
static void base_use(qa_q1am_monster *m, bool h, qa_actor_id a) { (void)m; (void)h; (void)a; }
static void base_pain(qa_q1am_monster *m, bool h, qa_actor_id a, double d) { (void)m; (void)h; (void)a; (void)d; }
static void base_die(qa_q1am_monster *m, bool h, qa_actor_id a) { (void)m; (void)h; (void)a; }
static void base_ai(qa_q1am_monster *m, qa_q1am_ai mode, double d) {
  (void)d;
  if (mode == QA_Q1AM_AI_RUN) m->ops.run_ai(m, d);
}
static void base_run(qa_q1am_monster *m, double d) { (void)m; (void)d; }
static void base_found(qa_q1am_monster *m, qa_actor_id target) {
  m->state->has_enemy = true;
  m->state->enemy = target;
}
static bool base_false_m(qa_q1am_monster *m) { (void)m; return false; }
static void base_attack_finished(qa_q1am_monster *m, double seconds) {
  m->state->attack_finished = S.game.time + seconds;
}
static qa_q1am_basis base_make_vectors(qa_q1am_monster *m) {
  return qa_q1am_game_make_vectors(&S.game, S.bodies[m->entity->actor.id.slot].angles);
}
static bool base_count_kill(void *ctx, qa_q1am_monster *m) { (void)ctx; (void)m; return true; }
static const char *base_next_melee(void *ctx) { (void)ctx; return "hknight_slice1"; }
static void base_finale(void *ctx, qa_q1am_monster *m) { (void)ctx; (void)m; }
static double base_read_flags(void *ctx) { (void)ctx; return S.campaign_flags; }
static void base_write_flags(void *ctx, double flags) { (void)ctx; S.campaign_flags = flags; }
static void base_level_begin(void *ctx, const char *map, bool h, qa_actor_id c) { (void)ctx; (void)map; (void)h; (void)c; }
static qa_q1am_monster *base_monsters_get(void *ctx, qa_owned_actor a) { (void)ctx; (void)a; return NULL; }
static void base_kill_rule(void *ctx, const char *id, bool (*fn)(void *, qa_q1am_monster *), void *fn_ctx) {
  (void)ctx; (void)id; (void)fn; (void)fn_ctx;
}
static void ctx_set_number(void *ctx, qa_q1am_actor *entity, const char *name, double value) {
  (void)ctx;
  qa_q1am_actor_set_field_number(entity, name, value);
}
static void ctx_set_vector(void *ctx, qa_q1am_actor *entity, const char *name, qa_vec3 value) {
  (void)ctx;
  char text[128];
  char x[32], y[32], z[32];
  qa_q1am_format_fround(x, sizeof x, value.x);
  qa_q1am_format_fround(y, sizeof y, value.y);
  qa_q1am_format_fround(z, sizeof z, value.z);
  (void)snprintf(text, sizeof text, "%s %s %s", x, y, z);
  qa_q1am_actor_set_field(entity, name, text);
}
static void ctx_set_player_number(void *ctx, qa_actor_id a, const char *n, double v) { (void)ctx; (void)a; (void)n; (void)v; }
static bool ctx_removed(void *ctx, qa_q1am_actor *e) { (void)ctx; (void)e; return false; }
static void ctx_services_emit(void *ctx, const qa_q1am_event *e) { (void)ctx; (void)e; S.services_emit_count++; }
static double ctx_cvar(void *ctx, const char *name) { (void)ctx; return strcmp(name, "skill") == 0 ? 1 : 0; }
static void stub_reset(void) {
  for (size_t i = 0; i < MAX_ACTORS; i++) qa_q1am_actor_destroy(&S.actors[i]);
  memset(&S, 0, sizeof S);
  S.random_fallback = 0.4;
  S.walk_move_result = 1;
  S.game.ctx = &S;
  S.game.options.skill = 1;
  S.game.options.edition = "rerelease";
  S.game.entity_count = stub_entity_count;
  S.game.entity_at = stub_entity_at;
  S.game.mission = stub_mission;
  S.game.named_register = stub_named_register;
  S.game.named_action = stub_named_action;
  S.game.named_use = stub_named_use;
  S.game.named_touch = stub_named_touch;
  S.game.named_pain = stub_named_pain;
  S.game.named_die = stub_named_die;
  S.game.call_action = stub_call_action;
  S.game.callback_name = stub_callback_name;
  S.game.register_spawn = stub_register_spawn;
  S.game.replace_spawn = stub_register_spawn;
  S.game.register_state_extension = stub_noop_ext;
  S.game.register_damage_source_effects = stub_noop_effects;
  S.game.create = stub_create;
  S.game.spawn_entity = stub_spawn_entity;
  S.game.remove = stub_remove;
  S.game.live = stub_live;
  S.game.entity = stub_entity;
  S.game.is_player = stub_is_player;
  S.game.health = stub_health;
  S.game.body = stub_body;
  S.game.set_body = stub_set_body;
  S.game.set_origin = stub_set_origin;
  S.game.set_bounds = stub_set_bounds;
  S.game.link = stub_link;
  S.game.schedule = stub_schedule;
  S.game.sound = stub_sound;
  S.game.effect = stub_effect;
  S.game.find = stub_find;
  S.game.use_targets = stub_use_targets;
  S.game.damage = stub_damage;
  S.game.can_damage = stub_can_damage;
  S.game.radius_damage = stub_radius_damage;
  S.game.make_vectors_raw = stub_make_vectors;
  S.game.player = stub_player;
  S.game.monster_target = stub_monster_target;
  S.game.base_frame = stub_base_frame;
  S.game.random = stub_random;
  S.game.trace = stub_trace;
  S.game.contents = stub_contents;
  S.game.classname_of = stub_classname;
  S.game.walk_move = stub_walk_move;
  S.game.change_yaw = stub_change_yaw;
  S.game.move_to_goal = stub_move_to_goal;
  S.game.check_bottom = stub_check_bottom;
  S.game.check_client = stub_check_client;
  S.game.players = stub_players;
  S.game.emit = stub_emit;
  S.game.body_read = stub_body_read;
  S.game.body_write = stub_body_write;
  S.game.body_link = stub_body_link;
  S.game.actors_on_release = stub_on_release_reg;
  S.game.actors_resolve_owned = stub_resolve_owned;
  S.game.actors_resolve_saved = stub_resolve_saved;
  S.game.observations = stub_observations;
  S.game.combat_set_health = stub_set_health;
  S.game.combat_set_team = stub_set_team;
  S.game.combat_set_can_take_damage = stub_set_can_damage;
  S.game.can_take_damage = stub_can_take_damage;
  S.game.combat_set_armor_none = stub_armor_none;
  S.game.inventory_count = stub_inv_count;
  S.game.inventory_item = stub_inv_item;
  S.game.inventory_set_count = stub_inv_set;
  S.game.launch_spike = stub_launch_spike;
  S.game.throw_gib = stub_throw_gib;
  S.game.throw_head = stub_throw_head;
  S.game.spawn_meat_spray = stub_meat;
  S.game.create_missile = stub_create_missile;
  S.game.sprite_explosion = stub_sprite_explosion;
  S.game.spawn_teleport_fog = stub_fog;
  S.game.spawn_bubble = stub_bubble;
  S.game.fire_bullets = stub_fire_bullets;
  S.game.register_monster_callbacks = stub_register_monster_callbacks;
  S.base.ctx = &S;
  S.base.game = &S.game;
  S.base.base_ops.play = base_play;
  S.base.base_ops.spawn = base_noop_m;
  S.base.base_ops.start = base_noop_m;
  S.base.base_ops.use = base_use;
  S.base.base_ops.pain = base_pain;
  S.base.base_ops.die = base_die;
  S.base.base_ops.awake = base_use;
  S.base.base_ops.ai = base_ai;
  S.base.base_ops.run_ai = base_run;
  S.base.base_ops.found = base_found;
  S.base.base_ops.find_target = base_false_m;
  S.base.base_ops.try_attack = base_false_m;
  S.base.base_ops.melee_attack = base_noop_m;
  S.base.base_ops.attack_finished = base_attack_finished;
  S.base.base_ops.sight_sound = base_noop_m;
  S.base.base_ops.make_vectors = base_make_vectors;
  S.base.base_ops.update_run_knowledge = base_noop_m;
  S.base.base_ops.check_attack = base_false_m;
  S.base.count_monster_kill = base_count_kill;
  S.base.next_hell_knight_melee = base_next_melee;
  S.base.finale = base_finale;
  S.base.finish_finale = base_finale;
  S.base.campaign_read_flags = base_read_flags;
  S.base.campaign_write_flags = base_write_flags;
  S.base.level_begin = base_level_begin;
  S.base.monsters_get = base_monsters_get;
  S.base.register_kill_count_rule = base_kill_rule;
  S.context.ctx = &S;
  S.context.base = &S.base;
  S.context.game = &S.game;
  S.context.program = "mg3";
  S.context.services_ctx = &S;
  S.context.services_emit = ctx_services_emit;
  S.context.services_cvar = ctx_cvar;
  S.context.set_number = ctx_set_number;
  S.context.set_vector = ctx_set_vector;
  S.context.set_player_number = ctx_set_player_number;
  S.context.removed_for_runes = ctx_removed;
  S.context.removed_outside_coop = ctx_removed;
  world_actor = stub_create(NULL, "worldspawn");
  S.game.world = world_actor;
  S.trace_result.fraction = 1;
}
static qa_q1am_actor *add_player(qa_vec3 origin) {
  qa_q1am_actor *player = stub_create(NULL, "player");
  qa_actor_id id = player->actor.id;
  S.player_ids[S.player_count] = id;
  S.player_states[S.player_count].actor = player->actor;
  (void)snprintf(S.player_states[S.player_count].weapon, 64, "%s", "shotgun");
  S.player_count++;
  S.bodies[id.slot].origin = origin;
  S.bodies[id.slot].bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  S.health[id.slot] = 100;
  S.can_damage[id.slot] = true;
  return player;
}
static void queue_random(double value) { S.random_queue[S.random_tail++] = value; }
static size_t count_classname(const char *classname) {
  size_t n = 0;
  for (size_t i = 0; i < S.next_slot; i++)
    if (S.alive[i] && strcmp(S.actors[i].classname, classname) == 0) n++;
  return n;
}

/* ---- navigation stub ---- */
static qa_q1am_nav_edge nav_edges[] = {{1, "walk"}, {2, "teleport"}};
static bool nav_node_exists(void *ctx, double id) { (void)ctx; return id == 1 || id == 2; }
static bool nav_route(void *ctx, const qa_q1am_nav_query *query, qa_q1am_nav_route *out) {
  (void)ctx;
  CHECK(query->edge_filter(query->filter_ctx, &nav_edges[0]));
  CHECK(!query->edge_filter(query->filter_ctx, &nav_edges[1]));
  memset(out, 0, sizeof *out);
  out->map_digest = "map-digest";
  out->nodes = calloc(2, sizeof *out->nodes);
  out->nodes[0] = 1;
  out->nodes[1] = 2;
  out->node_count = 2;
  out->edges = calloc(1, sizeof *out->edges);
  out->edges[0] = &nav_edges[0];
  out->edge_count = 1;
  out->points = calloc(1, sizeof *out->points);
  out->points[0] = query->goal;
  out->point_count = 1;
  return true;
}
static bool nav_still_valid(void *ctx, const qa_q1am_nav_route *route) { (void)ctx; (void)route; return true; }
static qa_q1am_nav_runtime nav_runtime;
static qa_q1am_nav_runtime *nav_for_actor(void *ctx, qa_owned_actor actor) {
  (void)ctx;
  nav_runtime.pass_actor = actor.id;
  return &nav_runtime;
}

int main(void) {
  /* Frame tables: counts cross-checked against the bun JSON dump. */
  CHECK(qa_q1am_mg3_lavaman_frames.count == 131);
  CHECK(qa_q1am_mg3_rknight_frames.count == 164);
  CHECK(qa_q1am_mg3_super_shambler_frames.count == 106);
  CHECK(qa_q1am_mg3_lavaman_frames.count + qa_q1am_mg3_rknight_frames.count +
        qa_q1am_mg3_super_shambler_frames.count == 401);
  CHECK(qa_q1am_final_boss_frames.count == 119);
  CHECK(qa_q1am_ghost_frames.count == 64);
  CHECK(qa_q1am_oldnew_frames.count == 127);
  CHECK(qa_q1am_orb_frames.count == 17);
  CHECK(qa_q1am_szombie_frames.count == 193);
  CHECK(qa_q1am_demodog_frames.count == 86);
  CHECK(qa_q1am_infected_frames.count == 113);
  const qa_q1am_frame *first = &qa_q1am_final_boss_frames.frames[0];
  CHECK(strcmp(first->name, "boss_final_rise1") == 0 && first->frame == 0 &&
        strcmp(first->next, "boss_final_rise2") == 0 && first->operation_count == 1 &&
        first->operations[0].kind == QA_Q1AM_OP_ACTION);
  const qa_q1am_frame *dog = qa_q1am_frame_table_get(&qa_q1am_demodog_frames, "demodog_atta4");
  CHECK(dog != NULL && dog->frame == 3 && dog->operation_count == 2 &&
        dog->operations[0].kind == QA_Q1AM_OP_SOUND &&
        strcmp(dog->operations[1].name, "demodog_bite") == 0);
  const qa_q1am_frame *corpse = qa_q1am_frame_table_get(&qa_q1am_infected_frames, "hknight_corpse2");
  CHECK(corpse != NULL && corpse->frame == 62 && corpse->operations[0].kind == QA_Q1AM_OP_SOLID);
  /* Heavy tables: every next resolves and every action has a handler. */
  const qa_q1am_heavy_definition *defs[2] = {&qa_q1am_super_shambler_definition, &qa_q1am_rune_knight_definition};
  for (int d = 0; d < 2; d++) {
    const qa_q1am_heavy_definition *def = defs[d];
    for (size_t i = 0; i < def->frames->count; i++) {
      const qa_q1am_frame *frame = &def->frames->frames[i];
      bool next_ok = qa_q1am_frame_table_get(def->frames, frame->next) != NULL;
      for (size_t a = 0; a < def->action_count && !next_ok; a++)
        next_ok = strcmp(def->actions[a].name, frame->next) == 0;
      CHECK(next_ok);
      for (size_t o = 0; o < frame->operation_count; o++) {
        if (frame->operations[o].kind != QA_Q1AM_OP_ACTION) continue;
        bool found = false;
        for (size_t a = 0; a < def->action_count; a++)
          found = found || strcmp(def->actions[a].name, frame->operations[o].name) == 0;
        CHECK(found);
      }
    }
  }
  /* Sphere points. */
  qa_vec3 p0 = qa_q1am_sphere_point(0);
  CHECK(p0.x == 0.056 && p0.y == 0.13 && p0.z == -0.99);
  qa_vec3 p99 = qa_q1am_sphere_point(99);
  CHECK(p99.x == 0.297 && p99.y == -0.098 && p99.z == -0.95);
  CHECK_ABORTS(qa_q1am_sphere_point(100));
  CHECK_ABORTS(qa_q1am_sphere_point(-1));
  /* Vector helpers. */
  CHECK(qa_q1am_yaw_for((qa_vec3){0, -1, 0}) == 270);
  qa_vec3 va = qa_q1am_velocity_angles((qa_vec3){0, 0, 5});
  CHECK(va.x == 90 && va.y == 0 && va.z == 0);
  qa_vec3 n = qa_q1am_normalize((qa_vec3){0, 0, 0});
  CHECK(n.x == 0 && n.y == 0 && n.z == 0);

  stub_reset();
  /* Actor field helpers. */
  qa_q1am_actor *probe = stub_create(NULL, "probe");
  qa_q1am_actor_set_field_number(probe, "hp", 0.1);
  CHECK(qa_q1am_actor_number(probe, "hp") == (double)(float)0.1);
  qa_q1am_actor_set_field(probe, "v", "1.5 -2 3");
  qa_vec3 parsed = qa_q1am_actor_vector(probe, "v");
  CHECK(parsed.x == 1.5 && parsed.y == -2 && parsed.z == 3);
  CHECK(qa_q1am_actor_number_or(probe, "missing", 7) == 7);

  /* MG3 AI: attackFinished fround staging and range categories. */
  qa_q1am_actor *player = add_player((qa_vec3){300, 0, 0});
  qa_q1am_actor *grunt_entity = stub_create(NULL, "monster_army");
  static const qa_q1am_source grunt_source = {"test", NULL, NULL, 0};
  qa_q1am_monster grunt;
  qa_q1am_monster_init(&grunt, &S.game, grunt_entity, &qa_q1am_army_species, &S.base, &grunt_source);
  qa_q1am_mg3_install(&grunt);
  S.game.time = 1.23456789;
  grunt.ops.attack_finished(&grunt, 0.5);
  CHECK(grunt.state->attack_finished == (double)(float)(1.23456789 + 0.5));
  CHECK(!grunt.state->refired);
  grunt.state->has_enemy = true;
  grunt.state->enemy = player->actor.id;
  /* eye distance: origins 300 apart, same view offset. */
  CHECK(qa_q1am_mg3_range_category(&grunt, false, (qa_actor_id){0}) == 1);
  S.bodies[player->actor.id.slot].origin.x = 50;
  CHECK(qa_q1am_mg3_range_category(&grunt, false, (qa_actor_id){0}) == 0);
  S.bodies[player->actor.id.slot].origin.x = 2000;
  CHECK(qa_q1am_mg3_range_category(&grunt, false, (qa_actor_id){0}) == 3);
  /* Sight sound: enforcer random selection (choice 0 -> sight3). */
  (void)snprintf(grunt_entity->classname, sizeof grunt_entity->classname, "%s", "monster_enforcer");
  queue_random(0);
  grunt.ops.sight_sound(&grunt);
  CHECK(strcmp(S.last_sound, "enforcer/sight3.wav") == 0);
  /* Contents damage: lava zombie takes 120 and reports death check. */
  (void)snprintf(grunt_entity->classname, sizeof grunt_entity->classname, "%s", "monster_zombie");
  grunt_entity->water_level = 2;
  grunt_entity->water_type = -5;
  S.health[grunt_entity->actor.id.slot] = 1000;
  int before_damage = S.damage_count;
  CHECK(qa_q1am_mg3_check_contents_damage(&grunt) == true);
  CHECK(S.damage_count == before_damage + 1 && S.last_damage == 120);
  /* Second call inside dmgtime window does nothing. */
  CHECK(qa_q1am_mg3_check_contents_damage(&grunt) == false);
  CHECK(S.damage_count == before_damage + 1);

  /* Army attack and pain against the stub trace. */
  stub_reset();
  player = add_player((qa_vec3){100, 0, 0});
  grunt_entity = stub_create(NULL, "monster_army");
  qa_q1am_monster_init(&grunt, &S.game, grunt_entity, &qa_q1am_army_species, &S.base, &grunt_source);
  qa_q1am_mg3_install(&grunt);
  grunt.state->has_enemy = true;
  grunt.state->enemy = player->actor.id;
  S.trace_result.has_actor = true;
  S.trace_result.actor = player->actor.id;
  queue_random(0.1); /* under 0.9 chance */
  queue_random(0.5); /* attackFinished delay */
  queue_random(0.9); /* no lefty flip */
  CHECK(qa_q1am_army_attack(&grunt) == true);
  CHECK(S.played_count > 0 && strcmp(S.played[S.played_count - 1], "army_atk1") == 0);
  queue_random(0.1);
  qa_q1am_army_pain(&grunt, true, player->actor.id, 10, false);
  CHECK(strcmp(S.played[S.played_count - 1], "army_pain1") == 0);
  CHECK(strcmp(S.last_sound, "soldier/pain1.wav") == 0);

  /* Infected species resolution. */
  qa_q1am_actor *inf = stub_create(NULL, "monster_army_infected");
  qa_q1am_actor_set_field(inf, "infected.kind", "army");
  qa_q1am_species spec = qa_q1am_infected_species(inf);
  CHECK(strcmp(spec.species, "army") == 0 && spec.health == 30);
  qa_q1am_actor_set_field_number(inf, "infected.transformed", 1);
  spec = qa_q1am_infected_species(inf);
  CHECK(strcmp(spec.species, "zombie") == 0 && spec.missile == NULL && spec.melee);
  qa_q1am_actor_set_field(inf, "infected.kind", "hellknight");
  qa_q1am_actor_set_field_number(inf, "infected.transformed", 0);
  inf->spawnflags = 65536;
  spec = qa_q1am_infected_species(inf);
  CHECK(strcmp(spec.stand, "hknight_corpse1") == 0 && strcmp(spec.run, "hknight_corpse1_rise0") == 0);
  qa_q1am_actor_set_field(inf, "infected.kind", "bogus");
  CHECK_ABORTS(qa_q1am_infected_kind(inf));

  /* Heavy: Super Shambler blast counts (TS: 11 then 20 knightspikes). */
  stub_reset();
  player = add_player((qa_vec3){500, 0, 0});
  qa_q1am_mg3_heavy heavy;
  qa_q1am_register_mg3_heavy(&S.context, &heavy);
  qa_q1am_actor *sham_entity = stub_create(NULL, "monster_super_shambler");
  stub_spawn_entity(NULL, sham_entity);
  qa_q1am_heavy_monster *sham = qa_q1am_mg3_heavy_require(&heavy, sham_entity);
  sham->base.state->has_enemy = true;
  sham->base.state->enemy = player->actor.id;
  sham->base.ops.play(&sham->base, "supsham_smash10");
  CHECK(count_classname("knightspike") == 11);
  sham->base.ops.play(&sham->base, "supsham_swingl7");
  CHECK(count_classname("knightspike") == 20);
  /* Rune Knight burst counter setup frame. */
  qa_q1am_actor *rk_entity = stub_create(NULL, "monster_ranged_knight");
  stub_spawn_entity(NULL, rk_entity);
  qa_q1am_heavy_monster *rk = qa_q1am_mg3_heavy_require(&heavy, rk_entity);
  rk->base.state->has_enemy = true;
  rk->base.state->enemy = player->actor.id;
  rk->base.ops.play(&rk->base, "rknight_magicb6");
  CHECK(qa_q1am_actor_number(rk_entity, "ammo_nails") == 3); /* floor(0.4*8+0.5) */
  /* Heavy checkpoint capture/restore through the state extension path. */
  qa_save_value *captured = qa_q1am_monster_capture(&rk->base);
  qa_save_reader reader;
  qa_save_reader_init(&reader, captured, "mg3:heavy");
  qa_q1am_monster_restore(&rk->base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
  CHECK(strcmp(rk->base.current_frame, "rknight_magicb6") == 0);
  /* Lava Man pain plays the shock sequence on first hit. */
  qa_q1am_actor *lava_entity = stub_create(NULL, "monster_lava_man");
  (void)snprintf(lava_entity->targetname, sizeof lava_entity->targetname, "%s", "lava1");
  stub_spawn_entity(NULL, lava_entity);
  qa_q1am_heavy_monster *lava = qa_q1am_mg3_heavy_require(&heavy, lava_entity);
  lava->base.ops.pain(&lava->base, true, player->actor.id, 1);
  CHECK(lava_entity->count == 1);
  CHECK(strcmp(S.played[S.played_count - 1], "lavaman_shocka1") == 0);
  /* Heavy lightning damage hits a repeated trace actor only once. */
  S.trace_result.has_actor = true;
  S.trace_result.actor = player->actor.id;
  S.trace_result.end = (qa_vec3){10, 0, 0};
  int damages = S.damage_count;
  qa_q1am_heavy_lightning_damage(&S.game, sham_entity->actor.id, QA_Q1AM_ZERO, (qa_vec3){10, 0, 0}, 10);
  CHECK(S.damage_count == damages + 1 && S.last_damage == 10);

  /* Navigation: in-progress walk, duplicate registration, missing host. */
  stub_reset();
  player = add_player((qa_vec3){100, 0, 0});
  grunt_entity = stub_create(NULL, "monster_army");
  qa_q1am_monster_init(&grunt, &S.game, grunt_entity, &qa_q1am_army_species, &S.base, &grunt_source);
  CHECK_ABORTS(qa_q1am_walk_mg3_path_to_goal(&grunt, 5, (qa_vec3){100, 0, 0}));
  memset(&nav_runtime, 0, sizeof nav_runtime);
  nav_runtime.map_digest = "map-digest";
  nav_runtime.profile_monster = true;
  nav_runtime.has_pass_actor = true;
  nav_runtime.graph_edges = nav_edges;
  nav_runtime.graph_edge_count = 2;
  nav_runtime.node_exists = nav_node_exists;
  nav_runtime.route = nav_route;
  nav_runtime.route_still_valid = nav_still_valid;
  qa_q1am_nav_host nav_host = {NULL, nav_for_actor};
  qa_q1am_register_mg3_monster_navigation(&S.game, nav_host);
  CHECK(qa_q1am_walk_mg3_path_to_goal(&grunt, 5, (qa_vec3){100, 0, 0}) == QA_Q1AM_PATH_IN_PROGRESS);
  CHECK(qa_q1am_walk_mg3_path_to_goal(&grunt, 5, (qa_vec3){0, 0, 0}) == QA_Q1AM_PATH_REACHED_GOAL);
  CHECK_ABORTS(qa_q1am_register_mg3_monster_navigation(&S.game, nav_host));

  /* Startup: activator selection and waiting detection. */
  qa_actor_id resolved;
  CHECK(qa_q1am_mg3_monster_activator(&S.game, false, (qa_actor_id){0}, &resolved));
  CHECK(qa_same_actor(resolved, player->actor.id));
  S.health[player->actor.id.slot] = 0;
  CHECK(!qa_q1am_mg3_monster_activator(&S.game, false, (qa_actor_id){0}, &resolved));
  S.health[player->actor.id.slot] = 100;

  /* Path targets: spawn validation errors. */
  stub_reset();
  qa_q1am_register_mg3_path_targets(&S.context);
  qa_q1am_actor *corner = stub_create(NULL, "path_corner");
  CHECK_ABORTS(stub_spawn_entity(NULL, corner));
  (void)snprintf(corner->targetname, sizeof corner->targetname, "%s", "corner1");
  stub_spawn_entity(NULL, corner);
  CHECK(strcmp(corner->solid, "trigger") == 0);
  CHECK(S.bodies[corner->actor.id.slot].bounds.max.x == 8);

  /* Boss registry: missing controller aborts; ghost spawn initializes. */
  stub_reset();
  qa_q1am_register_ghost(&S.context);
  qa_q1am_actor *ghost_entity = stub_create(NULL, "monster_ghost");
  queue_random(0.0); /* death choice */
  queue_random(0.5); /* wander direction */
  queue_random(0.0); /* count */
  stub_spawn_entity(NULL, ghost_entity);
  CHECK(strcmp(qa_q1am_actor_text(ghost_entity, "ghost.death"), "ghost_diea1") == 0);
  CHECK(S.health[ghost_entity->actor.id.slot] == 10);
  CHECK(ghost_entity->wait == 128);

  /* Demodog leap attack distance window. */
  stub_reset();
  player = add_player((qa_vec3){100, 0, 0});
  qa_q1am_register_mg3_demodog(&S.context);
  qa_q1am_actor *dog_entity = stub_create(NULL, "monster_demodog");
  stub_spawn_entity(NULL, dog_entity);
  /* The demodog registry is private; reach it through the spawn state:
   * classname was rewritten by spawn and pathfind fields set. */
  CHECK(strcmp(dog_entity->classname, "monster_dog") == 0);
  CHECK(strcmp(qa_q1am_actor_text(dog_entity, "allowPathFind"), "1") == 0);
  CHECK(S.health[dog_entity->actor.id.slot] == 25);

  /* Top-level registration installs ordinary + mg3 spawns once. */
  stub_reset();
  qa_q1am_mg3_heavy top_heavy;
  qa_q1am_register_addon_monsters(&S.context, &top_heavy);
  CHECK(S.spawn_count > 20);
  CHECK(find_handlers("mg3:final:T_RockTouch2") != NULL);
  CHECK(find_handlers("mg3:bosses:actual_sphere") != NULL);
  CHECK(find_handlers("mg3:t_movetarget") != NULL);

  printf("test_q1_addon_monsters: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
