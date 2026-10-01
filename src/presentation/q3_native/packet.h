#ifndef QA_Q3_NATIVE_PACKET_H
#define QA_Q3_NATIVE_PACKET_H

#include "frame.h"

typedef struct q3n_packet_options {
    bool smooth_clients, simple_items;
    int32_t obelisk_respawn_delay;
} q3n_packet_options;
typedef struct q3n_packet_imports {
    void *context;
    int32_t (*rand)(void *);
    bool (*body)(void *, const q3n_frame *, const qa_application_native_q3_entity *,
        q3n_entity *, const qa_q3_ref_entity *, qa_error *);
    bool (*player)(void *, const q3n_frame *, const qa_application_native_q3_entity *, q3n_entity *, qa_error *);
    bool (*trail)(void *, const q3n_frame *, const qa_application_native_q3_entity *, q3n_entity *,
        const q3n_weapon_media *, bool grapple, qa_error *);
    bool (*powerups)(void *, const q3n_frame *, const qa_application_native_q3_entity *,
        q3n_entity *, const qa_q3_ref_entity *, int32_t team, qa_error *);
} q3n_packet_imports;

/* Current S is a genuine borrowed observation. Presentation can change private
 * centity pose/timers and author refs without writing any source field. Native
 * time equals the completed source cut; no snapshot extrapolation is implied. */
bool q3n_packet_entity(const q3n_frame *, const qa_application_native_q3_entity *,
    q3n_entity *, const q3n_packet_options *, const q3n_packet_imports *, qa_error *);
bool q3n_packet_sound_position(const q3n_frame *, const qa_application_native_q3_entity *,
    const q3n_entity *, qa_error *);

#endif
