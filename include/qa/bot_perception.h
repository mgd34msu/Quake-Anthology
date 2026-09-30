#ifndef QA_BOT_PERCEPTION_H
#define QA_BOT_PERCEPTION_H

#include "qa/actors.h"
#include "qa/math.h"

typedef struct qa_bot_senses {
    float fov_angle,sight_time,sight_decay_time,sound_time,sound_decay_time;
    float forget_non_visible_time,sound_persist_time,sound_range;
    float invisible_sight_scalar,maximum_invisible_distance;
} qa_bot_senses;
typedef struct qa_bot_weapon_senses { float fov_angle,sight_time,decay_time; } qa_bot_weapon_senses;
typedef struct qa_bot_aiming {
    float maximum_speed,spring_stiffness,damping,velocity_offset;
    float modifier_maximum_angle,modifier_apply_time,modifier_acceleration_scalar,modifier_spring_scalar,modifier_damping_scalar;
} qa_bot_aiming;
typedef struct qa_bot_awareness {
    qa_actor_id actor;
    float sight,weapon;
    double last_contact,last_seen,last_heard;
    qa_vec3 last_known_origin;
} qa_bot_awareness;
typedef struct qa_bot_contact {
    bool line_of_sight,in_sight_fov,in_weapon_fov,audible,invisible;
    float distance;
    qa_vec3 origin;
} qa_bot_contact;
typedef struct qa_bot_sight_geometry { bool in_sight_fov,in_weapon_fov,within_invisible_range; float distance; } qa_bot_sight_geometry;
typedef struct qa_bot_sound { qa_vec3 origin; double time; float loudness; } qa_bot_sound;
typedef struct qa_bot_aim_state { float pitch,yaw,pitch_velocity,yaw_velocity; double modifier_until; } qa_bot_aim_state;
typedef struct qa_bot_random { uint32_t state; } qa_bot_random;
qa_bot_awareness qa_bot_awareness_new(qa_actor_id,double now,qa_vec3 origin);
qa_bot_sight_geometry qa_bot_sight(qa_vec3 eye,float pitch,float yaw,qa_vec3 target,bool invisible,const qa_bot_senses *,const qa_bot_weapon_senses *);
void qa_bot_sense_step(qa_bot_awareness *,const qa_bot_contact *,const qa_bot_senses *,const qa_bot_weapon_senses *,float elapsed,double now);
bool qa_bot_should_forget(const qa_bot_awareness *,const qa_bot_senses *,double now);
bool qa_bot_sound_audible(const qa_bot_sound *,qa_vec3 listener,const qa_bot_senses *,double now);
qa_vec3 qa_bot_aim_lead(qa_vec3 target,qa_vec3 velocity,const qa_bot_aiming *);
void qa_bot_aim_step(qa_bot_aim_state *,qa_vec3 direction,const qa_bot_aiming *,float elapsed,double now);
float qa_bot_aim_error(const qa_bot_aim_state *,qa_vec3 direction);
void qa_bot_random_seed(qa_bot_random *,uint32_t seed);
uint32_t qa_bot_random_word(qa_bot_random *);
double qa_bot_random_unit(qa_bot_random *);
uint32_t qa_bot_random_index(qa_bot_random *,uint32_t count);
bool qa_bot_random_chance(qa_bot_random *,float percent);
bool qa_bot_random_restore(qa_bot_random *,uint32_t state,qa_error *);

#endif
