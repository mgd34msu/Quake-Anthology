/*
 * Port of src/network/q2/codecs/codec.ts: the ProtocolCodec interface
 * and its parameter/result shapes. C accommodations:
 *  - The interface becomes a vtable (qa_q2_protocol_codec) bound to a
 *    net_message at context creation; optional TS methods are NULL.
 *  - Optional TS fields carry has_* flags; strings are borrowed on
 *    write and owned char[2048] buffers on read (TS strings here come
 *    from MSG_ReadString, capped at 2047 chars).
 *  - readDeltaEntity bits travel as uint64 (Q2PRO exceeds 32 bits; TS
 *    carries the same value as an exact double/BigInt).
 *  - writeFrame's writeEntities closure becomes fn + ctx.
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CODECS_CODEC_H
#define QA_NETWORK_Q2_CODECS_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/message.h"
#include "qa/network/q2/state.h"

/* One TS ClcBatchMoveFrameT: cmds array (owned on read). */
typedef struct qa_q2_clc_batch_move_frame {
  qa_q2_wire_usercmd *cmds;
  size_t cmd_count;
} qa_q2_clc_batch_move_frame;

/* TS ServerDataParamsT. */
typedef struct qa_q2_server_data_params {
  bool has_server_fps;
  double server_fps;
  bool has_wire_flags;
  double wire_flags;
  bool has_protocol_revision;
  double protocol_revision;
  double servercount;
  bool attractloop;
  const char *gamedir;
  double clientnum;
  bool has_clientnums;
  const double *clientnums;
  size_t clientnum_count;
  const char *levelname;
  double server_state;
  bool has_r1q2_version;
  double r1q2_version;
  bool has_r1q2_strafejump_hack;
  bool r1q2_strafejump_hack;
  bool has_q2pro_version;
  double q2pro_version;
  bool has_q2pro_strafejump_hack;
  bool q2pro_strafejump_hack;
  bool has_q2pro_qw_mode;
  bool q2pro_qw_mode;
  bool has_q2pro_waterjump_hack;
  bool q2pro_waterjump_hack;
} qa_q2_server_data_params;

/* TS FrameWriteParamsT (psFrom NULL is TS null). */
typedef struct qa_q2_frame_write_params {
  double framenum;
  double lastframe;
  double surpress_count;
  const uint8_t *areabits;
  size_t areabits_length;
  double areabytes;
  const qa_q2_wire_player *ps_from;
  const qa_q2_wire_player *ps_to;
} qa_q2_frame_write_params;

/* TS FrameHeaderT. */
typedef struct qa_q2_frame_header {
  double areabytes;
  double serverframe;
  double deltaframe;
  double surpress_count;
} qa_q2_frame_header;

#define QA_Q2_SERVER_DATA_CLIENTNUMS_MAX 256

/* TS ServerDataReadResultT. */
typedef struct qa_q2_server_data_result {
  bool has_server_fps;
  double server_fps;
  bool has_wire_flags;
  double wire_flags;
  bool has_protocol_revision;
  double protocol_revision;
  double servercount;
  bool attractloop;
  char gamedir[2048];
  double clientnum;
  bool has_clientnums;
  double clientnums[QA_Q2_SERVER_DATA_CLIENTNUMS_MAX];
  size_t clientnum_count;
  char levelname[2048];
  double server_state;
  bool has_r1q2_version;
  double r1q2_version;
  bool has_r1q2_strafejump_hack;
  bool r1q2_strafejump_hack;
  bool has_q2pro_version;
  double q2pro_version;
  bool has_q2pro_strafejump_hack;
  bool q2pro_strafejump_hack;
  bool has_q2pro_qw_mode;
  bool q2pro_qw_mode;
  bool has_q2pro_waterjump_hack;
  bool q2pro_waterjump_hack;
} qa_q2_server_data_result;

/* TS ClcBatchMoveT (frames owned; free with qa_q2_clc_batch_move_free). */
typedef struct qa_q2_clc_batch_move {
  double lastframe;
  double num_dups;
  qa_q2_clc_batch_move_frame *frames;
  size_t frame_count;
} qa_q2_clc_batch_move;

