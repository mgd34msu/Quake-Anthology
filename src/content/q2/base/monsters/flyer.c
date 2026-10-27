/* Port of src/content/q2/base/monsters/flyer.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/flyer.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/flyer.h"
#include "qa/content/q2/foundation/monsters/ai.h"

/* TS WeakMap<Q2GameServices, "melee"|"attack"|"run"> nextMoves. */
typedef struct next_move_entry { qa_q2f_game *game; int mode; struct next_move_entry *next; } next_move_entry;
enum { NEXT_MELEE, NEXT_ATTACK, NEXT_RUN };
static next_move_entry *next_moves;
static void next_set(qa_q2f_game *game, int mode) {
  for (next_move_entry *e = next_moves; e; e = e->next)
    if (e->game == game) { e->mode = mode; return; }
  next_move_entry *e = malloc(sizeof *e);
  if (!e) qa_q2f_error("Flyer next-move allocation failed");
  e->game = game; e->mode = mode; e->next = next_moves; next_moves = e;
}
static bool next_get(qa_q2f_game *game, int *out) {
  for (next_move_entry *e = next_moves; e; e = e->next)
    if (e->game == game) { *out = e->mode; return true; }
  return false;
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "flyer_move_stand" : "flyer_move_run");
}
static void fire(qa_q2m_context *c, double flash) {
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, flash, &aim)) return;
  double frame = c->entity->frame;
  double effect = frame == qa_q2bm_flyer_frames_value.attak204 || frame == qa_q2bm_flyer_frames_value.attak207
               || frame == qa_q2bm_flyer_frames_value.attak210 ? 64 : 0;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 1, 1000, effect,
                                false, false, 0);
  qa_q2bm_muzzle(c, flash, aim.direction, aim.start);
}
static void slash(qa_q2m_context *c, bool right) {
  qa_bounds bounds = q2bm_body(c->game, c->entity).bounds;
  (void)c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game,
                            q2bm_v3(80, right ? bounds.max.x : bounds.min.x, 0), 5, 0);
  q2bm_sound(c->game, c->entity, "flyer/flyatck2.wav", true, 1, false, 0, false, 0);
}
static const qa_q2bm_sound_spec sight_spec = {"flyer/flysght1.wav", 2, 1};
static const qa_q2bm_sound_spec idle_spec = {"flyer/flysrch1.wav", 2, 2};
static const qa_q2bm_sound_spec pop_spec = {"flyer/flyatck1.wav", 2, 1};

static bool map_is_jail5(const char *map) {
  const char *want = "jail5";
  if (!map) return false;
  for (size_t i = 0; want[i]; i++)
    if (tolower((unsigned char)map[i]) != want[i]) return false;
  return map[5] == '\0';
}
static void initialize_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (map_is_jail5(c->game->options.map_name) && q2bm_body(c->game, c->entity).origin.z == -104) {
    c->entity->targetname = c->entity->target;
    c->entity->target = "";
  }
  qa_q2bm_loop_sound(c, "flyer/flyidle1.wav");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  double n = floor(q2bm_rnd(c->game) * 3);
  q2bm_sound(c->game, c->entity, n == 1 ? "flyer/flypain2.wav" : "flyer/flypain1.wav",
             true, 2, false, 0, false, 0);
  q2bm_set_move(c, n == 0 ? "flyer_move_pain1" : n == 1 ? "flyer_move_pain2" : "flyer_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_explode(c, "flyer/flydeth1.wav");
}
static void fireleft_fn(void *ctx, qa_q2m_context *c) { (void)ctx; fire(c, 58); }
static void fireright_fn(void *ctx, qa_q2m_context *c) { (void)ctx; fire(c, 59); }
static void slash_left_fn(void *ctx, qa_q2m_context *c) { (void)ctx; slash(c, false); }
static void slash_right_fn(void *ctx, qa_q2m_context *c) { (void)ctx; slash(c, true); }
static void check_melee_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, qa_q2m_target_distance(c) < 80 && q2bm_rnd(c->game) <= 0.8
                   ? "flyer_move_loop_melee" : "flyer_move_end_melee");
}
static void setstart_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  next_set(c->game, NEXT_RUN);
  q2bm_set_move(c, "flyer_move_start");
}
static void nextmove_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  int next;
  if (!next_get(c->game, &next)) return;
  q2bm_set_move(c, next == NEXT_MELEE ? "flyer_move_start_melee"
                   : next == NEXT_ATTACK ? "flyer_move_attack2" : "flyer_move_run");
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler flyer_callbacks[] = {
  CB("flyer_run", run_fn),
  {"flyer_pop_blades", {(void *)&pop_spec, qa_q2bm_sound_handler_fn}},
  {"flyer_loop_melee", {(void *)"flyer_move_loop_melee", qa_q2bm_move_handler_fn}},
  CB("flyer_fireleft", fireleft_fn), CB("flyer_fireright", fireright_fn),
  CB("flyer_slash_left", slash_left_fn), CB("flyer_slash_right", slash_right_fn),
  CB("flyer_check_melee", check_melee_fn), CB("flyer_setstart", setstart_fn), CB("flyer_nextmove", nextmove_fn),
};
const qa_q2m_definition qa_q2bm_flyer_definition = {
  .classname = "monster_flyer", .kind = "flyer", .model = "models/monsters/flyer/tris.md2",
  .health = 50, .gib_health = 0, .mass = 50, .bounds = {{-16, -16, -24}, {16, 16, 32}}, .scale = 1,
  .has_locomotion = true, .locomotion = QA_Q2M_LOCO_FLY,
  .initial_move = "flyer_move_stand",
  .moves = qa_q2bm_flyer_moves, .move_count = 17,
  .callbacks = flyer_callbacks, .callback_count = sizeof flyer_callbacks / sizeof flyer_callbacks[0],
  .stand = {(void *)"flyer_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"flyer_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn},
  .attack = {(void *)"flyer_move_attack2", qa_q2bm_move_handler_fn},
  .melee = {(void *)"flyer_move_start_melee", qa_q2bm_move_handler_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .idle = {(void *)&idle_spec, qa_q2bm_sound_handler_fn},
  .initialize = {NULL, initialize_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
