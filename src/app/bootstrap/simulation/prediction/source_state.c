/* Port of src/app/bootstrap/simulation/prediction/source-state.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/prediction/source_state.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/prediction/step.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

qa_trace_hit qa_app_prediction_source_hit(double number, qa_app_prediction_source_entities entities) {
  qa_trace_hit hit; memset(&hit, 0, sizeof hit);
  if (number == 1022) { hit.kind = QA_TRACE_HIT_WORLD; hit.model = 0; return hit; }
  if (number == 1023) { hit.kind = QA_TRACE_HIT_NONE; return hit; }
  qa_actor_id actor;
  if (entities.actor_at && entities.actor_at(entities.ctx, number, &actor)) { hit.kind = QA_TRACE_HIT_ACTOR; hit.actor = actor; }
  else hit.kind = QA_TRACE_HIT_NONE;
  return hit;
}
double qa_app_prediction_source_number(qa_trace_hit hit, qa_app_prediction_source_entities entities) {
  if (hit.kind == QA_TRACE_HIT_WORLD) return 1022;
  if (hit.kind == QA_TRACE_HIT_NONE) return 1023;
  double number;
  return entities.number_of && entities.number_of(entities.ctx, hit.actor, &number) ? number : 1023;
}
static double stat_at(qa_double_view stats, size_t index) { return index < stats.count ? stats.values[index] : 0; }

qa_app_movement_prediction_snapshot qa_app_q2_prediction_snapshot(qa_app_movement_prediction_snapshot base,
    qa_q2_player_state classic, qa_q2_rerelease_player_state rerelease, bool is_rerelease,
    double sequence, double command_time_milliseconds) {
  qa_app_movement_prediction_snapshot copy = qa_app_copy_prediction_snapshot(base);
  qa_q2_player_view view = is_rerelease ? rerelease.view : classic.view;
  qa_app_movement_prediction_snapshot next = copy;
  next.sequence = sequence; next.command_time_milliseconds = command_time_milliseconds;
  if (is_rerelease) { next.state.kind = QA_MOVEMENT_STATE_Q2_RERELEASE; next.state.as.q2_rerelease = rerelease.movement; }
  else { next.state.kind = QA_MOVEMENT_STATE_Q2; next.state.as.q2 = classic.movement; }
  next.view_angles = view.view_angles; next.view_offset = view.view_offset;
  next.view_height = is_rerelease ? rerelease.movement.view_height : view.view_offset.z;
  next.environment.health = stat_at(view.stats, 1);
  qa_app_movement_prediction_snapshot result = qa_app_copy_prediction_snapshot(next);
  qa_app_movement_prediction_snapshot_destroy(&copy);
  return result;
}

static double slot_get(const qa_q3_base_player_state_slots *slots, double index) {
  return qa_q3_base_player_state_slots_get(slots, index);
}

qa_app_movement_prediction_snapshot qa_app_read_prediction_source_state(qa_app_movement_prediction_snapshot base,
    const qa_q3_base_player_state_record *ps, qa_app_prediction_source_entities entities) {
  qa_app_movement_prediction_snapshot source = qa_app_copy_prediction_snapshot(base);
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(ps->state_product);
  qa_q3_movement_state q3; memset(&q3, 0, sizeof q3);
  q3.command_time_milliseconds = ps->command_time; q3.movement_type = ps->pm_type;
  q3.bob_cycle = ps->bob_cycle; q3.movement_flags = ps->pm_flags; q3.movement_time_milliseconds = ps->pm_time;
  q3.origin = qa_q3_base_player_state_record_origin(ps); q3.velocity = qa_q3_base_player_state_record_velocity(ps);
  q3.gravity = ps->gravity; q3.speed = ps->speed;
  q3.delta_angle_words.values[0] = ps->delta_angles.x; q3.delta_angle_words.values[1] = ps->delta_angles.y;
  q3.delta_angle_words.values[2] = ps->delta_angles.z;
  q3.ground = qa_app_prediction_source_hit(ps->ground_entity_num, entities);
  q3.movement_direction = ps->movement_dir; q3.grapple_point = ps->grapple_point; q3.flags = ps->e_flags;
  q3.view_angles = ps->viewangles; q3.view_height = ps->viewheight;
  q3.predictable_event_sequence = ps->event_sequence;
  qa_actor_id jump_pad_storage; const qa_actor_id *jump_pad = NULL;
  if (ps->jumppad_ent != 0 && entities.actor_at && entities.actor_at(entities.ctx, ps->jumppad_ent, &jump_pad_storage)) jump_pad = &jump_pad_storage;
  (void)jump_pad; /* landed q3 state stores jump_pad pointer; set below via static slot */
  q3.movement_frame = ps->pmove_framecount; q3.jump_pad_frame = ps->jumppad_frame;
  bool arsenal_q3 = source.arsenal.state.kind && strcmp(source.arsenal.state.kind, "q3") == 0;
  if (arsenal_q3) {
    const qa_q3_weapon_item *active = qa_q3_get_weapon_item(ps->weapon);
    source.arsenal.active_weapon = active ? active->item : NULL;
    source.arsenal.state.kind = "q3"; source.arsenal.state.source_weapon = ps->weapon;
    source.arsenal.state.state = ps->weapon_state; source.arsenal.state.time_milliseconds = ps->weapon_time;
    free((void *)source.arsenal.ammo);
    size_t count = 0;
    qa_inventory_entry *ammo = malloc(26 * sizeof *ammo);
    if (!ammo) qa_range_error("Out of memory");
    for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) {
      const qa_q3_weapon_item *item = &qa_q3_weapon_items[i];
      if (ps->state_product != QA_Q3_BASE_PRODUCT_MISSIONPACK && item->weapon >= 11) continue;
      memset(&ammo[count], 0, sizeof ammo[count]);
      ammo[count].item = item->item;
      ammo[count].count = (((long long)slot_get(&ps->stats, schema->weapons) & (1LL << (long long)item->weapon)) != 0) ? 1 : 0;
      ammo[count].capacity = 1; count++;
      if (item->ammo) {
        memset(&ammo[count], 0, sizeof ammo[count]);
        ammo[count].item = item->ammo; ammo[count].count = slot_get(&ps->ammo, item->weapon); ammo[count].capacity = 200; count++;
      }
    }
    source.arsenal.ammo = ammo; source.arsenal.ammo_count = count;
  }
  double holdable = slot_get(&ps->stats, schema->holdable_item);
  if (source.state.kind == QA_MOVEMENT_STATE_Q3) { source.state.as.q3 = q3; }
  else if (source.state.kind == QA_MOVEMENT_STATE_Q2) {
    qa_vec3 origin = q3.origin, velocity = q3.velocity;
    source.state.as.q2.origin_eighths.values[0] = (double)(long long)(origin.x * 8);
    source.state.as.q2.origin_eighths.values[1] = (double)(long long)(origin.y * 8);
    source.state.as.q2.origin_eighths.values[2] = (double)(long long)(origin.z * 8);
    source.state.as.q2.velocity_eighths.values[0] = (double)(long long)(velocity.x * 8);
    source.state.as.q2.velocity_eighths.values[1] = (double)(long long)(velocity.y * 8);
    source.state.as.q2.velocity_eighths.values[2] = (double)(long long)(velocity.z * 8);
  } else if (source.state.kind == QA_MOVEMENT_STATE_Q1) { source.state.as.q1.origin = q3.origin; source.state.as.q1.velocity = q3.velocity; }
  else if (source.state.kind == QA_MOVEMENT_STATE_QW) { source.state.as.qw.origin = q3.origin; source.state.as.qw.velocity = q3.velocity; }
  else if (source.state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) { source.state.as.q2_rerelease.origin = q3.origin; source.state.as.q2_rerelease.velocity = q3.velocity; }
  source.command_time_milliseconds = ps->command_time;
  if (source.animation.state.kind && strcmp(source.animation.state.kind, "q3") == 0) {
    source.animation.state.legs = ps->legs_anim; source.animation.state.torso = ps->torso_anim;
    source.animation.state.legs_timer_milliseconds = ps->legs_timer; source.animation.state.torso_timer_milliseconds = ps->torso_timer;
  }
  source.view_angles = ps->viewangles; source.view_height = ps->viewheight;
  if (source.state.kind == QA_MOVEMENT_STATE_Q3) source.environment.gravity_multiplier = 1;
  source.environment.health = qa_q3_base_player_state_record_health(ps);
  source.environment.flight = slot_get(&ps->powerups, QA_Q3_PW_FLIGHT) != 0;
  source.environment.haste = slot_get(&ps->powerups, QA_Q3_PW_HASTE) != 0;
  source.environment.invulnerable = ps->state_product == QA_Q3_BASE_PRODUCT_MISSIONPACK
      && slot_get(&ps->powerups, QA_Q3_PW_INVULNERABILITY) != 0;
  if (!arsenal_q3) { source.has_q3_arsenal = false; }
  else {
    qa_q3_arsenal_runtime_state runtime; memset(&runtime, 0, sizeof runtime);
    runtime.product = qa_q3_base_product_name(ps->state_product);
    runtime.max_health = slot_get(&ps->stats, schema->max_health);
    runtime.spectator = ps->pm_type == 1;
    const qa_q3_base_item_definition *persistent = NULL, *held = NULL;
    if (schema->has_persistent_powerup && schema->product == QA_Q3_BASE_PRODUCT_MISSIONPACK)
      persistent = qa_q3_base_item_at(ps->state_product, slot_get(&ps->stats, schema->persistent_powerup));
    held = qa_q3_base_item_at(ps->state_product, holdable);
    runtime.persistent_powerup_tag = persistent ? persistent->tag : 0;
    runtime.holdable_item = holdable; runtime.holdable_tag = held ? held->tag : 0;
    runtime.respawned = (((long long)ps->pm_flags) & QA_Q3_FLAG_RESPAWNED) != 0;
    runtime.use_item_held = (((long long)ps->pm_flags) & QA_Q3_FLAG_USE_ITEM_HELD) != 0;
    runtime.event_sequence = ps->event_sequence;
    runtime.fractional_milliseconds = source.has_q3_arsenal ? source.q3_arsenal.fractional_milliseconds : 0;
    runtime.external_slot = source.has_q3_arsenal ? source.q3_arsenal.external_slot : QA_Q3_WEAPON_SLOT_ACTIVE;
    runtime.has_requested_weapon = source.has_q3_arsenal && source.q3_arsenal.has_requested_weapon;
    runtime.requested_weapon = source.has_q3_arsenal ? source.q3_arsenal.requested_weapon : 0;
    source.q3_arsenal = runtime; source.has_q3_arsenal = true;
  }
  return source;
}

