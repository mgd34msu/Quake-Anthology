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
    int32_t areas[2], cluster_count, clusters[16], headnode, solid;
    bool in_use, linked, creation_present;
} qa_native_host_q2_entity;

/* These read the installed GAME's published ABI prefixes and current full
 * source bindings. They neither reconcile actors nor invoke the module. */
bool qa_native_host_q2_wire_count(qa_native_host *, uint32_t *, qa_error *);
bool qa_native_host_q2_wire_entity(qa_native_host *, uint32_t,
    qa_native_host_q2_entity *, qa_error *);
/* This borrows the actual SDK row during a genuine GAME import callback.
 * New Source rows can be observed before the completed table count advances. */
bool qa_native_host_q2_wire_entity_import(qa_native_host *, uint32_t,
    qa_native_host_q2_entity *, qa_error *);
/* Pure observation during this Source's actual end-frame stage, after its
 * module has returned. Other Source turns and active imports are excluded. */
bool qa_native_host_q2_wire_entity_stage(qa_native_host *, uint32_t,
    qa_native_host_q2_entity *, qa_error *);
/* A returned component can observe at the genuine completed primary boundary,
 * including a boundary at which its own component clock was not due. */
bool qa_native_host_q2_wire_entity_completed(qa_native_host *, const qa_source_frame *,
    uint32_t, qa_native_host_q2_entity *, qa_error *);
/* Invokes the real API2023 visibility export during that end-frame stage.
 * Both slots must still name the supplied full Source actors after the call. */
bool qa_native_host_q2_entity_visible(qa_native_host *, uint32_t entity_source_slot,
    qa_actor_id entity, uint32_t viewer_source_slot, qa_actor_id viewer,
    bool *, qa_error *);
bool qa_native_host_q2_entity_visible_completed(qa_native_host *, const qa_source_frame *,
    uint32_t entity_source_slot, qa_actor_id entity, uint32_t viewer_source_slot,
    qa_actor_id viewer, bool *, qa_error *);
/* The selected Source has returned even when another GAME owns the current
 * session turn. Read only its actual signed public animation frame. */
bool qa_native_host_q2_character_frame(qa_native_host *, uint32_t source_slot,
    qa_actor_id, double *, qa_error *);
bool qa_native_host_q2_wire_player(qa_native_host *, uint32_t,
    qa_actor_id, qa_q2_player *, qa_error *);
bool qa_native_host_q2_wire_ping(qa_native_host *, uint32_t,
    qa_actor_id, int32_t *, qa_error *);

#endif
