#include "qa/scene_effects.h"

void qa_scene_q1_particle_advance(qa_scene_q1_particle_state *particle, double seconds, float gravity)
{
    static const uint32_t ramp1[8] = {111, 109, 107, 105, 103, 101, 99, 97};
    static const uint32_t ramp2[8] = {111, 110, 109, 108, 107, 106, 104, 102};
    static const uint32_t ramp3[8] = {109, 107, 6, 5, 4, 3, 0, 0};
    particle->origin = qa_v3((float)(particle->origin.x + particle->velocity.x * seconds),
                             (float)(particle->origin.y + particle->velocity.y * seconds),
                             (float)(particle->origin.z + particle->velocity.z * seconds));
    double grav = seconds * gravity * 0.05, scale = 0;
    bool scale_z = true, falling = false;
    const uint32_t *ramp = NULL;
    size_t ramp_count = 0;
    switch (particle->kind) {
    case QA_Q1_PARTICLE_STATIC: break;
    case QA_Q1_PARTICLE_FIRE:
        particle->ramp = (float)(particle->ramp + seconds * 5);
        ramp = ramp3;
        ramp_count = 6;
        particle->velocity.z = (float)(particle->velocity.z + grav);
        break;
    case QA_Q1_PARTICLE_EXPLODE:
        particle->ramp = (float)(particle->ramp + seconds * 10);
        ramp = ramp1;
        ramp_count = 8;
        scale = 4 * seconds;
        falling = true;
        break;
    case QA_Q1_PARTICLE_EXPLODE2:
        particle->ramp = (float)(particle->ramp + seconds * 15);
        ramp = ramp2;
        ramp_count = 8;
        scale = -seconds;
        falling = true;
        break;
    case QA_Q1_PARTICLE_BLOB:
        scale = 4 * seconds;
        falling = true;
        break;
    case QA_Q1_PARTICLE_BLOB2:
        scale = -4 * seconds;
        scale_z = false;
        falling = true;
        break;
    case QA_Q1_PARTICLE_GRAVITY: case QA_Q1_PARTICLE_SLOW_GRAVITY:
        falling = true;
        break;
    }
    if (ramp) {
        if (particle->ramp >= (float)ramp_count) particle->die = -1;
        else if (particle->ramp > -1 && isfinite(particle->ramp)) particle->color = ramp[(size_t)particle->ramp];
    }
    if (scale != 0) {
        particle->velocity.x = (float)(particle->velocity.x + particle->velocity.x * scale);
        particle->velocity.y = (float)(particle->velocity.y + particle->velocity.y * scale);
        if (scale_z) particle->velocity.z = (float)(particle->velocity.z + particle->velocity.z * scale);
    }
    if (falling) particle->velocity.z = (float)(particle->velocity.z - grav);
}

bool qa_scene_q2_particle_sample(const qa_scene_q2_particle_state *particle,
                                 int64_t milliseconds, qa_vec3 *origin, float *alpha)
{
    return qa_scene_q2_particle_sample_at(particle, (double)milliseconds, origin, alpha);
}

bool qa_scene_q2_particle_sample_at(const qa_scene_q2_particle_state *particle,
                                    double milliseconds, qa_vec3 *origin, float *alpha)
{
    if (!particle || !origin || !alpha || !isfinite(milliseconds)) return false;
    double seconds = particle->alpha_velocity == -10000 ? 0 :
        (milliseconds - (double)particle->spawn_milliseconds) * 0.001;
    double amount = particle->alpha + seconds * particle->alpha_velocity;
    if (amount <= 0) return false;
    double square = seconds * seconds;
    *origin = qa_v3((float)(particle->origin.x + particle->velocity.x * seconds + particle->acceleration.x * square),
                    (float)(particle->origin.y + particle->velocity.y * seconds + particle->acceleration.y * square),
                    (float)(particle->origin.z + particle->velocity.z * seconds + particle->acceleration.z * square));
    *alpha = (float)fmin(1, amount);
    return true;
}

bool qa_scene_particle_image(qa_scene_resources *resources, qa_game_family family,
                             qa_scene_image **out, qa_error *error)
{
    static const char q1[8][9] = {"01100000", "11110000", "11110000", "01100000",
                                 "00000000", "00000000", "00000000", "00000000"};
    static const char q2[8][9] = {"00000000", "00110000", "01111000", "01111000",
                                 "00110000", "00000000", "00000000", "00000000"};
    if (!resources || !out || (family != QA_GAME_Q1 && family != QA_GAME_Q2)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Indexed particle image requires Q1 or Q2");
        return false;
    }
    uint8_t pixels[8 * 8 * 4];
    const char (*pattern)[9] = family == QA_GAME_Q1 ? q1 : q2;
    for (size_t y = 0; y < 8; ++y) for (size_t x = 0; x < 8; ++x) {
        size_t offset = (y * 8 + x) * 4;
        pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 255;
        pixels[offset + 3] = pattern[x][y] == '1' ? 255 : 0;
    }
    qa_scene_image_level level = {8, 8, pixels, sizeof(pixels)};
    return qa_scene_image_create(resources, family == QA_GAME_Q1 ? "*q1-particle" : "*q2-particle",
                                 QA_SCENE_RGBA8, &level, 1, QA_SCENE_CLAMP, QA_SCENE_LINEAR,
                                 (qa_scene_vec4){0, 0, 0, 0}, out, error);
}

bool qa_scene_flags_cast_shadow(qa_game_family family, uint32_t flags, float alpha,
                                bool view_model, bool sprite)
{
    if (view_model || sprite) return false;
    if (family == QA_GAME_Q2) return (flags & (4u | 16u | 32u | 128u | 8192u | 0x00200000u)) == 0;
    if (family == QA_GAME_Q3 && (flags & (4u | 8u | 64u))) return false;
    return alpha >= 1;
}
