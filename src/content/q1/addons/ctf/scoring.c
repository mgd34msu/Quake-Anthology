/* Port of src/content/q1/addons/ctf/scoring.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/scoring.h"

#include <string.h>

static bool team_equal(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}
static bool friendly(qa_q1ad_ctf_state *state, qa_actor_id target, const qa_actor_id *attacker) {
  return attacker != NULL && !qa_same_actor(target, *attacker) &&
      team_equal(qa_q1ad_ctf_last_team(state, target), qa_q1ad_ctf_last_team(state, *attacker));
}
bool qa_q1ad_ctf_damage_before_quad_cancel(qa_q1ad_ctf_state *state, qa_actor_id target,
                                           const char *cause_kind, const char *cause_detail) {
  bool falling = (strcmp(cause_kind, "q1") == 0 && strcmp(cause_detail, "falling") == 0) ||
      (strcmp(cause_kind, "environment") == 0 && strcmp(cause_detail, "fall") == 0);
  return falling && qa_q1ad_ctf_grapple_pulling(state, target);
}
bool qa_q1ad_ctf_armor_allowed(qa_q1ad_ctf_state *state, qa_actor_id target, const qa_actor_id *attacker) {
  double teamplay = qa_q1ad_ctf_teamplay(state);
  return teamplay < 0 || qa_q1ad_ctf_start_map(state) ||
      ((int32_t)teamplay & QA_Q1AD_CTF_ARMOR_PROTECT) == 0 || !friendly(state, target, attacker);
}
bool qa_q1ad_ctf_protection_applies(qa_q1ad_ctf_state *state, qa_actor_id target) {
  const char *a = qa_q1ad_ctf_team(state, target);
  const char *b = qa_q1ad_ctf_last_team(state, target);
  return a == b || team_equal(a, b);
}
bool qa_q1ad_ctf_damage_before_health(qa_q1ad_ctf_state *state, qa_actor_id target,
                                      const qa_actor_id *attacker, const qa_actor_id *inflictor, double damage) {
  double teamplay = qa_q1ad_ctf_teamplay(state);
  if (teamplay < 0 || qa_q1ad_ctf_start_map(state) || !friendly(state, target, attacker)) return true;
  /* TeamHealthDam receives full post-rune damage after the first armor and momentum commit. */
  if (attacker != NULL && ((int32_t)teamplay & QA_Q1AD_CTF_REFLECT_DAMAGE) != 0) {
    qa_actor_id fallback = qa_q1ad_ctf_world(state)->actor.id;
    const qa_actor_id *source = inflictor != NULL ? inflictor : &fallback;
    qa_q1ad_damage(state->context, *attacker, source, attacker, damage, NULL, "direct", "ctf:reflection");
  }
  return ((int32_t)teamplay & QA_Q1AD_CTF_HEALTH_PROTECT) == 0;
}
void qa_q1ad_ctf_register_combat(qa_q1ad_ctf_state *state) {
  if (state->context->game.register_rule != NULL)
    state->context->game.register_rule(state->context->game.ctx, "ctf:team_damage");
}
static void add_score(qa_q1ad_ctf_state *state, qa_actor_id actor, double delta) {
  if (state->services.add_score != NULL) state->services.add_score(state->services.ctx, actor, delta);
}
static qa_vec3 body_center(qa_q1ad_body body) {
  return qa_add3(body.origin, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5));
}
void qa_q1ad_ctf_score_death(qa_q1ad_ctf_state *state, qa_actor_id victim, const qa_actor_id *attacker) {
  qa_q1ad_context *context = state->context;
  if (!qa_q1ad_is_player(context, &victim)) return;
  if (attacker == NULL || !qa_q1ad_is_player(context, attacker) || qa_same_actor(*attacker, victim)) {
    add_score(state, victim, -1);
    return;
  }
  qa_actor_id attacker_id = *attacker;
  const char *team = qa_q1ad_ctf_team(state, attacker_id);
  bool teammates = friendly(state, victim, attacker);
  if (qa_q1ad_ctf_teamplay(state) == 2 && team != NULL &&
      team_equal(team, qa_q1ad_ctf_team(state, victim))) {
    add_score(state, attacker_id, -1);
    return;
  }
  double teamplay = qa_q1ad_ctf_teamplay(state);
  double penalty = teamplay < 0 ? -teamplay
      : teammates && ((int32_t)teamplay & QA_Q1AD_CTF_FRAG_PENALTY) != 0 ? 1 : 0;
  if (penalty > 0) add_score(state, attacker_id, -penalty);
  else {
    add_score(state, attacker_id, 1);
    if (qa_q1ad_ctf_carried(state, victim) != NULL &&
        !team_equal(qa_q1ad_ctf_team(state, victim), team)) {
      qa_q1ad_ctf_set(state, attacker_id, "lastFraggedCarrier", context->game.time);
      if (qa_q1ad_ctf_number(state, victim, "flagSince") + 2 > context->game.time)
        qa_q1ad_message(context, attacker_id, "$qc_ctf_carrier_no_bonus", false, NULL, 0);
      else {
        add_score(state, attacker_id, 2);
        qa_q1ad_msg_arg args[] = {{.is_number = true, .number = 2}};
        qa_q1ad_message(context, attacker_id, "$qc_ctf_kill_carrier", false, args, 1);
      }
    }
    const char *team_name = team == NULL ? "" : strcmp(team, "red") == 0 ? "$qc_ctf_redteam" : "$qc_ctf_blueteam";
    bool carrier_bonus = false, flag_bonus = false;
    if (qa_q1ad_ctf_number(state, victim, "lastHurtCarrier") + 4 > context->game.time &&
        qa_q1ad_ctf_carried(state, attacker_id) == NULL) {
      add_score(state, attacker_id, 2);
      carrier_bonus = true;
      qa_q1ad_ctf_announce(state, "$qc_ks_defends_carrier_aggressive", &attacker_id, team_name);
    }
    qa_vec3 centers[2] = {qa_q1ad_ctf_body(state, attacker_id).origin, qa_q1ad_ctf_body(state, victim).origin};
    for (int pass = 0; pass < 2; pass++) {
      qa_actor_id players[64];
      size_t count = qa_q1ad_players(context, players, 64);
      for (size_t i = 0; i < count; i++) {
        qa_actor_id player = players[i];
        qa_vec3 center = body_center(qa_q1ad_ctf_body(state, player));
        bool observer = state->services.observer != NULL &&
            state->services.observer(state->services.ctx, player);
        if (!carrier_bonus && !observer && !qa_same_actor(player, attacker_id) &&
            team_equal(qa_q1ad_ctf_team(state, player), team) &&
            qa_q1ad_ctf_carried(state, player) != NULL &&
            qa_length3(qa_sub3(center, centers[pass])) <= 550) {
          add_score(state, attacker_id, 1);
          carrier_bonus = true;
          qa_q1ad_ctf_announce(state, "$qc_ks_defends_carrier", &attacker_id, team_name);
        }
      }
      qa_q1ad_actor *flag = team == NULL ? NULL : qa_q1ad_ctf_flag(state, team);
      /* Preserve the QC red-team branch precedence: its second radius pass can award a second flag bonus. */
      bool eligible = !flag_bonus || (team != NULL && strcmp(team, "red") == 0 && pass == 1);
      if (eligible && flag != NULL && flag->solid != NULL && strcmp(flag->solid, "none") != 0) {
        qa_q1ad_body flag_body = qa_q1ad_body_of(context, flag);
        if (qa_length3(qa_sub3(body_center(flag_body), centers[pass])) <= 550) {
          add_score(state, attacker_id, 1);
          flag_bonus = true;
          qa_q1ad_ctf_announce(state, "$qc_ks_defends_flag", &attacker_id, team_name);
        }
      }
    }
  }
  if (teamplay >= 0 && teammates && ((int32_t)teamplay & QA_Q1AD_CTF_DEATH_PENALTY) != 0) {
    qa_q1ad_damage(context, attacker_id, &attacker_id, &attacker_id, 1000, NULL, "direct", "ctf:teamkill");
    add_score(state, attacker_id, 1);
  }
}
