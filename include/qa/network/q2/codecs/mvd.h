/*
 * Port of src/network/q2/codecs/mvd.ts. TS nullable player arguments
 * become NULL pointers; read results carry the decoded player by
 * value. GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CODECS_MVD_H
#define QA_NETWORK_Q2_CODECS_MVD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/message.h"
#include "qa/network/q2/mvd_profile.h"
#include "qa/network/q2/state.h"

#define QA_Q2_MVD_MAGIC 0x3244564du
#define QA_Q2_PROTOCOL_VERSION_MVD 37
#define QA_Q2_PROTOCOL_VERSION_MVD_MINIMUM 2009
#define QA_Q2_PROTOCOL_VERSION_MVD_DEFAULT 2010
#define QA_Q2_PROTOCOL_VERSION_MVD_EXTENDED_LIMITS 2011
#define QA_Q2_PROTOCOL_VERSION_MVD_EXTENDED_LIMITS_2 2012
#define QA_Q2_PROTOCOL_VERSION_MVD_CURRENT 2013
#define QA_Q2_PROTOCOL_VERSION_MVD_RERELEASE 3038
#define QA_Q2_MVD_SVCMD_BITS 5
#define QA_Q2_MVD_SVCMD_MASK ((1 << QA_Q2_MVD_SVCMD_BITS) - 1)

/* TS mvd_* opcodes. */
typedef enum qa_q2_mvd_op {
  QA_Q2_MVD_BAD, QA_Q2_MVD_NOP, QA_Q2_MVD_DISCONNECT, QA_Q2_MVD_RECONNECT,
  QA_Q2_MVD_SERVERDATA, QA_Q2_MVD_CONFIGSTRING, QA_Q2_MVD_FRAME,
  QA_Q2_MVD_FRAME_NODELTA, QA_Q2_MVD_UNICAST, QA_Q2_MVD_UNICAST_R,
  QA_Q2_MVD_MULTICAST_ALL, QA_Q2_MVD_MULTICAST_PHS, QA_Q2_MVD_MULTICAST_PVS,
  QA_Q2_MVD_MULTICAST_ALL_R, QA_Q2_MVD_MULTICAST_PHS_R, QA_Q2_MVD_MULTICAST_PVS_R,
  QA_Q2_MVD_SOUND, QA_Q2_MVD_PRINT, QA_Q2_MVD_STUFFTEXT
} qa_q2_mvd_op;

/* TS MvdCmdT. */
typedef struct qa_q2_mvd_cmd {
  double op;
  double extrabits;
} qa_q2_mvd_cmd;

/* TS MSG_WriteMvdCmd / MSG_ReadMvdCmd. */
void qa_q2_msg_write_mvd_cmd(qa_q2_sizebuf *msg, double op, double extrabits);
void qa_q2_msg_read_mvd_cmd(qa_q2_sizebuf *msg, qa_q2_mvd_cmd *out);

#define QA_Q2_MVF_NOMSGS (1 << 0)
#define QA_Q2_MVF_EXTLIMITS (1 << 2)
#define QA_Q2_MVF_EXTLIMITS_2 (1 << 3)
#define QA_Q2_MVD_CLIENTNUM_NONE 255
#define QA_Q2_PPS_M_TYPE (1 << 0)
#define QA_Q2_PPS_M_ORIGIN (1 << 1)
#define QA_Q2_PPS_M_ORIGIN2 (1 << 2)
#define QA_Q2_PPS_VIEWOFFSET (1 << 3)
#define QA_Q2_PPS_VIEWANGLES (1 << 4)
#define QA_Q2_PPS_VIEWANGLE2 (1 << 5)
#define QA_Q2_PPS_KICKANGLES (1 << 6)
#define QA_Q2_PPS_BLEND (1 << 7)
#define QA_Q2_PPS_FOV (1 << 8)
#define QA_Q2_PPS_WEAPONINDEX (1 << 9)
#define QA_Q2_PPS_WEAPONFRAME (1 << 10)
#define QA_Q2_PPS_GUNOFFSET (1 << 11)
#define QA_Q2_PPS_GUNANGLES (1 << 12)
#define QA_Q2_PPS_RDFLAGS (1 << 13)
#define QA_Q2_PPS_STATS (1 << 14)
#define QA_Q2_PPS_MOREBITS (1 << 15)
#define QA_Q2_GTV_PROTOCOL_VERSION 0xed04
#define QA_Q2_MAX_GTC_MSGLEN 256
#define QA_Q2_GTF_DEFLATE 1
#define QA_Q2_GTF_STRINGCMDS 2

