/* Port of src/content/q2/rerelease/goals.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/goals.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/rerelease/players.h"
#include "rr_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slice511(const char *s) {
  size_t n = strlen(s); if (n > 511) n = 511;
  char *o = malloc(n + 1); if (o) { memcpy(o, s, n); o[n] = '\0'; } return o;
}
static void help_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *other, const qa_actor_id *act) {
  qa_q2_rerelease_goals *gl = c;
  qa_q2rr_mission *m = &gl->campaign->mission;
  bool changed = false;
  const char *msg = e->message ? e->message : "";
  if (rr_band(e->spawnflags, 1) != 0) {
    if (strcmp(m->primary, msg) != 0) { free(m->primary); m->primary = slice511(msg); m->primary_changes++; changed = true; }
  } else if (strcmp(m->secondary, msg) != 0) { free(m->secondary); m->secondary = slice511(msg); m->secondary_changes++; changed = true; }
  if (changed) {
    qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "help"; ev.slot = rr_band(e->spawnflags, 1) != 0 ? 1 : 2; ev.text = msg;
    rr_emit_host(g, ev);
  }
  if (rr_band(e->spawnflags, 2) != 0 && gl->set_poi.fn) gl->set_poi.fn(gl->set_poi.ctx, e, g, other, act);
}
static void verify_target(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)c;
  if (!e->targetname || e->targetname[0] == '\0') {
    char b[512]; snprintf(b, sizeof b, "WARNING: missing targetname on %s", e->classname); rr_diag(g, b); return;
  }
  size_t n = 0; qa_q2_entity *const *ents = g->entities(g->ctx, &n);
  for (size_t i = 0; i < n; i++) if (ents[i]->target && strcmp(ents[i]->target, e->targetname) == 0) return;
  char b[1024]; snprintf(b, sizeof b, "WARNING: nothing targets %s %s", e->classname, e->targetname); rr_diag(g, b);
}
static void goal_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *other, const qa_actor_id *act) {
  (void)other; qa_q2_rerelease_goals *gl = c;
  const char *noise = qa_q2_spawn_get(&e->spawn, "noise");
  rr_sound_ent(g, e, noise ? noise : "misc/secret.wav", 2, 1, 1);
  if (strcmp(e->classname, "target_secret") == 0) g->counters->found_secrets++;
  else {
    g->counters->found_goals++;
    if (g->counters->found_goals == g->counters->total_goals && rr_band(e->spawnflags, 1) == 0) {
      char tb[64]; snprintf(tb, sizeof tb, "%lld", (long long)(int64_t)qa_q2_number_field(&e->spawn, "sounds", 0));
      qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev); ev.kind = "music"; ev.track = tb; rr_emit_host(g, ev);
    }
    if (gl->goals) {
      gl->goal_number++; gl->campaign->mission.primary_changes++;
      size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
      for (size_t i = 0; i < pn; i++) { qa_q2_entity *pl = rr_ent(g, &pls[i]); if (pl) qa_q2_rerelease_goals_notify(gl, pl, g); }
    }
  }
  qa_q2_game_use_targets_entity(g, e, act, false);
  g->remove(g->ctx, e);
}
void qa_q2_rerelease_goals_init(qa_q2_rerelease_goals *gl, qa_q2_rerelease_players *p,
                                qa_q2_rerelease_campaign_state *camp, qa_q2_rerelease_hooks hooks, qa_q2_use set_poi) {
  memset(gl, 0, sizeof *gl);
  gl->players = p; gl->campaign = camp; gl->hooks = hooks; gl->set_poi = set_poi;
  gl->help_use = (qa_q2_use){gl, help_use}; gl->goal_use = (qa_q2_use){gl, goal_use};
  gl->verify_target = (qa_q2_think){gl, verify_target};
}
void qa_q2_rerelease_goals_destroy(qa_q2_rerelease_goals *gl) { free(gl->goals); memset(gl, 0, sizeof *gl); }
bool qa_q2_rerelease_goals_spawn(qa_q2_rerelease_goals *gl, qa_q2_entity *e, qa_q2_game_services *g) {
  if (e->classname && strcmp(e->classname, "worldspawn") == 0) {
    if (qa_q2_number_field(&e->spawn, "hub_map", 0) != 0) {
      free(gl->campaign->mission.primary); free(gl->campaign->mission.secondary);
      gl->campaign->mission.primary = qa_q2rr_dup(""); gl->campaign->mission.secondary = qa_q2rr_dup("");
      gl->campaign->mission.primary_changes = 0; gl->campaign->mission.secondary_changes = 0;
      for (size_t i = 0; i < gl->players->extra_count; i++) {
        gl->players->rerelease_states[i].state->game_help1_changed = 0;
        gl->players->rerelease_states[i].state->game_help2_changed = 0;
      }
    }
    const char *goals = qa_q2_spawn_get(&e->spawn, "goals");
    free(gl->goals); gl->goals = qa_q2rr_dup(goals);
    if (gl->goals) gl->campaign->mission.primary_changes++;
    return false;
  }
  if (!e->classname || (strcmp(e->classname, "target_help") != 0 && strcmp(e->classname, "target_secret") != 0 && strcmp(e->classname, "target_goal") != 0)) return false;
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH) { g->remove(g->ctx, e); return true; }
  if (strcmp(e->classname, "target_help") == 0) {
    if (!e->message || e->message[0] == '\0') { rr_diag(g, "target_help has no message"); g->remove(g->ctx, e); return true; }
    e->use = gl->help_use; return true;
  }
  e->visible = false; e->server_flags = rr_bor(e->server_flags, 1); e->use = gl->goal_use;
  if (strcmp(e->classname, "target_secret") == 0) { g->counters->total_secrets++; g->schedule(g->ctx, e, 0.01, gl->verify_target); }
  else g->counters->total_goals++;
  return true;
}
void qa_q2_rerelease_goals_notify(qa_q2_rerelease_goals *gl, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2rr_player_state *st = qa_q2rr_players_get_state(gl->players, e->actor.id);
  qa_q2rr_player_extra_state *extra = qa_q2rr_players_get_extra_or_null(gl->players, e->actor.id);
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH || !st || !extra || !extra->spawned || rr_now(g) - st->entered_at < 0.3) return;
  qa_q2rr_mission *m = &gl->campaign->mission;
  if (gl->goals) {
    if (m->primary_changes != m->secondary_changes) {
      /* goals.split("\t")[goalNumber] */
      long idx = (long)gl->goal_number;
      const char *s = gl->goals; const char *goal = NULL; size_t glen = 0;
      for (long i = 0;; i++) {
        const char *tab = strchr(s, '\t');
        if (i == idx) { goal = s; glen = tab ? (size_t)(tab - s) : strlen(s); break; }
        if (!tab) break;
        s = tab + 1;
      }
      if (!goal) qa_q2rr_error("Invalid Quake 64 goals: completed goal index exceeds the authored goal list");
      free(m->primary);
      size_t n = glen > 511 ? 511 : glen;
      m->primary = malloc(n + 1); memcpy(m->primary, goal, n); m->primary[n] = '\0';
      m->secondary_changes = m->primary_changes;
    }
    if (extra->game_help1_changed != m->primary_changes) {
      qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "mission-objective"; ev.has_actor = true; ev.actor = e->actor.id; ev.text = m->primary; ev.talk_sound = true;
      rr_emit_rr(&gl->hooks, ev);
      extra->game_help1_changed = m->primary_changes;
    }
    return;
  }
  if (extra->game_help1_changed != m->primary_changes) {
    extra->game_help1_changed = m->primary_changes; extra->help_changed = 1; extra->help_time = rr_now(g) + 5;
    if (m->primary[0]) {
      const char *args[1] = {m->primary};
      qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "mission-objective"; ev.has_actor = true; ev.actor = e->actor.id;
      ev.text = "$g_primary_mission_objective"; ev.args = args; ev.arg_count = 1; ev.talk_sound = false;
      rr_emit_rr(&gl->hooks, ev);
    }
  }
  if (extra->game_help2_changed != m->secondary_changes) {
    extra->game_help2_changed = m->secondary_changes; extra->help_changed = 1; extra->help_time = rr_now(g) + 5;
    if (m->secondary[0]) {
      const char *args[1] = {m->secondary};
      qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "mission-objective"; ev.has_actor = true; ev.actor = e->actor.id;
      ev.text = "$g_secondary_mission_objective"; ev.args = args; ev.arg_count = 1; ev.talk_sound = false;
      rr_emit_rr(&gl->hooks, ev);
    }
  }
}
void qa_q2_rerelease_goals_end_player_frame(qa_q2_rerelease_goals *gl, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(gl->players, e->actor.id);
  double now = rr_now(g);
  if (extra->help_changed != 0 && extra->help_changed <= 3 && extra->help_time < now) {
    if (extra->help_changed == 1) rr_sound_ent(g, e, "misc/pc_up.wav", 0, 1, 3);
    extra->help_changed++; extra->help_time = now + 5;
  }
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "mission-status"; ev.has_actor = true; ev.actor = e->actor.id;
  ev.icon_visible = extra->help_changed >= 1 && extra->help_changed <= 2 && (int64_t)trunc(now * 1000) % 1000 < 500;
  rr_emit_rr(&gl->hooks, ev);
}
void qa_q2_rerelease_goals_help(qa_q2_rerelease_goals *gl, qa_q2_entity *e, qa_q2_game_services *g) {
  if (!gl->players->intermission.playing) return;
  qa_q2rr_player_state *st = qa_q2_rerelease_players_context(gl->players, e, g).state;
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(gl->players, e->actor.id);
  qa_q2rr_mission *m = &gl->campaign->mission;
  st->show_inventory = false; st->show_scores = false;
  if (st->show_help && (extra->game_help1_changed == m->primary_changes || extra->game_help2_changed == m->secondary_changes)) st->show_help = false;
  else { st->show_help = true; extra->help_changed = 0; }
  qa_q2rr_player_event pev; memset(&pev, 0, sizeof pev);
  pev.kind = "help"; pev.has_actor = true; pev.actor = e->actor.id; pev.has_visible = true; pev.visible = st->show_help;
  gl->players->hooks.emit(gl->players->hooks.ctx, pev);
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "help-computer"; ev.has_actor = true; ev.actor = e->actor.id; ev.visible = st->show_help;
  ev.primary = m->primary; ev.secondary = m->secondary; ev.slow_time = st->show_help;
  rr_emit_rr(&gl->hooks, ev);
}
qa_q2_rerelease_goals_checkpoint qa_q2_rerelease_goals_capture(qa_q2_rerelease_goals *gl) {
  qa_q2_rerelease_goals_checkpoint cp = {gl->goals, gl->goal_number}; return cp;
}
void qa_q2_rerelease_goals_restore(qa_q2_rerelease_goals *gl, qa_q2_rerelease_goals_checkpoint cp) {
  free(gl->goals); gl->goals = qa_q2rr_dup(cp.goals); gl->goal_number = cp.goal_number;
}
static qa_q2_named_think gl_thinks[1]; static qa_q2_named_use gl_uses[2];
qa_q2_callback_definitions qa_q2_rerelease_goals_callbacks(qa_q2_rerelease_goals *gl) {
  gl_thinks[0] = (qa_q2_named_think){"rr.G_VerifyTargetted", gl->verify_target};
  gl_uses[0] = (qa_q2_named_use){"rr.Use_Target_Help", gl->help_use};
  gl_uses[1] = (qa_q2_named_use){"rr.use_target_goal_or_secret", gl->goal_use};
  qa_q2_callback_definitions d = {0}; d.think = gl_thinks; d.think_count = 1; d.use = gl_uses; d.use_count = 2; return d;
}
static bool goals_spawn_cb(void *c, qa_q2_entity *e, qa_q2_game_services *g) { return qa_q2_rerelease_goals_spawn(c, e, g); }
qa_q2_spawn_module qa_q2_rerelease_goals_as_spawn(qa_q2_rerelease_goals *gl) {
  qa_q2_spawn_module m = {0}; m.ctx = gl; m.spawn = goals_spawn_cb; return m;
}
