/* Port of src/content/q3/team-arena/client-effects.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/client_effects.h"

#include <math.h>
#include <stdint.h>

#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

enum { CONTENTS_LAVA = 8, CONTENTS_SLIME = 16, EF_TICKING = 2, EF_CONNECTION = 0x2000,
  EF_PLAYER_EVENT = 0x10, CHAN_VOICE = 3 };

static qa_q3_game_client *client_of(qa_q3_game_entity *entity) {
  if (entity->client == NULL) qa_range_error("Client effects require a client entity");
  return entity->client;
}
static double s32(double v) { return (int32_t)v; }
static double slot_get(qa_q3_game_player_slots_mirror *s, double i) { return qa_q3_game_slots_get_mirror(s, (size_t)i); }
static void slot_set(qa_q3_game_player_slots_mirror *s, double i, double v) { qa_q3_game_slots_set_mirror(s, (size_t)i, v); }
static qa_q3_base_product base_product(qa_q3_game_product p) { return p == QA_Q3_GAME_PRODUCT_MISSIONPACK ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3; }

void qa_q3_ta_damage_feedback(qa_q3_ta_event_context context, qa_q3_game_entity *player) {
  qa_q3_game_client *client = client_of(player);
  if (client->ps.pm_type == QA_Q3_PM_DEAD) return;
  double count = fmin(255, (double)(float)s32(client->damage_blood + client->damage_armor));
  if (count == 0) return;
  if (client->damage_from_world) {
    /* TS ps.damagePitch/Yaw = 255: no mirror fields. */
    client->damage_from_world = false;
  } else {
    /* TS computes damagePitch/Yaw from vectorToAngles(damageFrom): no mirror fields. */
    (void)qa_vector_to_angles(client->damage_from);
  }
  if (context.combat->time > player->pain_debounce_time && ((int32_t)player->flags & QA_Q3_GAME_FLAG_GODMODE) == 0) {
    player->pain_debounce_time = s32(context.combat->time + 700);
    qa_q3_entity_pool_add_event(context.combat->entities, player, QA_Q3_EV_PAIN, qa_q3_game_entity_health(player));
    /* TS ps.damageEvent++: no mirror field. */
  }
  /* TS ps.damageCount = trunc(count): no mirror field. */
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
        entity->damage = fmin(15, s32(entity->damage + 2));
        const char *path = qa_q3_game_entity_health(entity) <= entity->damage ? "*drown.wav"
            : (((int32_t)context->random_int(context->ctx) & 1) != 0 ? "sound/player/gurp1.wav" : "sound/player/gurp2.wav");
        context->sound(context->ctx, entity, CHAN_VOICE, context->sound_index(context->ctx, path));
        entity->pain_debounce_time = s32(time + 200);
        damage_self(context, entity, entity->damage, QA_Q3_DAMAGE_NO_ARMOR, 14);
      }
    }
  } else {
    client->air_out_time = s32(time + 12000);
    entity->damage = 2;
  }
  if (waterlevel != 0 && ((int32_t)entity->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) != 0 &&
      qa_q3_game_entity_health(entity) > 0 && entity->pain_debounce_time <= time) {
    if (suit) qa_q3_entity_pool_add_event(context->combat->entities, entity, QA_Q3_EV_POWERUP_BATTLESUIT, 0);
    else {
      if (((int32_t)entity->watertype & CONTENTS_LAVA) != 0)
        damage_self(context, entity, (double)(int32_t)(30 * (int32_t)waterlevel), 0, 16);
      if (((int32_t)entity->watertype & CONTENTS_SLIME) != 0)
        damage_self(context, entity, (double)(int32_t)(10 * (int32_t)waterlevel), 0, 15);
    }
  }
}

void qa_q3_ta_set_client_sound(qa_q3_ta_client_effects_context *context, qa_q3_game_entity *entity) {
  client_of(entity)->ps.loop_sound =
      (context->combat->product == QA_Q3_GAME_PRODUCT_MISSIONPACK && ((int32_t)entity->s.e_flags & EF_TICKING) != 0)
          ? context->sound_index(context->ctx, "sound/weapons/proxmine/wstbtick.wav")
          : (entity->waterlevel != 0 && ((int32_t)entity->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) != 0
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
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(base_product(client->ps.product));
  if (schema->product == QA_Q3_BASE_PRODUCT_BASEQ3) return QA_Q3_PW_NONE;
  return qa_q3_base_item_at(QA_Q3_BASE_PRODUCT_MISSIONPACK,
             slot_get(&client->ps.stats, schema->persistent_powerup))->tag;
}

double qa_q3_ta_client_speed_multiplier(const qa_q3_base_player_state *ps) {
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(ps));
  if (schema->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
      qa_q3_base_item_at(qa_q3_base_player_state_record_product(ps),
          qa_q3_base_player_state_slots_get(&ps->stats, schema->persistent_powerup))->tag == QA_Q3_PW_SCOUT)
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
  qa_q3_ta_ammo_regeneration_step r = {elapsed, true, fmin(rule->max, s32(count + rule->increment))};
  return r;
}

