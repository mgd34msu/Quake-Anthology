#ifndef QA_FRONTEND_SELECTED_EFFECTS_PARTICLES_H
#define QA_FRONTEND_SELECTED_EFFECTS_PARTICLES_H

#include "qa/builtin.h"
#include "qa/scene_effects.h"
#include "qa/source_save.h"

enum { FRONTEND_FX_PARTICLE_CAPACITY = 4096 };
typedef struct frontend_fx_q2_particle {
    double spawn_milliseconds;
    qa_vec3 origin, velocity, acceleration;
    uint32_t color, rgba;
    float alpha, alpha_velocity;
} frontend_fx_q2_particle;
typedef struct frontend_fx_particles {
    qa_game_family family;
    size_t count;
    uint32_t tracer_count;
    qa_vec3 angular[QA_BYTE_NORMAL_COUNT];
    union {
        qa_scene_q1_particle_state q1[FRONTEND_FX_PARTICLE_CAPACITY];
        frontend_fx_q2_particle q2[FRONTEND_FX_PARTICLE_CAPACITY];
    } values;
} frontend_fx_particles;
typedef enum frontend_fx_q2_impact {
    FRONTEND_FX_Q2_NORMAL, FRONTEND_FX_Q2_FIXED, FRONTEND_FX_Q2_UP, FRONTEND_FX_Q2_BLASTER
} frontend_fx_q2_impact;
typedef enum frontend_fx_q2_respawn {
    FRONTEND_FX_Q2_ITEM, FRONTEND_FX_Q2_LOGIN, FRONTEND_FX_Q2_LOGOUT, FRONTEND_FX_Q2_RESPAWN
} frontend_fx_q2_respawn;
typedef enum frontend_fx_q2_trail {
    FRONTEND_FX_Q2_ROCKET, FRONTEND_FX_Q2_SMOKE, FRONTEND_FX_Q2_BLOOD, FRONTEND_FX_Q2_GREEN_BLOOD
} frontend_fx_q2_trail;

/* The true application effect owner supplies its single RNG stream. Each
 * content group retains its own append array and source tracer/angular state. */
void frontend_fx_q1_impact(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3,
    uint32_t color, int32_t count, double seconds);
void frontend_fx_q1_entity(frontend_fx_particles *, qa_builtin_random *, qa_vec3, double);
void frontend_fx_q1_trail(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3,
    uint32_t type, double);
void frontend_fx_q1_explosion(frontend_fx_particles *, qa_builtin_random *, qa_vec3, double, bool blob);
void frontend_fx_q1_color_explosion(frontend_fx_particles *, qa_builtin_random *, qa_vec3,
    double, uint32_t color_start, uint32_t color_length);
void frontend_fx_q1_splash(frontend_fx_particles *, qa_builtin_random *, qa_vec3, double, bool lava);
void frontend_fx_q2_impact_particles(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3,
    uint32_t color, int32_t count, double, frontend_fx_q2_impact);
void frontend_fx_q2_explosion(frontend_fx_particles *, qa_builtin_random *, qa_vec3, double, bool bfg);
void frontend_fx_q2_color_explosion(frontend_fx_particles *, qa_builtin_random *, qa_vec3,
    double, uint32_t color, uint32_t run);
void frontend_fx_q2_berserk(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3, double);
bool frontend_fx_q2_steam(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3,
    uint32_t color, int32_t count, float magnitude, double, bool smoke);
void frontend_fx_q2_force_wall(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3, uint32_t, double);
void frontend_fx_q2_tracker_trail(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3, double);
void frontend_fx_q2_tracker_shell(frontend_fx_particles *, qa_builtin_random *, qa_vec3, double);
void frontend_fx_q2_respawn_particles(frontend_fx_particles *, qa_builtin_random *, qa_vec3,
    double, frontend_fx_q2_respawn);
void frontend_fx_q2_teleport(frontend_fx_particles *, qa_builtin_random *, qa_vec3, double);
void frontend_fx_q2_big_teleport(frontend_fx_particles *, qa_builtin_random *, qa_vec3, double);
void frontend_fx_q2_teleporter(frontend_fx_particles *, qa_builtin_random *, qa_vec3, double);
void frontend_fx_q2_blaster_trail(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3, double, bool);
int32_t frontend_fx_q2_diminishing_trail(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3,
    double, int32_t count, frontend_fx_q2_trail);
void frontend_fx_q2_rail(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3, double);
void frontend_fx_q2_rail_spiral(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3,
    double seconds, double lifetime_seconds, float radius, uint32_t rgba);
void frontend_fx_q2_bubbles(frontend_fx_particles *, qa_builtin_random *, qa_vec3, qa_vec3, double);
/* Pure fields. Decode into detached candidate state before owner adoption. */
bool frontend_fx_q2_sample(const frontend_fx_q2_particle *, double milliseconds, qa_vec3 *, float *alpha);

#endif
