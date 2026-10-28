/* Port of src/content/q3/team-arena/client-think.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/client_think.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

enum { MASK_PLAYERSOLID = 1 | 0x10000 | 0x2000000, CONTENTS_BODY = 0x2000000,
  CONTENTS_BOTCLIP = 0x400000, EF_FIRING = 0x100,
  REWARD_FLAGS = 0x8 | 0x40 | 0x800 | 0x8000 | 0x10000 | 0x20000 };
#define CONTENTS_TRIGGER_BITS 0x40000000
static double s32(double v) { return (int32_t)v; }
static double imul(double a, double b) { return (int32_t)((int32_t)a * (int32_t)b); }
static double slot_get(qa_q3_game_player_slots_mirror *s, double i) { return qa_q3_game_slots_get_mirror(s, (size_t)i); }

static qa_q3_game_client *client_for(qa_q3_game_entity *entity) {
  if (entity->client == NULL) qa_range_error("ClientThink requires a game client");
  return entity->client;
}
static bool overlap(qa_bounds a, qa_bounds b) {
  return a.min.x <= b.max.x && a.min.y <= b.max.y && a.min.z <= b.max.z &&
         a.max.x >= b.min.x && a.max.y >= b.min.y && a.max.z >= b.min.z;
}
static void copy_command(qa_q3_game_user_command_mirror *dst, qa_q3_base_user_command src) {
  dst->server_time = src.server_time; dst->angles = src.angles; dst->buttons = src.buttons;
  dst->weapon = src.weapon; dst->forwardmove = src.forwardmove; dst->rightmove = src.rightmove;
  dst->upmove = src.upmove;
}
static qa_q3_base_user_command to_command(qa_q3_game_user_command_mirror src) {
  qa_q3_base_user_command out = {src.server_time, src.angles, src.buttons, src.weapon,
      src.forwardmove, src.rightmove, src.upmove};
  return out;
}
/* clientSpeedMultiplier on mirror slots (same rule as client-effects). */
static double mirror_speed_multiplier(qa_q3_game_player_state_mirror *ps) {
  qa_q3_base_product product = ps->product == QA_Q3_GAME_PRODUCT_MISSIONPACK
      ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(product);
  if (product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
      qa_q3_base_item_at(product, slot_get(&ps->stats, schema->persistent_powerup))->tag == QA_Q3_PW_SCOUT)
    return 1.5;
  return slot_get(&ps->powerups, QA_Q3_PW_HASTE) != 0 ? 1.3 : 1;
}

