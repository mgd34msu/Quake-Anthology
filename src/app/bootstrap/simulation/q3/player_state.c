/* Port of src/app/bootstrap/simulation/q3/player-state.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/player_state.h"
#include <string.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

static qa_q3_game_player_state_mirror *player_state(qa_q3_game_entity *entity) {
  if (!entity->client) qa_range_error("Q3 player state requires an admitted client");
  return &entity->client->ps;
}
static double source_number(qa_trace_hit hit, qa_q3_base_entity_records *records) {
  if (hit.kind == QA_TRACE_HIT_WORLD) return 1022;
  if (hit.kind == QA_TRACE_HIT_NONE) return 1023;
  qa_q3_game_entity *native = qa_q3_base_entity_records_native_by_actor(records, true, hit.actor);
  return native ? native->slot : 1023;
}
static qa_q3_base_product product_of(qa_q3_game_product product) {
  return product == QA_Q3_GAME_PRODUCT_MISSIONPACK ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
}
static double slot_get(qa_q3_game_player_slots_mirror *slots, double index) {
  return qa_q3_game_slots_get_mirror(slots, (size_t)index);
}

qa_q3_movement_state qa_app_read_q3_movement_state(qa_q3_game_entity *entity, qa_q3_base_entity_records *records,
    qa_actor_id *jump_pad_storage) {
  qa_q3_game_player_state_mirror *ps = player_state(entity);
  qa_q3_game_entity *jump_pad = ps->jumppad_ent == 0 ? NULL : qa_q3_base_entity_records_get(records, ps->jumppad_ent);
  qa_q3_game_body_mirror *body = &entity->binding.body;
  qa_q3_game_entity *world = qa_q3_base_entity_records_get(records, 1022);
  qa_q3_movement_state state; memset(&state, 0, sizeof state);
  state.command_time_milliseconds = ps->command_time; state.movement_type = ps->pm_type;
  state.bob_cycle = ps->bob_cycle; state.movement_flags = ps->pm_flags; state.movement_time_milliseconds = ps->pm_time;
  state.origin = ps->origin; state.velocity = ps->velocity; state.gravity = ps->gravity; state.speed = ps->speed;
  state.delta_angle_words.values[0] = ps->delta_angles_vec.x;
  state.delta_angle_words.values[1] = ps->delta_angles_vec.y;
  state.delta_angle_words.values[2] = ps->delta_angles_vec.z;
  if (!body->has_ground) state.ground.kind = QA_TRACE_HIT_NONE;
  else if (world && qa_q3_game_entity_inuse(world) && qa_same_actor(body->ground, qa_q3_game_entity_actor(world).id)) {
    state.ground.kind = QA_TRACE_HIT_WORLD; state.ground.model = 0;
  } else { state.ground.kind = QA_TRACE_HIT_ACTOR; state.ground.actor = body->ground; }
  state.movement_direction = ps->movement_dir; state.grapple_point = ps->grapple_point; state.flags = ps->e_flags;
  state.view_angles = ps->viewangles; state.view_height = ps->viewheight;
  state.predictable_event_sequence = ps->event_sequence;
  if (jump_pad && qa_q3_game_entity_inuse(jump_pad)) {
    *jump_pad_storage = qa_q3_game_entity_actor(jump_pad).id;
    state.jump_pad = jump_pad_storage;
  }
  state.movement_frame = ps->pmove_framecount; state.jump_pad_frame = ps->jumppad_frame;
  return state;
}
void qa_app_write_q3_movement_state(qa_q3_game_entity *entity, qa_q3_movement_state state, qa_q3_base_entity_records *records) {
  qa_q3_game_player_state_mirror *ps = player_state(entity);
  ps->command_time = state.command_time_milliseconds; ps->pm_type = state.movement_type; ps->bob_cycle = state.bob_cycle;
  ps->pm_flags = state.movement_flags; ps->pm_time = state.movement_time_milliseconds; ps->gravity = state.gravity; ps->speed = state.speed;
  ps->delta_angles_vec = (qa_vec3){state.delta_angle_words.values[0], state.delta_angle_words.values[1], state.delta_angle_words.values[2]};
  ps->ground_entity_num = source_number(state.ground, records); ps->movement_dir = state.movement_direction;
  ps->grapple_point = state.grapple_point; ps->e_flags = state.flags; ps->viewangles = state.view_angles;
  ps->viewheight = state.view_height; ps->pmove_framecount = state.movement_frame; ps->jumppad_frame = state.jump_pad_frame;
  if (!state.jump_pad) ps->jumppad_ent = 0;
  else {
    qa_q3_game_entity *native = qa_q3_base_entity_records_native_by_actor(records, true, *state.jump_pad);
    ps->jumppad_ent = native ? native->slot : 0;
  }
}
void qa_app_write_q3_character_animation(qa_q3_game_entity *entity, qa_actor_animation_state animation) {
  if (!animation.state.kind || strcmp(animation.state.kind, "q3") != 0) return;
  qa_q3_game_player_state_mirror *ps = player_state(entity);
  ps->legs_anim = animation.state.legs; ps->torso_anim = animation.state.torso;
  ps->legs_timer = animation.state.legs_timer_milliseconds; ps->torso_timer = animation.state.torso_timer_milliseconds;
}
qa_movement_environment qa_app_read_q3_movement_environment(qa_q3_game_entity *entity, qa_movement_environment base) {
  qa_q3_game_player_state_mirror *ps = player_state(entity);
  qa_movement_environment environment = base;
  environment.health = ps->health;
  environment.flight = slot_get(&ps->powerups, QA_Q3_PW_FLIGHT) != 0;
  environment.haste = slot_get(&ps->powerups, QA_Q3_PW_HASTE) != 0;
  environment.invulnerable = base.invulnerable
      || (ps->product == QA_Q3_GAME_PRODUCT_MISSIONPACK && slot_get(&ps->powerups, QA_Q3_PW_INVULNERABILITY) != 0);
  return environment;
}
qa_q3_arsenal_runtime_state qa_app_read_q3_arsenal_runtime(qa_q3_game_entity *entity, qa_q3_arsenal_runtime_state previous) {
  qa_q3_game_player_state_mirror *ps = player_state(entity);
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(product_of(ps->product));
  double holdable = slot_get(&ps->stats, schema->holdable_item);
  qa_q3_arsenal_runtime_state runtime = previous;
  runtime.product = qa_q3_game_product_name(ps->product);
  runtime.max_health = slot_get(&ps->stats, schema->max_health);
  runtime.spectator = ps->pm_type == QA_Q3_PM_SPECTATOR || entity->client->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR;
  const qa_q3_base_item_definition *persistent = NULL;
  if (schema->has_persistent_powerup && schema->product == QA_Q3_BASE_PRODUCT_MISSIONPACK)
    persistent = qa_q3_base_item_at(product_of(ps->product), slot_get(&ps->stats, schema->persistent_powerup));
  runtime.persistent_powerup_tag = persistent ? persistent->tag : 0;
  runtime.holdable_item = holdable;
  const qa_q3_base_item_definition *held = qa_q3_base_item_at(product_of(ps->product), holdable);
  runtime.holdable_tag = held ? held->tag : 0;
  runtime.respawned = (((long long)ps->pm_flags) & QA_Q3_FLAG_RESPAWNED) != 0;
  runtime.use_item_held = (((long long)ps->pm_flags) & QA_Q3_FLAG_USE_ITEM_HELD) != 0;
  runtime.event_sequence = ps->event_sequence;
  return runtime;
}
void qa_app_write_q3_arsenal_runtime(qa_q3_game_entity *entity, qa_q3_arsenal_runtime_state runtime) {
  qa_q3_game_player_state_mirror *ps = player_state(entity);
  qa_q3_game_slots_set_mirror(&ps->stats, (size_t)qa_q3_base_stat_schema_for(product_of(ps->product))->holdable_item, runtime.holdable_item);
  ps->pm_flags = (double)((((long long)ps->pm_flags) & ~(QA_Q3_FLAG_RESPAWNED | QA_Q3_FLAG_USE_ITEM_HELD))
      | (runtime.respawned ? QA_Q3_FLAG_RESPAWNED : 0) | (runtime.use_item_held ? QA_Q3_FLAG_USE_ITEM_HELD : 0));
}
