#include "internal.h"
#include <stdio.h>

enum { FRONTEND_STYLES = 256 };
typedef struct frontend_event_resources {
    struct frontend_event_resources *next;
    qa_actor_owner owner;
    qa_audio_family family;
    qa_vfs *files;
    qa_audio_bank *sounds;
    qa_scene_resources *images;
} frontend_event_resources;
typedef struct frontend_retained_sound {
    struct frontend_retained_sound *next;
    qa_actor_id actor;
    qa_audio_loop loop;
    uint64_t static_key;
    qa_audio_mixer *static_mixers[QA_INPUT_LOCAL_SEATS];
} frontend_retained_sound;
typedef struct frontend_retained_light {
    struct frontend_retained_light *next;
    qa_actor_owner owner;
    qa_q2_map_event event;
    uint64_t identity, revision;
} frontend_retained_light;
typedef struct frontend_event_view {
    char *q1_patterns[FRONTEND_STYLES], *q2_patterns[FRONTEND_STYLES];
    float q1_styles[FRONTEND_STYLES];
    qa_vec3 q2_styles[FRONTEND_STYLES];
    qa_q2_fog fog_start, fog_target;
    uint64_t fog_time;
    double fog_duration;
    bool fog_received, sky_received;
    const qa_scene_image *sky[6];
    qa_vec3 sky_axis;
    float sky_rotation;
    bool sky_auto;
} frontend_event_view;
struct frontend_event_state {
    frontend_event_resources *resources;
    frontend_retained_sound *sounds;
    frontend_retained_light *lights;
    frontend_event_view views[QA_INPUT_LOCAL_SEATS];
};
static qa_audio_family audio_family(qa_game_family family)
{
    return family == QA_GAME_Q3 ? QA_AUDIO_Q3 : family == QA_GAME_Q2 ? QA_AUDIO_Q2 : QA_AUDIO_Q1;
}
static bool state_read(qa_frontend *frontend, frontend_event_state **out, qa_error *error)
{
    if (!frontend->events) {
        frontend->events = calloc(1, sizeof(*frontend->events));
        if (!frontend->events) return frontend_fail(error, QA_ERROR_MEMORY, "allocating frontend presentation state");
    }
    *out = frontend->events; return true;
}
static bool resources_read(qa_frontend *frontend, qa_actor_owner provider, qa_audio_family family,
    frontend_event_resources **out, qa_error *error)
{
    frontend_event_state *state;
    if (!state_read(frontend, &state, error)) return false;
    for (frontend_event_resources *entry = state->resources; entry; entry = entry->next)
        if (entry->owner == provider && entry->family == family) { *out = entry; return true; }
    qa_command_context context = {.owner = provider, .origin = QA_COMMAND_SERVER,
        .dialect = family == QA_AUDIO_Q3 ? QA_CONSOLE_Q3 : family == QA_AUDIO_Q2 ? QA_CONSOLE_Q2 : QA_CONSOLE_Q1};
    qa_command_context captured;
    if (!qa_application_capture_command_context(frontend->application, &context, &captured, error)) return false;
    qa_vfs *files = qa_application_context_files(frontend->application, &captured, NULL);
    if (!files) return frontend_fail(error, QA_ERROR_NOT_FOUND, "presentation event owner has no active content view");
    frontend_event_resources *entry = calloc(1, sizeof(*entry));
    if (!entry) return frontend_fail(error, QA_ERROR_MEMORY, "allocating presentation event resources");
    entry->owner = provider; entry->family = family;
    entry->files = qa_vfs_clone(files, error);
    entry->images = entry->files ? qa_scene_resources_create(entry->files, error) : NULL;
    bool ok = entry->files && entry->images &&
        (!frontend->audio || qa_audio_bank_create(entry->files, &entry->sounds, error));
    if (!ok) {
        qa_audio_bank_destroy(entry->sounds); qa_scene_resources_destroy(entry->images);
        qa_vfs_destroy(entry->files); free(entry); return false;
    }
    entry->next = state->resources; state->resources = entry; *out = entry; return true;
}
void frontend_event_retire(qa_frontend *frontend)
{
    frontend_event_state *state = frontend->events;
    if (!state) return;
    while (state->sounds) {
        frontend_retained_sound *entry = state->sounds; state->sounds = entry->next;
        qa_audio_asset_release(entry->loop.sound.asset); free(entry);
    }
    while (state->lights) {
        frontend_retained_light *entry = state->lights; state->lights = entry->next; free(entry);
    }
    for (unsigned i = 0; i < QA_INPUT_LOCAL_SEATS; ++i) {
        for (unsigned face = 0; face < 6; ++face) qa_scene_image_release(state->views[i].sky[face]);
        for (unsigned j = 0; j < FRONTEND_STYLES; ++j) {
            free(state->views[i].q1_patterns[j]); free(state->views[i].q2_patterns[j]);
        }
    }
    while (state->resources) {
        frontend_event_resources *entry = state->resources; state->resources = entry->next;
        qa_audio_bank_destroy(entry->sounds); qa_scene_resources_destroy(entry->images);
        qa_vfs_destroy(entry->files); free(entry);
    }
    free(state); frontend->events = NULL;
}
static bool seat_receives(qa_frontend *frontend, unsigned seat, qa_actor_id recipient)
{
    qa_actor_id actor;
    return !recipient.registry || (qa_application_player_actor(frontend->application, seat, &actor) &&
        qa_actor_id_equal(actor, recipient));
}
static bool pattern_set(char **out, const char *pattern, qa_error *error)
{
    if (!pattern) return frontend_fail(error, QA_ERROR_ARGUMENT, "lightstyle has no session pattern");
    if (*out && !strcmp(*out, pattern)) return true;
    char *copy = malloc(strlen(pattern) + 1);
    if (!copy) return frontend_fail(error, QA_ERROR_MEMORY, "retaining source lightstyle");
    strcpy(copy, pattern); free(*out); *out = copy; return true;
}
static float fog_fraction(float value)
{
    double byte = fmod(trunc((double)(value * 255)), 256);
    return (float)(byte < 0 ? byte + 256 : byte) / 255;
}
static qa_vec3 fog_color(qa_vec3 color)
{
    return qa_v3(fog_fraction(color.x), fog_fraction(color.y), fog_fraction(color.z));
}
static qa_q2_fog fog_source(qa_q2_fog fog)
{
    fog.color = fog_color(fog.color); fog.start_color = fog_color(fog.start_color);
    fog.end_color = fog_color(fog.end_color); fog.sky_factor = fog_fraction(fog.sky_factor);
    double start = fmod(trunc((double)fog.start_distance), 4294967296.0);
    double end = fmod(trunc((double)fog.end_distance), 4294967296.0);
    if (start < 0) start += 4294967296.0;
    if (end < 0) end += 4294967296.0;
    fog.start_distance = (float)(start >= 2147483648.0 ? start - 4294967296.0 : start);
    fog.end_distance = (float)(end >= 2147483648.0 ? end - 4294967296.0 : end);
    return fog;
}
bool frontend_map_events(qa_frontend *frontend, qa_error *error)
{
    if (frontend->options.dedicated) return true;
    frontend_event_state *state;
    if (!state_read(frontend, &state, error)) return false;
    qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    for (size_t i = 0; i < qa_application_event_count(frontend->application); ++i) {
        qa_builtin_event event;
        if (!qa_application_event_at(frontend->application, i, &event)) return frontend_fail(error, QA_ERROR_ARGUMENT, "builtin queue changed during map presentation");
        if (event.kind != QA_BUILTIN_LIGHT || event.family != QA_GAME_Q1 || event.code < 0 || event.code >= FRONTEND_STYLES) continue;
        for (unsigned seat = 0; seat < frontend->options.seats; ++seat)
            if (!pattern_set(&state->views[seat].q1_patterns[event.code], qa_strings_cstr(strings, event.resource), error)) return false;
    }
    for (size_t i = 0; i < qa_application_q2_map_event_count(frontend->application); ++i) {
        qa_application_q2_map_event source;
        if (!qa_application_q2_map_event_at(frontend->application, i, &source)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 queue changed during map presentation");
        const qa_q2_map_event *event = &source.event;
        if (event->kind == QA_Q2_MAP_DYNAMIC_LIGHT) {
            frontend_retained_light *light = state->lights;
            while (light && (light->owner != source.provider || !qa_actor_id_equal(light->event.actor, event->actor))) light = light->next;
            if (!light) {
                light = calloc(1, sizeof(*light));
                if (!light) return frontend_fail(error, QA_ERROR_MEMORY, "retaining authored dynamic light");
                light->owner = source.provider; light->identity = qa_scene_identity();
                light->next = state->lights; state->lights = light;
            }
            light->event = *event; light->event.arguments = NULL; light->event.argument_count = 0;
            if (light->revision == UINT64_MAX) return frontend_fail(error, QA_ERROR_MEMORY, "authored light revision exhausted");
            ++light->revision;
            continue;
        }
        const qa_scene_image *sky[6] = {0};
        if (event->kind == QA_Q2_MAP_SKY) {
            const char *name = qa_strings_cstr(strings, event->resource);
            if (!name) return frontend_fail(error, QA_ERROR_ARGUMENT, "sky event has no session name");
            frontend_event_resources *resources;
            if (!resources_read(frontend, source.provider, QA_AUDIO_Q2, &resources, error)) return false;
            size_t length = strlen(name);
            if (length > SIZE_MAX - 7) return frontend_fail(error, QA_ERROR_MEMORY, "sky path exceeds native storage");
            char *path = malloc(length + 7);
            if (!path) return frontend_fail(error, QA_ERROR_MEMORY, "allocating authored sky path");
            static const char *const suffixes[6] = {"rt", "lf", "bk", "ft", "up", "dn"};
            qa_scene_image_options options = {.family = QA_SCENE_Q2, .wrap = QA_SCENE_CLAMP,
                .filter = QA_SCENE_LINEAR, .usage = QA_IMAGE_USAGE_SKY, .transparent_index = -1};
            bool ok = true;
            for (unsigned face = 0; face < 6 && ok; ++face) {
                qa_scene_image *image = NULL;
                snprintf(path, length + 7, "env/%s%s", name, suffixes[face]);
                qa_error observed = {0};
                ok = qa_scene_image_load(resources->images, path, &options, &image, &observed);
                if (!ok && observed.code == QA_ERROR_NOT_FOUND) {
                    sky[face] = qa_scene_missing(resources->images);
                    qa_scene_image_retain(sky[face]); ok = true;
                } else {
                    if (!ok && error) *error = observed;
                    sky[face] = image;
                }
            }
            free(path);
            if (!ok) {
                for (unsigned face = 0; face < 6; ++face) qa_scene_image_release(sky[face]);
                return false;
            }
        }
        for (unsigned seat = 0; seat < frontend->options.seats; ++seat) {
            if (!seat_receives(frontend, seat, event->recipient)) continue;
            frontend_event_view *view = &state->views[seat];
            switch (event->kind) {
            case QA_Q2_MAP_LIGHTSTYLE:
                if (event->style >= 0 && event->style < FRONTEND_STYLES &&
                    !pattern_set(&view->q2_patterns[event->style], qa_strings_cstr(strings, event->text), error)) return false;
                break;
            case QA_Q2_MAP_FOG:
                if (event->duration != 0) { view->fog_start = view->fog_target; view->fog_time = source.time_ns; }
                view->fog_target = fog_source(event->fog); view->fog_duration = event->duration;
                view->fog_received = true; break;
            case QA_Q2_MAP_SKY:
                for (unsigned face = 0; face < 6; ++face) {
                    qa_scene_image_release(view->sky[face]);
                    view->sky[face] = sky[face]; qa_scene_image_retain(sky[face]);
                }
                view->sky_received = true;
                view->sky_rotation = event->value; view->sky_axis = event->direction;
                view->sky_auto = event->value != 0 && (event->flags & 1) != 0; break;
            default: break;
            }
        }
        for (unsigned face = 0; face < 6; ++face) qa_scene_image_release(sky[face]);
    }
    return true;
}
bool frontend_event_world(qa_frontend *frontend, unsigned seat, qa_scene_world_input *world, qa_error *error)
{
    qa_actor_id player; qa_q1_fog_state q1;
    bool q1_fog = qa_application_player_actor(frontend->application, seat, &player) &&
        qa_application_q1_fog_read(frontend->application, player, &q1);
    if (q1_fog) {
        qa_vec3 color = q1.color;
        color.x = roundf(fminf(1, fmaxf(0, color.x)) * 255) / 255;
        color.y = roundf(fminf(1, fmaxf(0, color.y)) * 255) / 255;
        color.z = roundf(fminf(1, fmaxf(0, color.z)) * 255) / 255;
        world->fog = (qa_scene_fog){.kind = QA_FOG_EXP2, .effect = QA_FOG_COLOR,
            .density = q1.density, .color = color, .sky_factor = .5f, .far_depth = 1};
    }
    frontend_event_state *state = frontend->events;
    if (!state) return true;
    frontend_event_view *view = &state->views[seat];
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    uint64_t sample = now / UINT64_C(100000000);
    for (unsigned i = 0; i < FRONTEND_STYLES; ++i) {
        const char *q1 = view->q1_patterns[i], *q2 = view->q2_patterns[i];
        size_t n1 = q1 ? strlen(q1) : 0, n2 = q2 ? strlen(q2) : 0;
        view->q1_styles[i] = n1 ? ((unsigned char)q1[sample % n1] - 97) * 22.0f : 256;
        float scale = n2 ? ((unsigned char)q2[sample % n2] - 97) / 12.0f : 1;
        view->q2_styles[i] = qa_v3(scale, scale, scale);
    }
    world->q1_styles = view->q1_styles; world->q2_styles = view->q2_styles; world->style_count = FRONTEND_STYLES;
    if (view->sky_received) {
        memcpy(world->sky_images, view->sky, sizeof(view->sky)); world->override_sky = true;
        world->sky_rotation = view->sky_rotation; world->sky_axis = view->sky_axis; world->sky_auto_rotate = view->sky_auto;
    }
    if (view->fog_received && !q1_fog) {
        double elapsed = now >= view->fog_time ? (double)(now - view->fog_time) / 1e9 : 0;
        float t = view->fog_duration <= 0 ? 1 : (float)fmin(1, elapsed / view->fog_duration);
        qa_q2_fog a = view->fog_start, b = view->fog_target;
#define MIX(field) (a.field + (b.field - a.field) * t)
#define COLOR(field) qa_vec_add(a.field, qa_vec_scale(qa_vec_sub(b.field, a.field), t))
        world->fog = (qa_scene_fog){.kind = QA_FOG_Q2, .effect = QA_FOG_OVERLAY,
            .color = COLOR(color), .density = MIX(density), .sky_factor = MIX(sky_factor),
            .height_color = COLOR(start_color), .height_end_color = COLOR(end_color),
            .height_start = MIX(start_distance), .height_end = MIX(end_distance),
            .height_density = MIX(height_density), .height_falloff = MIX(falloff), .far_depth = 1};
#undef MIX
#undef COLOR
    }
    size_t count = 0;
    for (frontend_retained_light *light = state->lights; light; light = light->next)
        if (light->event.visible && seat_receives(frontend, seat, light->event.recipient) &&
            qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), light->event.actor)) ++count;
    if (!count) return true;
    if (world->light_count > SIZE_MAX - count || count + world->light_count > SIZE_MAX / sizeof(qa_scene_light))
        return frontend_fail(error, QA_ERROR_MEMORY, "authored light array exceeds native storage");
    qa_scene_light *lights = qa_arena_alloc(&frontend->frame.storage, (count + world->light_count) * sizeof(*lights), _Alignof(qa_scene_light), error);
    if (!lights) return false;
    if (world->light_count) memcpy(lights, world->lights, world->light_count * sizeof(*lights));
    size_t used = world->light_count;
    for (frontend_retained_light *light = state->lights; light; light = light->next) {
        const qa_q2_map_event *event = &light->event;
        if (!event->visible || !seat_receives(frontend, seat, event->recipient) ||
            !qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), event->actor)) continue;
        float intensity = event->intensity;
        if (event->style >= 0 && event->style < FRONTEND_STYLES) intensity *= view->q2_styles[event->style].x;
        float distance = qa_vec_length(qa_vec_sub(world->view.origin, event->origin));
        if (event->fade_end > event->fade_start && distance > event->fade_start)
            intensity *= fmaxf(0, (event->fade_end - distance) / (event->fade_end - event->fade_start));
        lights[used++] = (qa_scene_light){.origin = event->origin, .color = qa_vec_scale(event->color, intensity),
            .radius = event->radius, .scale = 1, .direction = event->direction,
            .spot = (event->flags & 1) != 0, .cos_half_angle = event->cone_cosine,
            .identity = light->identity, .revision = light->revision, .family = QA_SCENE_Q2,
            .shadow_resolution = event->resolution};
    }
    world->lights = lights; world->light_count = used; return true;
}
bool frontend_event_sound(qa_frontend *frontend, const qa_builtin_event *event, qa_error *error)
{
    if (!frontend->audio || (event->kind != QA_BUILTIN_SOUND && event->kind != QA_BUILTIN_STOP_SOUND)) return true;
    frontend_event_state *state;
    if (!state_read(frontend, &state, error)) return false;
    uint64_t actor = frontend_audio_actor(frontend, event->actor, error);
    if (event->actor.registry && actor == QA_AUDIO_NO_ACTOR) return false;
    qa_audio_family family = audio_family(event->family);
    if (event->kind == QA_BUILTIN_STOP_SOUND) {
        frontend_retained_sound **link = &state->sounds;
        bool loop_stopped = false;
        while (*link) {
            frontend_retained_sound *entry = *link;
            if (!entry->static_key && entry->loop.sound.actor == actor && entry->loop.sound.owner == event->provider &&
                entry->loop.sound.family == family) {
                if (!qa_audio_engine_stop_loop(frontend->audio, actor, event->provider, QA_AUDIO_WORLD, error)) return false;
                *link = entry->next; qa_audio_asset_release(entry->loop.sound.asset); free(entry);
                loop_stopped = true;
            } else link = &entry->next;
        }
        if (!loop_stopped) qa_audio_engine_stop_channel(frontend->audio, actor, event->provider, family, event->channel);
        return true;
    }
    qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    const char *name = qa_strings_cstr(strings, event->resource);
    if (!name || !*name) return true;
    frontend_event_resources *resources;
    if (!resources_read(frontend, event->provider, family, &resources, error)) return false;
    qa_audio_asset *asset = NULL;
    if (!qa_audio_bank_register(resources->sounds, name, family, &asset, error)) return false;
    if (!asset) return true;
    bool live = event->actor.registry && qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), event->actor);
    qa_audio_play sound = {.sample = qa_audio_asset_sample(asset), .asset = asset, .name = name,
        .family = family, .actor = actor, .owner = event->provider, .audience = QA_AUDIO_WORLD,
        .origin_kind = live ? QA_AUDIO_ACTOR : QA_AUDIO_FIXED, .origin_actor = actor,
        .origin = event->origin, .channel = event->channel, .volume = event->volume, .attenuation = event->attenuation};
    if (live) {
        qa_body_state body; qa_error observed = {0};
        if (qa_world_body_read(qa_application_world(frontend->application), event->actor, &body, &observed)) {
            sound.origin = body.origin;
            if (!qa_audio_engine_position(frontend->audio, actor, body.origin, error)) {
                qa_audio_asset_release(asset); return false;
            }
        } else if (observed.code == QA_ERROR_NOT_FOUND) {
            sound.origin_kind = QA_AUDIO_FIXED;
        } else {
            if (error) *error = observed;
            qa_audio_asset_release(asset); return false;
        }
    }
    if (!(event->flags & 1)) {
        bool ok = qa_audio_engine_play(frontend->audio, &sound, (int32_t)((frontend->time_ns / 1000000) & INT32_MAX), error);
        qa_audio_asset_release(asset); return ok;
    }
    bool ambient = family == QA_AUDIO_Q1 && !event->actor.registry;
    if ((!ambient && !live) || (ambient && sound.sample->loop_start == QA_AUDIO_NO_LOOP)) {
        qa_audio_asset_release(asset); return true;
    }
    frontend_retained_sound *entry = state->sounds;
    while (entry && (ambient || entry->static_key || entry->loop.sound.actor != actor ||
        entry->loop.sound.owner != event->provider || entry->loop.sound.family != family)) entry = entry->next;
    if (!entry) {
        entry = calloc(1, sizeof(*entry));
        if (!entry) { qa_audio_asset_release(asset); return frontend_fail(error, QA_ERROR_MEMORY, "retaining builtin looping sound"); }
        entry->next = state->sounds; state->sounds = entry;
    } else qa_audio_asset_release(entry->loop.sound.asset);
    entry->actor = event->actor;
    entry->loop = (qa_audio_loop){.sound = sound, .persistent = true};
    entry->loop.sound.name = NULL;
    if (ambient) entry->static_key = qa_scene_identity();
    return true;
}
bool frontend_event_audio(qa_frontend *frontend, qa_error *error)
{
    frontend_event_state *state = frontend->events;
    if (!state || !frontend->audio) return true;
    frontend_retained_sound **link = &state->sounds;
    while (*link) {
        frontend_retained_sound *entry = *link;
        if (entry->static_key) {
            for (unsigned seat = 0; seat < frontend->options.seats; ++seat) {
                qa_audio_mixer *mixer = qa_audio_engine_seat_mixer(frontend->audio, seat);
                if (!mixer) { entry->static_mixers[seat] = NULL; continue; }
                if (entry->static_mixers[seat] == mixer) continue;
                const qa_audio_play *sound = &entry->loop.sound;
                if (!qa_audio_mixer_static(mixer, entry->static_key, sound->sample, sound->origin,
                    truncf(sound->volume * 255), truncf(sound->attenuation * 64), error)) return false;
                entry->static_mixers[seat] = mixer;
            }
        } else {
            if (!qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), entry->actor)) {
                if (!qa_audio_engine_stop_loop(frontend->audio, entry->loop.sound.actor,
                    entry->loop.sound.owner, QA_AUDIO_WORLD, error)) return false;
                *link = entry->next; qa_audio_asset_release(entry->loop.sound.asset); free(entry); continue;
            }
            qa_body_state body; qa_error observed = {0};
            if (!qa_world_body_read(qa_application_world(frontend->application), entry->actor, &body, &observed)) {
                if (observed.code != QA_ERROR_NOT_FOUND) { if (error) *error = observed; return false; }
            } else {
                entry->loop.velocity = body.velocity; entry->loop.sound.origin = body.origin;
                if (!qa_audio_engine_position(frontend->audio, entry->loop.sound.actor, body.origin, error)) return false;
            }
            entry->loop.frame_number = (int32_t)(frontend->frame_number & INT32_MAX);
            if (!qa_audio_engine_loop(frontend->audio, &entry->loop, error)) return false;
        }
        link = &entry->next;
    }
    return true;
}

const qa_scene_resources *frontend_event_images_at(qa_frontend *frontend, size_t index)
{
    frontend_event_resources *entry = frontend && frontend->events ? frontend->events->resources : NULL;
    while (entry && index) { entry = entry->next; --index; }
    return entry ? entry->images : NULL;
}

bool frontend_event_images(qa_frontend *frontend, qa_actor_owner owner, qa_game_family family,
    qa_scene_resources **out, qa_error *error)
{
    frontend_event_resources *entry;
    if (!resources_read(frontend, owner, audio_family(family), &entry, error)) return false;
    *out = entry->images; return true;
}
