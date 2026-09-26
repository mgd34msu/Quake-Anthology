#ifndef QA_Q2_CLASSIC_INTERNAL_H
#define QA_Q2_CLASSIC_INTERNAL_H

#include "internal.h"

enum {
    Q2C_U_ORIGIN1 = 1u << 0, Q2C_U_ORIGIN2 = 1u << 1,
    Q2C_U_ANGLE2 = 1u << 2, Q2C_U_ANGLE3 = 1u << 3,
    Q2C_U_FRAME8 = 1u << 4, Q2C_U_EVENT = 1u << 5,
    Q2C_U_REMOVE = 1u << 6, Q2C_U_MOREBITS1 = 1u << 7,
    Q2C_U_NUMBER16 = 1u << 8, Q2C_U_ORIGIN3 = 1u << 9,
    Q2C_U_ANGLE1 = 1u << 10, Q2C_U_MODEL = 1u << 11,
    Q2C_U_RENDERFX8 = 1u << 12, Q2C_U_EFFECTS8 = 1u << 14,
    Q2C_U_MOREBITS2 = 1u << 15, Q2C_U_SKIN8 = 1u << 16,
    Q2C_U_FRAME16 = 1u << 17, Q2C_U_RENDERFX16 = 1u << 18,
    Q2C_U_EFFECTS16 = 1u << 19, Q2C_U_MODEL2 = 1u << 20,
    Q2C_U_MODEL3 = 1u << 21, Q2C_U_MODEL4 = 1u << 22,
    Q2C_U_MOREBITS3 = 1u << 23, Q2C_U_OLDORIGIN = 1u << 24,
    Q2C_U_SKIN16 = 1u << 25, Q2C_U_SOUND = 1u << 26,
    Q2C_U_SOLID = 1u << 27,
    Q2C_PS_TYPE = 1u << 0, Q2C_PS_ORIGIN = 1u << 1,
    Q2C_PS_VELOCITY = 1u << 2, Q2C_PS_TIME = 1u << 3,
    Q2C_PS_FLAGS = 1u << 4, Q2C_PS_GRAVITY = 1u << 5,
    Q2C_PS_DELTA_ANGLES = 1u << 6, Q2C_PS_VIEWOFFSET = 1u << 7,
    Q2C_PS_VIEWANGLES = 1u << 8, Q2C_PS_KICKANGLES = 1u << 9,
    Q2C_PS_BLEND = 1u << 10, Q2C_PS_FOV = 1u << 11,
    Q2C_PS_WEAPONINDEX = 1u << 12, Q2C_PS_WEAPONFRAME = 1u << 13,
    Q2C_PS_RDFLAGS = 1u << 14,
    Q2C_EPS_GUNOFFSET = 1u << 0, Q2C_EPS_GUNANGLES = 1u << 1,
    Q2C_EPS_VELOCITY_Z = 1u << 2, Q2C_EPS_ORIGIN_Z = 1u << 3,
    Q2C_EPS_VIEWANGLE_Z = 1u << 4, Q2C_EPS_STATS = 1u << 5,
    Q2C_MAX_EDICTS = 1024, Q2C_STATS = 32,
    Q2C_SVC_SERVERDATA = 12, Q2C_SVC_PLAYERINFO = 17,
    Q2C_SVC_FRAME = 20, Q2C_RF_BEAM = 128
};

typedef struct qa_q2_classic_player_delta {
    uint16_t flags;
    uint8_t extra;
    uint32_t stats;
} qa_q2_classic_player_delta;

bool qa_q2_classic_read_serverdata_body(qa_net_reader *, qa_q2_serverdata *);
bool qa_q2_classic_write_serverdata_body(qa_net_writer *, const qa_q2_serverdata *, uint32_t);
bool qa_q2_classic_read_entity_header(qa_q2_codec *, qa_net_reader *, uint32_t *, uint64_t *);
bool qa_q2_classic_read_entity_fields(qa_net_reader *, const qa_q2_entity *, uint32_t,
                                     uint64_t, qa_q2_entity *, bool unsigned_words, bool long_solid);
bool qa_q2_classic_write_entity_fields(qa_q2_codec *, qa_net_writer *, const qa_q2_entity *,
                                      const qa_q2_entity *, bool force, bool fresh,
                                      bool r1_skin, bool long_solid);
bool qa_q2_classic_write_entity_remove(qa_q2_codec *, qa_net_writer *, uint32_t);
qa_q2_classic_player_delta qa_q2_classic_player_changes(const qa_q2_player *, const qa_q2_player *, bool split);
bool qa_q2_classic_player_valid(qa_net_writer *, const qa_q2_player *);
bool qa_q2_classic_read_player_fields(qa_net_reader *, const qa_q2_player *, qa_q2_player *,
                                     uint16_t, uint8_t, bool split);
bool qa_q2_classic_write_player_fields(qa_net_writer *, const qa_q2_player *,
                                      qa_q2_classic_player_delta, bool split);

#endif