void qa_q3_ta_client_timer_actions(qa_q3_ta_event_context context, qa_q3_game_entity *entity,
    double msec, const qa_q3_ta_client_timer_ownership *ownership) {
  static const qa_q3_ta_client_timer_ownership native_timers = {true, false, NULL, 0};
  if (ownership == NULL) ownership = &native_timers;
  qa_q3_game_client *client = client_of(entity);
  qa_q3_game_player_state_mirror *ps = &client->ps;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(base_product(ps->product));
  client->time_residual = s32(client->time_residual + msec);
  while (client->time_residual >= 1000) {
    client->time_residual -= 1000;
    double maximum = slot_get(&ps->stats, schema->max_health);
    double max_health = (ps->product == QA_Q3_GAME_PRODUCT_MISSIONPACK && persistent_tag(client) == QA_Q3_PW_GUARD)
        ? trunc(maximum / 2) : (slot_get(&ps->powerups, QA_Q3_PW_REGEN) != 0 ? maximum : 0);
    double health = qa_q3_game_entity_health(entity);
    if ((ps->product == QA_Q3_GAME_PRODUCT_BASEQ3 && slot_get(&ps->powerups, QA_Q3_PW_REGEN) != 0) || max_health != 0) {
      if (health < max_health) {
        health = s32(health + 15);
        double cap = (double)(float)((double)(float)max_health * (double)(float)1.1);
        if ((double)(float)health > cap) health = s32(trunc(cap));
        qa_q3_game_entity_set_health(entity, health);
        qa_q3_entity_pool_add_event(context.combat->entities, entity, QA_Q3_EV_POWERUP_REGEN, 0);
      } else if (health < (double)(int32_t)(max_health * 2)) {
        health = s32(health + 5);
        double two = (double)(int32_t)((int32_t)max_health * 2);
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
  if (ps->product == QA_Q3_GAME_PRODUCT_MISSIONPACK && persistent_tag(client) == QA_Q3_PW_AMMOREGEN) {
    if (!ownership->has_ammo) {
      for (int i = 0; i < AMMO_RULE_COUNT; i++) {
        const qa_q3_ta_ammo_regeneration_rule *rule = &ammo_regeneration[i];
        qa_q3_ta_ammo_regeneration_step next = qa_q3_ta_step_ammo_regeneration(rule,
            slot_get(&ps->ammo, rule->weapon), slot_get(&client->ammo_times, rule->weapon), msec);
        if (next.has_count) slot_set(&ps->ammo, rule->weapon, next.count);
        slot_set(&client->ammo_times, rule->weapon, next.milliseconds);
      }
    } else {
      for (size_t i = 0; i < ownership->ammo_count; i++) {
        const qa_q3_ta_mapped_ammo_timer *timer = &ownership->ammo[i];
        if (!timer->current(timer->ctx)) break;
        qa_q3_ta_ammo_regeneration_step next = qa_q3_ta_step_ammo_regeneration(timer->rule,
            timer->get_count(timer->ctx), timer->get_elapsed(timer->ctx), msec);
        if (next.has_count) timer->set_count(timer->ctx, next.count);
        if (timer->current(timer->ctx)) timer->set_elapsed(timer->ctx, next.milliseconds);
      }
    }
  }
}

void qa_q3_ta_send_pending_predictable_events(qa_q3_ta_event_context context,
    qa_q3_game_player_state_mirror *ps, double entity_event_sequence) {
  double event_sequence = ps->generic; /* mirror eventSequence */
  if (entity_event_sequence >= event_sequence) return;
  double event = slot_get(&ps->events, (double)((int32_t)entity_event_sequence & 1)) +
                 (double)(((int32_t)entity_event_sequence & 3) << 8);
  double external = ps->external_event;
  ps->external_event = 0;
  qa_q3_game_entity *temporary = qa_q3_entity_pool_temp_entity(context.combat->entities, ps->origin, event);
  double number = temporary->s.number;
  /* TS playerStateToEntityState(ps, temporary.s, true): mirror s cannot
   * take the shared snapshot conversion; the fields the TS sets after
   * the conversion are applied below. */
  temporary->s.number = number;
  temporary->s.e_type = QA_Q3_BASE_ET_EVENTS + event;
  temporary->s.e_flags = (double)((int32_t)temporary->s.e_flags | EF_PLAYER_EVENT);
  temporary->s.other_entity_num = ps->client_num;
  temporary->r.sv_flags = (double)((int32_t)temporary->r.sv_flags | QA_Q3_BASE_SVF_NOTSINGLECLIENT);
  temporary->r.single_client = ps->client_num;
  ps->external_event = external;
}

void qa_q3_ta_update_client_powerups(qa_q3_ta_event_context context, qa_q3_game_client *client) {
  double time = context.combat->time;
  for (size_t index = 0; index < client->ps.powerups.length; index++)
    if (slot_get(&client->ps.powerups, (double)index) < time) slot_set(&client->ps.powerups, (double)index, 0);
  if (context.combat->product == QA_Q3_GAME_PRODUCT_MISSIONPACK) {
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
    entity->s.e_flags = (double)((int32_t)entity->s.e_flags | EF_CONNECTION);
  else
    entity->s.e_flags = (double)((int32_t)entity->s.e_flags & ~EF_CONNECTION);
  current->ps.health = qa_q3_game_entity_health(entity);
  qa_q3_ta_set_client_sound(context, entity);
  /* TS extrapolate uses ps.commandTime (no mirror field): combat time. */
  if (context->smooth_clients) context->snapshot_extrapolate(context->ctx, current, entity, time);
  else context->snapshot(context->ctx, current, entity);
  qa_q3_ta_send_pending_predictable_events(events, &current->ps, 0);
}
