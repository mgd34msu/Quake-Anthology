/* Port of src/content/q1/missionpacks/world/campaign.ts.
 * q1Base(game) is game->q1_base (see types.h). Errors use
 * qa_range_error with the exact TS Error text. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/campaign.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/finale_text.h"
#include "qa/content/q1/missionpacks/world/index.h"
#include "qa/foundation/support.h"
#include <stdlib.h>

typedef struct camp_ctx { qa_q1mp_game *game; qa_q1mp_pack pack; qa_q1mp_world_hooks hooks; } camp_ctx;
void qa_q1mp_start_finale_timer(qa_q1mp_game *game) {
  if (qa_q1mp_streq(game->base->options.edition, "classic")) return;
  qa_q1mp_world_later(game, game->base->create(game->base, "mission_finale_timer"), 1, "mission:finale_check");
}
static void end_text(camp_ctx *c, qa_q1b_game *g) {
  qa_q1mp_q1base *base = c->game->q1_base;
  if (!g->has_intermission && base && base->begin_cutscene) base->begin_cutscene(base->ctx, g->map_name, QA_Q1MP_NONE, g->time);
  qa_q1mp_source_finale result = {0};
  if (base && base->advance_finale) result = base->advance_finale(base->ctx, g->time);
  if (qa_q1mp_streq(result.kind, "finale") || qa_q1mp_streq(result.kind, "sell-screen")) {
    if (!c->hooks.present_finale) qa_range_error("Hipnotic end text requires the shared finale journal");
    c->hooks.present_finale(c->hooks.ctx, &result);
  }
}
static void finale_transition(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  camp_ctx *c = vctx;
  if (g->options.coop) { if (g->travel) g->travel(g, "start", QA_Q1MP_NONE); }
  else if (c->game->emit_server_command) { c->game->emit_server_command(c->game->ctx, "menu_credits\n"); c->game->emit_server_command(c->game->ctx, "disconnect\n"); }
  g->remove(g, e);
}
static void finale_check(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  camp_ctx *c = vctx; qa_q1mp_q1base *base = c->game->q1_base;
  bool finished = base && base->has_finished_finale && base->has_finished_finale(base->ctx);
  qa_q1mp_world_later(c->game, e, finished ? 5 : 0.1, finished ? "mission:finale_transition" : "mission:finale_check");
  (void)g;
}
static qa_q1mp_source_finale finale_of(qa_q1mp_game *game, const char *key, double track) {
  qa_q1mp_source_finale f = {0}; f.has = true; f.kind = "finale"; f.track = track;
  f.text = qa_q1mp_mission_finale_text(game->base->options.edition, key); return f;
}
static qa_q1mp_source_finale rule_finale(void *vctx, double stage) {
  camp_ctx *c = vctx; qa_q1mp_game *game = c->game; qa_q1b_game *g = game->base; qa_q1mp_q1base *base = game->q1_base;
  qa_q1mp_source_finale none = {0}; const char *map = g->map_name;
  if (qa_q1mp_streq(c->pack, "rogue")) {
    if (stage == 2 && qa_q1mp_streq(map, "r1m7")) return finale_of(game, "$qc_finale_r1", 3);
    if (stage == 2 && qa_q1mp_streq(map, "r2m8") && g->options.coop && qa_q1mp_streq(g->options.edition, "rerelease")) {
      if (game->emit_server_command) game->emit_server_command(game->ctx, "menu_credits\ndisconnect\n");
      if (base && base->defer_exit) base->defer_exit(base->ctx, g->time + 10000000);
      qa_q1mp_source_finale f = {0}; f.has = true; f.kind = "finale"; f.text = ""; f.track = 3; return f;
    }
    return none;
  }
  if (stage == 2) {
    if (qa_q1mp_streq(map, "hip1m4")) return finale_of(game, "$qc_finale_hip1", 6);
    if (qa_q1mp_streq(map, "hip2m5")) return finale_of(game, "$qc_finale_hip2", 6);
    if (qa_q1mp_streq(map, "hipend")) {
      bool official = !base || !base->has_official_campaign || base->official_campaign;
      if (qa_q1mp_streq(g->options.edition, "rerelease") && official && game->emit_achievement) {
        game->emit_achievement(game->ctx, QA_Q1MP_NONE, "ACH_COMPLETE_HIPEND");
        if (g->options.skill == 3) game->emit_achievement(game->ctx, QA_Q1MP_NONE, "ACH_COMPLETE_HIPEND_NIGHTMARE");
      }
      return finale_of(game, "$qc_finale_hipend", 2);
    }
  }
  bool registered = !base || !base->has_registered || base->registered;
  double flags = base && base->campaign_read_flags ? base->campaign_read_flags(base->ctx) : 0;
  if (stage == 3 && registered && (((int)flags & 15) != 15)) {
    if (qa_q1mp_streq(map, "hip1m4")) return finale_of(game, "$qc_finale_hip1m4", 6);
    if (qa_q1mp_streq(map, "hip2m5")) return finale_of(game, "$qc_finale_hip2m5", 6);
    if (qa_q1mp_streq(map, "hipend")) { if (base && base->defer_exit) base->defer_exit(base->ctx, g->time + 10000000); qa_q1mp_start_finale_timer(game); return finale_of(game, "$qc_finale_hipend2", 2); }
  }
  return none;
}
static void start_end_text_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)e; end_text(vctx, g); }
static void start_end_text_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)e; (void)o; (void)a; end_text(vctx, g); }
static void effect_finale_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; (void)a; camp_ctx *c = vctx; qa_q1mp_game *game = c->game;
  if (qa_q1mp_number(e, "finale_state") == 1) return;
  qa_q1mp_world_number(e, "finale_state", 1);
  qa_q1mp_actor *point = qa_q1mp_find0(game, e->target);
  if (!point) qa_range_error("no target in finale");
  if (game->emit_finale) game->emit_finale(game->ctx, "", 1);
  if ((e->spawnflags & 2) == 0) {
    qa_q1mp_actor *target = qa_q1mp_find0(game, qa_q1mp_text(e, "mdl"));
    if (!target) qa_range_error("Hipnotic finale decoy path is missing");
    qa_actor_id players[16]; size_t np = g->host->players(g->host->ctx, players, 16);
    qa_q1b_body pb; qa_vec3 origin = QA_Q1MP_V3(0,0,0);
    if ((e->spawnflags & 1) != 0 && np > 0 && qa_q1mp_body_read(game, players[0], &pb)) origin = pb.origin;
    else origin = g->body(g, target).origin;
    if (!c->hooks.become_decoy) qa_range_error("Hipnotic finale requires the source decoy controller");
    (void)c->hooks.become_decoy(c->hooks.ctx, target->target, origin);
  }
  qa_actor_id players[16]; size_t np = g->host->players(g->host->ctx, players, 16);
  for (size_t i = 0; i < np; i++) if (game->control_player) game->control_player(game->ctx, players[i], "cutscene", g->body(g, point).origin, point->mangle, QA_Q1MP_ZERO);
  const char *callback = qa_q1mp_text(e, "spawnfunction");
  if (callback && callback[0]) { qa_q1b_fields_set(e, "hip:finale_callback", callback); qa_q1mp_world_later(game, e, e->wait, "hip:finale_callback"); }
}
static void finale_callback(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  camp_ctx *c = vctx; const char *callback = qa_q1mp_text(e, "hip:finale_callback");
  if (qa_q1mp_streq(callback, "info_startendtext_use")) { end_text(c, g); return; }
  if (qa_q1mp_streq(callback, "SUB_UseTargets")) { g->use_targets(g, e, e->activator); return; }
  if (qa_q1mp_streq(callback, "SUB_Remove")) { g->remove(g, e); return; }
  if (qa_q1mp_streq(callback, "SUB_Null")) return;
  char previous[64]; (void)snprintf(previous, sizeof previous, "%s", e->classname);
  qa_q1mp_set_classname(e, callback); g->spawn_entity(g, e);
  if (g->entity(g, QA_Q1MP_SOME(e->actor.id))) qa_q1mp_set_classname(e, previous);
}
static void effect_finale_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx;
  if (g->options.deathmatch != 0) { g->remove(g, e); return; }
  qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = e->mangle; g->set_body(g, e, &p);
  qa_q1mp_world_number(e, "finale_state", 0);
  e->use = g->named.use(g->named.ctx, e, "hip:effect_finale");
}
static void startendtext_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)vctx; e->use = g->named.use(g->named.ctx, e, "hip:start_end_text"); }
void qa_q1mp_register_mission_campaign(qa_q1mp_game *game, qa_q1mp_pack pack, const qa_q1mp_world_hooks *hooks) {
  camp_ctx *c = malloc(sizeof *c); c->game = game; c->pack = pack;
  if (hooks) c->hooks = *hooks; else memset(&c->hooks, 0, sizeof c->hooks);
  qa_q1b_game *g = game->base;
  qa_q1b_handlers h = {0}; h.ctx = c; h.action = finale_transition;
  g->named.register_named(g->named.ctx, "mission:finale_transition", &h);
  qa_q1b_handlers h2 = {0}; h2.ctx = c; h2.action = finale_check;
  g->named.register_named(g->named.ctx, "mission:finale_check", &h2);
  if (game->q1_base && game->q1_base->register_intermission_rule) {
    static char idbuf[64]; (void)snprintf(idbuf, sizeof idbuf, "q1:%s:campaign", pack);
    qa_q1mp_intermission_rule rule = {idbuf, c, rule_finale};
    game->q1_base->register_intermission_rule(game->q1_base->ctx, &rule);
  }
  if (!qa_q1mp_streq(pack, "hipnotic")) return;
  qa_q1b_handlers h3 = {0}; h3.ctx = c; h3.action = start_end_text_cb; h3.use = start_end_text_use;
  g->named.register_named(g->named.ctx, "hip:start_end_text", &h3);
  qa_q1b_handlers h4 = {0}; h4.ctx = c; h4.use = effect_finale_use;
  g->named.register_named(g->named.ctx, "hip:effect_finale", &h4);
  qa_q1b_handlers h5 = {0}; h5.ctx = c; h5.action = finale_callback;
  g->named.register_named(g->named.ctx, "hip:finale_callback", &h5);
  g->register_spawn(g, "effect_finale", effect_finale_spawn, c);
  g->register_spawn(g, "info_startendtext", startendtext_spawn, c);
}
