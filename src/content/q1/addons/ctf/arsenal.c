/* Port of src/content/q1/addons/ctf/arsenal.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/arsenal.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/ctf/runes.h"
#include "qa/foundation/js.h"

static int32_t teamplay_bits(qa_q1ad_ctf_state *state) { return qa_js_to_int32(qa_q1ad_ctf_teamplay(state)); }

/** Q1 model frames only; foreign character controllers use their own grapple animation mapping. */
qa_q1ad_ctf_character_pose qa_q1ad_ctf_character_pose_of(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  bool selected = state->services.input(state->services.ctx, actor).grapple_selected;
  if (!qa_q1ad_ctf_native_grapple_enabled(state) || !selected) {
    qa_item_id weapon = state->services.selected_weapon(state->services.ctx, actor);
    return (qa_q1ad_ctf_character_pose){.axe_pose = weapon != NULL && strcmp(weapon, "q1:weapon/axe") == 0};
  }
  if (state->grapple == NULL) return (qa_q1ad_ctf_character_pose){.axe_pose = true};
  qa_q1ad_ctf_character_pose pose = {0};
  qa_q1_threewave_character_pose(state->grapple, actor, &pose.axe_pose, &pose.has_frame, &pose.frame);
  return pose;
}

void qa_q1ad_ctf_spawn_arsenal(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  qa_q1_player_state *player = qa_q1_player(game, actor);
  if (player == NULL) return;
  /* for (kind of powerups.keys()) host.powerup(...); powerups.clear(): copy the keys first. */
  qa_q1_powerup kinds[16];
  size_t kind_count = player->powerup_count;
  for (size_t i = 0; i < kind_count; i++) kinds[i] = player->powerups[i].kind;
  for (size_t i = 0; i < kind_count; i++) game->host.powerup(game->host.ctx, player->actor, kinds[i], 0);
  player->powerup_count = 0;
  qa_q1ad_ctf_grant(state, actor, "q1:key/silver", 0, 1);
  qa_q1ad_ctf_grant(state, actor, "q1:key/gold", 0, 1);
  for (int i = 0; i < 4; i++) qa_q1ad_ctf_grant(state, actor, qa_q1ad_ctf_rune_item(QA_Q1AD_CTF_RUNES[i]), 0, 1);
  for (int i = 0; i < 8; i++) {
    qa_q1_base_weapon weapon = QA_Q1_WEAPONS[i];
    bool held = strcmp(weapon, "axe") == 0 || (strcmp(weapon, "shotgun") == 0 && !qa_q1ad_ctf_start_map(state));
    qa_q1ad_ctf_grant(state, actor, qa_q1_services_weapon_item(game, weapon), held ? 1 : 0, 1);
  }
  qa_q1ad_ctf_grant(state, actor, "q1:ammo/shells", qa_q1ad_ctf_start_map(state) ? 0 : 40, 100);
  qa_q1ad_ctf_grant(state, actor, "q1:ammo/nails", 0, 200);
  qa_q1ad_ctf_grant(state, actor, "q1:ammo/rockets", 0, 100);
  qa_q1ad_ctf_grant(state, actor, "q1:ammo/cells", 0, 100);
  bool grapple = qa_q1ad_ctf_native_grapple_enabled(state) && !qa_q1ad_ctf_start_map(state) &&
                 !(teamplay_bits(state) & QA_Q1AD_CTF_DISABLE_GRAPPLE);
  qa_q1ad_ctf_grant(state, actor, "q1:ctf/weapon/grapple", grapple ? 1 : 0, 1);
  qa_armor_state armor = {.powered = {.kind = "none"}};
  if (qa_q1ad_ctf_start_map(state)) armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  else armor.regular = (qa_regular_armor_state){.kind = QA_REGULAR_ARMOR_Q1, .points = 50, .absorption = 0.3,
                                                .item = "q1:item_armor1"};
  qa_gameplay_authority_set_armor(game->host.combat, player->actor, armor);
  (void)qa_q1_select_weapon(game, player->actor, qa_q1ad_ctf_start_map(state) ? "axe" : "shotgun");
}

