/* Port of src/compat/q2/rerelease/player-state.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/player_state.h"
#include <string.h>

static void wvec(uint8_t *v, size_t o, qa_vec3 x) { qa_compat_q2_view_set_f32(v, o, x.x); qa_compat_q2_view_set_f32(v, o + 4, x.y); qa_compat_q2_view_set_f32(v, o + 8, x.z); }
static qa_vec3 rvec(const uint8_t *v, size_t o) { return (qa_vec3){qa_compat_q2_view_f32(v, o), qa_compat_q2_view_f32(v, o + 4), qa_compat_q2_view_f32(v, o + 8)}; }
static qa_vec4 rcolor(const uint8_t *v, size_t o) { qa_vec3 x = rvec(v, o); return (qa_vec4){x.x, x.y, x.z, qa_compat_q2_view_f32(v, o + 12)}; }
static void wcolor(uint8_t *v, size_t o, qa_vec4 x) { wvec(v, o, (qa_vec3){x.x, x.y, x.z}); qa_compat_q2_view_set_f32(v, o + 12, x.w); }
void qa_compat_q2_write_rerelease_movement_state(uint8_t *view, qa_q2_rerelease_movement_state value) {
  qa_compat_q2_view_set_i32(view, 0, (int32_t)value.type); wvec(view, 4, value.origin); wvec(view, 16, value.velocity);
  qa_compat_q2_view_set_u16(view, 28, (uint32_t)value.flags); qa_compat_q2_view_set_u16(view, 30, (uint32_t)value.time_milliseconds);
  qa_compat_q2_view_set_i16(view, 32, (int32_t)value.gravity); wvec(view, 36, value.delta_angles); view[48] = (uint8_t)value.view_height;
}
qa_q2_rerelease_movement_state qa_compat_q2_read_rerelease_movement_state(const uint8_t *view) {
  qa_q2_rerelease_movement_state s; memset(&s, 0, sizeof s);
  s.type = qa_compat_q2_view_i32(view, 0); s.origin = rvec(view, 4); s.velocity = rvec(view, 16);
  s.flags = qa_compat_q2_view_u16(view, 28); s.time_milliseconds = qa_compat_q2_view_u16(view, 30);
  s.gravity = qa_compat_q2_view_i16(view, 32); s.delta_angles = rvec(view, 36); s.view_height = (int8_t)view[48];
  return s;
}
void qa_compat_q2_write_rerelease_player_state(const qa_compat_q2_memory *memory, qa_guest_address address, const qa_q2_rerelease_player_state *value) {
  if (value->view.stats.count > 64) qa_range_error("Rerelease player state has more than 64 source stats");
  qa_compat_q2_rerelease_layouts_ensure();
  memory->check(memory->ctx, address, qa_compat_q2_rerelease_player_state_layout.byte_length, "write");
  uint8_t *view = memory->borrow(memory->ctx, address, qa_compat_q2_rerelease_player_state_layout.byte_length);
  qa_compat_q2_write_rerelease_movement_state(view, value->movement);
  wvec(view, 52, value->view.view_angles); wvec(view, 64, value->view.view_offset); wvec(view, 76, value->view.kick_angles);
  wvec(view, 88, value->view.gun_angles); wvec(view, 100, value->view.gun_offset);
  qa_compat_q2_view_set_i32(view, 112, (int32_t)value->view.gun_index); qa_compat_q2_view_set_i32(view, 116, (int32_t)value->gun_skin);
  qa_compat_q2_view_set_i32(view, 120, (int32_t)value->view.gun_frame); qa_compat_q2_view_set_i32(view, 124, (int32_t)value->gun_rate);
  wcolor(view, 128, value->screen_blend); wcolor(view, 144, value->damage_blend);
  qa_compat_q2_view_set_f32(view, 160, value->view.fov); view[164] = (uint8_t)value->view.render_flags;
  for (size_t i = 0; i < 64; i++) qa_compat_q2_view_set_i16(view, 166 + i * 2, i < value->view.stats.count ? (int32_t)value->view.stats.values[i] : 0);
  view[294] = (uint8_t)value->team_id;
}
qa_q2_rerelease_player_state qa_compat_q2_read_rerelease_player_state(const qa_compat_q2_memory *memory, qa_guest_address address) {
  qa_compat_q2_rerelease_layouts_ensure();
  const uint8_t *view = memory->borrow(memory->ctx, address, qa_compat_q2_rerelease_player_state_layout.byte_length);
  qa_q2_rerelease_player_state s; memset(&s, 0, sizeof s);
  s.movement = qa_compat_q2_read_rerelease_movement_state(view);
  s.view.view_angles = rvec(view, 52); s.view.view_offset = rvec(view, 64); s.view.kick_angles = rvec(view, 76);
  s.view.gun_angles = rvec(view, 88); s.view.gun_offset = rvec(view, 100);
  s.view.gun_index = qa_compat_q2_view_i32(view, 112); s.gun_skin = qa_compat_q2_view_i32(view, 116);
  s.view.gun_frame = qa_compat_q2_view_i32(view, 120); s.gun_rate = qa_compat_q2_view_i32(view, 124);
  s.screen_blend = rcolor(view, 128); s.damage_blend = rcolor(view, 144);
  s.view.fov = qa_compat_q2_view_f32(view, 160); s.view.render_flags = view[164];
  static double stats_scratch[64];
  for (size_t i = 0; i < 64; i++) stats_scratch[i] = qa_compat_q2_view_i16(view, 166 + i * 2);
  s.view.stats.values = stats_scratch; s.view.stats.count = 64; s.team_id = view[294];
  return s;
}
void qa_compat_q2_write_rerelease_user_command(uint8_t *view, qa_q2_rerelease_user_command value) {
  view[0] = (uint8_t)value.milliseconds; view[1] = (uint8_t)value.buttons; wvec(view, 4, value.angles);
  qa_compat_q2_view_set_f32(view, 16, value.forward_move); qa_compat_q2_view_set_f32(view, 20, value.side_move);
  view[24] = (uint8_t)value.server_frame; view[25] = (uint8_t)((uint32_t)value.server_frame >> 8);
  view[26] = (uint8_t)((uint32_t)value.server_frame >> 16); view[27] = (uint8_t)((uint32_t)value.server_frame >> 24);
}
qa_q2_rerelease_user_command qa_compat_q2_read_rerelease_user_command(const uint8_t *view) {
  qa_q2_rerelease_user_command c; memset(&c, 0, sizeof c);
  c.milliseconds = view[0]; c.buttons = view[1]; c.angles = rvec(view, 4);
  c.forward_move = qa_compat_q2_view_f32(view, 16); c.side_move = qa_compat_q2_view_f32(view, 20);
  c.server_frame = qa_compat_q2_view_u32(view, 24);
  return c;
}
