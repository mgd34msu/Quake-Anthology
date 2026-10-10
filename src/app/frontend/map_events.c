#include "internal.h"
#include "qa/pool.h"
#include "qc_rerelease_events.h"
#include "qa/scene_resource_save.h"
#include "round.h"
#include "qa/application_equipment_content.h"
#include "particle_audio.h"
#include "particle_delivery.h"
#include "particle_clock.h"
#include "legacy_render_policy.h"
#include "qa/application_selected_effects.h"
#include "qa/application_players.h"
#include "resource_bindings.h"
#include "shared_resource_policy.h"
#include "q1_sky.h"
#include "music_sources.h"
#include "qa/text.h"
#include "remote_q2_effects.h"
#include "native_q2_messages.h"
#include "source_client_registry.h"
#include "qa/application_native_q2_presentation.h"
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
    qa_game_family family;
    qa_vfs *files;
    qa_audio_bank *sounds;
    qa_scene_resources *images;
    frontend_footsteps *footsteps;
    bool gear;
    qa_actor_owner selected_owner;
    qa_string_id service_owner;
    qa_launch_instance_lease *descriptor;
    qa_resource *artifact;
} frontend_event_resources;
typedef struct frontend_retained_sound {
    size_t slot;
    struct frontend_retained_sound *next;
    qa_actor_id actor;
    qa_audio_loop loop;
    uint64_t renewed_frame;
    uint64_t static_key;
    qa_audio_mixer *static_mixers[QA_INPUT_LOCAL_SEATS];
    uint32_t restored_static_seats;
} frontend_retained_sound;
typedef enum frontend_audio_projection_kind {
    FRONTEND_AUDIO_PLAY, FRONTEND_AUDIO_STOP_CHANNEL
} frontend_audio_projection_kind;
typedef struct frontend_audio_projection {
    size_t slot;
    struct frontend_audio_projection *next;
    frontend_audio_projection_kind kind;
    qa_actor_id actor;
    qa_actor_id recipient; /* Delivery identity; FIXED sounds have no emitter actor. */
    qa_audio_play sound;
    int32_t milliseconds;
} frontend_audio_projection;
typedef struct frontend_retained_light {
    size_t slot;
    struct frontend_retained_light *next;
    qa_actor_owner owner;
    qa_q2_map_event event;
    uint64_t identity, revision;
} frontend_retained_light;
typedef struct frontend_retained_bounds {
    size_t slot;
    struct frontend_retained_bounds *next;
    qa_actor_owner owner;
    qa_actor_id actor, recipient;
    qa_bounds bounds;
    uint64_t frame, deadline;
    unsigned color;
    bool depth;
} frontend_retained_bounds;
typedef struct frontend_q1_fog {
    qa_actor_owner owner;
    qa_actor_id recipient;
    qa_vec3 start_color, target_color;
    float start_density, target_density, sky_factor;
    uint64_t time;
    double duration;
} frontend_q1_fog;
typedef struct frontend_event_view {
    qa_string_id q1_patterns[FRONTEND_STYLES], q2_patterns[FRONTEND_STYLES];
    qa_actor_owner q1_style_owners[FRONTEND_STYLES], q2_style_owners[FRONTEND_STYLES];
    float q1_styles[FRONTEND_STYLES];
    qa_vec3 q2_styles[FRONTEND_STYLES];
    qa_q2_fog fog_start, fog_target;
    uint64_t fog_time;
    double fog_duration;
    bool fog_received, sky_received;
    frontend_q1_fog q1_fog;
    const qa_scene_image *sky[6];
    qa_string_id sky_name;
    qa_actor_owner sky_owner;
    qa_vec3 sky_axis;
    float sky_rotation;
    bool sky_auto;
} frontend_event_view;
struct frontend_event_state {
    qa_arena storage;
    qa_pool projections, sound_records, light_records, bound_records;
    frontend_event_resources *resources;
    frontend_event_image_policy *image_policy;
    frontend_retained_sound *sounds;
    frontend_retained_light *lights;
    frontend_retained_bounds *bounds;
    qa_builtin_random light_random;
    uint32_t step_random[624], step_cursor;
    qa_audio_asset *last_step;
    qa_actor_owner last_step_owner;
    frontend_audio_projection *audio_head, *audio_tail;
    bool audio_deferred;
    frontend_event_view views[QA_INPUT_LOCAL_SEATS];
};
typedef struct event_owner_plan {
    qa_actor_owner owner;
    qa_game_family family;
    uint64_t view;
    bool sounds;
    bool gear;
} event_owner_plan;
static bool gear_resources_bind(qa_application *app, frontend_event_resources *entry, qa_error *error)
{
    qa_application_equipment_content source;
    if (!entry->gear || entry->family != QA_GAME_Q3 ||
        !qa_application_equipment_content_read(app, entry->owner, &source, error) ||
        !qa_vfs_lookup_equal(entry->files, source.files))
        return frontend_fail(error, QA_ERROR_FORMAT, "Gear sound resources lost their actual retained content authority");
    if (entry->descriptor) {
        const qa_launch_instance *held = qa_launch_instance_lease_view(entry->descriptor);
        return (held && held->storage == source.descriptor->storage &&
            entry->artifact == source.artifact && entry->selected_owner == source.selected_owner &&
            entry->service_owner == source.service_owner) ||
            frontend_fail(error, QA_ERROR_FORMAT, "Gear sound resources changed their actual profile or receiver");
    }
    qa_launch_instance_lease *lease = NULL;
    if (!qa_launch_instance_retain_metadata(source.descriptor, &lease, error)) return false;
    if (!qa_application_equipment_content_current(app, &source)) {
        qa_launch_instance_lease_release(lease);
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear sound source retired during metadata admission");
    }
    entry->descriptor = lease; entry->artifact = (qa_resource *)source.artifact;
    qa_resource_retain(entry->artifact);
    entry->selected_owner = source.selected_owner; entry->service_owner = source.service_owner;
    return true;
}
static bool resources_current(const qa_application *app, const frontend_event_resources *entry)
{
    if (!entry->gear) return qa_application_provider_instance(app, entry->owner) != NULL;
    qa_application_equipment_content source;
    const qa_launch_instance *held = qa_launch_instance_lease_view(entry->descriptor);
    return entry->family == QA_GAME_Q3 && held &&
        qa_application_equipment_content_read(app, entry->owner, &source, NULL) &&
        held->storage == source.descriptor->storage && entry->artifact == source.artifact &&
        entry->selected_owner == source.selected_owner && entry->service_owner == source.service_owner &&
        qa_vfs_lookup_equal(entry->files, source.files);
}
bool frontend_event_q1_images_read(const qa_frontend *frontend, qa_actor_owner provider,
    qa_scene_resources **images, qa_vfs **files, qa_error *error)
{
    if (images) *images = NULL;
    if (files) *files = NULL;
    if (!frontend || !frontend->application || !provider || !images || !files)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 event images require their actual provider and borrowed outputs");
    for (const frontend_event_resources *entry = frontend->events ? frontend->events->resources : NULL;
        entry; entry = entry->next) {
        if (entry->owner != provider || entry->family != QA_GAME_Q1) continue;
        if (entry->gear || !entry->images || !entry->files ||
            qa_scene_resources_files(entry->images) != entry->files ||
            !resources_current(frontend->application, entry))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 event image owner is no longer physically bound");
        *images = entry->images; *files = entry->files; return true;
    }
    return frontend_fail(error, QA_ERROR_NOT_FOUND, "Q1 provider has no admitted event image bank");
}