static void weapon_frame(void *ctx, qa_actor_id actor, double frame) {
  qa_q1ad_ctf_state *state = ctx;
  qa_q1_services *game = state->context->game;
  qa_q1ad_ctf_grapple_state(state, actor)->weapon_frame = frame;
  qa_q1_player_state *player = qa_q1_player(game, actor);
  if (player != NULL) player->weapon_frame = frame;
  qa_q1_event event = {.kind = "weapon", .has_player = true, .player = actor, .weapon = "ctf:grapple",
                       .view_model = "progs/v_star.mdl", .frame = frame, .punch = 0};
  qa_q1_emit(game, event);
}
static void haste_sound(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  if (qa_q1ad_ctf_number(state, actor, "hasteSound") < game->time) {
    qa_q1ad_ctf_set(state, actor, "hasteSound", game->time + 1);
    qa_q1_sound(game, qa_q1ad_ctf_owner(state, actor).id, "rune/rune3.wav", "body", 1, 1);
  }
}
bool qa_q1ad_ctf_grapple_attack(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  bool fired = state->grapple_weapon != NULL && qa_q1_threewave_weapon_attack(state->grapple_weapon, actor);
  if (fired) {
    qa_q1_player_state *player = qa_q1_player(game, actor);
    if (player != NULL) {
      player->attack_finished = qa_q1ad_ctf_grapple_state(state, actor)->attack_finished;
      player->hostile_until = (double)(float)(game->time + 1);
    }
  }
  return fired;
}

/* ThreewaveWeapon hooks. */
static bool hook_selected(void *ctx, qa_actor_id actor) {
  qa_q1ad_ctf_state *state = ctx;
  return state->services.input(state->services.ctx, actor).grapple_selected;
}
static bool hook_available(void *ctx, qa_actor_id actor) {
  qa_q1ad_ctf_state *state = ctx;
  return (teamplay_bits(state) & QA_Q1AD_CTF_DISABLE_GRAPPLE) == 0 && !state->services.observer(state->services.ctx, actor);
}
/* registerWeapon("ctf:grapple"). */
static bool grapple_available(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  (void)game; (void)player; /* TS closure takes no arguments */
  return (teamplay_bits(ctx) & QA_Q1AD_CTF_DISABLE_GRAPPLE) == 0;
}
static bool grapple_best_available(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  (void)ctx; (void)game; (void)player; /* TS () => false */
  return false;
}
static bool grapple_fire(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  (void)game; /* TS ignores _runtime */
  return qa_q1ad_ctf_grapple_attack(ctx, player->actor.id);
}
static void grapple_animate(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double seconds) {
  (void)game; (void)seconds; /* TS ignores _runtime and the frame time */
  qa_q1ad_ctf_state *state = ctx;
  qa_q1_threewave_weapon_animate(state->grapple_weapon, player->actor.id);
}
/* registerWeaponRules("ctf:rune_weapons"). */
static bool is_rune(const char *rune, const char *kind) { return rune != NULL && strcmp(rune, kind) == 0; }
static void rune_before_fire(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  qa_q1ad_ctf_state *state = ctx;
  qa_actor_id actor = player->actor.id;
  if (!is_rune(qa_q1ad_ctf_rune(state, actor), "strength") || qa_q1ad_ctf_number(state, actor, "strengthSound") >= game->time)
    return;
  qa_q1ad_ctf_set(state, actor, "strengthSound", game->time + 1);
  double quad = qa_q1_player_powerup_get(player, "quad");
  qa_q1_sound(game, player->actor.id, quad > game->time ? "rune/rune22.wav" : "rune/rune2.wav", "body", 1, 1);
}
static double rune_attack_delay(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double delay) {
  qa_q1ad_ctf_state *state = ctx;
  if (!is_rune(qa_q1ad_ctf_rune(state, player->actor.id), "haste")) return delay;
  double source;
  if (!qa_q1ad_ctf_haste_interval(qa_q1_services_weapon_item(game, player->weapon), &source)) return delay;
  haste_sound(state, player->actor.id);
  return source;
}
static double rune_nail_speed(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double speed) {
  (void)game; /* TS ignores _runtime */
  qa_q1ad_ctf_state *state = ctx;
  if (!is_rune(qa_q1ad_ctf_rune(state, player->actor.id), "haste")) return speed;
  haste_sound(state, player->actor.id);
  return QA_Q1AD_CTF_HASTE_NAIL_SPEED;
}
/* registerPickupRules("ctf:weapon_pickups"). */
static double pickup_ammo_grant(void *ctx, qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon weapon,
                                double amount) {
  (void)player; (void)weapon; /* TS ignores _player and _weapon */
  return game->options.coop && (teamplay_bits(ctx) & QA_Q1AD_CTF_DROP_ITEMS) != 0 ? 0 : amount;
}
static bool pickup_auto_switch(void *ctx, qa_q1_services *game, qa_q1_player_state *player, bool owned) {
  (void)game; /* TS ignores _runtime */
  qa_q1ad_ctf_state *state = ctx;
  const char *mode = player->auto_switch != NULL ? player->auto_switch : "";
  return !state->services.is_bot(state->services.ctx, player->actor.id) &&
         !(player->weapon != NULL && strcmp(player->weapon, "ctf:grapple") == 0 && player->attack_held) &&
         strcmp(mode, "never") != 0 && (strcmp(mode, "new") != 0 || !owned);
}
static double pickup_weapon_rank(void *ctx, qa_q1_weapon weapon) {
  (void)ctx; /* TS closure reads no state */
  static const char *const ORDER[] = {"lightning", "rocketlauncher", "supernailgun", "grenadelauncher",
                                      "supershotgun", "nailgun"};
  for (size_t i = 0; i < sizeof ORDER / sizeof ORDER[0]; i++)
    if (strcmp(weapon, ORDER[i]) == 0) return (double)i + 1;
  return 7;
}

