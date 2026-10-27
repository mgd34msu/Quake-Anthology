/*
 * Port of src/render/scene/particles/q3-system.ts. The TS async
 * loadParticleAnimations/clear are synchronous. Source profile
 * strings are the exact TS "cg_marks.c" | "cg_particles.c".
 * GPL-2.0-or-later.
 */
#ifndef QA_RENDER_SCENE_PARTICLES_Q3_SYSTEM_H
#define QA_RENDER_SCENE_PARTICLES_Q3_SYSTEM_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/core/game_numeric.h"
#include "qa/render/scene/particles/q3_types.h"

enum { QA_MAX_PARTICLES = 1024 };

/* TS AnimationFrames rows. Frames are borrowed shader pointers. */
typedef struct qa_particle_animation_frames {
  const qa_particle_shader **frames; /* owned array of borrowed shaders */
  size_t count;
} qa_particle_animation_frames;
void qa_particle_animation_frames_destroy(qa_particle_animation_frames *frames);

/* TS ParticleAnimations. */
typedef struct qa_particle_animations {
  qa_particle_animation_frames explode1;
} qa_particle_animations;
/* TS StandaloneParticleAnimations. */
typedef struct qa_standalone_particle_animations {
  qa_particle_animation_frames explode1, blacksmokeanim, twiltb2, expblue,
      blacksmokeanimb, blood;
} qa_standalone_particle_animations;
void qa_particle_animations_destroy(qa_particle_animations *animations);
void qa_standalone_particle_animations_destroy(
    qa_standalone_particle_animations *animations);

/* False only on OOM. source NULL is the TS cg_marks.c default
 * overload; pass "cg_particles.c" for the standalone result (only
 * the matching out is written). */
bool qa_load_particle_animations(const qa_particle_resources *resources,
                                 const char *source,
                                 qa_particle_animations *out);
bool qa_load_standalone_particle_animations(
    const qa_particle_resources *resources,
    qa_standalone_particle_animations *out);

/* TS ParticleMedia. */
typedef struct qa_particle_media {
  const qa_particle_shader *tracer_shader;
  const qa_particle_shader *smoke_puff_shader;
  const qa_particle_shader *water_bubble_shader;
} qa_particle_media;

/* TS ParticleHost. hardware_type: "generic" | "rage-pro". */
typedef struct qa_particle_host {
  qa_particle_animations animations;
  qa_standalone_particle_animations standalone_animations; /* cg_particles.c */
  qa_particle_media media;
  qa_particle_tracer prediction;
  qa_game_random *random; /* borrowed */
  const char *hardware_type;
  void *config_ctx;
  const char *(*config_string)(void *ctx, double index);
  void *print_ctx;
  void (*print)(void *ctx, const char *message);
} qa_particle_host;

typedef struct qa_particle_explosion_request {
  const char *animation;
  qa_vec3 origin, velocity;
  double duration, size_start, size_end;
} qa_particle_explosion_request;

typedef struct qa_particle_system qa_particle_system;
struct qa_particle_system {
  qa_particle_client_state state;
  qa_particle_host host;
  void *particles; /* private pool */
  long active, free_list;
  double count;
  double old_time, view_roll;
  qa_axis view_axes, rotated_axes;
  void *animations; /* private registered animations */
  size_t animation_count;
  double capacity, explosion_alpha;
};

/* source NULL is "cg_marks.c". The host animations must match the
 * profile (standalone rows for "cg_particles.c"). */
void qa_particle_system_init(qa_particle_system *system,
                             qa_particle_client_state state,
                             qa_particle_host host, const char *source);
void qa_particle_system_destroy(qa_particle_system *system);
double qa_particle_system_active_count(const qa_particle_system *system);
void qa_particle_system_reset_round(qa_particle_system *system);
/* TS clear(): resetRound then re-register every animation frame. */
void qa_particle_system_clear(qa_particle_system *system,
                              const qa_particle_resources *resources);

void qa_particle_system_explosion(qa_particle_system *system,
                                  qa_particle_explosion_request request);
void qa_particle_system_snow_flurry(qa_particle_system *system,
                                    const qa_particle_shader *shader,
                                    qa_particle_client_entity entity);
void qa_particle_system_snow(qa_particle_system *system,
                             const qa_particle_shader *shader, qa_vec3 origin,
                             qa_vec3 end, bool turbulent, double range,
                             double snum);
void qa_particle_system_bubble(qa_particle_system *system,
                               const qa_particle_shader *shader,
                               qa_vec3 origin, qa_vec3 end, bool turbulent,
                               double range, double snum);
void qa_particle_system_smoke(qa_particle_system *system,
                              const qa_particle_shader *shader,
                              qa_particle_client_entity entity);
void qa_particle_system_bullet_debris(qa_particle_system *system,
                                      qa_vec3 origin, qa_vec3 velocity,
                                      double duration);
void qa_particle_system_add_particle_shrapnel(qa_particle_system *system);
bool qa_particle_system_new_particle_area(qa_particle_system *system,
                                          double index);
void qa_particle_system_snow_link(qa_particle_system *system,
                                  qa_particle_client_entity entity,
                                  bool enabled);
void qa_particle_system_impact_smoke_puff(qa_particle_system *system,
                                          const qa_particle_shader *shader,
                                          qa_vec3 origin);
void qa_particle_system_bleed(qa_particle_system *system,
                              const qa_particle_shader *shader, qa_vec3 start,
                              double flesh_entity_num, double duration);
void qa_particle_system_oil_particle(qa_particle_system *system,
                                     const qa_particle_shader *shader,
                                     qa_particle_client_entity entity);
void qa_particle_system_oil_slick(qa_particle_system *system,
                                  const qa_particle_shader *shader,
                                  qa_particle_client_entity entity);
void qa_particle_system_oil_slick_remove(qa_particle_system *system);
bool qa_particle_system_valid_blood_pool(qa_particle_system *system,
                                         qa_vec3 start);
void qa_particle_system_blood_pool(qa_particle_system *system,
                                   const qa_particle_shader *shader,
                                   qa_vec3 trace_end);
void qa_particle_system_blood_cloud(qa_particle_system *system,
                                    qa_vec3 origin, qa_vec3 direction);
void qa_particle_system_sparks(qa_particle_system *system, qa_vec3 origin,
                               qa_vec3 velocity, double duration, double x,
                               double y, double speed);
qa_vec3 qa_particle_system_dust(qa_particle_system *system, qa_vec3 origin,
                                qa_vec3 direction);
void qa_particle_system_misc(qa_particle_system *system,
                             const qa_particle_shader *shader, qa_vec3 origin,
                             double size, double duration, double alpha);
/* Owned polys; view_origin NULL is TS undefined. */
qa_ref_poly *qa_particle_system_add_particles(qa_particle_system *system,
                                              const qa_vec3 *view_origin,
                                              size_t *out_count);
void qa_ref_polys_destroy(qa_ref_poly *polys, size_t count);

#endif
