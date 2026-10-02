#include "selected_effects_particles.h"
#include <math.h>

bool frontend_fx_particles_fields(qa_source_save_io *io, frontend_fx_particles *state)
{
    uint32_t family = state->family;
    if (!qa_source_save_u32(io, &family) || (family != QA_GAME_Q1 && family != QA_GAME_Q2) ||
        !qa_source_save_count(io, &state->count, FRONTEND_FX_PARTICLE_CAPACITY) ||
        !qa_source_save_u32(io, &state->tracer_count)) return false;
    state->family = (qa_game_family)family;
    for (unsigned i = 0; i < QA_BYTE_NORMAL_COUNT; ++i)
        if (!qa_source_save_vec3(io, &state->angular[i]) || !qa_vec_finite(state->angular[i])) return false;
    for (size_t i = 0; i < state->count; ++i) {
        if (state->family == QA_GAME_Q1) {
            qa_scene_q1_particle_state *value = &state->values.q1[i];
            uint32_t kind = value->kind;
            if (!qa_source_save_vec3(io, &value->origin) || !qa_source_save_vec3(io, &value->velocity) ||
                !qa_source_save_f32(io, &value->ramp) || !qa_source_save_f64(io, &value->die) ||
                !qa_source_save_u32(io, &value->color) || !qa_source_save_u32(io, &kind) ||
                kind > QA_Q1_PARTICLE_SLOW_GRAVITY || !qa_vec_finite(value->origin) ||
                !qa_vec_finite(value->velocity) || !isfinite(value->ramp) || !isfinite(value->die)) return false;
            value->kind = (qa_scene_q1_particle_kind)kind;
        } else {
            frontend_fx_q2_particle *value = &state->values.q2[i];
            if (!qa_source_save_f64(io, &value->spawn_milliseconds) ||
                !qa_source_save_vec3(io, &value->origin) || !qa_source_save_vec3(io, &value->velocity) ||
                !qa_source_save_vec3(io, &value->acceleration) || !qa_source_save_u32(io, &value->color) ||
                !qa_source_save_f32(io, &value->alpha) || !qa_source_save_f32(io, &value->alpha_velocity) ||
                !isfinite(value->spawn_milliseconds) || !qa_vec_finite(value->origin) ||
                !qa_vec_finite(value->velocity) || !qa_vec_finite(value->acceleration) ||
                !isfinite(value->alpha) || !isfinite(value->alpha_velocity)) return false;
        }
    }
    return true;
}
