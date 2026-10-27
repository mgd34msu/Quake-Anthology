/*
 * Port of src/network/q2/constants.ts: Q2 protocol numbers, opcodes,
 * delta bits, ports, and the ComError throw. C accommodation: the TS
 * ComError class (code + message) becomes qa_q2_com_error_throw(),
 * which prints "ComError: <message>" and aborts (the TS throw reaches
 * the top-level crash handler); plain `throw new Error(msg)` sites in
 * this module family use qa_q2_throw_error() the same way.
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CONSTANTS_H
#define QA_NETWORK_Q2_CONSTANTS_H

#include <stdnoreturn.h>

#define QA_Q2_PROTOCOL_VERSION 34
#define QA_Q2_PROTOCOL_VERSION_RERELEASE 1038
#define QA_Q2_PROTOCOL_VERSION_RERELEASE_CLASSIC 4038
#define QA_Q2_PROTOCOL_VERSION_R1Q2 35
#define QA_Q2_PROTOCOL_VERSION_R1Q2_MINIMUM 1903
#define QA_Q2_PROTOCOL_VERSION_R1Q2_UCMD 1904
#define QA_Q2_PROTOCOL_VERSION_R1Q2_LONG_SOLID 1905
#define QA_Q2_PROTOCOL_VERSION_R1Q2_CURRENT 1905
#define QA_Q2_PROTOCOL_VERSION_Q2PRO 36
#define QA_Q2_PROTOCOL_VERSION_Q2PRO_MINIMUM 1015
#define QA_Q2_PROTOCOL_VERSION_Q2PRO_SERVER_STATE 1019
#define QA_Q2_PROTOCOL_VERSION_Q2PRO_CURRENT QA_Q2_PROTOCOL_VERSION_Q2PRO_SERVER_STATE
#define QA_Q2_EPS_GUNOFFSET (1 << 0)
#define QA_Q2_EPS_GUNANGLES (1 << 1)
#define QA_Q2_EPS_M_VELOCITY2 (1 << 2)
#define QA_Q2_EPS_M_ORIGIN2 (1 << 3)
#define QA_Q2_EPS_VIEWANGLE2 (1 << 4)
#define QA_Q2_EPS_STATS (1 << 5)
#define QA_Q2_SVC_ZPACKET 21
#define QA_Q2_PORT_MASTER 27900
#define QA_Q2_PORT_CLIENT 27901
#define QA_Q2_PORT_SERVER 27910
#define QA_Q2_UPDATE_BACKUP 16
#define QA_Q2_UPDATE_MASK (QA_Q2_UPDATE_BACKUP - 1)

/* TS SvcOpsT. */
typedef enum qa_q2_svc_op {
  QA_Q2_SVC_BAD, QA_Q2_SVC_MUZZLEFLASH, QA_Q2_SVC_MUZZLEFLASH2,
  QA_Q2_SVC_TEMP_ENTITY, QA_Q2_SVC_LAYOUT, QA_Q2_SVC_INVENTORY,
  QA_Q2_SVC_NOP, QA_Q2_SVC_DISCONNECT, QA_Q2_SVC_RECONNECT,
  QA_Q2_SVC_SOUND, QA_Q2_SVC_PRINT, QA_Q2_SVC_STUFFTEXT,
  QA_Q2_SVC_SERVERDATA, QA_Q2_SVC_CONFIGSTRING, QA_Q2_SVC_SPAWNBASELINE,
  QA_Q2_SVC_CENTERPRINT, QA_Q2_SVC_DOWNLOAD, QA_Q2_SVC_PLAYERINFO,
  QA_Q2_SVC_PACKETENTITIES, QA_Q2_SVC_DELTAPACKETENTITIES, QA_Q2_SVC_FRAME
} qa_q2_svc_op;

/* TS ClcOpsT. */
typedef enum qa_q2_clc_op {
  QA_Q2_CLC_BAD, QA_Q2_CLC_NOP, QA_Q2_CLC_MOVE, QA_Q2_CLC_USERINFO,
  QA_Q2_CLC_STRINGCMD, QA_Q2_CLC_R1Q2_SETTING = 5,
  QA_Q2_CLC_Q2PRO_MOVE_NODELTA = 10, QA_Q2_CLC_Q2PRO_MOVE_BATCHED,
  QA_Q2_CLC_Q2PRO_USERINFO_DELTA
} qa_q2_clc_op;

