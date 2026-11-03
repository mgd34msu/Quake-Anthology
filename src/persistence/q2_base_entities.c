/* Port of src/persistence/q2-base-entities.ts over the content checkpoint
 * type qa_q2_base_entities_module_checkpoint (content/q2/base/entities/
 * index.h). Builders emit the keys in the order the TS capture objects are
 * created; readers follow the TS reader. GPL-2.0-or-later. */
#include "qa/persistence/q2_base_entities.h"

#include <string.h>

#include "q2_codec_internal.h"

static qa_q2_base_movers_checkpoint read_movers(const qa_save_reader *movers, qa_persist_decoded *owner) {
  static const char *const PHASES[] = {"top", "bottom", "up", "down"};
  qa_q2_base_movers_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader platforms = q2c_field(movers, "platforms");
  out.platform_count = qa_save_reader_list_count(&platforms);
  out.platforms = qa_persist_decoded_array(owner, out.platform_count, sizeof *out.platforms);
  for (size_t i = 0; i < out.platform_count; i++) {
    qa_save_reader value = q2c_item(&platforms, i);
    out.platforms[i].actor = q2c_actor(&value, "actor");
    qa_save_reader state = q2c_field(&value, "state");
    out.platforms[i].state.top = q2c_vector(&state, "top");
    out.platforms[i].state.bottom = q2c_vector(&state, "bottom");
    out.platforms[i].state.phase = q2c_choice(&state, "phase", PHASES, 4);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&platforms);
  qa_save_reader secrets = q2c_field(movers, "secrets");
  out.secret_count = qa_save_reader_list_count(&secrets);
  out.secrets = qa_persist_decoded_array(owner, out.secret_count, sizeof *out.secrets);
  for (size_t i = 0; i < out.secret_count; i++) {
    qa_save_reader value = q2c_item(&secrets, i);
    out.secrets[i].actor = q2c_actor(&value, "actor");
    qa_save_reader state = q2c_field(&value, "state");
    qa_q2_base_secret_state *secret = &out.secrets[i].state;
    secret->first = q2c_vector(&state, "first");
    secret->second = q2c_vector(&state, "second");
    secret->home = q2c_vector(&state, "home");
    secret->shootable = q2c_boolean(&state, "shootable");
    secret->blocked_time = q2c_number(&state, "blockedTime");
    secret->message_time = q2c_number(&state, "messageTime");
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&secrets);
  qa_save_reader linear = q2c_field(movers, "linear");
  out.linear = qa_read_q2_linear_motion_checkpoint(&linear, owner);
  qa_save_reader_destroy(&linear);
  return out;
}

static qa_q2_base_scenery_checkpoint read_scenery(const qa_save_reader *scenery, qa_persist_decoded *owner) {
  qa_q2_base_scenery_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader animations = q2c_field(scenery, "animations");
  out.animation_count = qa_save_reader_list_count(&animations);
  out.animations = qa_persist_decoded_array(owner, out.animation_count, sizeof *out.animations);
  for (size_t i = 0; i < out.animation_count; i++) {
    qa_save_reader value = q2c_item(&animations, i);
    out.animations[i].actor = q2c_actor(&value, "actor");
    out.animations[i].first = q2c_integer(&value, "first", 0);
    out.animations[i].end = q2c_integer(&value, "end", 1);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&animations);
  qa_save_reader clocks = q2c_field(scenery, "clocks");
  out.clock_count = qa_save_reader_list_count(&clocks);
  out.clocks = qa_persist_decoded_array(owner, out.clock_count, sizeof *out.clocks);
  for (size_t i = 0; i < out.clock_count; i++) {
    qa_save_reader value = q2c_item(&clocks, i);
    out.clocks[i].actor = q2c_actor(&value, "actor");
    out.clocks[i].value = q2c_number(&value, "value");
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&clocks);
  return out;
}

