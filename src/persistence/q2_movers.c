/* Port of src/persistence/q2-movers.ts over the content checkpoint types
 * (content/q2/foundation/{movers,motion,angular_motion}.h). Builders emit
 * the keys in the order the TS capture objects are created; readers follow
 * the TS readers. GPL-2.0-or-later. */
#include "qa/persistence/q2_movers.h"

#include <string.h>

#include "q2_codec_internal.h"

/* ---- TS readQ2LinearMotionCheckpoint ---- */
qa_q2_linear_motion_checkpoint qa_read_q2_linear_motion_checkpoint(const qa_save_reader *reader,
                                                                   qa_persist_decoded *owner) {
  qa_q2_linear_motion_checkpoint out;
  out.count = qa_save_reader_list_count(reader);
  qa_q2_linear_motion_entry *entries = qa_persist_decoded_array(owner, out.count, sizeof *entries);
  for (size_t i = 0; i < out.count; i++) {
    qa_save_reader value = q2c_item(reader, i);
    qa_q2_linear_motion_entry *entry = &entries[i];
    entry->actor = q2c_actor(&value, "actor");
    qa_save_reader state = q2c_field(&value, "state");
    entry->direction = q2c_vector(&state, "direction");
    entry->destination = q2c_vector(&state, "destination");
    entry->reference = q2c_vector(&state, "reference");
    entry->remaining = q2c_number(&state, "remaining");
    entry->current_speed = q2c_number(&state, "currentSpeed");
    entry->move_speed = q2c_number(&state, "moveSpeed");
    entry->next_speed = q2c_number(&state, "nextSpeed");
    entry->decel_distance = q2c_number(&state, "decelDistance");
    entry->done = q2c_string(&state, "done");
    qa_save_reader curve = q2c_field(&state, "curve");
    entry->has_curve = !qa_save_reader_is_null(&curve);
    if (entry->has_curve) {
      qa_save_reader positions = q2c_field(&curve, "positions");
      entry->curve.position_count = qa_save_reader_list_count(&positions);
      double *values = qa_persist_decoded_array(owner, entry->curve.position_count, sizeof *values);
      for (size_t p = 0; p < entry->curve.position_count; p++) {
        qa_save_reader position = q2c_item(&positions, p);
        values[p] = qa_save_reader_number(&position);
        qa_save_reader_destroy(&position);
      }
      qa_save_reader_destroy(&positions);
      entry->curve.positions = values;
      entry->curve.frame = q2c_number(&curve, "frame");
      entry->curve.subframe = q2c_number(&curve, "subframe");
      entry->curve.subframes = q2c_number(&curve, "subframes");
    }
    qa_save_reader_destroy(&curve);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&value);
  }
  out.entries = entries;
  return out;
}

/* TS motion.ts capture: {actor, state: {...move, done, curve}}; a move made
 * by moveTo holds direction, remaining, destination, reference, the speeds,
 * done and curve in that order; a curve holds positions, frame, subframe,
 * subframes. */
qa_save_value *qa_q2_linear_motion_checkpoint_value(const qa_q2_linear_motion_checkpoint *checkpoint) {
  qa_save_value *array = qa_save_value_array();
  for (size_t i = 0; array != NULL && i < checkpoint->count; i++) {
    const qa_q2_linear_motion_entry *entry = &checkpoint->entries[i];
    q2c_object state = q2c_object_new();
    q2c_set(&state, "direction", q2c_vector_value(entry->direction));
    q2c_set(&state, "remaining", qa_save_value_number(entry->remaining));
    q2c_set(&state, "destination", q2c_vector_value(entry->destination));
    q2c_set(&state, "reference", q2c_vector_value(entry->reference));
    q2c_set(&state, "currentSpeed", qa_save_value_number(entry->current_speed));
    q2c_set(&state, "moveSpeed", qa_save_value_number(entry->move_speed));
    q2c_set(&state, "nextSpeed", qa_save_value_number(entry->next_speed));
    q2c_set(&state, "decelDistance", qa_save_value_number(entry->decel_distance));
    q2c_set(&state, "done", q2c_string_value(entry->done));
    if (entry->has_curve) {
      q2c_object curve = q2c_object_new();
      qa_save_value *positions = qa_save_value_array();
      for (size_t p = 0; positions != NULL && p < entry->curve.position_count; p++)
        if (!q2c_push(positions, qa_save_value_number(entry->curve.positions[p]))) { qa_save_value_free(positions); positions = NULL; }
      q2c_set(&curve, "positions", positions);
      q2c_set(&curve, "frame", qa_save_value_number(entry->curve.frame));
      q2c_set(&curve, "subframe", qa_save_value_number(entry->curve.subframe));
      q2c_set(&curve, "subframes", qa_save_value_number(entry->curve.subframes));
      q2c_set(&state, "curve", q2c_done(&curve));
    } else {
      q2c_set(&state, "curve", qa_save_value_null());
    }
    q2c_object object = q2c_object_new();
    q2c_set(&object, "actor", q2c_actor_value(entry->actor));
    q2c_set(&object, "state", q2c_done(&state));
    if (!q2c_push(array, q2c_done(&object))) { qa_save_value_free(array); array = NULL; }
  }
  return array;
}

