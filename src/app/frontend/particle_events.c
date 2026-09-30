#include "internal.h"
#include "qa/scene_effects.h"

enum { FRONTEND_PARTICLE_CAPACITY = 4096, FRONTEND_STEAM_CAPACITY = 32 };
typedef struct frontend_particle_owner {
    struct frontend_particle_owner *next;
    qa_actor_owner provider;
    qa_game_family family;
    qa_scene_resources *images;
    qa_scene_image *particle_image;
    qa_builtin_random random;
    float gravity;
    qa_scene_q1_particle_state *q1;
    qa_scene_q2_particle_state *q2;
    size_t count;
} frontend_particle_owner;
typedef struct frontend_steam {
    frontend_particle_owner *owner;
    qa_q2_map_event event;
    uint64_t end_ns, next_ns;
} frontend_steam;
struct frontend_particle_state {
    frontend_particle_owner *owners;
    frontend_steam steam[FRONTEND_STEAM_CAPACITY];
    size_t steam_count;
    uint64_t sample_ns;
};
static bool particle_owner(qa_frontend *frontend, qa_actor_owner provider, qa_game_family family,
    frontend_particle_owner **out, qa_error *error)
{
    if (!frontend->particles) {
        frontend->particles = calloc(1, sizeof(*frontend->particles));
        if (!frontend->particles) return frontend_fail(error, QA_ERROR_MEMORY, "allocating frontend particle continuation");
        frontend->particles->sample_ns = qa_session_elapsed(qa_application_session(frontend->application));
    }
    for (frontend_particle_owner *owner = frontend->particles->owners; owner; owner = owner->next)
        if (owner->provider == provider && owner->family == family) { *out = owner; return true; }
    frontend_particle_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "allocating provider particle pool");
    owner->provider = provider; owner->family = family;
    qa_builtin_random_seed(&owner->random, 1);
    if (family == QA_GAME_Q1) owner->q1 = calloc(FRONTEND_PARTICLE_CAPACITY, sizeof(*owner->q1));
    else owner->q2 = calloc(FRONTEND_PARTICLE_CAPACITY, sizeof(*owner->q2));
    bool ok = (owner->q1 || owner->q2) && frontend_event_images(frontend, provider, family, &owner->images, error) &&
        qa_scene_particle_image(owner->images, family == QA_GAME_Q1 ? QA_SCENE_Q1 : QA_SCENE_Q2, &owner->particle_image, error);
    if (!ok) {
        if (!owner->q1 && !owner->q2) frontend_fail(error, QA_ERROR_MEMORY, "allocating bounded source particle pool");
        qa_scene_image_release(owner->particle_image); free(owner->q1); free(owner->q2); free(owner); return false;
    }
    owner->next = frontend->particles->owners; frontend->particles->owners = owner; *out = owner; return true;
}
void frontend_particle_retire(qa_frontend *frontend)
{
    if (!frontend->particles) return;
    while (frontend->particles->owners) {
        frontend_particle_owner *owner = frontend->particles->owners;
        frontend->particles->owners = owner->next;
        qa_scene_image_release(owner->particle_image); free(owner->q1); free(owner->q2); free(owner);
    }
    free(frontend->particles); frontend->particles = NULL;
}
static uint32_t particle_random(frontend_particle_owner *owner)
{ return qa_builtin_random_integer(&owner->random); }
static float particle_unit(frontend_particle_owner *owner)
{ return qa_builtin_random_unit(&owner->random); }
static float particle_signed(frontend_particle_owner *owner)
{ return particle_unit(owner) * 2 - 1; }
static void q1_particles(frontend_particle_owner *owner, const qa_builtin_event *event)
{
    double seconds = (double)event->time_ns / 1e9;
    for (int i = 0; i < event->count && owner->count < FRONTEND_PARTICLE_CAPACITY; ++i) {
        qa_scene_q1_particle_state *particle = &owner->q1[owner->count++];
        if (event->count == 1024) {
            float ramp = (float)(particle_random(owner) & 3);
            qa_vec3 origin, velocity;
            origin.x = event->origin.x + (float)(particle_random(owner) % 32) - 16;
            velocity.x = (float)(particle_random(owner) % 512) - 256;
            origin.y = event->origin.y + (float)(particle_random(owner) % 32) - 16;
            velocity.y = (float)(particle_random(owner) % 512) - 256;
            origin.z = event->origin.z + (float)(particle_random(owner) % 32) - 16;
            velocity.z = (float)(particle_random(owner) % 512) - 256;
            *particle = (qa_scene_q1_particle_state){.origin = origin, .velocity = velocity,
                .ramp = ramp, .color = 111, .die = seconds + 5,
                .kind = (i & 1) ? QA_Q1_PARTICLE_EXPLODE : QA_Q1_PARTICLE_EXPLODE2};
        } else {
            double die = seconds + .1 * (particle_random(owner) % 5);
            uint32_t color = ((uint32_t)event->code & ~7u) + (particle_random(owner) & 7);
            qa_vec3 jitter;
            jitter.x = (float)(particle_random(owner) & 15) - 8;
            jitter.y = (float)(particle_random(owner) & 15) - 8;
            jitter.z = (float)(particle_random(owner) & 15) - 8;
            *particle = (qa_scene_q1_particle_state){.origin = qa_vec_add(event->origin, jitter),
                .velocity = qa_vec_scale(event->direction, 15), .color = color, .die = die,
                .kind = QA_Q1_PARTICLE_SLOW_GRAVITY};
        }
    }
}
static bool steam_particles(frontend_particle_owner *owner, const qa_q2_map_event *event, uint64_t now)
{
    qa_vec3 direction = event->direction, initial = qa_v3(direction.z, -direction.x, direction.y);
    qa_vec3 right = qa_vec_normalize(qa_vec_sub(initial, qa_vec_scale(direction, qa_vec_dot(initial, direction))));
    qa_vec3 up = qa_vec_cross(right, direction);
    for (int i = 0; i < event->count; ++i) {
        if (owner->count == FRONTEND_PARTICLE_CAPACITY) return false;
        uint32_t color = (uint32_t)event->style + (particle_random(owner) & 7);
        qa_vec3 jitter;
        jitter.x = event->value * .1f * particle_signed(owner);
        jitter.y = event->value * .1f * particle_signed(owner);
        jitter.z = event->value * .1f * particle_signed(owner);
        qa_vec3 velocity = qa_vec_add(qa_vec_scale(direction, event->value), qa_vec_scale(right, particle_signed(owner) * event->value / 3));
        velocity = qa_vec_add(velocity, qa_vec_scale(up, particle_signed(owner) * event->value / 3));
        owner->q2[owner->count++] = (qa_scene_q2_particle_state){.spawn_milliseconds = (int64_t)(now / UINT64_C(1000000)),
            .origin = qa_vec_add(event->origin, jitter), .velocity = velocity,
            .acceleration = {0, 0, -20}, .color = color, .alpha = 1,
            .alpha_velocity = -1 / (.5f + particle_unit(owner) * .3f)};
    }
    return true;
}
static bool force_wall(frontend_particle_owner *owner, const qa_q2_map_event *event, uint64_t now, qa_error *error)
{
    qa_vec3 delta = qa_vec_sub(event->direction, event->origin);
    float length = qa_vec_length(delta);
    if (!isfinite(length)) return frontend_fail(error, QA_ERROR_ARGUMENT, "force wall exceeds native coordinate range");
    qa_vec3 direction = qa_vec_normalize(delta);
    for (double distance = 0; distance < length && owner->count < FRONTEND_PARTICLE_CAPACITY; distance += 4) {
        if (particle_unit(owner) <= .3f) continue;
        float alpha_velocity = -1 / (3 + particle_unit(owner) * .5f);
        qa_vec3 jitter;
        jitter.x = particle_signed(owner) * 3; jitter.y = particle_signed(owner) * 3; jitter.z = particle_signed(owner) * 3;
        qa_vec3 origin = qa_vec_add(qa_vec_add(event->origin, qa_vec_scale(direction, (float)distance)), jitter);
        owner->q2[owner->count++] = (qa_scene_q2_particle_state){.spawn_milliseconds = (int64_t)(now / UINT64_C(1000000)),
            .origin = origin, .velocity = {0, 0, -40 - particle_signed(owner) * 10},
            .color = (uint32_t)event->style, .alpha = 1, .alpha_velocity = alpha_velocity};
    }
    return true;
}
bool frontend_particle_events(qa_frontend *frontend, qa_error *error)
{
    for (size_t i = 0; i < qa_application_event_count(frontend->application); ++i) {
        qa_builtin_event event;
        if (!qa_application_event_at(frontend->application, i, &event)) return frontend_fail(error, QA_ERROR_ARGUMENT, "particle event queue changed");
        if (event.kind != QA_BUILTIN_PARTICLES || event.family != QA_GAME_Q1 || event.count <= 0) continue;
        frontend_particle_owner *owner;
        if (!particle_owner(frontend, event.provider, QA_GAME_Q1, &owner, error)) return false;
        q1_particles(owner, &event);
    }
    for (size_t i = 0; i < qa_application_q2_map_event_count(frontend->application); ++i) {
        qa_application_q2_map_event source;
        if (!qa_application_q2_map_event_at(frontend->application, i, &source)) return frontend_fail(error, QA_ERROR_ARGUMENT, "steam event queue changed");
        if (source.event.kind != QA_Q2_MAP_STEAM && source.event.kind != QA_Q2_MAP_FORCE_WALL) continue;
        frontend_particle_owner *owner;
        if (!particle_owner(frontend, source.provider, QA_GAME_Q2, &owner, error)) return false;
        if (source.event.kind == QA_Q2_MAP_FORCE_WALL) {
            if (!force_wall(owner, &source.event, source.time_ns, error)) return false;
            continue;
        }
        if (source.event.slot == -1) { (void)steam_particles(owner, &source.event, source.time_ns); continue; }
        frontend_particle_state *state = frontend->particles;
        if (state->steam_count == FRONTEND_STEAM_CAPACITY) continue;
        double ns = trunc((double)source.event.duration * 1e6);
        if (ns < 0 || ns >= (double)UINT64_MAX || (uint64_t)ns > UINT64_MAX - source.time_ns)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "steam duration exceeds presentation clock");
        frontend_steam *steam = &state->steam[state->steam_count++];
        *steam = (frontend_steam){.owner = owner, .event = source.event,
            .end_ns = source.time_ns + (uint64_t)ns, .next_ns = source.time_ns};
        steam->event.arguments = NULL; steam->event.argument_count = 0;
    }
    if (!frontend->particles) return true;
    frontend_particle_state *state = frontend->particles;
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    size_t retained = 0;
    for (size_t i = 0; i < state->steam_count; ++i) {
        frontend_steam steam = state->steam[i];
        if (steam.end_ns < now) continue;
        if (steam.next_ns <= now && steam_particles(steam.owner, &steam.event, now))
            steam.next_ns = steam.next_ns > UINT64_MAX - UINT64_C(100000000) ? UINT64_MAX : steam.next_ns + UINT64_C(100000000);
        state->steam[retained++] = steam;
    }
    state->steam_count = retained; return true;
}
bool frontend_particle_draw(qa_frontend *frontend, const qa_scene_view *view, qa_error *error)
{
    if (!frontend->particles) return true;
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    double seconds = (double)now / 1e9;
    for (frontend_particle_owner *owner = frontend->particles->owners; owner; owner = owner->next) {
        qa_bytes palette;
        qa_scene_family family = owner->family == QA_GAME_Q1 ? QA_SCENE_Q1 : QA_SCENE_Q2;
        if (!qa_scene_resources_palette(owner->images, family, &palette, error)) return false;
        for (size_t i = owner->count; i > 0; --i) {
            qa_vec3 origin; float alpha = 1; uint32_t index;
            if (owner->q1) {
                const qa_scene_q1_particle_state *particle = &owner->q1[i - 1];
                if (particle->die < seconds) continue;
                origin = particle->origin; index = particle->color & 255;
            } else {
                const qa_scene_q2_particle_state *particle = &owner->q2[i - 1];
                if (!qa_scene_q2_particle_sample(particle, (int64_t)(now / UINT64_C(1000000)), &origin, &alpha)) continue;
                index = particle->color & 255;
            }
            qa_scene_vec4 color = {palette.data[index * 3] / 255.0f, palette.data[index * 3 + 1] / 255.0f,
                palette.data[index * 3 + 2] / 255.0f, alpha};
            if (!qa_scene_indexed_particle(&frontend->frame, view, family, origin, 1, color, owner->particle_image, error)) return false;
        }
    }
    return true;
}
bool frontend_particle_advance(qa_frontend *frontend, qa_error *error)
{
    if (!frontend->particles) return true;
    frontend_particle_state *state = frontend->particles;
    for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next)
        if (owner->q1 && (!qa_application_provider_gravity(frontend->application, owner->provider, &owner->gravity) ||
                !isfinite(owner->gravity)))
            return frontend_fail(error, QA_ERROR_NOT_FOUND, "particle provider has no authoritative gravity");
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    double seconds = (double)now / 1e9;
    double elapsed = now >= state->sample_ns ? (double)(now - state->sample_ns) / 1e9 : 0;
    state->sample_ns = now;
    for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next) {
        size_t retained = 0;
        for (size_t i = 0; i < owner->count; ++i) {
            if (owner->q1) {
                qa_scene_q1_particle_state particle = owner->q1[i];
                if (particle.die < seconds) continue;
                qa_scene_q1_particle_advance(&particle, elapsed, owner->gravity);
                owner->q1[retained++] = particle;
            } else {
                qa_vec3 origin; float alpha;
                if (!qa_scene_q2_particle_sample(&owner->q2[i], (int64_t)(now / UINT64_C(1000000)), &origin, &alpha) ||
                    owner->q2[i].alpha_velocity == -10000) continue;
                owner->q2[retained++] = owner->q2[i];
            }
        }
        owner->count = retained;
    }
    return true;
}
