/* Port of src/content/q1/addons/ctf/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/index.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/ctf/drops.h"
#include "qa/content/q1/addons/ctf/flags.h"
#include "qa/content/q1/addons/ctf/grapple.h"
#include "qa/content/q1/addons/ctf/maps.h"
#include "qa/content/q1/addons/ctf/observer.h"
#include "qa/content/q1/addons/ctf/runes.h"
#include "qa/content/q1/addons/ctf/scoring.h"
#include "qa/content/q1/addons/ctf/teams.h"
#include "qa/content/q1/addons/ctf/travel.h"
#include "qa/foundation/support.h"

void qa_q1ad_ctf_init(qa_q1ad_ctf *ctf, qa_q1ad_context *context, qa_q1ad_ctf_services services,
                      qa_shared_grapple_control *shared) {
  qa_q1ad_ctf_state_init(ctf, context, services, shared);
  if (context->program != QA_Q1AD_CTF)
    qa_range_error("CTF requires the selected CTF source program");
  qa_q1ad_ctf_register_flags(ctf);
  qa_q1ad_ctf_register_runes(ctf);
  qa_q1ad_ctf_register_combat(ctf);
  qa_q1ad_ctf_register_drops(ctf);
  qa_q1ad_ctf_register_arsenal(ctf);
  qa_q1ad_ctf_register_maps(ctf);
  if (context->game.register_rule != NULL)
    context->game.register_rule(context->game.ctx, "ctf:spawn_parameters");
}
qa_q1ad_ctf *qa_q1ad_register_ctf(qa_q1ad_ctf *ctf, qa_q1ad_context *context,
                                  qa_q1ad_ctf_services services, qa_shared_grapple_control *shared) {
  qa_q1ad_ctf_init(ctf, context, services, shared);
  return ctf;
}

/** Invoke on admission/respawn, after the selected character and arsenal are initialized. */
void qa_q1ad_ctf_spawn_player(qa_q1ad_ctf *ctf, qa_actor_id actor, bool first_admission) {
  qa_q1ad_ctf_spawn_arsenal(ctf, actor);
  if (ctf->shared_grapple != NULL && !ctf->shared_grapple->selection.is_enabled)
    qa_q1ad_ctf_grant(ctf, actor, "q1:ctf/weapon/grapple", 0, 1);
  qa_q1ad_ctf_set(ctf, actor, "lastHurtCarrier", -10);
  qa_q1ad_ctf_set(ctf, actor, "regenTime", 0);
  qa_q1ad_ctf_set(ctf, actor, "runeNotice", 0);
  if (first_admission) {
    qa_q1ad_ctf_set(ctf, actor, "killed", 0);
    qa_q1ad_ctf_set(ctf, actor, "motd", 0);
    if (((int32_t)qa_q1ad_ctf_teamplay(ctf) & QA_Q1AD_CTF_SELECT_TEAM) != 0 && !qa_q1ad_ctf_start_map(ctf))
      qa_q1ad_ctf_become_observer(ctf, actor);
    else qa_q1ad_ctf_check_team(ctf, actor);
  } else if (ctf->services.observer != NULL && ctf->services.observer(ctf->services.ctx, actor))
    qa_q1ad_ctf_become_observer(ctf, actor);
  if (qa_q1ad_ctf_native_grapple_enabled(ctf) && !qa_q1ad_ctf_start_map(ctf) &&
      ((int32_t)qa_q1ad_ctf_teamplay(ctf) & QA_Q1AD_CTF_DISABLE_GRAPPLE) == 0)
    qa_q1ad_ctf_grant(ctf, actor, "q1:ctf/weapon/grapple", 1, 1);
  qa_q1ad_ctf_set(ctf, actor, "stuffColor", 1);
  qa_q1ad_ctf_update(ctf, &actor);
}
qa_q1ad_actor *qa_q1ad_ctf_select_spawn(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  return qa_q1ad_ctf_spawn_point(ctf, actor);
}
bool qa_q1ad_ctf_fall_damage_allowed(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  return !qa_q1ad_ctf_grapple_pulling(ctf, actor);
}
/** Source prethink, called once per player by the shared session even with foreign characters. */
void qa_q1ad_ctf_player_frame(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  qa_q1ad_context *context = ctf->context;
  if (context->game.intermission) return;
  bool observer = ctf->services.observer != NULL && ctf->services.observer(ctf->services.ctx, actor);
  if (qa_q1ad_ctf_number(ctf, actor, "motd") == 2) {
    qa_q1ad_ctf_update(ctf, &actor);
    if (observer) qa_q1ad_ctf_show_team_prompt(ctf, actor);
    else {
      const char *team = qa_q1ad_ctf_team(ctf, actor);
      qa_q1ad_message(context, actor, qa_q1ad_ctf_start_map(ctf) ? "$qc_choose_exit"
          : team != NULL && strcmp(team, "red") == 0 ? "$qc_ctf_red" : "$qc_ctf_blue", false, NULL, 0);
    }
  }
  if (qa_q1ad_ctf_number(ctf, actor, "motd") <= 2)
    qa_q1ad_ctf_set(ctf, actor, "motd", qa_q1ad_ctf_number(ctf, actor, "motd") + 1);
  double forced = 103;
  bool bot_teamless = ctf->services.is_bot != NULL && ctf->services.is_bot(ctf->services.ctx, actor) &&
      qa_q1ad_ctf_team(ctf, actor) == NULL;
  if (qa_q1ad_ctf_observer_impulse(ctf, actor, bot_teamless ? &forced : NULL)) return;
  qa_q1ad_ctf_check_team_lock(ctf, actor);
  if (observer) { qa_q1ad_ctf_observer_frame(ctf, actor); return; }
  if (qa_q1ad_health(context, actor) <= 0) return;
  qa_q1ad_ctf_regenerate(ctf, actor);
}
void qa_q1ad_ctf_after_physics(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  qa_q1ad_ctf_grapple_trail(ctf, actor);
}
/** Returning true consumes only a CTF-specific command; the selected arsenal handles all others. */
bool qa_q1ad_ctf_impulse(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  qa_q1ad_context *context = ctf->context;
  qa_q1ad_ctf_input input = ctf->services.input != NULL
      ? ctf->services.input(ctf->services.ctx, actor) : (qa_q1ad_ctf_input){0};
  if (qa_q1ad_ctf_observer_impulse(ctf, actor, NULL)) return true;
  double teamplay = qa_q1ad_ctf_teamplay(ctf);
  bool observer = ctf->services.observer != NULL && ctf->services.observer(ctf->services.ctx, actor);
  if (input.impulse == 22 || (input.impulse == 1 && !input.grapple_selected)) {
    if (!qa_q1ad_ctf_native_grapple_enabled(ctf) ||
        ((int32_t)teamplay & QA_Q1AD_CTF_DISABLE_GRAPPLE) != 0)
      qa_q1ad_message(context, actor, "$qc_no_weapon", false, NULL, 0);
    else if (ctf->services.select_grapple != NULL) ctf->services.select_grapple(ctf->services.ctx, actor);
  } else if (!observer && ((int32_t)teamplay & QA_Q1AD_CTF_DROP_ITEMS) != 0 && input.impulse == 20)
    qa_q1ad_ctf_toss_ammo(ctf, actor);
  else if (!observer && ((int32_t)teamplay & QA_Q1AD_CTF_DROP_ITEMS) != 0 && input.impulse == 21)
    qa_q1ad_ctf_toss_weapon(ctf, actor);
  else if (input.impulse == 25) {
    static const struct { int flag; const char *name; } NAMES[] = {
      {QA_Q1AD_CTF_HEALTH_PROTECT, "Health-Protect"}, {QA_Q1AD_CTF_ARMOR_PROTECT, "Armor-Protect"},
      {QA_Q1AD_CTF_REFLECT_DAMAGE, "Mirror-Damage"}, {QA_Q1AD_CTF_FRAG_PENALTY, "Frag-Penalty"},
      {QA_Q1AD_CTF_DEATH_PENALTY, "Death-Penalty"}, {QA_Q1AD_CTF_STATIC_TEAMS, "Static-Teams"},
      {QA_Q1AD_CTF_DROP_ITEMS, "Drop-Items (Backpack Impulse 20, Weapon Impulse 21)"}};
    char text[256];
    if (teamplay < 0) (void)snprintf(text, sizeof text, "Frag Penalty: %d", (int)-teamplay);
    else {
      text[0] = '\0';
      for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (((int32_t)teamplay & NAMES[i].flag) != 0) {
          if (text[0] != '\0') (void)strncat(text, " ", sizeof text - strlen(text) - 1);
          (void)strncat(text, NAMES[i].name, sizeof text - strlen(text) - 1);
        }
    }
    qa_q1ad_message(context, actor, text, false, NULL, 0);
  } else return false;
  if (ctf->services.consume_impulse != NULL) ctf->services.consume_impulse(ctf->services.ctx, actor);
  return true;
}
bool qa_q1ad_ctf_attack(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  qa_q1ad_ctf_input input = ctf->services.input != NULL
      ? ctf->services.input(ctf->services.ctx, actor) : (qa_q1ad_ctf_input){0};
  bool observer = ctf->services.observer != NULL && ctf->services.observer(ctf->services.ctx, actor);
  if (!qa_q1ad_ctf_native_grapple_enabled(ctf) || !input.grapple_selected || !input.attack ||
      observer || qa_q1ad_health(ctf->context, actor) <= 0) return false;
  (void)qa_q1ad_ctf_grapple_attack(ctf, actor);
  return true;
}
/** Replaces ordinary frag credit, before dropping the carried flag needed for bonuses. */
void qa_q1ad_ctf_death(qa_q1ad_ctf *ctf, qa_actor_id victim, const qa_actor_id *attacker) {
  qa_q1ad_ctf_score_death(ctf, victim, attacker);
  qa_q1ad_ctf_set(ctf, victim, "killed", 1);
  qa_q1ad_ctf_drop_flag(ctf, victim);
  qa_q1ad_ctf_drop_rune(ctf, victim);
  qa_q1ad_ctf_unhook(ctf, victim);
}
void qa_q1ad_ctf_disconnect_player(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  qa_q1ad_ctf_drop_flag(ctf, actor);
  qa_q1ad_ctf_drop_rune(ctf, actor);
  qa_q1ad_ctf_unhook(ctf, actor);
}
void qa_q1ad_ctf_suicide(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  if ((ctf->services.observer != NULL && ctf->services.observer(ctf->services.ctx, actor)) ||
      qa_q1ad_ctf_start_map(ctf)) return;
  if (qa_q1ad_ctf_number(ctf, actor, "suicideCount") > 3) {
    qa_q1ad_message(ctf->context, actor, "$qc_ctf_too_many_suicide", false, NULL, 0);
    return;
  }
  qa_q1ad_ctf_announce(ctf, "$qc_suicides", &actor, "");
  qa_q1ad_ctf_drop_flag(ctf, actor);
  qa_q1ad_ctf_drop_rune(ctf, actor);
  qa_q1ad_ctf_unhook(ctf, actor);
  if (ctf->services.add_score != NULL) ctf->services.add_score(ctf->services.ctx, actor, -2);
  qa_q1ad_ctf_set(ctf, actor, "suicideCount", qa_q1ad_ctf_number(ctf, actor, "suicideCount") + 1);
  if (ctf->services.respawn != NULL)
    ctf->services.respawn(ctf->services.ctx, actor, qa_q1ad_ctf_select_spawn(ctf, actor));
}
