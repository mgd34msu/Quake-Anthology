#include "internal.h"
#include "qa/scene_effects.h"
#include "save_private.h"
#include "visual_access.h"
#include "qa/game_q2_feedback.h"

enum { FRONTEND_PARTICLE_CAPACITY = 4096, FRONTEND_STEAM_CAPACITY = 32,
       FRONTEND_Q2_IMPACT_CAPACITY = 32 };
typedef struct frontend_q2_impact {
    qa_vec3 origin;
    int64_t start_milliseconds;
    uint8_t kind; /* 0 free, 1 source ex_misc smoke, 2 source ex_flash. */
} frontend_q2_impact;
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
    frontend_q2_impact impacts[FRONTEND_Q2_IMPACT_CAPACITY];
    qa_scene_model *impact_models[2]; /* Borrowed from this frontend's actual appearance owner. */
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
static bool impact_model(qa_frontend *, frontend_particle_owner *, uint8_t,
    bool acquire, qa_scene_model **, qa_error *);
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
void frontend_particle_reset_round(qa_frontend *frontend)
{
    frontend_particle_state *state = frontend->particles;
    if (!state) return;
    for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next) {
        owner->count = 0;
        memset(owner->impacts, 0, sizeof(owner->impacts));
    }
    state->steam_count = 0;
    memset(state->steam, 0, sizeof(state->steam));
    state->sample_ns = qa_session_elapsed(qa_application_session(frontend->application));
}
static bool particle_signature(qa_source_save_io *io)
{
    uint8_t magic[4] = {'Q', 'A', 'P', 'T'}; uint32_t version = 2;
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QAPT", 4) &&
        qa_source_save_u32(io, &version) && version == 2;
}
static bool particle_fields(qa_source_save_io *io, frontend_particle_owner *owner)
{
    if (!frontend_save_random(io, &owner->random) || !qa_source_save_f32(io, &owner->gravity) ||
        !isfinite(owner->gravity) || !qa_source_save_count(io, &owner->count, FRONTEND_PARTICLE_CAPACITY)) return false;
    for (size_t i = 0; i < owner->count; ++i) {
        if (owner->q1) {
            qa_scene_q1_particle_state p = owner->q1[i]; uint32_t kind = p.kind;
            if (!qa_source_save_vec3(io, &p.origin) || !qa_source_save_vec3(io, &p.velocity) ||
                !qa_source_save_f32(io, &p.ramp) || !qa_source_save_f64(io, &p.die) ||
                !qa_source_save_u32(io, &p.color) || !qa_source_save_u32(io, &kind) || kind > QA_Q1_PARTICLE_SLOW_GRAVITY ||
                !qa_vec_finite(p.origin) || !qa_vec_finite(p.velocity) || !isfinite(p.ramp) || !isfinite(p.die)) return false;
            if (io->direction == QA_SOURCE_SAVE_READ) { p.kind = (qa_scene_q1_particle_kind)kind; owner->q1[i] = p; }
        } else {
            qa_scene_q2_particle_state p = owner->q2[i];
            if (!qa_source_save_i64(io, &p.spawn_milliseconds) || !qa_source_save_vec3(io, &p.origin) ||
                !qa_source_save_vec3(io, &p.velocity) || !qa_source_save_vec3(io, &p.acceleration) ||
                !qa_source_save_u32(io, &p.color) || !qa_source_save_f32(io, &p.alpha) || !qa_source_save_f32(io, &p.alpha_velocity) ||
                !qa_vec_finite(p.origin) || !qa_vec_finite(p.velocity) || !qa_vec_finite(p.acceleration) ||
                !isfinite(p.alpha) || !isfinite(p.alpha_velocity)) return false;
            if (io->direction == QA_SOURCE_SAVE_READ) owner->q2[i] = p;
        }
    }
    for (size_t i = 0; owner->q2 && i < FRONTEND_Q2_IMPACT_CAPACITY; ++i) {
        frontend_q2_impact *impact = &owner->impacts[i];
        if (!qa_source_save_u8(io, &impact->kind) || impact->kind > 2 ||
            !qa_source_save_i64(io, &impact->start_milliseconds) ||
            !qa_source_save_vec3(io, &impact->origin) || !qa_vec_finite(impact->origin)) return false;
    }
    return true;
}
bool frontend_particle_checkpoint(qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io = {0};
    if (!out || !qa_source_save_writer(&io, qa_application_session(frontend->application), error)) return false;
    bool present = frontend->particles != NULL;
    bool ok = particle_signature(&io) && qa_source_save_bool(&io, &present);
    if (present && ok) {
        frontend_particle_state *state = frontend->particles;
        uint64_t sample = state->sample_ns; size_t count = 0;
        for (frontend_particle_owner *owner = state->owners; owner; owner = owner->next) ++count;
        ok = qa_source_save_u64(&io, &sample) && qa_source_save_count(&io, &count, SIZE_MAX);
        for (frontend_particle_owner *owner = state->owners; ok && owner; owner = owner->next) {
            frontend_particle_owner copy = *owner; uint32_t family = owner->family;
            ok = frontend_save_provider(&io, frontend->application, &copy.provider) &&
                qa_source_save_u32(&io, &family) && particle_fields(&io, &copy);
        }
        count = state->steam_count; ok = ok && qa_source_save_count(&io, &count, FRONTEND_STEAM_CAPACITY);
        for (size_t i = 0; ok && i < count; ++i) {
            frontend_steam copy = state->steam[i]; qa_actor_owner owner = copy.owner->provider;
            ok = frontend_save_provider(&io, frontend->application, &owner) &&
                frontend_save_q2_event(&io, &copy.event) && qa_source_save_u64(&io, &copy.end_ns) && qa_source_save_u64(&io, &copy.next_ns);
        }
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) frontend_fail(error, QA_ERROR_FORMAT, "invalid retained particle continuation");
    qa_source_save_dispose(&io); return ok;
}
bool frontend_particle_restore(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (frontend->particles) return frontend_fail(error, QA_ERROR_ARGUMENT, "particle restore requires an empty detached candidate");
    qa_source_save_io io = {0}; bool present = false; size_t count = 0;
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        particle_signature(&io) && qa_source_save_bool(&io, &present);
    if (ok && present) {
        frontend->particles = calloc(1, sizeof(*frontend->particles));
        if (!frontend->particles) ok = frontend_fail(error, QA_ERROR_MEMORY, "restoring particle owner");
        else ok = qa_source_save_u64(&io, &frontend->particles->sample_ns) && qa_source_save_count(&io, &count, bytes.size / 8);
        for (size_t i = 0; ok && i < count; ++i) {
            qa_actor_owner provider = 0; uint32_t family = 0;
            ok = frontend_save_provider(&io, frontend->application, &provider) && provider && qa_source_save_u32(&io, &family) &&
                (family == QA_GAME_Q1 || family == QA_GAME_Q2);
            for (frontend_particle_owner *p = frontend->particles->owners; ok && p; p = p->next)
                if (p->provider == provider && p->family == family) ok = false;
            frontend_particle_owner *owner = NULL;
            if (ok) ok = particle_owner(frontend, provider, (qa_game_family)family, &owner, error) && particle_fields(&io, owner);
            for (size_t j = 0; ok && owner && owner->q2 && j < FRONTEND_Q2_IMPACT_CAPACITY; ++j) {
                qa_scene_model *model;
                if (owner->impacts[j].kind)
                    ok = impact_model(frontend, owner, owner->impacts[j].kind, false, &model, error);
            }
        }
        if (ok) {
            frontend_particle_owner *ordered = NULL, *owner = frontend->particles->owners;
            while (owner) { frontend_particle_owner *next = owner->next; owner->next = ordered; ordered = owner; owner = next; }
            frontend->particles->owners = ordered;
            ok = qa_source_save_count(&io, &frontend->particles->steam_count, FRONTEND_STEAM_CAPACITY);
        }
        for (size_t i = 0; ok && i < frontend->particles->steam_count; ++i) {
            frontend_steam *steam = &frontend->particles->steam[i]; qa_actor_owner provider = 0;
            ok = frontend_save_provider(&io, frontend->application, &provider);
            for (frontend_particle_owner *owner = frontend->particles->owners; owner; owner = owner->next)
                if (owner->provider == provider && owner->family == QA_GAME_Q2) steam->owner = owner;
            ok = ok && steam->owner && frontend_save_q2_event(&io, &steam->event) && steam->event.kind == QA_Q2_MAP_STEAM &&
                qa_source_save_u64(&io, &steam->end_ns) && qa_source_save_u64(&io, &steam->next_ns);
        }
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (!ok) {
        frontend_particle_retire(frontend);
        if (!error || error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "invalid saved particle continuation");
    }
    qa_source_save_dispose(&io); return ok;
}
static uint32_t particle_random(frontend_particle_owner *owner)
{ return qa_builtin_random_integer(&owner->random); }
static float particle_unit(frontend_particle_owner *owner)
{ return qa_builtin_random_unit(&owner->random); }
static float particle_signed(frontend_particle_owner *owner)
{ return particle_unit(owner) * 2 - 1; }
static const char *impact_path(uint8_t kind)
{ return kind == 1 ? "models/objects/smoke/tris.md2" : "models/objects/flash/tris.md2"; }
static bool impact_model(qa_frontend *frontend, frontend_particle_owner *owner,
    uint8_t kind, bool acquire, qa_scene_model **out, qa_error *error)
{
    if (kind < 1 || kind > 2)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Invalid Q2 impact model recipe");
    if (owner->impact_models[kind - 1]) { *out = owner->impact_models[kind - 1]; return true; }
    if (acquire) {
        frontend_visual_model_view model;
        if (!frontend_visual_model_acquire(frontend, owner->provider, QA_GAME_Q2,
                impact_path(kind), NULL, &model, error)) return false;
        owner->impact_models[kind - 1] = model.scene; *out = model.scene; return true;
    }
    size_t count = frontend_visual_owner_count(frontend);
    for (size_t i = 0; i < count; ++i) {
        frontend_visual_owner_view media;
        if (!frontend_visual_owner_read(frontend, i, &media) ||
            media.owner != owner->provider || media.family != QA_SCENE_Q2) continue;
        size_t models = frontend_visual_model_count(frontend, i);
        for (size_t j = 0; j < models; ++j) {
            frontend_visual_model_view model;
            if (frontend_visual_model_read(frontend, i, j, &model) &&
                !strcmp(model.path, impact_path(kind))) {
                owner->impact_models[kind - 1] = model.scene; *out = model.scene; return true;
            }
        }
    }
    return frontend_fail(error, QA_ERROR_NOT_FOUND, "Q2 impact lost its actual retained model owner");
}
static frontend_q2_impact *impact_allocate(frontend_particle_owner *owner, int64_t now)
{
    size_t oldest = 0; int64_t start = now;
    for (size_t i = 0; i < FRONTEND_Q2_IMPACT_CAPACITY; ++i)
        if (!owner->impacts[i].kind) return &owner->impacts[i];
    for (size_t i = 0; i < FRONTEND_Q2_IMPACT_CAPACITY; ++i)
        if (owner->impacts[i].start_milliseconds < start) {
            start = owner->impacts[i].start_milliseconds; oldest = i;
        }
    return &owner->impacts[oldest];
}
static bool q2_damage_particles(qa_frontend *frontend, frontend_particle_owner *owner,
    const qa_builtin_event *event, qa_error *error)
{
    uint32_t color; int count; bool fixed = false;
    const char *sound = NULL;
    switch (event->code) {
    case QA_Q2_DAMAGE_BLOOD: color = 0xe8; count = 60; break;
    case QA_Q2_DAMAGE_SPARKS: case QA_Q2_DAMAGE_BULLET_SPARKS: color = 0xe0; count = 6; break;
    case QA_Q2_DAMAGE_SCREEN_SPARKS: color = 0xd0; count = 40; sound = "weapons/lashit.wav"; break;
    case QA_Q2_DAMAGE_SHIELD_SPARKS: color = 0xb0; count = 40; sound = "weapons/lashit.wav"; break;
    case QA_Q2_DAMAGE_GREEN_BLOOD: color = 0xdf; count = 30; fixed = true; break;
    case QA_Q2_DAMAGE_MORE_BLOOD: color = 0xe8; count = 250; break;
    case QA_Q2_DAMAGE_ELECTRIC_SPARKS: color = 0x75; count = 40; sound = "weapons/lashit.wav"; break;
    default: return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Unknown Q2 damage particle recipe");
    }
    for (int i = 0; i < count && owner->count < FRONTEND_PARTICLE_CAPACITY; ++i) {
        uint32_t selected = color + (fixed ? 0 : particle_random(owner) & 7);
        float distance = (float)(particle_random(owner) & (fixed ? 7 : 31));
        qa_vec3 origin, velocity;
        origin.x = event->origin.x + (float)(particle_random(owner) & 7) - 4 + distance * event->direction.x;
        velocity.x = particle_signed(owner) * 20;
        origin.y = event->origin.y + (float)(particle_random(owner) & 7) - 4 + distance * event->direction.y;
        velocity.y = particle_signed(owner) * 20;
        origin.z = event->origin.z + (float)(particle_random(owner) & 7) - 4 + distance * event->direction.z;
        velocity.z = particle_signed(owner) * 20;
        owner->q2[owner->count++] = (qa_scene_q2_particle_state){
            .spawn_milliseconds = (int64_t)(event->time_ns / UINT64_C(1000000)),
            .origin = origin, .velocity = velocity, .acceleration = {0, 0, -40},
            .color = selected, .alpha = 1, .alpha_velocity = -1 / (.5f + particle_unit(owner) * .3f)};
    }
    if (event->code == QA_Q2_DAMAGE_BULLET_SPARKS) {
        int64_t now = (int64_t)(event->time_ns / UINT64_C(1000000));
        for (uint8_t kind = 1; kind <= 2; ++kind) {
            qa_scene_model *model;
            if (!impact_model(frontend, owner, kind, true, &model, error)) return false;
            *impact_allocate(owner, now) = (frontend_q2_impact){.kind = kind,
                .origin = event->origin, .start_milliseconds = now - 100};
        }
        uint32_t choice = particle_random(owner) & 15;
        if (choice >= 1 && choice <= 3) {
            static const char *const ricochets[] = {"world/ric1.wav", "world/ric2.wav", "world/ric3.wav"};
            sound = ricochets[choice - 1];
        }
    }
    if (sound) {
        qa_string_id resource;
        if (!qa_strings_intern_cstr(qa_session_strings(qa_application_session(frontend->application)),
                sound, &resource, error)) return false;
        qa_builtin_event audio = {.kind = QA_BUILTIN_SOUND, .family = QA_GAME_Q2,
            .provider = event->provider, .resource = resource, .origin = event->origin,
            .volume = 1, .attenuation = 1, .time_ns = event->time_ns};
        if (!frontend_event_sound(frontend, &audio, error)) return false;
    }
    return true;
}
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
        if (event.kind != QA_BUILTIN_PARTICLES ||
            (event.family != QA_GAME_Q1 && event.family != QA_GAME_Q2) ||
            (event.family == QA_GAME_Q1 && event.count <= 0)) continue;
        frontend_particle_owner *owner;
        if (!particle_owner(frontend, event.provider, event.family, &owner, error)) return false;
        if (event.family == QA_GAME_Q1) q1_particles(owner, &event);
        else if (!q2_damage_particles(frontend, owner, &event, error)) return false;
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
        for (size_t i = 0; owner->q2 && i < FRONTEND_Q2_IMPACT_CAPACITY; ++i) {
            const frontend_q2_impact *impact = &owner->impacts[i];
            if (!impact->kind) continue;
            double fraction = ((double)(now / UINT64_C(1000000)) - impact->start_milliseconds) / 100;
            double source_frame = floor(fraction);
            if (source_frame >= (impact->kind == 1 ? 3 : 1)) continue;
            uint32_t current = source_frame < 0 ? 0 : (uint32_t)source_frame;
            qa_scene_model *model;
            if (!impact_model(frontend, owner, impact->kind, false, &model, error)) return false;
            qa_model_transform placement; qa_model_transform_identity(&placement);
            placement.origin[0] = impact->origin.x;
            placement.origin[1] = impact->origin.y;
            placement.origin[2] = impact->origin.z;
            qa_scene_model_input input = {.view = *view, .transform = placement,
                .previous_origin = impact->origin, .family = QA_SCENE_Q2,
                .color = {1, 1, 1, impact->kind == 1 ? (float)(1 - fraction / 3) : 1},
                .frame = current + 1, .old_frame = current,
                .flags = impact->kind == 1 ? 32u : 8u, .entity = (uint32_t)i,
                .identity_light = 1, .seconds = seconds,
                .ambient = {1, 1, 1}, .source_path = impact_path(impact->kind)};
            if (frontend->scene_world)
                qa_scene_world_sample_light(frontend->scene_world, impact->origin,
                    &input.ambient, &input.directed, &input.light_direction);
            if (!qa_scene_model_submit(model, &input, &frontend->frame, error)) return false;
        }
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
        for (size_t i = 0; owner->q2 && i < FRONTEND_Q2_IMPACT_CAPACITY; ++i) {
            frontend_q2_impact *impact = &owner->impacts[i];
            double source_frame = floor(((double)(now / UINT64_C(1000000)) - impact->start_milliseconds) / 100);
            if (impact->kind && source_frame >= (impact->kind == 1 ? 3 : 1))
                *impact = (frontend_q2_impact){0};
        }
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
