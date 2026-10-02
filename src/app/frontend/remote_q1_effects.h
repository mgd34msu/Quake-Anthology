#ifndef QA_FRONTEND_REMOTE_Q1_EFFECTS_H
#define QA_FRONTEND_REMOTE_Q1_EFFECTS_H
#include "remote_q1_client.h"
#include "qa/source_save.h"

typedef struct frontend_remote_q1_effects frontend_remote_q1_effects;
typedef struct frontend_remote_q1_restore_refs frontend_remote_q1_restore_refs;
bool remote_q1_effects_service(frontend_remote_q1 *, const qa_nq_message *, qa_error *);
bool remote_q1_effects_clear(frontend_remote_q1 *, qa_error *);
bool remote_q1_effects_audio_detach(frontend_remote_q1 *,qa_error *);
bool remote_q1_effects_scene(frontend_remote_q1 *, double,
    const qa_scene_light **, size_t *, qa_error *);
bool remote_q1_effects_draw(frontend_remote_q1 *, const qa_scene_view *,
    const qa_scene_world_input *, qa_error *);
bool remote_q1_effects_models(frontend_remote_q1 *,const qa_scene_view *,const qa_scene_world_input *,qa_error *);
bool remote_q1_effects_blend(frontend_remote_q1 *,const qa_scene_view *,double,qa_scene_vec4,qa_error *);
/* Cold fields contain no sound dispatch or resource opening. Images are
 * restored through the enclosing owner's genuine shared image graph. */
bool remote_q1_effects_fields(frontend_remote_q1 *, qa_source_save_io *,
    const frontend_remote_q1_restore_refs *, qa_error *);
bool remote_q1_effects_restore_finish(frontend_remote_q1 *,const frontend_remote_q1_restore_refs *,qa_error *);
size_t remote_q1_effects_light_count(const frontend_remote_q1 *);
bool remote_q1_effects_light_at(const frontend_remote_q1 *,size_t,uint64_t *);
size_t remote_q1_effects_static_count(const frontend_remote_q1 *);
bool remote_q1_effects_static_at(const frontend_remote_q1 *,size_t,uint64_t *,const qa_audio_asset **,qa_audio_mixer **);
const qa_scene_image *remote_q1_effects_particle_image(const frontend_remote_q1 *);
#endif
