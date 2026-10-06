#ifndef QA_GL_PARTICLES_H
#define QA_GL_PARTICLES_H
#include "internal.h"
bool gl_particles_prepare(qa_gl_renderer *, const qa_scene_particle_batch *, qa_scene_draw *, qa_error *);
void gl_particles_destroy(qa_gl_renderer *);
#endif
