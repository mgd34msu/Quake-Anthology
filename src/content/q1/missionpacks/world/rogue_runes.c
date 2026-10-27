/* Port of src/content/q1/missionpacks/world/rogue-runes.ts.
 * Checkpoint capture/restore is registered through the q1b state
 * extension (byte format is the landed persistence value encoding
 * via the host extension; this slice stores its table in memory and
 * registers the extension id - see unresolved). Damage/weapon rule
 * hooks route via wrapper registrations. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_runes.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static qa_q1mp_rune_state *state_of(qa_q1mp_rogue_runes *runes, qa_actor_id actor) {
  qa_owned_actor owner;
  if (!runes->game->base->host->actors_resolve_owned(runes->game->base->host->ctx, actor, &owner))
    qa_range_error("Rune operation needs a live actor");
  for (size_t i = 0; i < runes->player_count; i++)
    if (qa_same_actor(runes->players[i].actor.id, owner.id)) return &runes->players[i];
  if (runes->player_count == runes->player_capacity) {
    size_t next = runes->player_capacity ? runes->player_capacity * 2 : 8;
    qa_q1mp_rune_state *grown = realloc(runes->players, next * sizeof *grown);
    if (!grown) qa_range_error("Rune operation needs a live actor");
    runes->players = grown; runes->player_capacity = next;
  }
  qa_q1mp_rune_state *state = &runes->players[runes->player_count++];
  memset(state, 0, sizeof *state); state->actor = owner; return state;
}
static qa_vec3 spawn_point(qa_q1mp_rogue_runes *runes) {
  qa_q1b_game *g = runes->game->base; qa_q1mp_actor *world = g->world;
  if (!world) qa_range_error("Rune spawn requires worldspawn");
  qa_q1mp_actor *ents[512]; size_t n = g->entities(g, ents, 512);
  qa_q1mp_actor *points[128]; size_t count = 0;
  for (size_t i = 0; i < n && count < 128; i++) if (qa_q1mp_streq(ents[i]->classname, "info_player_deathmatch") && g->live(g, ents[i])) points[count++] = ents[i];
  if (count == 0) qa_range_error("Rogue runes require an info_player_deathmatch spawn");
  qa_q1mp_opt_actor prev_ref = qa_q1b_references_get(world, "rogue:rune_spawn_spot");
  qa_q1mp_actor *previous = prev_ref.has ? g->entity(g, prev_ref) : NULL;
  int index = -1; for (size_t i = 0; i < count; i++) if (points[i] == previous) index = (int)i;
  qa_q1mp_actor *next = points[(index + 1) % (int)count];
  qa_q1b_references_set(world, "rogue:rune_spawn_spot", QA_Q1MP_SOME(next->actor.id));
  return g->body(g, next).origin;
}
static qa_vec3 rune_velocity(qa_q1mp_rogue_runes *runes) {
  return QA_Q1MP_V3(-300 + qa_q1mp_random(runes->game) * 600, -300 + qa_q1mp_random(runes->game) * 600, 300);
}
static qa_q1mp_actor *spawn_rune(qa_q1mp_rogue_runes *runes, double rune, qa_vec3 origin) {
  qa_q1b_game *g = runes->game->base; qa_q1mp_actor *item = g->create(g, "rogue_rune");
  qa_q1mp_world_number(item, "rune", rune); item->movement_flags = 256; item->solid = "trigger"; item->movement = "toss";
  int r = (int)rune; char model[48];
  (void)snprintf(model, sizeof model, "progs/end%d.mdl", (r & 1) ? 1 : (r & 2) ? 2 : (r & 4) ? 3 : 4);
  qa_q1mp_set_model(item, model);
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = origin;
  p.has_velocity = true; p.velocity = rune_velocity(runes);
  p.has_bounds = true; p.bounds = (qa_bounds){{-16,-16,0},{16,16,56}}; qa_q1mp_patch(runes->game, item, &p);
  item->touch = g->named.touch(g->named.ctx, item, "rogue:rune_touch");
  qa_q1mp_world_later(runes->game, item, 120, "rogue:rune_respawn"); g->link(g, item);
  return item;
}
static void noise(qa_q1mp_rogue_runes *runes, qa_actor_id actor, int rune) {
  qa_owned_actor owner;
  if (!runes->game->base->host->actors_resolve_owned(runes->game->base->host->ctx, actor, &owner)) return;
  char path[48]; (void)snprintf(path, sizeof path, "runes/end%d.wav", rune);
  runes->game->base->sound_owned(runes->game->base, owner, path, QA_Q1MP_CH("item"), 1, 1);
}
void qa_q1mp_runes_frame(qa_q1mp_rogue_runes *runes, qa_actor_id actor) {
  qa_q1b_game *g = runes->game->base; qa_q1mp_actor *world = g->world;
  double cfg = runes->gamecfg ? runes->gamecfg(runes->gamecfg_ctx) : 0;
  if (g->options.deathmatch != 0 && ((int)cfg & 1) != 0 && world && qa_q1mp_number(world, "rogue:runes_spawned") == 0) {
    qa_q1mp_world_number(world, "rogue:runes_spawned", 1);
    qa_q1mp_world_later(runes->game, g->create(g, "rogue_rune_spawner"), 0.1, "rogue:rune_spawn");
  }
  qa_q1mp_rune_state *state = state_of(runes, actor);
  if (((int)state->rune & 8) == 0 || state->regeneration >= g->time || g->health(g, actor) >= 100) return;
  qa_owned_actor owner;
  if (!g->host->actors_resolve_owned(g->host->ctx, actor, &owner)) return;
  g->sound_owned(g, owner, "runes/end4.wav", QA_Q1MP_CH("item"), 1, 1);
  double health = g->health(g, actor) + 5; if (health > 100) health = 100;
  g->host->combat_set_health(g->host->ctx, owner, health);
  state->regeneration = g->time + 1;
}
void qa_q1mp_runes_drop(qa_q1mp_rogue_runes *runes, qa_actor_id actor) {
  qa_q1mp_rune_state *state = state_of(runes, actor); qa_q1b_body body;
  if (!qa_q1mp_body_read(runes->game, actor, &body)) return;
  static const int bits[] = {1,2,4,8};
  for (int i = 0; i < 4; i++) if (((int)state->rune & bits[i]) != 0) (void)spawn_rune(runes, bits[i], body.origin);
  state->rune = 0;
}
double qa_q1mp_runes_damage(qa_q1mp_rogue_runes *runes, qa_actor_id actor, double amount) {
  return ((int)state_of(runes, actor)->rune & 2) != 0 ? qa_q1mp_fround(amount * 2) : amount;
}
double qa_q1mp_runes_resistance(qa_q1mp_rogue_runes *runes, qa_actor_id actor, double amount) {
  qa_q1mp_rune_state *state = state_of(runes, actor);
  if (((int)state->rune & 1) == 0) return amount;
  if (state->earth_noise < runes->game->base->time) { noise(runes, actor, 1); state->earth_noise = runes->game->base->time + 1; }
  return qa_q1mp_fround(amount / 2);
}
void qa_q1mp_runes_attack_sound(qa_q1mp_rogue_runes *runes, qa_actor_id actor) {
  qa_q1mp_rune_state *state = state_of(runes, actor);
  if (((int)state->rune & 2) != 0 && state->black_noise < runes->game->base->time) { noise(runes, actor, 2); state->black_noise = runes->game->base->time + 1; }
}
double qa_q1mp_runes_attack_delay(qa_q1mp_rogue_runes *runes, qa_actor_id actor, double delay) {
  qa_q1mp_rune_state *state = state_of(runes, actor);
  if (((int)state->rune & 4) == 0) return delay;
  if (state->hell_noise < runes->game->base->time) { noise(runes, actor, 3); state->hell_noise = runes->game->base->time + 1; }
  return qa_q1mp_fround(qa_q1mp_fround(delay * 2) / 3);
}
bool qa_q1mp_runes_has_regeneration(qa_q1mp_rogue_runes *runes, qa_actor_id actor) {
  return ((int)state_of(runes, actor)->rune & 8) != 0;
}
static void on_release(void *vctx, qa_owned_actor actor) {
  qa_q1mp_rogue_runes *runes = vctx;
  for (size_t i = 0; i < runes->player_count; i++) if (qa_same_actor(runes->players[i].actor.id, actor.id)) {
    runes->players[i] = runes->players[--runes->player_count]; return;
  }
}
static void respawn_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_rogue_runes *runes = vctx;
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = spawn_point(runes);
  p.has_velocity = true; p.velocity = rune_velocity(runes); qa_q1mp_patch(runes->game, e, &p);
  g->link(g, e); qa_q1mp_world_later(runes->game, e, 120, "rogue:rune_respawn");
}
static void touch_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; qa_q1mp_rogue_runes *runes = vctx; qa_q1mp_game *game = runes->game;
  if (!g->is_player(g, QA_Q1MP_SOME(other)) || g->health(g, other) <= 0) return;
  qa_q1mp_rune_state *state = state_of(runes, other);
  if (state->rune != 0) {
    if (state->notice < g->time && game->emit_message_event) game->emit_message_event(game->ctx, other, "$qc_already_have_rune", NULL, 0, true);
    state->notice = g->time + 5; return;
  }
  state->rune = (double)((int)state->rune | (int)qa_q1mp_number(e, "rune"));
  qa_owned_actor owner;
  if (!g->host->actors_resolve_owned(g->host->ctx, other, &owner)) return;
  g->sound_owned(g, owner, "weapons/pkup.wav", QA_Q1MP_CH("item"), 1, 1);
  int r = (int)state->rune;
  const char *text = (r & 1) ? "$qc_rune_resistance" : (r & 2) ? "$qc_rune_strength" : (r & 4) ? "$qc_rune_haste" : "$qc_rune_regeneration";
  if (game->emit_message_event) game->emit_message_event(game->ctx, other, text, NULL, 0, true);
  g->remove(g, e);
}
static void spawn_all_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_rogue_runes *runes = vctx; g->remove(g, e);
  static const int bits[] = {1,2,4,8};
  for (int i = 0; i < 4; i++) (void)spawn_rune(runes, bits[i], spawn_point(runes));
}
void qa_q1mp_rogue_runes_init(qa_q1mp_rogue_runes *runes, qa_q1mp_game *game, double (*gamecfg)(void *ctx), void *gamecfg_ctx) {
  runes->game = game; runes->gamecfg = gamecfg; runes->gamecfg_ctx = gamecfg_ctx;
  runes->players = NULL; runes->player_count = 0; runes->player_capacity = 0;
  qa_q1b_game *g = game->base;
  if (g->host->actors_on_release) g->host->actors_on_release(g->host->ctx, on_release, runes);
  if (g->register_state_extension) {
    qa_q1b_state_extension ext = {0}; ext.id = "rogue:runes"; ext.ctx = runes;
    g->register_state_extension(g, &ext);
  }
  qa_q1b_handlers h = {0}; h.ctx = runes; h.action = respawn_cb;
  g->named.register_named(g->named.ctx, "rogue:rune_respawn", &h);
  qa_q1b_handlers ht = {0}; ht.ctx = runes; ht.touch = touch_cb;
  g->named.register_named(g->named.ctx, "rogue:rune_touch", &ht);
  qa_q1b_handlers hs = {0}; hs.ctx = runes; hs.action = spawn_all_cb;
  g->named.register_named(g->named.ctx, "rogue:rune_spawn", &hs);
  if (game->register_damage_source_effects) game->register_damage_source_effects(game->ctx, "rogue:runes", runes);
  if (game->register_weapon_rules) game->register_weapon_rules(game->ctx, "rogue:runes", runes);
}