#define QA_Q2_CM_ANGLE1 (1 << 0)
#define QA_Q2_CM_ANGLE2 (1 << 1)
#define QA_Q2_CM_ANGLE3 (1 << 2)
#define QA_Q2_CM_FORWARD (1 << 3)
#define QA_Q2_CM_SIDE (1 << 4)
#define QA_Q2_CM_UP (1 << 5)
#define QA_Q2_CM_BUTTONS (1 << 6)
#define QA_Q2_CM_IMPULSE (1 << 7)
#define QA_Q2_U_ORIGIN1 (1 << 0)
#define QA_Q2_U_ORIGIN2 (1 << 1)
#define QA_Q2_U_ANGLE2 (1 << 2)
#define QA_Q2_U_ANGLE3 (1 << 3)
#define QA_Q2_U_FRAME8 (1 << 4)
#define QA_Q2_U_EVENT (1 << 5)
#define QA_Q2_U_REMOVE (1 << 6)
#define QA_Q2_U_MOREBITS1 (1 << 7)
#define QA_Q2_U_NUMBER16 (1 << 8)
#define QA_Q2_U_ORIGIN3 (1 << 9)
#define QA_Q2_U_ANGLE1 (1 << 10)
#define QA_Q2_U_MODEL (1 << 11)
#define QA_Q2_U_RENDERFX8 (1 << 12)
#define QA_Q2_U_EFFECTS8 (1 << 14)
#define QA_Q2_U_MOREBITS2 (1 << 15)
#define QA_Q2_U_SKIN8 (1 << 16)
#define QA_Q2_U_FRAME16 (1 << 17)
#define QA_Q2_U_RENDERFX16 (1 << 18)
#define QA_Q2_U_EFFECTS16 (1 << 19)
#define QA_Q2_U_MODEL2 (1 << 20)
#define QA_Q2_U_MODEL3 (1 << 21)
#define QA_Q2_U_MODEL4 (1 << 22)
#define QA_Q2_U_MOREBITS3 (1 << 23)
#define QA_Q2_U_OLDORIGIN (1 << 24)
#define QA_Q2_U_SKIN16 (1 << 25)
#define QA_Q2_U_SOUND (1 << 26)
#define QA_Q2_U_SOLID (1 << 27)
#define QA_Q2_PS_M_TYPE (1 << 0)
#define QA_Q2_PS_M_ORIGIN (1 << 1)
#define QA_Q2_PS_M_VELOCITY (1 << 2)
#define QA_Q2_PS_M_TIME (1 << 3)
#define QA_Q2_PS_M_FLAGS (1 << 4)
#define QA_Q2_PS_M_GRAVITY (1 << 5)
#define QA_Q2_PS_M_DELTA_ANGLES (1 << 6)
#define QA_Q2_PS_VIEWOFFSET (1 << 7)
#define QA_Q2_PS_VIEWANGLES (1 << 8)
#define QA_Q2_PS_KICKANGLES (1 << 9)
#define QA_Q2_PS_BLEND (1 << 10)
#define QA_Q2_PS_FOV (1 << 11)
#define QA_Q2_PS_WEAPONINDEX (1 << 12)
#define QA_Q2_PS_WEAPONFRAME (1 << 13)
#define QA_Q2_PS_RDFLAGS (1 << 14)
#define QA_Q2_PS_RR_VIEWHEIGHT (1 << 15)
#define QA_Q2_PORT_ANY (-1)
#define QA_Q2_MAX_MSGLEN 1400
#define QA_Q2_PACKET_HEADER 10
#define QA_Q2_ERR_FATAL 0
#define QA_Q2_ERR_DROP 1
#define QA_Q2_ERR_QUIT 2

/* TS `throw new ComError(code, message)`. */
noreturn void qa_q2_com_error_throw(int code, const char *format, ...);
/* TS `throw new Error(message)` in this module family. */
noreturn void qa_q2_throw_error(const char *format, ...);

#endif