static qa_q2_turrets_checkpoint read_turrets(const qa_save_reader *turrets, qa_persist_decoded *owner) {
  qa_q2_turrets_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader breaches = q2c_field(turrets, "breaches");
  out.breach_count = qa_save_reader_list_count(&breaches);
  out.breaches = qa_persist_decoded_array(owner, out.breach_count, sizeof *out.breaches);
  for (size_t i = 0; i < out.breach_count; i++) {
    qa_save_reader value = q2c_item(&breaches, i);
    out.breaches[i].actor = q2c_actor(&value, "actor");
    qa_save_reader state = q2c_field(&value, "state");
    qa_q2_base_turret_breach_state *breach = &out.breaches[i].state;
    breach->goal = q2c_vector(&state, "goal");
    breach->muzzle = q2c_vector(&state, "muzzle");
    breach->pitch_max = q2c_number(&state, "pitchMax");
    breach->pitch_min = q2c_number(&state, "pitchMin");
    breach->yaw_min = q2c_number(&state, "yawMin");
    breach->yaw_max = q2c_number(&state, "yawMax");
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&breaches);
  qa_save_reader drivers = q2c_field(turrets, "drivers");
  out.driver_count = qa_save_reader_list_count(&drivers);
  out.drivers = qa_persist_decoded_array(owner, out.driver_count, sizeof *out.drivers);
  for (size_t i = 0; i < out.driver_count; i++) {
    qa_save_reader value = q2c_item(&drivers, i);
    qa_q2_base_turret_driver_entry *driver = &out.drivers[i];
    driver->actor = q2c_actor(&value, "actor");
    driver->has_breach = q2c_nullable_actor(&value, "breach", &driver->breach);
    driver->radius = q2c_number(&value, "radius");
    driver->yaw_offset = q2c_number(&value, "yawOffset");
    driver->height = q2c_number(&value, "height");
    driver->monster_die = q2c_string(&value, "monsterDie");
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&drivers);
  return out;
}

qa_q2_base_entities_module_checkpoint qa_read_q2_base_entities_checkpoint(const qa_save_reader *reader,
                                                                          qa_persist_decoded *owner) {
  qa_q2_base_entities_module_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader version = q2c_field(reader, "version");
  out.version = qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader movers = q2c_field(reader, "movers");
  out.movers = read_movers(&movers, owner);
  qa_save_reader_destroy(&movers);
  qa_save_reader scenery = q2c_field(reader, "scenery");
  out.scenery = read_scenery(&scenery, owner);
  qa_save_reader_destroy(&scenery);
  qa_save_reader turrets = q2c_field(reader, "turrets");
  out.turrets = read_turrets(&turrets, owner);
  qa_save_reader_destroy(&turrets);
  qa_save_reader winds = q2c_field(reader, "windTimes");
  out.wind_time_count = qa_save_reader_list_count(&winds);
  out.wind_times = qa_persist_decoded_array(owner, out.wind_time_count, sizeof *out.wind_times);
  for (size_t i = 0; i < out.wind_time_count; i++) {
    qa_save_reader value = q2c_item(&winds, i);
    out.wind_times[i].actor = q2c_actor(&value, "actor");
    out.wind_times[i].until = q2c_number(&value, "until");
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&winds);
  return out;
}

/* Arrays of n entries built by entry(i); NULL on OOM. */
#define Q2C_ARRAY(name, n, expression)                                             \
  qa_save_value *name = qa_save_value_array();                                     \
  for (size_t i = 0; name != NULL && i < (n); i++)                                \
    if (!q2c_push(name, (expression))) { qa_save_value_free(name); name = NULL; }

static qa_save_value *platform_value(const qa_q2_base_platform_entry *entry) {
  q2c_object state = q2c_object_new();
  q2c_set(&state, "top", q2c_vector_value(entry->state.top));
  q2c_set(&state, "bottom", q2c_vector_value(entry->state.bottom));
  q2c_set(&state, "phase", q2c_string_value(entry->state.phase));
  q2c_object object = q2c_object_new();
  q2c_set(&object, "actor", q2c_actor_value(entry->actor));
  q2c_set(&object, "state", q2c_done(&state));
  return q2c_done(&object);
}
static qa_save_value *secret_value(const qa_q2_base_secret_entry *entry) {
  q2c_object state = q2c_object_new();
  q2c_set(&state, "first", q2c_vector_value(entry->state.first));
  q2c_set(&state, "second", q2c_vector_value(entry->state.second));
  q2c_set(&state, "home", q2c_vector_value(entry->state.home));
  q2c_set(&state, "shootable", qa_save_value_boolean(entry->state.shootable));
  q2c_set(&state, "blockedTime", qa_save_value_number(entry->state.blocked_time));
  q2c_set(&state, "messageTime", qa_save_value_number(entry->state.message_time));
  q2c_object object = q2c_object_new();
  q2c_set(&object, "actor", q2c_actor_value(entry->actor));
  q2c_set(&object, "state", q2c_done(&state));
  return q2c_done(&object);
}
static qa_save_value *animation_value(const qa_q2_base_scenery_animation_entry *entry) {
  q2c_object object = q2c_object_new();
  q2c_set(&object, "actor", q2c_actor_value(entry->actor));
  q2c_set(&object, "first", qa_save_value_number(entry->first));
  q2c_set(&object, "end", qa_save_value_number(entry->end));
  return q2c_done(&object);
}
static qa_save_value *clock_value(const qa_q2_base_scenery_clock_entry *entry) {
  q2c_object object = q2c_object_new();
  q2c_set(&object, "actor", q2c_actor_value(entry->actor));
  q2c_set(&object, "value", qa_save_value_number(entry->value));
  return q2c_done(&object);
}
static qa_save_value *breach_value(const qa_q2_base_turret_breach_entry *entry) {
  q2c_object state = q2c_object_new();
  q2c_set(&state, "goal", q2c_vector_value(entry->state.goal));
  q2c_set(&state, "muzzle", q2c_vector_value(entry->state.muzzle));
  q2c_set(&state, "pitchMax", qa_save_value_number(entry->state.pitch_max));
  q2c_set(&state, "pitchMin", qa_save_value_number(entry->state.pitch_min));
  q2c_set(&state, "yawMin", qa_save_value_number(entry->state.yaw_min));
  q2c_set(&state, "yawMax", qa_save_value_number(entry->state.yaw_max));
  q2c_object object = q2c_object_new();
  q2c_set(&object, "actor", q2c_actor_value(entry->actor));
  q2c_set(&object, "state", q2c_done(&state));
  return q2c_done(&object);
}
/* TS turrets.ts capture: {actor, monsterDie, radius, yawOffset, height, breach}. */
static qa_save_value *driver_value(const qa_q2_base_turret_driver_entry *entry) {
  q2c_object object = q2c_object_new();
  q2c_set(&object, "actor", q2c_actor_value(entry->actor));
  q2c_set(&object, "monsterDie", q2c_string_value(entry->monster_die));
  q2c_set(&object, "radius", qa_save_value_number(entry->radius));
  q2c_set(&object, "yawOffset", qa_save_value_number(entry->yaw_offset));
  q2c_set(&object, "height", qa_save_value_number(entry->height));
  q2c_set(&object, "breach", q2c_nullable_actor_value(entry->has_breach, entry->breach));
  return q2c_done(&object);
}
static qa_save_value *wind_value(const qa_q2_base_wind_time *entry) {
  q2c_object object = q2c_object_new();
  q2c_set(&object, "actor", q2c_actor_value(entry->actor));
  q2c_set(&object, "until", qa_save_value_number(entry->until));
  return q2c_done(&object);
}

