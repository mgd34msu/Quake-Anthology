/* Port of src/content/q3/team-arena/client-effects.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/client_effects.h"

#include <math.h>
#include "qa/core/numeric.h"
#include <stdint.h>

#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

enum { CONTENTS_LAVA = 8, CONTENTS_SLIME = 16, EF_TICKING = 2, EF_CONNECTION = 0x2000,
  EF_PLAYER_EVENT = 0x10, CHAN_VOICE = 3 };

static qa_q3_game_client *client_of(qa_q3_game_entity *entity) {
  if (entity->client == NULL) qa_range_error("Client effects require a client entity");
  return entity->client;
}
static double s32(double v) { return qa_js_to_int32(v); }
static double slot_get(qa_q3_base_player_state_slots *s, double i) { return qa_q3_base_player_state_slots_get(s, (size_t)i); }
static void slot_set(qa_q3_base_player_state_slots *s, double i, double v) { qa_q3_base_player_state_slots_set(s, (size_t)i, v); }
static qa_q3_base_product base_product(qa_q3_base_product p) { return p == QA_Q3_BASE_PRODUCT_MISSIONPACK ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3; }

void qa_q3_ta_damage_feedback(qa_q3_ta_event_context context, qa_q3_game_entity *player) {
  qa_q3_game_client *client = client_of(player);
  if (client->ps.pm_type == QA_Q3_PM_DEAD) return;
  double count = qa_js_min(255, (double)(float)s32(client->damage_blood + client->damage_armor));
  if (count == 0) return;
  if (client->damage_from_world) {
    client->ps.damage_pitch = 255;
    client->ps.damage_yaw = 255;
    client->damage_from_world = false;
  } else {
    qa_vec3 angles = qa_vector_to_angles(client->damage_from);
    client->ps.damage_pitch = s32(trunc((float)((float)(angles.x / 360) * 256)));
    client->ps.damage_yaw = s32(trunc((float)((float)(angles.y / 360) * 256)));
  }
  if (context.combat->time > player->pain_debounce_time && (qa_js_to_int32(player->flags) & QA_Q3_GAME_FLAG_GODMODE) == 0) {
    player->pain_debounce_time = s32(context.combat->time + 700);
    qa_q3_entity_pool_add_event(context.combat->entities, player, QA_Q3_EV_PAIN, qa_q3_game_entity_health(player));
    client->ps.damage_event = s32(client->ps.damage_event + 1);
  }
  client->ps.damage_count = trunc(count);
  client->damage_blood = 0;
  client->damage_armor = 0;
  client->damage_knockback = 0;
}

static void damage_self(qa_q3_ta_client_effects_context *context, qa_q3_game_entity *entity,
    double amount, double flags, double mod) {
  qa_q3_damage_participant target = qa_q3_participant_entity(entity);
  qa_q3_combat_damage(context->combat, target, NULL, NULL, NULL, NULL, amount, flags, mod);
}

void qa_q3_ta_world_effects(qa_q3_ta_client_effects_context *context, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = client_of(entity);
  double time = context->combat->time;
  if (client->noclip) { client->air_out_time = s32(time + 12000); return; }
  double waterlevel = entity->waterlevel;
  bool suit = slot_get(&client->ps.powerups, QA_Q3_PW_BATTLESUIT) > time;
  if (waterlevel == 3) {
    if (suit) client->air_out_time = s32(time + 10000);
    if (client->air_out_time < time) {
      client->air_out_time = s32(client->air_out_time + 1000);
      if (qa_q3_game_entity_health(entity) > 0) {
        entity->damage = qa_js_min(15, s32(entity->damage + 2));
        const char *path = qa_q3_game_entity_health(entity) <= entity->damage ? "*drown.wav"
            : ((qa_js_to_int32(context->random_int(context->ctx)) & 1) != 0 ? "sound/player/gurp1.wav" : "sound/player/gurp2.wav");
        context->sound(context->ctx, entity, CHAN_VOICE, context->sound_index(context->ctx, path));
        entity->pain_debounce_time = s32(time + 200);
        damage_self(context, entity, entity->damage, QA_Q3_DAMAGE_NO_ARMOR, 14);
      }
    }
  } else {
    client->air_out_time = s32(time + 12000);
    entity->damage = 2;
  }
  if (waterlevel != 0 && (qa_js_to_int32(entity->watertype) & (CONTENTS_LAVA | CONTENTS_SLIME)) != 0 &&
      qa_q3_game_entity_health(entity) > 0 && entity->pain_debounce_time <= time) {
    if (suit) qa_q3_entity_pool_add_event(context->combat->entities, entity, QA_Q3_EV_POWERUP_BATTLESUIT, 0);
    else {
      if ((qa_js_to_int32(entity->watertype) & CONTENTS_LAVA) != 0)
        damage_self(context, entity, (double)(int32_t)(30 * qa_js_to_int32(waterlevel)), 0, 16);
      if ((qa_js_to_int32(entity->watertype) & CONTENTS_SLIME) != 0)
        damage_self(context, entity, (double)(int32_t)(10 * qa_js_to_int32(waterlevel)), 0, 15);
    }
  }
}

void qa_q3_ta_set_client_sound(qa_q3_ta_client_effects_context *context, qa_q3_game_entity *entity) {
  double * ref = &client_of(entity)->ps.loop_sound;
  *ref =
      (context->combat->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && (qa_js_to_int32(entity->s.e_flags) & EF_TICKING) != 0)
          ? context->sound_index(context->ctx, "sound/weapons/proxmine/wstbtick.wav")
          : (entity->waterlevel != 0 && (qa_js_to_int32(entity->watertype) & (CONTENTS_LAVA | CONTENTS_SLIME)) != 0
                ? context->fry_sound : 0);
}

static const qa_q3_ta_ammo_regeneration_rule ammo_regeneration[] = {
  {QA_Q3_WP_MACHINEGUN, 50, 4, 1000}, {QA_Q3_WP_SHOTGUN, 10, 1, 1500},
  {QA_Q3_WP_GRENADE_LAUNCHER, 10, 1, 2000}, {QA_Q3_WP_ROCKET_LAUNCHER, 10, 1, 1750},
  {QA_Q3_WP_LIGHTNING, 50, 5, 1500}, {QA_Q3_WP_RAILGUN, 10, 1, 1750},
  {QA_Q3_WP_PLASMAGUN, 50, 5, 1500}, {QA_Q3_WP_BFG, 10, 1, 4000},
  {QA_Q3_WP_NAILGUN, 10, 1, 1250}, {QA_Q3_WP_PROX_LAUNCHER, 5, 1, 2000},
  {QA_Q3_WP_CHAINGUN, 100, 5, 1000}};
enum { AMMO_RULE_COUNT = 11 };

static double persistent_tag(qa_q3_game_client *client) {
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(base_product(client->ps.state_product));
  if (schema->product == QA_Q3_BASE_PRODUCT_BASEQ3) return QA_Q3_PW_NONE;
  return qa_q3_base_item_at(QA_Q3_BASE_PRODUCT_MISSIONPACK,
             slot_get(&client->ps.stats, schema->persistent_powerup))->tag;
}

double qa_q3_ta_client_speed_multiplier(const qa_q3_base_player_state *ps) {
  qa_q3_base_product product2;
  double index;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(ps));
  if (schema->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
      (product2 = qa_q3_base_player_state_record_product(ps), index = qa_q3_base_player_state_slots_get(&ps->stats, schema->persistent_powerup), qa_q3_base_item_at(product2,
          index))->tag == QA_Q3_PW_SCOUT)
    return 1.5;
  return qa_q3_base_player_state_slots_get(&ps->powerups, QA_Q3_PW_HASTE) != 0 ? 1.3 : 1;
}

const qa_q3_ta_ammo_regeneration_rule *qa_q3_ta_ammo_regeneration_rule_for(double weapon) {
  for (int i = 0; i < AMMO_RULE_COUNT; i++)
    if (ammo_regeneration[i].weapon == weapon) return &ammo_regeneration[i];
  qa_range_error("Weapon has no original Ammo Regen rule");
}

qa_q3_ta_ammo_regeneration_step qa_q3_ta_step_ammo_regeneration(
    const qa_q3_ta_ammo_regeneration_rule *rule, double count, double milliseconds, double msec) {
  double elapsed = s32(milliseconds + msec);
  if (count >= rule->max) elapsed = 0;
  if (elapsed < rule->time) { qa_q3_ta_ammo_regeneration_step r = {elapsed, false, 0}; return r; }
  while (elapsed >= rule->time) elapsed -= rule->time;
  qa_q3_ta_ammo_regeneration_step r = {elapsed, true, qa_js_min(rule->max, s32(count + rule->increment))};
  return r;
}

void qa_q3_ta_client_timer_actions(qa_q3_ta_event_context context, qa_q3_game_entity *entity,
    double msec, const qa_q3_ta_client_timer_ownership *ownership) {
  static const qa_q3_ta_client_timer_ownership native_timers = {true, false, NULL, 0};
  if (ownership == NULL) ownership = &native_timers;
  qa_q3_game_client *client = client_of(entity);
  qa_q3_base_player_state *ps = &client->ps;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(base_product(ps->state_product));
  client->time_residual = s32(client->time_residual + msec);
  while (client->time_residual >= 1000) {
    client->time_residual -= 1000;
    double maximum = slot_get(&ps->stats, schema->max_health);
    double max_health = (ps->state_product == QA_Q3_BASE_PRODUCT_MISSIONPACK && persistent_tag(client) == QA_Q3_PW_GUARD)
        ? trunc(maximum / 2) : (slot_get(&ps->powerups, QA_Q3_PW_REGEN) != 0 ? maximum : 0);
    double health = qa_q3_game_entity_health(entity);
    if ((ps->state_product == QA_Q3_BASE_PRODUCT_BASEQ3 && slot_get(&ps->powerups, QA_Q3_PW_REGEN) != 0) || max_health != 0) {
      if (health < max_health) {
        health = s32(health + 15);
        double cap = (double)(float)((double)(float)max_health * (double)(float)1.1);
        if ((double)(float)health > cap) health = s32(trunc(cap));
        qa_q3_game_entity_set_health(entity, health);
        qa_q3_entity_pool_add_event(context.combat->entities, entity, QA_Q3_EV_POWERUP_REGEN, 0);
      } else if (health < (double)qa_js_to_int32((max_health * 2))) {
        health = s32(health + 5);
        double two = (double)(int32_t)(qa_js_to_int32(max_health) * 2);
        if (health > two) health = two;
        qa_q3_game_entity_set_health(entity, health);
        qa_q3_entity_pool_add_event(context.combat->entities, entity, QA_Q3_EV_POWERUP_REGEN, 0);
      }
    } else if (ownership->ordinary_decay && health > maximum) {
      qa_q3_game_entity_set_health(entity, s32(health - 1));
    }
    if (ownership->ordinary_decay && slot_get(&ps->stats, schema->armor) > maximum)
      slot_set(&ps->stats, schema->armor, slot_get(&ps->stats, schema->armor) - 1);
  }
  if (ps->state_product == QA_Q3_BASE_PRODUCT_MISSIONPACK && persistent_tag(client) == QA_Q3_PW_AMMOREGEN) {
    if (!ownership->has_ammo) {
      for (int i = 0; i < AMMO_RULE_COUNT; i++) {
        const qa_q3_ta_ammo_regeneration_rule *rule = &ammo_regeneration[i];
        double count3 = slot_get(&ps->ammo, rule->weapon);
        double milliseconds3 = slot_get(&client->ammo_times, rule->weapon);
        qa_q3_ta_ammo_regeneration_step next = qa_q3_ta_step_ammo_regeneration(rule,
            count3, milliseconds3, msec);
        if (next.has_count) slot_set(&ps->ammo, rule->weapon, next.count);
        slot_set(&client->ammo_times, rule->weapon, next.milliseconds);
      }
    } else {
      for (size_t i = 0; i < ownership->ammo_count; i++) {
        const qa_q3_ta_mapped_ammo_timer *timer = &ownership->ammo[i];
        if (!timer->current(timer->ctx)) break;
        const qa_q3_ta_ammo_regeneration_rule * rule2 = timer->rule;
        double count2 = timer->get_count(timer->ctx);
        double milliseconds2 = timer->get_elapsed(timer->ctx);
        qa_q3_ta_ammo_regeneration_step next = qa_q3_ta_step_ammo_regeneration(rule2,
            count2, milliseconds2, msec);
        if (next.has_count) timer->set_count(timer->ctx, next.count);
        if (timer->current(timer->ctx)) timer->set_elapsed(timer->ctx, next.milliseconds);
      }
    }
  }
}

void qa_q3_ta_send_pending_predictable_events(qa_q3_ta_event_context context, qa_q3_base_player_state *ps) {
  if (ps->entity_event_sequence >= ps->event_sequence) return;
  int32_t sequence = qa_int32(ps->entity_event_sequence);
  double event = qa_int32(slot_get(&ps->events, sequence & 1)) | ((sequence & 3) << 8);
  double external = ps->external_event;
  ps->external_event = 0;
  qa_q3_game_entity *temporary = qa_q3_entity_pool_temp_entity(context.combat->entities, qa_q3_base_player_state_record_origin(ps), event);
  double number = temporary->s.number;
  qa_q3_game_player_state_to_entity_state(ps, &temporary->s, true, false, 0);
  temporary->s.number = number;
  temporary->s.e_type = QA_Q3_BASE_ET_EVENTS + event;
  temporary->s.e_flags = (double)(qa_js_to_int32(temporary->s.e_flags) | EF_PLAYER_EVENT);
  temporary->s.other_entity_num = ps->client_num;
  temporary->r.sv_flags = (double)(qa_js_to_int32(temporary->r.sv_flags) | QA_Q3_BASE_SVF_NOTSINGLECLIENT);
  temporary->r.single_client = ps->client_num;
  ps->external_event = external;
}

void qa_q3_ta_update_client_powerups(qa_q3_ta_event_context context, qa_q3_game_client *client) {
  double time = context.combat->time;
  for (size_t index = 0; index < client->ps.powerups.length; index++)
    if (slot_get(&client->ps.powerups, (double)index) < time) slot_set(&client->ps.powerups, (double)index, 0);
  if (context.combat->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    double tag = persistent_tag(client);
    static const double persistent[] = {QA_Q3_PW_GUARD, QA_Q3_PW_SCOUT, QA_Q3_PW_DOUBLER, QA_Q3_PW_AMMOREGEN};
    for (int i = 0; i < 4; i++)
      if (tag == persistent[i]) slot_set(&client->ps.powerups, persistent[i], time);
    if (client->invulnerability_time > time) slot_set(&client->ps.powerups, QA_Q3_PW_INVULNERABILITY, time);
  }
}

void qa_q3_ta_client_end_frame(qa_q3_ta_client_effects_context *context, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = client_of(entity);
  if (client->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR) {
    context->spectator_end_frame(context->ctx, entity);
    return;
  }
  double time = context->combat->time;
  qa_q3_ta_event_context events = {context->combat};
  qa_q3_ta_update_client_powerups(events, client);
  if (context->intermission_time != 0) return;
  qa_q3_ta_world_effects(context, entity);
  qa_q3_ta_damage_feedback(events, entity);
  qa_q3_game_client *current = client_of(entity);
  if (s32(time - current->last_cmd_time) > 1000)
    entity->s.e_flags = (double)(qa_js_to_int32(entity->s.e_flags) | EF_CONNECTION);
  else
    entity->s.e_flags = (double)(qa_js_to_int32(entity->s.e_flags) & ~EF_CONNECTION);
  qa_q3_base_player_state_record_set_health(&current->ps, qa_q3_game_entity_health(entity));
  qa_q3_ta_set_client_sound(context, entity);
  if (context->smooth_clients) qa_q3_game_player_state_to_entity_state(&current->ps, &entity->s, true, true, current->ps.command_time);
  else qa_q3_game_player_state_to_entity_state(&current->ps, &entity->s, true, false, 0);
  qa_q3_ta_send_pending_predictable_events(events, &current->ps);
}