/* ---- TS readQ2AngularMotionCheckpoint ---- */
qa_q2_angular_motion_checkpoint qa_read_q2_angular_motion_checkpoint(const qa_save_reader *reader,
                                                                     qa_persist_decoded *owner) {
  qa_q2_angular_motion_checkpoint out;
  out.count = qa_save_reader_list_count(reader);
  qa_q2_angular_motion_entry *entries = qa_persist_decoded_array(owner, out.count, sizeof *entries);
  for (size_t i = 0; i < out.count; i++) {
    qa_save_reader value = q2c_item(reader, i);
    entries[i].actor = q2c_actor(&value, "actor");
    entries[i].destination = q2c_vector(&value, "destination");
    entries[i].speed = q2c_number(&value, "speed");
    entries[i].done = q2c_string(&value, "done");
    qa_save_reader_destroy(&value);
  }
  out.entries = entries;
  return out;
}
/* TS angular-motion.ts capture: {actor, destination, speed, done}. */
qa_save_value *qa_q2_angular_motion_checkpoint_value(const qa_q2_angular_motion_checkpoint *checkpoint) {
  qa_save_value *array = qa_save_value_array();
  for (size_t i = 0; array != NULL && i < checkpoint->count; i++) {
    const qa_q2_angular_motion_entry *entry = &checkpoint->entries[i];
    q2c_object object = q2c_object_new();
    q2c_set(&object, "actor", q2c_actor_value(entry->actor));
    q2c_set(&object, "destination", q2c_vector_value(entry->destination));
    q2c_set(&object, "speed", qa_save_value_number(entry->speed));
    q2c_set(&object, "done", q2c_string_value(entry->done));
    if (!q2c_push(array, q2c_done(&object))) { qa_save_value_free(array); array = NULL; }
  }
  return array;
}

/* ---- TS readQ2MoversCheckpoint ---- */
qa_q2_movers_checkpoint qa_read_q2_movers_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner) {
  static const char *const PHASES[] = {"bottom", "up", "top", "down"};
  qa_q2_movers_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader doors = q2c_field(reader, "doors");
  out.door_count = qa_save_reader_list_count(&doors);
  qa_q2_door_checkpoint *door_entries = qa_persist_decoded_array(owner, out.door_count, sizeof *door_entries);
  for (size_t i = 0; i < out.door_count; i++) {
    qa_save_reader value = q2c_item(&doors, i);
    qa_q2_door_checkpoint *door = &door_entries[i];
    door->actor = q2c_actor(&value, "actor");
    door->master = q2c_actor(&value, "master");
    qa_save_reader team = q2c_field(&value, "team");
    door->team_count = qa_save_reader_list_count(&team);
    qa_saved_actor_id *members = qa_persist_decoded_array(owner, door->team_count, sizeof *members);
    for (size_t t = 0; t < door->team_count; t++) {
      qa_save_reader member = q2c_item(&team, t);
      members[t] = q2c_read_actor(&member);
      qa_save_reader_destroy(&member);
    }
    door->team = members;
    qa_save_reader_destroy(&team);
    qa_save_reader state = q2c_field(&value, "state");
    door->state.start = q2c_vector(&state, "start");
    door->state.end = q2c_vector(&state, "end");
    door->state.distance = q2c_number(&state, "distance");
    door->state.button = q2c_boolean(&state, "button");
    door->state.angular = q2c_boolean(&state, "angular");
    door->state.water = q2c_boolean(&state, "water");
    door->state.safe_direction = q2c_vector(&state, "safeDirection");
    door->state.water_divisor = q2c_number(&state, "waterDivisor");
    door->state.reversed = q2c_boolean(&state, "reversed");
    door->state.activated = q2c_boolean(&state, "activated");
    door->state.phase = q2c_choice(&state, "phase", PHASES, 4);
    door->state.debounce = q2c_number(&state, "debounce");
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&value);
  }
  out.doors = door_entries;
  qa_save_reader_destroy(&doors);
  qa_save_reader trains = q2c_field(reader, "trains");
  out.train_count = qa_save_reader_list_count(&trains);
  qa_q2_train_checkpoint *train_entries = qa_persist_decoded_array(owner, out.train_count, sizeof *train_entries);
  for (size_t i = 0; i < out.train_count; i++) {
    qa_save_reader value = q2c_item(&trains, i);
    qa_q2_train_checkpoint *train = &train_entries[i];
    train->actor = q2c_actor(&value, "actor");
    train->has_destination = q2c_nullable_actor(&value, "destination", &train->destination);
    train->debounce = q2c_number(&value, "debounce");
    /* Saves from before the destination wait was recorded omit it; the mover
       restore then reads 0. */
    train->has_wait = q2c_present(&value, "wait");
    train->wait = train->has_wait ? q2c_number(&value, "wait") : 0;
    train->ship = q2c_boolean(&value, "ship");
    qa_save_reader_destroy(&value);
  }
  out.trains = train_entries;
  qa_save_reader_destroy(&trains);
  qa_save_reader linear = q2c_field(reader, "linear");
  out.linear = qa_read_q2_linear_motion_checkpoint(&linear, owner);
  qa_save_reader_destroy(&linear);
  qa_save_reader angular = q2c_field(reader, "angular");
  out.angular = qa_read_q2_angular_motion_checkpoint(&angular, owner);
  qa_save_reader_destroy(&angular);
  return out;
}

