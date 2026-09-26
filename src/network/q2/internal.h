#ifndef QA_Q2_INTERNAL_H
#define QA_Q2_INTERNAL_H
#include "qa/network_q2.h"
#include <math.h>
#include <string.h>
#include <limits.h>
typedef struct qa_q2_codec_ops {
    bool (*read_serverdata)(qa_q2_codec *, qa_net_reader *, qa_q2_serverdata *);
    bool (*write_serverdata)(qa_q2_codec *, qa_net_writer *, const qa_q2_serverdata *);
    bool (*read_entity_header)(qa_q2_codec *, qa_net_reader *, uint32_t *, uint64_t *);
    bool (*read_entity)(qa_q2_codec *, qa_net_reader *, const qa_q2_entity *, uint32_t, uint64_t, qa_q2_entity *);
    bool (*write_entity)(qa_q2_codec *, qa_net_writer *, const qa_q2_entity *, const qa_q2_entity *, bool, bool);
    bool (*write_entity_remove)(qa_q2_codec *, qa_net_writer *, uint32_t);
    bool (*read_player)(qa_q2_codec *, qa_net_reader *, const qa_q2_player *, qa_q2_player *);
    bool (*write_player)(qa_q2_codec *, qa_net_writer *, const qa_q2_player *, const qa_q2_player *);
    bool (*read_frame_header)(qa_q2_codec *, qa_net_reader *, qa_q2_frame_header *);
    bool (*write_frame)(qa_q2_codec *, qa_net_writer *, const qa_q2_frame_header *, const qa_q2_player *, const qa_q2_player *, qa_q2_write_entities_fn, void *);
    bool (*read_usercmd)(qa_q2_codec *, qa_net_reader *, const qa_q2_usercmd *, qa_q2_usercmd *);
    bool (*write_usercmd)(qa_q2_codec *, qa_net_writer *, const qa_q2_usercmd *, const qa_q2_usercmd *);
} qa_q2_codec_ops;
extern const qa_q2_codec_ops qa_q2_vanilla_ops, qa_q2_r1q2_ops, qa_q2_q2pro_ops, qa_q2_rerelease_ops, qa_q2_kex_ops;
/* Explicit fixed-point conversion: never serialize native structs. */
float qa_q2_read_coord(qa_net_reader *);
float qa_q2_read_angle8(qa_net_reader *);
float qa_q2_read_angle16(qa_net_reader *);
bool qa_q2_write_coord(qa_net_writer *, float);
bool qa_q2_write_angle8(qa_net_writer *, float);
bool qa_q2_write_angle16(qa_net_writer *, float);
bool qa_q2_write_scaled(qa_net_writer *, float, float scale, unsigned bits, bool is_signed);
bool qa_q2_read_vec3(qa_net_reader *, float[3], bool floating);
bool qa_q2_write_vec3(qa_net_writer *, const float[3], bool floating);
bool qa_q2_read_entity_header_common(qa_q2_codec *, qa_net_reader *, uint32_t *, uint64_t *);
bool qa_q2_write_entity_header_common(qa_q2_codec *, qa_net_writer *, uint32_t, uint64_t);
bool qa_q2_classic_read_usercmd(qa_q2_codec *, qa_net_reader *, const qa_q2_usercmd *, qa_q2_usercmd *);
bool qa_q2_classic_write_usercmd(qa_q2_codec *, qa_net_writer *, const qa_q2_usercmd *, const qa_q2_usercmd *);
#endif