/* TS GtvServerOpT / GtvClientOpT. */
typedef enum qa_q2_gtv_server_op {
  QA_Q2_GTS_HELLO, QA_Q2_GTS_PONG, QA_Q2_GTS_STREAM_START, QA_Q2_GTS_STREAM_STOP,
  QA_Q2_GTS_STREAM_DATA, QA_Q2_GTS_ERROR, QA_Q2_GTS_BADREQUEST,
  QA_Q2_GTS_NOACCESS, QA_Q2_GTS_DISCONNECT, QA_Q2_GTS_RECONNECT
} qa_q2_gtv_server_op;
typedef enum qa_q2_gtv_client_op {
  QA_Q2_GTC_HELLO, QA_Q2_GTC_PING, QA_Q2_GTC_STREAM_START, QA_Q2_GTC_STREAM_STOP,
  QA_Q2_GTC_STRINGCMD
} qa_q2_gtv_client_op;

#define QA_Q2_MVD_MAX_STATS_OLD 32
#define QA_Q2_MVD_MAX_STATS_NEW 64
#define QA_Q2_MVD_GUNINDEX_BITS 13
#define QA_Q2_MVD_RERELEASE_VIEWOFFSET_SCALE 16
#define QA_Q2_MVD_RERELEASE_KICKANGLES_SCALE 1024
#define QA_Q2_MVD_RERELEASE_GUNOFFSET_SCALE 512
#define QA_Q2_MVD_RERELEASE_GUNANGLES_SCALE 4096

/* TS MSG_ValidMvdClientNumber / MSG_WriteMvdPlayersEnd. */
bool qa_q2_msg_valid_mvd_client_number(double number);
void qa_q2_msg_write_mvd_players_end(qa_q2_sizebuf *msg);

/* TS MvdPlayerReadResultT. */
typedef struct qa_q2_mvd_player_result {
  double number;
  bool removed;
  qa_q2_wire_player ps;
} qa_q2_mvd_player_result;

/* TS playerstate writers/readers (NULL is TS null). */
void qa_q2_msg_write_delta_mvd_playerstate(qa_q2_sizebuf *msg,
                                           const qa_q2_wire_player *from_or_null,
                                           const qa_q2_wire_player *to_or_null,
                                           double number, bool force);
void qa_q2_msg_read_delta_mvd_playerstate(qa_q2_sizebuf *msg,
                                          const qa_q2_wire_player *from_or_null,
                                          qa_q2_mvd_player_result *out);
void qa_q2_msg_read_delta_mvd_playerstate_body(
    qa_q2_sizebuf *msg, const qa_q2_wire_player *from_or_null, double number,
    qa_q2_mvd_player_result *out);
void qa_q2_msg_write_delta_mvd_playerstate_rerelease(
    qa_q2_sizebuf *msg, const qa_q2_wire_player *from_or_null,
    const qa_q2_wire_player *to_or_null, double number, bool force);
void qa_q2_msg_read_delta_mvd_playerstate_rerelease(
    qa_q2_sizebuf *msg, const qa_q2_wire_player *from_or_null,
    qa_q2_mvd_player_result *out);
void qa_q2_msg_read_delta_mvd_playerstate_rerelease_body(
    qa_q2_sizebuf *msg, const qa_q2_wire_player *from_or_null, double number,
    qa_q2_mvd_player_result *out);
/* TS readMvdPlayer(message, from, number, profile). */
void qa_q2_mvd_read_player(qa_q2_sizebuf *message,
                           const qa_q2_wire_player *from_or_null, double number,
                           qa_q2_mvd_profile profile,
                           qa_q2_mvd_player_result *out);

#endif
