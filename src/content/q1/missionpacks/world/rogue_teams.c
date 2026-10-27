/* Port of src/content/q1/missionpacks/world/rogue-teams.ts.
 * spawnMapActor/setBaseTeamHealth (TS owners foundation/spawns.ts,
 * entity-services) are ext/wrapper hooks. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_teams.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/index.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>

void qa_q1mp_ext_spawn_map_actor(qa_q1mp_game *game, qa_q1mp_actor *entity);
static const qa_bounds FLAG_BOUNDS = {{-16,-16,0},{16,16,74}};
static const char *team_name(double team) { return team == 5 ? "Red" : team == 14 ? "Blue" : team == 1 ? "Grey" : "UNKNOWN"; }
static double mode_of(qa_q1mp_rogue_teams *t) { return qa_q1mp_teamplay(t->game); }
static bool ctf_of(qa_q1mp_rogue_teams *t) { double m = mode_of(t); return m == 4 || m == 5 || m == 6; }
static double hook_gamecfg(qa_q1mp_rogue_teams *t) { return t->hooks && t->hooks->gamecfg ? t->hooks->gamecfg(t->hooks->ctx) : 0; }
static double color_of(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  if (!t->hooks || !t->hooks->team_color) {
    if (mode_of(t) <= 0) return 0;
    qa_range_error("Rogue teamplay requires shared player colors");
  }
  return t->hooks->team_color(t->hooks->ctx, actor);
}
static qa_q1mp_actor *state_of(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  qa_q1b_game *g = t->game->base; qa_q1mp_actor *ents[512]; size_t n = g->entities(g, ents, 512);
  for (size_t i = 0; i < n; i++) if (qa_q1mp_streq(ents[i]->classname,"rogue_team_state") && ents[i]->owner.has && qa_same_actor(ents[i]->owner.id, actor)) return ents[i];
  qa_q1mp_actor *state = g->create(g, "rogue_team_state"); state->owner = QA_Q1MP_SOME(actor);
  qa_q1mp_world_number(state, "steam", ctf_of(t) && ((int)hook_gamecfg(t) & 8) == 0 ? -1 : color_of(t, actor));
  return state;
}
double qa_q1mp_teams_team(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  if (!t->game->base->is_player(t->game->base, QA_Q1MP_SOME(actor))) return 0;
  return qa_q1mp_number(state_of(t, actor), "steam");
}
static double team_opt(qa_q1mp_rogue_teams *t, qa_q1mp_opt_actor actor) { return actor.has ? qa_q1mp_teams_team(t, actor.id) : 0; }
static void set_color(qa_q1mp_rogue_teams *t, qa_actor_id actor, double team) {
  if (!t->hooks || !t->hooks->set_team_color) qa_range_error("Rogue CTF requires shared color mutation");
  t->hooks->set_team_color(t->hooks->ctx, actor, team);
}
static void score(qa_q1mp_rogue_teams *t, qa_actor_id actor, double delta) {
  if (!t->hooks || !t->hooks->add_frags) qa_range_error("Rogue CTF requires shared score authority");
  t->hooks->add_frags(t->hooks->ctx, actor, delta);
}
static const char *player_name_of(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  if (!t->hooks || !t->hooks->player_name) qa_range_error("Rogue CTF requires player names");
  return t->hooks->player_name(t->hooks->ctx, actor);
}
static void message_to(qa_q1mp_rogue_teams *t, qa_actor_id actor, const char *text, bool center, const char *const *args, size_t nargs) {
  if (t->game->emit_message_event) {
    /* TS args may include numbers (2); string form passed by callers here. */
    t->game->emit_message_event(t->game->ctx, actor, text, args, nargs, center);
  }
}
static void broadcast(qa_q1mp_rogue_teams *t, const char *text) {
  qa_actor_id players[32]; size_t n = t->game->base->host->players(t->game->base->host->ctx, players, 32);
  for (size_t i = 0; i < n; i++) message_to(t, players[i], text, false, NULL, 0);
}
static void sound_actor(qa_q1mp_rogue_teams *t, qa_actor_id actor, const char *path, bool global) {
  qa_owned_actor owner;
  if (!t->game->base->host->actors_resolve_owned(t->game->base->host->ctx, actor, &owner)) return;
  t->game->base->sound_owned(t->game->base, owner, path, QA_Q1MP_CH(global ? "voice" : "item"), global ? 0 : 1, 1);
}
static bool legal_team(qa_q1mp_rogue_teams *t, double team) {
  double m = mode_of(t);
  return m < 4 ? team > 0 : ctf_of(t) ? (team == 5 || team == 14 || (m == 6 && team == 1)) : true;
}
void qa_q1mp_teams_player_spawned(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  qa_q1b_game *g = t->game->base; qa_q1mp_actor *state = state_of(t, actor);
  if (qa_q1mp_number(state, "steam") >= 0 || mode_of(t) < 4) {
    double color = color_of(t, actor);
    if (legal_team(t, color)) { qa_q1mp_world_number(state, "steam", color); return; }
  }
  double red = 0, blue = 0, grey = 0;
  qa_actor_id players[32]; size_t n = g->host->players(g->host->ctx, players, 32);
  for (size_t i = 0; i < n; i++) if (!qa_same_actor(actor, players[i])) {
    double team = qa_q1mp_teams_team(t, players[i]);
    if (team == 5) red++; else if (team == 14) blue++; else if (team == 1) grey++;
  }
  double team = 5, count = red;
  if (blue < count || (blue == count && qa_q1mp_random(t->game) < 0.5)) { team = 14; count = blue; }
  if (mode_of(t) == 6 && grey * 2 < count) team = 1;
  qa_q1mp_world_number(state, "steam", team);
  qa_q1mp_world_number(state, "ctf_flags", (double)((int)qa_q1mp_number(state,"ctf_flags") | 4));
  char buf[128]; (void)snprintf(buf, sizeof buf, "You have been assigned to the %s team.\n", team_name(team));
  message_to(t, actor, buf, false, NULL, 0);
  set_color(t, actor, team);
}
static void check_update(qa_q1mp_rogue_teams *t) {
  qa_q1b_game *g = t->game->base; qa_q1mp_actor *world = g->world;
  if (!world || qa_q1mp_number(world, "rogue:nextteamupdtime") > g->time || mode_of(t) < 1 || g->options.deathmatch == 0) return;
  qa_q1mp_world_number(world, "rogue:nextteamupdtime", g->time + 120);
  if (!ctf_of(t)) return;
  if (!t->hooks || !t->hooks->frags) qa_range_error("Rogue score update requires shared frags");
  double red = 0, blue = 0, grey = 0;
  qa_actor_id players[32]; size_t n = g->host->players(g->host->ctx, players, 32);
  for (size_t i = 0; i < n; i++) {
    double team = qa_q1mp_teams_team(t, players[i]), s = t->hooks->frags(t->hooks->ctx, players[i]);
    if (team == 5) red += s; else if (team == 14) blue += s; else if (team == 1) grey += s;
  }
  struct team_score { double team; double score; } scores[3] = {{5,red},{14,blue},{1,grey}};
  int count = mode_of(t) == 6 ? 3 : 2;
  for (int i = 0; i < count; i++) for (int j = i + 1; j < count; j++) if (scores[j].score > scores[i].score) { struct team_score tmp = scores[i]; scores[i] = scores[j]; scores[j] = tmp; }
  char buf[256];
  if (scores[0].score > scores[1].score) {
    (void)snprintf(buf, sizeof buf, "%s team is leading by %g points!\n", team_name(scores[0].team), scores[0].score - scores[1].score);
    broadcast(t, buf); return;
  }
  double tied0 = red == blue ? 5 : grey == blue ? 14 : 5, tied1 = red == blue ? 14 : grey == blue ? 1 : 1;
  (void)snprintf(buf, sizeof buf, "%s and %s teams are tied with %g points!\n", team_name(tied0), team_name(tied1), scores[0].score);
  broadcast(t, buf);
}
void qa_q1mp_teams_frame(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  qa_q1b_game *g = t->game->base; qa_q1mp_actor *state = state_of(t, actor);
  double color = color_of(t, actor);
  check_update(t);
  if (g->options.deathmatch == 0 || mode_of(t) < 4) { qa_q1mp_world_number(state, "steam", color); return; }
  if (((int)qa_q1mp_number(state,"ctf_flags") & 4) != 0) {
    qa_q1mp_world_number(state, "ctf_flags", (double)((int)qa_q1mp_number(state,"ctf_flags") & ~4));
    set_color(t, actor, qa_q1mp_number(state, "steam")); return;
  }
  if (!legal_team(t, color) && color == qa_q1mp_number(state, "steam")) qa_q1mp_world_number(state, "steam", -1);
  if (color == qa_q1mp_number(state, "steam")) return;
  double previous = qa_q1mp_number(state, "steam");
  bool changes = ((int)hook_gamecfg(t) & 16) != 0;
  if (previous >= 0 && legal_team(t, previous) && !changes) {
    if (qa_q1mp_number(state, "suicide_count") > 3) {
      message_to(t, actor, "$qc_color_games", false, NULL, 0);
      if (!t->hooks || !t->hooks->disconnect) qa_range_error("Rogue team enforcement requires shared disconnect");
      t->hooks->disconnect(t->hooks->ctx, actor);
    }
    if (qa_q1mp_number(state, "ctf_killed") != 1) qa_q1mp_world_number(state, "ctf_killed", 2);
    qa_q1b_game_damage(g, actor, actor, QA_Q1MP_SOME(actor), 1000);
    qa_q1mp_world_number(state, "suicide_count", qa_q1mp_number(state,"suicide_count") + 1);
    message_to(t, actor, "$qc_cannot_change_teams", false, NULL, 0);
    set_color(t, actor, previous); return;
  }
  if (previous >= 0 && !legal_team(t, previous)) qa_q1mp_world_number(state, "steam", -50);
  if (qa_q1mp_number(state, "steam") > 0) {
    if (qa_q1mp_number(state, "ctf_killed") != 1) qa_q1mp_world_number(state, "ctf_killed", 2);
    qa_q1b_game_damage(g, actor, actor, QA_Q1MP_SOME(actor), 1000);
  }
  if (!t->hooks || !t->hooks->frags) qa_range_error("Rogue team changes require shared scores");
  score(t, actor, -t->hooks->frags(t->hooks->ctx, actor));
  qa_q1mp_teams_player_spawned(t, actor);
}
qa_q1mp_actor *qa_q1mp_teams_select_spawn(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  qa_q1b_game *g = t->game->base; qa_q1mp_actor *world = g->world;
  if (g->options.coop || g->options.deathmatch == 0 || !ctf_of(t) || !world) return NULL;
  qa_q1mp_actor *ents[512]; size_t n = g->entities(g, ents, 512);
  for (size_t i = 0; i < n; i++) if (qa_q1mp_streq(ents[i]->classname, "testplayerstart")) return ents[i];
  double team = qa_q1mp_number(state_of(t, actor), "ctf_killed") == 0 ? qa_q1mp_teams_team(t, actor) : 0;
  const char *key = team == 5 ? "rogue:team1_lastspawn" : team == 14 ? "rogue:team2_lastspawn" : "rogue:lastspawn";
  const char *classname = team == 5 ? "info_player_team1" : team == 14 ? "info_player_team2" : "info_player_deathmatch";
  qa_q1mp_opt_actor last_ref = qa_q1b_references_get(world, key);
  qa_q1mp_actor *last = last_ref.has ? g->entity(g, last_ref) : NULL;
  qa_q1mp_actor *points[128]; size_t count = 0; int start = -1;
  for (size_t i = 0; i < n && count < 128; i++) if (qa_q1mp_streq(ents[i]->classname, classname)) { if (ents[i] == last) start = (int)count; points[count++] = ents[i]; }
  qa_actor_id players[32]; size_t np = g->host->players(g->host->ctx, players, 32);
  for (size_t step = 1; step <= count; step++) {
    qa_q1mp_actor *point = points[(start + (int)step) % (int)count];
    bool blocked = false;
    if (point != last) for (size_t i = 0; i < np; i++) {
      qa_q1b_body body;
      if (!qa_q1mp_body_read(t->game, players[i], &body)) continue;
      qa_vec3 center = qa_q1mp_vadd(body.origin, qa_q1mp_vscale(qa_q1mp_vadd(body.bounds.min, body.bounds.max), 0.5));
      if (qa_q1mp_length(qa_q1mp_vsub(center, g->body(g, point).origin)) <= 32) blocked = true;
    }
    if (point == last || !blocked) { qa_q1b_references_set(world, key, QA_Q1MP_SOME(point->actor.id)); return point; }
  }
  return NULL;
}
static size_t flags_list(qa_q1mp_rogue_teams *t, qa_q1mp_actor **out, size_t cap) {
  qa_q1mp_actor *ents[512]; size_t n = t->game->base->entities(t->game->base, ents, 512); size_t found = 0;
  for (size_t i = 0; i < n && found < cap; i++)
    if (qa_q1mp_streq(ents[i]->classname,"item_flag_team1")||qa_q1mp_streq(ents[i]->classname,"item_flag_team2")||qa_q1mp_streq(ents[i]->classname,"item_flag")) out[found++] = ents[i];
  return found;
}
static qa_q1mp_actor *find_flag(qa_q1mp_rogue_teams *t, const char *classname) {
  qa_q1mp_actor *flags[16]; size_t n = flags_list(t, flags, 16);
  for (size_t i = 0; i < n; i++) if (qa_q1mp_streq(flags[i]->classname, classname)) return flags[i];
  return NULL;
}
bool qa_q1mp_teams_impulse(qa_q1mp_rogue_teams *t, qa_actor_id actor, double impulse) {
  if (impulse != 23) return false;
  qa_q1b_game *g = t->game->base;
  if (g->options.deathmatch == 0) return true;
  if (!ctf_of(t)) { message_to(t, actor, "$qc_ctf_disabled", false, NULL, 0); return true; }
  char buf[512];
  if (mode_of(t) == 5) {
    qa_q1mp_actor *flag = find_flag(t, "item_flag");
    const char *text;
    if (!flag) text = "$qc_flag_missing";
    else if (qa_q1mp_number(flag,"cnt") == 0) text = "$qc_flag_at_base";
    else if (qa_q1mp_number(flag,"cnt") == 2) text = "$qc_flag_lying_about";
    else if (qa_q1mp_number(flag,"cnt") == 1 && flag->owner.has) {
      if (qa_same_actor(flag->owner.id, actor)) text = "$qc_you_have_flag";
      else { (void)snprintf(buf, sizeof buf, "%s of the %s team has the flag!\n", player_name_of(t, flag->owner.id), team_name(qa_q1mp_teams_team(t, flag->owner.id))); text = buf; }
    } else text = "$qc_flag_screwed_up";
    message_to(t, actor, text, false, NULL, 0); return true;
  }
  qa_q1mp_actor *red = find_flag(t, "item_flag_team1"), *blue = find_flag(t, "item_flag_team2");
  qa_q1mp_actor *ordered[2];
  bool swap = mode_of(t) == 4 && color_of(t, actor) != 5;
  ordered[0] = swap ? blue : red; ordered[1] = swap ? red : blue;
  for (int index = 0; index < 2; index++) {
    qa_q1mp_actor *flag = ordered[index];
    bool own = mode_of(t) == 4 ? index == 0 : qa_q1mp_teams_team(t, actor) == (red ? qa_q1mp_number(red,"team") : 0);
    const char *label = own ? "Your flag" : mode_of(t) == 4 ? "The enemy flag" : (index == 0 ? "Red flag" : "Blue flag");
    if (flag && qa_q1mp_number(flag,"cnt") == 1 && flag->owner.has) {
      qa_actor_id owner = flag->owner.id;
      if (qa_same_actor(owner, actor)) {
        if (mode_of(t) == 4) message_to(t, actor, "$qc_you_have_enemy_flag", false, NULL, 0);
        else { (void)snprintf(buf, sizeof buf, "You have the %s flag!\n", index == 0 ? "Red" : "Blue"); message_to(t, actor, buf, false, NULL, 0); }
      } else if (mode_of(t) == 4) { (void)snprintf(buf, sizeof buf, "%s has %s flag.\n", player_name_of(t, owner), own ? "your" : "the enemy"); message_to(t, actor, buf, false, NULL, 0); }
      else { (void)snprintf(buf, sizeof buf, "%s of the %s team has the %s flag.\n", player_name_of(t, owner), team_name(qa_q1mp_teams_team(t, owner)), index == 0 ? "Red" : "Blue"); message_to(t, actor, buf, false, NULL, 0); }
    } else {
      const char *where = !flag ? "missing!" : qa_q1mp_number(flag,"cnt") == 0 ? (mode_of(t) == 6 ? "at base." : own ? "in your base." : "in their base.") : qa_q1mp_number(flag,"cnt") == 2 ? "lying about." : " corrupt.";
      (void)snprintf(buf, sizeof buf, "%s is %s\n", label, where);
      message_to(t, actor, buf, false, NULL, 0);
    }
  }
  return true;
}
static bool drop_floor(qa_q1mp_rogue_teams *t, qa_q1mp_actor *entity) {
  qa_q1b_game *g = t->game->base; qa_q1b_body body = g->body(g, entity);
  qa_vec3 origin = qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,6));
  qa_q1b_trace_request req = {origin, qa_q1mp_vadd(origin, QA_Q1MP_V3(0,0,-256)), body.bounds, QA_Q1MP_SOME(entity->actor.id), true, false};
  qa_q1b_trace trace = g->host->trace(g->host->ctx, &req);
  if (trace.fraction == 1 || trace.all_solid) return false;
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = trace.end;
  p.has_velocity = true; p.velocity = QA_Q1MP_ZERO; p.has_ground = true; p.ground = trace.actor;
  qa_q1mp_patch(t->game, entity, &p); g->link(g, entity); return true;
}
static void flag_base(qa_q1mp_rogue_teams *t, qa_q1mp_actor *flag, const char *classname) {
  qa_q1b_game *g = t->game->base; qa_q1mp_actor *base = g->create(g, classname);
  qa_q1mp_set_model(base, "progs/ctfbase.mdl"); base->skin = flag->skin;
  qa_q1mp_world_number(base, "team", qa_q1mp_number(flag, "team"));
  base->movement_flags = 256; base->movement = "toss";
  base->solid = (mode_of(t) == 5 || mode_of(t) == 6) ? "trigger" : "none";
  if (qa_q1mp_streq(base->solid, "trigger")) base->touch = g->named.touch(g->named.ctx, base, "rogue:flagbase_touch");
  qa_q1b_body fb = g->body(g, flag);
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = fb.origin; p.has_angles = true; p.angles = fb.angles;
  p.has_bounds = true; p.bounds = (qa_bounds){{-8,-8,0},{8,8,8}}; qa_q1mp_patch(t->game, base, &p);
  if (!drop_floor(t, base)) g->remove(g, base);
}
static void regenerate(qa_q1mp_rogue_teams *t, qa_q1mp_actor *flag) {
  qa_q1b_game *g = t->game->base;
  flag->movement = "toss"; flag->solid = "trigger";
  g->sound(g, flag, "items/itembk2.wav", QA_Q1MP_CH("voice"), 1, 1);
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = qa_q1mp_vector_of(flag, "oldorigin");
  p.has_angles = true; p.angles = flag->mangle; qa_q1mp_patch(t->game, flag, &p);
  qa_q1mp_world_number(flag, "cnt", 0); flag->owner = QA_Q1MP_NONE; g->link(g, flag);
}
static void return_flag(qa_q1mp_rogue_teams *t, qa_q1mp_actor *flag) {
  regenerate(t, flag);
  qa_actor_id players[32]; size_t n = t->game->base->host->players(t->game->base->host->ctx, players, 32);
  char buf[256];
  for (size_t i = 0; i < n; i++) {
    const char *text;
    if (mode_of(t) == 5) text = "$qc_flag_returned";
    else if (mode_of(t) == 6) { (void)snprintf(buf, sizeof buf, "%s flag has been returned to base!\n", team_name(qa_q1mp_number(flag,"team"))); text = buf; }
    else text = qa_q1mp_teams_team(t, players[i]) == qa_q1mp_number(flag,"team") ? "$qc_your_flag_returned_base" : "$qc_enemy_flag_returned_base";
    message_to(t, players[i], text, true, NULL, 0);
  }
}
static void drop_flag(qa_q1mp_rogue_teams *t, qa_q1mp_actor *flag) {
  qa_q1b_game *g = t->game->base;
  qa_q1b_body body; bool has = flag->owner.has && qa_q1mp_body_read(t->game, flag->owner.id, &body);
  if (!has) { return_flag(t, flag); return; }
  char buf[512];
  (void)snprintf(buf, sizeof buf, "%s lost the %s%sflag!\n", player_name_of(t, flag->owner.id), mode_of(t) == 5 ? "" : team_name(qa_q1mp_number(flag,"team")), mode_of(t) == 5 ? "" : " ");
  broadcast(t, buf);
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,-24));
  p.has_velocity = true; p.velocity = QA_Q1MP_V3(0,0,300); p.has_bounds = true; p.bounds = FLAG_BOUNDS;
  qa_q1mp_patch(t->game, flag, &p);
  qa_q1mp_world_number(flag, "cnt", 2); flag->movement_flags = 256 | 131072; flag->solid = "trigger"; flag->movement = "toss";
  qa_q1mp_world_number(flag, "super_time", g->time + 40); g->link(g, flag);
}
static void flag_think_impl(qa_q1mp_rogue_teams *t, qa_q1mp_actor *flag) {
  qa_q1b_game *g = t->game->base;
  qa_q1mp_world_later(t->game, flag, 0.1, "rogue:flag_think");
  double status = qa_q1mp_number(flag, "cnt");
  if (status == 0) return;
  if (status == 2) { if (g->time - qa_q1mp_number(flag,"super_time") > 40) return_flag(t, flag); return; }
  if (status != 1) qa_range_error("Flag in invalid state");
  qa_q1b_body body; bool has = flag->owner.has && qa_q1mp_body_read(t->game, flag->owner.id, &body);
  if (!flag->owner.has || !has || !g->is_player(g, flag->owner) || g->health(g, flag->owner.id) <= 0) { drop_flag(t, flag); return; }
  qa_actor_id actor = flag->owner.id;
  int bits = (int)qa_q1mp_number(state_of(t, actor), "ctf_flags");
  double team = qa_q1mp_number(flag, "team");
  if ((mode_of(t) == 5 && (bits & 1) == 0) || (team == 5 && (bits & 1) == 0) || (team == 14 && (bits & 2) == 0)) { drop_flag(t, flag); return; }
  if (!t->hooks || !t->hooks->player_frame) qa_range_error("Rogue carried flags require character source frames");
  double frame = t->hooks->player_frame(t->hooks->ctx, actor);
  static const double offsets[] = {2,8,12,11,10,4,2,10,10,8,4,2};
  double distance = 14 + ((frame >= 29 && frame <= 40) ? offsets[(int)frame - 29] : (frame >= 103 && frame <= 106) ? 6 : (frame >= 107 && frame <= 118) ? 7 : 0);
  qa_q1b_basis basis = qa_q1mp_vectors(t->game, body.angles);
  qa_vec3 forward = basis.forward; forward.z = -basis.forward.z;
  qa_q1b_body_patch p = {0};
  p.has_origin = true; p.origin = qa_q1mp_vadd(qa_q1mp_vsub(qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,-16)), qa_q1mp_vscale(forward, distance)), qa_q1mp_vscale(basis.right, 22));
  p.has_angles = true; p.angles = qa_q1mp_vadd(body.angles, QA_Q1MP_V3(0,0,-45)); qa_q1mp_patch(t->game, flag, &p);
  g->link(g, flag);
  qa_q1mp_world_later(t->game, flag, 0.01, "rogue:flag_think");
}
static void clear_keys(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  qa_owned_actor owner;
  if (!t->game->base->host->actors_resolve_owned(t->game->base->host->ctx, actor, &owner)) return;
  static const char *const keys[] = {"q1:key/silver","q1:key/gold"};
  for (size_t i = 0; i < 2; i++) (void)qa_q1mp_consume(t->game, owner, keys[i], qa_q1mp_count(t->game, actor, keys[i]));
}
static void capture(qa_q1mp_rogue_teams *t, qa_actor_id actor, bool alternate) {
  qa_q1b_game *g = t->game->base; qa_q1mp_actor *state = state_of(t, actor);
  double team = qa_q1mp_teams_team(t, actor); int bits = (int)qa_q1mp_number(state, "ctf_flags");
  char buf[512]; (void)snprintf(buf, sizeof buf, "%s captured the flag!\n", player_name_of(t, actor));
  broadcast(t, buf); clear_keys(t, actor); sound_actor(t, actor, "misc/flagcap.wav", true);
  score(t, actor, alternate ? 8 : 15);
  qa_actor_id players[32]; size_t n = g->host->players(g->host->ctx, players, 32);
  for (size_t i = 0; i < n; i++) {
    qa_q1mp_actor *other = state_of(t, players[i]);
    if (color_of(t, players[i]) == team) {
      if (!qa_same_actor(actor, players[i])) score(t, players[i], alternate ? 4 : 10);
      if (!alternate) {
        if (mode_of(t) != 5 && qa_q1mp_number(other,"ctf_lastreturnedflag") + 4 > g->time) score(t, players[i], 1);
        if (qa_q1mp_number(other,"ctf_lastfraggedcarrier") + 6 > g->time) score(t, players[i], 2);
      }
      message_to(t, players[i], "$qc_your_team_captured", true, NULL, 0);
    } else {
      qa_q1mp_world_number(other, "ctf_lasthurtcarrier", -5);
      message_to(t, players[i], "$qc_your_flag_captured", true, NULL, 0);
    }
    if (!alternate) qa_q1mp_world_number(other, "ctf_flags", (double)((int)qa_q1mp_number(other,"ctf_flags") & ~3));
  }
  qa_q1mp_actor *flags[16]; size_t fn = flags_list(t, flags, 16);
  for (size_t i = 0; i < fn; i++) {
    bool regen = alternate ? qa_q1mp_number(flags[i],"team") == ((bits & 1) != 0 ? 5 : 14)
      : mode_of(t) == 5 ? qa_q1mp_streq(flags[i]->classname, "item_flag") : !qa_q1mp_streq(flags[i]->classname, "item_flag");
    if (regen) regenerate(t, flags[i]);
  }
  if (alternate) qa_q1mp_world_number(state, "ctf_flags", (double)(bits & ~3));
}
static void touch_flag(qa_q1mp_rogue_teams *t, qa_q1mp_actor *flag, qa_actor_id actor) {
  qa_q1b_game *g = t->game->base;
  if (!g->is_player(g, QA_Q1MP_SOME(actor)) || g->health(g, actor) <= 0 || color_of(t, actor) != qa_q1mp_teams_team(t, actor) || qa_q1mp_number(flag,"cnt") == 1) return;
  qa_q1mp_actor *state = state_of(t, actor);
  double team = qa_q1mp_number(flag, "team");
  int bits = (int)qa_q1mp_number(state, "ctf_flags");
  if (mode_of(t) != 5) {
    if (mode_of(t) != 4 && mode_of(t) != 6) return;
    if (team == qa_q1mp_teams_team(t, actor)) {
      if (qa_q1mp_number(flag,"cnt") == 0) {
        if ((team == 5 && (bits & 2) != 0) || (team == 14 && (bits & 1) != 0)) capture(t, actor, false);
        return;
      }
      score(t, actor, 1);
      qa_q1mp_world_number(state, "ctf_lastreturnedflag", g->time);
      sound_actor(t, actor, qa_q1mp_text(flag, "noise1"), false);
      return_flag(t, flag); return;
    }
    if ((bits & 3) != 0) return;
  }
  char buf[512];
  (void)snprintf(buf, sizeof buf, "%s got the %s%sflag!\n", player_name_of(t, actor), mode_of(t) == 5 ? "" : team_name(team), mode_of(t) == 5 ? "" : " ");
  broadcast(t, buf);
  sound_actor(t, actor, qa_q1mp_text(flag, "noise"), false);
  qa_q1mp_world_number(state, "ctf_flags", (double)(bits | (team == 14 ? 2 : 1)));
  qa_q1mp_world_number(state, "ctf_flagsince", g->time);
  qa_owned_actor owner;
  if (g->host->actors_resolve_owned(g->host->ctx, actor, &owner)) {
    if (team == 0 || team == 14) (void)qa_q1mp_give(t->game, owner, "q1:key/silver", 1);
    if (team == 0 || team == 5) (void)qa_q1mp_give(t->game, owner, "q1:key/gold", 1);
  }
  flag->owner = QA_Q1MP_SOME(actor); flag->movement = "noclip"; flag->solid = "none";
  qa_q1mp_world_number(flag, "cnt", 1); g->link(g, flag);
  message_to(t, actor, mode_of(t) == 5 ? "YOU GOT THE FLAG\n\nTAKE IT TO THEIR BASE\n" : "YOU GOT THE ENEMY FLAG\n\nRETURN TO BASE\n", true, NULL, 0);
  qa_actor_id players[32]; size_t n = g->host->players(g->host->ctx, players, 32);
  for (size_t i = 0; i < n; i++) if (!qa_same_actor(actor, players[i])) {
    if (mode_of(t) == 5) message_to(t, players[i], "$qc_flag_taken", true, NULL, 0);
    else if (qa_q1mp_teams_team(t, players[i]) == team) message_to(t, players[i], "$qc_your_flag_taken", true, NULL, 0);
    else { (void)snprintf(buf, sizeof buf, "%s team has the %s flag!\n", team_name(qa_q1mp_teams_team(t, actor)), team_name(team)); message_to(t, players[i], buf, true, NULL, 0); }
  }
}
static void base_touch_impl(qa_q1mp_rogue_teams *t, qa_q1mp_actor *base, qa_actor_id actor) {
  qa_q1b_game *g = t->game->base;
  if (!g->is_player(g, QA_Q1MP_SOME(actor)) || g->health(g, actor) <= 0 || color_of(t, actor) != qa_q1mp_teams_team(t, actor)) return;
  int bits = (int)qa_q1mp_number(state_of(t, actor), "ctf_flags");
  double team = qa_q1mp_number(base, "team");
  if (mode_of(t) == 5) {
    if (((team == 5 && qa_q1mp_teams_team(t, actor) == 14) || (team == 14 && qa_q1mp_teams_team(t, actor) == 5)) && (bits & 1) != 0) capture(t, actor, false);
    return;
  }
  if (mode_of(t) == 6 && qa_q1mp_teams_team(t, actor) == 1 && (((bits & 1) != 0 && team == 14) || ((bits & 2) != 0 && team == 5))) capture(t, actor, true);
}
void qa_q1mp_teams_confirmed_damage(qa_q1mp_rogue_teams *t, qa_actor_id target, qa_q1mp_opt_actor attacker) {
  qa_q1b_game *g = t->game->base;
  if (ctf_of(t) && attacker.has && g->is_player(g, QA_Q1MP_SOME(target)) && g->is_player(g, attacker) &&
      ((int)qa_q1mp_number(state_of(t, target), "ctf_flags") & 3) != 0 && qa_q1mp_teams_team(t, target) != qa_q1mp_teams_team(t, attacker.id))
    qa_q1mp_world_number(state_of(t, attacker.id), "ctf_lasthurtcarrier", g->time);
}
static void assists(qa_q1mp_rogue_teams *t, qa_actor_id target, qa_actor_id attacker) {
  qa_q1b_game *g = t->game->base;
  qa_q1mp_actor *victim = state_of(t, target), *killer = state_of(t, attacker);
  double team = qa_q1mp_teams_team(t, attacker);
  if (((int)qa_q1mp_number(victim,"ctf_flags") & 3) != 0 && qa_q1mp_teams_team(t, target) != team) {
    qa_q1mp_world_number(killer, "ctf_lastfraggedcarrier", g->time);
    if (qa_q1mp_number(victim,"ctf_flagsince") + 2 <= g->time) {
      score(t, attacker, 2);
      const char *args[] = {"2"}; message_to(t, attacker, "$qc_enemy_killed_bonus", false, args, 1);
    } else message_to(t, attacker, "$qc_enemy_killed_no_bonus", false, NULL, 0);
  }
  bool flag_bonus = false, carrier_bonus = false;
  if (qa_q1mp_number(victim,"ctf_lasthurtcarrier") + 4 > g->time && ((int)qa_q1mp_number(killer,"ctf_flags") & 3) == 0) { score(t, attacker, 2); carrier_bonus = true; }
  qa_actor_id origins[2] = {attacker, target};
  for (int oi = 0; oi < 2; oi++) {
    qa_q1b_body origin_body;
    if (!qa_q1mp_body_read(t->game, origins[oi], &origin_body)) continue;
    qa_vec3 origin = origin_body.origin;
    qa_q1mp_actor *ents[512]; size_t n = g->entities(g, ents, 512);
    for (size_t idx = n; idx-- > 0;) {
      qa_actor_id actor = ents[idx]->actor.id; qa_q1b_body body;
      if (!qa_q1mp_body_read(t->game, actor, &body)) continue;
      qa_vec3 center = qa_q1mp_vadd(body.origin, qa_q1mp_vscale(qa_q1mp_vadd(body.bounds.min, body.bounds.max), 0.5));
      if (qa_q1mp_length(qa_q1mp_vsub(center, origin)) > 400) continue;
      if (g->is_player(g, QA_Q1MP_SOME(actor)) && qa_q1mp_teams_team(t, actor) == team && ((int)qa_q1mp_number(state_of(t, actor),"ctf_flags") & 3) != 0 && !qa_same_actor(actor, attacker) && !carrier_bonus) { score(t, attacker, 1); carrier_bonus = true; }
      const char *cn = g->host->classname(g->host->ctx, actor);
      if ((team == 5 && qa_q1mp_streq(cn,"item_flag_team1")) || (team == 14 && qa_q1mp_streq(cn,"item_flag_team2")) || (qa_q1mp_streq(cn,"item_flag") && (!qa_same_actor(origins[oi], target) || !flag_bonus))) { score(t, attacker, 1); flag_bonus = true; }
    }
  }
}
void qa_q1mp_teams_player_died(qa_q1mp_rogue_teams *t, qa_actor_id actor, qa_q1mp_opt_actor attacker) {
  qa_q1b_game *g = t->game->base; qa_q1mp_actor *state = state_of(t, actor);
  qa_q1mp_world_number(state, "ctf_killed", qa_q1mp_number(state,"ctf_killed") == 2 ? 0 : 1);
  if (!ctf_of(t)) return;
  int bits = (int)qa_q1mp_number(state, "ctf_flags");
  if (attacker.has && g->is_player(g, attacker) && !qa_same_actor(actor, attacker.id)) assists(t, actor, attacker.id);
  if ((bits & 3) != 0) {
    qa_actor_id players[32]; size_t n = g->host->players(g->host->ctx, players, 32);
    for (size_t i = 0; i < n; i++)
      if (mode_of(t) == 5 || ((bits & 1) != 0 && qa_q1mp_teams_team(t, players[i]) == 5) || ((bits & 2) != 0 && qa_q1mp_teams_team(t, players[i]) == 14))
        qa_q1mp_world_number(state_of(t, players[i]), "ctf_lasthurtcarrier", -10);
  }
  qa_q1mp_teams_drop_carried_flag(t, actor);
}
void qa_q1mp_teams_drop_carried_flag(qa_q1mp_rogue_teams *t, qa_actor_id actor) {
  if (!ctf_of(t)) return;
  qa_q1mp_actor *state = state_of(t, actor); int bits = (int)qa_q1mp_number(state, "ctf_flags");
  const char *classname = (mode_of(t) == 5 && (bits & 1) != 0) ? "item_flag" : (bits & 1) != 0 ? "item_flag_team1" : (bits & 2) != 0 ? "item_flag_team2" : "";
  qa_q1mp_actor *flag = classname[0] ? find_flag(t, classname) : NULL;
  if (flag) { qa_q1mp_world_number(state, "ctf_flags", (double)(bits & ~3)); drop_flag(t, flag); }
}
static void flag_place_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_rogue_teams *t = vctx;
  e->movement_flags = 256 | 131072; e->solid = "trigger"; e->movement = "toss";
  e->touch = g->named.touch(g->named.ctx, e, "rogue:flag_touch");
  e->mangle = g->body(g, e).angles; e->effects |= 8;
  qa_q1mp_world_number(e, "cnt", 0);
  if (!drop_floor(t, e)) { g->remove(g, e); return; }
  qa_q1mp_world_vector(e, "oldorigin", g->body(g, e).origin);
  qa_q1mp_world_later(t->game, e, 0.1, "rogue:flag_think");
}
static void flag_think_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)g; flag_think_impl(vctx, e); }
static void flag_touch_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) { (void)g; (void)hn; (void)n; (void)s; touch_flag(vctx, e, other); }
static void flagbase_touch_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) { (void)g; (void)hn; (void)n; (void)s; base_touch_impl(vctx, e, other); }
static void flag_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_rogue_teams *t = vctx;
  double team = qa_q1mp_streq(e->classname,"item_flag_team1") ? 5 : qa_q1mp_streq(e->classname,"item_flag_team2") ? 14 : 0;
  double skin = team == 5 ? 0 : team == 14 ? 1 : 2;
  if (team == 0 ? mode_of(t) != 5 : (g->options.deathmatch == 0 || !ctf_of(t))) { g->remove(g, e); return; }
  qa_q1mp_world_number(e, "team", team); e->skin = skin;
  flag_base(t, e, team == 0 ? "item_flagbase" : team == 5 ? "item_flagbase_team1" : "item_flagbase_team2");
  if (team != 0 && mode_of(t) == 5) { g->remove(g, e); return; }
  qa_q1mp_set_model(e, "progs/ctfmodel.mdl");
  qa_q1b_fields_set(e, "noise", "misc/flagtk.wav"); qa_q1b_fields_set(e, "noise1", "misc/flagret.wav");
  g->set_bounds(g, e, FLAG_BOUNDS);
  qa_q1mp_world_later(t->game, e, 0.2, "rogue:flag_place");
}
static void noop_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)vctx; (void)g; (void)e; }
static void ctf_wall_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_rogue_teams *t = vctx;
  if (ctf_of(t)) qa_q1mp_world_brush(t->game, e);
  else g->remove(g, e);
}
static void teleport_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_rogue_teams *t = vctx;
  if ((e->spawnflags & 4) != 0 && !ctf_of(t)) g->remove(g, e);
  else qa_q1mp_ext_spawn_map_actor(t->game, e);
}
void qa_q1mp_rogue_teams_init(qa_q1mp_rogue_teams *teams, qa_q1mp_game *game, const qa_q1mp_world_hooks *hooks) {
  teams->game = game; teams->hooks = hooks; qa_q1b_game *g = game->base;
  /* TS game.setBaseTeamHealth(false): no q1b mirror field; the damage
   * effects registration below carries the team rules. */
  if (game->register_damage_source_effects) game->register_damage_source_effects(game->ctx, "rogue:teams", teams);
  (void)team_opt;
  qa_q1b_handlers h = {0}; h.ctx = teams; h.action = flag_place_cb;
  g->named.register_named(g->named.ctx, "rogue:flag_place", &h);
  qa_q1b_handlers ht = {0}; ht.ctx = teams; ht.action = flag_think_cb;
  g->named.register_named(g->named.ctx, "rogue:flag_think", &ht);
  qa_q1b_handlers hto = {0}; hto.ctx = teams; hto.touch = flag_touch_cb;
  g->named.register_named(g->named.ctx, "rogue:flag_touch", &hto);
  qa_q1b_handlers hb = {0}; hb.ctx = teams; hb.touch = flagbase_touch_cb;
  g->named.register_named(g->named.ctx, "rogue:flagbase_touch", &hb);
  static const char *const flags[] = {"item_flag_team1","item_flag_team2","item_flag"};
  for (size_t i = 0; i < 3; i++) g->register_spawn(g, flags[i], flag_spawn, teams);
  g->register_spawn(g, "info_player_team1", noop_spawn, teams);
  g->register_spawn(g, "info_player_team2", noop_spawn, teams);
  g->register_spawn(g, "func_ctf_wall", ctf_wall_spawn, teams);
  g->register_spawn(g, "trigger_teleport", teleport_spawn, teams);
}
