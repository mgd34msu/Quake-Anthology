/* Port of src/content/q1/addons/ctf/scoring.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/scoring.h"

#include <string.h>

#include "qa/foundation/js.h"

static bool same_team(const char *a, const char *b) { return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0); }
static int32_t teamplay_bits(qa_q1ad_ctf_state *state) { return qa_js_to_int32(qa_q1ad_ctf_teamplay(state)); }

static bool friendly(qa_q1ad_ctf_state *state, qa_actor_id target, const qa_actor_id *attacker) {
  return attacker != NULL && !qa_same_actor(target, *attacker) && qa_q1ad_ctf_last_team(state, target) != NULL &&
         same_team(qa_q1ad_ctf_last_team(state, target), qa_q1ad_ctf_last_team(state, *attacker));
}
static bool team_before_quad(void *ctx, qa_damage_request request, double amount, qa_combat_state target,
                             const qa_combat_state *attacker, qa_damage_preparation *out) {
  (void)target; (void)attacker; /* TS beforeQuad reads (request, amount) */
  const qa_attack_cause *cause = &request.attack.cause;
  bool falling = (cause->kind == QA_ATTACK_CAUSE_Q1 && cause->death_type != NULL && strcmp(cause->death_type, "falling") == 0) ||
                 (cause->kind == QA_ATTACK_CAUSE_ENVIRONMENT && cause->hazard != NULL && strcmp(cause->hazard, "fall") == 0);
  *out = falling && qa_q1ad_ctf_grapple_pulling(ctx, request.target) ? (qa_damage_preparation){.is_continue = false}
                                                                      : (qa_damage_preparation){.is_continue = true, .amount = amount};
  return true;
}
static bool team_armor_allowed(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                               const qa_combat_state *attacker) {
  (void)damage; (void)target; (void)attacker; /* TS armorAllowed reads request */
  qa_q1ad_ctf_state *state = ctx;
  return qa_q1ad_ctf_teamplay(state) < 0 || qa_q1ad_ctf_start_map(state) ||
         !(teamplay_bits(state) & QA_Q1AD_CTF_ARMOR_PROTECT) || !friendly(state, request.target, request.attack.attacker);
}
static bool team_protection_applies(void *ctx, qa_damage_request request, qa_combat_state target,
                                    const qa_combat_state *attacker) {
  (void)target; (void)attacker; /* TS protectionApplies reads request */
  qa_q1ad_ctf_state *state = ctx;
  const char * a = qa_q1ad_ctf_team(state, request.target);
  const char * b = qa_q1ad_ctf_last_team(state, request.target);
  return same_team(a, b);
}
static bool team_before_health(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                               const qa_combat_state *attacker_state) {
  (void)target; (void)attacker_state; /* TS beforeHealth reads (request, damage) */
  qa_q1ad_ctf_state *state = ctx;
  const qa_actor_id *attacker = request.attack.attacker;
  if (qa_q1ad_ctf_teamplay(state) < 0 || qa_q1ad_ctf_start_map(state) || !friendly(state, request.target, attacker)) return true;
  /* TeamHealthDam receives full post-rune damage after the first armor and momentum commit. */
  if (attacker != NULL && (teamplay_bits(state) & QA_Q1AD_CTF_REFLECT_DAMAGE)) {
    qa_actor_id inflictor = request.attack.inflictor != NULL ? *request.attack.inflictor : qa_q1ad_ctf_world(state)->actor.id;
    (void)qa_q1_damage(state->context->game, *attacker, inflictor, attacker, damage, NULL, "direct", "ctf:reflection", NULL);
  }
  return (teamplay_bits(state) & QA_Q1AD_CTF_HEALTH_PROTECT) == 0;
}
void qa_q1ad_ctf_register_combat(qa_q1ad_ctf_state *state) {
  qa_q1_register_damage_source_effects(state->context->game, "ctf:team_damage",
                                       (qa_q1_damage_source_effects){.ctx = state, .before_quad = team_before_quad,
                                                                     .armor_allowed = team_armor_allowed,
                                                                     .protection_applies = team_protection_applies,
                                                                     .before_health = team_before_health});
}

