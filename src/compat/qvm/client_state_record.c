/* Port of src/compat/qvm/client-state-record.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_state_record.h"

#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static bool modern(qa_qvm_abi_profile profile) {
  return profile == NULL || strcmp(profile, "q3-modern") == 0;
}

double qa_qvm_snapshot_bytes_for(qa_qvm_abi_profile profile) {
  return modern(profile) ? QA_QVM_SNAPSHOT_BYTES : 52724;
}

qa_qvm_user_command_layout qa_qvm_get_user_command_layout(qa_qvm_abi_profile profile) {
  qa_qvm_user_command_layout modern_layout = {16, 4, 4, 20, 21, 22, 23};
  qa_qvm_user_command_layout legacy_layout = {4, 1, 8, 5, 20, 21, 22};
  return modern(profile) ? modern_layout : legacy_layout;
}

static void require_bytes(const qa_qvmx_view *view, double size) {
  if ((double)view->length < size) qa_range_error("QVM client record requires %d bytes", (int)size);
}

static void check_player_record(const qa_qvmx_view *view, qa_qvm_abi_profile profile) {
  double bytes = modern(profile) ? 468 : 444;
  if ((double)view->length < bytes)
    qa_binary_error("QVM playerState_t", 0, "requires %d bytes, got %zu", (int)bytes, view->length);
}

static void write_vector(qa_qvmx_view *view, int64_t offset, qa_vec3 value) {
  qa_qvmx_view_set_f32(view, offset, qa_float32(value.x));
  qa_qvmx_view_set_f32(view, offset + 4, qa_float32(value.y));
  qa_qvmx_view_set_f32(view, offset + 8, qa_float32(value.z));
}

static void check_slots(const qa_q3_player_state *state) {
  if (state->stats.count != 16 || state->persistent.count != 16 || state->powerups.count != 16 ||
      state->ammo.count != 16)
    qa_range_error("QVM player-state arrays require 16 slots");
}

static void write_slots(qa_qvmx_view *view, int64_t offset, const double *values, size_t count) {
  for (size_t i = 0; i < count; i++) qa_qvmx_view_set_i32(view, offset + 4 * (int64_t)i, qa_int32(values[i]));
}

void qa_qvmx_write_source_player_state(qa_qvmx_view view, const qa_q3_player_state *state,
                                       qa_qvm_abi_profile profile) {
  check_player_record(&view, profile);
  check_slots(state);
  qa_qvmx_view_set_i32(&view, 0, qa_int32(state->command_time_milliseconds));
  qa_qvmx_view_set_i32(&view, 4, qa_int32(state->movement_type));
  qa_qvmx_view_set_i32(&view, 8, qa_int32(state->bob_cycle));
  qa_qvmx_view_set_i32(&view, 12, qa_int32(state->movement_flags));
  qa_qvmx_view_set_i32(&view, 16, qa_int32(state->movement_time_milliseconds));
  write_vector(&view, 20, state->origin);
  write_vector(&view, 32, state->velocity);
  qa_qvmx_view_set_i32(&view, 44, qa_int32(state->weapon_time_milliseconds));
  qa_qvmx_view_set_i32(&view, 48, qa_int32(state->gravity));
  qa_qvmx_view_set_i32(&view, 52, qa_int32(state->speed));
  qa_qvmx_view_set_i32(&view, 56, qa_int32(state->delta_angle_words.values[0]));
  qa_qvmx_view_set_i32(&view, 60, qa_int32(state->delta_angle_words.values[1]));
  qa_qvmx_view_set_i32(&view, 64, qa_int32(state->delta_angle_words.values[2]));
  qa_qvmx_view_set_i32(&view, 68, qa_int32(state->ground_entity_number));
  qa_qvmx_view_set_i32(&view, 72, qa_int32(state->legs_timer_milliseconds));
  qa_qvmx_view_set_i32(&view, 76, qa_int32(state->legs_animation));
  qa_qvmx_view_set_i32(&view, 80, qa_int32(state->torso_timer_milliseconds));
  qa_qvmx_view_set_i32(&view, 84, qa_int32(state->torso_animation));
  qa_qvmx_view_set_i32(&view, 88, qa_int32(state->movement_direction));
  write_vector(&view, 92, state->grapple_point);
  qa_qvmx_view_set_i32(&view, 104, qa_int32(state->flags));
  qa_qvmx_view_set_i32(&view, 108, qa_int32(state->event_sequence));
  write_slots(&view, 112, state->events, 2);
  write_slots(&view, 120, state->event_parameters, 2);
  qa_qvmx_view_set_i32(&view, 128, qa_int32(state->external_event));
  qa_qvmx_view_set_i32(&view, 132, qa_int32(state->external_event_parameter));
  qa_qvmx_view_set_i32(&view, 136, qa_int32(state->external_event_time_milliseconds));
  qa_qvmx_view_set_i32(&view, 140, qa_int32(state->client_number));
  qa_qvmx_view_set_i32(&view, 144, qa_int32(state->weapon));
  qa_qvmx_view_set_i32(&view, 148, qa_int32(state->weapon_state));
  write_vector(&view, 152, state->view_angles);
  qa_qvmx_view_set_i32(&view, 164, qa_int32(state->view_height));
  qa_qvmx_view_set_i32(&view, 168, qa_int32(state->damage_event));
  qa_qvmx_view_set_i32(&view, 172, qa_int32(state->damage_yaw));
  qa_qvmx_view_set_i32(&view, 176, qa_int32(state->damage_pitch));
  qa_qvmx_view_set_i32(&view, 180, qa_int32(state->damage_count));
  write_slots(&view, 184, state->stats.values, state->stats.count);
  write_slots(&view, 248, state->persistent.values, state->persistent.count);
  write_slots(&view, 312, state->powerups.values, state->powerups.count);
  write_slots(&view, 376, state->ammo.values, state->ammo.count);
  if (!modern(profile)) {
    qa_qvmx_view_set_i32(&view, 440, qa_int32(state->ping_milliseconds));
    return;
  }
  qa_qvmx_view_set_i32(&view, 440, qa_int32(state->generic1));
  qa_qvmx_view_set_i32(&view, 444, qa_int32(state->loop_sound));
  qa_qvmx_view_set_i32(&view, 448, qa_int32(state->jump_pad_entity));
  qa_qvmx_view_set_i32(&view, 452, qa_int32(state->ping_milliseconds));
  qa_qvmx_view_set_i32(&view, 456, qa_int32(state->movement_frame_count));
  qa_qvmx_view_set_i32(&view, 460, qa_int32(state->jump_pad_frame));
  qa_qvmx_view_set_i32(&view, 464, qa_int32(state->entity_event_sequence));
}

static void write_player_state(qa_qvmx_view view, const qa_q3_player_state *state,
                               qa_qvm_abi_profile profile, const char *persistent_write, bool lenient);
void qa_qvmx_write_player_state(qa_qvmx_view view, const qa_q3_player_state *state,
                                qa_qvm_abi_profile profile, const char *persistent_write) {
  write_player_state(view, state, profile, persistent_write, false);
}
static void write_player_state(qa_qvmx_view view, const qa_q3_player_state *state,
                               qa_qvm_abi_profile profile, const char *persistent_write, bool lenient) {
  check_player_record(&view, profile);
  check_slots(state);
  double persistent[16];
  if (modern(profile)) {
    memcpy(persistent, state->persistent.values, sizeof persistent);
  } else if (persistent_write != NULL && strcmp(persistent_write, "preserve-private") == 0) {
    for (int i = 0; i < 16; i++) persistent[i] = qa_qvmx_view_i32(&view, 248 + i * 4);
  } else {
    for (int i = 0; i < 16; i++) persistent[i] = 0;
  }
  if (!modern(profile)) {
    const int indices[8] = {0, 1, 2, 3, 4, 8, 9, 10};
    for (int i = 0; i < 8; i++) persistent[indices[i]] = state->persistent.values[indices[i]];
    persistent[7] = state->persistent.values[6];
    persistent[11] = state->persistent.values[13];
  }
  qa_q3_player_state translated = *state;
  qa_double_view persistent_view = {persistent, 16};
  translated.persistent = persistent_view;
  translated.events[0] = qa_qvmx_event_ex(state->events[0], profile, true, lenient);
  translated.events[1] = qa_qvmx_event_ex(state->events[1], profile, true, lenient);
  translated.external_event = qa_qvmx_event_ex(state->external_event, profile, true, lenient);
  /* qvmPowerups validation runs after the event mappings, as in the
   * TS object literal passed to writeSourceQvmPlayerState. */
  double powerups[16];
  if (!modern(profile) && lenient) {
    for (size_t i = 0; i < state->powerups.count && i < 16; i++) powerups[i] = i < 9 ? state->powerups.values[i] : 0;
    translated.powerups.values = powerups;
  } else if (!modern(profile)) {
    for (size_t i = 9; i < state->powerups.count; i++)
      if (state->powerups.values[i] != 0)
        qa_range_error("Legacy ball or private powerup has no modern presentation mapping");
  }
  qa_qvmx_write_source_player_state(view, &translated, profile);
}