void qa_q3_ta_client_think_runtime_init(qa_q3_ta_client_think_runtime *r, qa_q3_ta_client_think_host host) {
  r->host = host;
}
void qa_q3_ta_client_think_runtime_client_think(qa_q3_ta_client_think_runtime *r,
    double client_num, qa_q3_base_user_command command) {
  qa_q3_game_entity *entity = qa_q3_entity_pool_at(r->host.pool, client_num);
  qa_q3_game_client *client = client_for(entity);
  copy_command(&client->pers.cmd, command);
  client->last_cmd_time = r->host.frame(r->host.ctx).time;
  if (((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) == 0 && !r->host.settings(r->host.ctx).synchronous_clients)
    qa_q3_ta_client_think_runtime_client_think_real(r, entity);
}
void qa_q3_ta_client_think_runtime_run_client(qa_q3_ta_client_think_runtime *r, qa_q3_game_entity *entity) {
  if (((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) == 0 && !r->host.settings(r->host.ctx).synchronous_clients) return;
  client_for(entity)->pers.cmd.server_time = r->host.frame(r->host.ctx).time;
  qa_q3_ta_client_think_runtime_client_think_real(r, entity);
}
void qa_q3_ta_client_think_runtime_client_impacts(qa_q3_ta_client_think_runtime *r,
    qa_q3_game_entity *entity, const qa_actor_id *contacts, size_t contact_count) {
  qa_actor_id self = qa_q3_game_entity_actor(entity).id;
  for (size_t i = 0; i < contact_count; i++) {
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) if (qa_same_actor(contacts[j], contacts[i])) { duplicate = true; break; }
    if (duplicate) continue;
    if (((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) != 0 && entity->touch.fn != NULL)
      r->host.touches.touch(r->host.touches.ctx, self, contacts[i]);
    r->host.touches.touch(r->host.touches.ctx, contacts[i], self);
  }
}
void qa_q3_ta_client_think_runtime_touch_triggers(qa_q3_ta_client_think_runtime *r, qa_q3_game_entity *entity) {
  qa_actor_id self = qa_q3_game_entity_actor(entity).id;
  qa_q3_game_client *client = entity->client;
  if (client == NULL || client->ps.health <= 0) return;
  qa_q3_game_player_state_mirror *ps = &client->ps;
  qa_vec3 range = qa_math_vec3(40, 40, 52);
  qa_bounds area = {qa_sub3(ps->origin, range), qa_add3(ps->origin, range)};
  size_t count = 0;
  const qa_actor_id *touches = r->host.spatial->area_actors(r->host.spatial->ctx, area, QA_Q3_MAX_GENTITIES, &count);
  qa_bounds bounds = {qa_add3(ps->origin, entity->r.mins), qa_add3(ps->origin, entity->r.maxs)};
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = touches[i];
    qa_q3_game_entity *hit = r->host.touches.native(r->host.touches.ctx, actor);
    if (hit != NULL && hit->touch.fn == NULL && entity->touch.fn == NULL) continue;
    if (hit == NULL ? !r->host.touches.is_trigger(r->host.touches.ctx, actor)
                    : ((int32_t)hit->r.contents & CONTENTS_TRIGGER_BITS) == 0) continue;
    if (client->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR &&
        (hit == NULL || (hit->s.e_type != QA_Q3_BASE_ET_TELEPORT_TRIGGER && !r->host.is_door_trigger(r->host.ctx, hit)))) continue;
    if (hit != NULL && hit->s.e_type == QA_Q3_BASE_ET_ITEM) {
      qa_q3_base_trajectory pos = {hit->s.pos.type, hit->s.pos.time, hit->s.pos.duration, hit->s.pos.base, hit->s.pos.delta};
      if (!qa_q3_base_player_touches_item(ps->origin, pos, r->host.frame(r->host.ctx).time)) continue;
    } else if (!r->host.spatial->contact_actor(r->host.spatial->ctx, bounds, actor, false)) continue;
    r->host.touches.touch(r->host.touches.ctx, actor, self);
    if (((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) != 0 && entity->touch.fn != NULL)
      r->host.touches.touch(r->host.touches.ctx, self, actor);
  }
  /* TS jumppad frame reset: no mirror fields. */
}

void qa_q3_ta_client_think_runtime_client_think_real(qa_q3_ta_client_think_runtime *r, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = client_for(entity);
  if (client->pers.connected != QA_Q3_CONN_CONNECTED) return;
  qa_q3_ta_client_think_frame frame = r->host.frame(r->host.ctx);
  qa_q3_ta_client_think_settings settings = r->host.settings(r->host.ctx);
  qa_q3_game_user_command_mirror *command = &client->pers.cmd;
  if (command->server_time > s32(frame.time + 200)) command->server_time = s32(frame.time + 200);
  if (command->server_time < s32(frame.time - 1000)) command->server_time = s32(frame.time - 1000);
  double msec = s32(command->server_time - r->host.command_time(r->host.ctx, client));
  if (msec < 1 && client->sess.spectator_state != QA_Q3_SPEC_FOLLOW) return;
  if (msec > 200) msec = 200;
  if (settings.pmove_msec < 8) r->host.set_pmove_msec(r->host.ctx, 8);
  else if (settings.pmove_msec > 33) r->host.set_pmove_msec(r->host.ctx, 33);
  bool fixed = settings.pmove_fixed || client->pers.pmove_fixed;
  if (fixed) {
    if (settings.pmove_msec != trunc(settings.pmove_msec) || settings.pmove_msec <= 0)
      qa_range_error("Fixed pmove needs a positive cached pmove_msec");
    command->server_time = imul(trunc(s32(command->server_time + settings.pmove_msec - 1) / settings.pmove_msec), settings.pmove_msec);
  }
  if (frame.intermission_time != 0) { r->host.intermission_think(r->host.ctx, client); return; }
  if (client->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR) {
    if (client->sess.spectator_state != QA_Q3_SPEC_SCOREBOARD)
      r->host.spectator_think(r->host.ctx, entity, to_command(*command));
    return;
  }
  if (!r->host.check_inactivity(r->host.ctx, client)) return;
  qa_q3_game_player_state_mirror *ps = &client->ps;
  if (frame.time > client->reward_time) ps->e_flags = (double)((int32_t)ps->e_flags & ~REWARD_FLAGS);
  ps->pm_type = client->noclip ? QA_Q3_PM_NOCLIP : ps->health <= 0 ? QA_Q3_PM_DEAD : QA_Q3_PM_NORMAL;
  double gravity = trunc((float)settings.gravity);
  double speed = trunc((float)settings.speed);
  double multiplier;
  if (!(r->host.speed_multiplier && r->host.speed_multiplier(r->host.ctx, qa_q3_game_entity_actor(entity).id, &multiplier)))
    multiplier = mirror_speed_multiplier(ps);
  if (multiplier != 1) speed = trunc((float)((double)(float)speed * (double)(float)multiplier));
  if (r->host.set_gravity) r->host.set_gravity(r->host.ctx, client, s32(gravity));
  if (r->host.set_speed) r->host.set_speed(r->host.ctx, client, s32(speed));
  double weapon = r->host.weapon(r->host.ctx, client);
  if (weapon == QA_Q3_WP_GRAPPLING_HOOK && client->hook != NULL &&
      ((int32_t)command->buttons & QA_Q3_BUTTON_ATTACK) == 0)
    r->host.free_hook(r->host.ctx, client->hook);
  double old_event_sequence = ps->generic;
  bool gauntlet_hit = (weapon == QA_Q3_WP_GAUNTLET &&
      ((int32_t)command->buttons & QA_Q3_BUTTON_TALK) == 0 &&
      ((int32_t)command->buttons & QA_Q3_BUTTON_ATTACK) != 0 &&
      r->host.weapon_time(r->host.ctx, client) <= 0)
      ? r->host.check_gauntlet_attack(r->host.ctx, entity) : false;
  if (((int32_t)entity->flags & QA_Q3_GAME_FLAG_FORCE_GESTURE) != 0) {
    entity->flags = (double)((int32_t)entity->flags & ~QA_Q3_GAME_FLAG_FORCE_GESTURE);
    command->buttons = (double)((int32_t)command->buttons | QA_Q3_BUTTON_GESTURE);
  }
  qa_q3_ta_expand_invulnerability(r->host.pool, r->host.world, entity);
  double trace_mask = ps->pm_type == QA_Q3_PM_DEAD ? (MASK_PLAYERSOLID & ~CONTENTS_BODY)
      : ((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) != 0 ? (MASK_PLAYERSOLID | CONTENTS_BOTCLIP) : MASK_PLAYERSOLID;
  client->old_origin = ps->origin;
  qa_q3_base_user_command movement_command = to_command(*command);
  if (ps->product == QA_Q3_GAME_PRODUCT_MISSIONPACK && frame.intermission_queued != 0 && settings.single_player) {
    double elapsed = s32(frame.time - frame.intermission_queued);
    if (elapsed >= 1000) {
      movement_command.buttons = 0;
      movement_command.forwardmove = 0;
      movement_command.rightmove = 0;
      movement_command.upmove = 0;
      if (elapsed >= 2000 && elapsed <= 2500) r->host.append_console_command(r->host.ctx, "centerview\n");
      ps->pm_type = QA_Q3_PM_SPINTERMISSION;
    }
  }
  qa_q3_ta_client_movement_options options = {trace_mask, fixed, settings.pmove_msec,
      ((int32_t)settings.dmflags & 32) != 0, gauntlet_hit, settings.debug_move};
  qa_q3_ta_client_movement_result movement =
      r->host.movement.move_client(r->host.movement.ctx, entity, movement_command, options);
  if (ps->generic != old_event_sequence) entity->event_time = frame.time;
  double command_time = r->host.command_time(r->host.ctx, client);
  if (settings.smooth_clients) r->host.snapshot_extrapolate(r->host.ctx, client, entity, command_time);
  else r->host.snapshot(r->host.ctx, client, entity);
  qa_q3_ta_send_pending_predictable_events(r->host.effects, ps, 0);
  if (((int32_t)ps->e_flags & EF_FIRING) == 0) client->fire_held = false;
  entity->r.mins = movement.bounds.min;
  entity->r.maxs = movement.bounds.max;
  entity->waterlevel = movement.waterlevel;
  entity->watertype = movement.watertype;
  entity->r.current_origin = entity->s.pos.base; /* TS withCurrentOrigin view */
  r->host.client_events(r->host.ctx, entity, old_event_sequence);
  r->host.world->link(r->host.world->ctx, entity);
  if (!client->noclip) qa_q3_ta_client_think_runtime_touch_triggers(r, entity);
  entity->r.current_origin = ps->origin;
  r->host.bot_test_aas(r->host.ctx, entity->r.current_origin);
  qa_q3_ta_client_think_runtime_client_impacts(r, entity, movement.contacts, movement.contact_count);
  if (ps->generic != old_event_sequence) entity->event_time = frame.time;
  client->old_buttons = client->buttons;
  client->buttons = command->buttons;
  client->latched_buttons = (double)((int32_t)client->latched_buttons |
      ((int32_t)client->buttons & ~(int32_t)client->old_buttons));
  if (ps->health <= 0) {
    if (frame.time > client->respawn_time) {
      if (settings.force_respawn_seconds > 0 &&
          s32(frame.time - client->respawn_time) > imul(settings.force_respawn_seconds, 1000)) {
        r->host.respawn(r->host.ctx, entity);
        return;
      }
      if (((int32_t)command->buttons & (QA_Q3_BUTTON_ATTACK | QA_Q3_BUTTON_USE_HOLDABLE)) != 0)
        r->host.respawn(r->host.ctx, entity);
    }
    return;
  }
  qa_q3_ta_client_timer_ownership ownership;
  const qa_q3_ta_client_timer_ownership *ownership_ptr = NULL;
  if (r->host.timer_ownership &&
      r->host.timer_ownership(r->host.ctx, qa_q3_game_entity_actor(entity).id, &ownership))
    ownership_ptr = &ownership;
  qa_q3_ta_client_timer_actions(r->host.effects, entity, msec, ownership_ptr);
}

static bool stuck_in_other_client(qa_q3_entity_pool *pool, qa_q3_base_server_world *world, qa_q3_game_entity *entity) {
  qa_bounds zero = {qa_math_vec3(0, 0, 0), qa_math_vec3(0, 0, 0)};
  qa_q3_base_link_state link;
  qa_bounds bounds = (world->link_state && world->link_state(world->ctx, entity->slot, &link)) ? link.absbounds : zero;
  for (int index = 0; index < QA_Q3_MAX_CLIENTS; index++) {
    qa_q3_game_entity *other = qa_q3_entity_pool_at(pool, index);
    if (other == entity || !qa_q3_game_entity_inuse(other) || other->client == NULL ||
        qa_q3_game_entity_health(other) <= 0) continue;
    qa_q3_base_link_state other_link;
    qa_bounds other_bounds = (world->link_state && world->link_state(world->ctx, index, &other_link))
        ? other_link.absbounds : zero;
    if (overlap(bounds, other_bounds)) return true;
  }
  return false;
}
void qa_q3_ta_expand_invulnerability(qa_q3_entity_pool *pool, qa_q3_base_server_world *world, qa_q3_game_entity *entity) {
  qa_q3_game_player_state_mirror *ps = &client_for(entity)->ps;
  if (ps->product == QA_Q3_GAME_PRODUCT_MISSIONPACK && slot_get(&ps->powerups, QA_Q3_PW_INVULNERABILITY) != 0 &&
      ((int32_t)ps->pm_flags & QA_Q3_FLAG_INVULEXPAND) == 0) {
    qa_vec3 old_mins = entity->r.mins, old_maxs = entity->r.maxs;
    entity->r.mins = qa_math_vec3(-42, -42, -42);
    entity->r.maxs = qa_math_vec3(42, 42, 42);
    world->link(world->ctx, entity);
    if (!stuck_in_other_client(pool, world, entity))
      ps->pm_flags = (double)((int32_t)ps->pm_flags | QA_Q3_FLAG_INVULEXPAND);
    entity->r.mins = old_mins;
    entity->r.maxs = old_maxs;
    world->link(world->ctx, entity);
  }
}
