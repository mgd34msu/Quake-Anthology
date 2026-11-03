/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_combat.h"
#include "ai_access_internal.h"
#include "qa/bots/behavior/library/chat.h"
#include "qa/bots/behavior/library/libvars.h"
#include "qa/bots/behavior/library/weapons.h"
#include "qa/bots/behavior/q3/ai_chat.h"
#include "qa/bots/behavior/q3/ai_command.h"
#include "qa/bots/behavior/q3/ai_decision.h"
#include "qa/bots/behavior/q3/ai_main.h"
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "qa/bots/behavior/q3/ai_orders.h"
#include "qa/bots/behavior/q3/ai_team.h"
#include "qa/bots/behavior/q3/info.h"
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/team_arena/team.h"
#include "qa/core/game_numeric.h"
#include "qa/core/info_string.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

enum {
  SOLID = 1,
  LAVA = 8,
  SLIME = 16,
  WATER = 32,
  FOG = 64,
  PLAYERCLIP = 0x10000,
  LIQUID = LAVA | SLIME | WATER,
  MASK_SHOT = SOLID | 0x2000000 | 0x4000000,
  EF_TELEPORT_BIT = 4,
  EF_KAMIKAZE = 0x200,
  EF_FIRING = 0x100,
  EF_TALK = 0x1000,
  CS_SOUNDS = 288,
  CS_PLAYERS = 544,
  CHAT_NAME_SIZE = 64
};

static const int weapon_inventory[][2] = {
    {QA_INV_GAUNTLET, QA_Q3_WP_GAUNTLET},
    {QA_INV_SHOTGUN, QA_Q3_WP_SHOTGUN},
    {QA_INV_MACHINEGUN, QA_Q3_WP_MACHINEGUN},
    {QA_INV_GRENADELAUNCHER, QA_Q3_WP_GRENADE_LAUNCHER},
    {QA_INV_ROCKETLAUNCHER, QA_Q3_WP_ROCKET_LAUNCHER},
    {QA_INV_LIGHTNING, QA_Q3_WP_LIGHTNING},
    {QA_INV_RAILGUN, QA_Q3_WP_RAILGUN},
    {QA_INV_PLASMAGUN, QA_Q3_WP_PLASMAGUN},
    {QA_INV_BFG10K, QA_Q3_WP_BFG},
    {QA_INV_GRAPPLINGHOOK, QA_Q3_WP_GRAPPLING_HOOK},
    {QA_INV_NAILGUN, QA_Q3_WP_NAILGUN},
    {QA_INV_PROXLAUNCHER, QA_Q3_WP_PROX_LAUNCHER},
    {QA_INV_CHAINGUN, QA_Q3_WP_CHAINGUN},
};
static const int ammo_inventory[][2] = {
    {QA_INV_SHELLS, QA_Q3_WP_SHOTGUN},
    {QA_INV_BULLETS, QA_Q3_WP_MACHINEGUN},
    {QA_INV_GRENADES, QA_Q3_WP_GRENADE_LAUNCHER},
    {QA_INV_CELLS, QA_Q3_WP_PLASMAGUN},
    {QA_INV_LIGHTNINGAMMO, QA_Q3_WP_LIGHTNING},
    {QA_INV_ROCKETS, QA_Q3_WP_ROCKET_LAUNCHER},
    {QA_INV_SLUGS, QA_Q3_WP_RAILGUN},
    {QA_INV_BFGAMMO, QA_Q3_WP_BFG},
    {QA_INV_NAILS, QA_Q3_WP_NAILGUN},
    {QA_INV_MINES, QA_Q3_WP_PROX_LAUNCHER},
    {QA_INV_BELT, QA_Q3_WP_CHAINGUN},
};
static const int holdable_inventory[][2] = {
    {QA_INV_TELEPORTER, QA_MI_TELEPORTER},
    {QA_INV_MEDKIT, QA_MI_MEDKIT},
    {QA_INV_KAMIKAZE, QA_MI_KAMIKAZE},
    {QA_INV_PORTAL, QA_MI_PORTAL},
    {QA_INV_INVULNERABILITY, QA_MI_INVULNERABILITY},
};
static const int powerup_inventory[][2] = {
    {QA_INV_QUAD, QA_Q3_PW_QUAD},
    {QA_INV_ENVIRONMENTSUIT, QA_Q3_PW_BATTLESUIT},
    {QA_INV_HASTE, QA_Q3_PW_HASTE},
    {QA_INV_INVISIBILITY, QA_Q3_PW_INVIS},
    {QA_INV_REGEN, QA_Q3_PW_REGEN},
    {QA_INV_FLIGHT, QA_Q3_PW_FLIGHT},
    {QA_INV_REDFLAG, QA_Q3_PW_REDFLAG},
    {QA_INV_BLUEFLAG, QA_Q3_PW_BLUEFLAG},
    {QA_INV_NEUTRALFLAG, QA_Q3_PW_NEUTRALFLAG},
};
static const int persistent_inventory[][2] = {
    {QA_INV_SCOUT, QA_MI_SCOUT},
    {QA_INV_GUARD, QA_MI_GUARD},
    {QA_INV_DOUBLER, QA_MI_DOUBLER},
    {QA_INV_AMMOREGEN, QA_MI_AMMOREGEN},
};
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static bool missionpack(const qa_bot_state_bots_a *state) { return state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK; }

double qa_bot_inventory_value_bots_a(qa_bot_state_bots_a *state, int64_t index) {
  if (index < 0 || index >= QA_Q3_BASE_MAX_ITEMS)
    qa_range_error("Bot inventory index %lld outside its allocation", (long long)index);
  return state->src->inventory[index];
}

static double characteristic(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t index) {
  return ai_character_bounded(ctx, state->src->character, index);
}

qa_q3_base_server_trace_result qa_bot_ai_trace_bots_a(qa_game_ai_context_bots_a *ctx, qa_vec3 start, qa_vec3 end,
                                                      int64_t pass_entity, int64_t mask, const qa_bounds *bounds) {
  qa_q3_base_server_trace_query query = {0};
  query.start = start;
  query.end = end;
  query.pass_entity_num = (double)pass_entity;
  query.mask = (double)mask;
  if (bounds == NULL) {
    query.shape.kind = QA_Q3_BASE_SHAPE_POINT;
  } else {
    query.shape.kind = QA_Q3_BASE_SHAPE_BOX;
    query.shape.mins = bounds->min;
    query.shape.maxs = bounds->max;
  }
  qa_q3_base_server_trace_result trace = ai_world_trace(ctx, &query);
  /* ai_main.c clears bsp_trace_t.contents after copying the engine trace. */
  trace.contents = 0;
  return trace;
}

qa_aas_entity_info_bots_a qa_bot_entity_info_bots_a(qa_game_ai_context_bots_a *ctx, int64_t entity) {
  return ai_observation(ctx, entity);
}

bool qa_entity_is_dead_bots_a(qa_game_ai_context_bots_a *ctx, const qa_aas_entity_info_bots_a *info) {
  if (info->number < 0 || info->number >= 64)
    return false;
  qa_bot_observed_entity_bots_a entity = ai_entity(ctx, info->number);
  return entity.has_player && entity.player_state->pm_type != QA_Q3_PM_NORMAL;
}

bool qa_entity_carries_flag_bots_a(qa_game_ai_context_bots_a *ctx, const qa_aas_entity_info_bots_a *info) {
  int32_t mask = (1 << QA_Q3_PW_REDFLAG) | (1 << QA_Q3_PW_BLUEFLAG) |
                 (ai_is_missionpack(ctx) ? 1 << QA_Q3_PW_NEUTRALFLAG : 0);
  return (ai_int32(info->powerups) & mask) != 0;
}

bool qa_entity_is_invisible_bots_a(qa_game_ai_context_bots_a *ctx, const qa_aas_entity_info_bots_a *info) {
  return !qa_entity_carries_flag_bots_a(ctx, info) && (ai_int32(info->powerups) & (1 << QA_Q3_PW_INVIS)) != 0;
}

bool qa_entity_is_shooting_bots_a(const qa_aas_entity_info_bots_a *info) {
  return (ai_int32(info->flags) & EF_FIRING) != 0;
}
bool qa_entity_is_chatting_bots_a(const qa_aas_entity_info_bots_a *info) {
  return (ai_int32(info->flags) & EF_TALK) != 0;
}
bool qa_entity_has_quad_bots_a(const qa_aas_entity_info_bots_a *info) {
  return (ai_int32(info->powerups) & (1 << QA_Q3_PW_QUAD)) != 0;
}
bool qa_entity_has_kamikaze_bots_a(const qa_aas_entity_info_bots_a *info) {
  return (ai_int32(info->flags) & EF_KAMIKAZE) != 0;
}
bool qa_entity_carries_cubes_bots_a(qa_game_ai_context_bots_a *ctx, const qa_aas_entity_info_bots_a *info) {
  return ai_deathmatch(ctx)->gametype == QA_Q3_BASE_GT_HARVESTER && ai_entity(ctx, info->number).state.generic1 > 0;
}

void qa_bot_choose_weapon_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  int32_t weapon_state = qa_bot_state_weapon_state_checked(state);
  if (weapon_state != QA_Q3_WEAPON_RAISING && weapon_state != QA_Q3_WEAPON_DROPPING) {
    int64_t weapon = ai_knowledge_choose_weapon(ctx, state);
    if (state->src->weapon_num != weapon)
      state->src->weapon_change_time = (float)ctx->time;
    state->src->weapon_num = (int32_t)weapon;
  }
  qa_bot_action_select_weapon(ai_actions(ctx), state->src->client, state->src->weapon_num);
}