void qa_qvm_write_source_snapshot(qa_qvmx_memory *memory, qa_qvmx_view view,
                                  const qa_qvm_source_snapshot *snapshot,
                                  qa_qvm_abi_profile profile) {
  require_bytes(&view, qa_qvm_snapshot_bytes_for(profile));
  if (snapshot->area_mask_length != 32 || snapshot->entity_count > 256)
    qa_range_error("Invalid source snapshot_t extent");
  int64_t ps_bytes = modern(profile) ? 468 : 444;
  int64_t entity_bytes = modern(profile) ? 208 : 204;
  int64_t offset = view.bytes - memory->bytes;
  qa_qvmx_view_set_i32(&view, 0, qa_int32(snapshot->flags));
  qa_qvmx_view_set_i32(&view, 4, qa_int32(snapshot->player_state.ping_milliseconds));
  qa_qvmx_view_set_i32(&view, 8, qa_int32(snapshot->server_time));
  qa_qvmx_memory_write_bytes(memory, (double)(offset + 12), snapshot->area_mask, 32);
  qa_qvmx_view player = qa_qvmx_memory_data_view(memory, (double)(offset + 44), (double)ps_bytes);
  qa_qvmx_write_source_player_state(player, &snapshot->player_state, profile);
  qa_qvmx_view_set_i32(&view, 44 + ps_bytes, (int32_t)snapshot->entity_count);
  for (size_t i = 0; i < snapshot->entity_count; i++) {
    qa_qvmx_view entity = qa_qvmx_memory_data_view(
        memory, (double)(offset + 48 + ps_bytes + (int64_t)i * entity_bytes), (double)entity_bytes);
    qa_qvm_write_source_entity_state(entity, &snapshot->entities[i], profile);
  }
  qa_qvmx_view_set_i32(&view, (int64_t)qa_qvm_snapshot_bytes_for(profile) - 4,
                       qa_int32(snapshot->server_command_sequence));
}