bool qa_q1ad_ctf_register_arsenal(qa_q1ad_ctf_state *state) {
  qa_q1_services *game = state->context->game;
  if (state->grapple != NULL) {
    qa_q1_threewave_weapon *weapon = malloc(sizeof *weapon);
    if (weapon == NULL) return false;
    qa_q1_threewave_weapon_init(weapon, state->grapple,
                                (qa_q1_threewave_weapon_hooks){.ctx = state, .selected = hook_selected,
                                                               .available = hook_available, .frame = weapon_frame});
    state->grapple_weapon = weapon;
    state->grapple_definition = (qa_q1_weapon_definition){
        .ctx = state, .id = "ctf:grapple", .item = "q1:ctf/weapon/grapple", .ammo = NULL,
        .model = "progs/v_star.mdl", .rank = 0, .available = grapple_available,
        .best_available = grapple_best_available, .fire = grapple_fire, .animate = grapple_animate};
    qa_q1_register_weapon(game, &state->grapple_definition);
  }
  state->rune_weapons = (qa_q1_weapon_rules){.ctx = state, .id = "ctf:rune_weapons", .before_fire = rune_before_fire,
                                             .attack_delay = rune_attack_delay, .nail_speed = rune_nail_speed};
  qa_q1_register_weapon_rules(game, &state->rune_weapons);
  state->weapon_pickups = (qa_q1_pickup_rules){.ctx = state, .id = "ctf:weapon_pickups",
                                               .weapon_ammo_grant = pickup_ammo_grant,
                                               .auto_switch = pickup_auto_switch, .weapon_rank = pickup_weapon_rank};
  qa_q1_register_pickup_rules(game, &state->weapon_pickups);
  return true;
}
