/* See include/qa/compat/qvm/player_record.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/player_record.h"

#include <string.h>

#include "qa/compat/qvm/legacy_presentation.h"
#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

static bool is_modern(qa_qvm_abi_profile profile) {
  return profile == NULL || strcmp(profile, "q3-modern") == 0;
}

int qa_qvm_player_state_bytes(qa_qvm_abi_profile profile) {
  return is_modern(profile) ? 468 : 444;
}

static void check_record(const qa_qvm_writable_view *view, qa_qvm_abi_profile profile) {
  int bytes = qa_qvm_player_state_bytes(profile);
  if (view->length < bytes)
    qa_binary_error("QVM playerState_t", 0, "requires %d bytes, got %lld", bytes, (long long)view->length);
}

static int32_t get_i32(const qa_qvm_writable_view *view, int64_t offset) {
  return qa_qvm_writable_view_get_int32(view, offset, true);
}

static qa_vec3 read_vector(const qa_qvm_writable_view *view, int64_t offset) {
  qa_vec3 result = {(double)qa_qvm_writable_view_get_float32(view, offset, true),
                    (double)qa_qvm_writable_view_get_float32(view, offset + 4, true),
                    (double)qa_qvm_writable_view_get_float32(view, offset + 8, true)};
  return result;
}

static void write_vector(qa_qvm_writable_view *view, int64_t offset, qa_vec3 value) {
  qa_qvm_writable_view_set_float32(view, offset, value.x, true);
  qa_qvm_writable_view_set_float32(view, offset + 4, value.y, true);
  qa_qvm_writable_view_set_float32(view, offset + 8, value.z, true);
}

static void read_slots(const qa_qvm_writable_view *view, int64_t offset, double out[16]) {
  for (int i = 0; i < 16; i++) out[i] = (double)get_i32(view, offset + i * 4);
}

static void write_slots(qa_qvm_writable_view *view, int64_t offset, const double *slots, size_t count) {
  for (size_t i = 0; i < count; i++)
    qa_qvm_writable_view_set_int32(view, offset + (int64_t)(4 * i), (int32_t)slots[i], true);
}

void qa_qvm_read_source_player_state(const qa_qvm_writable_view *view, qa_qvm_abi_profile profile, qa_q3_player_state *out) {
  if (profile == NULL) profile = "q3-modern";
  check_record(view, profile);
  bool modern = is_modern(profile);
  out->command_time_milliseconds = get_i32(view, 0);
  out->movement_type = get_i32(view, 4);
  out->bob_cycle = get_i32(view, 8);
  out->movement_flags = get_i32(view, 12);
  out->movement_time_milliseconds = get_i32(view, 16);
  out->origin = read_vector(view, 20);
  out->velocity = read_vector(view, 32);
  out->weapon_time_milliseconds = get_i32(view, 44);
  out->gravity = get_i32(view, 48);
  out->speed = get_i32(view, 52);
  out->delta_angle_words.values[0] = get_i32(view, 56);
  out->delta_angle_words.values[1] = get_i32(view, 60);
  out->delta_angle_words.values[2] = get_i32(view, 64);
  out->ground_entity_number = get_i32(view, 68);
  out->legs_timer_milliseconds = get_i32(view, 72);
  out->legs_animation = get_i32(view, 76);
  out->torso_timer_milliseconds = get_i32(view, 80);
  out->torso_animation = get_i32(view, 84);
  out->movement_direction = get_i32(view, 88);
  out->grapple_point = read_vector(view, 92);
  out->flags = get_i32(view, 104);
  out->event_sequence = get_i32(view, 108);
  out->events[0] = get_i32(view, 112);
  out->events[1] = get_i32(view, 116);
  out->event_parameters[0] = get_i32(view, 120);
  out->event_parameters[1] = get_i32(view, 124);
  out->external_event = get_i32(view, 128);
  out->external_event_parameter = get_i32(view, 132);
  out->external_event_time_milliseconds = get_i32(view, 136);
  out->client_number = get_i32(view, 140);
  out->weapon = get_i32(view, 144);
  out->weapon_state = get_i32(view, 148);
  out->view_angles = read_vector(view, 152);
  out->view_height = get_i32(view, 164);
  out->damage_event = get_i32(view, 168);
  out->damage_yaw = get_i32(view, 172);
  out->damage_pitch = get_i32(view, 176);
  out->damage_count = get_i32(view, 180);
  double *stats = (double *)(void *)out->stats.values;
  double *persistent = (double *)(void *)out->persistent.values;
  double *powerups = (double *)(void *)out->powerups.values;
  double *ammo = (double *)(void *)out->ammo.values;
  read_slots(view, 184, stats);
  read_slots(view, 248, persistent);
  read_slots(view, 312, powerups);
  read_slots(view, 376, ammo);
  out->stats.count = 16;
  out->persistent.count = 16;
  out->powerups.count = 16;
  out->ammo.count = 16;
  out->generic1 = modern ? get_i32(view, 440) : 0;
  out->loop_sound = modern ? get_i32(view, 444) : 0;
  out->jump_pad_entity = modern ? get_i32(view, 448) : 0;
  out->ping_milliseconds = get_i32(view, modern ? 452 : 440);
  out->movement_frame_count = modern ? get_i32(view, 456) : 0;
  out->jump_pad_frame = modern ? get_i32(view, 460) : 0;
  out->entity_event_sequence = modern ? get_i32(view, 464) : 0;
}

void qa_qvm_read_player_state(const qa_qvm_writable_view *view, qa_qvm_abi_profile profile, qa_q3_player_state *out) {
  if (profile == NULL) profile = "q3-modern";
  qa_qvm_read_source_player_state(view, profile, out);
  out->events[0] = qa_qvm_legacy_event((int32_t)out->events[0], profile, false);
  out->events[1] = qa_qvm_legacy_event((int32_t)out->events[1], profile, false);
  out->external_event = qa_qvm_legacy_event((int32_t)out->external_event, profile, false);
  double *persistent = (double *)(void *)out->persistent.values;
  double translated[16];
  qa_qvm_legacy_persistent(persistent, 16, profile, translated);
  memcpy(persistent, translated, sizeof translated);
  (void)qa_qvm_legacy_powerups(out->powerups.values, out->powerups.count, profile);
}

static void check_slots(const qa_q3_player_state *state) {
  if (state->stats.count != 16 || state->persistent.count != 16 ||
      state->powerups.count != 16 || state->ammo.count != 16)
    qa_range_error("QVM player-state arrays require 16 slots");
}

void qa_qvm_write_source_player_state(qa_qvm_writable_view *view, const qa_q3_player_state *state,
                                      qa_qvm_abi_profile profile) {
  if (profile == NULL) profile = "q3-modern";
  check_record(view, profile);
  check_slots(state);
  bool modern = is_modern(profile);
  qa_qvm_writable_view_set_int32(view, 0, (int32_t)state->command_time_milliseconds, true);
  qa_qvm_writable_view_set_int32(view, 4, (int32_t)state->movement_type, true);
  qa_qvm_writable_view_set_int32(view, 8, (int32_t)state->bob_cycle, true);
  qa_qvm_writable_view_set_int32(view, 12, (int32_t)state->movement_flags, true);
  qa_qvm_writable_view_set_int32(view, 16, (int32_t)state->movement_time_milliseconds, true);
  write_vector(view, 20, state->origin);
  write_vector(view, 32, state->velocity);
  qa_qvm_writable_view_set_int32(view, 44, (int32_t)state->weapon_time_milliseconds, true);
  qa_qvm_writable_view_set_int32(view, 48, (int32_t)state->gravity, true);
  qa_qvm_writable_view_set_int32(view, 52, (int32_t)state->speed, true);
  qa_qvm_writable_view_set_int32(view, 56, (int32_t)state->delta_angle_words.values[0], true);
  qa_qvm_writable_view_set_int32(view, 60, (int32_t)state->delta_angle_words.values[1], true);
  qa_qvm_writable_view_set_int32(view, 64, (int32_t)state->delta_angle_words.values[2], true);
  qa_qvm_writable_view_set_int32(view, 68, (int32_t)state->ground_entity_number, true);
  qa_qvm_writable_view_set_int32(view, 72, (int32_t)state->legs_timer_milliseconds, true);
  qa_qvm_writable_view_set_int32(view, 76, (int32_t)state->legs_animation, true);
  qa_qvm_writable_view_set_int32(view, 80, (int32_t)state->torso_timer_milliseconds, true);
  qa_qvm_writable_view_set_int32(view, 84, (int32_t)state->torso_animation, true);
  qa_qvm_writable_view_set_int32(view, 88, (int32_t)state->movement_direction, true);
  write_vector(view, 92, state->grapple_point);
  qa_qvm_writable_view_set_int32(view, 104, (int32_t)state->flags, true);
  qa_qvm_writable_view_set_int32(view, 108, (int32_t)state->event_sequence, true);
  write_slots(view, 112, state->events, 2);
  write_slots(view, 120, state->event_parameters, 2);
  qa_qvm_writable_view_set_int32(view, 128, (int32_t)state->external_event, true);
  qa_qvm_writable_view_set_int32(view, 132, (int32_t)state->external_event_parameter, true);
  qa_qvm_writable_view_set_int32(view, 136, (int32_t)state->external_event_time_milliseconds, true);
  qa_qvm_writable_view_set_int32(view, 140, (int32_t)state->client_number, true);
  qa_qvm_writable_view_set_int32(view, 144, (int32_t)state->weapon, true);
  qa_qvm_writable_view_set_int32(view, 148, (int32_t)state->weapon_state, true);
  write_vector(view, 152, state->view_angles);
  qa_qvm_writable_view_set_int32(view, 164, (int32_t)state->view_height, true);
  qa_qvm_writable_view_set_int32(view, 168, (int32_t)state->damage_event, true);
  qa_qvm_writable_view_set_int32(view, 172, (int32_t)state->damage_yaw, true);
  qa_qvm_writable_view_set_int32(view, 176, (int32_t)state->damage_pitch, true);
  qa_qvm_writable_view_set_int32(view, 180, (int32_t)state->damage_count, true);
  write_slots(view, 184, state->stats.values, state->stats.count);
  write_slots(view, 248, state->persistent.values, state->persistent.count);
  write_slots(view, 312, state->powerups.values, state->powerups.count);
  write_slots(view, 376, state->ammo.values, state->ammo.count);
  if (!modern) {
    qa_qvm_writable_view_set_int32(view, 440, (int32_t)state->ping_milliseconds, true);
    return;
  }
  qa_qvm_writable_view_set_int32(view, 440, (int32_t)state->generic1, true);
  qa_qvm_writable_view_set_int32(view, 444, (int32_t)state->loop_sound, true);
  qa_qvm_writable_view_set_int32(view, 448, (int32_t)state->jump_pad_entity, true);
  qa_qvm_writable_view_set_int32(view, 452, (int32_t)state->ping_milliseconds, true);
  qa_qvm_writable_view_set_int32(view, 456, (int32_t)state->movement_frame_count, true);
  qa_qvm_writable_view_set_int32(view, 460, (int32_t)state->jump_pad_frame, true);
  qa_qvm_writable_view_set_int32(view, 464, (int32_t)state->entity_event_sequence, true);
}

void qa_qvm_write_player_state(qa_qvm_writable_view *view, const qa_q3_player_state *state,
                               qa_qvm_abi_profile profile, const char *persistent_write) {
  if (profile == NULL) profile = "q3-modern";
  if (persistent_write == NULL) persistent_write = "initialize";
  check_record(view, profile);
  check_slots(state);
  bool modern = is_modern(profile);
  double persistent[16];
  if (modern) {
    memcpy(persistent, state->persistent.values, sizeof persistent);
  } else if (strcmp(persistent_write, "preserve-private") == 0) {
    read_slots(view, 248, persistent);
  } else {
    memset(persistent, 0, sizeof persistent);
  }
  if (!modern) {
    static const int direct[] = {0, 1, 2, 3, 4, 8, 9, 10};
    for (size_t i = 0; i < sizeof direct / sizeof direct[0]; i++)
      persistent[direct[i]] = state->persistent.values[direct[i]];
    persistent[7] = state->persistent.values[6];
    persistent[11] = state->persistent.values[13];
  }
  qa_q3_player_state translated = *state;
  translated.persistent.values = persistent;
  translated.persistent.count = 16;
  translated.events[0] = qa_qvm_legacy_event((int32_t)state->events[0], profile, true);
  translated.events[1] = qa_qvm_legacy_event((int32_t)state->events[1], profile, true);
  translated.external_event = qa_qvm_legacy_event((int32_t)state->external_event, profile, true);
  (void)qa_qvm_legacy_powerups(state->powerups.values, state->powerups.count, profile);
  qa_qvm_write_source_player_state(view, &translated, profile);
}
