#include "qa/scene_effects.h"
#include <limits.h>
#include <math.h>

qa_scene_particle_sample *qa_scene_particles_alloc(qa_scene_frame *frame, size_t count, qa_error *error)
{
    if (!frame || count > SIZE_MAX / sizeof(qa_scene_particle_sample)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Particle batch exceeds native storage");
        return NULL;
    }
    if (!count) return NULL;
    return qa_arena_alloc(&frame->storage, count * sizeof(qa_scene_particle_sample),
        _Alignof(qa_scene_particle_sample), error);
}

bool qa_scene_particles(qa_scene_frame *frame, const qa_scene_particle_batch *batch, qa_error *error)
{
    if (!frame || !batch || (batch->family != QA_SCENE_Q1 && batch->family != QA_SCENE_Q2) ||
        (batch->count && (!batch->samples || !batch->image)) ||
        !batch->view.viewport.width || !batch->view.viewport.height ||
        batch->view.viewport.width > INT_MAX || batch->view.viewport.height > INT_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid particle batch storage or view");
        return false;
    }
    if (!batch->count) return true;
    for (size_t i = 0; i < batch->count; ++i) {
        const qa_scene_particle_sample *sample = batch->samples + i;
        if (!qa_vec_finite(sample->origin) || !isfinite(sample->color.x) ||
            !isfinite(sample->color.y) || !isfinite(sample->color.z) || !isfinite(sample->color.w)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "Nonfinite particle sample");
            return false;
        }
    }
    qa_scene_command command = {.kind = QA_SCENE_COMMAND_PARTICLES, .data.particles = *batch};
    return qa_scene_frame_emit(frame, &command, error);
}
