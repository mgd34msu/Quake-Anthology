/* Port of src/content/q1/missionpacks/world/rogue-tag.ts.
 * Spawn selection (q1Base spawnSelector, TS owner base/provider.ts)
 * is the ext below. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_tag.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/index.h"
#include "qa/foundation/support.h"
#include <stdlib.h>

qa_q1mp_actor *qa_q1mp_ext_select_spawn_point(qa_q1mp_game *game, bool deathmatch);
static void announce(qa_q1mp_rogue_tag *tag, const char *text, qa_actor_id actor) {
  if (!tag->hooks || !tag->hooks->player_name) qa_range_error("Rogue token messages require shared player names");
  qa_actor_id players[32]; size_t n = tag->game->base->host->players(tag->game->base->host->ctx, players, 32);
  const char *args[] = {tag->hooks->player_name(tag->hooks->ctx, actor)};
  for (size_t i = 0; i < n; i++) if (tag->game->emit_message_event) tag->game->emit_message_event(tag->game->ctx, players[i], text, args, 1, false);
}
static qa_q1mp_actor *find_token(qa_q1mp_rogue_tag *tag) {
  qa_q1mp_actor *ents[512]; size_t n = tag->game->base->entities(tag->game->base, ents, 512);
  for (size_t i = 0; i < n; i++) if (qa_q1mp_streq(ents[i]->classname, "dmatch_tag_token")) return ents[i];
  return NULL;
}
static bool drop_floor(qa_q1mp_rogue_tag *tag, qa_q1mp_actor *entity) {
  qa_q1b_game *g = tag->game->base; qa_q1b_body body = g->body(g, entity);
  qa_q1b_trace_request req = {body.origin, qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,-256)), body.bounds, QA_Q1MP_SOME(entity->actor.id), true, false};
  qa_q1b_trace hit = g->host->trace(g->host->ctx, &req);
  if (hit.fraction == 1 || hit.all_solid) return false;
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = hit.end; p.has_ground = true; p.ground = hit.actor;
  qa_q1mp_patch(tag->game, entity, &p); g->link(g, entity); return true;
}
static void take_token(qa_q1mp_rogue_tag *tag, qa_q1mp_actor *token, qa_actor_id actor, double announcement_delay) {
  qa_q1b_game *g = tag->game->base;
  if (g->world) qa_q1b_references_set(g->world, "rogue:tag_token_owner", QA_Q1MP_SOME(actor));
  token->owner = QA_Q1MP_SOME(actor);
  qa_q1mp_world_number(token, "tag_frags", 0);
  qa_q1mp_world_number(token, "tag_message_time", g->time + announcement_delay);
  token->solid = "none"; token->touch.has = false;
  qa_q1mp_world_later(tag->game, token, 0.1, "rogue:tag_think");
}
static void respawn_token(qa_q1mp_rogue_tag *tag) {
  qa_q1mp_actor *token = find_token(tag);
  if (!token) return;
  qa_q1b_game *g = tag->game->base;
  qa_q1mp_actor *point = qa_q1mp_ext_select_spawn_point(tag->game, true);
  if (!point) qa_range_error("Tag token has no respawn point");
  g->set_origin(g, token, g->body(g, point).origin);
  if (g->world) qa_q1b_references_set(g->world, "rogue:tag_token_owner", QA_Q1MP_NONE);
  token->solid = "trigger";
  token->touch = g->named.touch(g->named.ctx, token, "rogue:tag_touch");
  token->think = g->named.action(g->named.ctx, token, "SUB_Null");
  token->owner = QA_Q1MP_NONE; qa_q1mp_world_number(token, "tag_frags", 0);
  (void)drop_floor(tag, token);
}
double qa_q1mp_tag_score(qa_q1mp_rogue_tag *tag, qa_actor_id victim, qa_actor_id attacker) {
  qa_q1mp_actor *token = find_token(tag);
  if (!token) return 1;
  qa_q1b_game *g = tag->game->base;
  qa_q1mp_opt_actor owner = g->world ? qa_q1b_references_get(g->world, "rogue:tag_token_owner") : QA_Q1MP_NONE;
  if (owner.has && qa_same_actor(attacker, owner.id)) {
    qa_q1mp_world_number(token, "tag_frags", qa_q1mp_number(token, "tag_frags") + 1);
    if (qa_q1mp_number(token, "tag_frags") == 5) {
      qa_q1mp_player_state *player = g->player(g, attacker);
      if (player) {
        g->message(g, QA_Q1MP_SOME(attacker), "$qc_got_quad", false);
        if (tag->game->give_powerup) tag->game->give_powerup(tag->game->ctx, player, "quad", false, 0);
      }
    } else if (qa_q1mp_number(token, "tag_frags") == 10) { announce(tag, "$qc_lost_token", attacker); respawn_token(tag); }
    return 3;
  }
  if (owner.has && qa_same_actor(victim, owner.id)) {
    qa_owned_actor source;
    if (g->host->actors_resolve_owned(g->host->ctx, victim, &source)) g->sound_owned(g, source, "runes/end1.wav", QA_Q1MP_CH("auto"), 1, 1);
    if (g->is_player(g, QA_Q1MP_SOME(attacker))) take_token(tag, token, attacker, 0.5);
    return 5;
  }
  return 1;
}
static void respawn_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)g; (void)e; respawn_token(vctx); }
static void fall_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)g; qa_q1mp_rogue_tag *tag = vctx;
  qa_q1mp_world_number(e, "tag_frags", 0); (void)drop_floor(tag, e);
  qa_q1mp_world_later(tag->game, e, 30, "rogue:tag_respawn");
}
static void place_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_rogue_tag *tag = vctx;
  e->movement = "toss"; e->solid = "trigger";
  g->set_origin(g, e, qa_q1mp_vadd(g->body(g, e).origin, QA_Q1MP_V3(0,0,6)));
  if (!drop_floor(tag, e)) g->remove(g, e);
}
static void think_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_rogue_tag *tag = vctx;
  if (e->owner.has && g->health(g, e->owner.id) > 0) {
    if (qa_q1mp_number(e, "tag_message_time") < g->time) { announce(tag, "$qc_has_token", e->owner.id); qa_q1mp_world_number(e, "tag_message_time", g->time + 30); }
    qa_q1b_body body; qa_vec3 origin = qa_q1mp_body_read(tag->game, e->owner.id, &body) ? body.origin : QA_Q1MP_ZERO;
    g->set_origin(g, e, qa_q1mp_vadd(origin, QA_Q1MP_V3(0,0,48)));
    qa_q1mp_world_later(tag->game, e, 0.1, "rogue:tag_think"); return;
  }
  if (e->owner.has) announce(tag, "$qc_lost_token", e->owner.id);
  qa_q1mp_world_number(e, "tag_frags", 0); e->solid = "trigger"; e->owner = QA_Q1MP_NONE;
  e->touch = g->named.touch(g->named.ctx, e, "rogue:tag_touch");
  qa_q1mp_world_later(tag->game, e, 0.1, "rogue:tag_fall");
}
static void touch_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; qa_q1mp_rogue_tag *tag = vctx;
  if (!g->is_player(g, QA_Q1MP_SOME(other))) return;
  take_token(tag, e, other, 30);
  qa_q1mp_sound(tag->game, e, "runes/end1.wav");
  announce(tag, "$qc_got_token", other);
}
static void token_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_rogue_tag *tag = vctx;
  if (qa_q1mp_teamplay(tag->game) != 3 || !g->options.has_teamplay) {
    /* TS: game.options.teamplay !== 3 (undefined !== 3 also removes) */
    if (!(g->options.has_teamplay && g->options.teamplay == 3)) { g->remove(g, e); return; }
  }
  qa_q1mp_set_model(e, "progs/sphere.mdl"); e->skin = 1; e->effects |= 8;
  e->touch = g->named.touch(g->named.ctx, e, "rogue:tag_touch");
  g->set_bounds(g, e, (qa_bounds){{-16,-16,-16},{16,16,16}});
  qa_q1mp_world_later(tag->game, e, 0.2, "rogue:tag_place");
}
void qa_q1mp_rogue_tag_init(qa_q1mp_rogue_tag *tag, qa_q1mp_game *game, const qa_q1mp_world_hooks *hooks) {
  tag->game = game; tag->hooks = hooks; qa_q1b_game *g = game->base;
  qa_q1b_handlers h = {0}; h.ctx = tag; h.action = respawn_cb;
  g->named.register_named(g->named.ctx, "rogue:tag_respawn", &h);
  qa_q1b_handlers hf = {0}; hf.ctx = tag; hf.action = fall_cb;
  g->named.register_named(g->named.ctx, "rogue:tag_fall", &hf);
  qa_q1b_handlers hp = {0}; hp.ctx = tag; hp.action = place_cb;
  g->named.register_named(g->named.ctx, "rogue:tag_place", &hp);
  qa_q1b_handlers ht = {0}; ht.ctx = tag; ht.action = think_cb;
  g->named.register_named(g->named.ctx, "rogue:tag_think", &ht);
  qa_q1b_handlers hto = {0}; hto.ctx = tag; hto.touch = touch_cb;
  g->named.register_named(g->named.ctx, "rogue:tag_touch", &hto);
  g->register_spawn(g, "dmatch_tag_token", token_spawn, tag);
}
