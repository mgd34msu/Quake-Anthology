#ifndef QA_FRONTEND_REMOTE_UNIFIED_Q3_H
#define QA_FRONTEND_REMOTE_UNIFIED_Q3_H
#include "remote_unified_media.h"
#include "qa/persistence_content.h"
#include "qa/q3_source_scene_bank.h"
#include "../../presentation/q3_native/player_fx.h"
typedef struct frontend_unified_q3 frontend_unified_q3;
struct frontend_unified_events;
struct frontend_unified_components;
struct frontend_unified_render_equipment;
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
bool frontend_unified_q3_reflected_world(frontend_unified_q3 *,const qa_scene_world_input *,qa_scene_frame *,qa_error *);
bool frontend_unified_q3_lights(frontend_unified_q3 *, const qa_scene_view *, const qa_scene_world_input *, const qa_scene_light **, size_t *, qa_error *);
/* The pre-WORLD sampler owns this actual shared Source bank. Component lights
 * and HUD packets borrow its returned physical recipient, never a private CG. */
bool frontend_unified_q3_scene_bank_read(frontend_unified_q3 *, const qa_scene_frame *,
    qa_q3_source_scene_bank **, qa_error *);
bool frontend_unified_q3_hud_recipient_read(frontend_unified_q3 *, const qa_scene_frame *,
    qa_q3_presentation **, qa_error *);
bool frontend_unified_q3_body_hidden(void *, const q3n_frame *, const q3n_compiled_entity *, bool *, qa_error *);
bool frontend_unified_q3_body_submit(void *, const q3n_frame *, const q3n_compiled_entity *, uint32_t,
    const qa_q3_ref_entity *, bool, bool *, qa_error *);
bool frontend_unified_q3_player_weapon(void *, const q3n_frame *, const q3n_compiled_entity *,
    const qa_q3_ref_entity *, int32_t, qa_error *);
/* The parent reads this receipt from its actual registered renderer and
 * requalifies that renderer after the returned composition call. */
bool frontend_unified_q3_equipment_replacement(frontend_unified_q3 *,const q3n_frame *,const qa_q3_player *,
    const struct frontend_unified_render_equipment *,bool present,bool *consumed,qa_error *);
bool frontend_unified_q3_idle(const frontend_unified_q3 *);
bool frontend_unified_q3_current(const frontend_unified_q3 *);
bool frontend_unified_q3_destroy(frontend_unified_q3 **, qa_error *);
bool frontend_unified_q3_visit(const frontend_unified_q3 *,const qa_application_content_visitor *,qa_error *);
bool frontend_unified_q3_checkpoint(frontend_unified_q3 *, const qa_application_content_graph *,qa_buffer *, qa_error *);
bool frontend_unified_q3_restore(qa_frontend *, frontend_remote_unified *, frontend_unified_media *,struct frontend_unified_components *,const qa_application_content_graph *,qa_bytes, frontend_unified_q3 **, qa_error *);
#endif
