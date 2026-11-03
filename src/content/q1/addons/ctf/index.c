/* Port of src/content/q1/addons/ctf/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/index.h"

#include <stdio.h>
#include <stdlib.h>
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
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

static int32_t teamplay_bits(qa_q1ad_ctf *ctf) { return qa_js_to_int32(qa_q1ad_ctf_teamplay(ctf)); }
static bool observer(qa_q1ad_ctf *ctf, qa_actor_id actor) { return ctf->services.observer(ctf->services.ctx, actor); }

/* registerPlayerExtension("ctf:spawn_parameters"). */
static bool extension_capture_travel(void *ctx, qa_q1_services *game, qa_q1_player_state *player, uint8_t **out,
                                     size_t *out_len) {
  (void)game; /* TS ignores _runtime */
  *out = qa_q1ad_ctf_capture_travel(ctx, player->actor.id, out_len);
  return *out != NULL;
}
static void extension_restore_travel(void *ctx, qa_q1_services *game, qa_q1_player_state *player, const uint8_t *bytes,
                                     size_t len) {
  (void)game; /* TS ignores _runtime */
  qa_q1ad_ctf_restore_travel(ctx, player->actor.id, bytes, len);
}

qa_q1ad_ctf *qa_q1ad_register_ctf(qa_q1ad_context *context, qa_q1ad_ctf_services services,
                                  const qa_shared_grapple_control *shared) {
  qa_q1ad_ctf *ctf = malloc(sizeof *ctf);
  if (ctf == NULL) return NULL;
  if (!qa_q1ad_ctf_state_init(ctf, context, services, shared)) {
    qa_q1ad_ctf_state_free(ctf);
    free(ctf);
    return NULL;
  }
  if (context->program != QA_Q1AD_CTF) qa_range_error("CTF requires the selected CTF source program");
  qa_q1ad_ctf_register_flags(ctf);
  qa_q1ad_ctf_register_runes(ctf);
  qa_q1ad_ctf_register_combat(ctf);
  qa_q1ad_ctf_register_drops(ctf);
  if (!qa_q1ad_ctf_register_arsenal(ctf)) qa_range_error("out of memory");
  qa_q1ad_ctf_register_maps(ctf);
  ctf->spawn_parameters = (qa_q1_player_extension){.ctx = ctf, .id = "ctf:spawn_parameters",
                                                   .capture_travel = extension_capture_travel,
                                                   .restore_travel = extension_restore_travel};
  qa_q1_register_player_extension(context->game, &ctf->spawn_parameters);
  return ctf;
}
void qa_q1ad_ctf_destroy(qa_q1ad_ctf *ctf) {
  if (ctf == NULL) return;
  qa_q1ad_ctf_state_free(ctf);
  free(ctf);
}