static bool picked(qa_bot_state_bots_a *state, const int32_t *old_inventory, int index) {
  /* old_inventory is the full slice: no index here can be undefined. */
  return old_inventory[index] == 0 && qa_bot_inventory_value_bots_a(state, index) >= 1;
}

void qa_bot_check_item_pickup_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                     const int32_t *old_inventory) {
  qa_game_ai_deathmatch_state_bots_a *dm = ai_deathmatch(ctx);
  if (!missionpack(state) || dm->gametype <= QA_Q3_BASE_GT_TEAM)
    return;
  bool has_offense = false, offense = false;
  /* TS short-circuits the second picked(); neither read has a side effect. */
  if (picked(state, old_inventory, QA_INV_KAMIKAZE) || picked(state, old_inventory, QA_INV_INVULNERABILITY)) {
    has_offense = true;
    offense = true;
  }
  if (qa_bot_inventory_value_bots_a(state, QA_INV_KAMIKAZE) == 0 &&
      qa_bot_inventory_value_bots_a(state, QA_INV_INVULNERABILITY) == 0) {
    if (picked(state, old_inventory, QA_INV_SCOUT)) {
      has_offense = true;
      offense = true;
    }
    if (picked(state, old_inventory, QA_INV_GUARD)) {
      has_offense = true;
      offense = true;
    }
    if (picked(state, old_inventory, QA_INV_DOUBLER)) {
      has_offense = true;
      offense = false;
    }
    if (picked(state, old_inventory, QA_INV_AMMOREGEN)) {
      has_offense = true;
      offense = false;
    }
  }
  if (!has_offense)
    return;
  int64_t leader = qa_client_from_name_bots_a(ctx, qa_bot_state_text(state, state->src->team_leader, "teamLeader"));
  bool flags_at_base =
      (dm->gametype != QA_Q3_BASE_GT_CTF || (state->src->red_flag_status == 0 && state->src->blue_flag_status == 0)) &&
      (dm->gametype != QA_Q3_BASE_GT_1FCTF || state->src->neutral_flag_status == 0);
  if (offense) {
    if ((state->src->team_task_preference & QA_TASK_ATTACKER) == 0) {
      if (qa_bot_team_leader_bots_a(ctx, state)) {
        qa_bot_voice_chat_bots_a(ctx, state, leader, "wantonoffense");
      } else if (ai_cvar_integer(ctx, "g_spSkill") <= 3) {
        if (state->src->ltg_type != QA_LTG_GETFLAG && state->src->ltg_type != QA_LTG_ATTACKENEMYBASE &&
            state->src->ltg_type != QA_LTG_HARVEST && flags_at_base)
          qa_bot_voice_chat_bots_a(ctx, state, leader, "wantonoffense");
        state->src->team_task_preference |= QA_TASK_ATTACKER;
      }
    }
    state->src->team_task_preference &= ~QA_TASK_DEFENDER;
  } else {
    if ((state->src->team_task_preference & QA_TASK_DEFENDER) == 0) {
      if (qa_bot_team_leader_bots_a(ctx, state)) {
        qa_bot_voice_chat_bots_a(ctx, state, -1, "wantondefense");
      } else if (ai_cvar_integer(ctx, "g_spSkill") <= 3 && state->src->ltg_type != QA_LTG_DEFENDKEYAREA &&
                 flags_at_base) {
        qa_bot_voice_chat_bots_a(ctx, state, -1, "wantondefense");
      }
      state->src->team_task_preference |= QA_TASK_DEFENDER;
    }
    state->src->team_task_preference &= ~QA_TASK_ATTACKER;
  }
}

static bool stat_weapon_owned(void *ctx, int64_t weapon) {
  const qa_bot_state_bots_a *state = ctx;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(state->product);
  return (state->src->cur_ps.stats[(int)schema->weapons] & (1 << weapon)) != 0;
}

static double ps_ammo(void *ctx, int64_t weapon) {
  const qa_bot_state_bots_a *state = ctx;
  return state->src->cur_ps.ammo[weapon];
}

void qa_update_q3_bot_inventory_bots_a(qa_bot_state_bots_a *state) {
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(state->product);
  state->src->inventory[QA_INV_ARMOR] = state->src->cur_ps.stats[(int)schema->armor];
  /* The TS snapshots the weapons stat; the updater below never writes stats. */
  qa_update_q3_bot_weapon_inventory_bots_a(state, stat_weapon_owned, ps_ammo, state);
  qa_update_q3_bot_item_inventory_bots_a(state);
}

void qa_update_q3_bot_weapon_inventory_bots_a(qa_bot_state_bots_a *state, bool (*owned)(void *ctx, int64_t weapon),
                                              double (*ammo)(void *ctx, int64_t weapon), void *ctx) {
  bool baseq3 = state->product == QA_Q3_BASE_PRODUCT_BASEQ3;
  for (size_t i = 0; i < COUNT_OF(weapon_inventory); i++) {
    int index = weapon_inventory[i][0], weapon = weapon_inventory[i][1];
    if (baseq3 && weapon > QA_Q3_WP_GRAPPLING_HOOK)
      continue;
    state->src->inventory[index] = owned(ctx, weapon) ? 1 : 0;
  }
  for (size_t i = 0; i < COUNT_OF(ammo_inventory); i++) {
    int index = ammo_inventory[i][0], weapon = ammo_inventory[i][1];
    if (baseq3 && weapon > QA_Q3_WP_GRAPPLING_HOOK)
      continue;
    state->src->inventory[index] = qa_int32(ammo(ctx, weapon));
  }
}

void qa_update_q3_bot_item_inventory_bots_a(qa_bot_state_bots_a *state) {
  const qa_bot_player_state_source *ps = &state->src->cur_ps;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(state->product);
  bool baseq3 = state->product == QA_Q3_BASE_PRODUCT_BASEQ3;
  state->src->inventory[QA_INV_HEALTH] = ps->stats[(int)schema->health];
  for (size_t i = 0; i < COUNT_OF(holdable_inventory); i++) {
    int index = holdable_inventory[i][0], model = holdable_inventory[i][1];
    if (baseq3 && index > QA_INV_MEDKIT)
      continue;
    state->src->inventory[index] = ps->stats[(int)schema->holdable_item] == model;
  }
  for (size_t i = 0; i < COUNT_OF(powerup_inventory); i++) {
    int index = powerup_inventory[i][0], powerup = powerup_inventory[i][1];
    if (baseq3 && powerup == QA_Q3_PW_NEUTRALFLAG)
      continue;
    state->src->inventory[index] = ps->powerups[powerup] != 0;
  }
  if (schema->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    for (size_t i = 0; i < COUNT_OF(persistent_inventory); i++)
      state->src->inventory[persistent_inventory[i][0]] =
          ps->stats[(int)schema->persistent_powerup] == persistent_inventory[i][1];
    bool red = ps->persistant[3] == QA_Q3_BASE_TEAM_RED;
    state->src->inventory[QA_INV_REDCUBE] = red ? ps->generic1 : 0;
    state->src->inventory[QA_INV_BLUECUBE] = red ? 0 : ps->generic1;
  }
}

void qa_bot_update_inventory_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  int32_t old_inventory[QA_Q3_BASE_MAX_ITEMS];
  memcpy(old_inventory, state->src->inventory, sizeof old_inventory);
  ai_knowledge_update_inventory(ctx, state);
  qa_bot_check_item_pickup_bots_a(ctx, state, old_inventory);
}

void qa_bot_update_battle_inventory_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t enemy) {
  qa_vec3 direction = qa_sub3(qa_bot_entity_info_bots_a(ctx, enemy).origin, qa_bot_v3(state->src->origin));
  state->src->inventory[QA_INV_ENEMY_HEIGHT] = qa_qvm_float_to_int(direction.z);
  state->src->inventory[QA_INV_ENEMY_HORIZONTAL_DIST] =
      qa_qvm_float_to_int(qa_length3(qa_math_vec3(direction.x, direction.y, 0)));
}

static bool carrying_objective(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_ctf_carrying_flag_bots_a(ctx, state) != 0)
    return true;
  return missionpack(state) &&
         (qa_bot_1fctf_carrying_flag_bots_a(ctx, state) || qa_bot_harvester_carrying_cubes_bots_a(ctx, state));
}

static bool carrier_within(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t entity,
                           double distance) {
  if (entity < 0)
    return false;
  qa_vec3 direction = qa_sub3(qa_bot_entity_info_bots_a(ctx, entity).origin, qa_bot_v3(state->src->origin));
  return qa_dot3(direction, direction) < AI_F(distance * distance);
}

static bool goal_visible_within(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const qa_bot_goal *goal,
                                double distance) {
  qa_vec3 target = qa_add3(goal->origin, qa_math_vec3(0, 0, 1));
  qa_vec3 direction = qa_sub3(qa_bot_v3(state->src->origin), target);
  if (qa_dot3(direction, direction) >= AI_F(distance * distance))
    return false;
  qa_q3_base_server_trace_result trace =
      qa_bot_ai_trace_bots_a(ctx, qa_bot_v3(state->src->eye), target, state->src->client, SOLID, NULL);
  return trace.fraction >= 1 || trace.entity_num == goal->entity;
}