static qa_save_value *base_entities_value(const qa_q2_base_entities_module_checkpoint *checkpoint) {
  const qa_q2_base_movers_checkpoint *m = &checkpoint->movers;
  Q2C_ARRAY(platforms, m->platform_count, platform_value(&m->platforms[i]))
  Q2C_ARRAY(secrets, m->secret_count, secret_value(&m->secrets[i]))
  q2c_object movers = q2c_object_new();
  q2c_set(&movers, "platforms", platforms);
  q2c_set(&movers, "secrets", secrets);
  q2c_set(&movers, "linear", qa_q2_linear_motion_checkpoint_value(&m->linear));
  const qa_q2_base_scenery_checkpoint *s = &checkpoint->scenery;
  Q2C_ARRAY(animations, s->animation_count, animation_value(&s->animations[i]))
  Q2C_ARRAY(clocks, s->clock_count, clock_value(&s->clocks[i]))
  q2c_object scenery = q2c_object_new();
  q2c_set(&scenery, "animations", animations);
  q2c_set(&scenery, "clocks", clocks);
  const qa_q2_turrets_checkpoint *t = &checkpoint->turrets;
  Q2C_ARRAY(breaches, t->breach_count, breach_value(&t->breaches[i]))
  Q2C_ARRAY(drivers, t->driver_count, driver_value(&t->drivers[i]))
  q2c_object turrets = q2c_object_new();
  q2c_set(&turrets, "breaches", breaches);
  q2c_set(&turrets, "drivers", drivers);
  Q2C_ARRAY(winds, checkpoint->wind_time_count, wind_value(&checkpoint->wind_times[i]))
  q2c_object object = q2c_object_new();
  q2c_set(&object, "version", qa_save_value_number(checkpoint->version));
  q2c_set(&object, "movers", q2c_done(&movers));
  q2c_set(&object, "scenery", q2c_done(&scenery));
  q2c_set(&object, "turrets", q2c_done(&turrets));
  q2c_set(&object, "windTimes", winds);
  return q2c_done(&object);
}

uint8_t *qa_encode_q2_base_entities_checkpoint(const qa_q2_base_entities_module_checkpoint *checkpoint,
                                               size_t *out_length) {
  return q2c_encode(base_entities_value(checkpoint), out_length);
}

bool qa_decode_q2_base_entities_checkpoint(const uint8_t *bytes, size_t length,
                                           qa_q2_base_entities_module_checkpoint *out, qa_persist_decoded *owner) {
  qa_save_reader reader;
  if (!q2c_open(bytes, length, "q2-base-entities", owner, &reader)) return false;
  *out = qa_read_q2_base_entities_checkpoint(&reader, owner);
  qa_save_reader_destroy(&reader);
  return true;
}
