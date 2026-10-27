/* Port of src/content/q2/base/monsters/brain.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/brain.h"

#include <math.h>
#include <stdlib.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/brain.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static qa_q2bm_brain_power_hooks power_hooks;
void qa_q2bm_brain_set_power_hooks(qa_q2bm_brain_power_hooks hooks) { power_hooks = hooks; }

/* TS WeakSet<OwnedActor> boundPowerArmor (identity: id + owner + token). */
typedef struct cell_binding {
  qa_owned_actor actor;
  qa_q2f_entity *entity;
  qa_q2f_game *game;
  struct cell_binding *next;
} cell_binding;
static cell_binding *bindings;
static double cell_read(void *ctx) {
  cell_binding *b = ctx;
  return b->game->host->inventory.count(b->game->host->inventory.ctx, b->entity->actor.id, "q2:monster-power");
}
static void cell_write(void *ctx, double count) {
  cell_binding *b = ctx;
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "q2:monster-power"; entry.count = count; entry.capacity = 100;
  b->game->host->inventory.configure(b->game->host->inventory.ctx, b->entity->actor, entry);
}
static bool same_owned(qa_owned_actor a, qa_owned_actor b) {
  return qa_same_actor(a.id, b.id) && a.token == b.token
      && ((a.owner == NULL && b.owner == NULL) || (a.owner && b.owner && strcmp(a.owner, b.owner) == 0));
}
static void bind_power_armor_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  for (cell_binding *b = bindings; b; b = b->next)
    if (same_owned(b->actor, entity->actor)) return;
  if (!power_hooks.bind_cells) qa_q2f_error("Brain power armor has no registered combat hooks");
  cell_binding *b = malloc(sizeof *b);
  if (!b) qa_q2f_error("Brain power armor binding allocation failed");
  b->actor = entity->actor; b->entity = entity; b->game = game; b->next = bindings; bindings = b;
  qa_power_armor_cell_binding cells = {b, cell_read, cell_write};
  power_hooks.bind_cells(power_hooks.ctx, entity->actor, cells);
}
static void screen(qa_q2m_context *c, bool active) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  if (!power_hooks.set_powered) qa_q2f_error("Brain power armor has no registered combat hooks");
  double cells = game->host->inventory.count(game->host->inventory.ctx, entity->actor.id, "q2:monster-power");
  power_hooks.set_powered(power_hooks.ctx, entity->actor, active ? "screen" : "none", active ? cells : 0);
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  screen(c, true);
  q2bm_set_move(c, c->state->stand_ground ? "brain_move_stand" : "brain_move_run");
}
static void melee_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, q2bm_rnd(c->game) <= 0.5 ? "brain_move_attack1" : "brain_move_attack2");
}
static const qa_q2bm_sound_spec sight_spec = {"brain/brnsght1.wav", 2, 1};
static const qa_q2bm_sound_spec search_spec = {"brain/brnsrch1.wav", 2, 1};
static const qa_q2bm_sound_spec swing_right_spec = {"brain/melee1.wav", 4, 1};
static const qa_q2bm_sound_spec swing_left_spec = {"brain/melee2.wav", 4, 1};