void qa_bot_use_kamikaze_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_inventory_value_bots_a(state, QA_INV_KAMIKAZE) <= 0 || state->src->kamikaze_time > ctx->time)
    return;
  state->src->kamikaze_time = (float)(ctx->time + AI_F(0.2));
  qa_game_ai_deathmatch_state_bots_a *dm = ai_deathmatch(ctx);
  int64_t mode = dm->gametype;
  if (mode == QA_Q3_BASE_GT_CTF || mode == QA_Q3_BASE_GT_1FCTF) {
    if (carrying_objective(ctx, state))
      return;
    if (carrier_within(ctx, state, qa_bot_team_flag_carrier_visible_bots_a(ctx, state), 1024))
      return;
    if (carrier_within(ctx, state, qa_bot_enemy_flag_carrier_visible_bots_a(ctx, state), 1024)) {
      qa_bot_action_use(ai_actions(ctx), state->src->client);
      return;
    }
  } else if (mode == QA_Q3_BASE_GT_OBELISK) {
    const qa_bot_goal *goal =
        qa_bot_team_bots_a(ctx, state) == QA_Q3_BASE_TEAM_RED ? &dm->blue_obelisk : &dm->red_obelisk;
    if (goal_visible_within(ctx, state, goal, AI_F(1024 * AI_F(0.9)))) {
      qa_bot_action_use(ai_actions(ctx), state->src->client);
      return;
    }
  } else if (mode == QA_Q3_BASE_GT_HARVESTER) {
    if (qa_bot_harvester_carrying_cubes_bots_a(ctx, state))
      return;
    if (carrier_within(ctx, state, qa_bot_team_cube_carrier_visible_bots_a(ctx, state), 1024))
      return;
    if (carrier_within(ctx, state, qa_bot_enemy_cube_carrier_visible_bots_a(ctx, state), 1024)) {
      qa_bot_action_use(ai_actions(ctx), state->src->client);
      return;
    }
  }
  int64_t teammates = 0, enemies = 0;
  qa_bot_visible_team_mates_and_enemies_bots_a(ctx, state, 1024, &teammates, &enemies);
  if (enemies > 2 && enemies > teammates + 1)
    qa_bot_action_use(ai_actions(ctx), state->src->client);
}

void qa_bot_use_invulnerability_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_inventory_value_bots_a(state, QA_INV_INVULNERABILITY) <= 0 || state->src->invulnerability_time > ctx->time)
    return;
  state->src->invulnerability_time = (float)(ctx->time + AI_F(0.2));
  qa_game_ai_deathmatch_state_bots_a *dm = ai_deathmatch(ctx);
  int64_t mode = dm->gametype;
  bool red = qa_bot_team_bots_a(ctx, state) == QA_Q3_BASE_TEAM_RED;
  if (mode == QA_Q3_BASE_GT_CTF || mode == QA_Q3_BASE_GT_1FCTF) {
    if (carrying_objective(ctx, state) || qa_bot_enemy_flag_carrier_visible_bots_a(ctx, state) >= 0)
      return;
    if (goal_visible_within(ctx, state, red ? &dm->ctf_blue_flag : &dm->ctf_red_flag, 200))
      qa_bot_action_use(ai_actions(ctx), state->src->client);
  } else if (mode == QA_Q3_BASE_GT_OBELISK) {
    if (goal_visible_within(ctx, state, red ? &dm->blue_obelisk : &dm->red_obelisk, 300))
      qa_bot_action_use(ai_actions(ctx), state->src->client);
  } else if (mode == QA_Q3_BASE_GT_HARVESTER) {
    if (qa_bot_harvester_carrying_cubes_bots_a(ctx, state) || qa_bot_enemy_cube_carrier_visible_bots_a(ctx, state) >= 0)
      return;
    if (goal_visible_within(ctx, state, red ? &dm->blue_obelisk : &dm->red_obelisk, 200))
      qa_bot_action_use(ai_actions(ctx), state->src->client);
  }
}

void qa_bot_battle_use_items_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH) < 40 &&
      qa_bot_inventory_value_bots_a(state, QA_INV_TELEPORTER) > 0 && !carrying_objective(ctx, state))
    qa_bot_action_use(ai_actions(ctx), state->src->client);
  if (qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH) < 60 &&
      qa_bot_inventory_value_bots_a(state, QA_INV_MEDKIT) > 0)
    qa_bot_action_use(ai_actions(ctx), state->src->client);
  if (missionpack(state)) {
    qa_bot_use_kamikaze_bots_a(ctx, state);
    qa_bot_use_invulnerability_bots_a(ctx, state);
  }
}

void qa_bot_set_teleport_time_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (((state->src->cur_ps.e_flags ^ state->src->last_eflags) & EF_TELEPORT_BIT) != 0)
    state->src->teleport_time = (float)ctx->time;
  state->src->last_eflags = state->src->cur_ps.e_flags;
}

bool qa_bot_is_dead_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  (void)ctx; /* TS _context */
  return qa_bot_state_pm_type_checked(state) == QA_Q3_PM_DEAD;
}

bool qa_bot_is_observer_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_state_pm_type_checked(state) == QA_Q3_PM_SPECTATOR)
    return true;
  char info[1024];
  ai_configstring(ctx, CS_PLAYERS + state->src->client, info, sizeof info);
  char *value = qa_info_value_for_key(info, "t");
  if (value == NULL)
    qa_range_error("Out of memory reading client team");
  int32_t team = qa_game_atoi(value);
  free(value);
  return team == QA_Q3_BASE_TEAM_SPECTATOR;
}

bool qa_bot_intermission_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (ai_clock_intermission_time(ctx) != 0)
    return true;
  int32_t pm_type = qa_bot_state_pm_type_checked(state);
  return pm_type == QA_Q3_PM_FREEZE || pm_type == QA_Q3_PM_INTERMISSION;
}

bool qa_bot_in_lava_or_slime_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  return (ai_point_contents(ctx, qa_add3(qa_bot_v3(state->src->origin), qa_math_vec3(0, 0, -23)), -1) &
          (LAVA | SLIME)) != 0;
}

double qa_bot_aggression_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  return ai_knowledge_aggression(ctx, state);
}

double qa_bot_feeling_bad_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (ai_knowledge_tactics(ctx, state->src->weapon_num).melee || qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH) < 40)
    return 100;
  double weakness = ai_knowledge_tactics(ctx, state->src->weapon_num).weakness;
  if (weakness > 0)
    return weakness;
  if (qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH) < 60)
    return 80;
  return 0;
}

bool qa_bot_wants_to_retreat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (carrying_objective(ctx, state))
    return true;
  qa_game_ai_deathmatch_state_bots_a *dm = ai_deathmatch(ctx);
  if (missionpack(state) && dm->gametype == QA_Q3_BASE_GT_OBELISK) {
    if (state->src->ltg_type == QA_LTG_ATTACKENEMYBASE &&
        (state->src->enemy != dm->red_obelisk.entity || state->src->enemy != dm->blue_obelisk.entity))
      return true;
    return qa_bot_feeling_bad_bots_a(ctx, state) > 50;
  }
  if (state->src->enemy >= 0) {
    qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, state->src->enemy);
    if (qa_entity_carries_flag_bots_a(ctx, &info))
      return false;
  }
  return state->src->ltg_type == QA_LTG_GETFLAG || qa_bot_aggression_bots_a(ctx, state) < 50;
}

bool qa_bot_wants_to_chase_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (carrying_objective(ctx, state))
    return false;
  qa_game_ai_deathmatch_state_bots_a *dm = ai_deathmatch(ctx);
  int64_t mode = dm->gametype;
  if (mode == QA_Q3_BASE_GT_CTF || (missionpack(state) && mode == QA_Q3_BASE_GT_1FCTF)) {
    qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, state->src->enemy);
    if (qa_entity_carries_flag_bots_a(ctx, &info))
      return true;
  }
  if (missionpack(state) && mode == QA_Q3_BASE_GT_OBELISK && state->src->ltg_type == QA_LTG_ATTACKENEMYBASE &&
      (state->src->enemy != dm->red_obelisk.entity || state->src->enemy != dm->blue_obelisk.entity))
    return false;
  return state->src->ltg_type != QA_LTG_GETFLAG && qa_bot_aggression_bots_a(ctx, state) > 50;
}

bool qa_bot_wants_to_help_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  (void)ctx; /* TS _context */
  (void)state; /* TS _state */
  return true;
}

bool qa_bot_can_and_wants_to_rocket_jump_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (ai_cvar_integer(ctx, "bot_rocketjump") == 0)
    return false;
  if (qa_bot_inventory_value_bots_a(state, QA_INV_ROCKETLAUNCHER) <= 0 ||
      qa_bot_inventory_value_bots_a(state, QA_INV_ROCKETS) < 3)
    return false;
  if (qa_bot_inventory_value_bots_a(state, QA_INV_QUAD) != 0 || qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH) < 60)
    return false;
  if (qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH) < 90 && qa_bot_inventory_value_bots_a(state, QA_INV_ARMOR) < 40)
    return false;
  return characteristic(ctx, state, QA_BCHAR_WEAPONJUMPING) >= 0.5;
}

bool qa_bot_has_persistant_powerup_and_weapon_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  (void)ctx; /* TS _context */
  if (missionpack(state)) {
    bool none = true;
    for (size_t i = 0; i < COUNT_OF(persistent_inventory); i++)
      if (qa_bot_inventory_value_bots_a(state, persistent_inventory[i][0]) != 0) {
        none = false;
        break;
      }
    if (none)
      return false;
  }
  if (qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH) < 60)
    return false;
  if (qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH) < 80 && qa_bot_inventory_value_bots_a(state, QA_INV_ARMOR) < 40)
    return false;
  static const int table[][3] = {
      {QA_INV_BFG10K, QA_INV_BFGAMMO, 7},          {QA_INV_RAILGUN, QA_INV_SLUGS, 5},
      {QA_INV_LIGHTNING, QA_INV_LIGHTNINGAMMO, 50}, {QA_INV_ROCKETLAUNCHER, QA_INV_ROCKETS, 5},
      {QA_INV_NAILGUN, QA_INV_NAILS, 5},           {QA_INV_PROXLAUNCHER, QA_INV_MINES, 5},
      {QA_INV_CHAINGUN, QA_INV_BELT, 40},          {QA_INV_PLASMAGUN, QA_INV_CELLS, 20},
  };
  for (size_t i = 0; i < COUNT_OF(table); i++)
    if (qa_bot_inventory_value_bots_a(state, table[i][0]) > 0 && qa_bot_inventory_value_bots_a(state, table[i][1]) > table[i][2])
      return true;
  return false;
}

