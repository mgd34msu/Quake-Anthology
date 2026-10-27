/* Port of network/q1/qw-constants.ts (constants dumped mechanically via bun JSON). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_QW_CONSTANTS_H
#define QA_NETWORK_Q1_QW_CONSTANTS_H
#include <stdint.h>

#define QA_QW_A2A_ACK "l"
#define QA_QW_A2A_ECHO "e"
#define QA_QW_A2A_NACK "m"
#define QA_QW_A2A_PING "k"
#define QA_QW_A2C_CLIENT_COMMAND "B"
#define QA_QW_A2C_PRINT "n"
#define QA_QW_CM_ANGLE1 1
#define QA_QW_CM_ANGLE2 128
#define QA_QW_CM_ANGLE3 2
#define QA_QW_CM_BUTTONS 32
#define QA_QW_CM_FORWARD 4
#define QA_QW_CM_IMPULSE 64
#define QA_QW_CM_SIDE 8
#define QA_QW_CM_UP 16
typedef enum qa_qw_clcopst {
  QA_QW_CLC_BAD = 0,
  QA_QW_CLC_NOP = 1,
  QA_QW_CLC_MOVE = 3,
  QA_QW_CLC_STRINGCMD = 4,
  QA_QW_CLC_DELTA = 5,
  QA_QW_CLC_TMOVE = 6,
  QA_QW_CLC_UPLOAD = 7,
} qa_qw_clcopst;
#define QA_QW_HAS_CLCOPST 1
#define QA_QW_DEFAULT_SOUND_PACKET_ATTENUATION 1
#define QA_QW_DEFAULT_SOUND_PACKET_VOLUME 255
#define QA_QW_MAX_CLIENTS 32
#define QA_QW_MAX_PACKET_ENTITIES 64
#define QA_QW_PF_COMMAND 2
#define QA_QW_PF_DEAD 512
#define QA_QW_PF_EFFECTS 128
#define QA_QW_PF_GIB 1024
#define QA_QW_PF_MODEL 32
#define QA_QW_PF_MSEC 1
#define QA_QW_PF_NOGRAV 2048
#define QA_QW_PF_SKINNUM 64
#define QA_QW_PF_VELOCITY1 4
#define QA_QW_PF_VELOCITY2 8
#define QA_QW_PF_VELOCITY3 16
#define QA_QW_PF_WEAPONFRAME 256
#define QA_QW_PORT_CLIENT 27001
#define QA_QW_PORT_MASTER 27000
#define QA_QW_PORT_SERVER 27500
#define QA_QW_PRINT_CHAT 3
#define QA_QW_PRINT_HIGH 2
#define QA_QW_PRINT_LOW 0
#define QA_QW_PRINT_MEDIUM 1
#define QA_QW_PROTOCOL_VERSION 28
#define QA_QW_QW_CHECK_HASH 20823
#define QA_QW_S2C_CHALLENGE "c"
#define QA_QW_S2C_CONNECTION "j"
#define QA_QW_S2M_HEARTBEAT "a"
#define QA_QW_S2M_SHUTDOWN "C"
#define QA_QW_SND_ATTENUATION 16384
#define QA_QW_SND_VOLUME 32768
typedef enum qa_qw_svcopst {
  QA_QW_SVC_BAD = 0,
  QA_QW_SVC_NOP = 1,
  QA_QW_SVC_DISCONNECT = 2,
  QA_QW_SVC_UPDATESTAT = 3,
  QA_QW_SVC_SETVIEW = 5,
  QA_QW_SVC_SOUND = 6,
  QA_QW_SVC_PRINT = 8,
  QA_QW_SVC_STUFFTEXT = 9,
  QA_QW_SVC_SETANGLE = 10,
  QA_QW_SVC_SERVERDATA = 11,
  QA_QW_SVC_LIGHTSTYLE = 12,
  QA_QW_SVC_UPDATEFRAGS = 14,
  QA_QW_SVC_STOPSOUND = 16,
  QA_QW_SVC_DAMAGE = 19,
  QA_QW_SVC_SPAWNSTATIC = 20,
  QA_QW_SVC_SPAWNBASELINE = 22,
  QA_QW_SVC_TEMP_ENTITY = 23,
  QA_QW_SVC_SETPAUSE = 24,
  QA_QW_SVC_CENTERPRINT = 26,
  QA_QW_SVC_KILLEDMONSTER = 27,
  QA_QW_SVC_FOUNDSECRET = 28,
  QA_QW_SVC_SPAWNSTATICSOUND = 29,
  QA_QW_SVC_INTERMISSION = 30,
  QA_QW_SVC_FINALE = 31,
  QA_QW_SVC_CDTRACK = 32,
  QA_QW_SVC_SELLSCREEN = 33,
  QA_QW_SVC_SMALLKICK = 34,
  QA_QW_SVC_BIGKICK = 35,
  QA_QW_SVC_UPDATEPING = 36,
  QA_QW_SVC_UPDATEENTERTIME = 37,
  QA_QW_SVC_UPDATESTATLONG = 38,
  QA_QW_SVC_MUZZLEFLASH = 39,
  QA_QW_SVC_UPDATEUSERINFO = 40,
  QA_QW_SVC_DOWNLOAD = 41,
  QA_QW_SVC_PLAYERINFO = 42,
  QA_QW_SVC_NAILS = 43,
  QA_QW_SVC_CHOKECOUNT = 44,
  QA_QW_SVC_MODELLIST = 45,
  QA_QW_SVC_SOUNDLIST = 46,
  QA_QW_SVC_PACKETENTITIES = 47,
  QA_QW_SVC_DELTAPACKETENTITIES = 48,
  QA_QW_SVC_MAXSPEED = 49,
  QA_QW_SVC_ENTGRAVITY = 50,
  QA_QW_SVC_SETINFO = 51,
  QA_QW_SVC_SERVERINFO = 52,
  QA_QW_SVC_UPDATEPL = 53,
} qa_qw_svcopst;
#define QA_QW_HAS_SVCOPST 1
#define QA_QW_TE_BLOOD 12
#define QA_QW_TE_EXPLOSION 3
#define QA_QW_TE_GUNSHOT 2
#define QA_QW_TE_KNIGHTSPIKE 8
#define QA_QW_TE_LAVASPLASH 10
#define QA_QW_TE_LIGHTNING1 5
#define QA_QW_TE_LIGHTNING2 6
#define QA_QW_TE_LIGHTNING3 9
#define QA_QW_TE_LIGHTNINGBLOOD 13
#define QA_QW_TE_SPIKE 0
#define QA_QW_TE_SUPERSPIKE 1
#define QA_QW_TE_TAREXPLOSION 4
#define QA_QW_TE_TELEPORT 11
#define QA_QW_TE_WIZSPIKE 7
#define QA_QW_UPDATE_BACKUP 64
#define QA_QW_UPDATE_MASK 63
#define QA_QW_U_ANGLE1 1
#define QA_QW_U_ANGLE2 4096
#define QA_QW_U_ANGLE3 2
#define QA_QW_U_COLORMAP 8
#define QA_QW_U_EFFECTS 32
#define QA_QW_U_FRAME 8192
#define QA_QW_U_MODEL 4
#define QA_QW_U_MOREBITS 32768
#define QA_QW_U_ORIGIN1 512
#define QA_QW_U_ORIGIN2 1024
#define QA_QW_U_ORIGIN3 2048
#define QA_QW_U_REMOVE 16384
#define QA_QW_U_SKIN 16
#define QA_QW_U_SOLID 64

#include "qa/network/q1/wire_types.h"
typedef struct qa_qw_entity_state {
  double number, flags; qa_wire_vector origin, angles;
  double modelindex, frame, colormap, skinnum, effects, alpha, scale;
} qa_qw_entity_state;
void qa_qw_entity_state_init(qa_qw_entity_state *s);
void qa_qw_entity_state_clear(qa_qw_entity_state *s);
void qa_qw_entity_state_copy_from(qa_qw_entity_state *s, const qa_qw_entity_state *from);
typedef struct qa_qw_packet_entities {
  double num_entities; qa_qw_entity_state entities[64];
} qa_qw_packet_entities;
void qa_qw_packet_entities_init(qa_qw_packet_entities *p);
typedef struct qa_qw_usercmd {
  double msec; qa_wire_vector angles;
  double forwardmove, sidemove, upmove, buttons, impulse;
} qa_qw_usercmd;
void qa_qw_usercmd_init(qa_qw_usercmd *c);

#endif
