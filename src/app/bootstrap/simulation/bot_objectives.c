/* Port of src/app/bootstrap/simulation/bot-objectives.ts. The TS instanceof
 * tests on match.source read qa_q2_product_match.source_kind. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_objectives.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q1/addons/ctf/index.h"
#include "qa/foundation/support.h"

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
double qa_app_sim_rerelease_bot_team_of(const char *team) {
  if (is(team, "team:red") || is(team, "red") || is(team, "1")) return 1;
  if (is(team, "team:blue") || is(team, "blue") || is(team, "2")) return 2;
  return 0;
}
static qa_q2_product_match *match_of(const qa_app_sim_bot_simulation *sim) {
  qa_q2_product_runtime *q2 = sim->q2_source(sim->ctx);
  return q2 == NULL ? NULL : &q2->match;
}
static bool is_kind(const qa_q2_product_match *match, qa_q2_match_kind kind) { return match != NULL && match->source_kind == kind; }
static qa_q1ad_ctf *q1_ctf(const qa_app_sim_bot_simulation *sim) {
  const qa_app_sim_bot_q1_source *q1 = sim->q1_source(sim->ctx);
  return q1 == NULL ? NULL : q1->composition->ctf;
}
/* match.states.get(actor)?.team; false is undefined. */
static bool team_state(qa_q2_product_match *match, qa_actor_id actor, double *out) {
  if (is_kind(match, QA_Q2_MATCH_CTF)) {
    qa_q2_ctf_player_state *state = qa_q2_ctf_context_get(qa_q2_ctf_get_context(match->ctf), actor);
    if (state == NULL) return false;
    *out = state->team; return true;
  }
  if (is_kind(match, QA_Q2_MATCH_LMCTF)) {
    qa_lmctf_player_state *state = qa_lmctf_context_get(qa_q2_lmctf_get_context(match->lmctf), actor);
    if (state == NULL) return false;
    *out = state->team; return true;
  }
  return false;
}
static void objectives_admit(void *ctx, qa_actor_id actor) {
  qa_app_sim_rerelease_bot_objectives_state *o = ctx;
  const qa_app_sim_bot_simulation *sim = o->simulation;
  qa_q2_product_match *match = match_of(sim);
  double team = 0;
  if (!(is_kind(match, QA_Q2_MATCH_CTF) || is_kind(match, QA_Q2_MATCH_LMCTF)) || ((team_state(match, actor, &team) ? team : 0) != 0)) return;
  int red = 0, blue = 0;
  size_t count = 0;
  const qa_actor_id *players = sim->players(sim->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    double value;
    if (!team_state(match, players[i], &value)) continue;
    if (value == 1) red++; else if (value == 2) blue++;
  }
  const char *args[1] = {red <= blue ? "red" : "blue"};
  sim->player_command(sim->ctx, actor, "team", args, 1);
}
static double cvar_value(void *ctx, const char *name) {
  qa_app_sim_rerelease_bot_objectives_state *o = ctx;
  return qa_cvar_registry_variable_value(o->registry, name);
}
static qa_bot_game_mode objectives_mode(void *ctx) {
  qa_app_sim_rerelease_bot_objectives_state *o = ctx;
  const qa_app_sim_bot_simulation *sim = o->simulation;
  qa_bot_game_mode mode = qa_bot_knowledge_game_mode(o->knowledge, cvar_value, o);
  qa_q2_product_match *match = match_of(sim);
  if (q1_ctf(sim) != NULL || is_kind(match, QA_Q2_MATCH_CTF) || is_kind(match, QA_Q2_MATCH_LMCTF)) {
    mode.game_type = QA_BOT_GAME_TYPE_CTF; mode.has_has_teams = true; mode.has_teams = true; return mode;
  }
  if (is(sim->mode, "coop") || is(sim->mode, "singleplayer")) {
    mode.game_type = qa_cvar_registry_variable_value(o->registry, "horde") != 0 ? QA_BOT_GAME_TYPE_HORDE : QA_BOT_GAME_TYPE_COOP;
    mode.has_has_teams = true; mode.has_teams = true; return mode;
  }
  if (is_kind(match, QA_Q2_MATCH_DEATHBALL)) { mode.has_has_teams = true; mode.has_teams = true; }
  return mode;
}
static int64_t objectives_team(void *ctx, qa_actor_id actor) {
  qa_app_sim_rerelease_bot_objectives_state *o = ctx;
  const qa_app_sim_bot_simulation *sim = o->simulation;
  if (!is(sim->mode, "deathmatch") && sim->movement_player(sim->ctx, actor) != NULL) return 1;
  qa_q2_product_match *match = match_of(sim);
  if (is_kind(match, QA_Q2_MATCH_CTF) || is_kind(match, QA_Q2_MATCH_LMCTF)) {
    double team;
    return team_state(match, actor, &team) ? (int64_t)team : 0;
  }
  if (is_kind(match, QA_Q2_MATCH_DEATHBALL)) {
    const char *skin = match->ball.hooks.skin(match->ball.hooks.ctx, actor);
    qa_q2mp_deathball_settings settings = match->ball.hooks.settings(match->ball.hooks.ctx);
    return is(skin, settings.team1_skin) ? 1 : is(skin, settings.team2_skin) ? 2 : 0;
  }
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  return (int64_t)qa_app_sim_rerelease_bot_team_of(combat.has_value ? combat.value.team : NULL);
}
static bool objectives_carrying(void *ctx, qa_actor_id actor) {
  qa_app_sim_rerelease_bot_objectives_state *o = ctx;
  const qa_app_sim_bot_simulation *sim = o->simulation;
  size_t count = 0;
  const qa_app_sim_source_objective *objectives = sim->source_objectives(sim->ctx, &count);
  for (size_t i = 0; i < count; i++) if (objectives[i].has_carrier && qa_same_actor(objectives[i].carrier, actor)) return true;
  qa_q1ad_ctf *ctf = q1_ctf(sim);
  if (ctf != NULL) return qa_q1ad_ctf_carried(ctf, actor) != NULL;
  qa_q2_product_match *match = match_of(sim);
  qa_q2_product_runtime *q2 = sim->q2_source(sim->ctx);
  if (is_kind(match, QA_Q2_MATCH_LMCTF) && q2 != NULL) return qa_lmctf_flags_carried(qa_q2_lmctf_get_flags(match->lmctf), actor, q2->services) != NULL;
  if (is_kind(match, QA_Q2_MATCH_CTF))
    return qa_shared_inventory_table_count(sim->inventory, actor, "q2:item_flag_team1") > 0
        || qa_shared_inventory_table_count(sim->inventory, actor, "q2:item_flag_team2") > 0;
  qa_actor_id owner;
  return is_kind(match, QA_Q2_MATCH_TAG) && qa_q2mp_tag_owner_actor(&match->tag, &owner) && qa_same_actor(owner, actor);
}
static bool body_origin(const qa_app_sim_bot_simulation *sim, qa_actor_id actor, qa_vec3 *out) {
  qa_body_state_result body = qa_shared_body_table_read(sim->bodies, actor);
  if (!body.has_value) return false;
  *out = body.value.origin; return true;
}
static bool objectives_goal(void *ctx, qa_actor_id actor, qa_vec3 *out) {
  qa_app_sim_rerelease_bot_objectives_state *o = ctx;
  const qa_app_sim_bot_simulation *sim = o->simulation;
  size_t count = 0;
  const qa_app_sim_source_objective *objectives = sim->source_objectives(sim->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_source_objective *value = &objectives[i];
    if (!(value->bot_goal && !value->complete && value->has_target && !qa_same_actor(value->target, actor))) continue;
    return body_origin(sim, value->target, out); /* the first match's target is never null */
  }
  qa_q2_product_runtime *q2 = sim->q2_source(sim->ctx);
  if (!is(sim->mode, "deathmatch")) {
    if (q2 == NULL || q2->rerelease == NULL) return false;
    const qa_q2rr_poi *poi = &q2->rerelease->entities.base.poi;
    if (!poi->has) return false;
    *out = poi->origin; return true;
  }
  qa_q2_product_match *match = match_of(sim);
  qa_actor_id target;
  bool has_target = is_kind(match, QA_Q2_MATCH_TAG) ? qa_q2mp_tag_owner_actor(&match->tag, &target)
      : is_kind(match, QA_Q2_MATCH_DEATHBALL) ? qa_q2mp_deathball_ball_actor(&match->ball, &target) : false;
  if (!has_target || qa_same_actor(target, actor)) return false;
  return body_origin(sim, target, out);
}

qa_app_sim_rerelease_bot_objectives_state *qa_app_sim_rerelease_bot_objectives_create(const qa_app_sim_bot_simulation *simulation,
    const qa_bot_knowledge *knowledge) {
  const qa_app_sim_bot_q1_source *q1 = simulation->q1_source(simulation->ctx);
  qa_cvar_registry *registry = q1 != NULL ? q1->cvars : simulation->q2_server_cvars(simulation->ctx);
  if (registry == NULL) qa_raise("Error", 0, 0, "Bot objective source registry unavailable");
  qa_app_sim_rerelease_bot_objectives_state *o = calloc(1, sizeof *o);
  if (o == NULL) qa_range_error("Out of memory");
  o->simulation = simulation; o->knowledge = knowledge; o->registry = registry;
  o->objectives.ctx = o;
  o->objectives.admit = objectives_admit; o->objectives.mode = objectives_mode; o->objectives.team = objectives_team;
  o->objectives.carrying = objectives_carrying; o->objectives.goal = objectives_goal;
  return o;
}
void qa_app_sim_rerelease_bot_objectives_destroy(qa_app_sim_rerelease_bot_objectives_state *o) { free(o); }
