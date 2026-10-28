/* Port of src/content/q3/team-arena/client-events.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/client_events.h"

#include <math.h>
#include <stdint.h>

#include "qa/content/q3/base/game/rankings.h"
#include "qa/foundation/support.h"

static double s32(double v) { return (int32_t)v; }
static double slot_get(qa_q3_game_player_slots_mirror *s, double i) { return qa_q3_game_slots_get_mirror(s, (size_t)i); }
static void slot_set(qa_q3_game_player_slots_mirror *s, double i, double v) { qa_q3_game_slots_set_mirror(s, (size_t)i, v); }

static void use_teleporter(qa_q3_ta_client_events_context *context, qa_q3_game_entity *entity);

typedef struct teleport_closure { qa_q3_ta_client_events_context *context; } teleport_closure;
static void teleport_call(void *ctx, qa_q3_game_entity *entity) {
  use_teleporter(((teleport_closure *)ctx)->context, entity);
}
static void kamikaze_call(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_ta_client_events_context *c = ((teleport_closure *)ctx)->context;
  c->start_kamikaze(c->ctx, entity);
}
static void portal_source_call(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_ta_client_events_context *c = ((teleport_closure *)ctx)->context;
  c->drop_portal_source(c->ctx, entity);
}
static void portal_destination_call(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_ta_client_events_context *c = ((teleport_closure *)ctx)->context;
  c->drop_portal_destination(c->ctx, entity);
}

void qa_q3_ta_client_events(qa_q3_ta_client_events_context *context, qa_q3_game_entity *entity,
    double old_event_sequence) {
  qa_q3_game_client *client = entity->client;
  if (client == NULL) qa_range_error("ClientEvents requires a client entity");
  old_event_sequence = s32(old_event_sequence);
  double oldest = s32(client->ps.generic - 2);
  if (old_event_sequence < oldest) old_event_sequence = oldest;
  for (double index = old_event_sequence; index < client->ps.generic; index = s32(index + 1)) {
    double event = slot_get(&client->ps.events, (double)((int32_t)index & 1));
    if (event == QA_Q3_EV_FALL_MEDIUM || event == QA_Q3_EV_FALL_FAR) {
      if (entity->s.e_type != QA_Q3_BASE_ET_PLAYER || ((int32_t)context->dmflags & 8) != 0) continue;
      entity->pain_debounce_time = s32(context->combat->time + 200);
      qa_q3_damage_participant target = qa_q3_participant_entity(entity);
      qa_q3_combat_damage(context->combat, target, NULL, NULL, NULL, NULL,
          event == QA_Q3_EV_FALL_FAR ? 10 : 5, 0, 19);
    } else if (event == QA_Q3_EV_FIRE_WEAPON) {
      if (context->primary_attack_allowed == NULL ||
          context->primary_attack_allowed(context->ctx, qa_q3_game_entity_actor(entity).id))
        context->fire_weapon(context->ctx, entity);
    } else if (event >= QA_Q3_EV_USE_ITEM1 && event <= QA_Q3_EV_USE_ITEM5) {
      teleport_closure closure = {context};
      qa_q3_ta_holdable_context holdable = {context->product, context->combat, &closure,
          teleport_call, kamikaze_call, portal_source_call, portal_destination_call};
      qa_q3_ta_use_holdable(&holdable, entity, event);
    }
  }
}

void qa_q3_ta_use_holdable(qa_q3_ta_holdable_context *context, qa_q3_game_entity *entity, double event) {
  qa_q3_game_client *client = entity->client;
  if (client == NULL) qa_range_error("Holdable effects require a client entity");
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(context->product);
  if (event == QA_Q3_EV_USE_ITEM1) {
    qa_q3_ranking_reports_use_holdable(&context->combat->entities->rankings, entity->slot, 1);
    context->teleport(context->ctx, entity);
  } else if (event == QA_Q3_EV_USE_ITEM2) {
    qa_q3_ranking_reports_use_holdable(&context->combat->entities->rankings, entity->slot, 2);
    qa_q3_game_entity_set_health(entity, s32(slot_get(&client->ps.stats, schema->max_health) + 25));
  } else if (event == QA_Q3_EV_USE_ITEM3) {
    if (context->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
      client->invulnerability_time = 0;
      context->start_kamikaze(context->ctx, entity);
    }
  } else if (event == QA_Q3_EV_USE_ITEM4) {
    if (context->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
      if (client->portal_id != 0) context->drop_portal_source(context->ctx, entity);
      else context->drop_portal_destination(context->ctx, entity);
    }
  } else if (event == QA_Q3_EV_USE_ITEM5) {
    if (context->product == QA_Q3_BASE_PRODUCT_MISSIONPACK)
      client->invulnerability_time = s32(context->combat->time + 10000);
  }
}

void qa_q3_ta_drop_teleport_objectives(qa_q3_ta_teleport_objectives_context *context, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = entity->client;
  if (client == NULL) qa_range_error("Teleporter requires a client entity");
  double powerup = slot_get(&client->ps.powerups, QA_Q3_PW_REDFLAG) ? QA_Q3_PW_REDFLAG :
      slot_get(&client->ps.powerups, QA_Q3_PW_BLUEFLAG) ? QA_Q3_PW_BLUEFLAG :
      slot_get(&client->ps.powerups, QA_Q3_PW_NEUTRALFLAG) ? QA_Q3_PW_NEUTRALFLAG : QA_Q3_PW_NONE;
  const qa_q3_base_item_definition *flag = powerup == QA_Q3_PW_NONE ? NULL
      : qa_q3_base_find_item_for_powerup(context->product, (qa_q3_powerup)powerup);
  if (flag != NULL) {
    qa_q3_game_entity *dropped = context->drop_item(context->ctx, entity, flag, 0);
    dropped->count = fmax(1, trunc(s32(slot_get(&client->ps.powerups, powerup) - context->combat->time) / 1000));
    slot_set(&client->ps.powerups, powerup, 0);
  }
  if (context->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
      context->combat->game_type == QA_Q3_BASE_GT_HARVESTER &&
      context->generic1(context->ctx, client) > 0) {
    const qa_q3_base_item_definition *cube = qa_q3_base_find_item(context->product,
        client->sess.session_team == QA_Q3_BASE_TEAM_RED ? "Blue Cube" : "Red Cube");
    if (cube != NULL) {
      for (double count = 0; count < context->generic1(context->ctx, client); count = s32(count + 1)) {
        qa_q3_game_entity *dropped = context->drop_item(context->ctx, entity, cube, 0);
        dropped->spawnflags = client->sess.session_team == QA_Q3_BASE_TEAM_RED
            ? QA_Q3_BASE_TEAM_BLUE : QA_Q3_BASE_TEAM_RED;
      }
    }
    context->set_generic1(context->ctx, client, 0);
  }
}

static void use_teleporter(qa_q3_ta_client_events_context *context, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = entity->client;
  if (client == NULL) qa_range_error("Teleporter requires a client entity");
  qa_q3_ta_teleport_objectives_context objectives = {context->product, context->combat,
      context->ctx, context->drop_item, context->generic1, context->set_generic1};
  qa_q3_ta_drop_teleport_objectives(&objectives, entity);
  qa_q3_ta_spawn_pose spawn = context->select_spawn_point(context->ctx, client->ps.origin);
  qa_q3_teleport_player(context->teleport, entity, spawn.origin, spawn.angles);
}
