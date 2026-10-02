#ifndef QA_FRONTEND_REMOTE_UNIFIED_Q3_H
#define QA_FRONTEND_REMOTE_UNIFIED_Q3_H
#include "remote_unified_media.h"
#include "qa/persistence_content.h"
typedef struct frontend_unified_q3 frontend_unified_q3;
struct frontend_unified_events;
struct frontend_unified_components;
bool frontend_unified_q3_create(qa_frontend *, frontend_remote_unified *, frontend_unified_media *, frontend_unified_q3 **, qa_error *);
bool frontend_unified_q3_audio(frontend_unified_q3 *, uint64_t, void *, bool (*)(void *, qa_actor_id, uint64_t *, qa_error *), qa_error *);
bool frontend_unified_q3_events(frontend_unified_q3 *, struct frontend_unified_events *, qa_error *);
bool frontend_unified_q3_components(frontend_unified_q3 *,struct frontend_unified_components *,qa_error *);
bool frontend_unified_q3_validate(frontend_unified_q3 *, bool simulation, const qa_unified_document *, qa_json_id, qa_error *);
bool frontend_unified_q3_owner_validate(frontend_unified_q3 *,const qa_unified_document *,qa_json_id,qa_error *);
bool frontend_unified_q3_owner_retire(frontend_unified_q3 *,const qa_unified_document *,qa_json_id,qa_error *);
bool frontend_unified_q3_presentation(frontend_unified_q3 *, const qa_unified_document *, qa_json_id, bool *mirrored, qa_error *);
bool frontend_unified_q3_simulation(frontend_unified_q3 *, const qa_unified_document *, qa_json_id, qa_error *);
bool frontend_unified_q3_frame_prepare(frontend_unified_q3 *, const qa_unified_document *, qa_error *);
bool frontend_unified_q3_frame_ready(frontend_unified_q3 *, const qa_unified_document *, qa_error *);
void frontend_unified_q3_frame_commit(frontend_unified_q3 *);
void frontend_unified_q3_frame_abort(frontend_unified_q3 *);
bool frontend_unified_q3_world(frontend_unified_q3 *, const qa_scene_view *, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
bool frontend_unified_q3_lights(frontend_unified_q3 *, const qa_scene_view *, const qa_scene_world_input *, const qa_scene_light **, size_t *, qa_error *);
bool frontend_unified_q3_idle(const frontend_unified_q3 *);
bool frontend_unified_q3_current(const frontend_unified_q3 *);
bool frontend_unified_q3_destroy(frontend_unified_q3 **, qa_error *);
bool frontend_unified_q3_visit(const frontend_unified_q3 *,const qa_application_content_visitor *,qa_error *);
bool frontend_unified_q3_checkpoint(frontend_unified_q3 *, const qa_application_content_graph *,qa_buffer *, qa_error *);
bool frontend_unified_q3_restore(qa_frontend *, frontend_remote_unified *, frontend_unified_media *,struct frontend_unified_components *,const qa_application_content_graph *,qa_bytes, frontend_unified_q3 **, qa_error *);
#endif
