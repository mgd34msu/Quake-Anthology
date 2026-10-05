#ifndef QA_Q3_NATIVE_PARTICLES_INTERNAL_H
#define QA_Q3_NATIVE_PARTICLES_INTERNAL_H
#include "particles.h"
#include "events.h"
#include "../q3/internal.h"

enum { Q3N_PARTICLE_CAPACITY = 1024, Q3N_PARTICLE_FRAMES = 23 };
/* Fields reached by the selected CGAME's actual explosion producer. Free
 * slots retain these values until reused; their shader changes during draw. */
typedef struct q3n_particle {
    int32_t next, type, roll, shader;
    float time, end_time, alpha, alpha_velocity;
    float width, height, end_width, end_height;
    qa_vec3 origin, velocity, acceleration;
} q3n_particle;
struct q3n_particles {
    qa_q3_presentation_assets *assets;
    qa_q3_product product;
    q3n_remote_source *remote_source;
    q3n_compiled_source *compiled_source;
    q3n_particle slots[Q3N_PARTICLE_CAPACITY];
    int32_t shaders[Q3N_PARTICLE_FRAMES];
    int32_t active, free;
    uint32_t count;
    float old_time, view_roll;
    qa_vec3 view_axes[3], rotated_axes[3];
    bool initialized, busy;
};
bool q3np_fail(qa_error *, qa_status, const char *);
#endif
