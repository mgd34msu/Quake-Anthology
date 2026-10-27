/* Port of src/content/q1/addons/ctf/arsenal.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/arsenal.h"

#include <string.h>

#include "qa/content/q1/addons/ctf/runes.h"

qa_q1ad_ctf_character_pose qa_q1ad_ctf_pose_of(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  bool selected = state->services.input != NULL &&
      state->services.input(state->services.ctx, actor).grapple_selected;
  if (!qa_q1ad_ctf_native_grapple_enabled(state) || !selected) {
    const char *weapon = state->services.selected_weapon != NULL
        ? state->services.selected_weapon(state->services.ctx, actor) : NULL;
    return (qa_q1ad_ctf_character_pose){.axe_pose = weapon != NULL && strcmp(weapon, "q1:weapon/axe") == 0};
  }
  if (!state->grapple.present) return (qa_q1ad_ctf_character_pose){.axe_pose = true};
  qa_q1ad_grapple_pose pose = {0};
  if (state->grapple.character_pose != NULL &&
      state->grapple.character_pose(state->grapple.ctx, actor, &pose))
    return (qa_q1ad_ctf_character_pose){.axe_pose = pose.axe_pose, .has_frame = pose.has_frame, .frame = pose.frame};
  return (qa_q1ad_ctf_character_pose){.axe_pose = true};
}

void qa_q1ad_ctf_spawn_arsenal(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_player *player = qa_q1ad_player_state(context, actor);
  if (player == NULL) return;
  for (size_t i = 0; i < player->powerup_count; i++)
    if (context->game.powerup != NULL)
      context->game.powerup(context->game.ctx, player->actor, player->powerup_names[i], 0);
  player->powerup_count = 0;
  qa_q1ad_ctf_grant(state, actor, "q1:key/silver", 0, 1);
  qa_q1ad_ctf_grant(state, actor, "q1:key/gold", 0, 1);
  char item[64];
  for (int i = 0; i < 4; i++)
    qa_q1ad_ctf_grant(state, actor, qa_q1ad_ctf_rune_item(QA_Q1AD_CTF_RUNES[i], item), 0, 1);
  bool start = qa_q1ad_ctf_start_map(state);
  for (int i = 0; i < QA_Q1AD_WEAPONS_COUNT; i++) {
    const char *weapon = QA_Q1AD_WEAPONS[i];
    double count = strcmp(weapon, "axe") == 0 || (strcmp(weapon, "shotgun") == 0 && !start) ? 1 : 0;
    qa_q1ad_ctf_grant(state, actor, qa_q1ad_weapon_item(weapon, item), count, 1);
  }
  qa_q1ad_ctf_grant(state, actor, "q1:ammo/shells", start ? 0 : 40, 100);
  qa_q1ad_ctf_grant(state, actor, "q1:ammo/nails", 0, 200);
  qa_q1ad_ctf_grant(state, actor, "q1:ammo/rockets", 0, 100);
  qa_q1ad_ctf_grant(state, actor, "q1:ammo/cells", 0, 100);
  bool grapple = qa_q1ad_ctf_native_grapple_enabled(state) && !start &&
      ((int32_t)qa_q1ad_ctf_teamplay(state) & QA_Q1AD_CTF_DISABLE_GRAPPLE) == 0;
  qa_q1ad_ctf_grant(state, actor, "q1:ctf/weapon/grapple", grapple ? 1 : 0, 1);
  if (context->game.combat_set_armor != NULL) {
    qa_q1ad_armor armor = start ? (qa_q1ad_armor){.kind = "none"}
        : (qa_q1ad_armor){.kind = "q1", .points = 50, .absorption = 0.3, .item = "q1:item_armor1"};
    context->game.combat_set_armor(context->game.ctx, player->actor, &armor);
  }
  if (context->game.select_weapon != NULL)
    (void)context->game.select_weapon(context->game.ctx, player->actor, start ? "axe" : "shotgun");
}

void qa_q1ad_ctf_weapon_frame(qa_q1ad_ctf_state *state, qa_actor_id actor, double frame) {
  if (state->grapple.set_weapon_frame != NULL) {
    qa_q1ad_grapple_pose pose;
    qa_q1ad_ctf_grapple_state(state, actor, &pose);
    state->grapple.set_weapon_frame(state->grapple.ctx, actor, frame);
  }
  qa_q1ad_player *player = qa_q1ad_player_state(state->context, actor);
  if (player != NULL) player->weapon_frame = frame;
  qa_q1ad_host_event event = {.kind = "weapon", .player = actor, .has_player = true,
      .weapon = "ctf:grapple", .view_model = "progs/v_star.mdl", .frame = frame, .punch = 0};
  qa_q1ad_host_emit(state->context, &event);
}
static void haste_sound(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  if (qa_q1ad_ctf_number(state, actor, "hasteSound") < state->context->game.time) {
    qa_q1ad_ctf_set(state, actor, "hasteSound", state->context->game.time + 1);
    qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
    qa_q1ad_sound_actor(state->context, owner.id, "rune/rune3.wav", "body");
  }
}
bool qa_q1ad_ctf_grapple_attack(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  bool fired = state->grapple_weapon.present && state->grapple_weapon.attack != NULL &&
      state->grapple_weapon.attack(state->grapple_weapon.ctx, actor);
  if (fired) {
    qa_q1ad_player *player = qa_q1ad_player_state(state->context, actor);
    if (player != NULL) {
      qa_q1ad_grapple_pose pose;
      qa_q1ad_ctf_grapple_state(state, actor, &pose);
      player->attack_finished = pose.attack_finished;
    }
  }
  return fired;
}
void qa_q1ad_ctf_weapon_animate(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  if (state->grapple_weapon.present && state->grapple_weapon.animate != NULL)
    state->grapple_weapon.animate(state->grapple_weapon.ctx, actor);
}
void qa_q1ad_ctf_rune_before_fire(qa_q1ad_ctf_state *state, qa_q1ad_player *player) {
  qa_actor_id actor = player->actor.id;
  const char *rune = qa_q1ad_ctf_rune(state, actor);
  if (rune == NULL || strcmp(rune, "strength") != 0 ||
      qa_q1ad_ctf_number(state, actor, "strengthSound") >= state->context->game.time) return;
  qa_q1ad_ctf_set(state, actor, "strengthSound", state->context->game.time + 1);
  double quad = qa_q1ad_player_powerup(player, "quad");
  qa_q1ad_sound_actor(state->context, player->actor.id,
      quad > state->context->game.time ? "rune/rune22.wav" : "rune/rune2.wav", "body");
}
double qa_q1ad_ctf_rune_attack_delay(qa_q1ad_ctf_state *state, qa_q1ad_player *player, double delay) {
  const char *rune = qa_q1ad_ctf_rune(state, player->actor.id);
  if (rune == NULL || strcmp(rune, "haste") != 0) return delay;
  char item[64];
  double interval;
  if (!qa_q1ad_ctf_haste_interval(
          qa_q1ad_weapon_item(player->weapon != NULL ? player->weapon : "", item), &interval))
    return delay;
  haste_sound(state, player->actor.id);
  return interval;
}
double qa_q1ad_ctf_rune_nail_speed(qa_q1ad_ctf_state *state, qa_q1ad_player *player, double speed) {
  const char *rune = qa_q1ad_ctf_rune(state, player->actor.id);
  if (rune == NULL || strcmp(rune, "haste") != 0) return speed;
  haste_sound(state, player->actor.id);
  return QA_Q1AD_CTF_HASTE_NAIL_SPEED;
}
double qa_q1ad_ctf_weapon_ammo_grant(qa_q1ad_ctf_state *state, double amount) {
  return state->context->game.coop &&
          ((int32_t)qa_q1ad_ctf_teamplay(state) & QA_Q1AD_CTF_DROP_ITEMS) != 0 ? 0 : amount;
}
bool qa_q1ad_ctf_auto_switch(qa_q1ad_ctf_state *state, qa_q1ad_player *player, bool owned) {
  bool bot = state->services.is_bot != NULL && state->services.is_bot(state->services.ctx, player->actor.id);
  const char *weapon = player->weapon != NULL ? player->weapon : "";
  const char *mode = player->auto_switch != NULL ? player->auto_switch : "";
  return !bot && !(strcmp(weapon, "ctf:grapple") == 0 && player->attack_held) &&
      strcmp(mode, "never") != 0 && (strcmp(mode, "new") != 0 || !owned);
}
double qa_q1ad_ctf_weapon_rank(const char *weapon) {
  static const char *const ORDER[] = {"lightning", "rocketlauncher", "supernailgun",
      "grenadelauncher", "supershotgun", "nailgun"};
  for (size_t i = 0; i < sizeof ORDER / sizeof ORDER[0]; i++)
    if (strcmp(weapon, ORDER[i]) == 0) return (double)i + 1;
  return 7;
}
void qa_q1ad_ctf_register_arsenal(qa_q1ad_ctf_state *state) {
  qa_q1ad_context *context = state->context;
  if (state->grapple.present) {
    if (state->weapon_factory != NULL)
      state->grapple_weapon = state->weapon_factory(state->factory_ctx, state);
    else state->grapple_weapon = (qa_q1ad_threewave_weapon){.present = true};
    if (context->game.register_rule != NULL)
      context->game.register_rule(context->game.ctx, "ctf:grapple");
  }
  if (context->game.register_rule != NULL) {
    context->game.register_rule(context->game.ctx, "ctf:rune_weapons");
    context->game.register_rule(context->game.ctx, "ctf:weapon_pickups");
  }
}