void qa_bot_go_camp_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const qa_bot_goal *goal) {
  state->src->decisionmaker = state->src->client;
  state->src->team_message_time = 0;
  state->src->ltg_type = QA_LTG_CAMP;
  qa_bot_goal_state_copy_from(&state->src->team_goal, *goal);
  double camper = characteristic(ctx, state, QA_BCHAR_CAMPER);
  state->src->team_goal_time =
      camper > AI_F(0.99) ? (float)(ctx->time + 99999)
                          : (float)(AI_F(AI_F(ctx->time + 120) + AI_F(180 * camper)) + AI_F(ai_random(ctx) * 15));
  state->src->camp_time = (float)ctx->time;
  state->src->teammate = 0;
  state->src->arrive_time = 1;
}

bool qa_bot_wants_to_camp_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  double camper = characteristic(ctx, state, QA_BCHAR_CAMPER);
  if (camper < AI_F(0.1))
    return false;
  switch (state->src->ltg_type) {
  case QA_LTG_TEAMHELP:
  case QA_LTG_TEAMACCOMPANY:
  case QA_LTG_DEFENDKEYAREA:
  case QA_LTG_GETFLAG:
  case QA_LTG_RUSHBASE:
  case QA_LTG_CAMP:
  case QA_LTG_CAMPORDER:
  case QA_LTG_PATROL:
    return false;
  default:
    break;
  }
  if (state->src->camp_time > AI_F(AI_F(ctx->time - 60) + AI_F(300 * AI_F(1 - camper))))
    return false;
  if (ai_random(ctx) > camper) {
    state->src->camp_time = (float)ctx->time;
    return false;
  }
  if (qa_bot_aggression_bots_a(ctx, state) < 50)
    return false;
  /* The original indexes inventory[INVENTORY_ROCKETS < 10], which selects slot zero. */
  if ((qa_bot_inventory_value_bots_a(state, QA_INV_ROCKETLAUNCHER) <= 0 || qa_bot_inventory_value_bots_a(state, 0) != 0) &&
      (qa_bot_inventory_value_bots_a(state, QA_INV_RAILGUN) <= 0 || qa_bot_inventory_value_bots_a(state, QA_INV_SLUGS) < 10) &&
      (qa_bot_inventory_value_bots_a(state, QA_INV_BFG10K) <= 0 || qa_bot_inventory_value_bots_a(state, QA_INV_BFGAMMO) < 10))
    return false;
  if (!ai_nav_ready(ctx))
    return false;
  double best_time = 99999;
  qa_bot_goal best_goal = {0};
  qa_bot_goal spot;
  int64_t next = 0;
  for (bool found = qa_bot_goal_get_next_camp_spot_goal(ai_goals(ctx), 0, NULL, &next, &spot); found;
       found = qa_bot_goal_get_next_camp_spot_goal(ai_goals(ctx), next, NULL, &next, &spot)) {
    double time = ai_nav_travel_time(ctx, state->src->area_num, qa_bot_v3(state->src->origin), spot.area, QA_TFL_DEFAULT);
    if (time != 0 && time < best_time) {
      best_time = time;
      ai_goal_copy(&best_goal, &spot);
    }
  }
  if (best_time > 150)
    return false;
  qa_bot_go_camp_bots_a(ctx, state, &best_goal);
  state->src->ordered = 0;
  return true;
}

void qa_bot_dont_avoid_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *item_name) {
  qa_bot_goal goal;
  for (bool found = qa_bot_goal_get_level_item_goal(ai_goals(ctx), -1, qa_goal_name_text(item_name), NULL, &goal); found;
       found = qa_bot_goal_get_level_item_goal(ai_goals(ctx), goal.number, qa_goal_name_text(item_name), NULL, &goal))
    qa_bot_goal_remove_from_avoid_goals(ai_goals(ctx), state->src->gs, goal.number);
}

void qa_bot_go_for_powerups_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  static const char *const names[] = {"Quad Damage", "Regeneration", "Battle Suit", "Speed", "Invisibility"};
  for (size_t i = 0; i < COUNT_OF(names); i++)
    qa_bot_dont_avoid_bots_a(ctx, state, names[i]);
  state->src->ltg_time = 0;
}

static bool fov_axis(double view, double fov, double *angle_cell) {
  double angle = qa_qvm_angle_mod(view), target = qa_qvm_angle_mod(*angle_cell);
  *angle_cell = target;
  double difference = AI_F(target - angle);
  if (target > angle) {
    if (difference > 180)
      difference = AI_F(difference - 360);
  } else if (difference < -180) {
    difference = AI_F(difference + 360);
  }
  return !(difference > AI_F(fov * 0.5) || difference < AI_F(-fov * 0.5));
}

bool qa_in_field_of_vision_bots_a(qa_vec3 viewangles, double fov, qa_vec3 *angles) {
  return fov_axis(viewangles.x, fov, &angles->x) && fov_axis(viewangles.y, fov, &angles->y);
}

double qa_bot_entity_visible_bots_a(qa_game_ai_context_bots_a *ctx, int64_t viewer, qa_vec3 eye, qa_vec3 viewangles,
                                    double fov, int64_t entity) {
  qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, entity);
  qa_vec3 middle = qa_add3(info.origin, qa_scale3(qa_add3(info.mins, info.maxs), 0.5));
  qa_vec3 to_middle = qa_vector_to_angles(qa_sub3(middle, eye));
  if (!qa_in_field_of_vision_bots_a(viewangles, fov, &to_middle))
    return 0;
  int64_t contents = ai_point_contents(ctx, eye, -1);
  bool in_fog = (contents & FOG) != 0, in_water = (contents & LIQUID) != 0;
  double best_visibility = 0;
  for (int i = 0; i < 3; i++) {
    int64_t mask = SOLID | PLAYERCLIP, pass = viewer, hit = entity;
    qa_vec3 start = eye, end = middle;
    if ((ai_point_contents(ctx, middle, -1) & LIQUID) != 0)
      mask |= LIQUID;
    if (in_water) {
      if ((mask & LIQUID) == 0) {
        pass = entity;
        hit = viewer;
        start = middle;
        end = eye;
      }
      mask ^= LIQUID;
    }
    qa_q3_base_server_trace_result trace = qa_bot_ai_trace_bots_a(ctx, start, end, pass, mask, NULL);
    double water_factor = 1;
    if (((int64_t)trace.contents & LIQUID) != 0) {
      mask &= ~(int64_t)LIQUID;
      trace = qa_bot_ai_trace_bots_a(ctx, trace.end, end, pass, mask, NULL);
      water_factor = 0.5;
    }
    if (trace.fraction >= 1 || trace.entity_num == hit) {
      bool other_in_fog = (ai_point_contents(ctx, middle, -1) & FOG) != 0;
      double fog_distance_squared = 0;
      if (in_fog && other_in_fog) {
        qa_vec3 direction = qa_sub3(trace.end, eye);
        fog_distance_squared = qa_dot3(direction, direction);
      } else if (in_fog) {
        trace = qa_bot_ai_trace_bots_a(ctx, trace.end, eye, viewer, FOG, NULL);
        qa_vec3 direction = qa_sub3(eye, trace.end);
        fog_distance_squared = qa_dot3(direction, direction);
      } else if (other_in_fog) {
        end = trace.end;
        trace = qa_bot_ai_trace_bots_a(ctx, eye, end, viewer, FOG, NULL);
        qa_vec3 direction = qa_sub3(end, trace.end);
        fog_distance_squared = qa_dot3(direction, direction);
      }
      double scaled = AI_F(fog_distance_squared * AI_F(0.001));
      double visibility = AI_F(AI_F(1.0 / qa_js_max(1, scaled)) * water_factor);
      if (visibility > best_visibility)
        best_visibility = visibility;
      if (best_visibility >= AI_F(0.95))
        return best_visibility;
    }
    if (i == 0)
      middle = qa_add3(middle, qa_math_vec3(0, 0, info.mins.z));
    else if (i == 1)
      middle = qa_add3(middle, qa_math_vec3(0, 0, AI_F(info.maxs.z - info.mins.z)));
  }
  return best_visibility;
}

