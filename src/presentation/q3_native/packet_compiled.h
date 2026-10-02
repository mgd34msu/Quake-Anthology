#ifndef QA_Q3_NATIVE_PACKET_COMPILED_H
#define QA_Q3_NATIVE_PACKET_COMPILED_H

#include "packet.h"
#include "compiled_frame.h"

typedef struct q3n_packet_compiled_imports {
    void *context;
    int32_t (*rand)(void *);
    bool (*body)(void *, const q3n_frame *, const q3n_compiled_entity *,
        q3n_entity *, const qa_q3_ref_entity *, qa_error *);
    bool (*player)(void *, const q3n_frame *, const q3n_compiled_entity *, q3n_entity *, qa_error *);
    bool (*trail)(void *, const q3n_frame *, const q3n_compiled_entity *, q3n_entity *,
        const q3n_weapon_media *, bool grapple, qa_error *);
    bool (*powerups)(void *, const q3n_frame *, const q3n_compiled_entity *,
        q3n_entity *, const qa_q3_ref_entity *, int32_t team, qa_error *);
} q3n_packet_compiled_imports;
bool q3n_packet_compiled_predict(const q3n_frame *, qa_error *);
bool q3n_packet_compiled_lerp(const q3n_frame *, const q3n_compiled_entity *,
    const q3n_packet_options *, qa_error *);
bool q3n_packet_compiled_entity(const q3n_frame *, const q3n_compiled_entity *,
    const q3n_packet_options *, const q3n_packet_compiled_imports *, qa_error *);
bool q3n_packet_compiled_sound_position(const q3n_frame *, const q3n_compiled_entity *, qa_error *);

#endif