static qa_vec3 body_center(qa_q1_body body) {
  return qa_q1_vadd(body.origin, qa_q1_vscale(qa_q1_vadd(body.bounds.min, body.bounds.max), 0.5));
}
/** Called once by the score owner before death drops; it replaces the ordinary Q1 frag decision. */
void qa_q1ad_ctf_score_death(qa_q1ad_ctf_state *state, qa_actor_id victim, const qa_actor_id *attacker) {
  qa_q1_services *game = state->context->game;
  qa_q1ad_ctf_services *services = &state->services;
  if (!qa_q1_is_player(game, &victim)) return;
  /* EXPERT CTF: a dead carrier clears the opposing players' carrier-hurt timers. */
  if (qa_q1ad_ctf_carried(state, victim) != NULL) {
    const char * a2;
    const char * b;
    size_t count = 0;
    const qa_actor_id *players = game->host.players(game->host.ctx, &count);
    for (size_t i = 0; i < count; i++)
      if (!(a2 = qa_q1ad_ctf_team(state, players[i]), b = qa_q1ad_ctf_team(state, victim), same_team(a2, b)))
        qa_q1ad_ctf_set(state, players[i], "lastHurtCarrier", -10);
  }
  /* client.qc ClientObituary: a telefrag credits the owner of the teledeath and charges the victim nothing. */
  if (attacker != NULL) {
    const char *classname = game->host.classname(game->host.ctx, *attacker);
    if (classname != NULL && strcmp(classname, "teledeath") == 0) {
      qa_q1_actor *teledeath = qa_q1_get_entity(game, attacker);
      if (teledeath != NULL && teledeath->has_owner && qa_q1_is_player(game, &teledeath->owner))
        services->add_score(services->ctx, teledeath->owner, 1);
      return;
    }
  }
  if (attacker == NULL || !qa_q1_is_player(game, attacker) || qa_same_actor(*attacker, victim)) {
    services->add_score(services->ctx, victim, -1);
    return;
  }
  qa_actor_id attacker_id = *attacker;
  const char *team = qa_q1ad_ctf_team(state, attacker_id);
  bool teammates = friendly(state, victim, attacker);
  if (qa_q1ad_ctf_teamplay(state) == 2 && team != NULL && same_team(team, qa_q1ad_ctf_team(state, victim))) {
    services->add_score(services->ctx, attacker_id, -1);
    return;
  }
  double teamplay = qa_q1ad_ctf_teamplay(state);
  double penalty = teamplay < 0 ? -teamplay : teammates && (teamplay_bits(state) & QA_Q1AD_CTF_FRAG_PENALTY) ? 1 : 0;
  if (penalty > 0) services->add_score(services->ctx, attacker_id, -penalty);
  else {
    services->add_score(services->ctx, attacker_id, 1);
    if (qa_q1ad_ctf_carried(state, victim) != NULL && !same_team(qa_q1ad_ctf_team(state, victim), team)) {
      qa_q1ad_ctf_set(state, attacker_id, "lastFraggedCarrier", game->time);
      if (qa_q1ad_ctf_number(state, victim, "flagSince") + 2 > game->time)
        qa_q1_message(game, &attacker_id, "$qc_ctf_carrier_no_bonus", true, NULL, 0);
      else {
        services->add_score(services->ctx, attacker_id, 2);
        qa_q1_msg_arg args[1] = {{.is_number = true, .number = 2}};
        qa_q1_message(game, &attacker_id, "$qc_ctf_kill_carrier", false, args, 1);
      }
    }
    const char *team_name = same_team(team, "red") ? "$qc_ctf_redteam" : same_team(team, "blue") ? "$qc_ctf_blueteam" : "";
    bool carrier_bonus = false, flag_bonus = false;
    if (qa_q1ad_ctf_number(state, victim, "lastHurtCarrier") + 4 > game->time && qa_q1ad_ctf_carried(state, attacker_id) == NULL) {
      services->add_score(services->ctx, attacker_id, 2);
      carrier_bonus = true;
      qa_q1ad_ctf_announce(state, "$qc_ks_defends_carrier_aggressive", &attacker_id, team_name);
    }
    qa_vec3 centers[2];
    centers[0] = qa_q1ad_ctf_body(state, attacker_id).origin;
    centers[1] = qa_q1ad_ctf_body(state, victim).origin;
    for (int pass = 0; pass < 2; pass++) {
      size_t count = 0;
      const qa_actor_id *players = game->host.players(game->host.ctx, &count);
      for (size_t i = 0; i < count; i++) {
        qa_actor_id player = players[i];
        qa_vec3 player_center = body_center(qa_q1ad_ctf_body(state, player));
        if (!carrier_bonus && !services->observer(services->ctx, player) && !qa_same_actor(player, attacker_id) &&
            same_team(qa_q1ad_ctf_team(state, player), team) && qa_q1ad_ctf_carried(state, player) != NULL &&
            qa_q1_length(qa_q1_vsub(player_center, centers[pass])) <= 550) {
          services->add_score(services->ctx, attacker_id, 1);
          carrier_bonus = true;
          qa_q1ad_ctf_announce(state, "$qc_ks_defends_carrier", &attacker_id, team_name);
        }
      }
      qa_q1_actor *flag = team == NULL ? NULL : qa_q1ad_ctf_flag(state, team);
      bool has_flag_body = flag != NULL;
      qa_q1_body flag_body = has_flag_body ? qa_q1_get_body(game, flag) : (qa_q1_body){0};
      /* Preserve the QC red-team branch precedence: its second radius pass can award a second flag bonus. */
      if ((!flag_bonus || (same_team(team, "red") && pass == 1)) && flag != NULL && strcmp(flag->solid, "none") != 0 &&
          has_flag_body && qa_q1_length(qa_q1_vsub(body_center(flag_body), centers[pass])) <= 550) {
        services->add_score(services->ctx, attacker_id, 1);
        flag_bonus = true;
        qa_q1ad_ctf_announce(state, "$qc_ks_defends_flag", &attacker_id, team_name);
      }
    }
  }
  if (qa_q1ad_ctf_teamplay(state) >= 0 && teammates && (teamplay_bits(state) & QA_Q1AD_CTF_DEATH_PENALTY)) {
    (void)qa_q1_damage(game, attacker_id, attacker_id, &attacker_id, 1000, NULL, "direct", "ctf:teamkill", NULL);
    services->add_score(services->ctx, attacker_id, 1);
  }
}