static void write_game_state(qa_qvmx_memory *memory, qa_qvmx_view view,
                             const qa_qvmx_source_game_state_record *state,
                             qa_qvm_abi_profile profile, bool has_profile) {
  require_bytes(&view, QA_QVM_GAME_STATE_BYTES);
  if (state->string_offset_count != 1024 || state->string_data_length != 16000)
    qa_range_error("Invalid source gameState_t extent");
  for (int index = 0; index < 1024; index++) {
    int32_t source = !has_profile ? index
                                  : (!modern(profile) && index >= 16 && index <= 26
                                         ? -1
                                         : qa_qvmx_configstring(index, profile));
    qa_qvmx_view_set_i32(&view, index * 4, source < 0 ? 0 : state->string_offsets[source]);
  }
  qa_qvmx_memory_write_bytes(memory, (double)(view.bytes - memory->bytes + 4096),
                             state->string_data, state->string_data_length);
  qa_qvmx_view_set_i32(&view, 20096, qa_int32(state->data_count));
}

void qa_qvm_write_game_state(qa_qvmx_memory *memory, qa_qvmx_view view,
                             const qa_qvmx_source_game_state_record *state,
                             qa_qvm_abi_profile profile) {
  write_game_state(memory, view, state, profile == NULL ? "q3-modern" : profile, true);
}