/* TS movers.ts capture: doors {actor, state, master, team} with the door
 * state in creation order; trains {actor, destination, debounce, wait,
 * ship}. */
static qa_save_value *movers_value(const qa_q2_movers_checkpoint *checkpoint) {
  q2c_object object = q2c_object_new();
  qa_save_value *doors = qa_save_value_array();
  for (size_t i = 0; doors != NULL && i < checkpoint->door_count; i++) {
    const qa_q2_door_checkpoint *door = &checkpoint->doors[i];
    q2c_object state = q2c_object_new();
    q2c_set(&state, "start", q2c_vector_value(door->state.start));
    q2c_set(&state, "end", q2c_vector_value(door->state.end));
    q2c_set(&state, "distance", qa_save_value_number(door->state.distance));
    q2c_set(&state, "button", qa_save_value_boolean(door->state.button));
    q2c_set(&state, "angular", qa_save_value_boolean(door->state.angular));
    q2c_set(&state, "water", qa_save_value_boolean(door->state.water));
    q2c_set(&state, "safeDirection", q2c_vector_value(door->state.safe_direction));
    q2c_set(&state, "waterDivisor", qa_save_value_number(door->state.water_divisor));
    q2c_set(&state, "reversed", qa_save_value_boolean(door->state.reversed));
    q2c_set(&state, "activated", qa_save_value_boolean(door->state.activated));
    q2c_set(&state, "phase", q2c_string_value(door->state.phase));
    q2c_set(&state, "debounce", qa_save_value_number(door->state.debounce));
    qa_save_value *team = qa_save_value_array();
    for (size_t t = 0; team != NULL && t < door->team_count; t++)
      if (!q2c_push(team, q2c_actor_value(door->team[t]))) { qa_save_value_free(team); team = NULL; }
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(door->actor));
    q2c_set(&entry, "state", q2c_done(&state));
    q2c_set(&entry, "master", q2c_actor_value(door->master));
    q2c_set(&entry, "team", team);
    if (!q2c_push(doors, q2c_done(&entry))) { qa_save_value_free(doors); doors = NULL; }
  }
  q2c_set(&object, "doors", doors);
  qa_save_value *trains = qa_save_value_array();
  for (size_t i = 0; trains != NULL && i < checkpoint->train_count; i++) {
    const qa_q2_train_checkpoint *train = &checkpoint->trains[i];
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(train->actor));
    q2c_set(&entry, "destination", q2c_nullable_actor_value(train->has_destination, train->destination));
    q2c_set(&entry, "debounce", qa_save_value_number(train->debounce));
    if (train->has_wait) q2c_set(&entry, "wait", qa_save_value_number(train->wait));
    q2c_set(&entry, "ship", qa_save_value_boolean(train->ship));
    if (!q2c_push(trains, q2c_done(&entry))) { qa_save_value_free(trains); trains = NULL; }
  }
  q2c_set(&object, "trains", trains);
  q2c_set(&object, "linear", qa_q2_linear_motion_checkpoint_value(&checkpoint->linear));
  q2c_set(&object, "angular", qa_q2_angular_motion_checkpoint_value(&checkpoint->angular));
  return q2c_done(&object);
}

uint8_t *qa_encode_q2_movers_checkpoint(const qa_q2_movers_checkpoint *checkpoint, size_t *out_length) {
  return q2c_encode(movers_value(checkpoint), out_length);
}

bool qa_decode_q2_movers_checkpoint(const uint8_t *bytes, size_t length, qa_q2_movers_checkpoint *out,
                                    qa_persist_decoded *owner) {
  qa_save_reader reader;
  if (!q2c_open(bytes, length, "q2-movers", owner, &reader)) return false;
  *out = qa_read_q2_movers_checkpoint(&reader, owner);
  qa_save_reader_destroy(&reader);
  return true;
}
