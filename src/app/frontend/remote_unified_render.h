#ifndef QA_FRONTEND_REMOTE_UNIFIED_RENDER_H
#define QA_FRONTEND_REMOTE_UNIFIED_RENDER_H
#include "remote_unified_media.h"
#include "qa/hud.h"
#include "remote_unified_prediction.h"
#include "qa/unified_frame_visuals.h"
typedef struct frontend_unified_render frontend_unified_render;
bool frontend_unified_render_pending_current(const frontend_unified_render *);
typedef struct frontend_unified_render_equipment {
    qa_actor_id actor;
    uint32_t provider;
    const char *instance,*content,*path;
    bool slot,visible;
    frontend_unified_model binding;
    const qa_scene_model_input *input;
    uint64_t source_frame,scene_sequence;
} frontend_unified_render_equipment;
/* Reads only a published view-weapon row and its existing registered model
 * binding. This receipt does not claim that a model draw has completed. */
bool frontend_unified_render_equipment_read(const frontend_unified_render *,qa_actor_id,
    frontend_unified_render_equipment *,bool *present,qa_error *);
typedef struct frontend_unified_render_entity_effects {
    qa_actor_id actor;
    const qa_product *product;
    const qa_model *model;
    qa_scene_family family;
    qa_vec3 origin, angles;
    uint64_t effects;
    uint32_t q1_effects;
} frontend_unified_render_entity_effects;
typedef struct frontend_unified_render_children {
    void *context;
    bool (*camera)(void *,qa_scene_view *,float *source_fov,bool *owned,qa_error *);
    bool (*status_replacement)(void *,bool *,qa_error *);
    void (*q1_status)(void *,qa_hud_q1_status *);
    bool (*source_model)(void *,qa_actor_id,uint32_t provider,const char *instance,bool *owned,qa_error *);
    bool (*equipment_model)(void *,qa_actor_id,uint32_t provider,const char *instance,bool slot,bool *owned,qa_error *);
    bool (*selected_weapon)(void *,const qa_unified_model_state *,const qa_scene_world_input *,
        qa_scene_frame *,bool *submitted,qa_error *);
    bool (*view_origin)(void *,qa_actor_id,qa_vec3,float player_fov,qa_error *);
    bool (*entity_effects)(void *,const frontend_unified_render_entity_effects *,double,qa_error *);
    bool (*entity_beam)(void *,const char *,const qa_scene_view *,qa_vec3,qa_vec3,
        uint32_t,int32_t,qa_scene_frame *,qa_error *);
    bool (*world_input)(void *, qa_scene_world_input *, qa_error *);
    bool (*lights)(void *, const qa_scene_view *, const qa_scene_world_input *,
        const qa_scene_light **, size_t *, qa_error *);
    bool (*reflected_lights)(void *,qa_scene_world_input *,qa_scene_frame *,qa_error *);
    bool (*world)(void *, const qa_scene_view *, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
    bool (*reflected_world)(void *,const qa_scene_world_input *,qa_scene_frame *,qa_error *);
    bool (*world_models)(void *,const qa_scene_world_input *,qa_scene_frame *,qa_error *);
    bool (*particles)(void *,const qa_scene_world_input *,qa_scene_frame *,qa_error *);
    bool (*dlights)(void *,const qa_scene_world_input *,qa_scene_frame *,qa_scene_vec4 *,qa_error *);
    bool (*blend)(void *,const qa_scene_world_input *,qa_scene_vec4,qa_scene_frame *,qa_error *);
    bool (*player_blend)(void *,qa_actor_id,bool,const qa_scene_vec4 *,bool,const qa_scene_vec4 *,
        qa_scene_rect,qa_scene_frame *,qa_error *);
    bool (*hud)(void *, qa_ui *, qa_scene_rect, qa_scene_frame *, qa_error *);
    bool (*model)(void *, qa_actor_id, const char *content, const char *path, qa_scene_model_input *, qa_error *);
    bool (*model_after)(void *, qa_actor_id, const char *content, const char *path,
        const qa_scene_model_input *, qa_scene_frame *, qa_error *);
} frontend_unified_render_children;
/* Owns a clone of one actually received frame and registered immutable model
 * bindings. No local application world or player state is observed. */
bool frontend_unified_render_create(qa_frontend *, frontend_remote_unified *,
    frontend_unified_media *, const qa_unified_document *, frontend_unified_render **, qa_error *);
/* Reads declared QC values from the existing received FRAME for its full player. */
bool frontend_unified_render_client_presentation_read(const frontend_unified_render *,qa_actor_id,
    qa_application_camera_view *,qa_hud_value vitals[2],bool *has_view,bool *has_vitals,qa_error *);
bool frontend_unified_render_draw(frontend_unified_render *,
    const frontend_unified_prediction_view *, const frontend_unified_render_children *,
    float stereo, qa_audio_listener *, qa_error *);
bool frontend_unified_render_idle(const frontend_unified_render *);
bool frontend_unified_render_destroy(frontend_unified_render **, qa_error *);
#endif
