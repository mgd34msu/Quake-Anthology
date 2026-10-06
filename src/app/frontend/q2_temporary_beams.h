#ifndef QA_FRONTEND_Q2_TEMPORARY_BEAMS_H
#define QA_FRONTEND_Q2_TEMPORARY_BEAMS_H
#include "q2_effect_models.h"
#include "selected_effects_particles.h"
#include "qa/network_q2_messages.h"
#include "qa/scene.h"
typedef struct frontend_q2_temporary_beam {
    bool active, player, monster, unkeyed;
    uint8_t model;
    qa_actor_id actor, destination;
    qa_vec3 start, end, offset;
    double die, sound_until;
} frontend_q2_temporary_beam;
typedef struct frontend_q2_beam_recipe {
    q2fx_model model;
    qa_vec3 offset;
    bool player,monster,lightning_sound,destination,independent;
    double duration_seconds;
} frontend_q2_beam_recipe;
typedef struct frontend_q2_beam_random {
    uint32_t bin,base,seed;
    uint64_t frame;
} frontend_q2_beam_random;
typedef struct frontend_q2_beam_draw {
    uint8_t model;
    qa_vec3 origin,angles;
    int32_t frame,old_frame,skin;
    uint32_t flags;
    float alpha,back_lerp;
    qa_vec3 scale;
} frontend_q2_beam_draw;
typedef struct frontend_q2_beam_view {
    double milliseconds;
    qa_scene_view view;
    qa_actor_id viewer;
    qa_vec3 viewer_origin,gun_offset;
    float player_fov,gun_fov;
    int32_t hand,gun;
    bool viewer_origin_present,hardware;
} frontend_q2_beam_view;
typedef struct frontend_q2_beam_context {
    bool rerelease;
    size_t active_beams;
    frontend_fx_particles *particles;
    qa_builtin_random *random;
    frontend_q2_beam_random *roll;
    void *context;
    bool (*model_ready)(void *,q2fx_model);
    bool (*draw)(void *,const frontend_q2_beam_draw *,qa_error *);
    bool (*render_clock)(void *,uint64_t *,uint64_t *,qa_error *);
} frontend_q2_beam_context;
bool frontend_q2_named_temporary(const char *,qa_vec3,qa_vec3,qa_q2_temp_entity *);
bool frontend_q2_beam_named_recipe(const char *,qa_vec3 offset,double duration_seconds,frontend_q2_beam_recipe *);
bool frontend_q2_beam_recipe_read(uint32_t type,qa_vec3 offset,frontend_q2_beam_recipe *);
frontend_q2_temporary_beam *frontend_q2_beam_retain(frontend_q2_temporary_beam *,size_t,
    bool rerelease,const frontend_q2_beam_recipe *,qa_actor_id,qa_actor_id,
    qa_vec3 start,qa_vec3 end,double milliseconds);
bool frontend_q2_beam_lightning_sound(frontend_q2_temporary_beam *,bool rerelease,double milliseconds);
bool frontend_q2_beams_prepare(frontend_q2_beam_context *,frontend_q2_temporary_beam *,size_t,
    const frontend_q2_beam_view *,bool advance,qa_error *);
void frontend_q2_temporary_radial(frontend_fx_particles *,qa_builtin_random *,bool rerelease,
    qa_vec3,double milliseconds,int32_t count,float radius,float speed,uint32_t palette,bool instant);
#endif
