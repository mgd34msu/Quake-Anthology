#ifndef QA_FRONTEND_NATIVE_CHARACTER_H
#define QA_FRONTEND_NATIVE_CHARACTER_H
#include "native_q3_client.h"
#include "selected_character.h"

typedef struct frontend_native_character frontend_native_character;
bool frontend_native_character_create(qa_frontend *, frontend_native_q3 *,
    frontend_native_character **, qa_error *);
bool frontend_native_character_begin(frontend_native_character *, const q3n_frame *, qa_error *);
void frontend_native_character_end(frontend_native_character *);
bool frontend_native_character_idle(const frontend_native_character *);
bool frontend_native_character_destroy(frontend_native_character *, qa_error *);
bool frontend_native_character_rebind_ready(const frontend_native_character *, const qa_frontend *, qa_error *);
void frontend_native_character_rebind(frontend_native_character *, qa_frontend *);
bool frontend_native_character_body_hidden(frontend_native_character *, const q3n_frame *,
    const qa_application_native_q3_entity *, bool *, qa_error *);
bool frontend_native_character_body(frontend_native_character *, const q3n_frame *,
    const qa_application_native_q3_entity *, uint32_t, const qa_q3_ref_entity *, bool,
    bool *, qa_error *);
bool frontend_native_character_packet(frontend_native_character *, const q3n_frame *,
    const qa_application_native_q3_entity *, const qa_q3_ref_entity *, bool *, qa_error *);
bool frontend_native_character_admitted(const frontend_native_character *, qa_actor_id);
bool frontend_native_character_origin(const frontend_native_character *, const q3n_frame *,
    qa_actor_id, qa_vec3 *, bool *found, qa_error *);
bool frontend_native_character_torso(const frontend_native_character *, qa_actor_id,
    const qa_q3_presentation_assets **, const qa_q3_ref_entity **, bool *, qa_error *);
bool frontend_native_character_submit(frontend_native_character *, const qa_q3_scene_options *,
    qa_scene_frame *, qa_error *);
bool frontend_native_character_prepare_view(frontend_native_character *, qa_q3_scene_options *, qa_error *);
#endif