/** Invoke on admission/respawn, after the selected character and arsenal are initialized. */
void qa_q1ad_ctf_spawn_player(qa_q1ad_ctf *ctf, qa_actor_id actor, bool first_admission) {
  qa_q1ad_ctf_spawn_arsenal(ctf, actor);
  if (ctf->shared_grapple != NULL && !ctf->shared_grapple->selection.is_enabled)
    qa_q1ad_ctf_grant(ctf, actor, "q1:ctf/weapon/grapple", 0, 1);
  static const char *const RESET[4] = {"lastHurtCarrier", "lastReturned", "lastFraggedCarrier", "flagSince"};
  for (int i = 0; i < 4; i++) qa_q1ad_ctf_set(ctf, actor, RESET[i], -10);
  qa_q1ad_ctf_set(ctf, actor, "regenTime", 0);
  qa_q1ad_ctf_set(ctf, actor, "runeNotice", 0);
  if (first_admission) {
    qa_q1ad_ctf_set(ctf, actor, "killed", 0);
    qa_q1ad_ctf_set(ctf, actor, "motd", 0);
    if ((teamplay_bits(ctf) & QA_Q1AD_CTF_SELECT_TEAM) != 0 && !qa_q1ad_ctf_start_map(ctf)) qa_q1ad_ctf_become_observer(ctf, actor);
    else qa_q1ad_ctf_check_team(ctf, actor);
  } else if (observer(ctf, actor)) qa_q1ad_ctf_become_observer(ctf, actor);
  if (qa_q1ad_ctf_native_grapple_enabled(ctf) && !qa_q1ad_ctf_start_map(ctf) && !(teamplay_bits(ctf) & QA_Q1AD_CTF_DISABLE_GRAPPLE))
    qa_q1ad_ctf_grant(ctf, actor, "q1:ctf/weapon/grapple", 1, 1);
  qa_q1ad_ctf_set(ctf, actor, "stuffColor", 1);
  qa_q1ad_ctf_update(ctf, &actor);
}
qa_q1_actor *qa_q1ad_ctf_select_spawn(qa_q1ad_ctf *ctf, qa_actor_id actor) { return qa_q1ad_ctf_spawn_point(ctf, actor); }
qa_q1ad_ctf_character_pose qa_q1ad_ctf_player_pose(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  return qa_q1ad_ctf_character_pose_of(ctf, actor);
}
bool qa_q1ad_ctf_fall_damage_allowed(qa_q1ad_ctf *ctf, qa_actor_id actor) { return !qa_q1ad_ctf_grapple_pulling(ctf, actor); }
uint8_t *qa_q1ad_ctf_capture_player_travel(qa_q1ad_ctf *ctf, qa_actor_id actor, size_t *out_length) {
  return qa_q1ad_ctf_capture_travel(ctf, actor, out_length);
}
void qa_q1ad_ctf_restore_player_travel(qa_q1ad_ctf *ctf, qa_actor_id actor, const uint8_t *bytes, size_t length) {
  qa_q1ad_ctf_restore_travel(ctf, actor, bytes, length);
}
/** Source prethink, called once per player by the shared session even with foreign characters. */
void qa_q1ad_ctf_player_frame(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  qa_q1_services *game = ctf->context->game;
  if (game->has_intermission) return;
  if (qa_q1ad_ctf_number(ctf, actor, "motd") == 2) {
    qa_q1ad_ctf_update(ctf, &actor);
    if (observer(ctf, actor)) qa_q1ad_ctf_show_team_prompt(ctf, actor);
    else {
      const char *team = qa_q1ad_ctf_start_map(ctf) ? NULL : qa_q1ad_ctf_team(ctf, actor);
      const char *text = qa_q1ad_ctf_start_map(ctf) ? "$qc_choose_exit"
                         : team != NULL && strcmp(team, "red") == 0 ? "$qc_ctf_red" : "$qc_ctf_blue";
      qa_q1_message(game, &actor, text, true, NULL, 0);
    }
  }
  if (qa_q1ad_ctf_number(ctf, actor, "motd") <= 2)
    qa_q1ad_ctf_set(ctf, actor, "motd", qa_q1ad_ctf_number(ctf, actor, "motd") + 1);
  double forced = 103;
  bool bot_teamless = ctf->services.is_bot(ctf->services.ctx, actor) && qa_q1ad_ctf_team(ctf, actor) == NULL;
  if (qa_q1ad_ctf_observer_impulse(ctf, actor, bot_teamless ? &forced : NULL)) return;
  qa_q1ad_ctf_check_team_lock(ctf, actor);
  if (observer(ctf, actor)) { qa_q1ad_ctf_observer_frame(ctf, actor); return; }
  if (qa_q1_health(game, actor) <= 0) return;
  qa_q1ad_ctf_regenerate(ctf, actor);
}
void qa_q1ad_ctf_after_physics(qa_q1ad_ctf *ctf, qa_actor_id actor) { qa_q1ad_ctf_grapple_trail(ctf, actor); }
/** Returning true consumes only a CTF-specific command; the selected arsenal handles all others. */
bool qa_q1ad_ctf_impulse(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  qa_q1_services *game = ctf->context->game;
  qa_q1ad_ctf_input input = ctf->services.input(ctf->services.ctx, actor);
  if (qa_q1ad_ctf_observer_impulse(ctf, actor, NULL)) return true;
  if (input.impulse == 22 || (input.impulse == 1 && !input.grapple_selected)) {
    if (!qa_q1ad_ctf_native_grapple_enabled(ctf) || (teamplay_bits(ctf) & QA_Q1AD_CTF_DISABLE_GRAPPLE))
      qa_q1_message(game, &actor, "$qc_no_weapon", true, NULL, 0);
    else ctf->services.select_grapple(ctf->services.ctx, actor);
  } else if (input.impulse == 31) {
    qa_q1_player_state *player = qa_q1_player(game, actor);
    if (player != NULL) (void)qa_q1_select_weapon(game, player->actor, "axe");
  } else if (input.impulse == 99) {
    /* `You are at '${x.toFixed(1).padStart(5)} ${y...} ${z...}'\n` */
    qa_vec3 origin = qa_q1ad_ctf_body(ctf, actor).origin;
    char x[QA_JS_TO_FIXED_CAPACITY], y[QA_JS_TO_FIXED_CAPACITY], z[QA_JS_TO_FIXED_CAPACITY];
    char text[3 * QA_JS_TO_FIXED_CAPACITY + 32];
    (void)qa_js_to_fixed(origin.x, 1, x, sizeof x);
    (void)qa_js_to_fixed(origin.y, 1, y, sizeof y);
    (void)qa_js_to_fixed(origin.z, 1, z, sizeof z);
    (void)snprintf(text, sizeof text, "You are at '%5s %5s %5s'\n", x, y, z);
    qa_q1_message(game, &actor, text, false, NULL, 0);
  } else if (!observer(ctf, actor) && (teamplay_bits(ctf) & QA_Q1AD_CTF_DROP_ITEMS) && input.impulse == 20)
    qa_q1ad_ctf_toss_ammo(ctf, actor);
  else if (!observer(ctf, actor) && (teamplay_bits(ctf) & QA_Q1AD_CTF_DROP_ITEMS) && input.impulse == 21)
    qa_q1ad_ctf_toss_weapon(ctf, actor);
  else if (input.impulse == 25) {
    static const struct { int flag; const char *name; } NAMES[] = {
      {QA_Q1AD_CTF_HEALTH_PROTECT, "Health-Protect"}, {QA_Q1AD_CTF_ARMOR_PROTECT, "Armor-Protect"},
      {QA_Q1AD_CTF_REFLECT_DAMAGE, "Mirror-Damage"}, {QA_Q1AD_CTF_FRAG_PENALTY, "Frag-Penalty"},
      {QA_Q1AD_CTF_DEATH_PENALTY, "Death-Penalty"}, {QA_Q1AD_CTF_STATIC_TEAMS, "Static-Teams"},
      {QA_Q1AD_CTF_DROP_ITEMS, "Drop-Items (Backpack Impulse 20, Weapon Impulse 21)"}};
    char text[256];
    double teamplay = qa_q1ad_ctf_teamplay(ctf);
    if (teamplay < 0) {
      char number[64];
      qa_net_js_number(-teamplay, number, sizeof number); /* `Frag Penalty: ${-teamplay}` */
      (void)snprintf(text, sizeof text, "Frag Penalty: %s", number);
    } else {
      text[0] = '\0';
      for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if ((qa_js_to_int32(teamplay) & NAMES[i].flag) != 0) {
          if (text[0] != '\0') (void)strncat(text, " ", sizeof text - strlen(text) - 1);
          (void)strncat(text, NAMES[i].name, sizeof text - strlen(text) - 1);
        }
    }
    qa_q1_message(game, &actor, text, false, NULL, 0);
  } else return false;
  ctf->services.consume_impulse(ctf->services.ctx, actor);
  return true;
}
bool qa_q1ad_ctf_attack(qa_q1ad_ctf *ctf, qa_actor_id actor) {
  qa_q1ad_ctf_input input = ctf->services.input(ctf->services.ctx, actor);
  if (!qa_q1ad_ctf_native_grapple_enabled(ctf) || !input.grapple_selected || !input.attack || observer(ctf, actor) ||
      qa_q1_health(ctf->context->game, actor) <= 0)
    return false;
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
  if (observer(ctf, actor) || qa_q1ad_ctf_start_map(ctf)) return;
  if (qa_q1ad_ctf_number(ctf, actor, "suicideCount") > 3) {
    qa_q1_message(ctf->context->game, &actor, "$qc_ctf_too_many_suicide", true, NULL, 0);
    return;
  }
  qa_q1ad_ctf_announce(ctf, "$qc_suicides", &actor, "");
  qa_q1ad_ctf_drop_flag(ctf, actor);
  qa_q1ad_ctf_drop_rune(ctf, actor);
  qa_q1ad_ctf_unhook(ctf, actor);
  ctf->services.add_score(ctf->services.ctx, actor, -2);
  qa_q1ad_ctf_set(ctf, actor, "suicideCount", qa_q1ad_ctf_number(ctf, actor, "suicideCount") + 1);
  ctf->services.respawn(ctf->services.ctx, actor, qa_q1ad_ctf_select_spawn(ctf, actor));
}
