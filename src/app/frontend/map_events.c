#include "internal.h"
#include "save_private.h"
#include <stdio.h>

enum { FRONTEND_STYLES = 256 };
typedef struct frontend_footsteps {
    struct frontend_footsteps *next;
    char material[16];
    qa_audio_asset *assets[16];
    uint32_t count;
} frontend_footsteps;
typedef struct frontend_event_resources {
    struct frontend_event_resources *next;
    qa_actor_owner owner;
    qa_audio_family family;
    qa_vfs *files;
    qa_audio_bank *sounds;
    qa_scene_resources *images;
    frontend_footsteps *footsteps;
} frontend_event_resources;
typedef struct frontend_retained_sound {
    struct frontend_retained_sound *next;
    qa_actor_id actor;
    qa_audio_loop loop;
    uint64_t static_key;
    qa_audio_mixer *static_mixers[QA_INPUT_LOCAL_SEATS];
    uint32_t restored_static_seats;
} frontend_retained_sound;
typedef struct frontend_retained_light {
    struct frontend_retained_light *next;
    qa_actor_owner owner;
    qa_q2_map_event event;
    uint64_t identity, revision;
} frontend_retained_light;
typedef struct frontend_retained_bounds {
    struct frontend_retained_bounds *next;
    qa_actor_owner owner;
    qa_actor_id actor, recipient;
    qa_bounds bounds;
    uint64_t frame, deadline;
    unsigned color;
    bool depth;
} frontend_retained_bounds;
typedef struct frontend_q1_light {
    qa_actor_id actor;
    qa_scene_light light;
    double die;
} frontend_q1_light;
typedef struct frontend_event_view {
    char *q1_patterns[FRONTEND_STYLES], *q2_patterns[FRONTEND_STYLES];
    float q1_styles[FRONTEND_STYLES];
    qa_vec3 q2_styles[FRONTEND_STYLES];
    qa_q2_fog fog_start, fog_target;
    uint64_t fog_time;
    double fog_duration;
    bool fog_received, sky_received;
    const qa_scene_image *sky[6];
    qa_actor_owner sky_owner;
    qa_vec3 sky_axis;
    float sky_rotation;
    bool sky_auto;
} frontend_event_view;
struct frontend_event_state {
    frontend_event_resources *resources;
    frontend_retained_sound *sounds;
    frontend_retained_light *lights;
    frontend_retained_bounds *bounds;
    frontend_q1_light q1_lights[32];
    qa_builtin_random light_random;
    uint32_t step_random[624], step_cursor;
    qa_audio_asset *last_step;
    qa_actor_owner last_step_owner;
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
        qa_builtin_random_seed(&frontend->events->light_random, 1);
        frontend->events->step_random[0] = 1;
        for (uint32_t i = 1; i < 624; ++i) {
            uint32_t prior = frontend->events->step_random[i - 1];
            frontend->events->step_random[i] = 1812433253u * (prior ^ (prior >> 30)) + i;
        }
        frontend->events->step_cursor = 624;
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
    while (state->bounds) {
        frontend_retained_bounds *entry = state->bounds; state->bounds = entry->next; free(entry);
    }
    for (unsigned i = 0; i < QA_INPUT_LOCAL_SEATS; ++i) {
        for (unsigned face = 0; face < 6; ++face) qa_scene_image_release(state->views[i].sky[face]);
        for (unsigned j = 0; j < FRONTEND_STYLES; ++j) {
            free(state->views[i].q1_patterns[j]); free(state->views[i].q2_patterns[j]);
        }
    }
    while (state->resources) {
        frontend_event_resources *entry = state->resources; state->resources = entry->next;
        while (entry->footsteps) {
            frontend_footsteps *steps = entry->footsteps; entry->footsteps = steps->next;
            for (uint32_t i = 0; i < steps->count; ++i) qa_audio_asset_release(steps->assets[i]);
            free(steps);
        }
        qa_audio_bank_destroy(entry->sounds); qa_scene_resources_destroy(entry->images);
        qa_vfs_destroy(entry->files); free(entry);
    }
    qa_audio_asset_release(state->last_step); free(state); frontend->events = NULL;
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
static bool q1_effects(qa_frontend *frontend, frontend_event_state *state, qa_error *error)
{
    double seconds = (double)qa_session_elapsed(qa_application_session(frontend->application)) / 1e9;
    const qa_actor_record *record; uint32_t cursor = 0;
    qa_actor_registry *actors = qa_world_actors(qa_application_world(frontend->application));
    while (qa_actors_next(actors, &cursor, &record)) {
        qa_application_visual_view view; qa_error observed = {0};
        if (!qa_application_visual_read(frontend->application, record->id, &view, &observed)) {
            if (observed.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = observed; return false;
        }
        uint32_t effects = view.q1_effects | (view.family == QA_GAME_Q1 ? (uint32_t)view.effects : 0);
        if (!(effects & 14)) continue;
        frontend_q1_light *entry = NULL;
        for (unsigned i = 0; i < 32; ++i)
            if (qa_actor_id_equal(state->q1_lights[i].actor, record->id)) { entry = &state->q1_lights[i]; break; }
        if (!entry) for (unsigned i = 0; i < 32; ++i)
            if (!state->q1_lights[i].actor.registry || state->q1_lights[i].die < seconds) { entry = &state->q1_lights[i]; break; }
        if (!entry) entry = &state->q1_lights[0];
        entry->actor = record->id;
        qa_vec3 origin = view.body.origin; float radius = 0, minimum = 0;
        if (effects & 2) {
            qa_vec3 axes[3]; frontend_camera_axes(view.body.angles, axes);
            origin.z += 16; origin = qa_vec_add(origin, qa_vec_scale(axes[0], 18));
            radius = 200 + (float)(qa_builtin_random_integer(&state->light_random) & 31); minimum = 32;
            entry->die = seconds + .1;
        }
        if (effects & 4) {
            origin = view.body.origin; origin.z += 16;
            radius = 400 + (float)(qa_builtin_random_integer(&state->light_random) & 31);
            minimum = 0; entry->die = seconds + .001;
        }
        if (effects & 8) {
            origin = view.body.origin; radius = 200 + (float)(qa_builtin_random_integer(&state->light_random) & 31);
            minimum = 0; entry->die = seconds + .001;
        }
        entry->light = (qa_scene_light){.origin = origin, .color = {1, 1, 1}, .radius = radius,
            .minimum = minimum, .scale = 1, .identity = qa_scene_identity(), .revision = 1, .family = QA_SCENE_Q1};
    }
    return true;
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
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    frontend_retained_bounds **link = &state->bounds;
    while (*link) {
        frontend_retained_bounds *entry = *link;
        if ((!entry->deadline && entry->frame != frontend->frame_number) || (entry->deadline && now >= entry->deadline) ||
            (entry->actor.registry && !qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), entry->actor))) {
            *link = entry->next; free(entry);
        } else link = &entry->next;
    }
    for (size_t i = 0; i < qa_application_event_count(frontend->application); ++i) {
        qa_builtin_event event;
        if (!qa_application_event_at(frontend->application, i, &event)) return frontend_fail(error, QA_ERROR_ARGUMENT, "builtin queue changed during map presentation");
        if (event.family == QA_GAME_Q1 && event.kind == QA_BUILTIN_EFFECT) {
            const char *resource = qa_strings_cstr(strings, event.resource);
            if (resource && !strcmp(resource, "debug-bounds")) {
                if (!qa_vec_finite(event.origin) || !qa_vec_finite(event.end) || event.code < 0 || event.code > 255 ||
                    !isfinite(event.value) || event.value < 0 || event.value > (double)(UINT64_MAX - event.time_ns) / 1e9)
                    return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid builtin debug bounds");
                frontend_retained_bounds *entry = calloc(1, sizeof(*entry));
                if (!entry) return frontend_fail(error, QA_ERROR_MEMORY, "retaining builtin debug bounds");
                *entry = (frontend_retained_bounds){.next = state->bounds, .owner = event.provider, .actor = event.actor,
                    .recipient = event.other, .bounds = {event.origin, event.end}, .color = (unsigned)event.code,
                    .frame = frontend->frame_number, .deadline = event.value > 0 ? event.time_ns + (uint64_t)(event.value * 1e9) : 0,
                    .depth = (event.flags & 1) != 0};
                state->bounds = entry;
            }
        }
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
                view->sky_owner = source.provider;
                view->sky_rotation = event->value; view->sky_axis = event->direction;
                view->sky_auto = event->value != 0 && (event->flags & 1) != 0; break;
            default: break;
            }
        }
        for (unsigned face = 0; face < 6; ++face) qa_scene_image_release(sky[face]);
    }
    return q1_effects(frontend, state, error);
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
    double seconds = (double)now / 1e9;
    for (unsigned i = 0; i < 32; ++i)
        if (state->q1_lights[i].actor.registry && state->q1_lights[i].die >= seconds &&
            qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), state->q1_lights[i].actor)) ++count;
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
    for (unsigned i = 0; i < 32; ++i)
        if (state->q1_lights[i].actor.registry && state->q1_lights[i].die >= seconds &&
            qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), state->q1_lights[i].actor))
            lights[used++] = state->q1_lights[i].light;
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
bool frontend_event_debug(qa_frontend *frontend, const qa_scene_view *view, qa_error *error)
{
    if (!frontend->events) return true;
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    for (frontend_retained_bounds *entry = frontend->events->bounds; entry; entry = entry->next) {
        if (!seat_receives(frontend, view->seat, entry->recipient) ||
            (entry->deadline ? now >= entry->deadline : entry->frame != frontend->frame_number)) continue;
        frontend_event_resources *resources; qa_bytes palette;
        if (!resources_read(frontend, entry->owner, QA_AUDIO_Q1, &resources, error) ||
            !qa_scene_resources_palette(resources->images, QA_SCENE_Q1, &palette, error)) return false;
        unsigned color = entry->color * 3;
        qa_scene_vec4 rgba = {palette.data[color] / 255.0f, palette.data[color + 1] / 255.0f, palette.data[color + 2] / 255.0f, 1};
        qa_debug_shape shape = {.kind = QA_DEBUG_BOUNDS, .data.bounds = entry->bounds};
        const qa_debug_line *lines; size_t count;
        if (!qa_debug_shape_lines(&shape, rgba, entry->depth, &frontend->frame.storage, &lines, &count, error) ||
            !qa_debug_draw(&frontend->frame, view, qa_scene_white(resources->images), lines, count, 1, error)) return false;
    }
    return true;
}
static uint32_t step_random(frontend_event_state *state)
{
    if (state->step_cursor == 624) {
        for (unsigned i = 0; i < 624; ++i) {
            uint32_t value = (state->step_random[i] & UINT32_C(0x80000000)) |
                (state->step_random[(i + 1) % 624] & UINT32_C(0x7fffffff));
            state->step_random[i] = state->step_random[(i + 397) % 624] ^ (value >> 1) ^
                ((value & 1) ? UINT32_C(0x9908b0df) : 0);
        }
        state->step_cursor = 0;
    }
    uint32_t value = state->step_random[state->step_cursor++];
    value ^= value >> 11; value ^= (value << 7) & UINT32_C(0x9d2c5680);
    value ^= (value << 15) & UINT32_C(0xefc60000); return value ^ (value >> 18);
}
static uint32_t step_uniform(frontend_event_state *state, uint32_t count)
{
    if (count < 2) return 0;
    uint32_t reject = (0u - count) % count, value;
    do value = step_random(state); while (value < reject);
    return value % count;
}
static bool material_equal(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return *a == *b;
}
static bool footsteps_read(frontend_event_resources *resources, const char *material,
    frontend_footsteps **out, qa_error *error)
{
    if (material_equal(material, "default")) material = "";
    if (material_equal(material, "ladder")) material = "ladder";
    for (frontend_footsteps *entry = resources->footsteps; entry; entry = entry->next)
        if (material_equal(entry->material, material)) { *out = entry; return true; }
    frontend_footsteps *entry = calloc(1, sizeof(*entry));
    if (!entry) return frontend_fail(error, QA_ERROR_MEMORY, "retaining source footstep table");
    snprintf(entry->material, sizeof(entry->material), "%s", material);
    for (unsigned i = 0; i < 16; ++i) {
        char path[64];
        if (*material) snprintf(path, sizeof(path), "#sound/player/steps/%s%u.wav", material, i + 1);
        else snprintf(path, sizeof(path), "#sound/player/step%u.wav", i + 1);
        qa_audio_asset *asset = NULL;
        if (!qa_audio_bank_register(resources->sounds, path, QA_AUDIO_Q2, &asset, error)) {
            for (unsigned j = 0; j < entry->count; ++j) qa_audio_asset_release(entry->assets[j]);
            free(entry); return false;
        }
        if (!asset) break;
        entry->assets[entry->count++] = asset;
    }
    entry->next = resources->footsteps; resources->footsteps = entry; *out = entry; return true;
}
static bool entity_footstep(qa_frontend *frontend, frontend_event_state *state,
    const qa_builtin_event *event, qa_error *error)
{
    const qa_cvar_view *enabled = qa_cvars_find(qa_application_cvars(frontend->application), "cl_footsteps");
    if (enabled && enabled->number == 0) return true;
    qa_world *world = qa_application_world(frontend->application);
    if (!qa_actors_get(qa_world_actors(world), event->actor)) return true;
    qa_body_state body; qa_actor_collision collision;
    if (!qa_world_body_read(world, event->actor, &body, error) ||
        !qa_world_get_collision(world, event->actor, &collision, error)) return false;
    char material[16] = "";
    if (event->code == 9) strcpy(material, "ladder");
    else if (!enabled || enabled->number < 2) {
        qa_vec3 start = body.origin; start.z += 1;
        qa_vec3 end = start; end.z -= 9;
        bool box = !collision.inline_model && collision.role != QA_COLLISION_TRIGGER && collision.contents;
        end.z += box ? body.bounds.mins.z : -66;
        qa_trace_query query = {.start = start, .end = end,
            .shape = {.kind = QA_SHAPE_BOX, .bounds = {qa_v3(box ? body.bounds.mins.x : 0, box ? body.bounds.mins.y : 0, 0),
                qa_v3(box ? body.bounds.maxs.x : 0, box ? body.bounds.maxs.y : 0, 0)}},
            .policy = qa_collision_default_policy(QA_COLLISION_Q2), .pass_actor = event->actor};
        query.policy.contents_mask = 1;
        qa_trace_result hit;
        if (!qa_world_trace(world, &query, &hit, error)) return false;
        if (hit.fraction < 1 && hit.has_surface) {
            memcpy(material, hit.surface.material, sizeof(material)); material[15] = 0;
            query.end = hit.end; query.end.z += 1; query.policy.contents_mask = 1 | 8 | 16 | 32;
            if (!qa_world_trace(world, &query, &hit, error)) return false;
            if (hit.has_surface) { memcpy(material, hit.surface.material, sizeof(material)); material[15] = 0; }
        }
    }
    frontend_event_resources *resources; frontend_footsteps *steps;
    if (!resources_read(frontend, event->provider, QA_AUDIO_Q2, &resources, error) ||
        !footsteps_read(resources, material, &steps, error)) return false;
    if (!steps->count && !footsteps_read(resources, "", &steps, error)) return false;
    if (!steps->count) return true;
    uint32_t index = step_uniform(state, steps->count);
    if (steps->assets[index] == state->last_step) index = (index + 1) % steps->count;
    qa_audio_asset *asset = steps->assets[index];
    uint64_t actor = frontend_audio_actor(frontend, event->actor, error);
    if (actor == QA_AUDIO_NO_ACTOR || !qa_audio_engine_position(frontend->audio, actor, body.origin, error)) return false;
    qa_audio_play sound = {.sample = qa_audio_asset_sample(asset), .asset = asset,
        .family = QA_AUDIO_Q2, .actor = actor, .owner = event->provider, .audience = QA_AUDIO_WORLD,
        .origin_kind = QA_AUDIO_ACTOR, .origin_actor = actor, .origin = body.origin, .channel = 6,
        .volume = event->code == 2 ? 1 : .5f, .attenuation = event->code == 2 ? 1 : 2};
    if (!qa_audio_engine_play(frontend->audio, &sound, (int32_t)((frontend->time_ns / 1000000) & INT32_MAX), error)) return false;
    qa_audio_asset_release(state->last_step); state->last_step = qa_audio_asset_retain(asset);
    state->last_step_owner = event->provider; return true;
}
bool frontend_event_sound(qa_frontend *frontend, const qa_builtin_event *event, qa_error *error)
{
    if (!frontend->audio) return true;
    if (event->kind == QA_BUILTIN_EFFECT && event->family == QA_GAME_Q2 &&
        (event->code == 2 || event->code == 8 || event->code == 9)) {
        const char *resource = qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)), event->resource);
        if (resource && !strcmp(resource, "q2:entity-event")) {
            frontend_event_state *state;
            return state_read(frontend, &state, error) && entity_footstep(frontend, state, event, error);
        }
    }
    if (event->kind != QA_BUILTIN_SOUND && event->kind != QA_BUILTIN_STOP_SOUND) return true;
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
static bool event_signature(qa_source_save_io *io)
{
    uint8_t magic[4] = {'Q','A','P','E'}; uint32_t version = 1;
    return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QAPE", 4) && qa_source_save_u32(io, &version) && version == 1;
}
static bool fog_fields(qa_source_save_io *io, qa_q2_fog *fog)
{
    float *fields[] = {&fog->density, &fog->sky_factor, &fog->start_distance, &fog->end_distance, &fog->falloff, &fog->height_density};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        if (!qa_source_save_f32(io, fields[i]) || !isfinite(*fields[i])) return false;
    return qa_source_save_vec3(io, &fog->color) && qa_vec_finite(fog->color) &&
        qa_source_save_vec3(io, &fog->start_color) && qa_vec_finite(fog->start_color) &&
        qa_source_save_vec3(io, &fog->end_color) && qa_vec_finite(fog->end_color);
}
static bool retained_asset(qa_source_save_io *io, qa_frontend *frontend, qa_actor_owner *owner,
    qa_audio_family *family, qa_audio_asset **asset)
{
    uint32_t kind = *family; const char *name = io->direction == QA_SOURCE_SAVE_WRITE ? qa_audio_asset_name(*asset) : NULL;
    qa_sha256_digest digest = {{0}};
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        const qa_sha256_digest *actual = qa_resource_digest(qa_audio_asset_resource(*asset));
        if (!actual || !name) return false;
        digest = *actual;
    }
    if (!frontend_save_provider(io, frontend->application, owner) || !*owner ||
        !qa_source_save_u32(io, &kind) || kind > QA_AUDIO_Q3 || !qa_source_save_text(io, &name) || !name ||
        !qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        frontend_event_resources *resources;
        if (!frontend->audio || !resources_read(frontend, *owner, (qa_audio_family)kind, &resources, io->error) ||
            !qa_audio_bank_register(resources->sounds, name, (qa_audio_family)kind, asset, io->error) || !*asset) return false;
        const qa_sha256_digest *actual = qa_resource_digest(qa_audio_asset_resource(*asset));
        if (!actual || memcmp(actual->bytes, digest.bytes, sizeof(digest.bytes))) return false;
        *family = (qa_audio_family)kind;
    }
    return true;
}
static bool sky_image_fields(qa_source_save_io *io, const qa_scene_image *image)
{
    if (image->kind == QA_SCENE_DEPTH32F) return false;
    uint32_t expected[] = {image->kind, image->wrap, image->filter, image->logical_width, image->logical_height};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        uint32_t field = expected[i];
        if (!qa_source_save_u32(io, &field) || field != expected[i]) return false;
    }
    float border[] = {image->border.x, image->border.y, image->border.z, image->border.w};
    for (size_t i = 0; i < 4; ++i) {
        float field = border[i];
        if (!qa_source_save_f32(io, &field) || !isfinite(field) || field != border[i]) return false;
    }
    size_t levels = image->level_count;
    if (!qa_source_save_count(io, &levels, image->level_count) || levels != image->level_count) return false;
    for (size_t i = 0; i < levels; ++i) {
        const qa_scene_image_level *level = &image->levels[i];
        uint32_t width = level->width, height = level->height; uint64_t size = level->bytes;
        qa_sha256_digest actual, saved; qa_sha256_context hash;
        qa_sha256_init(&hash); qa_sha256_update(&hash, (qa_bytes){level->pixels, level->bytes}); qa_sha256_final(&hash, &actual);
        saved = actual;
        if (!qa_source_save_u32(io, &width) || width != level->width ||
            !qa_source_save_u32(io, &height) || height != level->height ||
            !qa_source_save_u64(io, &size) || size != level->bytes ||
            !qa_source_save_bytes(io, saved.bytes, sizeof(saved.bytes)) || memcmp(saved.bytes, actual.bytes, sizeof(saved.bytes))) return false;
    }
    return true;
}
static bool view_fields(qa_source_save_io *io, qa_frontend *frontend, frontend_event_view *view)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    for (unsigned i = 0; i < FRONTEND_STYLES; ++i)
        if (!frontend_save_text(io, &view->q1_patterns[i]) || !frontend_save_text(io, &view->q2_patterns[i]) ||
            !qa_source_save_f32(io, &view->q1_styles[i]) || !isfinite(view->q1_styles[i]) ||
            !qa_source_save_vec3(io, &view->q2_styles[i]) || !qa_vec_finite(view->q2_styles[i])) return false;
    if (!fog_fields(io, &view->fog_start) || !fog_fields(io, &view->fog_target) ||
        !qa_source_save_u64(io, &view->fog_time) || !qa_source_save_f64(io, &view->fog_duration) || !isfinite(view->fog_duration) ||
        !qa_source_save_bool(io, &view->fog_received) || !qa_source_save_bool(io, &view->sky_received) ||
        !frontend_save_provider(io, frontend->application, &view->sky_owner) ||
        !qa_source_save_vec3(io, &view->sky_axis) || !qa_vec_finite(view->sky_axis) ||
        !qa_source_save_f32(io, &view->sky_rotation) || !isfinite(view->sky_rotation) || !qa_source_save_bool(io, &view->sky_auto)) return false;
    if (view->sky_received && !view->sky_owner) return false;
    for (unsigned i = 0; i < 6; ++i) {
        const char *name = reading ? NULL : view->sky[i] ? view->sky[i]->name : NULL;
        if (!qa_source_save_text(io, &name) || (view->sky_received && !name)) return false;
        if (!name) continue;
        if (reading) {
            frontend_event_resources *resources;
            if (!resources_read(frontend, view->sky_owner, QA_AUDIO_Q2, &resources, io->error)) return false;
            qa_scene_image_options options = {.family = QA_SCENE_Q2, .wrap = QA_SCENE_CLAMP, .filter = QA_SCENE_LINEAR,
                .usage = QA_IMAGE_USAGE_SKY, .transparent_index = -1};
            qa_scene_image *image = NULL;
            if (!qa_scene_image_load(resources->images, name, &options, &image, io->error)) return false;
            view->sky[i] = image;
        }
        if (!sky_image_fields(io, view->sky[i])) return false;
    }
    return true;
}
static bool sound_fields(qa_source_save_io *io, qa_frontend *frontend, frontend_retained_sound *entry)
{
    qa_audio_play *sound = &entry->loop.sound; bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t origin_kind = sound->origin_kind;
    bool is_static = entry->static_key != 0; uint32_t seats = entry->restored_static_seats;
    if (!reading) for (unsigned i = 0; i < frontend->options.seats; ++i) if (entry->static_mixers[i]) seats |= 1u << i;
    if (!qa_source_save_actor(io, &entry->actor) ||
        !retained_asset(io, frontend, &sound->owner, &sound->family, &sound->asset) ||
        !qa_source_save_bool(io, &is_static) || !qa_source_save_u32(io, &seats) || seats >= (1u << frontend->options.seats) ||
        !qa_source_save_u32(io, &origin_kind) || (origin_kind != QA_AUDIO_FIXED && origin_kind != QA_AUDIO_ACTOR) ||
        !qa_source_save_vec3(io, &sound->origin) || !qa_vec_finite(sound->origin) ||
        !qa_source_save_i32(io, &sound->channel) || !qa_source_save_f32(io, &sound->volume) ||
        !qa_source_save_f32(io, &sound->attenuation) || !isfinite(sound->volume) || sound->volume < 0 || sound->volume > 1 ||
        !isfinite(sound->attenuation) || sound->attenuation < 0 || !qa_source_save_vec3(io, &entry->loop.velocity) ||
        !qa_vec_finite(entry->loop.velocity) || !qa_source_save_i32(io, &entry->loop.frame_number) ||
        !qa_source_save_bool(io, &entry->loop.persistent)) return false;
    if (sound->channel < 0 && !(sound->family == QA_AUDIO_Q1 && sound->channel == -1)) return false;
    if (is_static ? entry->actor.registry != 0 || sound->family != QA_AUDIO_Q1 : !entry->actor.registry || seats != 0) return false;
    if (reading) {
        sound->sample = qa_audio_asset_sample(sound->asset); sound->audience = QA_AUDIO_WORLD;
        sound->actor = frontend_audio_actor(frontend, entry->actor, io->error); sound->origin_actor = sound->actor;
        if (entry->actor.registry && sound->actor == QA_AUDIO_NO_ACTOR) return false;
        sound->origin_kind = (qa_audio_origin_kind)origin_kind;
        if (is_static && (origin_kind != QA_AUDIO_FIXED || sound->sample->loop_start == QA_AUDIO_NO_LOOP)) return false;
        entry->static_key = is_static ? qa_scene_identity() : 0; entry->restored_static_seats = seats;
    }
    return true;
}
static bool light_fields(qa_source_save_io *io, frontend_q1_light *entry)
{
    qa_scene_light *light = &entry->light;
    if (!qa_source_save_actor(io, &entry->actor) || !qa_source_save_f64(io, &entry->die) || !isfinite(entry->die) ||
        !qa_source_save_vec3(io, &light->origin) || !qa_vec_finite(light->origin) ||
        !qa_source_save_vec3(io, &light->color) || !qa_vec_finite(light->color) ||
        !qa_source_save_f32(io, &light->radius) || !isfinite(light->radius) || light->radius < 0 ||
        !qa_source_save_f32(io, &light->minimum) || !isfinite(light->minimum) || light->minimum < 0) return false;
    if (io->direction == QA_SOURCE_SAVE_READ)
        light->identity = qa_scene_identity(), light->revision = 1, light->family = QA_SCENE_Q1, light->scale = 1;
    return true;
}
static bool bounds_fields(qa_source_save_io *io, qa_frontend *frontend, frontend_retained_bounds *entry)
{
    uint32_t color = entry->color;
    if (!frontend_save_provider(io, frontend->application, &entry->owner) || !entry->owner ||
        !qa_source_save_actor(io, &entry->actor) || !qa_source_save_actor(io, &entry->recipient) ||
        !qa_persistence_bounds(io, &entry->bounds) || !qa_source_save_u64(io, &entry->frame) ||
        !qa_source_save_u64(io, &entry->deadline) || !qa_source_save_u32(io, &color) || color > 255 ||
        !qa_source_save_bool(io, &entry->depth)) return false;
    entry->color = color; return true;
}
static bool last_step_fields(qa_source_save_io *io, qa_frontend *frontend, frontend_event_state *state)
{
    bool present = state->last_step != NULL;
    if (!qa_source_save_bool(io, &present) || !present) return !io->failed;
    qa_audio_family family = QA_AUDIO_Q2;
    return retained_asset(io, frontend, &state->last_step_owner, &family, &state->last_step) && family == QA_AUDIO_Q2;
}
bool frontend_event_checkpoint(qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io = {0};
    if (!out || !qa_source_save_writer(&io, qa_application_session(frontend->application), error)) return false;
    bool present = frontend->events != NULL; uint32_t seats = frontend->options.seats;
    bool ok = event_signature(&io) && qa_source_save_u32(&io, &seats) && qa_source_save_bool(&io, &present);
    frontend_event_state *state = frontend->events;
    if (ok && present) {
        qa_builtin_random random = state->light_random;
        ok = frontend_save_random(&io, &random);
        for (unsigned i = 0; ok && i < 624; ++i) { uint32_t word = state->step_random[i]; ok = qa_source_save_u32(&io, &word); }
        uint32_t cursor = state->step_cursor; ok = ok && qa_source_save_u32(&io, &cursor) && last_step_fields(&io, frontend, state);
        for (unsigned i = 0; ok && i < seats; ++i) { frontend_event_view copy = state->views[i]; ok = view_fields(&io, frontend, &copy); }
        for (unsigned i = 0; ok && i < 32; ++i) { frontend_q1_light copy = state->q1_lights[i]; ok = light_fields(&io, &copy); }
        size_t count = 0;
        for (frontend_retained_sound *entry = state->sounds; entry; entry = entry->next) ++count;
        ok = ok && qa_source_save_count(&io, &count, SIZE_MAX);
        for (frontend_retained_sound *entry = state->sounds; ok && entry; entry = entry->next) {
            frontend_retained_sound copy = *entry; ok = sound_fields(&io, frontend, &copy);
        }
        count = 0; for (frontend_retained_light *entry = state->lights; entry; entry = entry->next) ++count;
        ok = ok && qa_source_save_count(&io, &count, SIZE_MAX);
        for (frontend_retained_light *entry = state->lights; ok && entry; entry = entry->next) {
            frontend_retained_light copy = *entry;
            ok = frontend_save_provider(&io, frontend->application, &copy.owner) && frontend_save_q2_event(&io, &copy.event) &&
                qa_source_save_u64(&io, &copy.revision);
        }
        count = 0; for (frontend_retained_bounds *entry = state->bounds; entry; entry = entry->next) ++count;
        ok = ok && qa_source_save_count(&io, &count, SIZE_MAX);
        for (frontend_retained_bounds *entry = state->bounds; ok && entry; entry = entry->next) {
            frontend_retained_bounds copy = *entry; ok = bounds_fields(&io, frontend, &copy);
        }
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) frontend_fail(error, QA_ERROR_FORMAT, "invalid retained builtin presentation");
    qa_source_save_dispose(&io); return ok;
}
bool frontend_event_restore(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (frontend->events) return frontend_fail(error, QA_ERROR_ARGUMENT, "event restore requires an empty detached candidate");
    qa_source_save_io io = {0}; bool present = false; uint32_t seats = 0;
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) && event_signature(&io) &&
        qa_source_save_u32(&io, &seats) && seats == frontend->options.seats && qa_source_save_bool(&io, &present);
    frontend_event_state *state = NULL;
    if (ok && present) {
        ok = state_read(frontend, &state, error) && frontend_save_random(&io, &state->light_random);
        uint32_t any = 0;
        for (unsigned i = 0; ok && i < 624; ++i) { ok = qa_source_save_u32(&io, &state->step_random[i]); any |= state->step_random[i]; }
        ok = ok && any && qa_source_save_u32(&io, &state->step_cursor) && state->step_cursor <= 624 && last_step_fields(&io, frontend, state);
        for (unsigned i = 0; ok && i < seats; ++i) ok = view_fields(&io, frontend, &state->views[i]);
        for (unsigned i = 0; ok && i < 32; ++i) ok = light_fields(&io, &state->q1_lights[i]);
        size_t count = 0; frontend_retained_sound **sounds = &state->sounds;
        ok = ok && qa_source_save_count(&io, &count, bytes.size / 8);
        for (size_t i = 0; ok && i < count; ++i) {
            *sounds = calloc(1, sizeof(**sounds));
            if (!*sounds) { ok = frontend_fail(error, QA_ERROR_MEMORY, "restoring builtin sound continuation"); break; }
            frontend_retained_sound *entry = *sounds;
            ok = sound_fields(&io, frontend, entry);
            for (frontend_retained_sound *prior = state->sounds; ok && prior != entry; prior = prior->next)
                if (!entry->static_key && !prior->static_key && prior->loop.sound.owner == entry->loop.sound.owner &&
                    prior->loop.sound.family == entry->loop.sound.family && qa_actor_id_equal(prior->actor, entry->actor)) ok = false;
            sounds = &entry->next;
        }
        frontend_retained_light **lights = &state->lights;
        ok = ok && qa_source_save_count(&io, &count, bytes.size / 8);
        for (size_t i = 0; ok && i < count; ++i) {
            *lights = calloc(1, sizeof(**lights));
            if (!*lights) { ok = frontend_fail(error, QA_ERROR_MEMORY, "restoring authored light continuation"); break; }
            frontend_retained_light *entry = *lights;
            ok = frontend_save_provider(&io, frontend->application, &entry->owner) && entry->owner &&
                frontend_save_q2_event(&io, &entry->event) && entry->event.kind == QA_Q2_MAP_DYNAMIC_LIGHT &&
                qa_source_save_u64(&io, &entry->revision) && entry->revision;
            for (frontend_retained_light *prior = state->lights; ok && prior != entry; prior = prior->next)
                if (prior->owner == entry->owner && qa_actor_id_equal(prior->event.actor, entry->event.actor)) ok = false;
            entry->identity = qa_scene_identity(); lights = &entry->next;
        }
        frontend_retained_bounds **bounds = &state->bounds;
        ok = ok && qa_source_save_count(&io, &count, bytes.size / 8);
        for (size_t i = 0; ok && i < count; ++i) {
            *bounds = calloc(1, sizeof(**bounds));
            if (!*bounds) { ok = frontend_fail(error, QA_ERROR_MEMORY, "restoring hunter bounds continuation"); break; }
            frontend_retained_bounds *entry = *bounds;
            ok = bounds_fields(&io, frontend, entry);
            bounds = &entry->next;
        }
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (!ok) {
        frontend_event_retire(frontend);
        if (!error || error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "invalid saved builtin presentation");
    }
    qa_source_save_dispose(&io); return ok;
}
bool frontend_event_reconnect_audio(qa_frontend *frontend, qa_error *error)
{
    if (!frontend->events) return true;
    for (frontend_retained_sound *entry = frontend->events->sounds; entry; entry = entry->next) {
        for (unsigned i = 0; i < frontend->options.seats; ++i) {
            if (!(entry->restored_static_seats & (1u << i))) continue;
            entry->static_mixers[i] = qa_audio_engine_seat_mixer(frontend->audio, i);
            if (!entry->static_mixers[i]) return frontend_fail(error, QA_ERROR_FORMAT, "saved static sound has no restored seat mixer");
        }
        entry->restored_static_seats = 0;
    }
    return true;
}
bool frontend_event_static_index(const qa_frontend *frontend, uint64_t key, uint64_t *out)
{
    uint64_t index = 0;
    if (!key || !out) return false;
    for (const frontend_retained_sound *entry = frontend->events ? frontend->events->sounds : NULL; entry; entry = entry->next) {
        if (!entry->static_key) continue;
        if (entry->static_key == key) { *out = index; return true; }
        ++index;
    }
    return false;
}
bool frontend_event_static_key(const qa_frontend *frontend, uint64_t index, uint64_t *out)
{
    if (!out) return false;
    for (const frontend_retained_sound *entry = frontend->events ? frontend->events->sounds : NULL; entry; entry = entry->next) {
        if (!entry->static_key) continue;
        if (!index) { *out = entry->static_key; return true; }
        --index;
    }
    return false;
}
