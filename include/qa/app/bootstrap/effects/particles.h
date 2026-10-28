/* Port of src/app/bootstrap/effects/particles.ts (Quake r_part.c /
 * Quake II cl_fx.c particle constructors). Random draw order and
 * Math.fround staging match the TS. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_EFFECTS_PARTICLES_H
#define QA_APP_BOOTSTRAP_EFFECTS_PARTICLES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
/* TS SourceParticles. random is borrowed (TS readonly field). */
typedef struct qa_app_source_particles qa_app_source_particles;
/* capacity < 0 selects the TS default 4096. */
qa_app_source_particles *qa_app_source_particles_create(qa_app_sim_source_random *random, double capacity);
void qa_app_source_particles_destroy(qa_app_source_particles *particles);
qa_app_sim_source_random *qa_app_source_particles_random(const qa_app_source_particles *particles);
double qa_app_source_particles_capacity(const qa_app_source_particles *particles);
void qa_app_source_particles_clear(qa_app_source_particles *particles);
/* TS sample(): malloc'd arrays (caller frees each array). */
void qa_app_source_particles_sample(qa_app_source_particles *particles, double seconds, double elapsed,
    qa_scene_particle **out_q1, size_t *out_q1_count, qa_scene_particle **out_q2, size_t *out_q2_count);
void qa_app_source_particles_q1_impact(qa_app_source_particles *p, qa_vec3 origin, qa_vec3 direction,
                                       double color, double count, double seconds);
void qa_app_source_particles_q1_entity(qa_app_source_particles *p, qa_vec3 origin, double seconds);
/* type: TS 0|1|2|3|4|5|6. */
void qa_app_source_particles_q1_trail(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end,
                                      double type, double seconds);
void qa_app_source_particles_q1_explosion(qa_app_source_particles *p, qa_vec3 origin, double seconds, bool blob);
/* Aborts with the TS Error text for an invalid palette range. */
void qa_app_source_particles_q1_color_explosion(qa_app_source_particles *p, qa_vec3 origin, double seconds,
                                                double color_start, double color_length);
void qa_app_source_particles_q1_splash(qa_app_source_particles *p, qa_vec3 origin, double seconds, bool lava);
/* variant: "normal" | "fixed" | "up" | "blaster". */
void qa_app_source_particles_q2_impact(qa_app_source_particles *p, qa_vec3 origin, qa_vec3 direction,
                                       double color, double count, double seconds, const char *variant);
void qa_app_source_particles_q2_explosion(qa_app_source_particles *p, qa_vec3 origin, double seconds, bool bfg);
void qa_app_source_particles_q2_color_explosion(qa_app_source_particles *p, qa_vec3 origin, double seconds,
                                                double color, double run);
void qa_app_source_particles_q2_berserk_slam(qa_app_source_particles *p, qa_vec3 origin, qa_vec3 direction, double seconds);
bool qa_app_source_particles_q2_steam(qa_app_source_particles *p, qa_vec3 origin, qa_vec3 direction,
                                      double color, double count, double magnitude, double seconds, bool smoke);
void qa_app_source_particles_q2_force_wall(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end,
                                           double color, double seconds);
void qa_app_source_particles_q2_tracker_trail(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end, double seconds);
void qa_app_source_particles_q2_tracker_shell(qa_app_source_particles *p, qa_vec3 origin, double seconds);
/* kind: "item" | "login" | "logout" | "respawn". */
void qa_app_source_particles_q2_respawn(qa_app_source_particles *p, qa_vec3 origin, double seconds, const char *kind);
void qa_app_source_particles_q2_teleport(qa_app_source_particles *p, qa_vec3 origin, double seconds);
void qa_app_source_particles_q2_big_teleport(qa_app_source_particles *p, qa_vec3 origin, double seconds);
void qa_app_source_particles_q2_teleporter(qa_app_source_particles *p, qa_vec3 origin, double seconds);
void qa_app_source_particles_q2_blaster_trail(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end,
                                              double seconds, bool green);
/* kind: "rocket" | "smoke" | "blood" | "green-blood". Returns count. */
double qa_app_source_particles_q2_diminishing_trail(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end,
                                                    double seconds, double count, const char *kind);
void qa_app_source_particles_q2_rail(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end, double seconds);
void qa_app_source_particles_q2_bubbles(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end, double seconds);
#endif