bool qa_bot_find_enemy_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t current_enemy) {
  double alertness = characteristic(ctx, state, QA_BCHAR_ALERTNESS);
  double easy_fragger = characteristic(ctx, state, QA_BCHAR_EASY_FRAGGER);
  bool health_decreased = state->src->last_health > qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH);
  state->src->last_health = (int32_t)qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH);
  double current_distance = 0;
  qa_game_ai_deathmatch_state_bots_a *dm = ai_deathmatch(ctx);
  qa_vec3 self_origin = qa_bot_v3(state->src->origin);
  if (current_enemy >= 0) {
    qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, current_enemy);
    if (qa_entity_carries_flag_bots_a(ctx, &info))
      return false;
    qa_vec3 direction = qa_sub3(info.origin, self_origin);
    current_distance = qa_dot3(direction, direction);
  }
  if (missionpack(state) && dm->gametype == QA_Q3_BASE_GT_OBELISK) {
    const qa_bot_goal *goal =
        qa_bot_team_bots_a(ctx, state) == QA_Q3_BASE_TEAM_RED ? &dm->blue_obelisk : &dm->red_obelisk;
    qa_q3_base_server_trace_result trace =
        qa_bot_ai_trace_bots_a(ctx, qa_bot_v3(state->src->eye), qa_add3(goal->origin, qa_math_vec3(0, 0, 1)),
                               state->src->client, SOLID, NULL);
    if (trace.fraction >= 1 || trace.entity_num == goal->entity) {
      if (goal->entity == state->src->enemy)
        return false;
      state->src->enemy = (int32_t)goal->entity;
      state->src->enemy_sight_time = (float)ctx->time;
      state->src->enemy_suicide = 0;
      state->src->enemy_death_time = 0;
      state->src->enemy_visible_time = (float)ctx->time;
      return true;
    }
  }
  for (int64_t client = 0; client < dm->maxclients && client < 64; client++) {
    if (client == state->src->client || client == current_enemy)
      continue;
    qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, client);
    if (!info.valid || qa_entity_is_dead_bots_a(ctx, &info) || info.number == state->src->entity_num)
      continue;
    if (qa_entity_is_invisible_bots_a(ctx, &info) && !qa_entity_is_shooting_bots_a(&info))
      continue;
    if (easy_fragger < 0.5 && qa_entity_is_chatting_bots_a(&info))
      continue;
    if (dm->last_teleport_time > AI_F(ctx->time - 3)) {
      qa_vec3 direction = qa_sub3(info.origin, dm->last_teleport_origin);
      if (qa_dot3(direction, direction) < 70 * 70)
        continue;
    }
    qa_vec3 direction = qa_sub3(info.origin, self_origin);
    double distance = qa_dot3(direction, direction);
    if (!qa_entity_carries_flag_bots_a(ctx, &info) && current_enemy >= 0 && distance > current_distance)
      continue;
    double alert_range = AI_F(900 + AI_F(alertness * 4000));
    if (distance > AI_F(alert_range * alert_range) || qa_bot_same_team_bots_a(ctx, state, client))
      continue;
    double fov;
    if (current_enemy < 0 && (health_decreased || qa_entity_is_shooting_bots_a(&info)))
      fov = 360;
    else
      fov = AI_F(180 - AI_F(90 - AI_F(qa_js_min(810.0 * 810.0, distance) / (810.0 * 9.0))));
    if (qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, qa_bot_v3(state->src->eye),
                                     qa_bot_v3(state->src->viewangles), fov, client) <= 0)
      continue;
    if (current_enemy < 0 && distance > 100 * 100 && !health_decreased && !qa_entity_is_shooting_bots_a(&info)) {
      qa_vec3 toward_self = qa_vector_to_angles(qa_sub3(self_origin, info.origin));
      if (!qa_in_field_of_vision_bots_a(info.angles, 90, &toward_self)) {
        qa_bot_update_battle_inventory_bots_a(ctx, state, client);
        if (qa_bot_wants_to_retreat_bots_a(ctx, state))
          continue;
      }
    }
    state->src->enemy = (int32_t)info.number;
    state->src->enemy_sight_time = current_enemy >= 0 ? (float)(ctx->time - 2) : (float)ctx->time;
    state->src->enemy_suicide = 0;
    state->src->enemy_death_time = 0;
    state->src->enemy_visible_time = (float)ctx->time;
    return true;
  }
  return false;
}

static void weapon_info(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_weapon_info_bots_a *out) {
  if (!ai_knowledge_weapon_info(ctx, state->src->ws, state->src->weapon_num, out))
    qa_range_error("Bot weapon %d has no loaded weapon information", (int)state->src->weapon_num);
}

void qa_bot_aim_at_enemy_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (state->src->enemy < 0)
    return;
  qa_game_ai_deathmatch_state_bots_a *dm = ai_deathmatch(ctx);
  qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, state->src->enemy);
  qa_vec3 eye = qa_bot_v3(state->src->eye), self_origin = qa_bot_v3(state->src->origin);
  if (state->src->enemy >= 64) {
    qa_vec3 target = missionpack(state) && (state->src->enemy == dm->red_obelisk.entity ||
                                            state->src->enemy == dm->blue_obelisk.entity)
                         ? qa_add3(info.origin, qa_math_vec3(0, 0, 32))
                         : info.origin;
    state->src->ideal_viewangles = qa_bot_fv3(qa_vector_to_angles(qa_sub3(target, eye)));
    state->src->aim_target = qa_bot_fv3(target);
    return;
  }
  double aim_skill = characteristic(ctx, state, QA_BCHAR_AIM_SKILL);
  double aim_accuracy = characteristic(ctx, state, QA_BCHAR_AIM_ACCURACY);
  if (aim_skill > AI_F(0.95)) {
    double reaction_time = AI_F(0.5 * characteristic(ctx, state, QA_BCHAR_REACTIONTIME));
    if (state->src->enemy_sight_time > AI_F(ctx->time - reaction_time) ||
        state->src->teleport_time > AI_F(ctx->time - reaction_time))
      return;
  }
  qa_weapon_info_bots_a weapon;
  weapon_info(ctx, state, &weapon);
  double weapon_speed = weapon.speed;
  qa_bot_weapon_tactics_bots_a tactics = ai_knowledge_tactics(ctx, state->src->weapon_num);
  if (tactics.has_aim_accuracy)
    aim_accuracy = characteristic(ctx, state, (int64_t)tactics.aim_accuracy);
  if (tactics.has_aim_skill)
    aim_skill = characteristic(ctx, state, (int64_t)tactics.aim_skill);
  if (aim_accuracy <= 0)
    aim_accuracy = AI_F(0.0001);
  info = qa_bot_entity_info_bots_a(ctx, state->src->enemy);
  if (qa_entity_is_invisible_bots_a(ctx, &info) && ai_random(ctx) > AI_F(0.1))
    aim_accuracy = AI_F(aim_accuracy * AI_F(0.4));
  qa_vec3 velocity = qa_scale3(qa_sub3(info.origin, info.last_visible_origin), AI_F(1.0 / (double)info.update_interval));
  if (state->src->enemy_position_time < ctx->time) {
    state->src->enemy_position_time = (float)(ctx->time + 0.5);
    state->src->enemy_velocity = qa_bot_fv3(velocity);
    state->src->enemy_origin = qa_bot_fv3(info.origin);
  }
  qa_vec3 movement = qa_sub3(info.origin, qa_bot_v3(state->src->enemy_origin));
  if (aim_skill < AI_F(0.9) && qa_dot3(movement, movement) > 48 * 48 &&
      qa_dot3(qa_bot_v3(state->src->enemy_velocity), velocity) < 0)
    aim_accuracy = AI_F(aim_accuracy * AI_F(0.7));
  /* The source stores the float visibility result in an int before testing it. */
  bool visible = qa_qvm_float_to_int(qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, eye,
                                                                  qa_bot_v3(state->src->viewangles), 360,
                                                                  state->src->enemy)) != 0;
  qa_vec3 best_origin;
  if (visible) {
    best_origin = qa_add3(info.origin, qa_math_vec3(0, 0, 8));
    qa_vec3 start = qa_add3(qa_add3(self_origin, qa_math_vec3(0, 0, state->src->cur_ps.viewheight)),
                            qa_math_vec3(0, 0, weapon.offset.z));
    qa_bounds box = {qa_math_vec3(-4, -4, -4), qa_math_vec3(4, 4, 4)};
    qa_q3_base_server_trace_result trace =
        qa_bot_ai_trace_bots_a(ctx, start, best_origin, state->src->entity_num, MASK_SHOT, &box);
    if (trace.fraction <= 1 && trace.entity_num != info.number)
      best_origin = qa_add3(best_origin, qa_math_vec3(0, 0, 16));
    if (weapon_speed != 0) {
      double distance = qa_length3(qa_sub3(best_origin, self_origin));
      if (!(distance > 100 && qa_dot3(movement, movement) < 32 * 32)) {
        if (aim_skill > AI_F(0.8) && qa_bot_state_weapon_state_checked(state) == QA_Q3_WEAPON_READY) {
          qa_bot_movement_prediction query = {0};
          query.entity_num = state->src->enemy;
          query.origin = qa_add3(info.origin, qa_math_vec3(0, 0, 1));
          query.presence = 4;
          query.on_ground = false;
          query.velocity = velocity;
          query.command_move = qa_math_vec3(0, 0, 0);
          query.command_frames = 0;
          query.max_frames = qa_qvm_float_to_int(AI_F(AI_F(qa_length3(qa_sub3(info.origin, self_origin)) * 10) / weapon_speed));
          query.frame_time = AI_F(0.1);
          query.stop_events = 0;
          query.stop_area = 0;
          query.visualize = false;
          best_origin = ai_nav_predict_client_movement(ctx, &query);
        } else if (aim_skill > AI_F(0.4)) {
          qa_vec3 displacement = qa_sub3(info.origin, info.last_visible_origin);
          qa_vec3 horizontal = qa_math_vec3(displacement.x, displacement.y, 0);
          double speed = AI_F(qa_length3(horizontal) / (double)info.update_interval);
          best_origin = qa_add3(
              info.origin,
              qa_scale3(qa_normalize3(horizontal),
                        AI_F(AI_F(qa_length3(qa_sub3(info.origin, self_origin)) / weapon_speed) * speed)));
        }
      }
    }
    if (aim_skill > AI_F(0.6) && (weapon.projectile_info.damage_type & QA_DAMAGE_TYPE_RADIAL) != 0 &&
        info.origin.z < AI_F(self_origin.z + 16)) {
      trace = qa_bot_ai_trace_bots_a(ctx, info.origin, qa_add3(info.origin, qa_math_vec3(0, 0, -64)), info.number,
                                     MASK_SHOT, NULL);
      qa_vec3 ground_target =
          qa_math_vec3(best_origin.x, best_origin.y,
                       trace.solidity != QA_Q3_BASE_SOLID_CLEAR ? info.origin.z - 16 : trace.end.z - 8);
      trace = qa_bot_ai_trace_bots_a(ctx, start, ground_target, state->src->entity_num, MASK_SHOT, NULL);
      qa_vec3 ground_delta = qa_sub3(trace.end, ground_target), self_delta = qa_sub3(trace.end, start);
      if (fabs(AI_F(trace.end.z - ground_target.z)) < 50 && qa_dot3(ground_delta, ground_delta) < 60 * 60 &&
          qa_dot3(self_delta, self_delta) > 100 * 100) {
        trace = qa_bot_ai_trace_bots_a(ctx, qa_add3(trace.end, qa_math_vec3(0, 0, 1)), info.origin, info.number,
                                       MASK_SHOT, NULL);
        if (trace.fraction >= 1)
          best_origin = ground_target;
      }
    }
    /* Argument order is the TS left-to-right random order. */
    double error_x = AI_F(AI_F(20 * ai_crandom(ctx)) * AI_F(1 - aim_accuracy));
    double error_y = AI_F(AI_F(20 * ai_crandom(ctx)) * AI_F(1 - aim_accuracy));
    double error_z = AI_F(AI_F(10 * ai_crandom(ctx)) * AI_F(1 - aim_accuracy));
    best_origin = qa_math_vec3(best_origin.x + error_x, best_origin.y + error_y, best_origin.z + error_z);
  } else {
    best_origin = qa_add3(qa_bot_v3(state->src->last_enemy_origin), qa_math_vec3(0, 0, 8));
    if (aim_skill > 0.5 && ai_knowledge_tactics(ctx, state->src->weapon_num).predict_occluded_splash) {
      qa_bot_goal goal = {0};
      goal.entity = state->src->client;
      goal.area = state->src->area_num;
      goal.origin = eye;
      goal.mins = qa_math_vec3(-8, -8, -8);
      goal.maxs = qa_math_vec3(8, 8, 8);
      qa_vec3 target = qa_math_vec3(0, 0, 0);
      if (ai_nav_predict_visible_position(ctx, qa_bot_v3(state->src->last_enemy_origin), state->src->last_enemy_area_num,
                                          &goal, QA_TFL_DEFAULT, &target)) {
        qa_vec3 direction = qa_sub3(target, eye);
        if (qa_dot3(direction, direction) > 80 * 80)
          best_origin = qa_add3(target, qa_math_vec3(0, 0, -20));
      }
      aim_accuracy = 1;
    }
  }
  state->src->aim_target = qa_bot_fv3(
      visible ? qa_bot_ai_trace_bots_a(ctx, eye, best_origin, state->src->entity_num, MASK_SHOT, NULL).end : best_origin);
  qa_vec3 direction = qa_sub3(best_origin, eye);
  if (ai_knowledge_tactics(ctx, state->src->weapon_num).close_range_aim_penalty)
    aim_accuracy = AI_F(aim_accuracy * AI_F(AI_F(0.6) + AI_F(AI_F(qa_js_min(qa_length3(direction), 150) / 150) * AI_F(0.4))));
  if (aim_accuracy < AI_F(0.8)) {
    direction = qa_normalize3(direction);
    double error_x = AI_F(AI_F(AI_F(0.3) * ai_crandom(ctx)) * AI_F(1 - aim_accuracy));
    double error_y = AI_F(AI_F(AI_F(0.3) * ai_crandom(ctx)) * AI_F(1 - aim_accuracy));
    double error_z = AI_F(AI_F(AI_F(0.3) * ai_crandom(ctx)) * AI_F(1 - aim_accuracy));
    direction = qa_math_vec3(direction.x + error_x, direction.y + error_y, direction.z + error_z);
  }
  qa_vec3 angles = qa_vector_to_angles(direction);
  double pitch = qa_qvm_angle_mod(
      AI_F(angles.x + AI_F(AI_F(AI_F(6 * (double)weapon.vertical_spread) * ai_crandom(ctx)) * AI_F(1 - aim_accuracy))));
  double yaw = qa_qvm_angle_mod(
      AI_F(angles.y + AI_F(AI_F(AI_F(6 * (double)weapon.horizontal_spread) * ai_crandom(ctx)) * AI_F(1 - aim_accuracy))));
  state->src->ideal_viewangles = qa_bot_fv3(qa_math_vec3(pitch, yaw, angles.z));
  if (ai_cvar_integer(ctx, "bot_challenge") != 0 && aim_accuracy > AI_F(0.9) &&
      state->src->enemy_sight_time < AI_F(ctx->time - 1)) {
    if (state->src->ideal_viewangles.x > 180)
      state->src->ideal_viewangles.x = (float)((double)state->src->ideal_viewangles.x - 360);
    state->src->viewangles = state->src->ideal_viewangles;
    qa_bot_action_view(ai_actions(ctx), state->src->client, qa_bot_v3(state->src->viewangles));
  }
}