static void initialize_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  if (!power_hooks.inventory_has || !power_hooks.inventory_create_empty)
    qa_q2f_error("Brain power armor has no registered combat hooks");
  if (!power_hooks.inventory_has(power_hooks.ctx, entity->actor.id))
    power_hooks.inventory_create_empty(power_hooks.ctx, entity->actor);
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "q2:monster-power"; entry.count = 100; entry.capacity = 100;
  game->host->inventory.configure(game->host->inventory.ctx, entity->actor, entry);
  bind_power_armor_fn(NULL, c);
  screen(c, true);
}
static void idle_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_sound(c->game, c->entity, "brain/brnlens1.wav", true, 0, true, 1, true, 2);
  q2bm_set_move(c, "brain_move_idle");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  double r = q2bm_rnd(c->game);
  q2bm_sound(c->game, c->entity, r < 0.33 || r >= 0.66 ? "brain/brnpain1.wav" : "brain/brnpain2.wav",
             true, 2, false, 0, false, 0);
  q2bm_set_move(c, r < 0.33 ? "brain_move_pain1" : r < 0.66 ? "brain_move_pain2" : "brain_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  c->entity->effects = 0;
  screen(c, false);
  if (qa_q2bm_standard_gib_default(c, reaction) || c->state->dead) return;
  qa_q2bm_begin_death_default(c, reaction, "brain/brndeth1.wav",
                              q2bm_rnd(c->game) <= 0.5 ? "brain_move_death1" : "brain_move_death2");
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta,
                     const qa_trace_result *trace, bool gravity) {
  (void)ctx; (void)trace; (void)gravity;
  if (q2bm_rnd(c->game) > 0.25) return;
  if (!c->entity->has_enemy) { c->entity->has_enemy = true; c->entity->enemy = attacker; }
  c->state->pause_time = q2bm_now(c->game) + eta + 0.5;
  q2bm_set_move(c, "brain_move_duck");
}
static void duck_down_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!c->state->ducked) qa_q2m_set_duck(c, true);
}
static void duck_hold_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->hold_frame = q2bm_now(c->game) < c->state->pause_time;
}
static void duck_up_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_set_duck(c, false); }
static void hit_right_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game,
                          q2bm_v3(80, q2bm_body(c->game, c->entity).bounds.max.x, 8),
                          15 + floor(q2bm_rnd(c->game) * 5), 40))
    q2bm_sound(c->game, c->entity, "brain/melee3.wav", true, 1, false, 0, false, 0);
}
static void hit_left_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game,
                          q2bm_v3(80, q2bm_body(c->game, c->entity).bounds.min.x, 8),
                          15 + floor(q2bm_rnd(c->game) * 5), 40))
    q2bm_sound(c->game, c->entity, "brain/melee3.wav", true, 1, false, 0, false, 0);
}
static void chest_open_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->spawnflags = (double)((uint32_t)c->entity->spawnflags & ~65536u);
  screen(c, false);
  q2bm_sound(c->game, c->entity, "brain/brnatck1.wav", true, 4, false, 0, false, 0);
}
static void tentacle_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2bm_v3(80, 0, 8),
                          10 + floor(q2bm_rnd(c->game) * 5), -600)
      && c->game->options.skill > 0)
    c->entity->spawnflags = (double)((uint32_t)c->entity->spawnflags | 65536u);
  q2bm_sound(c->game, c->entity, "brain/brnatck3.wav", true, 1, false, 0, false, 0);
}
static void chest_closed_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  screen(c, true);
  if (((uint32_t)c->entity->spawnflags & 65536u) != 0) {
    c->entity->spawnflags = (double)((uint32_t)c->entity->spawnflags & ~65536u);
    q2bm_set_move(c, "brain_move_attack1");
  }
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler brain_callbacks[] = {
  {"brain_stand", {(void *)"brain_move_stand", qa_q2bm_move_handler_fn}},
  CB("brain_run", run_fn), CB("brain_dead", qa_q2bm_finish_corpse_handler),
  CB("brain_duck_down", duck_down_fn), CB("brain_duck_hold", duck_hold_fn), CB("brain_duck_up", duck_up_fn),
  {"brain_swing_right", {(void *)&swing_right_spec, qa_q2bm_sound_handler_fn}},
  {"brain_swing_left", {(void *)&swing_left_spec, qa_q2bm_sound_handler_fn}},
  CB("brain_hit_right", hit_right_fn), CB("brain_hit_left", hit_left_fn),
  CB("brain_chest_open", chest_open_fn), CB("brain_tentacle_attack", tentacle_fn),
  CB("brain_chest_closed", chest_closed_fn),
};
const qa_q2m_definition qa_q2bm_brain_definition = {
  .classname = "monster_brain", .kind = "brain", .model = "models/monsters/brain/tris.md2",
  .health = 300, .gib_health = -150, .mass = 400, .bounds = {{-16, -16, -24}, {16, 16, 32}}, .scale = 1,
  .initial_move = "brain_move_stand",
  .moves = qa_q2bm_brain_moves, .move_count = 13,
  .callbacks = brain_callbacks, .callback_count = sizeof brain_callbacks / sizeof brain_callbacks[0],
  .stand = {(void *)"brain_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"brain_move_walk1", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn}, .attack = {NULL, melee_fn}, .melee = {NULL, melee_fn},
  .has_ranged_attack_flag = true, .has_ranged_attack = false,
  .initialize = {NULL, initialize_fn}, .restore = {NULL, bind_power_armor_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .search = {(void *)&search_spec, qa_q2bm_sound_handler_fn},
  .idle = {NULL, idle_fn},
  .reactions = {NULL, pain_fn, die_fn},
  .dodge = dodge_fn,
};
