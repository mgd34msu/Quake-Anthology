/*
 * Port of src/network/q2/server-messages.ts (Q2ServerMessageReader).
 * Records are malloc'd lists (free helper); frame events hold a deep
 * clone of the history frame; private opcodes decode through
 * fn + ctx (the TS Q2UnboundServerMessage throw becomes the abort
 * helper with the same message). The streaming deflate download
 * re-inflates the joined buffer like TS (raw deflate via zlib).
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_SERVER_MESSAGES_H
#define QA_NETWORK_Q2_SERVER_MESSAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/network/q2/codec.h"
#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/codecs/kexdemo.h"
#include "qa/network/q2/fog.h"
#include "qa/network/q2/frames.h"
#include "qa/network/q2/message.h"
#include "qa/network/q2/temp_entities.h"

/* TS Q2UnboundServerMessage throw. */
noreturn void qa_q2_throw_unbound_server_message(double opcode,
                                                 qa_q2_protocol_identity protocol);

/* TS Q2SoundMessage (has_position false is TS null). */
typedef struct qa_q2_sound_message {
  double flags, index, entity, channel;
  bool has_position;
  qa_vec3 position;
  double volume, attenuation, delay_seconds;
} qa_q2_sound_message;

/* TS Q2ServerEvent kinds. */
typedef enum qa_q2_server_event_kind {
  QA_Q2_SERVER_NOP, QA_Q2_SERVER_DISCONNECT, QA_Q2_SERVER_RECONNECT,
  QA_Q2_SERVER_LEVEL_RESTART, QA_Q2_SERVER_DATA, QA_Q2_SERVER_PRINT,
  QA_Q2_SERVER_CENTER_PRINT, QA_Q2_SERVER_COMMAND_TEXT, QA_Q2_SERVER_LAYOUT,
  QA_Q2_SERVER_ACHIEVEMENT, QA_Q2_SERVER_CONFIG_STRING, QA_Q2_SERVER_BASELINE,
  QA_Q2_SERVER_FRAME, QA_Q2_SERVER_SOUND, QA_Q2_SERVER_TEMPORARY_ENTITY,
  QA_Q2_SERVER_MUZZLE_FLASH, QA_Q2_SERVER_INVENTORY, QA_Q2_SERVER_DOWNLOAD,
  QA_Q2_SERVER_SETTING, QA_Q2_SERVER_SEAT, QA_Q2_SERVER_DAMAGE,
  QA_Q2_SERVER_LOCALIZED_PRINT, QA_Q2_SERVER_FOG, QA_Q2_SERVER_POI,
  QA_Q2_SERVER_HELP_PATH, QA_Q2_SERVER_PRIVATE
} qa_q2_server_event_kind;

/* TS Q2ServerEvent. All pointer fields owned; invalid for other kinds. */
typedef struct qa_q2_server_event {
  qa_q2_server_event_kind kind;
  qa_q2_server_data_result data;
  double level; /* print */
  char *text; /* print/center/command/layout/achievement/config value */
  double index; /* config-string/setting */
  double value_number; /* setting */
  qa_q2_wire_entity entity; /* baseline */
  qa_q2_wire_frame frame; /* frame (deep storage) */
  qa_q2_sound_message sound;
  qa_q2_temp_entity temp;
  double flash_entity, flash;
  bool monster, silenced;
  double *counts; /* inventory (owned) */
  size_t count_count;
  double percent; /* download */
  bool has_bytes; /* download (false is TS null) */
  uint8_t *bytes; /* download/private payload (owned) */
  size_t bytes_length;
  double seat;
  qa_q2_kex_damage_indicator *indicators; /* owned */
  size_t indicator_count;
  qa_q2_kex_locprint localized;
  qa_q2_svc_fog_data fog;
  qa_q2_kex_poi poi;
  qa_q2_kex_help_path help_path;
  char *private_name; /* owned */
} qa_q2_server_event;

/* TS Q2ServerRecord. */
typedef struct qa_q2_server_record {
  double seat;
  double opcode;
  uint8_t *raw; /* owned */
  size_t raw_length;
  qa_q2_server_event event;
} qa_q2_server_record;

