#ifndef QA_Q3_NATIVE_PACKET_REMOTE_H
#define QA_Q3_NATIVE_PACKET_REMOTE_H

#include "packet.h"
#include "remote_frame.h"

typedef struct q3n_packet_remote_imports {
    void *context;
    int32_t (*rand)(void *);
    bool (*body)(void *, const q3n_frame *, const q3n_remote_entity *,
        q3n_entity *, const qa_q3_ref_entity *, qa_error *);
    bool (*player)(void *, const q3n_frame *, const q3n_remote_entity *, q3n_entity *, qa_error *);
    bool (*trail)(void *, const q3n_frame *, const q3n_remote_entity *, q3n_entity *,
        const q3n_weapon_media *, bool grapple, qa_error *);
    bool (*powerups)(void *, const q3n_frame *, const q3n_remote_entity *,
        q3n_entity *, const qa_q3_ref_entity *, int32_t team, qa_error *);
} q3n_packet_remote_imports;

/* The reached snapshot row and its physical centity borrow the actual remote
 * frame. No decoded number is interpreted as a local GAME actor. */
bool q3n_packet_remote_entity(const q3n_frame *, const q3n_remote_entity *,
    const q3n_packet_options *, const q3n_packet_remote_imports *, qa_error *);
/* AddPacketEntities consumes one predictable event from the genuine retained
 * CG-private predicted PS. Transport and movement-predictor PS stay borrowed. */
bool q3n_packet_remote_predict(const q3n_frame *, qa_error *);
/* Exposes the followed, nonpredicted player's position for the source
 * AddPacketEntities ordering and later lightning/mover consumers. */
bool q3n_packet_remote_lerp(const q3n_frame *, const q3n_remote_entity *,
    const q3n_packet_options *, qa_error *);
bool q3n_packet_remote_sound_position(const q3n_frame *, const q3n_remote_entity *, qa_error *);

#endif