void qa_bot_check_attack_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  int64_t attack_entity = state->src->enemy;
  qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, attack_entity);
  qa_game_ai_deathmatch_state_bots_a *dm = ai_deathmatch(ctx);
  if (attack_entity >= 64 && missionpack(state) &&
      (info.number == dm->red_obelisk.entity || info.number == dm->blue_obelisk.entity)) {
    qa_bot_observed_entity_bots_a entity = ai_entity(ctx, info.number);
    if (entity.has_activator_frame && entity.activator_frame == 2)
      return;
  }
  double reaction_time = characteristic(ctx, state, QA_BCHAR_REACTIONTIME);
  if (state->src->enemy_sight_time > AI_F(ctx->time - reaction_time) ||
      state->src->teleport_time > AI_F(ctx->time - reaction_time))
    return;
  if (state->src->weapon_change_time > AI_F(ctx->time - AI_F(0.1)) || state->src->fire_throttle_wait_time > ctx->time)
    return;
  double throttle = characteristic(ctx, state, QA_BCHAR_FIRETHROTTLE);
  if (state->src->fire_throttle_shoot_time < ctx->time) {
    if (ai_random(ctx) > throttle) {
      state->src->fire_throttle_wait_time = (float)(ctx->time + throttle);
      state->src->fire_throttle_shoot_time = 0;
    } else {
      state->src->fire_throttle_shoot_time = (float)(AI_F(ctx->time + 1) - throttle);
      state->src->fire_throttle_wait_time = 0;
    }
  }
  qa_vec3 eye = qa_bot_v3(state->src->eye), aim_target = qa_bot_v3(state->src->aim_target);
  qa_vec3 direction = qa_sub3(aim_target, eye);
  double distance = qa_dot3(direction, direction);
  qa_bot_weapon_tactics_bots_a tactics = ai_knowledge_tactics(ctx, state->src->weapon_num);
  if (tactics.has_max_range && distance > tactics.max_range * tactics.max_range)
    return;
  qa_vec3 aim_angles = qa_vector_to_angles(direction);
  if (!qa_in_field_of_vision_bots_a(qa_bot_v3(state->src->viewangles), distance < 100 * 100 ? 120 : 50, &aim_angles))
    return;
  qa_q3_base_server_trace_result sight =
      qa_bot_ai_trace_bots_a(ctx, eye, aim_target, state->src->client, SOLID | PLAYERCLIP, NULL);
  if (sight.fraction < 1 && sight.entity_num != attack_entity)
    return;
  qa_weapon_info_bots_a weapon;
  weapon_info(ctx, state, &weapon);
  qa_angle_vectors axes = qa_qvm_angle_vectors(qa_bot_v3(state->src->viewangles));
  qa_vec3 start = qa_add3(qa_bot_v3(state->src->origin), qa_math_vec3(0, 0, state->src->cur_ps.viewheight));
  start = qa_math_vec3(start.x + AI_F(AI_F(axes.forward.x * weapon.offset.x) + AI_F(axes.right.x * weapon.offset.y)),
                       start.y + AI_F(AI_F(axes.forward.y * weapon.offset.x) + AI_F(axes.right.y * weapon.offset.y)),
                       start.z + AI_F(AI_F(AI_F(axes.forward.z * weapon.offset.x) + AI_F(axes.right.z * weapon.offset.y)) +
                                      weapon.offset.z));
  qa_vec3 end = qa_add3(start, qa_scale3(axes.forward, 1000));
  start = qa_add3(start, qa_scale3(axes.forward, -12));
  qa_bounds box = {qa_math_vec3(-8, -8, -8), qa_math_vec3(8, 8, 8)};
  qa_q3_base_server_trace_result trace =
      qa_bot_ai_trace_bots_a(ctx, start, end, state->src->entity_num, MASK_SHOT, &box);
  if (trace.entity_num > 0 && trace.entity_num <= 64 && trace.entity_num != attack_entity &&
      qa_bot_same_team_bots_a(ctx, state, (int64_t)trace.entity_num))
    return;
  if ((trace.entity_num != attack_entity || attack_entity >= 64) &&
      (weapon.projectile_info.damage_type & QA_DAMAGE_TYPE_RADIAL) != 0) {
    if (AI_F(trace.fraction * 1000) < weapon.projectile_info.radius &&
        AI_F(AI_F(weapon.projectile_info.damage - AI_F(AI_F(0.5 * trace.fraction) * 1000)) * 0.5) > 0)
      return;
  }
  if ((weapon.flags & QA_WEAPON_FIRE_RELEASED_BOTS_A) == 0 || (state->src->flags & QA_BOT_FLAG_ATTACKED) != 0)
    qa_bot_action_attack(ai_actions(ctx), state->src->client);
  state->src->flags ^= QA_BOT_FLAG_ATTACKED;
}