void qa_q2_server_records_free(qa_q2_server_record *records, size_t count);
/* Event-only free (used by writers building single events). */
void qa_q2_server_event_clear(qa_q2_server_event *event);

/* TS privateMessage callback: fills a private event. */
typedef void (*qa_q2_private_message_fn)(void *ctx, double opcode,
                                         qa_q2_sizebuf *message,
                                         qa_q2_protocol_identity protocol,
                                         qa_q2_server_event *out);

/* configStrings Map entry. */
typedef struct qa_q2_config_string_entry {
  double index;
  char *value; /* owned */
} qa_q2_config_string_entry;

/* One per-seat history entry. */
typedef struct qa_q2_seat_history {
  double seat;
  qa_q2_frame_history history;
} qa_q2_seat_history;

/* One latestFrames() row (frame borrowed from history). */
typedef struct qa_q2_latest_frame {
  double seat;
  qa_q2_wire_frame *frame;
} qa_q2_latest_frame;

typedef enum qa_q2_server_stream {
  QA_Q2_STREAM_NONE, QA_Q2_STREAM_CONFIG, QA_Q2_STREAM_BASELINE,
  QA_Q2_STREAM_GAMESTATE_CONFIG
} qa_q2_server_stream;

/* TS Q2ServerMessageOptions subset retained on the reader. */
typedef struct qa_q2_server_message_options {
  bool read_mode_demo; /* TS readMode "demo" */
  double max_config_strings;
  double inventory_slots;
  bool has_q2pro_extended_temp;
  bool q2pro_extended_temp;
  const int32_t *private_opcodes; /* borrowed set */
  size_t private_opcode_count;
  qa_q2_private_message_fn private_message;
  void *private_ctx;
} qa_q2_server_message_options;

/* TS Q2ServerMessageReader. */
typedef struct qa_q2_server_message_reader {
  qa_q2_wire_codec wire;
  qa_q2_server_message_options options;
  qa_q2_config_string_entry *config_strings; /* owned */
  size_t config_count, config_capacity;
  qa_q2_seat_history *histories; /* owned */
  size_t history_count, history_capacity;
  qa_q2_baseline_list baselines;
  double selected_seat;
  bool legacy_demo26;
  qa_q2_server_stream stream;
  uint8_t *compressed_download; /* owned */
  size_t compressed_download_length;
  size_t inflated_download_bytes;
} qa_q2_server_message_reader;

/* False only on OOM; invalid limits abort. */
bool qa_q2_server_message_reader_init(qa_q2_server_message_reader *reader,
                                      qa_q2_protocol_identity protocol,
                                      qa_q2_server_message_options options);
void qa_q2_server_message_reader_destroy(qa_q2_server_message_reader *reader);
double qa_q2_server_message_reader_get_seat(const qa_q2_server_message_reader *reader);
/* TS acceptDecoded / history(seat) / reset / read. read false is OOM. */
void qa_q2_server_message_reader_accept_decoded(
    qa_q2_server_message_reader *reader, qa_q2_server_record *records, size_t count);
qa_q2_frame_history *qa_q2_server_message_reader_get_history(
    qa_q2_server_message_reader *reader, double seat);
qa_q2_frame_history *qa_q2_server_message_reader_get_history_default(
    qa_q2_server_message_reader *reader);
void qa_q2_server_message_reader_reset(qa_q2_server_message_reader *reader);
bool qa_q2_server_message_reader_read(qa_q2_server_message_reader *reader,
                                      const uint8_t *bytes, size_t length,
                                      qa_q2_server_record **out, size_t *out_count);
/* TS latestFrames generator, collected (frames borrowed). */
bool qa_q2_server_message_reader_latest_frames(qa_q2_server_message_reader *reader,
                                               qa_q2_latest_frame **out,
                                               size_t *out_count);
/* configStrings Map access (NULL when absent). */
const char *qa_q2_server_message_reader_config_string(
    const qa_q2_server_message_reader *reader, double index);

#endif
