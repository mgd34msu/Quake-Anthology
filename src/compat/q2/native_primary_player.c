/* Port of src/compat/q2/native-primary-player.ts: source spawn selection and
 * private client pose; the caller owns the equipment's teleport effect.
 * GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_player.h"
#include <math.h>
#include <string.h>
#include "qa/compat/q2/rerelease/api.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"

typedef struct { qa_guest_address entity, client; } player_current;

static const qa_guest_value_layout pointer_layout = {.is_aggregate = false, .storage = "pointer"};
static const qa_guest_value_layout uint8_layout = {.is_aggregate = false, .storage = "uint8"};

static bool classic_abi(const qa_compat_q2_native_primary_player *s) { return strcmp(s->weapon->abi.kind, "windows-i386") == 0; }

static player_current current(const qa_compat_q2_native_primary_player *s, qa_actor_id actor) {
  const qa_compat_q2_native_primary_weapon_host *h = &s->host;
  qa_raw_entity_view record; qa_actor_id owner; player_current out;
  if (!h->record_for(h->ctx, actor, &record) || !h->actor(h->ctx, record, &owner) || !qa_same_actor(owner, actor)) qa_range_error("Native player service has no current actor");
  if (!qa_compat_q2_mem_read_pointer(h->memory, qa_compat_q2_mem_offset(h->memory, record.address, (int64_t)s->weapon->entity_client), &out.client)) qa_range_error("Native player service has no source client");
  h->memory->check(h->memory->ctx, out.client, s->weapon->client_byte_length, "read");
  out.entity = record.address;
  return out;
}
static bool safe_integer(double value) { return value == trunc(value) && fabs(value) <= (double)QA_MAX_SAFE_INTEGER; }

void qa_compat_q2_native_primary_player_init(qa_compat_q2_native_primary_player *s, qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_weapon_profile *weapon, const qa_compat_q2_native_primary_player_profile *profile) {
  if (profile->has_match && (!safe_integer(profile->match.score) || profile->match.score < 0 || fmod(profile->match.score, 4) != 0 || profile->match.score + 4 > weapon->client_byte_length)) qa_range_error("Native score exceeds the declared client record");
  if (strcmp(host.memory->module.digest, profile->digest) != 0 || strcmp(profile->digest, weapon->digest) != 0) qa_range_error("Native player service belongs to another executable");
  s->host = host; s->weapon = weapon; s->profile = profile;
}
double qa_compat_q2_native_primary_player_score(qa_compat_q2_native_primary_player *s, qa_actor_id actor) {
  if (!s->profile->has_match) qa_range_error("Original native score storage has no declaration");
  return qa_compat_q2_mem_read_i32(s->host.memory, qa_compat_q2_mem_offset(s->host.memory, current(s, actor).client, (int64_t)s->profile->match.score));
}
void qa_compat_q2_native_primary_player_set_score(qa_compat_q2_native_primary_player *s, qa_actor_id actor, double score) {
  if (!s->profile->has_match) qa_range_error("Original native score storage has no declaration");
  qa_guest_address address = qa_compat_q2_mem_offset(s->host.memory, current(s, actor).client, (int64_t)s->profile->match.score);
  qa_compat_q2_mem_write_i32(s->host.memory, address, qa_js_to_int32(qa_source_score(score)));
}
double qa_compat_q2_native_primary_player_max_health(qa_compat_q2_native_primary_player *s, qa_actor_id actor) {
  return qa_compat_q2_mem_read_i32(s->host.memory, qa_compat_q2_mem_offset(s->host.memory, current(s, actor).entity, (int64_t)s->weapon->entity_max_health.field.offset));
}
void qa_compat_q2_native_primary_player_set_max_health(qa_compat_q2_native_primary_player *s, qa_actor_id actor, double value) {
  if (value != trunc(value) || !isfinite(value) || value <= 0 || value > 0x7fffffff) qa_range_error("Native player max health requires a positive int32");
  qa_compat_q2_mem_write_i32(s->host.memory, qa_compat_q2_mem_offset(s->host.memory, current(s, actor).entity, (int64_t)s->weapon->entity_max_health.field.offset), qa_js_to_int32(value));
}
void qa_compat_q2_native_primary_player_drop_objectives(qa_compat_q2_native_primary_player *s, qa_actor_id actor) {
  player_current now = current(s, actor);
  if (strcmp(s->profile->objectives_kind, "none") == 0) return;
  qa_guest_value_layout parameters[1] = {pointer_layout};
  qa_guest_value_layout none = {0};
  qa_compat_q2_guest_call_signature signature = classic_abi(s)
    ? qa_compat_q2_classic_signature(parameters, 1, true, none, false)
    : qa_compat_q2_rerelease_signature(parameters, 1, true, none);
  qa_guest_call_value value = qa_compat_q2_call_pointer(true, now.entity);
  s->host.invoke(s->host.ctx, qa_compat_q2_mem_offset(s->host.memory, s->host.image, (int64_t)s->profile->objectives_entry), signature, &value, 1);
  current(s, actor);
}
static qa_vec3 read_vector(qa_compat_q2_memory *memory, qa_guest_address address) {
  qa_vec3 v;
  v.x = qa_compat_q2_mem_read_f32(memory, address);
  v.y = qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, address, 4));
  v.z = qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, address, 8));
  return v;
}
qa_compat_q2_native_spawn_point qa_compat_q2_native_primary_player_spawn_point(qa_compat_q2_native_primary_player *s, qa_actor_id actor) {
  player_current now = current(s, actor);
  qa_compat_q2_memory *memory = s->host.memory;
  qa_guest_address scratch = memory->allocate(memory->ctx, 32, true, 8, "source player spawn result");
  qa_guest_address origin = scratch, angles = qa_compat_q2_mem_offset(memory, scratch, 12), landmark = qa_compat_q2_mem_offset(memory, scratch, 24);
  uint8_t *storage = memory->borrow(memory->ctx, scratch, 32);
  memset(storage, 0, 32);
  qa_guest_call_value values[5] = {qa_compat_q2_call_pointer(true, now.entity), qa_compat_q2_call_pointer(true, origin), qa_compat_q2_call_pointer(true, angles),
                                   qa_compat_q2_call_uint32(1), qa_compat_q2_call_pointer(true, landmark)};
  qa_guest_address target = qa_compat_q2_mem_offset(memory, s->host.image, (int64_t)s->profile->spawn);
  qa_guest_value_layout none = {0};
  if (classic_abi(s)) {
    qa_guest_value_layout parameters[3] = {pointer_layout, pointer_layout, pointer_layout};
    s->host.invoke(s->host.ctx, target, qa_compat_q2_classic_signature(parameters, 3, true, none, false), values, 3);
  } else {
    qa_guest_value_layout parameters[5] = {pointer_layout, pointer_layout, pointer_layout, uint8_layout, pointer_layout};
    qa_guest_call_result result = s->host.invoke(s->host.ctx, target, qa_compat_q2_rerelease_signature(parameters, 5, false, uint8_layout), values, 5);
    if (result.kind != QA_GUEST_VALUE_UINT32 || result.number_value == 0) qa_range_error("Original native spawn selector could not place the player");
  }
  current(s, actor);
  qa_vec3 origin2 = read_vector(memory, origin);
  qa_vec3 angles2 = read_vector(memory, angles);
  qa_compat_q2_native_spawn_point point = {origin2, angles2};
  memory->unmap(memory->ctx, scratch, 32);
  return point;
}
static void write_vector(qa_compat_q2_memory *memory, qa_guest_address address, qa_vec3 value) {
  qa_compat_q2_mem_write_f32(memory, address, value.x);
  qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, address, 4), value.y);
  qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, address, 8), value.z);
}
/* DataView.setInt16(Math.trunc(Math.fround(value))): ToInt16 modular store. */
static int32_t fround_trunc(double value) { return qa_int32(trunc((double)(float)value)); }
void qa_compat_q2_native_primary_player_teleport(qa_compat_q2_native_primary_player *s, qa_actor_id actor, qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles, double hold_milliseconds) {
  player_current now = current(s, actor);
  qa_compat_q2_memory *memory = s->host.memory;
  bool classic = classic_abi(s);
  uint8_t *state = memory->borrow(memory->ctx, now.client, s->weapon->client_byte_length);
  write_vector(memory, qa_compat_q2_mem_offset(memory, now.entity, 4), origin);
  write_vector(memory, qa_compat_q2_mem_offset(memory, now.entity, 28), origin);
  write_vector(memory, qa_compat_q2_mem_offset(memory, now.entity, (int64_t)s->profile->velocity), velocity);
  const double origins[3] = {origin.x, origin.y, origin.z}, velocities[3] = {velocity.x, velocity.y, velocity.z}, rotations[3] = {angles.x, angles.y, angles.z};
  for (size_t axis = 0; axis < 3; axis++) {
    if (classic) qa_compat_q2_view_set_i16(state, 4 + axis * 2, fround_trunc(origins[axis] * 8));
    else qa_compat_q2_view_set_f32(state, 4 + axis * 4, origins[axis]);
  }
  for (size_t axis = 0; axis < 3; axis++) {
    if (classic) qa_compat_q2_view_set_i16(state, 10 + axis * 2, fround_trunc(velocities[axis] * 8));
    else qa_compat_q2_view_set_f32(state, 16 + axis * 4, velocities[axis]);
  }
  if (classic) {
    state[16] = (uint8_t)((state[16] & ~4) | 32);
    double hold = ceil(hold_milliseconds / 8);
    state[17] = (uint8_t)qa_int32(hold < 255 ? hold : 255);
  } else {
    qa_compat_q2_view_set_u16(state, 28, qa_js_to_uint32(((qa_compat_q2_view_u16(state, 28) & ~4) | 32)));
    qa_compat_q2_view_set_u16(state, 30, qa_js_to_uint32(qa_int32(hold_milliseconds < 65535 ? hold_milliseconds : 65535)));
  }
  for (size_t axis = 0; axis < 3; axis++) {
    double delta = (double)(float)(rotations[axis] - qa_compat_q2_view_f32(state, (size_t)s->profile->command_angles + axis * 4));
    if (classic) qa_compat_q2_view_set_i16(state, 20 + axis * 2, fround_trunc(delta * (65536.0 / 360.0)));
    else qa_compat_q2_view_set_f32(state, 36 + axis * 4, delta);
    qa_compat_q2_view_set_f32(state, (classic ? 28 : 52) + axis * 4, rotations[axis]);
    qa_compat_q2_view_set_f32(state, (size_t)s->weapon->client_view_angles + axis * 4, rotations[axis]);
  }
  if (s->profile->has_forward) write_vector(memory, qa_compat_q2_mem_offset(memory, now.client, (int64_t)s->profile->forward), qa_qvm_angle_vectors(angles).forward);
}