/* TS String.slice index for a possibly negative signed-char capture offset. */
static size_t js_slice_index(int64_t offset, size_t length) {
  if (offset < 0)
    return offset + (int64_t)length < 0 ? 0 : (size_t)(offset + (int64_t)length);
  return (size_t)offset > length ? length : (size_t)offset;
}

void qa_bot_check_console_messages_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  qa_bot_chat_library *library = ai_chat(ctx);
  int32_t cs = state->src->cs;
  char bot_name[CHAT_NAME_SIZE];
  qa_client_name_bots_a(ctx, state->src->client, 36, bot_name);
  qa_console_chat_message message;
  while (qa_bot_chat_library_next_console_message(library, cs, &message)) {
    if (qa_bot_chat_library_num_console_messages(library, cs) < 10 && message.type == 1 &&
        message.time > AI_F(ctx->time - AI_F(1 + ai_random(ctx))))
      break;
    int64_t offset = 0;
    qa_chat_match match;
    if (message.type == 1) {
      if (qa_bot_chat_library_find_match(library, message.message, 128, &match) && match.variables[2].present)
        offset = match.variables[2].offset;
    }
    int64_t synonym_context = qa_bot_synonym_context_bots_a(ctx, state);
    size_t message_length = strlen(message.message);
    size_t split = js_slice_index(offset, message_length);
    char *unified = qa_unify_white_spaces_bots_a(message.message + split);
    char *replaced = qa_bot_chat_library_replace_synonyms(library, unified, synonym_context);
    size_t replaced_length = strlen(replaced);
    char *text = malloc(split + replaced_length + 1);
    if (text == NULL)
      qa_range_error("Out of memory building console message");
    memcpy(text, message.message, split);
    memcpy(text + split, replaced, replaced_length + 1);
    free(unified);
    free(replaced);
    if (!qa_bot_match_message_bots_a(ctx, state, text) && message.type == 1 &&
        ai_cvar_integer(ctx, "bot_nochat") == 0) {
      if (!qa_bot_chat_library_find_match(library, text, 128, &match) || (match.subtype & 32768) != 0) {
        qa_bot_chat_library_remove_console_message(library, cs, message.handle);
        free(text);
        continue;
      }
      char *net_name = qa_bot_chat_library_match_variable(library, &match, 0, 36);
      char *raw_body = qa_bot_chat_library_match_variable(library, &match, 2, QA_CHAT_MESSAGE_SIZE);
      char *body = qa_unify_white_spaces_bots_a(raw_body);
      free(raw_body);
      if (state->src->client == qa_client_from_name_bots_a(ctx, net_name)) {
        qa_bot_chat_library_remove_console_message(library, cs, message.handle);
        free(net_name);
        free(body);
        free(text);
        continue;
      }
      qa_chat_text_source variables[8] = {{0}};
      variables[6] = qa_chat_text(bot_name);
      variables[7] = qa_chat_text(net_name);
      ai_cvar_update(ctx, "bot_testrchat");
      bool stop = false;
      if (ai_cvar_integer(ctx, "bot_testrchat") != 0) {
        qa_bot_lib_vars_set(ai_variables(ctx), "bot_testrchat", "1");
        bool replied = qa_bot_chat_library_reply_chat(library, cs, qa_chat_text(body), synonym_context, 16, variables);
        ai_print(ctx, replied ? "------------------------\n" : "**** no valid reply ****\n");
      } else if (state->ai_node != QA_AI_NODE_STAND && qa_bot_valid_chat_position_bots_a(ctx, state) &&
                 !qa_team_play_is_on_bots_a(ctx)) {
        double reply_chance = characteristic(ctx, state, QA_BCHAR_CHAT_REPLY);
        if (ai_random(ctx) < AI_F(1.5 / (double)qa_int32((double)ctx->num_bots + 1)) && ai_random(ctx) < reply_chance &&
            qa_bot_chat_library_reply_chat(library, cs, qa_chat_text(body), synonym_context, 16, variables)) {
          qa_bot_chat_library_remove_console_message(library, cs, message.handle);
          state->src->stand_time = (float)(ctx->time + qa_bot_chat_time_bots_a(ctx, state));
          qa_ai_enter_stand_bots_a(ctx, state, "BotCheckConsoleMessages: reply chat");
          stop = true;
        }
      }
      free(net_name);
      free(body);
      if (stop) {
        free(text);
        break;
      }
    }
    qa_bot_chat_library_remove_console_message(library, cs, message.handle);
    free(text);
  }
}

void qa_bot_check_for_grenades_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                      const qa_q3_base_entity_state *entity) {
  if (entity->e_type == QA_Q3_BASE_ET_MISSILE && entity->weapon == QA_Q3_WP_GRENADE_LAUNCHER)
    ai_move_add_avoid_spot(ctx, state->src->ms, ai_entity_pos_base(entity), 160, QA_AVOID_ALWAYS);
}

void qa_bot_check_for_prox_mines_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        const qa_q3_base_entity_state *entity) {
  if (entity->e_type != QA_Q3_BASE_ET_MISSILE || entity->weapon != QA_Q3_WP_PROX_LAUNCHER ||
      entity->generic1 == qa_bot_team_bots_a(ctx, state))
    return;
  if (!(qa_bot_inventory_value_bots_a(state, QA_INV_PLASMAGUN) > 0 && qa_bot_inventory_value_bots_a(state, QA_INV_CELLS) > 0) &&
      !(qa_bot_inventory_value_bots_a(state, QA_INV_ROCKETLAUNCHER) > 0 && qa_bot_inventory_value_bots_a(state, QA_INV_ROCKETS) > 0) &&
      !(qa_bot_inventory_value_bots_a(state, QA_INV_BFG10K) > 0 && qa_bot_inventory_value_bots_a(state, QA_INV_BFGAMMO) > 0))
    return;
  ai_move_add_avoid_spot(ctx, state->src->ms, ai_entity_pos_base(entity), 160, QA_AVOID_ALWAYS);
  if (state->src->num_prox_mines >= QA_MAX_PROXMINES_BOTS_A)
    return;
  state->src->prox_mines[state->src->num_prox_mines++] = qa_js_to_int32(entity->number);
}

void qa_bot_check_for_kamikaze_body_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                           const qa_q3_base_entity_state *entity) {
  (void)ctx; /* TS _context */
  if ((qa_js_to_int32(entity->e_flags) & EF_KAMIKAZE) != 0 && (qa_js_to_int32(entity->e_flags) & 1) != 0)
    state->src->kamikaze_body = qa_js_to_int32(entity->number);
}

static void configstring_sound(qa_game_ai_context_bots_a *ctx, int32_t parameter, char *out, size_t size) {
  ai_configstring(ctx, CS_SOUNDS + parameter, out, size);
}