void qa_qvm_write_source_game_state(qa_qvmx_memory *memory, qa_qvmx_view view,
                                    const qa_qvmx_source_game_state_record *state) {
  write_game_state(memory, view, state, NULL, false);
}

void qa_qvm_write_snapshot(qa_qvmx_memory *memory, qa_qvmx_view view,
                           const qa_qvmx_snapshot *snapshot, double ping,
                           qa_qvm_abi_profile profile) {
  require_bytes(&view, qa_qvm_snapshot_bytes_for(profile));
  int64_t ps_bytes = modern(profile) ? 468 : 444;
  int64_t entity_bytes = modern(profile) ? 208 : 204;
  if (snapshot->area_mask_length != 32) qa_range_error("Invalid source snapshot_t extent");
  /* CL_GetSnapshot truncates to MAX_ENTITIES_IN_SNAPSHOT */
  const size_t entity_count = snapshot->entity_count > 256 ? 256 : snapshot->entity_count;
  const qa_qvmx_player_state_fields *ps = &snapshot->player_state;
  int64_t offset = view.bytes - memory->bytes;
  qa_qvmx_view_set_i32(&view, 0, qa_int32(snapshot->flags));
  qa_qvmx_view_set_i32(&view, 4, qa_int32(ping));
  qa_qvmx_view_set_i32(&view, 8, qa_int32(snapshot->server_time));
  qa_qvmx_memory_write_bytes(memory, (double)(offset + 12), snapshot->area_mask, 32);
  qa_q3_player_state state;
  memset(&state, 0, sizeof state);
  state.command_time_milliseconds = ps->command_time;
  state.movement_type = ps->pm_type;
  state.bob_cycle = ps->bob_cycle;
  state.movement_flags = ps->pm_flags;
  state.movement_time_milliseconds = ps->pm_time;
  state.origin = ps->origin;
  state.velocity = ps->velocity;
  state.weapon_time_milliseconds = ps->weapon_time;
  state.gravity = ps->gravity;
  state.speed = ps->speed;
  state.delta_angle_words.values[0] = ps->delta_angles.x;
  state.delta_angle_words.values[1] = ps->delta_angles.y;
  state.delta_angle_words.values[2] = ps->delta_angles.z;
  state.ground_entity_number = ps->ground_entity_num;
  state.legs_timer_milliseconds = ps->legs_timer;
  state.legs_animation = ps->legs_anim;
  state.torso_timer_milliseconds = ps->torso_timer;
  state.torso_animation = ps->torso_anim;
  state.movement_direction = ps->movement_dir;
  state.grapple_point = ps->grapple_point;
  state.flags = ps->e_flags;
  state.event_sequence = ps->event_sequence;
  state.events[0] = ps->events[0];
  state.events[1] = ps->events[1];
  state.event_parameters[0] = ps->event_parms[0];
  state.event_parameters[1] = ps->event_parms[1];
  state.external_event = ps->external_event;
  state.external_event_parameter = ps->external_event_parm;
  state.external_event_time_milliseconds = ps->external_event_time;
  state.client_number = ps->client_num;
  state.weapon = ps->weapon;
  state.weapon_state = ps->weapon_state;
  state.view_angles = ps->viewangles;
  state.view_height = ps->viewheight;
  state.damage_event = ps->damage_event;
  state.damage_yaw = ps->damage_yaw;
  state.damage_pitch = ps->damage_pitch;
  state.damage_count = ps->damage_count;
  qa_double_view stats = {ps->stats, 16}, persistent = {ps->persistant, 16};
  qa_double_view powerups = {ps->powerups, 16}, ammo = {ps->ammo, 16};
  state.stats = stats;
  state.persistent = persistent;
  state.powerups = powerups;
  state.ammo = ammo;
  state.generic1 = ps->generic1;
  state.loop_sound = ps->loop_sound;
  state.jump_pad_entity = ps->jumppad_ent;
  state.ping_milliseconds = ps->ping;
  state.movement_frame_count = ps->pmove_framecount;
  state.jump_pad_frame = ps->jumppad_frame;
  state.entity_event_sequence = ps->entity_event_sequence;
  qa_qvmx_view player = qa_qvmx_memory_data_view(memory, (double)(offset + 44), (double)ps_bytes);
  write_player_state(player, &state, profile, NULL, true);
  qa_qvmx_view_set_i32(&view, 44 + ps_bytes, (int32_t)entity_count);
  for (size_t i = 0; i < entity_count; i++) {
    qa_qvmx_view entity = qa_qvmx_memory_data_view(
        memory, (double)(offset + 48 + ps_bytes + (int64_t)i * entity_bytes), (double)entity_bytes);
    qa_qvm_write_entity_state_unmapped(entity, &snapshot->entities[i], profile, true);
  }
  qa_qvmx_view_set_i32(&view, (int64_t)qa_qvm_snapshot_bytes_for(profile) - 4,
                       qa_int32(snapshot->server_command_number));
}

