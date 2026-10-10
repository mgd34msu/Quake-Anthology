#ifndef QA_USERCMD_H
#define QA_USERCMD_H

#include "qa/math.h"
#include "qa/ruleset.h"
#include "qa/actors.h"
#include "qa/network_ids.h"

typedef enum qa_frame_phase {
    QA_FRAME_ENTRY, QA_CLIENT_COMMAND, QA_ENTITY_PRETHINK, QA_ENTITY_PHYSICS,
    QA_ENTITY_THINK, QA_CLIENT_END_FRAME, QA_FRAME_EXIT
} qa_frame_phase;

typedef struct qa_usercmd_arsenal {
    qa_bytes provider, weapon;
    bool use_holdable, has_impulse;
    uint8_t impulse;
} qa_usercmd_arsenal;

/* One engine command, retained unchanged from input through simulation and
 * prediction. Native byte/short widths are applied by protocol/module codecs. */
typedef struct qa_usercmd {
    qa_ruleset_id kind;
    uint64_t sequence;
    uint32_t milliseconds;
    uint64_t duration_ns;
    int32_t server_time_ms, server_frame;
    double acknowledged_server_seconds;
    qa_vec3 angles;
    int32_t angle_words[3];
    float forward_move, side_move, up_move;
    uint32_t buttons;
    uint8_t impulse, light_level, weapon;
    qa_actor_id actor;
    qa_actor_owner provider;
    qa_frame_phase phase;
    uint64_t completed_frame_number, time_ns, elapsed_ns, host_elapsed_ns;
    qa_net_client_id client;
    qa_net_seat_id seat;
    uint64_t epoch;
    bool has_arsenal;
    qa_usercmd_arsenal arsenal;
} qa_usercmd;

#endif