void qa_bot_check_events_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                const qa_q3_base_entity_state *entity) {
  /* TS state.entityEventTime[entity.number] is undefined off the allocation. */
  if (!(entity->number == trunc(entity->number)) || entity->number < 0 || entity->number >= QA_Q3_MAX_GENTITIES)
    qa_range_error("Bot event entity %s outside its allocation", QA_JS_NUMBER(entity->number));
  size_t number = (size_t)entity->number;
  int32_t last_time = state->src->entity_event_time[number];
  int64_t current_time = ai_entity(ctx, (int64_t)number).event_time;
  if (last_time == current_time)
    return;
  state->src->entity_event_time[number] = (int32_t)current_time;
  qa_game_ai_deathmatch_state_bots_a *dm = ai_deathmatch(ctx);
  int32_t event = qa_js_to_int32(entity->e_type > QA_Q3_BASE_ET_EVENTS ? entity->e_type - QA_Q3_BASE_ET_EVENTS : entity->event) &
                  ~QA_Q3_BASE_EV_EVENT_BITS;
  switch (event) {
  case QA_Q3_EV_OBITUARY: {
    int32_t target = qa_js_to_int32(entity->other_entity_num), attacker = qa_js_to_int32(entity->other_entity_num2),
        mod = qa_js_to_int32(entity->event_parm);
    if (target == state->src->client) {
      state->src->bot_death_type = mod;
      state->src->last_killed_by = attacker;
      state->src->bot_suicide =
          target == attacker || target == QA_Q3_BASE_ENTITYNUM_NONE || target == QA_Q3_BASE_ENTITYNUM_WORLD;
      state->src->num_deaths = qa_int32((double)state->src->num_deaths + 1);
    } else if (attacker == state->src->client) {
      state->src->enemy_death_type = mod;
      state->src->last_killed_player = target;
      state->src->killed_enemy_time = (float)ctx->time;
      state->src->num_kills = qa_int32((double)state->src->num_kills + 1);
    } else if (attacker == state->src->enemy && target == attacker) {
      state->src->enemy_suicide = 1;
    }
    if (missionpack(state) && dm->gametype == QA_Q3_BASE_GT_1FCTF) {
      qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, target);
      if ((ai_int32(info.powerups) & (1 << QA_Q3_PW_NEUTRALFLAG)) != 0 && !qa_bot_same_team_bots_a(ctx, state, target)) {
        state->src->neutral_flag_status = 3;
        state->src->flag_status_changed = 1;
      }
    }
    break;
  }
  case QA_Q3_EV_GLOBAL_SOUND: {
    if (entity->event_parm < 0 || entity->event_parm > 256) {
      char line[80];
      snprintf(line, sizeof line, "EV_GLOBAL_SOUND: eventParm (%s) out of range\n", QA_JS_NUMBER(entity->event_parm));
      ai_print(ctx, line);
      break;
    }
    char sound[128];
    configstring_sound(ctx, qa_js_to_int32(entity->event_parm), sound, sizeof sound);
    if (missionpack(state) && strcmp(sound, "sound/items/kamikazerespawn.wav") == 0)
      qa_bot_dont_avoid_bots_a(ctx, state, "Kamikaze");
    else if (strcmp(sound, "sound/items/poweruprespawn.wav") == 0)
      qa_bot_go_for_powerups_bots_a(ctx, state);
    break;
  }
  case QA_Q3_EV_GLOBAL_TEAM_SOUND: {
    if (dm->gametype == QA_Q3_BASE_GT_CTF) {
      switch (qa_js_to_int32(entity->event_parm)) {
      case QA_Q3_TA_GTS_RED_CAPTURE:
      case QA_Q3_TA_GTS_BLUE_CAPTURE:
        state->src->blue_flag_status = 0;
        state->src->red_flag_status = 0;
        state->src->flag_status_changed = 1;
        break;
      case QA_Q3_TA_GTS_RED_RETURN:
        state->src->blue_flag_status = 0;
        state->src->flag_status_changed = 1;
        break;
      case QA_Q3_TA_GTS_BLUE_RETURN:
        state->src->red_flag_status = 0;
        state->src->flag_status_changed = 1;
        break;
      case QA_Q3_TA_GTS_RED_TAKEN:
        state->src->blue_flag_status = 1;
        state->src->flag_status_changed = 1;
        break;
      case QA_Q3_TA_GTS_BLUE_TAKEN:
        state->src->red_flag_status = 1;
        state->src->flag_status_changed = 1;
        break;
      default:
        break;
      }
    } else if (missionpack(state) && dm->gametype == QA_Q3_BASE_GT_1FCTF) {
      switch (qa_js_to_int32(entity->event_parm)) {
      case QA_Q3_TA_GTS_RED_CAPTURE:
      case QA_Q3_TA_GTS_BLUE_CAPTURE:
      case QA_Q3_TA_GTS_RED_RETURN:
      case QA_Q3_TA_GTS_BLUE_RETURN:
        state->src->neutral_flag_status = 0;
        state->src->flag_status_changed = 1;
        break;
      case QA_Q3_TA_GTS_RED_TAKEN:
        state->src->neutral_flag_status = qa_bot_team_bots_a(ctx, state) == QA_Q3_BASE_TEAM_RED ? 2 : 1;
        state->src->flag_status_changed = 1;
        break;
      case QA_Q3_TA_GTS_BLUE_TAKEN:
        state->src->neutral_flag_status = qa_bot_team_bots_a(ctx, state) == QA_Q3_BASE_TEAM_BLUE ? 2 : 1;
        state->src->flag_status_changed = 1;
        break;
      default:
        break;
      }
    }
    break;
  }
  case QA_Q3_EV_PLAYER_TELEPORT_IN:
    dm->last_teleport_origin = ai_fv(entity->origin);
    dm->last_teleport_time = ctx->time;
    break;
  case QA_Q3_EV_GENERAL_SOUND:
    if (entity->number == state->src->client) {
      if (entity->event_parm < 0 || entity->event_parm > 256) {
        char line[80];
        snprintf(line, sizeof line, "EV_GENERAL_SOUND: eventParm (%s) out of range\n", QA_JS_NUMBER(entity->event_parm));
        ai_print(ctx, line);
        break;
      }
      char sound[128];
      configstring_sound(ctx, qa_js_to_int32(entity->event_parm), sound, sizeof sound);
      if (strcmp(sound, "*falling1.wav") == 0 && qa_bot_inventory_value_bots_a(state, QA_INV_TELEPORTER) > 0)
        qa_bot_action_use(ai_actions(ctx), state->src->client);
    }
    break;
  default:
    break;
  }
}

void qa_bot_check_snapshot_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  ai_move_add_avoid_spot(ctx, state->src->ms, qa_math_vec3(0, 0, 0), 0, QA_AVOID_CLEAR);
  state->src->kamikaze_body = 0;
  state->src->num_prox_mines = 0;
  int64_t sequence = 0;
  while (true) {
    qa_q3_base_entity_state entity;
    int64_t next_sequence = ai_snapshot_entity(ctx, state->src->client, sequence, &entity);
    if (next_sequence == -1)
      break;
    sequence = next_sequence;
    qa_bot_check_events_bots_a(ctx, state, &entity);
    qa_bot_check_for_grenades_bots_a(ctx, state, &entity);
    if (missionpack(state)) {
      qa_bot_check_for_prox_mines_bots_a(ctx, state, &entity);
      qa_bot_check_for_kamikaze_body_bots_a(ctx, state, &entity);
    }
  }
  qa_q3_base_entity_state player;
  ai_entity_state_or_blank(ctx, state->src->client, &player);
  player.event = state->src->cur_ps.external_event;
  player.event_parm = state->src->cur_ps.external_event_parm;
  qa_bot_check_events_bots_a(ctx, state, &player);
}

void qa_bot_check_air_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_inventory_value_bots_a(state, QA_INV_ENVIRONMENTSUIT) <= 0 &&
      (ai_point_contents(ctx, qa_bot_v3(state->src->eye), -1) & LIQUID) != 0)
    return;
  state->src->last_air_time = (float)ctx->time;
}

static void engine_print_callback(void *ctx, const char *text) { ai_print(ctx, text); }

static void first_think_setup(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  char gender[144];
  snprintf(gender, sizeof gender, "%s", qa_bot_character_string(ai_characters(ctx), state->src->character, QA_BCHAR_GENDER));
  char userinfo[1024];
  ai_engine_get_userinfo(ctx, state->src->client, userinfo, sizeof userinfo);
  char *updated = qa_bot_info_set_value_for_key(userinfo, "sex", gender, engine_print_callback, ctx);
  if (updated == NULL)
    qa_range_error("Out of memory writing userinfo");
  ai_engine_set_userinfo(ctx, state->src->client, updated);
  free(updated);
  if (!state->src->map_restart && ai_game_game_type(ctx) != QA_Q3_BASE_GT_TOURNAMENT) {
    char command[144];
    snprintf(command, sizeof command, "team %s", qa_bot_state_text(state, state->src->team, "team"));
    qa_bot_action_command(ai_actions(ctx), state->src->client, command);
  }
  qa_bot_chat_library_set_gender(ai_chat(ctx), state->src->cs,
                                 gender[0] == 'm'   ? QA_CHAT_MALE
                                 : gender[0] == 'f' ? QA_CHAT_FEMALE
                                                    : QA_CHAT_GENDERLESS);
  char name[144 + 22];
  qa_client_name_bots_a(ctx, state->src->client, 144, name);
  qa_bot_chat_library_set_name(ai_chat(ctx), state->src->cs, qa_chat_text(name), true, state->src->client);
  state->src->last_frame_health = (int32_t)qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH);
  state->src->last_hit_count = state->src->cur_ps.persistant[QA_Q3_BASE_PERS_HITS];
  state->src->setup_count = 0;
  qa_bot_setup_alternative_route_goals_bots_a(ctx);
}

void qa_bot_deathmatch_ai_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, double think_time) {
  (void)think_time; /* TS _thinkTime */
  if (state->src->setup_count > 0) {
    state->src->setup_count--;
    if (state->src->setup_count > 0)
      return;
    first_think_setup(ctx, state);
  }
  state->src->flags &= ~QA_BOT_FLAG_IDEALVIEWSET;
  if (!qa_bot_intermission_bots_a(ctx, state)) {
    qa_bot_set_teleport_time_bots_a(ctx, state);
    qa_bot_update_inventory_bots_a(ctx, state);
    qa_bot_check_snapshot_bots_a(ctx, state);
    qa_bot_check_air_bots_a(ctx, state);
  }
  qa_bot_check_console_messages_bots_a(ctx, state);
  if (!qa_bot_intermission_bots_a(ctx, state) && !qa_bot_is_observer_bots_a(ctx, state))
    qa_bot_team_ai_bots_a(ctx, state);
  if (state->ai_node == QA_AI_NODE_NONE)
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "BotDeathmatchAI: no ai node");
  if (!state->src->enter_game_chat && state->src->enter_game_time > AI_F(ctx->time - 8)) {
    if (qa_bot_chat_enter_game_bots_a(ctx, state)) {
      state->src->stand_time = (float)(ctx->time + qa_bot_chat_time_bots_a(ctx, state));
      qa_ai_enter_stand_bots_a(ctx, state, "BotDeathmatchAI: chat enter game");
    }
    state->src->enter_game_chat = 1;
  }
  qa_bot_reset_node_switches_bots_a(ctx);
  int switches = 0;
  for (; switches < QA_MAX_NODESWITCHES; switches++)
    if (qa_run_ai_node_bots_a(ctx, state))
      break;
  if (!state->src->inuse)
    return;
  if (switches >= QA_MAX_NODESWITCHES) {
    qa_bot_goal_dump_goal_stack(ai_goals(ctx), state->src->gs, NULL, 0);
    qa_bot_goal_dump_avoid_goals(ai_goals(ctx), state->src->gs, NULL, 0);
    qa_bot_dump_node_switches_bots_a(ctx, state);
    char name[144 + 22];
    qa_client_name_bots_a(ctx, state->src->client, 144, name);
    qa_q3_game_format_arg args[3] = {qa_q3_game_format_string(name), qa_q3_game_format_number(ctx->time),
                                     qa_q3_game_format_number(QA_MAX_NODESWITCHES)};
    char *line = qa_q3_game_format("^1Error: %s at %1.1f switched more than %d AI nodes\n", args, 3, 32000);
    if (line == NULL)
      qa_range_error("Out of memory formatting AI node error");
    ai_print(ctx, line);
    free(line);
  }
  state->src->last_frame_health = (int32_t)qa_bot_inventory_value_bots_a(state, QA_INV_HEALTH);
  state->src->last_hit_count = state->src->cur_ps.persistant[QA_Q3_BASE_PERS_HITS];
}