void qa_q2_clc_batch_move_free(qa_q2_clc_batch_move *move);

/* TS ClcUserinfoDeltaT / ClcClientSettingT. */
typedef struct qa_q2_clc_userinfo_delta {
  char name[2048];
  char value[2048];
} qa_q2_clc_userinfo_delta;
typedef struct qa_q2_clc_client_setting {
  double index;
  double value;
} qa_q2_clc_client_setting;

/* TS readEntityBits result. */
typedef struct qa_q2_entity_bits {
  double number;
  double bits; /* TS number form */
  uint64_t bits_u64;
} qa_q2_entity_bits;

typedef struct qa_q2_protocol_codec qa_q2_protocol_codec;
typedef void (*qa_q2_write_entities_fn)(void *ctx, qa_q2_sizebuf *msg);

/* TS ProtocolCodec. Every fn receives the codec (self) first. */
struct qa_q2_protocol_codec {
  const char *name;
  qa_q2_sizebuf *net_message;
  void *ctx;
  bool has_clc_move_checksum;
  bool clc_move_has_checksum;
  void (*write_server_data)(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                            const qa_q2_server_data_params *params);
  void (*write_delta_entity)(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                             const qa_q2_wire_entity *from,
                             const qa_q2_wire_entity *to, bool force,
                             bool newentity);
  void (*write_entity_remove)(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                              double oldnum);
  void (*write_packet_entities_end)(qa_q2_protocol_codec *self,
                                    qa_q2_sizebuf *msg);
  void (*write_spawn_baseline)(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                               const qa_q2_wire_entity *base);
  void (*write_player_state_delta)(qa_q2_protocol_codec *self,
                                   qa_q2_sizebuf *msg,
                                   const qa_q2_wire_player *from,
                                   const qa_q2_wire_player *to);
  void (*write_packet_entities_begin)(qa_q2_protocol_codec *self,
                                      qa_q2_sizebuf *msg);
  void (*write_frame)(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                      const qa_q2_frame_write_params *params,
                      qa_q2_write_entities_fn write_entities, void *ctx);
  void (*write_delta_usercmd)(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                              const qa_q2_wire_usercmd *from,
                              const qa_q2_wire_usercmd *cmd);
  void (*read_delta_usercmd)(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                             const qa_q2_wire_usercmd *from,
                             qa_q2_wire_usercmd *move);
  void (*read_server_data)(qa_q2_protocol_codec *self,
                           qa_q2_server_data_result *out);
  void (*read_entity_bits)(qa_q2_protocol_codec *self, qa_q2_entity_bits *out);
  void (*read_delta_entity)(qa_q2_protocol_codec *self,
                            const qa_q2_wire_entity *from,
                            qa_q2_wire_entity *to, double number,
                            uint64_t bits);
  void (*read_player_state_delta)(qa_q2_protocol_codec *self,
                                  qa_q2_sizebuf *msg,
                                  const qa_q2_wire_player *from,
                                  qa_q2_wire_player *to);
  void (*read_frame_header)(qa_q2_protocol_codec *self, uint8_t *areabits,
                            size_t areabits_length, bool read_suppress_byte,
                            qa_q2_frame_header *out);
  void (*read_frame_playerstate)(qa_q2_protocol_codec *self,
                                 const qa_q2_wire_player *from,
                                 qa_q2_wire_player *to);
  void (*read_packet_entities_begin)(qa_q2_protocol_codec *self);
  /* Optional in TS (NULL when absent). read_batch_move fills an owned
   * move; false only on OOM. */
  bool (*read_batch_move)(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                          bool nodelta, double opcode_extra,
                          qa_q2_clc_batch_move *out);
  void (*write_batch_move)(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                           bool has_lastframe, double lastframe,
                           const qa_q2_clc_batch_move_frame *frames,
                           size_t frame_count);
  void (*read_userinfo_delta)(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                              qa_q2_clc_userinfo_delta *out);
  void (*read_client_setting)(qa_q2_protocol_codec *self, qa_q2_sizebuf *msg,
                              qa_q2_clc_client_setting *out);
};

#endif
