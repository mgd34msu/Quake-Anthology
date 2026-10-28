/* Port of src/app/bootstrap/network/unified-prediction.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_prediction.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include "qa/app/bootstrap/simulation/prediction/step.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static qa_save_reader field_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("out of memory");
  return out;
}
static double finite_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  double value = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_vec3 vec_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field_of(reader, name);
  qa_vec3 value = qa_app_unified_read_vector(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_save_value *object(void) {
  qa_save_value *value = qa_save_value_object();
  if (!value) qa_range_error("out of memory");
  return value;
}
static void set(qa_save_value *target, const char *key, qa_save_value *child) {
  if (!child || !qa_save_value_set(target, key, child)) qa_range_error("out of memory");
}
static void push(qa_save_value *target, qa_save_value *child) {
  if (!child || !qa_save_value_push(target, child)) qa_range_error("out of memory");
}
static qa_save_value *num(double value) { return qa_save_value_number(value); }
static qa_save_value *write_vector(qa_vec3 v) {
  qa_save_value *out = object();
  set(out, "x", num(v.x)); set(out, "y", num(v.y)); set(out, "z", num(v.z));
  return out;
}
static qa_save_value *write_bounds(qa_bounds bounds) {
  qa_save_value *out = object();
  set(out, "min", write_vector(bounds.min));
  set(out, "max", write_vector(bounds.max));
  return out;
}
static qa_bounds read_bounds(const qa_save_reader *reader) {
  qa_bounds out = {vec_field(reader, "min"), vec_field(reader, "max")};
  return out;
}
static void read_triple(const qa_save_reader *reader, double out[3]) {
  size_t count = qa_save_reader_list_count(reader);
  if (count != 3) qa_save_reader_fail(reader, "expected three numbers");
  for (size_t i = 0; i < 3; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(reader, i, &item)) qa_range_error("out of memory");
    out[i] = qa_save_reader_finite(&item);
    qa_save_reader_destroy(&item);
  }
}
static qa_save_value *write_triple(const double values[3]) {
  qa_save_value *out = qa_save_value_array();
  if (!out) qa_range_error("out of memory");
  for (int i = 0; i < 3; i++) push(out, num(values[i]));
  return out;
}
static qa_save_value *write_hit(qa_trace_hit hit) {
  qa_save_value *out = object();
  switch (hit.kind) {
    case QA_TRACE_HIT_NONE: set(out, "kind", qa_save_value_string("none")); break;
    case QA_TRACE_HIT_WORLD:
      set(out, "kind", qa_save_value_string("world"));
      set(out, "model", num(hit.model));
      break;
    case QA_TRACE_HIT_ACTOR:
      set(out, "kind", qa_save_value_string("actor"));
      set(out, "actor", qa_app_unified_wire_actor(hit.actor));
      break;
  }
  return out;
}
static qa_trace_hit read_hit(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity) {
  static const char *kinds[] = {"none", "world", "actor"};
  qa_save_reader kind_reader = field_of(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 3);
  qa_save_reader_destroy(&kind_reader);
  qa_trace_hit hit = {0};
  if (strcmp(kind, "none") == 0) hit.kind = QA_TRACE_HIT_NONE;
  else if (strcmp(kind, "world") == 0) {
    hit.kind = QA_TRACE_HIT_WORLD;
    qa_save_reader model = field_of(reader, "model");
    hit.model = qa_save_reader_integer_min(&model, 0);
    qa_save_reader_destroy(&model);
  } else {
    hit.kind = QA_TRACE_HIT_ACTOR;
    qa_save_reader actor_reader = field_of(reader, "actor");
    hit.actor = qa_app_unified_read_actor(&actor_reader, identity);
    qa_save_reader_destroy(&actor_reader);
  }
  return hit;
}
static const char *movement_kind_name(qa_movement_state_kind kind) {
  switch (kind) {
    case QA_MOVEMENT_STATE_Q1: return "q1-netquake";
    case QA_MOVEMENT_STATE_QW: return "q1-quakeworld";
    case QA_MOVEMENT_STATE_Q2: return "q2-classic";
    case QA_MOVEMENT_STATE_Q2_RERELEASE: return "q2-rerelease";
    case QA_MOVEMENT_STATE_Q3: return "q3";
  }
  return "q3";
}
static qa_save_value *write_movement(const qa_movement_state *state) {
  qa_save_value *out = object();
  set(out, "kind", qa_save_value_string(movement_kind_name(state->kind)));
  switch (state->kind) {
    case QA_MOVEMENT_STATE_Q1: {
      const qa_q1_movement_state *s = &state->as.q1;
      set(out, "origin", write_vector(s->origin));
      set(out, "velocity", write_vector(s->velocity));
      set(out, "angles", write_vector(s->angles));
      set(out, "oldOrigin", write_vector(s->old_origin));
      set(out, "angularVelocity", write_vector(s->angular_velocity));
      set(out, "viewAngles", write_vector(s->view_angles));
      set(out, "punchAngles", write_vector(s->punch_angles));
      set(out, "moveType", num(s->move_type));
      set(out, "flags", num(s->flags));
      set(out, "ground", write_hit(s->ground));
      set(out, "waterLevel", num(s->water_level));
      set(out, "waterType", num(s->water_type));
      set(out, "teleportTimeSeconds", num(s->teleport_time_seconds));
      set(out, "waterJumpDirection", write_vector(s->water_jump_direction));
      set(out, "idealPitch", num(s->ideal_pitch));
      set(out, "fixAngle", qa_save_value_boolean(s->fix_angle));
      set(out, "health", num(s->health));
      break;
    }
    case QA_MOVEMENT_STATE_QW: {
      const qa_qw_movement_state *s = &state->as.qw;
      set(out, "origin", write_vector(s->origin));
      set(out, "velocity", write_vector(s->velocity));
      set(out, "angles", write_vector(s->angles));
      set(out, "oldButtons", num(s->old_buttons));
      set(out, "waterJumpTimeSeconds", num(s->water_jump_time_seconds));
      set(out, "dead", qa_save_value_boolean(s->dead));
      set(out, "spectator", num(s->spectator));
      set(out, "ground", write_hit(s->ground));
      break;
    }
    case QA_MOVEMENT_STATE_Q2: {
      const qa_q2_movement_state *s = &state->as.q2;
      set(out, "type", num(s->type));
      set(out, "originEighths", write_triple(s->origin_eighths.values));
      set(out, "velocityEighths", write_triple(s->velocity_eighths.values));
      set(out, "flags", num(s->flags));
      set(out, "timeEightMilliseconds", num(s->time_eight_milliseconds));
      set(out, "gravity", num(s->gravity));
      set(out, "deltaAngleShorts", write_triple(s->delta_angle_shorts.values));
      break;
    }
    case QA_MOVEMENT_STATE_Q2_RERELEASE: {
      const qa_q2_rerelease_movement_state *s = &state->as.q2_rerelease;
      set(out, "type", num(s->type));
      set(out, "origin", write_vector(s->origin));
      set(out, "velocity", write_vector(s->velocity));
      set(out, "flags", num(s->flags));
      set(out, "timeMilliseconds", num(s->time_milliseconds));
      set(out, "gravity", num(s->gravity));
      set(out, "deltaAngles", write_vector(s->delta_angles));
      set(out, "viewHeight", num(s->view_height));
      break;
    }
    case QA_MOVEMENT_STATE_Q3: {
      const qa_q3_movement_state *s = &state->as.q3;
      set(out, "commandTimeMilliseconds", num(s->command_time_milliseconds));
      set(out, "movementType", num(s->movement_type));
      set(out, "bobCycle", num(s->bob_cycle));
      set(out, "movementFlags", num(s->movement_flags));
      set(out, "movementTimeMilliseconds", num(s->movement_time_milliseconds));
      set(out, "origin", write_vector(s->origin));
      set(out, "velocity", write_vector(s->velocity));
      set(out, "gravity", num(s->gravity));
      set(out, "speed", num(s->speed));
      set(out, "deltaAngleWords", write_triple(s->delta_angle_words.values));
      set(out, "movementDirection", num(s->movement_direction));
      set(out, "grapplePoint", write_vector(s->grapple_point));
      set(out, "flags", num(s->flags));
      set(out, "viewAngles", write_vector(s->view_angles));
      set(out, "viewHeight", num(s->view_height));
      set(out, "ground", write_hit(s->ground));
      set(out, "predictableEventSequence", num(s->predictable_event_sequence));
      if (s->jump_pad) set(out, "jumpPad", qa_app_unified_wire_actor(*s->jump_pad));
      else set(out, "jumpPad", qa_save_value_null());
      set(out, "movementFrame", num(s->movement_frame));
      set(out, "jumpPadFrame", num(s->jump_pad_frame));
      break;
    }
  }
  return out;
}
static qa_movement_state read_movement(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity) {
  static const char *kinds[] = {"q1-netquake", "q1-quakeworld", "q2-classic", "q2-rerelease", "q3"};
  qa_save_reader kind_reader = field_of(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 5);
  qa_save_reader_destroy(&kind_reader);
  qa_movement_state state;
  memset(&state, 0, sizeof state);
  if (strcmp(kind, "q1-netquake") == 0) {
    state.kind = QA_MOVEMENT_STATE_Q1;
    qa_q1_movement_state *s = &state.as.q1;
    s->origin = vec_field(reader, "origin");
    s->velocity = vec_field(reader, "velocity");
    s->angles = vec_field(reader, "angles");
    s->old_origin = vec_field(reader, "oldOrigin");
    s->angular_velocity = vec_field(reader, "angularVelocity");
    s->view_angles = vec_field(reader, "viewAngles");
    s->punch_angles = vec_field(reader, "punchAngles");
    s->move_type = finite_field(reader, "moveType");
    s->flags = finite_field(reader, "flags");
    qa_save_reader ground = field_of(reader, "ground");
    s->ground = read_hit(&ground, identity);
    qa_save_reader_destroy(&ground);
    s->water_level = finite_field(reader, "waterLevel");
    s->water_type = finite_field(reader, "waterType");
    s->teleport_time_seconds = finite_field(reader, "teleportTimeSeconds");
    s->water_jump_direction = vec_field(reader, "waterJumpDirection");
    s->ideal_pitch = finite_field(reader, "idealPitch");
    qa_save_reader fix = field_of(reader, "fixAngle");
    s->fix_angle = qa_save_reader_boolean(&fix);
    qa_save_reader_destroy(&fix);
    s->health = finite_field(reader, "health");
  } else if (strcmp(kind, "q1-quakeworld") == 0) {
    state.kind = QA_MOVEMENT_STATE_QW;
    qa_qw_movement_state *s = &state.as.qw;
    s->origin = vec_field(reader, "origin");
    s->velocity = vec_field(reader, "velocity");
    s->angles = vec_field(reader, "angles");
    s->old_buttons = finite_field(reader, "oldButtons");
    s->water_jump_time_seconds = finite_field(reader, "waterJumpTimeSeconds");
    qa_save_reader dead = field_of(reader, "dead");
    s->dead = qa_save_reader_boolean(&dead);
    qa_save_reader_destroy(&dead);
    s->spectator = finite_field(reader, "spectator");
    qa_save_reader ground = field_of(reader, "ground");
    s->ground = read_hit(&ground, identity);
    qa_save_reader_destroy(&ground);
  } else if (strcmp(kind, "q2-classic") == 0) {
    state.kind = QA_MOVEMENT_STATE_Q2;
    qa_q2_movement_state *s = &state.as.q2;
    s->type = finite_field(reader, "type");
    qa_save_reader v = field_of(reader, "originEighths");
    read_triple(&v, s->origin_eighths.values);
    qa_save_reader_destroy(&v);
    v = field_of(reader, "velocityEighths");
    read_triple(&v, s->velocity_eighths.values);
    qa_save_reader_destroy(&v);
    s->flags = finite_field(reader, "flags");
    s->time_eight_milliseconds = finite_field(reader, "timeEightMilliseconds");
    s->gravity = finite_field(reader, "gravity");
    v = field_of(reader, "deltaAngleShorts");
    read_triple(&v, s->delta_angle_shorts.values);
    qa_save_reader_destroy(&v);
  } else if (strcmp(kind, "q2-rerelease") == 0) {
    state.kind = QA_MOVEMENT_STATE_Q2_RERELEASE;
    qa_q2_rerelease_movement_state *s = &state.as.q2_rerelease;
    s->type = finite_field(reader, "type");
    s->origin = vec_field(reader, "origin");
    s->velocity = vec_field(reader, "velocity");
    s->flags = finite_field(reader, "flags");
    s->time_milliseconds = finite_field(reader, "timeMilliseconds");
    s->gravity = finite_field(reader, "gravity");
    s->delta_angles = vec_field(reader, "deltaAngles");
    s->view_height = finite_field(reader, "viewHeight");
  } else {
    state.kind = QA_MOVEMENT_STATE_Q3;
    qa_q3_movement_state *s = &state.as.q3;
    s->command_time_milliseconds = finite_field(reader, "commandTimeMilliseconds");
    s->movement_type = finite_field(reader, "movementType");
    s->bob_cycle = finite_field(reader, "bobCycle");
    s->movement_flags = finite_field(reader, "movementFlags");
    s->movement_time_milliseconds = finite_field(reader, "movementTimeMilliseconds");
    s->origin = vec_field(reader, "origin");
    s->velocity = vec_field(reader, "velocity");
    s->gravity = finite_field(reader, "gravity");
    s->speed = finite_field(reader, "speed");
    qa_save_reader v = field_of(reader, "deltaAngleWords");
    read_triple(&v, s->delta_angle_words.values);
    qa_save_reader_destroy(&v);
    s->movement_direction = finite_field(reader, "movementDirection");
    s->grapple_point = vec_field(reader, "grapplePoint");
    s->flags = finite_field(reader, "flags");
    s->view_angles = vec_field(reader, "viewAngles");
    s->view_height = finite_field(reader, "viewHeight");
    qa_save_reader ground = field_of(reader, "ground");
    s->ground = read_hit(&ground, identity);
    qa_save_reader_destroy(&ground);
    s->predictable_event_sequence = finite_field(reader, "predictableEventSequence");
    qa_save_reader jump = field_of(reader, "jumpPad");
    if (!qa_save_reader_is_null(&jump)) {
      qa_actor_id *owned = xmalloc(sizeof *owned);
      *owned = qa_app_unified_read_actor(&jump, identity);
      s->jump_pad = owned;
    }
    qa_save_reader_destroy(&jump);
    s->movement_frame = finite_field(reader, "movementFrame");
    s->jump_pad_frame = finite_field(reader, "jumpPadFrame");
  }
  return state;
}
static qa_save_value *write_time(qa_source_time time) {
  qa_save_value *out = object();
  set(out, "kind", qa_save_value_string(
      time.kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds"));
  set(out, "value", num(time.value));
  return out;
}
static qa_save_value *write_weapon(const qa_weapon_state *state) {
  qa_save_value *out = object();
  set(out, "kind", qa_save_value_string(state->kind));
  if (strcmp(state->kind, "q1") == 0) {
    set(out, "frame", num(state->frame));
    set(out, "attackFinishedSeconds", num(state->attack_finished_seconds));
    set(out, "sourceWeapon", num(state->source_weapon));
  } else if (strcmp(state->kind, "q2") == 0) {
    set(out, "gunFrame", num(state->gun_frame));
    set(out, "state", num(state->state));
    if (state->pending_weapon) set(out, "pendingWeapon", qa_save_value_string(state->pending_weapon));
    else set(out, "pendingWeapon", qa_save_value_null());
    set(out, "machinegunShots", num(state->machinegun_shots));
    set(out, "grenadeTime", write_time(state->grenade_time));
    set(out, "grenadeBlewUp", qa_save_value_boolean(state->grenade_blew_up));
  } else {
    set(out, "sourceWeapon", num(state->source_weapon));
    set(out, "state", num(state->state));
    set(out, "timeMilliseconds", num(state->time_milliseconds));
  }
  return out;
}
static qa_weapon_state read_weapon(const qa_save_reader *reader) {
  static const char *kinds[] = {"q1", "q2", "q3"};
  qa_save_reader kind_reader = field_of(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 3);
  qa_save_reader_destroy(&kind_reader);
  qa_weapon_state state;
  memset(&state, 0, sizeof state);
  state.kind = kind;
  if (strcmp(kind, "q1") == 0) {
    state.frame = finite_field(reader, "frame");
    state.attack_finished_seconds = finite_field(reader, "attackFinishedSeconds");
    state.source_weapon = finite_field(reader, "sourceWeapon");
  } else if (strcmp(kind, "q2") == 0) {
    state.gun_frame = finite_field(reader, "gunFrame");
    state.state = finite_field(reader, "state");
    qa_save_reader pending = field_of(reader, "pendingWeapon");
    state.pending_weapon = qa_save_reader_is_null(&pending) ? NULL : qa_namespaced(&pending);
    qa_save_reader_destroy(&pending);
    state.machinegun_shots = finite_field(reader, "machinegunShots");
    qa_save_reader grenade = field_of(reader, "grenadeTime");
    state.grenade_time = qa_read_time(&grenade);
    qa_save_reader_destroy(&grenade);
    qa_save_reader blew = field_of(reader, "grenadeBlewUp");
    state.grenade_blew_up = qa_save_reader_boolean(&blew);
    qa_save_reader_destroy(&blew);
  } else {
    state.source_weapon = finite_field(reader, "sourceWeapon");
    state.state = finite_field(reader, "state");
    state.time_milliseconds = finite_field(reader, "timeMilliseconds");
  }
  return state;
}
static qa_save_value *write_inventory(const qa_inventory_entry *entries, size_t count) {
  qa_save_value *list = qa_save_value_array();
  if (!list) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_value *entry = object();
    set(entry, "item", qa_save_value_string(entries[i].item));
    set(entry, "count", num(entries[i].count));
    push(list, entry);
  }
  return list;
}
static size_t read_inventory(const qa_save_reader *reader, qa_inventory_entry **out) {
  size_t count = qa_save_reader_list_count(reader);
  qa_inventory_entry *entries = xmalloc(count * sizeof *entries);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(reader, i, &row)) qa_range_error("out of memory");
    entries[i] = qa_read_inventory_entry(&row);
    qa_save_reader_destroy(&row);
  }
  *out = entries;
  return count;
}
static qa_save_value *write_arsenal(const qa_arsenal_state *arsenal) {
  qa_save_value *out = object();
  set(out, "provider", qa_save_value_string(arsenal->provider));
  if (arsenal->active_weapon) set(out, "activeWeapon", qa_save_value_string(arsenal->active_weapon));
  else set(out, "activeWeapon", qa_save_value_null());
  set(out, "state", write_weapon(&arsenal->state));
  set(out, "ammo", write_inventory(arsenal->ammo, arsenal->ammo_count));
  return out;
}
static qa_arsenal_state read_arsenal(const qa_save_reader *reader) {
  qa_arsenal_state arsenal;
  memset(&arsenal, 0, sizeof arsenal);
  qa_save_reader provider = field_of(reader, "provider");
  arsenal.provider = qa_namespaced(&provider);
  qa_save_reader_destroy(&provider);
  qa_save_reader active = field_of(reader, "activeWeapon");
  arsenal.active_weapon = qa_save_reader_is_null(&active) ? NULL : qa_namespaced(&active);
  qa_save_reader_destroy(&active);
  qa_save_reader state = field_of(reader, "state");
  arsenal.state = read_weapon(&state);
  qa_save_reader_destroy(&state);
  qa_save_reader ammo = field_of(reader, "ammo");
  qa_inventory_entry *entries = NULL;
  arsenal.ammo_count = read_inventory(&ammo, &entries);
  arsenal.ammo = entries;
  qa_save_reader_destroy(&ammo);
  return arsenal;
}
static qa_save_value *write_animation(const qa_animation_state *state) {
  qa_save_value *out = object();
  set(out, "kind", qa_save_value_string(state->kind));
  if (strcmp(state->kind, "q1") == 0) {
    set(out, "frame", num(state->frame));
    set(out, "nextFrameSeconds", num(state->next_frame_seconds));
  } else if (strcmp(state->kind, "q2") == 0) {
    set(out, "frame", num(state->frame));
    set(out, "endFrame", num(state->end_frame));
    set(out, "priority", num(state->priority));
    set(out, "duck", qa_save_value_boolean(state->duck));
    set(out, "run", qa_save_value_boolean(state->run));
  } else {
    set(out, "legs", num(state->legs));
    set(out, "torso", num(state->torso));
    set(out, "legsTimerMilliseconds", num(state->legs_timer_milliseconds));
    set(out, "torsoTimerMilliseconds", num(state->torso_timer_milliseconds));
  }
  return out;
}
static qa_animation_state read_animation(const qa_save_reader *reader) {
  static const char *kinds[] = {"q1", "q2", "q3"};
  qa_save_reader kind_reader = field_of(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 3);
  qa_save_reader_destroy(&kind_reader);
  qa_animation_state state;
  memset(&state, 0, sizeof state);
  state.kind = kind;
  if (strcmp(kind, "q1") == 0) {
    state.frame = finite_field(reader, "frame");
    state.next_frame_seconds = finite_field(reader, "nextFrameSeconds");
  } else if (strcmp(kind, "q2") == 0) {
    state.frame = finite_field(reader, "frame");
    state.end_frame = finite_field(reader, "endFrame");
    state.priority = finite_field(reader, "priority");
    qa_save_reader flag = field_of(reader, "duck");
    state.duck = qa_save_reader_boolean(&flag);
    qa_save_reader_destroy(&flag);
    flag = field_of(reader, "run");
    state.run = qa_save_reader_boolean(&flag);
    qa_save_reader_destroy(&flag);
  } else {
    state.legs = finite_field(reader, "legs");
    state.torso = finite_field(reader, "torso");
    state.legs_timer_milliseconds = finite_field(reader, "legsTimerMilliseconds");
    state.torso_timer_milliseconds = finite_field(reader, "torsoTimerMilliseconds");
  }
  return state;
}
static const char *clock_kind_name(qa_clock_profile_kind kind) {
  switch (kind) {
    case QA_CLOCK_Q1_NETQUAKE: return "q1-netquake";
    case QA_CLOCK_Q1_QUAKEWORLD: return "q1-quakeworld";
    case QA_CLOCK_Q2_CLASSIC: return "q2-classic";
    case QA_CLOCK_Q2_RERELEASE: return "q2-rerelease";
    case QA_CLOCK_Q3: return "q3";
  }
  return "q3";
}
static qa_save_value *write_clock(const qa_clock_profile *clock) {
  qa_save_value *out = object();
  set(out, "kind", qa_save_value_string(clock_kind_name(clock->kind)));
  switch (clock->kind) {
    case QA_CLOCK_Q1_NETQUAKE:
      set(out, "minimumFrameSeconds", num(clock->minimum_frame_seconds));
      set(out, "maximumFrameSeconds", num(clock->maximum_frame_seconds));
      if (clock->has_fixed_frame_seconds) set(out, "fixedFrameSeconds", num(clock->fixed_frame_seconds));
      else set(out, "fixedFrameSeconds", qa_save_value_null());
      break;
    case QA_CLOCK_Q1_QUAKEWORLD:
      set(out, "maximumCommandMilliseconds", num(clock->maximum_command_milliseconds));
      break;
    case QA_CLOCK_Q2_CLASSIC:
      set(out, "frameMilliseconds", num(clock->frame_milliseconds));
      break;
    case QA_CLOCK_Q2_RERELEASE:
      set(out, "frameMilliseconds", num(clock->frame_milliseconds));
      set(out, "preparation", qa_save_value_string("before-frame"));
      break;
    case QA_CLOCK_Q3:
      set(out, "serverFrameMilliseconds", num(clock->server_frame_milliseconds));
      if (clock->has_fixed_movement_milliseconds)
        set(out, "fixedMovementMilliseconds", num(clock->fixed_movement_milliseconds));
      else set(out, "fixedMovementMilliseconds", qa_save_value_null());
      set(out, "maximumCommandMilliseconds", num(clock->maximum_command_milliseconds));
      break;
  }
  return out;
}
static const char *rounding_name(qa_floating_rounding rounding) {
  switch (rounding) {
    case QA_FLOATING_ROUNDING_NEAREST_EVEN: return "nearest-even";
    case QA_FLOATING_ROUNDING_TOWARD_ZERO: return "toward-zero";
    case QA_FLOATING_ROUNDING_TOWARD_POSITIVE: return "toward-positive";
    case QA_FLOATING_ROUNDING_TOWARD_NEGATIVE: return "toward-negative";
  }
  return "nearest-even";
}
static qa_save_value *write_numeric(const qa_numeric_profile *profile) {
  qa_save_value *out = object();
  set(out, "id", qa_save_value_string(profile->id));
  qa_save_value *arithmetic = object();
  switch (profile->arithmetic.kind) {
    case QA_ARITHMETIC_BINARY32:
      set(arithmetic, "kind", qa_save_value_string("binary32"));
      set(arithmetic, "round", qa_save_value_string("each-operation"));
      break;
    case QA_ARITHMETIC_DONOR_BINARY64:
      set(arithmetic, "kind", qa_save_value_string("donor-binary64"));
      set(arithmetic, "source", qa_save_value_string(profile->arithmetic.source));
      break;
    case QA_ARITHMETIC_X87:
      set(arithmetic, "kind", qa_save_value_string("x87"));
      set(arithmetic, "precisionBits", num((double)profile->arithmetic.precision_bits));
      set(arithmetic, "rounding", qa_save_value_string(rounding_name(profile->arithmetic.rounding)));
      break;
    case QA_ARITHMETIC_SSE:
      set(arithmetic, "kind", qa_save_value_string("sse"));
      set(arithmetic, "flushToZero", qa_save_value_boolean(profile->arithmetic.flush_to_zero));
      set(arithmetic, "denormalsAreZero", qa_save_value_boolean(profile->arithmetic.denormals_are_zero));
      set(arithmetic, "rounding", qa_save_value_string(rounding_name(profile->arithmetic.rounding)));
      break;
  }
  set(out, "arithmetic", arithmetic);
  set(out, "scalarStorage", qa_save_value_string("binary32"));
  const char *float_to_int = profile->float_to_int == QA_FLOAT_TO_INT_QVM_INDEFINITE
      ? "qvm-indefinite" : profile->float_to_int == QA_FLOAT_TO_INT_X86_INDEFINITE
      ? "x86-indefinite" : "checked-c-truncation";
  set(out, "floatToInt", qa_save_value_string(float_to_int));
  set(out, "integerOverflow", qa_save_value_string("wrap32"));
  return out;
}
static qa_save_value *write_profile(const qa_movement_profile *profile) {
  qa_save_value *out = object();
  set(out, "id", qa_save_value_string(profile->id));
  set(out, "clock", write_clock(&profile->clock));
  set(out, "numeric", write_numeric(&profile->numeric));
  set(out, "kind", qa_save_value_string(profile->kind));
  if (strcmp(profile->kind, "q1-netquake") == 0 || strcmp(profile->kind, "q1-quakeworld") == 0) {
    qa_save_value *parameters = object();
    set(parameters, "gravity", num(profile->parameters.gravity));
    set(parameters, "stopSpeed", num(profile->parameters.stop_speed));
    set(parameters, "maxSpeed", num(profile->parameters.max_speed));
    set(parameters, "spectatorMaxSpeed", num(profile->parameters.spectator_max_speed));
    set(parameters, "accelerate", num(profile->parameters.accelerate));
    set(parameters, "airAccelerate", num(profile->parameters.air_accelerate));
    set(parameters, "waterAccelerate", num(profile->parameters.water_accelerate));
    set(parameters, "friction", num(profile->parameters.friction));
    set(parameters, "waterFriction", num(profile->parameters.water_friction));
    set(parameters, "entityGravity", num(profile->parameters.entity_gravity));
    set(out, "parameters", parameters);
    if (strcmp(profile->kind, "q1-netquake") == 0) {
      set(out, "edition", qa_save_value_string(profile->edition));
      set(out, "edgeFriction", num(profile->edge_friction));
      set(out, "noClipAngleHack", qa_save_value_boolean(profile->no_clip_angle_hack));
    }
  } else if (strcmp(profile->kind, "q2-classic") == 0) {
    set(out, "airAccelerate", num(profile->air_accelerate));
    set(out, "snapInitial", qa_save_value_boolean(profile->snap_initial));
    if (profile->has_strafejump_hack)
      set(out, "strafejumpHack", qa_save_value_boolean(profile->strafejump_hack));
  } else if (strcmp(profile->kind, "q2-rerelease") == 0) {
    set(out, "airAccelerate", num(profile->air_accelerate));
    set(out, "n64Physics", qa_save_value_boolean(profile->n64_physics));
  } else {
    set(out, "product", qa_save_value_string(profile->product));
    if (profile->has_fixed_milliseconds) set(out, "fixedMilliseconds", num(profile->fixed_milliseconds));
    else set(out, "fixedMilliseconds", qa_save_value_null());
    set(out, "noFootsteps", qa_save_value_boolean(profile->no_footsteps));
  }
  return out;
}
static qa_movement_profile read_profile(const qa_save_reader *reader) {
  qa_movement_profile profile;
  memset(&profile, 0, sizeof profile);
  qa_save_reader id_reader = field_of(reader, "id");
  profile.id = qa_namespaced(&id_reader);
  qa_save_reader_destroy(&id_reader);
  qa_save_reader clock = field_of(reader, "clock");
  profile.clock = qa_read_clock(&clock);
  qa_save_reader_destroy(&clock);
  qa_save_reader numeric = field_of(reader, "numeric");
  profile.numeric = qa_read_numeric(&numeric);
  qa_save_reader_destroy(&numeric);
  static const char *kinds[] = {"q1-netquake", "q1-quakeworld", "q2-classic", "q2-rerelease", "q3"};
  qa_save_reader kind_reader = field_of(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 5);
  qa_save_reader_destroy(&kind_reader);
  profile.kind = kind;
  if (strcmp(clock_kind_name(profile.clock.kind), kind) != 0)
    qa_save_reader_fail(reader, "movement clock differs from profile");
  if (strcmp(kind, "q1-netquake") == 0 || strcmp(kind, "q1-quakeworld") == 0) {
    qa_save_reader p = field_of(reader, "parameters");
    profile.parameters.gravity = finite_field(&p, "gravity");
    profile.parameters.stop_speed = finite_field(&p, "stopSpeed");
    profile.parameters.max_speed = finite_field(&p, "maxSpeed");
    profile.parameters.spectator_max_speed = finite_field(&p, "spectatorMaxSpeed");
    profile.parameters.accelerate = finite_field(&p, "accelerate");
    profile.parameters.air_accelerate = finite_field(&p, "airAccelerate");
    profile.parameters.water_accelerate = finite_field(&p, "waterAccelerate");
    profile.parameters.friction = finite_field(&p, "friction");
    profile.parameters.water_friction = finite_field(&p, "waterFriction");
    profile.parameters.entity_gravity = finite_field(&p, "entityGravity");
    qa_save_reader_destroy(&p);
    if (strcmp(kind, "q1-netquake") == 0) {
      static const char *editions[] = {"classic", "rerelease", "quake64"};
      qa_save_reader edition = field_of(reader, "edition");
      profile.edition = qa_save_reader_choice_string(&edition, editions, 3);
      qa_save_reader_destroy(&edition);
      profile.edge_friction = finite_field(reader, "edgeFriction");
      qa_save_reader hack = field_of(reader, "noClipAngleHack");
      profile.no_clip_angle_hack = qa_save_reader_boolean(&hack);
      qa_save_reader_destroy(&hack);
    }
  } else if (strcmp(kind, "q2-classic") == 0) {
    profile.air_accelerate = finite_field(reader, "airAccelerate");
    qa_save_reader snap = field_of(reader, "snapInitial");
    profile.snap_initial = qa_save_reader_boolean(&snap);
    qa_save_reader_destroy(&snap);
    qa_save_reader hack = field_of(reader, "strafejumpHack");
    if (!qa_save_reader_is_undefined(&hack)) {
      profile.has_strafejump_hack = true;
      profile.strafejump_hack = qa_save_reader_boolean(&hack);
    }
    qa_save_reader_destroy(&hack);
  } else if (strcmp(kind, "q2-rerelease") == 0) {
    profile.air_accelerate = finite_field(reader, "airAccelerate");
    qa_save_reader n64 = field_of(reader, "n64Physics");
    profile.n64_physics = qa_save_reader_boolean(&n64);
    qa_save_reader_destroy(&n64);
  } else {
    static const char *products[] = {"baseq3", "missionpack"};
    qa_save_reader product = field_of(reader, "product");
    profile.product = qa_save_reader_choice_string(&product, products, 2);
    qa_save_reader_destroy(&product);
    qa_save_reader fixed = field_of(reader, "fixedMilliseconds");
    if (!qa_save_reader_is_null(&fixed)) {
      profile.has_fixed_milliseconds = true;
      profile.fixed_milliseconds = qa_save_reader_finite(&fixed);
    }
    qa_save_reader_destroy(&fixed);
    qa_save_reader footsteps = field_of(reader, "noFootsteps");
    profile.no_footsteps = qa_save_reader_boolean(&footsteps);
    qa_save_reader_destroy(&footsteps);
  }
  return profile;
}
static qa_save_value *write_body_state(const qa_body_state *state) {
  qa_save_value *out = object();
  set(out, "origin", write_vector(state->origin));
  set(out, "angles", write_vector(state->angles));
  set(out, "velocity", write_vector(state->velocity));
  set(out, "bounds", write_bounds(state->bounds));
  if (state->ground) set(out, "ground", qa_app_unified_wire_actor(*state->ground));
  else set(out, "ground", qa_save_value_null());
  return out;
}
static qa_save_value *write_collision(const qa_spatial_actor *actor) {
  qa_save_value *out = object();
  qa_save_value *body = object();
  set(body, "actor", qa_app_unified_wire_actor(actor->body.actor));
  set(body, "state", write_body_state(&actor->body.state));
  set(body, "linkCount", num(actor->body.link_count));
  set(body, "absoluteBounds", write_bounds(actor->body.absolute_bounds));
  set(out, "body", body);
  qa_save_value *collision = object();
  const char *family = actor->collision.family;
  set(collision, "family", qa_save_value_string(family));
  qa_save_value *shape = object();
  set(shape, "kind", qa_save_value_string(actor->collision.shape_kind));
  if (strcmp(actor->collision.shape_kind, "model") == 0)
    set(shape, "model", num(actor->collision.shape_model));
  set(collision, "shape", shape);
  set(collision, "contents", num(actor->collision.contents));
  if (actor->collision.owner)
    set(collision, "owner", qa_app_unified_wire_actor(*actor->collision.owner));
  else set(collision, "owner", qa_save_value_null());
  set(collision, "role", qa_save_value_string(actor->collision.role));
  set(collision, "monster", qa_save_value_boolean(actor->collision.monster));
  set(collision, "deadMonster", qa_save_value_boolean(actor->collision.dead_monster));
  if (actor->collision.q1_corpse) set(collision, "q1Corpse", qa_save_value_boolean(true));
  if (actor->collision.has_q3_owner) {
    qa_save_value *owner = object();
    set(owner, "entityNumber", num(actor->collision.q3_entity_number));
    set(owner, "ownerNumber", num(actor->collision.q3_owner_number));
    set(collision, "q3Owner", owner);
  }
  set(out, "collision", collision);
  return out;
}
static qa_spatial_actor read_collision(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity) {
  qa_spatial_actor out;
  memset(&out, 0, sizeof out);
  qa_save_reader b = field_of(reader, "body");
  qa_save_reader s = field_of(&b, "state");
  qa_save_reader c = field_of(reader, "collision");
  qa_save_reader shape = field_of(&c, "shape");
  static const char *shape_kinds[] = {"box", "capsule", "model"};
  qa_save_reader shape_kind = field_of(&shape, "kind");
  const char *kind = qa_save_reader_choice_string(&shape_kind, shape_kinds, 3);
  qa_save_reader_destroy(&shape_kind);
  qa_save_reader actor_reader = field_of(&b, "actor");
  out.body.actor = qa_app_unified_read_actor(&actor_reader, identity);
  qa_save_reader_destroy(&actor_reader);
  out.body.state.origin = vec_field(&s, "origin");
  out.body.state.angles = vec_field(&s, "angles");
  out.body.state.velocity = vec_field(&s, "velocity");
  qa_save_reader bounds = field_of(&s, "bounds");
  out.body.state.bounds = read_bounds(&bounds);
  qa_save_reader_destroy(&bounds);
  qa_save_reader ground = field_of(&s, "ground");
  if (!qa_save_reader_is_null(&ground)) {
    qa_actor_id *owned = xmalloc(sizeof *owned);
    *owned = qa_app_unified_read_actor(&ground, identity);
    out.body.state.ground = owned;
  }
  qa_save_reader_destroy(&ground);
  qa_save_reader_destroy(&s);
  qa_save_reader link = field_of(&b, "linkCount");
  out.body.link_count = qa_save_reader_integer_min(&link, 0);
  qa_save_reader_destroy(&link);
  qa_save_reader absolute = field_of(&b, "absoluteBounds");
  out.body.absolute_bounds = read_bounds(&absolute);
  qa_save_reader_destroy(&absolute);
  qa_save_reader_destroy(&b);
  static const char *families[] = {"q1", "q2", "q3"};
  qa_save_reader family_reader = field_of(&c, "family");
  const char *family = qa_save_reader_choice_string(&family_reader, families, 3);
  qa_save_reader_destroy(&family_reader);
  out.collision.family = family;
  out.collision.shape_kind = kind;
  if (strcmp(kind, "model") == 0) {
    qa_save_reader model = field_of(&shape, "model");
    out.collision.shape_model = qa_save_reader_integer_min(&model, 0);
    qa_save_reader_destroy(&model);
  }
  qa_save_reader_destroy(&shape);
  qa_save_reader contents = field_of(&c, "contents");
  out.collision.contents = qa_save_reader_integer(&contents);
  qa_save_reader_destroy(&contents);
  qa_save_reader owner = field_of(&c, "owner");
  if (!qa_save_reader_is_null(&owner)) {
    qa_actor_id *owned = xmalloc(sizeof *owned);
    *owned = qa_app_unified_read_actor(&owner, identity);
    out.collision.owner = owned;
  }
  qa_save_reader_destroy(&owner);
  static const char *roles[] = {"solid", "trigger"};
  qa_save_reader role = field_of(&c, "role");
  out.collision.role = qa_save_reader_choice_string(&role, roles, 2);
  qa_save_reader_destroy(&role);
  qa_save_reader flag = field_of(&c, "monster");
  out.collision.monster = qa_save_reader_boolean(&flag);
  qa_save_reader_destroy(&flag);
  flag = field_of(&c, "deadMonster");
  out.collision.dead_monster = qa_save_reader_boolean(&flag);
  qa_save_reader_destroy(&flag);
  flag = field_of(&c, "q1Corpse");
  if (!qa_save_reader_is_undefined(&flag))
    out.collision.q1_corpse = qa_save_reader_literal_boolean(&flag, true);
  qa_save_reader_destroy(&flag);
  flag = field_of(&c, "q3Owner");
  if (!qa_save_reader_is_undefined(&flag)) {
    out.collision.has_q3_owner = true;
    qa_save_reader number = field_of(&flag, "entityNumber");
    out.collision.q3_entity_number = qa_save_reader_integer(&number);
    qa_save_reader_destroy(&number);
    number = field_of(&flag, "ownerNumber");
    out.collision.q3_owner_number = qa_save_reader_integer(&number);
    qa_save_reader_destroy(&number);
  }
  qa_save_reader_destroy(&flag);
  qa_save_reader_destroy(&c);
  return out;
}
static qa_save_value *write_environment(const qa_movement_environment *environment,
    qa_mod_client_movement_outputs *outputs_storage) {
  (void)outputs_storage;
  qa_save_value *out = object();
  set(out, "health", num(environment->health));
  set(out, "flight", qa_save_value_boolean(environment->flight));
  set(out, "haste", qa_save_value_boolean(environment->haste));
  set(out, "invulnerable", qa_save_value_boolean(environment->invulnerable));
  set(out, "gravityMultiplier", num(environment->gravity_multiplier));
  if (environment->client_outputs) {
    const qa_mod_client_movement_outputs *outputs = environment->client_outputs;
    qa_save_value *client = object();
    if (outputs->view_offset) set(client, "viewOffset", write_vector(*outputs->view_offset));
    if (outputs->has_mode) {
      const char *mode = outputs->mode == QA_MOD_CLIENT_MOVEMENT_NOCLIP ? "noclip"
          : outputs->mode == QA_MOD_CLIENT_MOVEMENT_FREEZE ? "freeze" : "normal";
      set(client, "mode", qa_save_value_string(mode));
    }
    if (outputs->has_stance) set(client, "stance", qa_save_value_boolean(outputs->stance));
    if (outputs->body_bounds) set(client, "bodyBounds", write_bounds(*outputs->body_bounds));
    set(out, "clientOutputs", client);
  }
  return out;
}
uint8_t *qa_app_unified_encode_prediction(const qa_app_unified_prediction_projection *value,
    size_t *out_length) {
  const qa_app_movement_prediction_snapshot *snapshot = &value->snapshot;
  qa_save_value *root = object();
  set(root, "schema", qa_save_value_string("qts-unified-prediction"));
  set(root, "version", num(1));
  set(root, "actor", qa_app_unified_wire_actor(value->actor));
  set(root, "sequence", num(snapshot->sequence));
  set(root, "commandTimeMilliseconds", num(snapshot->command_time_milliseconds));
  set(root, "profile", write_profile(&value->profile));
  set(root, "state", write_movement(&snapshot->state));
  set(root, "arsenal", write_arsenal(&snapshot->arsenal));
  qa_save_value *animation = object();
  set(animation, "provider", qa_save_value_string(snapshot->animation.provider));
  set(animation, "state", write_animation(&snapshot->animation.state));
  set(root, "animation", animation);
  set(root, "standingBounds", write_bounds(value->standing_bounds));
  set(root, "standingViewHeight", num(value->standing_view_height));
  set(root, "bounds", write_bounds(snapshot->bounds));
  set(root, "viewAngles", write_vector(snapshot->view_angles));
  set(root, "viewHeight", num(snapshot->view_height));
  set(root, "viewOffset", write_vector(snapshot->view_offset));
  set(root, "environment", write_environment(&snapshot->environment, NULL));
  if (!snapshot->has_contact) {
    set(root, "contact", qa_save_value_null());
  } else {
    qa_save_value *contact = object();
    set(contact, "ground", write_hit(snapshot->contact_ground));
    set(contact, "waterLevel", num(snapshot->contact_water_level));
    set(contact, "waterType", num(snapshot->contact_water_type));
    set(root, "contact", contact);
  }
  qa_save_value *collisions = qa_save_value_array();
  if (!collisions) qa_range_error("out of memory");
  for (size_t i = 0; i < value->collision_count; i++)
    push(collisions, write_collision(&value->collisions[i]));
  set(root, "collisions", collisions);
  uint8_t *bytes = qa_encode_checkpoint_value(root, out_length);
  qa_save_value_free(root);
  return bytes;
}
qa_app_unified_prediction_projection qa_app_unified_decode_prediction(
    const uint8_t *bytes, size_t length, const qa_app_unified_identity_decoder *identity) {
  if (length > 32 * 1024 * 1024) qa_range_error("Unified prediction exceeds byte limit");
  qa_save_value *tree = qa_decode_checkpoint_value(bytes, length);
  if (!tree) qa_range_error("out of memory");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, tree, "unified-prediction")) qa_range_error("out of memory");
  qa_save_reader schema = field_of(&reader, "schema");
  qa_save_reader_literal_string(&schema, "qts-unified-prediction");
  qa_save_reader_destroy(&schema);
  qa_save_reader version = field_of(&reader, "version");
  qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_app_unified_prediction_projection out;
  memset(&out, 0, sizeof out);
  qa_save_reader state_reader = field_of(&reader, "state");
  qa_movement_state state = read_movement(&state_reader, identity);
  qa_save_reader_destroy(&state_reader);
  qa_save_reader profile_reader = field_of(&reader, "profile");
  qa_movement_profile profile = read_profile(&profile_reader);
  qa_save_reader_destroy(&profile_reader);
  if (strcmp(movement_kind_name(state.kind), profile.kind) != 0)
    qa_save_reader_fail(&reader, "movement state differs from profile");
  out.snapshot.state = state;
  out.profile = profile;
  qa_save_reader actor_reader = field_of(&reader, "actor");
  out.actor = qa_app_unified_read_actor(&actor_reader, identity);
  qa_save_reader_destroy(&actor_reader);
  qa_save_reader sequence = field_of(&reader, "sequence");
  out.snapshot.sequence = qa_save_reader_integer_min(&sequence, -1);
  qa_save_reader_destroy(&sequence);
  out.snapshot.command_time_milliseconds = finite_field(&reader, "commandTimeMilliseconds");
  qa_save_reader arsenal = field_of(&reader, "arsenal");
  out.snapshot.arsenal = read_arsenal(&arsenal);
  qa_save_reader_destroy(&arsenal);
  qa_save_reader animation = field_of(&reader, "animation");
  qa_save_reader provider = field_of(&animation, "provider");
  out.snapshot.animation.provider = qa_namespaced(&provider);
  qa_save_reader_destroy(&provider);
  qa_save_reader anim_state = field_of(&animation, "state");
  out.snapshot.animation.state = read_animation(&anim_state);
  qa_save_reader_destroy(&anim_state);
  qa_save_reader_destroy(&animation);
  qa_save_reader bounds = field_of(&reader, "standingBounds");
  out.standing_bounds = read_bounds(&bounds);
  qa_save_reader_destroy(&bounds);
  out.standing_view_height = finite_field(&reader, "standingViewHeight");
  bounds = field_of(&reader, "bounds");
  out.snapshot.bounds = read_bounds(&bounds);
  qa_save_reader_destroy(&bounds);
  out.snapshot.view_angles = vec_field(&reader, "viewAngles");
  out.snapshot.view_height = finite_field(&reader, "viewHeight");
  out.snapshot.view_offset = vec_field(&reader, "viewOffset");
  qa_save_reader environment = field_of(&reader, "environment");
  qa_movement_environment env;
  memset(&env, 0, sizeof env);
  env.health = finite_field(&environment, "health");
  qa_save_reader flag = field_of(&environment, "flight");
  env.flight = qa_save_reader_boolean(&flag); qa_save_reader_destroy(&flag);
  flag = field_of(&environment, "haste");
  env.haste = qa_save_reader_boolean(&flag); qa_save_reader_destroy(&flag);
  flag = field_of(&environment, "invulnerable");
  env.invulnerable = qa_save_reader_boolean(&flag); qa_save_reader_destroy(&flag);
  env.gravity_multiplier = finite_field(&environment, "gravityMultiplier");
  qa_save_reader outputs_reader = field_of(&environment, "clientOutputs");
  if (!qa_save_reader_is_undefined(&outputs_reader)) {
    qa_mod_client_movement_outputs *outputs = xmalloc(sizeof *outputs);
    memset(outputs, 0, sizeof *outputs);
    qa_save_reader part = field_of(&outputs_reader, "viewOffset");
    if (!qa_save_reader_is_undefined(&part)) {
      qa_vec3 *offset = xmalloc(sizeof *offset);
      *offset = qa_app_unified_read_vector(&part);
      outputs->view_offset = offset;
    }
    qa_save_reader_destroy(&part);
    part = field_of(&outputs_reader, "mode");
    if (!qa_save_reader_is_undefined(&part)) {
      static const char *modes[] = {"normal", "noclip", "freeze"};
      const char *mode = qa_save_reader_choice_string(&part, modes, 3);
      outputs->has_mode = true;
      outputs->mode = strcmp(mode, "noclip") == 0 ? QA_MOD_CLIENT_MOVEMENT_NOCLIP
          : strcmp(mode, "freeze") == 0 ? QA_MOD_CLIENT_MOVEMENT_FREEZE
          : QA_MOD_CLIENT_MOVEMENT_NORMAL;
    }
    qa_save_reader_destroy(&part);
    part = field_of(&outputs_reader, "stance");
    if (!qa_save_reader_is_undefined(&part)) {
      outputs->has_stance = true;
      outputs->stance = qa_save_reader_boolean(&part);
    }
    qa_save_reader_destroy(&part);
    part = field_of(&outputs_reader, "bodyBounds");
    if (!qa_save_reader_is_undefined(&part)) {
      qa_bounds *body_bounds = xmalloc(sizeof *body_bounds);
      *body_bounds = read_bounds(&part);
      outputs->body_bounds = body_bounds;
    }
    qa_save_reader_destroy(&part);
    env.client_outputs = outputs;
  }
  qa_save_reader_destroy(&outputs_reader);
  out.snapshot.environment = env;
  qa_save_reader_destroy(&environment);
  qa_save_reader contact = field_of(&reader, "contact");
  if (!qa_save_reader_is_null(&contact)) {
    out.snapshot.has_contact = true;
    qa_save_reader ground = field_of(&contact, "ground");
    out.snapshot.contact_ground = read_hit(&ground, identity);
    qa_save_reader_destroy(&ground);
    qa_save_reader level = field_of(&contact, "waterLevel");
    out.snapshot.contact_water_level = qa_save_reader_integer(&level);
    qa_save_reader_destroy(&level);
    qa_save_reader type = field_of(&contact, "waterType");
    out.snapshot.contact_water_type = qa_save_reader_integer(&type);
    qa_save_reader_destroy(&type);
  }
  qa_save_reader_destroy(&contact);
  qa_save_reader collisions = field_of(&reader, "collisions");
  size_t collision_count = qa_save_reader_list_count(&collisions);
  qa_spatial_actor *actors = xmalloc(collision_count * sizeof *actors);
  for (size_t i = 0; i < collision_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&collisions, i, &row)) qa_range_error("out of memory");
    actors[i] = read_collision(&row, identity);
    qa_save_reader_destroy(&row);
  }
  out.collisions = actors;
  out.collision_count = collision_count;
  qa_save_reader_destroy(&collisions);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(tree);
  return out;
}
void qa_app_unified_prediction_projection_free(qa_app_unified_prediction_projection *projection) {
  free((void *)projection->snapshot.state.as.q3.jump_pad);
  free((void *)projection->snapshot.arsenal.ammo);
  if (projection->snapshot.environment.client_outputs) {
    const qa_mod_client_movement_outputs *outputs = projection->snapshot.environment.client_outputs;
    free((void *)outputs->view_offset);
    free((void *)outputs->body_bounds);
    free((void *)outputs);
  }
  for (size_t i = 0; i < projection->collision_count; i++) {
    free((void *)projection->collisions[i].body.state.ground);
    free((void *)projection->collisions[i].collision.owner);
  }
  free(projection->collisions);
  memset(projection, 0, sizeof *projection);
}
typedef struct collect_ctx {
  const qa_app_unified_prediction_simulation *simulation;
  qa_spatial_actor *actors; size_t count, capacity;
} collect_ctx;
static void collect_visit(void *ctx, qa_actor_id actor) {
  collect_ctx *collect = ctx;
  qa_spatial_actor linked;
  if (!collect->simulation->linked_actor(collect->simulation->ctx, actor, &linked)) return;
  if (collect->count == collect->capacity) {
    collect->capacity = collect->capacity ? collect->capacity * 2 : 16;
    collect->actors = realloc(collect->actors, collect->capacity * sizeof *collect->actors);
    if (!collect->actors) qa_range_error("out of memory");
  }
  collect->actors[collect->count++] = linked;
}
qa_app_unified_prediction_projection qa_app_unified_project_prediction(
    const qa_app_unified_prediction_simulation *simulation, qa_actor_id actor,
    double acknowledged_input) {
  qa_app_unified_prediction_player player;
  if (!simulation->movement_player(simulation->ctx, actor, &player))
    qa_range_error("Unified prediction requires an admitted movement player");
  qa_app_movement_prediction_snapshot input;
  memset(&input, 0, sizeof input);
  input.sequence = acknowledged_input;
  input.command_time_milliseconds = player.state.kind == QA_MOVEMENT_STATE_Q3
      ? player.state.as.q3.command_time_milliseconds
      : simulation->time_seconds(simulation->ctx) * 1000;
  input.state = player.state;
  input.arsenal = player.arsenal;
  input.animation = player.animation;
  input.environment = player.environment;
  input.bounds = player.bounds;
  input.view_angles = player.view_angles;
  input.view_height = player.view_height;
  input.view_offset = (qa_vec3){0, 0, player.view_height};
  input.has_contact = true;
  input.contact_ground = player.ground;
  input.contact_water_level = player.water_level;
  input.contact_water_type = player.water_type;
  qa_app_movement_prediction_snapshot snapshot = qa_app_copy_prediction_snapshot(input);
  collect_ctx collect = {simulation, NULL, 0, 0};
  simulation->observations(simulation->ctx, collect_visit, &collect);
  qa_app_unified_prediction_projection out;
  memset(&out, 0, sizeof out);
  out.snapshot = snapshot;
  out.actor = actor;
  out.profile = player.profile;
  out.standing_bounds = player.standing_bounds;
  out.standing_view_height = player.character_kind
      && strcmp(player.character_kind, "q3") == 0 ? 26 : 22;
  out.collisions = collect.actors;
  out.collision_count = collect.count;
  return out;
}
