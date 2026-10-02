#ifndef QA_NATIVE_HOST_Q2_WIRE_H
#define QA_NATIVE_HOST_Q2_WIRE_H

#include "qa/native_host.h"
#include "qa/network_q2.h"

typedef struct qa_native_host_q2_origin {
    uint64_t source_frame;
    qa_vec3 origin;
    bool present;
} qa_native_host_q2_origin;
typedef struct qa_native_host_q2_entity {
    qa_native_slot_binding binding;
    qa_q2_entity state;
    qa_bounds bounds, absolute_bounds;
    uint32_t server_flags, owner_slot, link_count;
    uint64_t creation_frame;
    qa_vec3 creation_origin;
    qa_native_host_q2_origin origins[8];
    int32_t areas[2], cluster_count, clusters[16], headnode;
    bool in_use, linked, creation_present;
} qa_native_host_q2_entity;

/* These read the installed GAME's published ABI prefixes and current full
 * source bindings. They neither reconcile actors nor invoke the module. */
bool qa_native_host_q2_wire_count(qa_native_host *, uint32_t *, qa_error *);
bool qa_native_host_q2_wire_entity(qa_native_host *, uint32_t,
    qa_native_host_q2_entity *, qa_error *);
bool qa_native_host_q2_wire_player(qa_native_host *, uint32_t,
    qa_actor_id, qa_q2_player *, qa_error *);
bool qa_native_host_q2_wire_ping(qa_native_host *, uint32_t,
    qa_actor_id, int32_t *, qa_error *);

#endif