void qa_qvm_write_user_command(qa_qvmx_view view, const qa_qvmx_wire_user_command *command,
                               qa_qvm_abi_profile profile, const char *mode) {
  require_bytes(&view, QA_QVM_USER_COMMAND_BYTES);
  qa_qvm_user_command_layout layout = qa_qvm_get_user_command_layout(profile);
  qa_qvmx_view_set_i32(&view, 0, qa_int32(command->server_time));
  if (layout.buttons_bytes == 1) {
    int kept = mode != NULL && strcmp(mode, "update") == 0
                   ? qa_qvmx_view_u8(&view, (int64_t)layout.buttons_offset) & 96
                   : 0;
    int buttons = qa_int32(command->buttons);
    qa_qvmx_view_set_u8(&view, (int64_t)layout.buttons_offset,
                        (uint8_t)(kept | (buttons & 31) | ((buttons & 2048) != 0 ? 128 : 0)));
  } else {
    qa_qvmx_view_set_i32(&view, (int64_t)layout.buttons_offset, qa_int32(command->buttons));
  }
  for (int i = 0; i < 3; i++)
    qa_qvmx_view_set_i32(&view, (int64_t)layout.angles + i * 4, qa_int32(command->angles[i]));
  qa_qvmx_view_set_u8(&view, (int64_t)layout.weapon, (uint8_t)qa_uint32(command->weapon));
  qa_qvmx_view_set_u8(&view, (int64_t)layout.forward, (uint8_t)qa_int32(command->forwardmove));
  qa_qvmx_view_set_u8(&view, (int64_t)layout.right, (uint8_t)qa_int32(command->rightmove));
  qa_qvmx_view_set_u8(&view, (int64_t)layout.up, (uint8_t)qa_int32(command->upmove));
}

qa_qvmx_wire_user_command qa_qvm_read_user_command(qa_qvmx_view view, qa_qvm_abi_profile profile) {
  require_bytes(&view, QA_QVM_USER_COMMAND_BYTES);
  qa_qvm_user_command_layout layout = qa_qvm_get_user_command_layout(profile);
  int32_t buttons = layout.buttons_bytes == 1
                        ? qa_qvmx_view_u8(&view, (int64_t)layout.buttons_offset)
                        : qa_qvmx_view_i32(&view, (int64_t)layout.buttons_offset);
  qa_qvmx_wire_user_command command;
  command.server_time = qa_qvmx_view_i32(&view, 0);
  command.angles[0] = qa_qvmx_view_i32(&view, (int64_t)layout.angles);
  command.angles[1] = qa_qvmx_view_i32(&view, (int64_t)layout.angles + 4);
  command.angles[2] = qa_qvmx_view_i32(&view, (int64_t)layout.angles + 8);
  command.buttons = layout.buttons_bytes == 1
                        ? (buttons & 31) | ((buttons & 128) == 0 ? 0 : 2048)
                        : buttons;
  command.weapon = qa_qvmx_view_u8(&view, (int64_t)layout.weapon);
  command.forwardmove = (int8_t)qa_qvmx_view_u8(&view, (int64_t)layout.forward);
  command.rightmove = (int8_t)qa_qvmx_view_u8(&view, (int64_t)layout.right);
  command.upmove = (int8_t)qa_qvmx_view_u8(&view, (int64_t)layout.up);
  return command;
}
