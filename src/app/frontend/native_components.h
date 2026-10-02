#ifndef QA_FRONTEND_NATIVE_COMPONENTS_H
#define QA_FRONTEND_NATIVE_COMPONENTS_H
#include "native_q3_client_internal.h"
bool frontend_native_components_prepare(frontend_native_q3 *,const q3n_frame *,qa_error *);
bool frontend_native_components_body(frontend_native_q3 *,const q3n_frame *,qa_actor_id,uint32_t,
    const qa_q3_ref_entity *,bool,bool *,qa_error *);
bool frontend_native_components_scene_prepare(frontend_native_q3 *,qa_q3_scene_options *,qa_error *);
bool frontend_native_components_scene_submit(frontend_native_q3 *,const qa_q3_scene_options *,qa_scene_frame *,qa_error *);
void frontend_native_components_clear(frontend_native_q3 *);
void frontend_native_components_release(frontend_native_q3 *);
bool frontend_native_components_hud(frontend_native_q3 *,qa_error *);
#endif
