/* Port of src/network/common/commands.ts. GPL-2.0-or-later. */
#include "qa/network/common/commands.h"
#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const qa_user_command_kind NAMES[5] = {
  QA_USER_COMMAND_Q1, QA_USER_COMMAND_QW, QA_USER_COMMAND_Q2, QA_USER_COMMAND_Q2_RERELEASE, QA_USER_COMMAND_Q3
};
static int kind_index(qa_user_command_kind k) {
  for (int i = 0; i < 5; i++) if (NAMES[i] == k) return i;
  return -1;
}
static void check_identifier(const char *value) {
  const char *colon = strchr(value, ':');
  size_t len = strlen(value);
  if (!colon || colon == value || colon == value + len - 1)
    qa_range_error("Unified command identifier must have a namespace and name");
}
static const char *string_bytes(const char *value, size_t *out_len) {
  check_identifier(value);
  size_t len = strlen(value);
  if (len > 65535) qa_range_error("Unified command identifier is too long");
  *out_len = len; return value;
}
static double read_number(qa_binary_reader *reader) {
  double v = qa_binary_reader_f64(reader);
  if (!isfinite(v)) qa_range_error("Unified command contains a non-finite number");
  return v;
}
static qa_vec3 read_vector(qa_binary_reader *reader) {
  qa_vec3 v; v.x = read_number(reader); v.y = read_number(reader); v.z = read_number(reader); return v;
}
static size_t fields_of(const qa_user_command *command, double values[10]) {
  switch (command->kind) {
    case QA_USER_COMMAND_Q1: {
      const qa_q1_user_command *c = &command->as.q1;
      double v[9] = { c->acknowledged_server_time_seconds, c->view_angles.x, c->view_angles.y, c->view_angles.z,
                      c->forward_move, c->side_move, c->up_move, c->buttons, c->impulse };
      memcpy(values, v, sizeof v); return 9;
    }
    case QA_USER_COMMAND_QW: {
      const qa_qw_user_command *c = &command->as.qw;
      double v[9] = { c->milliseconds, c->angles.x, c->angles.y, c->angles.z,
                      c->forward_move, c->side_move, c->up_move, c->buttons, c->impulse };
      memcpy(values, v, sizeof v); return 9;
    }
    case QA_USER_COMMAND_Q2: {
      const qa_q2_user_command *c = &command->as.q2;
      double v[10] = { c->milliseconds, c->angle_shorts.values[0], c->angle_shorts.values[1], c->angle_shorts.values[2],
                       c->forward_move, c->side_move, c->up_move, c->buttons, c->impulse, c->light_level };
      memcpy(values, v, sizeof v); return 10;
    }
    case QA_USER_COMMAND_Q2_RERELEASE: {
      const qa_q2_rerelease_user_command *c = &command->as.q2_rerelease;
      double v[8] = { c->milliseconds, c->angles.x, c->angles.y, c->angles.z,
                      c->forward_move, c->side_move, c->buttons, c->server_frame };
      memcpy(values, v, sizeof v); return 8;
    }
    case QA_USER_COMMAND_Q3: {
      const qa_q3_user_command *c = &command->as.q3;
      double v[9] = { c->server_time_milliseconds, c->angle_words.values[0], c->angle_words.values[1], c->angle_words.values[2],
                      c->buttons, c->weapon, c->forward_move, c->right_move, c->up_move };
      memcpy(values, v, sizeof v); return 9;
    }
  }
  return 0;
}
static qa_user_command read_movement(qa_binary_reader *reader) {
  unsigned idx = qa_binary_reader_u8(reader);
  qa_user_command cmd; memset(&cmd, 0, sizeof cmd);
  if (idx >= 5) qa_range_error("Unknown unified movement command dialect");
  cmd.kind = NAMES[idx];
  switch (cmd.kind) {
    case QA_USER_COMMAND_Q1: {
      qa_q1_user_command *c = &cmd.as.q1;
      c->acknowledged_server_time_seconds = read_number(reader);
      c->view_angles = read_vector(reader);
      c->forward_move = read_number(reader); c->side_move = read_number(reader); c->up_move = read_number(reader);
      c->buttons = read_number(reader); c->impulse = read_number(reader);
      break;
    }
    case QA_USER_COMMAND_QW: {
      qa_qw_user_command *c = &cmd.as.qw;
      c->milliseconds = read_number(reader); c->angles = read_vector(reader);
      c->forward_move = read_number(reader); c->side_move = read_number(reader); c->up_move = read_number(reader);
      c->buttons = read_number(reader); c->impulse = read_number(reader);
      break;
    }
    case QA_USER_COMMAND_Q2: {
      qa_q2_user_command *c = &cmd.as.q2;
      c->milliseconds = read_number(reader);
      c->angle_shorts.values[0] = read_number(reader); c->angle_shorts.values[1] = read_number(reader); c->angle_shorts.values[2] = read_number(reader);
      c->forward_move = read_number(reader); c->side_move = read_number(reader); c->up_move = read_number(reader);
      c->buttons = read_number(reader); c->impulse = read_number(reader); c->light_level = read_number(reader);
      break;
    }
    case QA_USER_COMMAND_Q2_RERELEASE: {
      qa_q2_rerelease_user_command *c = &cmd.as.q2_rerelease;
      c->milliseconds = read_number(reader); c->angles = read_vector(reader);
      c->forward_move = read_number(reader); c->side_move = read_number(reader);
      c->buttons = read_number(reader); c->server_frame = read_number(reader);
      break;
    }
    case QA_USER_COMMAND_Q3: {
      qa_q3_user_command *c = &cmd.as.q3;
      c->server_time_milliseconds = read_number(reader);
      c->angle_words.values[0] = read_number(reader); c->angle_words.values[1] = read_number(reader); c->angle_words.values[2] = read_number(reader);
      c->buttons = read_number(reader); c->weapon = read_number(reader);
      c->forward_move = read_number(reader); c->right_move = read_number(reader); c->up_move = read_number(reader);
      break;
    }
  }
  return cmd;
}
static bool is_safe_int(double v) { return isfinite(v) && v == trunc(v) && v >= -9007199254740991.0 && v <= 9007199254740991.0; }
uint8_t *qa_encode_unified_actor_command(const qa_actor_command *input, size_t *out_len) {
  if (!is_safe_int(input->sequence) || input->sequence < 0)
    qa_range_error("Unified command sequence must be a nonnegative safe integer");
  double values[10]; size_t nvalues = fields_of(&input->command, values);
  const qa_arsenal_intent *intent = input->arsenal;
  size_t provider_len = 0, weapon_len = 0;
  const char *provider = NULL, *weapon = NULL;
  if (intent) {
    provider = string_bytes(intent->provider, &provider_len);
    if (intent->weapon) weapon = string_bytes(intent->weapon, &weapon_len);
  }
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)(32 + nvalues * 8 + provider_len + weapon_len))) return NULL;
  static const uint8_t magic[4] = { 'Q', 'T', 'C', 'M' };
  qa_binary_writer_bytes(&writer, magic, 4);
  qa_binary_writer_u16(&writer, 1);
  qa_binary_writer_u32(&writer, input->actor.slot);
  qa_binary_writer_u32(&writer, input->actor.generation);
  qa_binary_writer_f64(&writer, input->sequence);
  qa_binary_writer_u8(&writer, kind_index(input->command.kind));
  for (size_t i = 0; i < nvalues; i++) {
    if (!isfinite(values[i])) { qa_binary_writer_destroy(&writer); qa_range_error("Unified command contains a non-finite number"); }
    qa_binary_writer_f64(&writer, values[i]);
  }
  qa_binary_writer_u8(&writer, intent ? 1 : 0);
  if (intent && provider) {
    qa_binary_writer_u16(&writer, (int64_t)provider_len);
    qa_binary_writer_bytes(&writer, (const uint8_t *)provider, provider_len);
    qa_binary_writer_u8(&writer, weapon ? 1 : 0);
    if (weapon) {
      qa_binary_writer_u16(&writer, (int64_t)weapon_len);
      qa_binary_writer_bytes(&writer, (const uint8_t *)weapon, weapon_len);
    }
    qa_binary_writer_u8(&writer, intent->use_holdable ? 1 : 0);
  }
  uint8_t *out = qa_binary_writer_finish(&writer, out_len);
  qa_binary_writer_destroy(&writer);
  return out;
}
void qa_decoded_actor_command_destroy(qa_decoded_actor_command *d) {
  free(d->provider); free(d->weapon); memset(d, 0, sizeof *d);
}
static char *read_identifier(qa_binary_reader *reader) {
  uint16_t len = qa_binary_reader_u16(reader);
  uint8_t *raw = qa_binary_reader_bytes(reader, len);
  char *text = malloc((size_t)len + 1);
  if (len) memcpy(text, raw, len);
  text[len] = '\0'; free(raw);
  /* TS fatal UTF-8 decode: reject invalid sequences (simplified check). */
  for (size_t i = 0; i < (size_t)len;) {
    uint8_t b = (uint8_t)text[i];
    size_t need = b < 0x80 ? 1 : b >= 0xc2 && b < 0xe0 ? 2 : b >= 0xe0 && b < 0xf0 ? 3 : b >= 0xf0 && b < 0xf5 ? 4 : 0;
    bool bad = need == 0 || i + need > (size_t)len;
    for (size_t j = 1; !bad && j < need; j++) if (((uint8_t)text[i+j] & 0xc0) != 0x80) bad = true;
    if (bad) { free(text); qa_range_error("The encoded data was not valid for encoding utf-8"); }
    i += need;
  }
  check_identifier(text);
  return text;
}
static bool same_session(qa_session_id a, qa_session_id b) { return a.token == b.token; }
void qa_decode_unified_actor_command(const uint8_t *bytes, size_t length,
                                     const qa_unified_command_receiver *receiver,
                                     qa_decoded_actor_command *out) {
  memset(out, 0, sizeof *out);
  qa_binary_reader reader;
  /* Reader borrows storage; bytes are const here but never mutated. */
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, "unified-command"))
    qa_range_error("Unified command identifier must have a namespace and name");
  qa_binary_reader_expect_magic(&reader, "QTCM");
  if (qa_binary_reader_u16(&reader) != 1) qa_range_error("Unsupported unified command version");
  uint32_t slot = qa_binary_reader_u32(&reader), generation = qa_binary_reader_u32(&reader);
  double sequence = read_number(&reader);
  if (!is_safe_int(sequence) || sequence < 0) qa_range_error("Invalid unified command sequence");
  qa_user_command command = read_movement(&reader);
  unsigned has_intent = qa_binary_reader_u8(&reader);
  if (has_intent == 1) {
    out->has_intent = true;
    out->provider = read_identifier(&reader);
    unsigned has_weapon = qa_binary_reader_u8(&reader);
    if (has_weapon != 0 && has_weapon != 1) qa_range_error("Invalid unified weapon selection tag");
    out->weapon = has_weapon == 0 ? NULL : read_identifier(&reader);
    unsigned use_holdable = qa_binary_reader_u8(&reader);
    if (use_holdable != 0 && use_holdable != 1) qa_range_error("Invalid unified holdable state");
    out->intent.provider = out->provider;
    out->intent.weapon = out->weapon;
    out->intent.use_holdable = use_holdable == 1;
    out->intent.has_impulse = false; out->intent.impulse = 0;
  } else if (has_intent != 0) qa_range_error("Invalid unified arsenal intent tag");
  if (qa_binary_reader_remaining(&reader) != 0) qa_range_error("Trailing unified command bytes");
  qa_unified_command_actor controlled;
  int found = receiver->resolve_controlled_actor(receiver->ctx, slot, generation, &controlled);
  if (!found || (double)controlled.actor.slot != (double)slot || (double)controlled.actor.generation != (double)generation)
    qa_range_error("Unified command actor is stale or not controlled by this source");
  qa_command_source source = receiver->source;
  bool session_mismatch = false;
  if (source.kind != QA_COMMAND_SOURCE_BOT && !same_session(controlled.actor.session, source.client.session)) session_mismatch = true;
  if (source.kind == QA_COMMAND_SOURCE_LOCAL_SEAT && !same_session(controlled.actor.session, source.seat.session)) session_mismatch = true;
  if (session_mismatch) qa_range_error("Unified command source belongs to another session");
  if (command.kind != controlled.movement) qa_range_error("Unified movement dialect differs from the selected actor provider");
  if (out->has_intent && strcmp(out->provider, controlled.arsenal) != 0)
    qa_range_error("Unified arsenal intent belongs to another provider");
  out->command.actor = controlled.actor;
  out->command.source = source;
  out->command.sequence = sequence;
  out->command.command = command;
  out->command.angle_space = NULL;
  out->command.arsenal = out->has_intent ? &out->intent : NULL;
  qa_binary_reader_destroy(&reader);
}