void qa_app_write_prediction_source_state(qa_q3_base_player_state_record *ps,
    qa_app_movement_prediction_snapshot output, qa_app_prediction_source_entities entities) {
  qa_movement_state state = output.state;
  ps->command_time = output.command_time_milliseconds;
  qa_vec3 origin, velocity;
  if (state.kind == QA_MOVEMENT_STATE_Q2) {
    origin = (qa_vec3){state.as.q2.origin_eighths.values[0] / 8, state.as.q2.origin_eighths.values[1] / 8, state.as.q2.origin_eighths.values[2] / 8};
    velocity = (qa_vec3){state.as.q2.velocity_eighths.values[0] / 8, state.as.q2.velocity_eighths.values[1] / 8, state.as.q2.velocity_eighths.values[2] / 8};
  } else if (state.kind == QA_MOVEMENT_STATE_Q1) { origin = state.as.q1.origin; velocity = state.as.q1.velocity; }
  else if (state.kind == QA_MOVEMENT_STATE_QW) { origin = state.as.qw.origin; velocity = state.as.qw.velocity; }
  else if (state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) { origin = state.as.q2_rerelease.origin; velocity = state.as.q2_rerelease.velocity; }
  else { origin = state.as.q3.origin; velocity = state.as.q3.velocity; }
  qa_q3_base_player_state_record_set_origin(ps, origin);
  qa_q3_base_player_state_record_set_velocity(ps, velocity);
  ps->viewangles = output.view_angles; ps->viewheight = output.view_height;
  if (output.has_contact) ps->ground_entity_num = qa_app_prediction_source_number(output.contact_ground, entities);
  if (state.kind == QA_MOVEMENT_STATE_Q3) {
    ps->command_time = state.as.q3.command_time_milliseconds; ps->pm_type = state.as.q3.movement_type;
    ps->pm_flags = state.as.q3.movement_flags; ps->pm_time = state.as.q3.movement_time_milliseconds;
    ps->bob_cycle = state.as.q3.bob_cycle;
    ps->delta_angles = (qa_vec3){state.as.q3.delta_angle_words.values[0], state.as.q3.delta_angle_words.values[1], state.as.q3.delta_angle_words.values[2]};
    ps->ground_entity_num = qa_app_prediction_source_number(state.as.q3.ground, entities);
    ps->movement_dir = state.as.q3.movement_direction; ps->e_flags = state.as.q3.flags;
    ps->pmove_framecount = state.as.q3.movement_frame; ps->jumppad_frame = state.as.q3.jump_pad_frame;
    double pad = 0;
    ps->jumppad_ent = state.as.q3.jump_pad && entities.number_of && entities.number_of(entities.ctx, *state.as.q3.jump_pad, &pad) ? pad : 0;
  }
  if (output.arsenal.state.kind && strcmp(output.arsenal.state.kind, "q3") == 0) {
    ps->weapon = output.arsenal.state.source_weapon; ps->weapon_state = output.arsenal.state.state;
    ps->weapon_time = output.arsenal.state.time_milliseconds;
    long long owned = 0;
    for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) {
      const qa_q3_weapon_item *item = &qa_q3_weapon_items[i];
      double item_count = 0, ammo_count = 0;
      for (size_t j = 0; j < output.arsenal.ammo_count; j++) {
        if (output.arsenal.ammo[j].item == item->item) item_count = output.arsenal.ammo[j].count;
        if (item->ammo && output.arsenal.ammo[j].item == item->ammo) ammo_count = output.arsenal.ammo[j].count;
      }
      if (item_count > 0) owned |= 1LL << (long long)item->weapon;
      if (item->ammo) qa_q3_base_player_state_slots_set(&ps->ammo, item->weapon, ammo_count);
    }
    const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(ps->state_product);
    qa_q3_base_player_state_slots_set(&ps->stats, schema->weapons, (double)owned);
    if (output.has_q3_arsenal) {
      qa_q3_base_player_state_slots_set(&ps->stats, schema->holdable_item, output.q3_arsenal.holdable_item);
      ps->pm_flags = (double)((((long long)ps->pm_flags) & ~(QA_Q3_FLAG_RESPAWNED | QA_Q3_FLAG_USE_ITEM_HELD))
          | (output.q3_arsenal.respawned ? QA_Q3_FLAG_RESPAWNED : 0) | (output.q3_arsenal.use_item_held ? QA_Q3_FLAG_USE_ITEM_HELD : 0));
    }
  }
  if (output.animation.state.kind && strcmp(output.animation.state.kind, "q3") == 0) {
    ps->legs_anim = output.animation.state.legs; ps->torso_anim = output.animation.state.torso;
    ps->legs_timer = output.animation.state.legs_timer_milliseconds; ps->torso_timer = output.animation.state.torso_timer_milliseconds;
  }
}
