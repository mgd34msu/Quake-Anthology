/* Port of src/app/bootstrap/network/unified-control.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_control.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include "qa/foundation/support.h"
#include "qa/persistence/recipe.h"
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
static qa_save_value *clone_value(const qa_save_value *value) {
  if (!value) return qa_save_value_undefined();
  qa_save_value *out = NULL;
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: out = qa_save_value_undefined(); break;
    case QA_SAVE_NULL: out = qa_save_value_null(); break;
    case QA_SAVE_BOOLEAN: out = qa_save_value_boolean(value->boolean); break;
    case QA_SAVE_NUMBER: out = qa_save_value_number(value->number); break;
    case QA_SAVE_BIGINT: out = qa_save_value_bigint_text(value->text); break;
    case QA_SAVE_STRING: out = qa_save_value_string(value->text); break;
    case QA_SAVE_BYTES: out = qa_save_value_bytes(value->bytes, value->byte_length); break;
    case QA_SAVE_ARRAY:
      out = qa_save_value_array();
      if (out) for (size_t i = 0; i < value->item_count; i++) push(out, clone_value(value->items[i]));
      break;
    case QA_SAVE_OBJECT:
      out = qa_save_value_object();
      if (out) for (size_t i = 0; i < value->entry_count; i++)
        set(out, value->entries[i].key, clone_value(value->entries[i].value));
      break;
  }
  if (!out) qa_range_error("out of memory");
  return out;
}
static double bounded(const qa_save_reader *reader, double minimum, double maximum) {
  double value = qa_save_reader_integer_min(reader, minimum);
  if (value > maximum || value != trunc(value))
    qa_save_reader_fail(reader, "number exceeds protocol limit");
  return value;
}
static const char *protocol_string(const qa_save_reader *reader, size_t maximum) {
  const char *value = qa_save_reader_string(reader);
  if (strlen(value) > maximum) qa_save_reader_fail(reader, "invalid protocol string");
  return value;
}
static const char *field_string(const qa_save_reader *reader, const char *name, size_t maximum) {
  qa_save_reader f = field_of(reader, name);
  const char *value = protocol_string(&f, maximum);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_app_unified_actor_reference read_actor_ref(const qa_save_reader *reader) {
  qa_save_reader slot = field_of(reader, "slot"), generation = field_of(reader, "generation");
  qa_app_unified_actor_reference out = {bounded(&slot, 0, 1048575),
      (double)qa_save_reader_integer_min(&generation, 0)};
  qa_save_reader_destroy(&slot);
  qa_save_reader_destroy(&generation);
  return out;
}
static qa_save_value *write_actor_ref(qa_app_unified_actor_reference reference) {
  qa_save_value *out = object();
  set(out, "slot", qa_save_value_number(reference.slot));
  set(out, "generation", qa_save_value_number(reference.generation));
  return out;
}
static const char *read_token(const qa_save_reader *reader) {
  const char *value = protocol_string(reader, 32);
  if (strlen(value) != 32) qa_save_reader_fail(reader, "invalid connection token");
  for (size_t i = 0; i < 32; i++) {
    char c = value[i];
    if (!((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9')))
      qa_save_reader_fail(reader, "invalid connection token");
  }
  return value;
}
static qa_save_value *write_vector(qa_vec3 v) {
  qa_save_value *out = object();
  set(out, "x", qa_save_value_number(v.x));
  set(out, "y", qa_save_value_number(v.y));
  set(out, "z", qa_save_value_number(v.z));
  return out;
}
static qa_save_value *write_angles(const double values[3]) {
  qa_save_value *out = qa_save_value_array();
  if (!out) qa_range_error("out of memory");
  for (int i = 0; i < 3; i++) push(out, qa_save_value_number(values[i]));
  return out;
}
static void read_angles(const qa_save_reader *reader, double out[3]) {
  size_t count = qa_save_reader_list_count(reader);
  double values[3] = {0, 0, 0};
  for (size_t i = 0; i < count && i < 3; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(reader, i, &item)) qa_range_error("out of memory");
    values[i] = bounded(&item, -2147483648.0, 2147483647);
    qa_save_reader_destroy(&item);
  }
  if (count != 3) qa_save_reader_fail(reader, "expected three command angles");
  out[0] = values[0]; out[1] = values[1]; out[2] = values[2];
}
bool qa_app_unified_command_in_bounds(const qa_user_command *command) {
  switch (command->kind) {
    case QA_USER_COMMAND_Q3: {
      double axes[3] = {command->as.q3.forward_move, command->as.q3.right_move,
          command->as.q3.up_move};
      for (int i = 0; i < 3; i++)
        if (axes[i] != trunc(axes[i]) || axes[i] < -128 || axes[i] > 127) return false;
      return true;
    }
    case QA_USER_COMMAND_QW: {
      double ms = command->as.qw.milliseconds;
      return ms == trunc(ms) && ms >= 0 && ms <= 255;
    }
    case QA_USER_COMMAND_Q2: {
      double ms = command->as.q2.milliseconds;
      return ms == trunc(ms) && ms >= 0 && ms <= 255;
    }
    case QA_USER_COMMAND_Q2_RERELEASE: {
      double ms = command->as.q2_rerelease.milliseconds;
      return ms == trunc(ms) && ms >= 0 && ms <= 255;
    }
    case QA_USER_COMMAND_Q1: return true;
  }
  return true;
}
static qa_user_command read_command(const qa_save_reader *reader) {
  static const char *kinds[] = {"q1-netquake", "q1-quakeworld", "q2-classic", "q2-rerelease", "q3"};
  qa_save_reader kind_reader = field_of(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 5);
  qa_save_reader_destroy(&kind_reader);
  qa_save_reader buttons_reader = field_of(reader, "buttons");
  double buttons = bounded(&buttons_reader, 0, 4294967295.0);
  qa_save_reader_destroy(&buttons_reader);
  qa_user_command command;
  memset(&command, 0, sizeof command);
  if (strcmp(kind, "q3") == 0) {
    command.kind = QA_USER_COMMAND_Q3;
    command.as.q3.buttons = buttons;
    qa_save_reader f = field_of(reader, "serverTimeMilliseconds");
    command.as.q3.server_time_milliseconds = bounded(&f, 0, 2147483647);
    qa_save_reader_destroy(&f);
    f = field_of(reader, "angleWords");
    read_angles(&f, command.as.q3.angle_words.values);
    qa_save_reader_destroy(&f);
    f = field_of(reader, "weapon");
    command.as.q3.weapon = bounded(&f, 0, 255);
    qa_save_reader_destroy(&f);
    const char *names[3] = {"forwardMove", "rightMove", "upMove"};
    double *targets[3] = {&command.as.q3.forward_move, &command.as.q3.right_move,
        &command.as.q3.up_move};
    for (int i = 0; i < 3; i++) {
      f = field_of(reader, names[i]);
      *targets[i] = bounded(&f, -128, 127);
      qa_save_reader_destroy(&f);
    }
    return command;
  }
  if (strcmp(kind, "q2-rerelease") == 0) {
    command.kind = QA_USER_COMMAND_Q2_RERELEASE;
    command.as.q2_rerelease.buttons = buttons;
    qa_save_reader f = field_of(reader, "milliseconds");
    command.as.q2_rerelease.milliseconds = bounded(&f, 0, 255);
    qa_save_reader_destroy(&f);
    f = field_of(reader, "angles");
    command.as.q2_rerelease.angles = qa_app_unified_read_vector(&f);
    qa_save_reader_destroy(&f);
    f = field_of(reader, "forwardMove");
    command.as.q2_rerelease.forward_move = qa_save_reader_finite(&f);
    qa_save_reader_destroy(&f);
    f = field_of(reader, "sideMove");
    command.as.q2_rerelease.side_move = qa_save_reader_finite(&f);
    qa_save_reader_destroy(&f);
    f = field_of(reader, "serverFrame");
    command.as.q2_rerelease.server_frame = bounded(&f, -1, 2147483647);
    qa_save_reader_destroy(&f);
    return command;
  }
  double moves[3];
  const char *names[3] = {"forwardMove", "sideMove", "upMove"};
  for (int i = 0; i < 3; i++) {
    qa_save_reader f = field_of(reader, names[i]);
    moves[i] = bounded(&f, -32768, 32767);
    qa_save_reader_destroy(&f);
  }
  qa_save_reader impulse_reader = field_of(reader, "impulse");
  double impulse = bounded(&impulse_reader, 0, 255);
  qa_save_reader_destroy(&impulse_reader);
  if (strcmp(kind, "q1-netquake") == 0) {
    command.kind = QA_USER_COMMAND_Q1;
    command.as.q1.buttons = buttons;
    command.as.q1.forward_move = moves[0];
    command.as.q1.side_move = moves[1];
    command.as.q1.up_move = moves[2];
    command.as.q1.impulse = impulse;
    qa_save_reader f = field_of(reader, "acknowledgedServerTimeSeconds");
    command.as.q1.acknowledged_server_time_seconds = qa_save_reader_finite(&f);
    qa_save_reader_destroy(&f);
    f = field_of(reader, "viewAngles");
    command.as.q1.view_angles = qa_app_unified_read_vector(&f);
    qa_save_reader_destroy(&f);
    return command;
  }
  qa_save_reader ms_reader = field_of(reader, "milliseconds");
  double milliseconds = bounded(&ms_reader, 0, 255);
  qa_save_reader_destroy(&ms_reader);
  if (strcmp(kind, "q1-quakeworld") == 0) {
    command.kind = QA_USER_COMMAND_QW;
    command.as.qw.buttons = buttons;
    command.as.qw.forward_move = moves[0];
    command.as.qw.side_move = moves[1];
    command.as.qw.up_move = moves[2];
    command.as.qw.impulse = impulse;
    command.as.qw.milliseconds = milliseconds;
    qa_save_reader f = field_of(reader, "angles");
    command.as.qw.angles = qa_app_unified_read_vector(&f);
    qa_save_reader_destroy(&f);
    return command;
  }
  command.kind = QA_USER_COMMAND_Q2;
  command.as.q2.buttons = buttons;
  command.as.q2.forward_move = moves[0];
  command.as.q2.side_move = moves[1];
  command.as.q2.up_move = moves[2];
  command.as.q2.impulse = impulse;
  command.as.q2.milliseconds = milliseconds;
  qa_save_reader f = field_of(reader, "angleShorts");
  read_angles(&f, command.as.q2.angle_shorts.values);
  qa_save_reader_destroy(&f);
  f = field_of(reader, "lightLevel");
  command.as.q2.light_level = bounded(&f, 0, 255);
  qa_save_reader_destroy(&f);
  return command;
}
static qa_save_value *write_command(const qa_user_command *command) {
  qa_save_value *out = object();
  switch (command->kind) {
    case QA_USER_COMMAND_Q3:
      set(out, "kind", qa_save_value_string("q3"));
      set(out, "buttons", qa_save_value_number(command->as.q3.buttons));
      set(out, "serverTimeMilliseconds", qa_save_value_number(command->as.q3.server_time_milliseconds));
      set(out, "angleWords", write_angles(command->as.q3.angle_words.values));
      set(out, "weapon", qa_save_value_number(command->as.q3.weapon));
      set(out, "forwardMove", qa_save_value_number(command->as.q3.forward_move));
      set(out, "rightMove", qa_save_value_number(command->as.q3.right_move));
      set(out, "upMove", qa_save_value_number(command->as.q3.up_move));
      break;
    case QA_USER_COMMAND_Q2_RERELEASE:
      set(out, "kind", qa_save_value_string("q2-rerelease"));
      set(out, "buttons", qa_save_value_number(command->as.q2_rerelease.buttons));
      set(out, "milliseconds", qa_save_value_number(command->as.q2_rerelease.milliseconds));
      set(out, "angles", write_vector(command->as.q2_rerelease.angles));
      set(out, "forwardMove", qa_save_value_number(command->as.q2_rerelease.forward_move));
      set(out, "sideMove", qa_save_value_number(command->as.q2_rerelease.side_move));
      set(out, "serverFrame", qa_save_value_number(command->as.q2_rerelease.server_frame));
      break;
    case QA_USER_COMMAND_Q1:
      set(out, "kind", qa_save_value_string("q1-netquake"));
      set(out, "buttons", qa_save_value_number(command->as.q1.buttons));
      set(out, "forwardMove", qa_save_value_number(command->as.q1.forward_move));
      set(out, "sideMove", qa_save_value_number(command->as.q1.side_move));
      set(out, "upMove", qa_save_value_number(command->as.q1.up_move));
      set(out, "impulse", qa_save_value_number(command->as.q1.impulse));
      set(out, "acknowledgedServerTimeSeconds",
          qa_save_value_number(command->as.q1.acknowledged_server_time_seconds));
      set(out, "viewAngles", write_vector(command->as.q1.view_angles));
      break;
    case QA_USER_COMMAND_QW:
      set(out, "kind", qa_save_value_string("q1-quakeworld"));
      set(out, "buttons", qa_save_value_number(command->as.qw.buttons));
      set(out, "forwardMove", qa_save_value_number(command->as.qw.forward_move));
      set(out, "sideMove", qa_save_value_number(command->as.qw.side_move));
      set(out, "upMove", qa_save_value_number(command->as.qw.up_move));
      set(out, "impulse", qa_save_value_number(command->as.qw.impulse));
      set(out, "milliseconds", qa_save_value_number(command->as.qw.milliseconds));
      set(out, "angles", write_vector(command->as.qw.angles));
      break;
    case QA_USER_COMMAND_Q2:
      set(out, "kind", qa_save_value_string("q2-classic"));
      set(out, "buttons", qa_save_value_number(command->as.q2.buttons));
      set(out, "forwardMove", qa_save_value_number(command->as.q2.forward_move));
      set(out, "sideMove", qa_save_value_number(command->as.q2.side_move));
      set(out, "upMove", qa_save_value_number(command->as.q2.up_move));
      set(out, "impulse", qa_save_value_number(command->as.q2.impulse));
      set(out, "milliseconds", qa_save_value_number(command->as.q2.milliseconds));
      set(out, "angleShorts", write_angles(command->as.q2.angle_shorts.values));
      set(out, "lightLevel", qa_save_value_number(command->as.q2.light_level));
      break;
  }
  return out;
}
static qa_arsenal_intent read_arsenal(const qa_save_reader *reader) {
  qa_arsenal_intent intent;
  memset(&intent, 0, sizeof intent);
  qa_save_reader provider = field_of(reader, "provider");
  intent.provider = qa_namespaced(&provider);
  qa_save_reader_destroy(&provider);
  qa_save_reader weapon = field_of(reader, "weapon");
  intent.weapon = qa_save_reader_is_null(&weapon) ? NULL : qa_namespaced(&weapon);
  qa_save_reader_destroy(&weapon);
  qa_save_reader holdable = field_of(reader, "useHoldable");
  intent.use_holdable = qa_save_reader_boolean(&holdable);
  qa_save_reader_destroy(&holdable);
  qa_save_reader impulse = field_of(reader, "impulse");
  if (!qa_save_reader_is_undefined(&impulse)) {
    intent.has_impulse = true;
    intent.impulse = bounded(&impulse, 0, 255);
  }
  qa_save_reader_destroy(&impulse);
  return intent;
}
static qa_save_value *write_arsenal(const qa_arsenal_intent *intent) {
  qa_save_value *out = object();
  set(out, "provider", qa_save_value_string(intent->provider));
  if (intent->weapon) set(out, "weapon", qa_save_value_string(intent->weapon));
  else set(out, "weapon", qa_save_value_null());
  set(out, "useHoldable", qa_save_value_boolean(intent->use_holdable));
  if (intent->has_impulse) set(out, "impulse", qa_save_value_number(intent->impulse));
  return out;
}
static qa_app_unified_resource_key read_key(const qa_save_reader *reader) {
  const char *path = field_string(reader, "path", 1024);
  bool ok = path[0] != '\0' && path[0] != '/' && !strchr(path, '\\')
      && !(path[0] && path[1] == ':' && ((path[0] >= 'a' && path[0] <= 'z')
          || (path[0] >= 'A' && path[0] <= 'Z')));
  if (ok) {
    const char *cursor = path;
    for (;;) {
      const char *slash = strchr(cursor, '/');
      size_t part = slash ? (size_t)(slash - cursor) : strlen(cursor);
      if (part == 0 || (part == 1 && cursor[0] == '.')
          || (part == 2 && cursor[0] == '.' && cursor[1] == '.')) ok = false;
      if (!slash) break;
      cursor = slash + 1;
    }
  }
  if (!ok) qa_save_reader_fail(reader, "invalid resource member");
  qa_app_unified_resource_key key = {0};
  qa_save_reader content = field_of(reader, "content");
  key.content = qa_read_content_id(&content);
  qa_save_reader_destroy(&content);
  key.path = path;
  qa_save_reader digest = field_of(reader, "digest");
  key.digest = qa_read_digest(&digest);
  qa_save_reader_destroy(&digest);
  qa_save_reader length = field_of(reader, "byteLength");
  key.byte_length = bounded(&length, 0, 2147483647);
  qa_save_reader_destroy(&length);
  return key;
}
static qa_save_reader envelope(const uint8_t *bytes, size_t length,
    const char *schema, size_t maximum, qa_save_value **out_tree) {
  if (length > maximum) qa_range_error("%s exceeds protocol limit", schema);
  qa_save_value *tree = qa_decode_checkpoint_value(bytes, length);
  if (!tree) qa_range_error("out of memory");
  *out_tree = tree;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, tree, schema)) qa_range_error("out of memory");
  qa_save_reader schema_reader = field_of(&reader, "schema");
  qa_save_reader_literal_string(&schema_reader, schema);
  qa_save_reader_destroy(&schema_reader);
  qa_save_reader version = field_of(&reader, "version");
  qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader value = field_of(&reader, "value");
  qa_save_reader_destroy(&reader);
  return value;
}
static uint8_t *encode_envelope(const char *schema, qa_save_value *value, size_t *out_length) {
  qa_save_value *out = object();
  set(out, "schema", qa_save_value_string(schema));
  set(out, "version", qa_save_value_number(1));
  set(out, "value", value);
  uint8_t *bytes = qa_encode_checkpoint_value(out, out_length);
  qa_save_value_free(out);
  return bytes;
}
static qa_save_value *composition_identity_tree(const qa_app_unified_composition *identity) {
  qa_save_value *composition = object();
  set(composition, "schemaVersion", qa_save_value_number(1));
  set(composition, "recipe", clone_value(identity->recipe));
  set(composition, "snapshotSchema", qa_save_value_string("qts:snapshot-v10"));
  qa_save_value *empty = qa_save_value_array();
  if (!empty) qa_range_error("out of memory");
  set(composition, "actorConfigurations", empty);
  qa_save_value *sidecars = qa_save_value_array();
  if (!sidecars) qa_range_error("out of memory");
  for (size_t i = 0; i < identity->sidecar_count; i++) {
    qa_save_value *entry = object();
    set(entry, "content", qa_save_value_string(identity->sidecars[i].content));
    set(entry, "path", qa_save_value_string(identity->sidecars[i].path));
    if (identity->sidecars[i].has_resource) {
      qa_save_value *resource = qa_app_unified_write_resource_key(&identity->sidecars[i].resource);
      if (!resource) qa_range_error("out of memory");
      set(entry, "resource", resource);
    }
    push(sidecars, entry);
  }
  set(composition, "sidecars", sidecars);
  qa_save_value *out = object();
  set(out, "digest", qa_save_value_string(identity->digest));
  set(out, "composition", composition);
  return out;
}
static qa_save_value *write_control(const qa_app_unified_control *value) {
  qa_save_value *out = object();
  static const char *names[] = {"offer", "ready", "admitted", "resources", "events",
    "components", "component-command", "command", "userinfo", "disconnect"};
  set(out, "kind", qa_save_value_string(names[value->kind]));
  if (value->kind == QA_APP_UNIFIED_DISCONNECT) {
    set(out, "reason", qa_save_value_string(value->reason));
    return out;
  }
  set(out, "epoch", qa_save_value_number(value->epoch));
  switch (value->kind) {
    case QA_APP_UNIFIED_OFFER:
      set(out, "composition", composition_identity_tree(value->composition));
      set(out, "mode", qa_save_value_string(value->mode));
      set(out, "maxClients", qa_save_value_number(value->max_clients));
      break;
    case QA_APP_UNIFIED_READY:
      set(out, "composition", qa_save_value_string(value->composition_digest));
      set(out, "userinfo", qa_save_value_string(value->userinfo));
      break;
    case QA_APP_UNIFIED_ADMITTED:
      set(out, "client", write_actor_ref(value->client));
      set(out, "actor", write_actor_ref(value->actor));
      set(out, "sourceEntity", qa_save_value_number(value->source_entity));
      break;
    case QA_APP_UNIFIED_RESOURCES: {
      qa_save_value *list = qa_save_value_array();
      if (!list) qa_range_error("out of memory");
      for (size_t i = 0; i < value->resource_count; i++) {
        qa_save_value *key = qa_app_unified_write_resource_key(&value->resources[i]);
        if (!key) qa_range_error("out of memory");
        push(list, key);
      }
      set(out, "resources", list);
      break;
    }
    case QA_APP_UNIFIED_EVENTS:
      set(out, "frame", qa_save_value_number(value->frame));
      set(out, "payload", qa_save_value_bytes(value->payload, value->payload_length));
      set(out, "simulation", qa_save_value_bytes(value->simulation, value->simulation_length));
      break;
    case QA_APP_UNIFIED_COMPONENTS: {
      qa_save_value *update = qa_app_unified_write_component_update(&value->update);
      set(out, "update", update);
      break;
    }
    case QA_APP_UNIFIED_COMPONENT_COMMAND: {
      qa_save_value *owner = object();
      set(owner, "provider", qa_save_value_string(value->owner.provider));
      set(owner, "generation", qa_save_value_number(value->owner.generation));
      set(out, "owner", owner);
      set(out, "generation", qa_save_value_number(value->generation));
      qa_save_value *args = qa_save_value_array();
      if (!args) qa_range_error("out of memory");
      for (size_t i = 0; i < value->arg_count; i++)
        push(args, qa_save_value_string(value->args[i]));
      set(out, "args", args);
      break;
    }
    case QA_APP_UNIFIED_COMMAND: {
      set(out, "name", qa_save_value_string(value->name));
      qa_save_value *args = qa_save_value_array();
      if (!args) qa_range_error("out of memory");
      for (size_t i = 0; i < value->arg_count; i++)
        push(args, qa_save_value_string(value->args[i]));
      set(out, "args", args);
      break;
    }
    case QA_APP_UNIFIED_USERINFO:
      set(out, "value", qa_save_value_string(value->value));
      break;
    case QA_APP_UNIFIED_DISCONNECT: break;
  }
  return out;
}
uint8_t *qa_app_unified_encode_control(const qa_app_unified_control *value, size_t *out_length) {
  return encode_envelope("qts-control", write_control(value), out_length);
}
void qa_app_unified_control_free(qa_app_unified_control *control) {
  qa_app_unified_composition_destroy(control->composition);
  free(control->resources);
  free(control->payload);
  free(control->simulation);
  if (control->has_update) qa_app_unified_component_update_free(&control->update);
  free(control->args);
  memset(control, 0, sizeof *control);
}
qa_app_unified_control qa_app_unified_decode_control(const uint8_t *bytes, size_t length) {
  qa_save_value *tree = NULL;
  qa_save_reader r = envelope(bytes, length, "qts-control", 4 * 1024 * 1024, &tree);
  static const char *kinds[] = {"offer", "ready", "admitted", "resources", "events",
    "components", "component-command", "command", "userinfo", "disconnect"};
  qa_save_reader kind_reader = field_of(&r, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 10);
  qa_save_reader_destroy(&kind_reader);
  qa_app_unified_control out;
  memset(&out, 0, sizeof out);
  if (strcmp(kind, "disconnect") == 0) {
    out.kind = QA_APP_UNIFIED_DISCONNECT;
    out.reason = field_string(&r, "reason", 1024);
    qa_save_reader_destroy(&r);
    /* Strings borrow the tree, retained for the caller's lifetime. */
    (void)tree;
    return out;
  }
  qa_save_reader epoch_reader = field_of(&r, "epoch");
  double epoch = bounded(&epoch_reader, 1, 4294967295.0);
  qa_save_reader_destroy(&epoch_reader);
  out.epoch = epoch;
  if (strcmp(kind, "offer") == 0) {
    out.kind = QA_APP_UNIFIED_OFFER;
    qa_save_reader composition = field_of(&r, "composition");
    out.composition = qa_app_unified_read_composition(composition.value);
    qa_save_reader_destroy(&composition);
    static const char *modes[] = {"singleplayer", "coop", "deathmatch"};
    qa_save_reader mode = field_of(&r, "mode");
    out.mode = qa_save_reader_choice_string(&mode, modes, 3);
    qa_save_reader_destroy(&mode);
    qa_save_reader max_clients = field_of(&r, "maxClients");
    out.max_clients = bounded(&max_clients, 1, 256);
    qa_save_reader_destroy(&max_clients);
  } else if (strcmp(kind, "ready") == 0) {
    out.kind = QA_APP_UNIFIED_READY;
    qa_save_reader composition = field_of(&r, "composition");
    out.composition_digest = qa_read_digest(&composition);
    qa_save_reader_destroy(&composition);
    out.userinfo = field_string(&r, "userinfo", 8192);
  } else if (strcmp(kind, "admitted") == 0) {
    out.kind = QA_APP_UNIFIED_ADMITTED;
    qa_save_reader client = field_of(&r, "client");
    out.client = read_actor_ref(&client);
    qa_save_reader_destroy(&client);
    qa_save_reader actor_reader = field_of(&r, "actor");
    out.actor = read_actor_ref(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    qa_save_reader source = field_of(&r, "sourceEntity");
    out.source_entity = (double)qa_save_reader_integer_min(&source, 0);
    qa_save_reader_destroy(&source);
  } else if (strcmp(kind, "resources") == 0) {
    out.kind = QA_APP_UNIFIED_RESOURCES;
    qa_save_reader resources_reader = field_of(&r, "resources");
    size_t count = qa_save_reader_list_count(&resources_reader);
    qa_app_unified_resource_key *resources = xmalloc(count * sizeof *resources);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&resources_reader, i, &item)) qa_range_error("out of memory");
      resources[i] = read_key(&item);
      qa_save_reader_destroy(&item);
    }
    if (count > 32768) qa_save_reader_fail(&resources_reader, "too many resource declarations");
    out.resources = resources;
    out.resource_count = count;
    qa_save_reader_destroy(&resources_reader);
  } else if (strcmp(kind, "events") == 0) {
    out.kind = QA_APP_UNIFIED_EVENTS;
    qa_save_reader frame = field_of(&r, "frame");
    out.frame = (double)qa_save_reader_integer_min(&frame, 0);
    qa_save_reader_destroy(&frame);
    qa_save_reader payload = field_of(&r, "payload");
    const uint8_t *payload_bytes = qa_save_reader_bytes(&payload, &out.payload_length);
    out.payload = xmalloc(out.payload_length);
    memcpy(out.payload, payload_bytes, out.payload_length);
    qa_save_reader_destroy(&payload);
    qa_save_reader simulation = field_of(&r, "simulation");
    const uint8_t *simulation_bytes = qa_save_reader_bytes(&simulation, &out.simulation_length);
    out.simulation = xmalloc(out.simulation_length);
    memcpy(out.simulation, simulation_bytes, out.simulation_length);
    qa_save_reader_destroy(&simulation);
  } else if (strcmp(kind, "components") == 0) {
    out.kind = QA_APP_UNIFIED_COMPONENTS;
    qa_save_reader update = field_of(&r, "update");
    out.update = qa_app_unified_read_component_update(&update);
    out.has_update = true;
    qa_save_reader_destroy(&update);
  } else if (strcmp(kind, "component-command") == 0) {
    out.kind = QA_APP_UNIFIED_COMPONENT_COMMAND;
    qa_save_reader args_reader = field_of(&r, "args");
    size_t count = qa_save_reader_list_count(&args_reader);
    const char **args = xmalloc(count * sizeof *args);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&args_reader, i, &item)) qa_range_error("out of memory");
      args[i] = protocol_string(&item, 8192);
      qa_save_reader_destroy(&item);
    }
    if (count < 1 || count > 128) qa_save_reader_fail(&args_reader, "invalid component command");
    out.args = args;
    out.arg_count = count;
    qa_save_reader_destroy(&args_reader);
    qa_save_reader owner = field_of(&r, "owner");
    out.owner = qa_app_unified_read_component_owner(&owner);
    qa_save_reader_destroy(&owner);
    qa_save_reader generation = field_of(&r, "generation");
    out.generation = bounded(&generation, 0, 9007199254740991.0);
    qa_save_reader_destroy(&generation);
  } else if (strcmp(kind, "command") == 0) {
    out.kind = QA_APP_UNIFIED_COMMAND;
    const char *name = field_string(&r, "name", 128);
    qa_save_reader args_reader = field_of(&r, "args");
    size_t count = qa_save_reader_list_count(&args_reader);
    const char **args = xmalloc(count * sizeof *args);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&args_reader, i, &item)) qa_range_error("out of memory");
      args[i] = protocol_string(&item, 8192);
      qa_save_reader_destroy(&item);
    }
    bool name_ok = name[0] != '\0'
        && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z')
            || name[0] == '_' || name[0] == '+');
    for (size_t i = 1; name_ok && name[i]; i++) {
      char ch = name[i];
      name_ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
          || (ch >= '0' && ch <= '9') || ch == '_' || ch == '+' || ch == '-';
    }
    if (!name_ok || count > 128) qa_save_reader_fail(&r, "invalid command");
    out.name = name;
    out.args = args;
    out.arg_count = count;
    qa_save_reader_destroy(&args_reader);
  } else if (strcmp(kind, "userinfo") == 0) {
    out.kind = QA_APP_UNIFIED_USERINFO;
    out.value = field_string(&r, "value", 8192);
  }
  qa_save_reader_destroy(&r);
  (void)tree;
  return out;
}
void qa_app_unified_input_batch_free(qa_app_unified_input_batch *batch) {
  free(batch->commands);
  memset(batch, 0, sizeof *batch);
}
uint8_t *qa_app_unified_encode_inputs(const qa_app_unified_input_batch *value, size_t *out_length) {
  qa_save_value *out = object();
  set(out, "epoch", qa_save_value_number(value->epoch));
  qa_save_value *commands = qa_save_value_array();
  if (!commands) qa_range_error("out of memory");
  for (size_t i = 0; i < value->command_count; i++) {
    qa_save_value *entry = object();
    set(entry, "sequence", qa_save_value_number(value->commands[i].sequence));
    set(entry, "command", write_command(&value->commands[i].command));
    if (value->commands[i].has_arsenal)
      set(entry, "arsenal", write_arsenal(&value->commands[i].arsenal));
    push(commands, entry);
  }
  set(out, "commands", commands);
  return encode_envelope("qts-input", out, out_length);
}
qa_app_unified_input_batch qa_app_unified_decode_inputs(const uint8_t *bytes, size_t length) {
  qa_save_value *tree = NULL;
  qa_save_reader r = envelope(bytes, length, "qts-input", 65536, &tree);
  qa_save_reader commands_reader = field_of(&r, "commands");
  size_t count = qa_save_reader_list_count(&commands_reader);
  qa_app_unified_input *commands = xmalloc(count * sizeof *commands);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&commands_reader, i, &item)) qa_range_error("out of memory");
    memset(&commands[i], 0, sizeof commands[i]);
    qa_save_reader sequence = field_of(&item, "sequence");
    commands[i].sequence = (double)qa_save_reader_integer_min(&sequence, 0);
    qa_save_reader_destroy(&sequence);
    qa_save_reader command = field_of(&item, "command");
    commands[i].command = read_command(&command);
    qa_save_reader_destroy(&command);
    qa_save_reader arsenal = field_of(&item, "arsenal");
    if (!qa_save_reader_is_undefined(&arsenal)) {
      commands[i].has_arsenal = true;
      commands[i].arsenal = read_arsenal(&arsenal);
    }
    qa_save_reader_destroy(&arsenal);
    qa_save_reader_destroy(&item);
  }
  if (count > 64) qa_save_reader_fail(&commands_reader, "too many input commands");
  qa_save_reader_destroy(&commands_reader);
  qa_save_reader epoch_reader = field_of(&r, "epoch");
  double epoch = bounded(&epoch_reader, 1, 4294967295.0);
  qa_save_reader_destroy(&epoch_reader);
  qa_save_reader_destroy(&r);
  (void)tree;
  qa_app_unified_input_batch batch = {epoch, commands, count};
  return batch;
}
uint8_t *qa_app_unified_encode_handshake(const qa_app_unified_handshake *value, size_t *out_length) {
  qa_save_value *out = object();
  static const char *names[] = {"hello", "challenge", "connect"};
  set(out, "kind", qa_save_value_string(names[value->kind]));
  set(out, "nonce", qa_save_value_string(value->nonce));
  if (value->kind != QA_APP_UNIFIED_HELLO)
    set(out, "token", qa_save_value_string(value->token));
  return encode_envelope("qts-connect", out, out_length);
}
bool qa_app_unified_decode_handshake(const uint8_t *bytes, size_t length,
    qa_app_unified_handshake *out) {
  if (length > 512) return false;
  /* TS catches every decode failure into null; the C readers abort,
   * so pre-validate the envelope shape before reading fields. */
  qa_save_value *tree = qa_decode_checkpoint_value(bytes, length);
  if (!tree) return false;
  const qa_save_value *schema = qa_save_value_get(tree, "schema");
  const qa_save_value *version = qa_save_value_get(tree, "version");
  const qa_save_value *value = qa_save_value_get(tree, "value");
  bool ok = schema && schema->kind == QA_SAVE_STRING
      && strcmp(schema->text, "qts-connect") == 0
      && version && version->kind == QA_SAVE_NUMBER && version->number == 1
      && value && value->kind == QA_SAVE_OBJECT;
  const qa_save_value *kind_value = ok ? qa_save_value_get(value, "kind") : NULL;
  const qa_save_value *nonce_value = ok ? qa_save_value_get(value, "nonce") : NULL;
  ok = ok && kind_value && kind_value->kind == QA_SAVE_STRING
      && nonce_value && nonce_value->kind == QA_SAVE_STRING;
  qa_app_unified_handshake_kind kind = QA_APP_UNIFIED_HELLO;
  if (ok) {
    if (strcmp(kind_value->text, "hello") == 0) kind = QA_APP_UNIFIED_HELLO;
    else if (strcmp(kind_value->text, "challenge") == 0) kind = QA_APP_UNIFIED_CHALLENGE;
    else if (strcmp(kind_value->text, "connect") == 0) kind = QA_APP_UNIFIED_CONNECT;
    else ok = false;
  }
  if (!ok) { qa_save_value_free(tree); return false; }
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, (qa_save_value *)value, "qts-connect")) {
    qa_save_value_free(tree);
    return false;
  }
  qa_save_reader nonce_reader = field_of(&reader, "nonce");
  /* Token validation aborts on malformed hex (TS would return null);
   * pre-check the shape first. */
  const char *nonce_text = nonce_reader.value && nonce_reader.value->kind == QA_SAVE_STRING
      ? nonce_reader.value->text : "";
  bool hex_ok = strlen(nonce_text) == 32;
  for (size_t i = 0; hex_ok && i < 32; i++) {
    char c = nonce_text[i];
    hex_ok = (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
  }
  const qa_save_value *token_value = kind == QA_APP_UNIFIED_HELLO
      ? NULL : qa_save_value_get(value, "token");
  if (kind != QA_APP_UNIFIED_HELLO) {
    hex_ok = hex_ok && token_value && token_value->kind == QA_SAVE_STRING
        && strlen(token_value->text) == 32;
    for (size_t i = 0; hex_ok && i < 32; i++) {
      char c = token_value->text[i];
      hex_ok = (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
    }
  }
  if (!hex_ok) {
    qa_save_reader_destroy(&nonce_reader);
    qa_save_reader_destroy(&reader);
    qa_save_value_free(tree);
    return false;
  }
  out->kind = kind;
  out->nonce = read_token(&nonce_reader);
  qa_save_reader_destroy(&nonce_reader);
  out->token = NULL;
  if (kind != QA_APP_UNIFIED_HELLO) {
    qa_save_reader token_reader = field_of(&reader, "token");
    out->token = read_token(&token_reader);
    qa_save_reader_destroy(&token_reader);
  }
  qa_save_reader_destroy(&reader);
  (void)tree;
  return true;
}