static qa_game_family audio_family(qa_game_family family)
{
    return family == QA_GAME_Q3 ? QA_GAME_Q3 : family == QA_GAME_Q2 ? QA_GAME_Q2 : QA_GAME_Q1;
}
static void q1_fog_sample(const frontend_q1_fog *fog, uint64_t now, float *density, qa_vec3 *color)
{
    double elapsed = now >= fog->time ? (double)(now - fog->time) / 1e9 : 0;
    float t = fog->duration <= 0 ? 1 : (float)fmin(1, elapsed / fog->duration);
    *density = fog->start_density + (fog->target_density - fog->start_density) * t;
    *color = qa_vec_add(fog->start_color, qa_vec_scale(qa_vec_sub(fog->target_color, fog->start_color), t));
}
static bool q1_fog_initialize(qa_frontend *frontend, unsigned seat, frontend_q1_fog *fog, qa_error *error)
{
    qa_actor_owner owner = 0; qa_actor_id recipient;
    if (!qa_application_q1_fog_owner(frontend->application, &owner) ||
        !frontend_seat_actor_read(frontend,seat,&recipient)) {
        *fog = (frontend_q1_fog){0}; return true;
    }
    if (fog->owner == owner && qa_actor_id_equal(fog->recipient, recipient)) return true;
    frontend_q1_fog initial = {.owner = owner, .recipient = recipient,
        .start_color = {.3f, .3f, .3f}, .target_color = {.3f, .3f, .3f}, .sky_factor = .5f};
    if (frontend->map_resource) {
        qa_bsp_view bsp;
        if (!qa_bsp_open(qa_resource_bytes(frontend->map_resource), &bsp, error)) return false;
        if (bsp.family == QA_BSP_Q1) {
            qa_entities entities = {0};
            if (!qa_entities_parse(bsp.lumps[QA_BSP_ENTITIES].bytes, QA_ENTITY_Q1, &entities, error)) return false;
            if (entities.count) for (size_t i = 0; i < entities.records[0].property_count; ++i) {
                const qa_entity_property *property = &entities.properties[entities.records[0].first_property + i];
                qa_bytes key = property->key;
                if (key.size && key.data[0] == '_') ++key.data, --key.size;
                while (key.size && key.data[key.size - 1] == ' ') --key.size;
                if (key.size != 3 || memcmp(key.data, "fog", 3)) continue;
                if (property->value.size == SIZE_MAX) { qa_entities_free(&entities); return frontend_fail(error, QA_ERROR_MEMORY, "fog text exceeds native storage"); }
                char *text = malloc(property->value.size + 1);
                if (!text) { qa_entities_free(&entities); return frontend_fail(error, QA_ERROR_MEMORY, "reading worldspawn fog"); }
                memcpy(text, property->value.data, property->value.size); text[property->value.size] = 0;
                sscanf(text, "%f %f %f %f", &initial.target_density, &initial.target_color.x,
                    &initial.target_color.y, &initial.target_color.z);
                free(text);
            }
            qa_entities_free(&entities);
        }
    }
    if (!isfinite(initial.target_density) || !qa_vec_finite(initial.target_color))
        return frontend_fail(error, QA_ERROR_FORMAT, "non-finite worldspawn fog");
    initial.start_density = initial.target_density; initial.start_color = initial.target_color;
    *fog = initial; return true;
}
static bool state_read(qa_frontend *frontend, frontend_event_state **out, qa_error *error)
{
    if (frontend->capture || frontend->resource_inventory)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Event mutation overlaps the actual frontend capture");
    if (!frontend->events) {
        frontend->events = calloc(1, sizeof(*frontend->events));
        if (!frontend->events) return frontend_fail(error, QA_ERROR_MEMORY, "allocating frontend presentation state");
        frontend_event_state *state = frontend->events;
        size_t capacity = qa_actors_capacity(qa_session_actor_registry(qa_application_session(frontend->application)));
        if (capacity > (SIZE_MAX - 256) / 2) {
            free(state); frontend->events = NULL;
            return frontend_fail(error, QA_ERROR_MEMORY, "Frontend event capacity overflow");
        }
        capacity = capacity * 2 + 256;
        qa_arena_init(&state->storage, 0);
        if (!qa_pool_prepare(&state->projections, &state->storage, capacity,
                sizeof(frontend_audio_projection), _Alignof(frontend_audio_projection), error) ||
            !qa_pool_prepare(&state->sound_records, &state->storage, capacity,
                sizeof(frontend_retained_sound), _Alignof(frontend_retained_sound), error) ||
            !qa_pool_prepare(&state->light_records, &state->storage, capacity,
                sizeof(frontend_retained_light), _Alignof(frontend_retained_light), error) ||
            !qa_pool_prepare(&state->bound_records, &state->storage, capacity,
                sizeof(frontend_retained_bounds), _Alignof(frontend_retained_bounds), error)) {
            qa_arena_destroy(&state->storage); free(state); frontend->events = NULL; return false;
        }
        qa_arena_seal(&state->storage);
        qa_builtin_random_seed(&frontend->events->light_random, 1);
        frontend->events->step_random[0] = 1;
        for (uint32_t i = 1; i < 624; ++i) {
            uint32_t prior = frontend->events->step_random[i - 1];
            frontend->events->step_random[i] = 1812433253u * (prior ^ (prior >> 30)) + i;
        }
        frontend->events->step_cursor = 624;
        frontend->events->audio_deferred = frontend_round_audio_pending(frontend);
    }
    *out = frontend->events; return true;
}
bool frontend_event_prepare(qa_frontend *frontend, qa_error *error)
{
    frontend_event_state *state;
    return state_read(frontend, &state, error);
}
static void audio_projection_free(frontend_event_state *state, frontend_audio_projection *entry)
{
    qa_audio_asset_release(entry->sound.asset); qa_pool_release(&state->projections, entry->slot);
}
static void audio_projection_clear(frontend_event_state *state)
{
    while (state->audio_head) {
        frontend_audio_projection *entry = state->audio_head;
        state->audio_head = entry->next; audio_projection_free(state, entry);
    }
    state->audio_tail = NULL;
}
static bool audio_projection_append(frontend_event_state *state, qa_actor_id actor, qa_actor_id recipient,
    const qa_audio_play *sound, frontend_audio_projection_kind kind,
    int32_t milliseconds, qa_error *error)
{
    size_t slot;
    frontend_audio_projection *entry = qa_pool_take(&state->projections, &slot);
    if (!entry) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining round sound projection");
    *entry = (frontend_audio_projection){.slot = slot, .kind = kind, .actor = actor, .sound = *sound};
    entry->recipient = recipient;
    entry->milliseconds = milliseconds;
    entry->sound.asset = qa_audio_asset_retain(sound->asset);
    if (sound->asset && !entry->sound.asset) {
        audio_projection_free(state, entry); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining projected sound asset");
    }
    /* Every named projection comes from a retained sound-bank asset. Channel
     * stops have no name; the asset owns its canonical path through delivery. */
    entry->sound.name = sound->name ? qa_audio_asset_name(entry->sound.asset) : NULL;
    if (state->audio_tail) state->audio_tail->next = entry;
    else state->audio_head = entry;
    state->audio_tail = entry; return true;
}
static bool audio_receives(qa_frontend *frontend, uint32_t audience, uint32_t seat)
{
    return !frontend_network_local_input_owned(frontend,seat) &&
        (audience == QA_AUDIO_WORLD || audience == seat);
}
static bool audio_play_receivers(qa_frontend *frontend, const qa_audio_play *sound,
    int32_t milliseconds, qa_error *error)
{
    for (uint32_t seat = 0; seat < frontend->options.seats; ++seat) {
        if (!audio_receives(frontend,sound->audience,seat)) continue;
        qa_audio_play delivered = *sound;
        delivered.audience = seat;
        if (!qa_audio_engine_play(frontend->audio,&delivered,milliseconds,error)) return false;
    }
    return true;
}
static bool audio_play(qa_frontend *frontend, frontend_event_state *state,
    qa_actor_id actor, const qa_audio_play *sound, qa_error *error)
{
    int32_t milliseconds = (int32_t)((frontend->time_ns / 1000000) & INT32_MAX);
    if (state->audio_deferred || state->audio_head)
        return audio_projection_append(state, actor, (qa_actor_id){0}, sound, FRONTEND_AUDIO_PLAY, milliseconds, error);
    return audio_play_receivers(frontend, sound, milliseconds, error);
}
static bool audio_stop_channel(qa_frontend *frontend, frontend_event_state *state,
    qa_actor_id actor, uint64_t identity, qa_actor_owner owner,
    qa_game_family family, int32_t channel, qa_error *error)
{
    if (state->audio_deferred || state->audio_head) {
        qa_audio_play sound = {.actor = identity, .owner = owner, .family = family, .channel = channel};
        return audio_projection_append(state, actor, (qa_actor_id){0}, &sound, FRONTEND_AUDIO_STOP_CHANNEL, 0, error);
    }
    qa_audio_engine_stop_channel(frontend->audio, identity, owner, family, channel); return true;
}
static bool audio_projection_publish(qa_frontend *frontend, frontend_event_state *state,
    qa_error *error)
{
    if (!state->audio_deferred && !state->audio_head) return true;
    if (frontend->round) return true;
    bool listeners = false;
    for (unsigned i = 0; i < frontend->options.seats; ++i)
        listeners = listeners || qa_audio_engine_seat_mixer(frontend->audio, i) != NULL;
    if (!listeners) return true;
    state->audio_deferred = false;
    while (state->audio_head) {
        frontend_audio_projection *entry = state->audio_head;
        state->audio_head = entry->next;
        if (!state->audio_head) state->audio_tail = NULL;
        bool ok = true;
        qa_actor_id recipient;
        bool delivered = !entry->recipient.registry ||
            (frontend_seat_actor_read(frontend, entry->sound.audience, &recipient) &&
                qa_actor_id_equal(recipient, entry->recipient));
        if (entry->kind == FRONTEND_AUDIO_PLAY && delivered)
            ok = audio_play_receivers(frontend, &entry->sound, entry->milliseconds, error);
        else if (entry->kind == FRONTEND_AUDIO_STOP_CHANNEL) qa_audio_engine_stop_channel(frontend->audio, entry->sound.actor,
            entry->sound.owner, entry->sound.family, entry->sound.channel);
        audio_projection_free(state, entry);
        if (!ok) return false;
    }
    return true;
}
static bool resources_read(qa_frontend *frontend, qa_actor_owner provider, qa_game_family family,
    frontend_event_resources **out, qa_error *error)
{
    frontend_event_state *state;
    if (!state_read(frontend, &state, error)) return false;
    for (frontend_event_resources *entry = state->resources; entry; entry = entry->next)
        if (entry->owner == provider && entry->family == family) {
            if (entry->gear && !resources_current(frontend->application, entry))
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear event resources belong to a retired source");
            *out = entry; return true;
        }
    qa_application_equipment_content source;
    bool gear = !qa_application_provider_instance(frontend->application, provider) &&
        qa_application_equipment_content_read(frontend->application, provider, &source, NULL);
    if (gear && family != QA_GAME_Q3)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear events require their actual Q3 sound family");
    const qa_vfs *files = gear ? source.files : qa_application_provider_files(frontend->application, provider);
    if (!files) return frontend_fail(error, QA_ERROR_NOT_FOUND, "presentation event owner has no active content view");
    frontend_event_resources *entry = calloc(1, sizeof(*entry));
    if (!entry) return frontend_fail(error, QA_ERROR_MEMORY, "allocating presentation event resources");
    entry->owner = provider; entry->family = family; entry->gear = gear;
    entry->files = qa_vfs_clone(files, error);
    entry->images = entry->files ? qa_scene_resources_create(entry->files, error) : NULL;
    bool ok = entry->files && entry->images &&
        frontend_image_policy_initialize(frontend, entry->images, error) &&
        (!frontend->audio || qa_audio_bank_create(entry->files, &entry->sounds, error)) &&
        (!gear || gear_resources_bind(frontend->application, entry, error));
    if (!ok) {
        qa_audio_bank_destroy(entry->sounds); qa_scene_resources_destroy(entry->images);
        qa_vfs_destroy(entry->files); qa_resource_release(entry->artifact);
        qa_launch_instance_lease_release(entry->descriptor); free(entry); return false;
    }
    entry->next = state->resources; state->resources = entry; *out = entry; return true;
}
bool frontend_event_qc_resources(qa_frontend *frontend,qa_actor_owner owner,
    qa_scene_resources **images,qa_audio_bank **sounds,qa_error *error)
{
    if (!frontend || !frontend->application || !owner || !images || !sounds)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC event resources require their actual source owner");
    *images=NULL; *sounds=NULL;
    frontend_event_resources *resources;
    if (!resources_read(frontend,owner,QA_GAME_Q1,&resources,error)) return false;
    *images=resources->images; *sounds=resources->sounds; return true;
}
void frontend_event_reset_round(qa_frontend *frontend)
{
    if (frontend->capture || frontend->resource_inventory) return;
    frontend_event_state *state = frontend->events;
    if (!state) return;
    audio_projection_clear(state); state->audio_deferred = true;
    while (state->sounds) {
        frontend_retained_sound *entry = state->sounds; state->sounds = entry->next;
        qa_audio_asset_release(entry->loop.sound.asset); qa_pool_release(&state->sound_records, entry->slot);
    }
    while (state->lights) {
        frontend_retained_light *entry = state->lights; state->lights = entry->next; qa_pool_release(&state->light_records, entry->slot);
    }
    while (state->bounds) {
        frontend_retained_bounds *entry = state->bounds; state->bounds = entry->next; qa_pool_release(&state->bound_records, entry->slot);
    }
    for (unsigned i = 0; i < QA_INPUT_LOCAL_SEATS; ++i) {
        frontend_event_view *view = &state->views[i];
        view->fog_start = view->fog_target = (qa_q2_fog){0};
        view->fog_time = 0; view->fog_duration = 0; view->fog_received = false;
        view->q1_fog = (frontend_q1_fog){0};
    }
}
bool frontend_event_retire_checked(qa_frontend *frontend, qa_error *error)
{
    if (!frontend || frontend->capture || frontend->resource_inventory)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Event retirement retains an active resource owner");
    frontend_event_state *state = frontend->events;
    if (!state) return true;
    if (frontend->q1_sky)
        for (frontend_event_resources *entry = state->resources; entry; entry = entry->next)
            if (entry->family == QA_GAME_Q1 &&
                !frontend_q1_sky_retire_provider(frontend->q1_sky, entry->owner, error)) return false;
    audio_projection_clear(state);
    while (state->sounds) {
        frontend_retained_sound *entry = state->sounds; state->sounds = entry->next;
        qa_audio_asset_release(entry->loop.sound.asset); qa_pool_release(&state->sound_records, entry->slot);
    }
    while (state->lights) {
        frontend_retained_light *entry = state->lights; state->lights = entry->next; qa_pool_release(&state->light_records, entry->slot);
    }
    while (state->bounds) {
        frontend_retained_bounds *entry = state->bounds; state->bounds = entry->next; qa_pool_release(&state->bound_records, entry->slot);
    }
    for (unsigned i = 0; i < QA_INPUT_LOCAL_SEATS; ++i) {
        for (unsigned face = 0; face < 6; ++face) qa_scene_image_release(state->views[i].sky[face]);
    }
    while (state->resources) {
        frontend_event_resources *entry = state->resources; state->resources = entry->next;
        while (entry->footsteps) {
            frontend_footsteps *steps = entry->footsteps; entry->footsteps = steps->next;
            for (uint32_t i = 0; i < steps->count; ++i) qa_audio_asset_release(steps->assets[i]);
            free(steps);
        }
        qa_audio_bank_destroy(entry->sounds); qa_scene_resources_destroy(entry->images);
        qa_vfs_destroy(entry->files); qa_resource_release(entry->artifact);
        qa_launch_instance_lease_release(entry->descriptor); free(entry);
    }
    qa_audio_asset_release(state->last_step); qa_arena_destroy(&state->storage); free(state); frontend->events = NULL;
    return true;
}

static bool seat_receives(qa_frontend *frontend, unsigned seat, qa_actor_id recipient)
{
    qa_actor_id actor;
    return !frontend_network_local_input_owned(frontend,seat) &&
        (!recipient.registry || (frontend_seat_actor_read(frontend,seat,&actor) &&
            qa_actor_id_equal(actor, recipient)));
}
static float fog_fraction(float value)
{
    uint8_t byte = (uint8_t)(uint32_t)qa_source_float_to_i32(value * 255.0f);
    return (float)byte / 255;
}
static qa_vec3 fog_color(qa_vec3 color)
{
    return qa_v3(fog_fraction(color.x), fog_fraction(color.y), fog_fraction(color.z));
}
static qa_q2_fog fog_source(qa_q2_fog fog)
{
    fog.color = fog_color(fog.color); fog.start_color = fog_color(fog.start_color);
    fog.end_color = fog_color(fog.end_color); fog.sky_factor = fog_fraction(fog.sky_factor);
    fog.start_distance = (float)qa_source_float_to_i32(fog.start_distance);
    fog.end_distance = (float)qa_source_float_to_i32(fog.end_distance);
    return fog;
}
bool frontend_map_events(qa_frontend *frontend, qa_error *error)
{
    if (frontend->options.dedicated) return true;
    frontend_event_state *state;
    if (!state_read(frontend, &state, error)) return false;
    qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    for (unsigned seat = 0; seat < frontend->options.seats; ++seat)
        if (!frontend_network_local_input_owned(frontend,seat) &&
            !q1_fog_initialize(frontend, seat, &state->views[seat].q1_fog, error)) return false;
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    frontend_retained_bounds **link = &state->bounds;
    while (*link) {
        frontend_retained_bounds *entry = *link;
        if ((!entry->deadline && entry->frame != frontend->frame_number) || (entry->deadline && now >= entry->deadline) ||
            (entry->actor.registry && !qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), entry->actor))) {
            *link = entry->next; qa_pool_release(&state->bound_records, entry->slot);
        } else link = &entry->next;
    }
    for (uint64_t i = qa_application_events_local_first(frontend->application);
        i < qa_application_events_next(frontend->application); ++i) {
        qa_application_event_view output;
        if (!qa_application_event_read(frontend->application, &(qa_application_event_cursor){.id = i, .projection = 0}, &output) ||
            output.kind != QA_APPLICATION_EVENT_BUILTIN) continue;
        qa_builtin_event event = *output.value.builtin;
        bool handled;
        if (!frontend_qc_rerelease_event(frontend,&event,&handled,error)) return false;
        if (handled) continue;
        if (event.family == QA_GAME_Q1 && event.kind == QA_BUILTIN_EFFECT) {
            const char *resource = qa_strings_cstr(strings, event.resource);
            if (resource && !strcmp(resource, "music")) {
                if (!frontend_music_sources_world_cd(frontend->music_sources,
                    (uint8_t)(uint32_t)event.code, error)) return false;
                continue;
            }
            if (resource && !strcmp(resource, "q1:fog")) {
                if (!qa_vec_finite(event.origin) || !isfinite(event.value) || !isfinite(event.end.x) || event.end.x < 0)
                    return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid authored Q1 fog");
                qa_clock_state clock;
                if (!qa_session_clock(qa_application_session(frontend->application), event.provider, &clock) ||
                    event.time_ns > clock.frame.time_ns)
                    return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 fog has no matching source clock");
                for (unsigned seat = 0; seat < frontend->options.seats; ++seat) {
                    if (frontend_network_local_input_owned(frontend,seat)) continue;
                    frontend_q1_fog *fog = &state->views[seat].q1_fog;
                    if (fog->owner != event.provider || !qa_actor_id_equal(fog->recipient, event.actor)) continue;
                    q1_fog_sample(fog, event.time_ns, &fog->start_density, &fog->start_color);
                    fog->target_density = event.value; fog->target_color = event.origin;
                    fog->time = event.time_ns; fog->duration = event.end.x;
                    fog->sky_factor = 0;
                }
            }
            if (resource && !strcmp(resource, "debug-bounds")) {
                if (!qa_vec_finite(event.origin) || !qa_vec_finite(event.end) || event.code < 0 || event.code > 255 ||
                    !isfinite(event.value) || event.value < 0 || event.value > (double)(UINT64_MAX - event.time_ns) / 1e9)
                    return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid builtin debug bounds");
                size_t slot;
                frontend_retained_bounds *entry = qa_pool_take(&state->bound_records, &slot);
                if (!entry) return frontend_fail(error, QA_ERROR_MEMORY, "retaining builtin debug bounds");
                *entry = (frontend_retained_bounds){.slot = slot, .next = state->bounds, .owner = event.provider, .actor = event.actor,
                    .recipient = event.other, .bounds = {event.origin, event.end}, .color = (unsigned)event.code,
                    .frame = frontend->frame_number, .deadline = event.value > 0 ? event.time_ns + (uint64_t)(event.value * 1e9) : 0,
                    .depth = (event.flags & 1) != 0};
                state->bounds = entry;
            }
        }
        if (event.kind != QA_BUILTIN_LIGHT || event.family != QA_GAME_Q1 || event.code < 0 || event.code >= FRONTEND_STYLES) continue;
        for (unsigned seat = 0; seat < frontend->options.seats; ++seat) {
            if (frontend_network_local_input_owned(frontend,seat)) continue;
            frontend_event_view *view=&state->views[seat];
            view->q1_patterns[event.code] = event.resource;
            view->q1_style_owners[event.code]=event.provider;
        }
    }
    for (uint64_t i = qa_application_events_local_first(frontend->application);
        i < qa_application_events_next(frontend->application); ++i) {
        qa_application_event_view output;
        if (!qa_application_event_read(frontend->application, &(qa_application_event_cursor){.id = i, .projection = 0}, &output) ||
            output.kind != QA_APPLICATION_EVENT_Q2_MAP) continue;
        qa_application_q2_map_event source = *output.value.q2_map;
        const qa_q2_map_event *event = &source.event;
        if (event->kind == QA_Q2_MAP_DYNAMIC_LIGHT) {
            frontend_retained_light *light = state->lights;
            while (light && (light->owner != source.provider || !qa_actor_id_equal(light->event.actor, event->actor))) light = light->next;
            if (!light) {
                size_t slot; light = qa_pool_take(&state->light_records, &slot);
                if (!light) return frontend_fail(error, QA_ERROR_MEMORY, "retaining authored dynamic light");
                *light = (frontend_retained_light){.slot = slot};
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
            if (!resources_read(frontend, source.provider, QA_GAME_Q2, &resources, error)) return false;
            size_t length = strlen(name);
            if (length > SIZE_MAX - 7) return frontend_fail(error, QA_ERROR_MEMORY, "sky path exceeds native storage");
            char *path = qa_arena_alloc(&frontend->frame.storage,length + 7,1,error);
            if (!path) return frontend_fail(error, QA_ERROR_MEMORY, "allocating authored sky path");
            static const char *const suffixes[6] = {"rt", "lf", "bk", "ft", "up", "dn"};
            qa_scene_image_options options = {.family = QA_GAME_Q2, .wrap = QA_SCENE_CLAMP,
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
                if (event->style >= 0 && event->style < FRONTEND_STYLES) {
                    view->q2_patterns[event->style] = event->text;
                    view->q2_style_owners[event->style]=source.provider;
                }
                break;
            case QA_Q2_MAP_FOG:
                if (event->duration != 0) { view->fog_start = view->fog_target; view->fog_time = source.time_ns; }
                view->fog_target = fog_source(event->fog); view->fog_duration = event->duration;
                view->fog_received = true; break;
            case QA_Q2_MAP_SKY:
                view->sky_name = event->resource;
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
    return true;
}
static bool lightstyle_source(qa_frontend *frontend,qa_actor_owner owner,qa_game_family family,
    double *seconds,qa_error *error)
{
    if (family==QA_GAME_Q2) {
        bool found;
        if (!frontend_particle_q2_client_time(frontend,owner,seconds,&found,error)) return false;
        if (found) return true;
    }
    qa_application_selected_effects source;
    if (!qa_application_effects_producer_read(frontend->application,owner,&source,error)) return false;
    *seconds=(double)source.source_time_ns/1000000000.0;
    if (family==QA_GAME_Q1 && source.launch->selection.clock.kind!=QA_RULESET_QUAKEWORLD)
        *seconds=(float)*seconds;
    return true;
}
bool frontend_event_world(qa_frontend *frontend, unsigned seat, qa_scene_world_input *world, qa_error *error)
{
    if (!frontend || seat >= frontend->options.seats || !world || frontend->resource_inventory)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Event world is retained by resource preparation");
    frontend_event_state *state = frontend->events;
    if (!state) return frontend_particle_world(frontend,seat,world,error);
    frontend_event_view *view = &state->views[seat];
    if (!q1_fog_initialize(frontend, seat, &view->q1_fog, error)) return false;
    bool q1_fog = view->q1_fog.owner != 0;
    if (q1_fog) {
        qa_clock_state clock; float density; qa_vec3 color;
        if (!qa_session_clock(qa_application_session(frontend->application), view->q1_fog.owner, &clock))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 fog source clock retired");
        q1_fog_sample(&view->q1_fog, clock.frame.time_ns, &density, &color);
        color.x = roundf(fminf(1, fmaxf(0, color.x)) * 255) / 255;
        color.y = roundf(fminf(1, fmaxf(0, color.y)) * 255) / 255;
        color.z = roundf(fminf(1, fmaxf(0, color.z)) * 255) / 255;
        world->fog = (qa_scene_fog){.kind = QA_FOG_EXP2, .effect = QA_FOG_COLOR,
            .density = density, .color = color, .sky_factor = view->q1_fog.sky_factor, .far_depth = 1};
    }
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    qa_actor_owner q1_owner=0,q2_owner=0;
    double q1_seconds=0,q2_seconds=0;
    const qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    for (unsigned i = 0; i < FRONTEND_STYLES; ++i) {
        const char *q1 = qa_strings_cstr(strings, view->q1_patterns[i]);
        const char *q2 = qa_strings_cstr(strings, view->q2_patterns[i]);
        if (q1 && *q1 && view->q1_style_owners[i]!=q1_owner) {
            q1_owner=view->q1_style_owners[i];
            if (!lightstyle_source(frontend,q1_owner,QA_GAME_Q1,&q1_seconds,error)) return false;
        }
        if (q2 && *q2 && view->q2_style_owners[i]!=q2_owner) {
            q2_owner=view->q2_style_owners[i];
            if (!lightstyle_source(frontend,q2_owner,QA_GAME_Q2,&q2_seconds,error)) return false;
        }
        view->q1_styles[i]=frontend_legacy_lightstyle_sample(QA_GAME_Q1,q1,q1_seconds);
        float scale=frontend_legacy_lightstyle_sample(QA_GAME_Q2,q2,q2_seconds);
        view->q2_styles[i]=qa_v3(scale,scale,scale);
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
    if (!count) return frontend_particle_world(frontend,seat,world,error);
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
            .identity = light->identity, .revision = light->revision, .family = QA_GAME_Q2,
            .shadow_resolution = event->resolution};
    }
    world->lights = lights; world->light_count = used;
    return frontend_particle_world(frontend,seat,world,error);
}
bool frontend_event_debug(qa_frontend *frontend, const qa_scene_view *view, qa_error *error)
{
    if (!frontend->events) return true;
    uint64_t now = qa_session_elapsed(qa_application_session(frontend->application));
    for (frontend_retained_bounds *entry = frontend->events->bounds; entry; entry = entry->next) {
        if (!seat_receives(frontend, view->seat, entry->recipient) ||
            (entry->deadline ? now >= entry->deadline : entry->frame != frontend->frame_number)) continue;
        frontend_event_resources *resources; qa_bytes palette;
        if (!resources_read(frontend, entry->owner, QA_GAME_Q1, &resources, error) ||
            !qa_scene_resources_palette(resources->images, QA_GAME_Q1, &palette, error)) return false;
        unsigned color = entry->color * 3;
        qa_vec4 rgba = {palette.data[color] / 255.0f, palette.data[color + 1] / 255.0f, palette.data[color + 2] / 255.0f, 1};
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
        if (!qa_audio_bank_register(resources->sounds, path, QA_GAME_Q2, &asset, error)) {
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
    const qa_cvar_view *enabled = qa_cvars_read(qa_application_cvars(frontend->application),
        frontend->engine_cvars.legacy.cl_footsteps);
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
        bool box = !collision.inline_model && collision.role != QA_COLLISION_TRIGGER &&
            qa_collision_bits_any(collision.contents);
        end.z += box ? body.bounds.mins.z : -66;
        qa_trace_query query = {.start = start, .end = end,
            .shape = {.kind = QA_SHAPE_BOX, .bounds = {qa_v3(box ? body.bounds.mins.x : 0, box ? body.bounds.mins.y : 0, 0),
                qa_v3(box ? body.bounds.maxs.x : 0, box ? body.bounds.maxs.y : 0, 0)}},
            .policy = qa_collision_default_policy(QA_COLLISION_Q2), .pass_actor = event->actor};
        query.policy.contents_mask = qa_collision_contents_mask(1, QA_COLLISION_Q2);
        qa_trace_result hit;
        if (!qa_world_trace(world, &query, &hit, error)) return false;
        if (hit.fraction < 1 && hit.has_surface) {
            memcpy(material, hit.surface.material, sizeof(material)); material[15] = 0;
            query.end = hit.end; query.end.z += 1;
            query.policy.contents_mask = qa_collision_contents_mask(1 | 8 | 16 | 32, QA_COLLISION_Q2);
            if (!qa_world_trace(world, &query, &hit, error)) return false;
            if (hit.has_surface) { memcpy(material, hit.surface.material, sizeof(material)); material[15] = 0; }
        }
    }
    frontend_event_resources *resources; frontend_footsteps *steps;
    if (!resources_read(frontend, event->provider, QA_GAME_Q2, &resources, error) ||
        !footsteps_read(resources, material, &steps, error)) return false;
    if (!steps->count && !footsteps_read(resources, "", &steps, error)) return false;
    if (!steps->count) return true;
    uint32_t index = step_uniform(state, steps->count);
    if (steps->assets[index] == state->last_step) index = (index + 1) % steps->count;
    qa_audio_asset *asset = steps->assets[index];
    uint64_t actor = frontend_audio_actor(frontend, event->actor, error);
    if (actor == QA_AUDIO_NO_ACTOR || !qa_audio_engine_position(frontend->audio, actor, body.origin, error)) return false;
    qa_audio_play sound = {.sample = qa_audio_asset_sample(asset), .asset = asset,
        .family = QA_GAME_Q2, .actor = actor, .owner = event->provider, .audience = QA_AUDIO_WORLD,
        .origin_kind = QA_AUDIO_ACTOR, .origin_actor = actor, .origin = body.origin, .channel = 6,
        .volume = event->code == 2 ? 1 : .5f, .attenuation = event->code == 2 ? 1 : 2};
    if (!audio_play(frontend, state, event->actor, &sound, error)) return false;
    qa_audio_asset_release(state->last_step); state->last_step = qa_audio_asset_retain(asset);
    state->last_step_owner = event->provider; return true;
}
bool frontend_particle_sound(qa_frontend *frontend, const qa_builtin_event *event,
    uint32_t seat, qa_actor_id recipient, qa_error *error)
{
    qa_actor_id current;
    if (!frontend || !event || (event->family != QA_GAME_Q1 && event->family != QA_GAME_Q2) || event->kind != QA_BUILTIN_SOUND ||
        !recipient.registry || seat >= frontend->options.seats ||
        !frontend_seat_actor_read(frontend, seat, &current) || !qa_actor_id_equal(current, recipient))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 particle sound requires its actual delivered physical client");
    if (!frontend->audio || frontend_network_local_input_owned(frontend,seat)) return true;
    const char *name = qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)), event->resource);
    if (!name || !*name) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 particle sound has no source resource name");
    qa_game_family family=event->family==QA_GAME_Q1?QA_GAME_Q1:QA_GAME_Q2;
    frontend_event_state *state; frontend_event_resources *resources;
    if (!state_read(frontend, &state, error) ||
        !resources_read(frontend, event->provider, family, &resources, error)) return false;
    uint64_t actor = QA_AUDIO_NO_ACTOR;
    if (event->actor.registry) {
        qa_error identity = {0};
        actor = frontend_audio_retained_event_actor(frontend, event, &identity);
        if (actor == QA_AUDIO_NO_ACTOR) { if (error) *error = identity; return false; }
    }
    qa_audio_asset *asset = NULL;
    if (!qa_audio_bank_register(resources->sounds, name, family, &asset, error)) return false;
    if (!asset) return true;
    qa_audio_play sound = {.sample = qa_audio_asset_sample(asset), .asset = asset, .name = name,
        .family = family, .actor = actor, .owner = event->provider, .audience = seat,
        .origin_kind = QA_AUDIO_FIXED, .origin_actor = actor, .origin = event->origin,
        .channel = event->channel, .volume = event->volume, .attenuation = event->attenuation};
    int32_t milliseconds = (int32_t)((frontend->time_ns / 1000000) & INT32_MAX);
    bool ok = true;
    if (event->actor.registry && qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), event->actor)) {
        qa_body_state position; qa_error observed = {0};
        if (qa_world_body_read(qa_application_world(frontend->application), event->actor, &position, &observed)) {
            ok = frontend_audio_actor_position(frontend, event->actor, actor, &position, &sound.origin, error);
            if (ok) sound.origin_kind = QA_AUDIO_ACTOR;
        } else if (observed.code != QA_ERROR_NOT_FOUND) {
            if (error) *error = observed;
            ok = false;
        }
    }
    if (ok && (!frontend_seat_actor_read(frontend, seat, &current) || !qa_actor_id_equal(current, recipient)))
        ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 particle sound recipient retired during resource admission");
    if (ok && (state->audio_deferred || state->audio_head))
        ok = audio_projection_append(state, event->actor, recipient, &sound,
            FRONTEND_AUDIO_PLAY, milliseconds, error);
    else if (ok) ok = audio_play_receivers(frontend, &sound, milliseconds, error);
    qa_audio_asset_release(asset); return ok;
}
typedef struct builtin_muzzle_audio {
    qa_frontend *frontend;
    frontend_event_state *state;
    const qa_builtin_event *event;
    const qa_application_protocol_event *message;
    uint32_t seat;
    bool fixed;
} builtin_muzzle_audio;
static bool builtin_muzzle_sound(void *context, const char *name, int32_t channel,
    float volume, float attenuation, double delay, qa_error *error)
{
    builtin_muzzle_audio *audio=context;
    qa_frontend *frontend=audio->frontend;
    const qa_builtin_event *event=audio->event;
    frontend_event_resources *resources;
    if (!resources_read(frontend,event->provider,QA_GAME_Q2,&resources,error)) return false;
    qa_audio_asset *asset=NULL;
    if (!qa_audio_bank_register(resources->sounds,name,QA_GAME_Q2,&asset,error)) return false;
    if (!asset) return true;
    uint64_t actor=audio->message?frontend_audio_q2_protocol_actor(frontend,audio->message,event->actor,error):
        frontend_audio_actor(frontend,event->actor,error);
    if (!audio->message && actor==QA_AUDIO_NO_ACTOR && (!error || !error->code) &&
        !qa_actors_get(qa_world_actors(qa_application_world(frontend->application)),event->actor))
        actor=frontend_audio_retained_event_actor(frontend,event,error);
    bool ok=actor!=QA_AUDIO_NO_ACTOR;
    qa_vec3 origin=event->origin;
    if (ok && !audio->fixed) {
        qa_body_state body;
        ok=qa_world_body_read(qa_application_world(frontend->application),event->actor,&body,error);
        if (ok) {
            origin=body.origin;
            ok=qa_audio_engine_position(frontend->audio,actor,origin,error);
        }
    }
    qa_audio_play sound={.sample=qa_audio_asset_sample(asset),.asset=asset,.name=name,
        .family=QA_GAME_Q2,.actor=actor,.owner=event->provider,.audience=audio->seat,
        .origin_kind=audio->fixed?QA_AUDIO_FIXED:QA_AUDIO_ACTOR,
        .origin_actor=actor,.origin=origin,.channel=channel,
        .volume=volume,.attenuation=attenuation,.delay_seconds=delay};
    if (ok) ok=audio_play(frontend,audio->state,event->actor,&sound,error);
    qa_audio_asset_release(asset); return ok;
}
static bool q2_muzzle_deliver(qa_frontend *frontend, const qa_builtin_event *event,
    const qa_application_protocol_event *message, const qa_application_q2_audience *audience,
    bool monster, qa_q2_edition edition, qa_error *error)
{
    frontend_event_state *state;
    if (!state_read(frontend,&state,error)) return false;
    for (uint32_t seat=0;seat<frontend->options.seats;++seat) {
        if (frontend_network_local_input_owned(frontend,seat)) continue;
        qa_actor_id recipient;
        if (!frontend_seat_actor_read(frontend,seat,&recipient)) continue;
        if (audience && audience->captured) {
            bool delivered=false;
            for (size_t i=0;i<audience->count;++i)
                if (qa_actor_id_equal(recipient,audience->recipients[i].actor)) { delivered=true; break; }
            if (!delivered) continue;
        }
        frontend_source_client_registry client;
        bool found;
        if (!frontend_source_client_registry_read(frontend,seat,&client,&found,error)) return false;
        const qa_cvar_view *effects=found?qa_cvars_read(client.cvars,frontend->engine_cvars.cl_rerelease_effects):NULL;
        bool modern=effects && effects->integer!=0;
        if (found && !frontend_source_client_registry_current(frontend,&client))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 muzzle lost its physical CLIENT controls");
        builtin_muzzle_audio audio={frontend,state,event,message,seat,monster || message!=NULL};
        bool ok=monster?frontend_q2_monster_muzzle_sounds(&state->light_random,(uint32_t)event->code,
            edition==QA_Q2_RERELEASE,builtin_muzzle_sound,&audio,error):
            frontend_q2_player_muzzle_sounds(&state->light_random,(uint32_t)event->code,
                (event->flags&128u)!=0,edition==QA_Q2_RERELEASE,modern,builtin_muzzle_sound,&audio,error);
        if (!ok) return false;
    }
    return true;
}
bool frontend_native_q2_muzzle_sound(qa_frontend *frontend,
    const qa_application_protocol_event *message, const qa_application_q2_audience *audience,
    const qa_builtin_event *event, bool monster, qa_q2_edition edition, qa_error *error)
{
    if (!frontend->audio || frontend_network_client_only(frontend)) return true;
    return q2_muzzle_deliver(frontend,event,message,audience,monster,edition,error);
}
static bool builtin_muzzle(qa_frontend *frontend, const qa_builtin_event *event, qa_error *error)
{
    if (frontend_network_client_only(frontend)) return true;
    const char *resource=event->resource?qa_strings_cstr(
        qa_session_strings(qa_application_session(frontend->application)),event->resource):NULL;
    bool monster=resource && !strcmp(resource,"q2:monster-muzzle");
    if (event->resource && !monster) return true;
    qa_q2_edition edition;
    bool found;
    if (!qa_application_native_q2_source_profile_read(frontend->application,event->provider,
        &edition,&found,error) || !found)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Muzzle lost its emitting Q2 Source profile");
    return q2_muzzle_deliver(frontend,event,NULL,NULL,monster,edition,error);
}
bool frontend_audio_actor_position(qa_frontend *frontend, qa_actor_id actor, uint64_t identity,
    const qa_body_state *body, qa_vec3 *origin, qa_error *error)
{
    qa_world *world = qa_application_world(frontend->application);
    qa_actor_collision collision;
    qa_error observed = {0};
    bool present = qa_world_get_link_collision(world, actor, &collision, &observed);
    if (!present && observed.code) {
        if (error) *error = observed;
        return false;
    }
    *origin = present && collision.inline_model ?
        qa_vec_add(body->origin, qa_vec_scale(qa_vec_add(body->bounds.mins, body->bounds.maxs), .5f)) :
        body->origin;
    if (!qa_audio_engine_position(frontend->audio, identity, *origin, error)) return false;
    if (!present || !collision.inline_model || collision.family != QA_COLLISION_Q2) return true;
    const qa_actor_record *record = qa_actors_get(qa_world_actors(world), actor);
    qa_q2_edition edition;
    bool found;
    if (!qa_application_native_q2_source_profile_read(frontend->application, record->owner,
        &edition, &found, error)) return false;
    if (!found || edition != QA_Q2_RERELEASE) return true;
    qa_vec3 mins = qa_vec_add(body->origin, body->bounds.mins);
    qa_vec3 maxs = qa_vec_add(body->origin, body->bounds.maxs);
    for (uint32_t seat = 0; seat < frontend->options.seats; ++seat) {
        if (frontend_network_local_input_owned(frontend,seat)) continue;
        qa_audio_mixer *mixer = qa_audio_engine_seat_mixer(frontend->audio, seat);
        if (!mixer) continue;
        qa_vec3 listener = qa_audio_mixer_listener_origin(mixer);
        qa_vec3 nearest = {
            fminf(fmaxf(listener.x, mins.x), maxs.x),
            fminf(fmaxf(listener.y, mins.y), maxs.y),
            fminf(fmaxf(listener.z, mins.z), maxs.z)};
        if (!qa_audio_mixer_position(mixer, identity, nearest, error)) return false;
    }
    return true;
}
bool frontend_event_sound(qa_frontend *frontend, const qa_builtin_event *event, qa_error *error)
{
    if (!frontend->audio) return true;
    if (event->family==QA_GAME_Q2 && event->kind==QA_BUILTIN_MUZZLE)
        return builtin_muzzle(frontend,event,error);
    if (event->kind == QA_BUILTIN_EFFECT && event->family == QA_GAME_Q2 &&
        (event->code == 2 || event->code == 8 || event->code == 9)) {
        const char *resource = qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)), event->resource);
        if (resource && !strcmp(resource, "q2:entity-event")) {
            frontend_event_state *state;
            return state_read(frontend, &state, error) && entity_footstep(frontend, state, event, error);
        }
    }
    if (event->kind != QA_BUILTIN_SOUND && event->kind != QA_BUILTIN_STOP_SOUND) return true;
    if (event->kind == QA_BUILTIN_SOUND && (event->flags & 1u) && event->actor.registry &&
        !qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), event->actor))
        return true;
    frontend_event_state *state;
    if (!state_read(frontend, &state, error)) return false;
    uint64_t actor = frontend_audio_actor(frontend, event->actor, error);
    if (event->actor.registry && actor == QA_AUDIO_NO_ACTOR && event->kind == QA_BUILTIN_SOUND &&
        !(event->flags & 1u) && (!error || !error->code) &&
        !qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), event->actor))
        actor = frontend_audio_retained_event_actor(frontend, event, error);
    if (event->actor.registry && actor == QA_AUDIO_NO_ACTOR) return false;
    qa_game_family family = audio_family(event->family);
    if (event->kind == QA_BUILTIN_STOP_SOUND) {
        frontend_retained_sound **link = &state->sounds;
        bool loop_stopped = false;
        while (*link) {
            frontend_retained_sound *entry = *link;
            if (!entry->static_key && entry->loop.sound.actor == actor && entry->loop.sound.owner == event->provider &&
                entry->loop.sound.family == family) {
                if (!qa_audio_engine_stop_loop(frontend->audio, actor, event->provider, entry->loop.sound.audience, error)) return false;
                *link = entry->next; qa_audio_asset_release(entry->loop.sound.asset); qa_pool_release(&state->sound_records, entry->slot);
                loop_stopped = true;
            } else link = &entry->next;
        }
        if (!loop_stopped && !audio_stop_channel(frontend, state, event->actor,
            actor, event->provider, family, event->channel, error)) return false;
        return true;
    }
    qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    const char *name = qa_strings_cstr(strings, event->resource);
    if (!name || !*name) return true;
    uint32_t audience=QA_AUDIO_WORLD;
    bool private_sound=event->family==QA_GAME_Q1 && (event->flags&2)!=0;
    if (private_sound) {
        for (uint32_t seat=0;seat<frontend->options.seats;++seat) {
            qa_actor_id recipient;
            if (frontend_seat_actor_read(frontend,seat,&recipient) &&
                qa_actor_id_equal(recipient,event->actor)) { audience=seat; break; }
        }
        if (audience==QA_AUDIO_WORLD) return true;
    }
    frontend_event_resources *resources;
    if (!resources_read(frontend, event->provider, family, &resources, error)) return false;
    qa_audio_asset *asset = NULL;
    if (family == QA_GAME_Q2 && name[0] == '*') {
        qa_builtin_player_info player = {0};
        (void)qa_application_player_info_read(frontend->application, event->actor, &player);
        if (!qa_audio_bank_sexed(resources->sounds, name, player.skin ? player.skin : "", &asset, error))
            return false;
    } else if (!qa_audio_bank_register(resources->sounds, name, family, &asset, error)) return false;
    if (resources->gear && !resources_current(frontend->application, resources)) {
        qa_audio_asset_release(asset);
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear sound source retired during registration");
    }
    if (!asset) return true;
    bool frame_loop = resources->gear && (event->flags & 1);
    bool live = event->actor.registry && qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), event->actor);
    bool positioned = (event->flags & QA_BUILTIN_SOUND_POSITIONED) != 0;
    qa_audio_play sound = {.sample = qa_audio_asset_sample(asset), .asset = asset, .name = name,
        .family = family, .actor = actor, .owner = event->provider, .audience = audience,
        .origin_kind = private_sound ? QA_AUDIO_LOCAL : (frame_loop || positioned) ? QA_AUDIO_FIXED : live ? QA_AUDIO_ACTOR : QA_AUDIO_FIXED, .origin_actor = actor,
        .origin = private_sound ? (qa_vec3){0} : event->origin, .channel = event->channel,
        .volume = event->volume, .attenuation = private_sound ? 0 : event->attenuation};
    if (live && !private_sound && !frame_loop && !positioned) {
        qa_body_state body; qa_error observed = {0};
        if (qa_world_body_read(qa_application_world(frontend->application), event->actor, &body, &observed)) {
            if (!frontend_audio_actor_position(frontend, event->actor, actor, &body, &sound.origin, error)) {
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
        bool ok = audio_play(frontend, state, event->actor, &sound, error);
        qa_audio_asset_release(asset);
        return ok && (!resources->gear || resources_current(frontend->application, resources) ||
            frontend_fail(error, QA_ERROR_ARGUMENT, "Gear sound source retired during playback"));
    }
    bool ambient = family == QA_GAME_Q1 && !event->actor.registry;
    if ((!ambient && !live) || (ambient && sound.sample->loop_start == QA_AUDIO_NO_LOOP)) {
        qa_audio_asset_release(asset); return true;
    }
    frontend_retained_sound *entry = state->sounds;
    while (entry && (ambient || entry->static_key || entry->loop.sound.actor != actor ||
        entry->loop.sound.owner != event->provider || entry->loop.sound.family != family ||
        entry->loop.sound.audience != audience)) entry = entry->next;
    if (!entry) {
        size_t slot; entry = qa_pool_take(&state->sound_records, &slot);
        if (!entry) { qa_audio_asset_release(asset); return frontend_fail(error, QA_ERROR_MEMORY, "retaining builtin looping sound"); }
        *entry = (frontend_retained_sound){.slot = slot};
        entry->next = state->sounds; state->sounds = entry;
    } else qa_audio_asset_release(entry->loop.sound.asset);
    entry->actor = event->actor;
    entry->loop = (qa_audio_loop){.sound = sound, .persistent = !frame_loop,
        .velocity = frame_loop ? event->direction : (qa_vec3){0},
        .frame_number = frame_loop ? (int32_t)(frontend->frame_number & INT32_MAX) : 0};
    entry->renewed_frame = frame_loop ? frontend->frame_number : 0;
    entry->loop.sound.name = NULL;
    if (ambient) entry->static_key = qa_scene_identity();
    return true;
}
bool frontend_event_audio(qa_frontend *frontend, qa_error *error)
{
    frontend_event_state *state = frontend->events;
    if (!state || !frontend->audio) return true;
    if (!audio_projection_publish(frontend, state, error)) return false;
    frontend_retained_sound **link = &state->sounds;
    while (*link) {
        frontend_retained_sound *entry = *link;
        if (entry->static_key) {
            for (unsigned seat = 0; seat < frontend->options.seats; ++seat) {
                qa_audio_mixer *mixer = qa_audio_engine_seat_mixer(frontend->audio, seat);
                if (!audio_receives(frontend,entry->loop.sound.audience,seat)) {
                    if (mixer && entry->static_mixers[seat] == mixer)
                        qa_audio_mixer_remove_static(mixer,entry->static_key);
                    entry->static_mixers[seat] = NULL;
                    continue;
                }
                if (!mixer) { entry->static_mixers[seat] = NULL; continue; }
                if (entry->static_mixers[seat] == mixer) continue;
                const qa_audio_play *sound = &entry->loop.sound;
                if (!qa_audio_mixer_static(mixer, entry->static_key, sound->sample, sound->origin,
                    truncf(sound->volume * 255), truncf(sound->attenuation * 64), error)) return false;
                entry->static_mixers[seat] = mixer;
            }
        } else {
            if ((!entry->loop.persistent && entry->renewed_frame != frontend->frame_number) ||
                !qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), entry->actor)) {
                if (!qa_audio_engine_stop_loop(frontend->audio, entry->loop.sound.actor,
                    entry->loop.sound.owner, entry->loop.sound.audience, error)) return false;
                *link = entry->next; qa_audio_asset_release(entry->loop.sound.asset); qa_pool_release(&state->sound_records, entry->slot); continue;
            }
            if (entry->loop.persistent) {
                qa_body_state body; qa_error observed = {0};
                if (!qa_world_body_read(qa_application_world(frontend->application), entry->actor, &body, &observed)) {
                    if (observed.code != QA_ERROR_NOT_FOUND) { if (error) *error = observed; return false; }
                } else if (entry->loop.sound.origin_kind!=QA_AUDIO_LOCAL) {
                    entry->loop.velocity = body.velocity;
                    if (!frontend_audio_actor_position(frontend, entry->actor,
                        entry->loop.sound.actor, &body, &entry->loop.sound.origin, error)) return false;
                }
            }
            entry->loop.frame_number = (int32_t)(frontend->frame_number & INT32_MAX);
            for (uint32_t seat = 0; seat < frontend->options.seats; ++seat) {
                if (!audio_receives(frontend,entry->loop.sound.audience,seat)) continue;
                qa_audio_loop delivered = entry->loop;
                delivered.sound.audience = seat;
                if (!qa_audio_engine_loop(frontend->audio,&delivered,error)) return false;
            }
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

typedef struct event_policy_view {
    qa_actor_owner owner;
    qa_string_id name;
    bool received;
    const qa_scene_image *original[6], *prepared[6];
    qa_scene_resource_policy *bank;
} event_policy_view;
struct frontend_event_image_policy {
    qa_frontend *frontend;
    qa_application *application;
    frontend_event_state *state;
    frontend_event_resources *resources;
    event_policy_view views[QA_INPUT_LOCAL_SEATS];
    bool sealed, published;
};
static bool event_policy_current(const frontend_event_image_policy *ticket)
{
    if (!ticket || ticket->frontend->application != ticket->application ||
        ticket->frontend->events != ticket->state || ticket->frontend->stepping) return false;
    if (!ticket->state) return true;
    if (ticket->state->image_policy != ticket || ticket->state->resources != ticket->resources) return false;
    for (unsigned seat = 0; seat < QA_INPUT_LOCAL_SEATS; ++seat) {
        const frontend_event_view *view = ticket->state->views + seat;
        const event_policy_view *saved = ticket->views + seat;
        if (view->sky_owner != saved->owner || view->sky_name != saved->name ||
            view->sky_received != saved->received) return false;
        for (unsigned face = 0; face < 6; ++face)
            if (view->sky[face] != (ticket->published ? saved->prepared[face] : saved->original[face])) return false;
    }
    return true;
}
static void event_policy_dispose(frontend_event_image_policy *ticket)
{
    for (unsigned seat = 0; seat < QA_INPUT_LOCAL_SEATS; ++seat)
        for (unsigned face = 0; face < 6; ++face)
            qa_scene_image_release(ticket->published ? ticket->views[seat].original[face] :
                ticket->views[seat].prepared[face]);
    if (ticket->state) ticket->state->image_policy = NULL;
    free(ticket);
}
bool frontend_event_image_policy_prepare(qa_frontend *f, qa_scene_resource_policy *const *banks,
    size_t count, frontend_event_image_policy **out, qa_error *error)
{
    if (!f || !f->application || !f->resource_inventory || f->stepping || !out || *out ||
        (count && !banks) || (f->events && f->events->image_policy))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Sky image preparation requires its actual retained physical event owner");
    frontend_event_image_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining authored sky image bindings");
    ticket->frontend = f; ticket->application = f->application; ticket->state = f->events;
    ticket->resources = f->events ? f->events->resources : NULL;
    if (ticket->state) ticket->state->image_policy = ticket;
    static const char *const suffixes[6] = {"rt", "lf", "bk", "ft", "up", "dn"};
    for (unsigned seat = 0; ticket->state && seat < QA_INPUT_LOCAL_SEATS; ++seat) {
        const frontend_event_view *view = ticket->state->views + seat;
        event_policy_view *saved = ticket->views + seat;
        saved->owner = view->sky_owner; saved->name = view->sky_name; saved->received = view->sky_received;
        memcpy(saved->original, view->sky, sizeof(saved->original));
        if (!view->sky_received) continue;
        if (!view->sky_name) {
            frontend_fail(error, QA_ERROR_UNSUPPORTED, "Imported sky lacks its original authored request receipt"); goto failed;
        }
        frontend_event_resources *resources = ticket->resources;
        while (resources && (resources->owner != view->sky_owner || resources->family != QA_GAME_Q2)) resources = resources->next;
        if (!resources) goto invalid;
        for (size_t i = 0; i < count; ++i)
            if (qa_scene_resource_policy_source(banks[i]) == resources->images) saved->bank = banks[i];
        qa_scene_resources *destination = qa_scene_resource_policy_destination(saved->bank);
        const char *name = qa_strings_cstr(qa_session_strings(qa_application_session(f->application)), view->sky_name);
        size_t length = strlen(name);
        if (!destination || length > SIZE_MAX - 7) goto invalid;
        char *path = malloc(length + 7);
        if (!path) { frontend_fail(error, QA_ERROR_MEMORY, "Preparing authored sky requests"); goto failed; }
        qa_scene_image_options options = {.family = QA_GAME_Q2, .wrap = QA_SCENE_CLAMP,
            .filter = QA_SCENE_LINEAR, .usage = QA_IMAGE_USAGE_SKY, .transparent_index = -1};
        bool ok = true;
        for (unsigned face = 0; ok && face < 6; ++face) {
            qa_scene_image *image = NULL; qa_error observed = {0};
            snprintf(path, length + 7, "env/%s%s", name, suffixes[face]);
            ok = qa_scene_image_load(destination, path, &options, &image, &observed);
            if (!ok && observed.code == QA_ERROR_NOT_FOUND) {
                saved->prepared[face] = qa_scene_missing(destination);
                qa_scene_image_retain(saved->prepared[face]); ok = true;
            } else {
                saved->prepared[face] = image;
                if (!ok && error) *error = observed;
            }
        }
        free(path); if (!ok) goto failed;
    }
    if (!event_policy_current(ticket)) goto invalid;
    *out = ticket; return true;
invalid:
    frontend_fail(error, QA_ERROR_ARGUMENT, "Authored sky lost its actual source image bank or request");
failed:
    event_policy_dispose(ticket); return false;
}
bool frontend_event_image_policy_ready(frontend_event_image_policy *ticket, qa_error *error)
{
    if (!event_policy_current(ticket) || ticket->published)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Prepared sky images lost their actual physical owner");
    ticket->sealed = true; return true;
}
bool frontend_event_image_policy_ready_is(const frontend_event_image_policy *ticket)
{
    if (!event_policy_current(ticket) || !ticket->sealed || ticket->published) return false;
    for (unsigned seat = 0; seat < QA_INPUT_LOCAL_SEATS; ++seat)
        if (ticket->views[seat].bank && !qa_scene_resource_policy_ready_is(ticket->views[seat].bank)) return false;
    return true;
}
void frontend_event_image_policy_publish(frontend_event_image_policy *ticket)
{
    if (!event_policy_current(ticket) || !ticket->sealed || ticket->published) return;
    for (unsigned seat = 0; ticket->state && seat < QA_INPUT_LOCAL_SEATS; ++seat)
        memcpy(ticket->state->views[seat].sky, ticket->views[seat].prepared, sizeof(ticket->views[seat].prepared));
    ticket->published = true;
}
static bool event_policy_end(frontend_event_image_policy **owner, bool published, qa_error *error)
{
    if (!owner || !*owner) return true;
    if (!event_policy_current(*owner) || (*owner)->published != published)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Sky image cleanup retains its nonterminal actual owner");
    event_policy_dispose(*owner); *owner = NULL; return true;
}
bool frontend_event_image_policy_finish(frontend_event_image_policy **owner, qa_error *error)
{ return event_policy_end(owner, true, error); }
bool frontend_event_image_policy_abort(frontend_event_image_policy **owner, qa_error *error)
{ return event_policy_end(owner, false, error); }
