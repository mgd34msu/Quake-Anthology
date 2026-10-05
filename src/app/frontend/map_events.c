#include "internal.h"
#include "save_private.h"
#include "audio_inventory.h"
#include "event_restore.h"
#include "scene_identity.h"
#include "qc_rerelease_events.h"
#include "round.h"
#include "qa/persistence_content.h"
#include "qa/scene_resource_save.h"
#include "qa/application_equipment_content.h"
#include "particle_audio.h"
#include "particle_delivery.h"
#include "resource_bindings.h"
#include "shared_resource_policy.h"
#include "q1_sky.h"
#include "qa/text.h"
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
    bool gear;
    qa_actor_owner selected_owner;
    qa_string_id service_owner;
    qa_launch_instance_lease *descriptor;
    qa_resource *artifact;
} frontend_event_resources;
typedef struct frontend_retained_sound {
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
    struct frontend_audio_projection *next;
    frontend_audio_projection_kind kind;
    qa_actor_id actor;
    qa_actor_id recipient; /* Delivery identity; FIXED sounds have no emitter actor. */
    qa_audio_play sound;
    char *name;
    int32_t milliseconds;
} frontend_audio_projection;
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
typedef struct frontend_q1_fog {
    qa_actor_owner owner;
    qa_actor_id recipient;
    qa_vec3 start_color, target_color;
    float start_density, target_density, sky_factor;
    uint64_t time;
    double duration;
} frontend_q1_fog;
typedef struct frontend_event_view {
    char *q1_patterns[FRONTEND_STYLES], *q2_patterns[FRONTEND_STYLES];
    float q1_styles[FRONTEND_STYLES];
    qa_vec3 q2_styles[FRONTEND_STYLES];
    qa_q2_fog fog_start, fog_target;
    uint64_t fog_time;
    double fog_duration;
    bool fog_received, sky_received;
    frontend_q1_fog q1_fog;
    const qa_scene_image *sky[6];
    char *sky_name;
    qa_actor_owner sky_owner;
    qa_vec3 sky_axis;
    float sky_rotation;
    bool sky_auto;
} frontend_event_view;
struct frontend_event_state {
    frontend_event_resources *resources;
    frontend_event_image_policy *image_policy;
    frontend_retained_sound *sounds;
    frontend_retained_light *lights;
    frontend_retained_bounds *bounds;
    frontend_q1_light q1_lights[32];
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
    qa_audio_family family;
    uint64_t view;
    bool sounds;
    bool gear;
} event_owner_plan;
static bool gear_resources_bind(qa_application *app, frontend_event_resources *entry, qa_error *error)
{
    qa_application_equipment_content source;
    if (!entry->gear || entry->family != QA_AUDIO_Q3 ||
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
    return entry->family == QA_AUDIO_Q3 && held &&
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
        if (entry->owner != provider || entry->family != QA_AUDIO_Q1) continue;
        if (entry->gear || !entry->images || !entry->files ||
            qa_scene_resources_files(entry->images) != entry->files ||
            !resources_current(frontend->application, entry))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 event image owner is no longer physically bound");
        *images = entry->images; *files = entry->files; return true;
    }
    return frontend_fail(error, QA_ERROR_NOT_FOUND, "Q1 provider has no admitted event image bank");
}
static bool event_topology_fields(qa_source_save_io *io, qa_application *application,
    bool *present, event_owner_plan **plans, size_t *count)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','F','E','T'}; if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFET", 4) ||
        !qa_source_save_bool(io, present) ||
        !qa_source_save_count(io, count, reading ? io->input.size / 22 : SIZE_MAX / sizeof(**plans)) ||
        (!*present && *count)) return false;
    if (reading && *count) {
        *plans = calloc(*count, sizeof(**plans));
        if (!*plans) return frontend_fail(io->error, QA_ERROR_MEMORY, "Retaining actual event owner topology");
    }
    qa_strings *strings = qa_session_strings(qa_application_session(application));
    for (size_t i = 0; i < *count; ++i) {
        event_owner_plan *plan = &(*plans)[i]; uint32_t family = plan->family;
        char *key = !reading && plan->owner <= UINT32_MAX ? (char *)qa_strings_cstr(strings, (qa_string_id)plan->owner) : NULL;
        if (!reading && (!key || !plan->owner)) return false;
        bool ok = qa_source_save_owned_text(io, &key);
        if (reading) {
            plan->owner = key ? qa_strings_find(strings, (qa_bytes){(const uint8_t *)key, strlen(key)}) : 0;
            free(key);
        }
        if (!ok || !plan->owner || !qa_source_save_u32(io, &family) || family > QA_AUDIO_Q3 ||
            !qa_source_save_u64(io, &plan->view) || !plan->view || !qa_source_save_bool(io, &plan->sounds) ||
            !qa_source_save_bool(io, &plan->gear) || (plan->gear && family != QA_AUDIO_Q3)) return false;
        plan->family = (qa_audio_family)family;
        for (size_t j = 0; j < i; ++j)
            if (plan->view == (*plans)[j].view ||
                (plan->owner == (*plans)[j].owner && plan->family == (*plans)[j].family)) return false;
    }
    return true;
}
bool frontend_event_topology_checkpoint(const qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->resource_inventory ||
        !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Event topology capture requires actual idle owners and empty output");
    qa_application_content_graph *graph = qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "Event topology requires the actual content graph lease");
    size_t count = frontend_event_audio_owner_count(frontend);
    event_owner_plan *plans = count ? calloc(count, sizeof(*plans)) : NULL;
    if (count && !plans) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating actual event owner metadata");
    const frontend_event_resources *entry = frontend->events ? frontend->events->resources : NULL; bool ok = true;
    for (size_t i = 0; ok && i < count; ++i, entry = entry->next) {
        uint64_t view = qa_application_content_view_id(graph, entry->files);
        ok = view && entry->images && qa_scene_resources_files(entry->images) == entry->files &&
            (entry->sounds != NULL) == (frontend->audio != NULL) &&
            (!entry->sounds || qa_audio_bank_files(entry->sounds) == entry->files) &&
            resources_current(frontend->application, entry);
        plans[i] = (event_owner_plan){entry->owner, entry->family, view, entry->sounds != NULL, entry->gear};
    }
    qa_source_save_io io = {0}; bool present = frontend->events != NULL;
    ok = ok && qa_source_save_writer(&io, qa_application_session(frontend->application), error) &&
        event_topology_fields(&io, frontend->application, &present, &plans, &count) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); free(plans);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Event resource topology is not source-qualified");
    return ok;
}
bool frontend_event_prepare_restored(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->events)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Event topology prepare requires an empty detached owner");
    qa_source_save_io io = {0}; bool present = false; event_owner_plan *plans = NULL; size_t count = 0;
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        event_topology_fields(&io, frontend->application, &present, &plans, &count) && qa_source_save_finish(&io, NULL);
    qa_application_content_graph *graph = qa_application_content_graph_read(frontend->application);
    if (ok && !graph) ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Event restoration requires the actual saved content graph");
    for (size_t i = 0; ok && i < count; ++i)
        ok = qa_application_content_view(graph, plans[i].view) && plans[i].sounds == (frontend->audio != NULL);
    if (ok && present) {
        frontend->events = calloc(1, sizeof(*frontend->events));
        if (!frontend->events) ok = frontend_fail(error, QA_ERROR_MEMORY, "Creating detached event continuation owner");
    }
    frontend_event_resources **link = ok && frontend->events ? &frontend->events->resources : NULL;
    for (size_t i = 0; ok && i < count; ++i) {
        *link = calloc(1, sizeof(**link));
        if (!*link) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Creating actual event resource owner"); break; }
        frontend_event_resources *entry = *link;
        entry->owner = plans[i].owner; entry->family = plans[i].family; entry->gear = plans[i].gear;
        ok = qa_application_content_claim_view(graph, plans[i].view, &entry->files, error);
        if (ok) { entry->images = qa_scene_resources_create_detached(entry->files, error); ok = entry->images != NULL; }
        if (ok && plans[i].sounds) ok = qa_audio_bank_create(entry->files, &entry->sounds, error);
        link = &entry->next;
    }
    qa_source_save_dispose(&io); free(plans);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Saved event topology is not admitted by the candidate");
    return ok;
}
bool frontend_event_topology_ready(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Event topology qualification requires idle actual providers");
    for (frontend_event_resources *entry = frontend->events ? frontend->events->resources : NULL; entry; entry = entry->next)
        if (!entry->files || !entry->images || qa_scene_resources_files(entry->images) != entry->files ||
            (entry->sounds != NULL) != (frontend->audio != NULL) ||
            (entry->sounds && qa_audio_bank_files(entry->sounds) != entry->files) ||
            (entry->gear ? !gear_resources_bind(frontend->application, entry, error) :
                !qa_application_provider_instance(frontend->application, entry->owner)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Actual event resource/provider owners are not completely bound");
    return true;
}
size_t frontend_event_audio_owner_count(const qa_frontend *frontend)
{
    size_t count = 0;
    if (frontend && frontend->events)
        for (const frontend_event_resources *entry = frontend->events->resources; entry; entry = entry->next) ++count;
    return count;
}
bool frontend_event_audio_owner_read(const qa_frontend *frontend, size_t index,
    frontend_event_audio_owner_view *out)
{
    if (!frontend || !frontend->application || frontend->stepping || !out) return false;
    const frontend_event_resources *entry = frontend->events ? frontend->events->resources : NULL;
    while (entry && index--) entry = entry->next;
    if (!entry || !entry->files || !entry->images) return false;
    *out = (frontend_event_audio_owner_view){entry->owner, entry->family, entry->files, entry->sounds};
    return true;
}
bool frontend_event_audio_view(const qa_frontend *frontend,const qa_audio_asset *asset,qa_vfs **out)
{
    if (!frontend || !asset || !out) return false;
    qa_resource *resource=qa_audio_asset_resource(asset);
    if (!resource) return false;
    for (const frontend_event_resources *entry=frontend->events?frontend->events->resources:NULL;entry;entry=entry->next)
        if (entry->sounds && qa_audio_bank_get(entry->sounds,qa_resource_id(resource),qa_audio_asset_family(asset))==asset) {
            *out=entry->files; return *out!=NULL;
        }
    return false;
}
static bool append_asset(qa_audio_asset ***assets, size_t *count, qa_audio_asset *asset, qa_error *error)
{
    if (!asset) return true;
    if (*count == SIZE_MAX / sizeof(**assets)) return frontend_fail(error, QA_ERROR_MEMORY, "Event sound inventory overflows");
    qa_audio_asset **grown = realloc(*assets, (*count + 1) * sizeof(*grown));
    if (!grown) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating borrowed event sound inventory");
    *assets = grown; grown[(*count)++] = asset; return true;
}
bool frontend_event_audio_assets_read(const qa_frontend *frontend, qa_audio_asset ***out,
    size_t *out_count, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !out || *out || !out_count)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Event sound inventory requires an idle frontend and empty output");
    qa_audio_asset **assets = NULL; size_t count = 0; bool ok = true;
    const frontend_event_state *state = frontend->events;
    for (const frontend_event_resources *entry = state ? state->resources : NULL; ok && entry; entry = entry->next)
        for (const frontend_footsteps *steps = entry->footsteps; ok && steps; steps = steps->next) {
            if (steps->count > 16) { ok = frontend_fail(error, QA_ERROR_FORMAT, "Footstep holder inventory exceeds its actual slots"); break; }
            for (uint32_t i = 0; ok && i < steps->count; ++i) ok = append_asset(&assets, &count, steps->assets[i], error);
        }
    for (const frontend_retained_sound *sound = state ? state->sounds : NULL; ok && sound; sound = sound->next)
        ok = append_asset(&assets, &count, sound->loop.sound.asset, error);
    for (const frontend_audio_projection *sound = state ? state->audio_head : NULL; ok && sound; sound = sound->next)
        ok = append_asset(&assets, &count, sound->sound.asset, error);
    if (ok && state) ok = append_asset(&assets, &count, state->last_step, error);
    if (!ok) { free(assets); return false; }
    *out = assets; *out_count = count; return true;
}
static qa_audio_family audio_family(qa_game_family family)
{
    return family == QA_GAME_Q3 ? QA_AUDIO_Q3 : family == QA_GAME_Q2 ? QA_AUDIO_Q2 : QA_AUDIO_Q1;
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
static void audio_projection_free(frontend_audio_projection *entry)
{
    qa_audio_asset_release(entry->sound.asset); free(entry->name); free(entry);
}
static void audio_projection_clear(frontend_event_state *state)
{
    while (state->audio_head) {
        frontend_audio_projection *entry = state->audio_head;
        state->audio_head = entry->next; audio_projection_free(entry);
    }
    state->audio_tail = NULL;
}
static bool audio_projection_append(frontend_event_state *state, qa_actor_id actor, qa_actor_id recipient,
    const qa_audio_play *sound, frontend_audio_projection_kind kind,
    int32_t milliseconds, qa_error *error)
{
    frontend_audio_projection *entry = calloc(1, sizeof(*entry));
    if (!entry) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining round sound projection");
    entry->kind = kind; entry->actor = actor; entry->sound = *sound;
    entry->recipient = recipient;
    entry->milliseconds = milliseconds;
    entry->sound.asset = qa_audio_asset_retain(sound->asset);
    if (sound->asset && !entry->sound.asset) {
        audio_projection_free(entry); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining projected sound asset");
    }
    if (sound->name) {
        size_t size = strlen(sound->name) + 1;
        entry->name = malloc(size);
        if (!entry->name) {
            audio_projection_free(entry); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining projected sound name");
        }
        memcpy(entry->name, sound->name, size);
    }
    entry->sound.name = entry->name;
    if (state->audio_tail) state->audio_tail->next = entry;
    else state->audio_head = entry;
    state->audio_tail = entry; return true;
}
static bool audio_play(qa_frontend *frontend, frontend_event_state *state,
    qa_actor_id actor, const qa_audio_play *sound, qa_error *error)
{
    int32_t milliseconds = (int32_t)((frontend->time_ns / 1000000) & INT32_MAX);
    if (state->audio_deferred || state->audio_head)
        return audio_projection_append(state, actor, (qa_actor_id){0}, sound, FRONTEND_AUDIO_PLAY, milliseconds, error);
    return qa_audio_engine_play(frontend->audio, sound, milliseconds, error);
}
static bool audio_stop_channel(qa_frontend *frontend, frontend_event_state *state,
    qa_actor_id actor, uint64_t identity, qa_actor_owner owner,
    qa_audio_family family, int32_t channel, qa_error *error)
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
            ok = qa_audio_engine_play(frontend->audio, &entry->sound, entry->milliseconds, error);
        else if (entry->kind == FRONTEND_AUDIO_STOP_CHANNEL) qa_audio_engine_stop_channel(frontend->audio, entry->sound.actor,
            entry->sound.owner, entry->sound.family, entry->sound.channel);
        audio_projection_free(entry);
        if (!ok) return false;
    }
    return true;
}
static bool resources_read(qa_frontend *frontend, qa_actor_owner provider, qa_audio_family family,
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
    qa_command_context context = {.owner = provider, .origin = QA_COMMAND_SERVER,
        .dialect = family == QA_AUDIO_Q3 ? QA_CONSOLE_Q3 : family == QA_AUDIO_Q2 ? QA_CONSOLE_Q2 : QA_CONSOLE_Q1};
    qa_command_context captured;
    qa_application_equipment_content source;
    bool gear = !qa_application_provider_instance(frontend->application, provider) &&
        qa_application_equipment_content_read(frontend->application, provider, &source, NULL);
    if (gear && family != QA_AUDIO_Q3)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear events require their actual Q3 sound family");
    if (!gear && !qa_application_capture_command_context(frontend->application, &context, &captured, error)) return false;
    const qa_vfs *files = gear ? source.files : qa_application_context_files(frontend->application, &captured, NULL);
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
    if (!resources_read(frontend,owner,QA_AUDIO_Q1,&resources,error)) return false;
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
        qa_audio_asset_release(entry->loop.sound.asset); free(entry);
    }
    while (state->lights) {
        frontend_retained_light *entry = state->lights; state->lights = entry->next; free(entry);
    }
    while (state->bounds) {
        frontend_retained_bounds *entry = state->bounds; state->bounds = entry->next; free(entry);
    }
    memset(state->q1_lights, 0, sizeof(state->q1_lights));
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
            if (entry->family == QA_AUDIO_Q1 &&
                !frontend_q1_sky_retire_provider(frontend->q1_sky, entry->owner, error)) return false;
    audio_projection_clear(state);
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
        free(state->views[i].sky_name);
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
        qa_vfs_destroy(entry->files); qa_resource_release(entry->artifact);
        qa_launch_instance_lease_release(entry->descriptor); free(entry);
    }
    qa_audio_asset_release(state->last_step); free(state); frontend->events = NULL;
    return true;
}
void frontend_event_retire(qa_frontend *frontend)
{
    (void)frontend_event_retire_checked(frontend, NULL);
}
static bool seat_receives(qa_frontend *frontend, unsigned seat, qa_actor_id recipient)
{
    qa_actor_id actor;
    return !recipient.registry || (frontend_seat_actor_read(frontend,seat,&actor) &&
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
            if (error) *error = observed;
            return false;
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
        if (!q1_fog_initialize(frontend, seat, &state->views[seat].q1_fog, error)) return false;
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
        bool handled;
        if (!frontend_qc_rerelease_event(frontend,&event,&handled,error)) return false;
        if (handled) continue;
        if (event.family == QA_GAME_Q1 && event.kind == QA_BUILTIN_EFFECT) {
            const char *resource = qa_strings_cstr(strings, event.resource);
            if (resource && !strcmp(resource, "q1:fog")) {
                if (!qa_vec_finite(event.origin) || !isfinite(event.value) || !isfinite(event.end.x) || event.end.x < 0)
                    return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid authored Q1 fog");
                qa_clock_state clock;
                if (!qa_session_clock(qa_application_session(frontend->application), event.provider, &clock) ||
                    event.time_ns > clock.frame.time_ns)
                    return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 fog has no matching source clock");
                for (unsigned seat = 0; seat < frontend->options.seats; ++seat) {
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
                if (!pattern_set(&view->sky_name, qa_strings_cstr(strings, event->resource), error)) {
                    for (unsigned face = 0; face < 6; ++face) qa_scene_image_release(sky[face]);
                    return false;
                }
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
    if (!count) return frontend_particle_world(frontend,seat,world,error);
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
    if (!audio_play(frontend, state, event->actor, &sound, error)) return false;
    qa_audio_asset_release(state->last_step); state->last_step = qa_audio_asset_retain(asset);
    state->last_step_owner = event->provider; return true;
}
bool frontend_particle_sound(qa_frontend *frontend, const qa_builtin_event *event,
    uint32_t seat, qa_actor_id recipient, qa_error *error)
{
    qa_actor_id current;
    if (!frontend || !event || event->family != QA_GAME_Q2 || event->kind != QA_BUILTIN_SOUND ||
        event->actor.registry || !recipient.registry || seat >= frontend->options.seats ||
        !frontend_seat_actor_read(frontend, seat, &current) || !qa_actor_id_equal(current, recipient))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 particle sound requires its actual delivered physical client");
    if (!frontend->audio) return true;
    const char *name = qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)), event->resource);
    if (!name || !*name) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 particle sound has no source resource name");
    frontend_event_state *state; frontend_event_resources *resources;
    if (!state_read(frontend, &state, error) ||
        !resources_read(frontend, event->provider, QA_AUDIO_Q2, &resources, error)) return false;
    qa_audio_asset *asset = NULL;
    if (!qa_audio_bank_register(resources->sounds, name, QA_AUDIO_Q2, &asset, error)) return false;
    if (!asset) return true;
    qa_audio_play sound = {.sample = qa_audio_asset_sample(asset), .asset = asset, .name = name,
        .family = QA_AUDIO_Q2, .actor = QA_AUDIO_NO_ACTOR, .owner = event->provider, .audience = seat,
        .origin_kind = QA_AUDIO_FIXED, .origin_actor = QA_AUDIO_NO_ACTOR, .origin = event->origin,
        .channel = event->channel, .volume = event->volume, .attenuation = event->attenuation};
    int32_t milliseconds = (int32_t)((frontend->time_ns / 1000000) & INT32_MAX);
    bool ok = frontend_seat_actor_read(frontend, seat, &current) && qa_actor_id_equal(current, recipient);
    if (!ok) frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 particle sound recipient retired during resource admission");
    else if (state->audio_deferred || state->audio_head)
        ok = audio_projection_append(state, (qa_actor_id){0}, recipient, &sound,
            FRONTEND_AUDIO_PLAY, milliseconds, error);
    else ok = qa_audio_engine_play(frontend->audio, &sound, milliseconds, error);
    qa_audio_asset_release(asset); return ok;
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
    if (event->actor.registry && actor == QA_AUDIO_NO_ACTOR && event->kind == QA_BUILTIN_SOUND &&
        event->family == QA_GAME_Q2 && !(event->flags & 1u) &&
        !qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), event->actor))
        actor = frontend_audio_retained_q2_actor(frontend, event, error);
    if (event->actor.registry && actor == QA_AUDIO_NO_ACTOR) return false;
    qa_audio_family family = audio_family(event->family);
    if (event->kind == QA_BUILTIN_STOP_SOUND) {
        frontend_retained_sound **link = &state->sounds;
        bool loop_stopped = false;
        while (*link) {
            frontend_retained_sound *entry = *link;
            if (!entry->static_key && entry->loop.sound.actor == actor && entry->loop.sound.owner == event->provider &&
                entry->loop.sound.family == family) {
                if (!qa_audio_engine_stop_loop(frontend->audio, actor, event->provider, entry->loop.sound.audience, error)) return false;
                *link = entry->next; qa_audio_asset_release(entry->loop.sound.asset); free(entry);
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
    if (!qa_audio_bank_register(resources->sounds, name, family, &asset, error)) return false;
    if (resources->gear && !resources_current(frontend->application, resources)) {
        qa_audio_asset_release(asset);
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear sound source retired during registration");
    }
    if (!asset) return true;
    bool frame_loop = resources->gear && (event->flags & 1);
    bool live = event->actor.registry && qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), event->actor);
    qa_audio_play sound = {.sample = qa_audio_asset_sample(asset), .asset = asset, .name = name,
        .family = family, .actor = actor, .owner = event->provider, .audience = audience,
        .origin_kind = private_sound ? QA_AUDIO_LOCAL : frame_loop ? QA_AUDIO_FIXED : live ? QA_AUDIO_ACTOR : QA_AUDIO_FIXED, .origin_actor = actor,
        .origin = private_sound ? (qa_vec3){0} : event->origin, .channel = event->channel,
        .volume = event->volume, .attenuation = private_sound ? 0 : event->attenuation};
    if (live && !private_sound && !frame_loop) {
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
        bool ok = audio_play(frontend, state, event->actor, &sound, error);
        qa_audio_asset_release(asset);
        return ok && (!resources->gear || resources_current(frontend->application, resources) ||
            frontend_fail(error, QA_ERROR_ARGUMENT, "Gear sound source retired during playback"));
    }
    bool ambient = family == QA_AUDIO_Q1 && !event->actor.registry;
    if ((!ambient && !live) || (ambient && sound.sample->loop_start == QA_AUDIO_NO_LOOP)) {
        qa_audio_asset_release(asset); return true;
    }
    frontend_retained_sound *entry = state->sounds;
    while (entry && (ambient || entry->static_key || entry->loop.sound.actor != actor ||
        entry->loop.sound.owner != event->provider || entry->loop.sound.family != family ||
        entry->loop.sound.audience != audience)) entry = entry->next;
    if (!entry) {
        entry = calloc(1, sizeof(*entry));
        if (!entry) { qa_audio_asset_release(asset); return frontend_fail(error, QA_ERROR_MEMORY, "retaining builtin looping sound"); }
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
                *link = entry->next; qa_audio_asset_release(entry->loop.sound.asset); free(entry); continue;
            }
            if (entry->loop.persistent) {
                qa_body_state body; qa_error observed = {0};
                if (!qa_world_body_read(qa_application_world(frontend->application), entry->actor, &body, &observed)) {
                    if (observed.code != QA_ERROR_NOT_FOUND) { if (error) *error = observed; return false; }
                } else if (entry->loop.sound.origin_kind!=QA_AUDIO_LOCAL) {
                    entry->loop.velocity = body.velocity; entry->loop.sound.origin = body.origin;
                    if (!qa_audio_engine_position(frontend->audio, entry->loop.sound.actor, body.origin, error)) return false;
                }
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
    uint8_t magic[4] = {'Q','A','P','E'};
    return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QAPE", 4);
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
static bool retained_asset(qa_source_save_io *io, qa_frontend *frontend, const qa_audio_asset_inventory *inventory, qa_actor_owner *owner,
    qa_audio_family *family, qa_audio_asset **asset)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t index = 0;
    char *name = reading ? NULL : (char *)qa_audio_asset_name(*asset);
    qa_sha256_digest digest = {{0}};
    if (!reading) {
        const qa_sha256_digest *actual = qa_resource_digest(qa_audio_asset_resource(*asset));
        if (!actual || !name || qa_audio_asset_family(*asset) != *family ||
            !qa_audio_asset_inventory_index(inventory, *asset, &index)) return false;
        digest = *actual;
    }
    bool ok = frontend_save_sound_owner(io, frontend->application, owner, family) && *owner &&
        qa_source_save_owned_text(io, &name) && name &&
        qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes)) && qa_source_save_u64(io, &index);
    qa_audio_asset *qualified = reading ? qa_audio_asset_inventory_at(inventory, index) : *asset;
    const frontend_event_resources *resources = frontend->events ? frontend->events->resources : NULL;
    while (resources && (resources->owner != *owner || resources->family != *family)) resources = resources->next;
    if (ok) ok = resources && resources->sounds && qa_audio_bank_files(resources->sounds) == resources->files &&
        resources_current(frontend->application, resources) && qualified &&
        qa_audio_bank_get(resources->sounds, qa_resource_id(qa_audio_asset_resource(qualified)), *family) == qualified;
    if (ok && reading) {
        const qa_sha256_digest *actual = qa_resource_digest(qa_audio_asset_resource(qualified));
        const char *actual_name = qa_audio_asset_name(qualified);
        ok = frontend->audio && qualified && actual && actual_name &&
            qa_audio_asset_family(qualified) == *family &&
            !strcmp(actual_name, name) && !memcmp(actual->bytes, digest.bytes, sizeof(digest.bytes));
        if (ok) {
            *asset = qa_audio_asset_retain(qualified);
            ok = *asset != NULL;
        }
    }
    if (reading) free(name);
    return ok;
}
static bool audio_owner_fields(qa_source_save_io *io, qa_application *application,
    uint64_t *owner, qa_audio_family *family)
{
    if (io->direction == QA_SOURCE_SAVE_WRITE && *owner > UINT32_MAX)
        return frontend_fail(io->error, QA_ERROR_ARGUMENT, "Audio owner has no actor-owner namespace identity");
    qa_actor_owner provider = io->direction == QA_SOURCE_SAVE_READ ? 0 : (qa_actor_owner)*owner;
    if (!frontend_save_sound_owner(io, application, &provider, family)) return false;
    *owner = provider;
    return true;
}
static bool retained_audio_asset(qa_source_save_io *io, qa_frontend *frontend,
    const qa_audio_asset_inventory *inventory, uint64_t *owner,
    qa_audio_family *family, qa_audio_asset **asset)
{
    if (io->direction == QA_SOURCE_SAVE_WRITE && *owner > UINT32_MAX)
        return frontend_fail(io->error, QA_ERROR_ARGUMENT, "Audio asset owner has no actor-owner namespace identity");
    qa_actor_owner provider = io->direction == QA_SOURCE_SAVE_READ ? 0 : (qa_actor_owner)*owner;
    if (!retained_asset(io, frontend, inventory, &provider, family, asset)) return false;
    *owner = provider;
    return true;
}
static bool audio_identity_matches(const qa_frontend *frontend, qa_actor_id actor, uint64_t identity)
{
    if (!actor.registry) return identity == QA_AUDIO_NO_ACTOR;
    for (size_t i = 0; i < frontend->audio_id_count; ++i)
        if (qa_actor_id_equal(frontend->audio_ids[i].actor, actor) && frontend->audio_ids[i].id == identity)
            return true;
    return false;
}
static bool audio_audience_matches(const qa_frontend *frontend, qa_actor_id actor,
    const qa_audio_play *sound, uint32_t origin_kind, uint32_t audience)
{
    if (audience == QA_AUDIO_WORLD)
        return origin_kind == QA_AUDIO_FIXED || (origin_kind == QA_AUDIO_ACTOR && actor.registry);
    qa_actor_id recipient;
    return audience < frontend->options.seats && sound->family == QA_AUDIO_Q1 && origin_kind == QA_AUDIO_LOCAL &&
        actor.registry && frontend_seat_actor_read(frontend,audience,&recipient) &&
        qa_actor_id_equal(actor, recipient) && sound->attenuation == 0 &&
        sound->origin.x == 0 && sound->origin.y == 0 && sound->origin.z == 0;
}
static bool audio_projection_fields(qa_source_save_io *io, qa_frontend *frontend, const qa_audio_asset_inventory *inventory,
    frontend_audio_projection *entry)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t kind = entry->kind; qa_audio_play *sound = &entry->sound;
    if (!qa_source_save_u32(io, &kind) || kind > FRONTEND_AUDIO_STOP_CHANNEL ||
        !qa_source_save_actor(io, &entry->actor) || !qa_source_save_actor(io, &entry->recipient) ||
        !qa_source_save_u64(io, &sound->actor) ||
        !audio_identity_matches(frontend, entry->actor, sound->actor)) return false;
    entry->kind = (frontend_audio_projection_kind)kind;
    if (kind == FRONTEND_AUDIO_STOP_CHANNEL) {
        if (entry->recipient.registry) return false;
        if (!audio_owner_fields(io, frontend->application, &sound->owner, &sound->family) || !sound->owner ||
            !qa_source_save_i32(io, &sound->channel)) return false;
        return true;
    }
    uint32_t origin_kind = sound->origin_kind, audience = sound->audience;
    if (!retained_audio_asset(io, frontend, inventory, &sound->owner, &sound->family, &sound->asset) ||
        !qa_source_save_owned_text(io, &entry->name) || !qa_source_save_i32(io, &entry->milliseconds) ||
        !qa_source_save_u32(io, &origin_kind) || !qa_source_save_u32(io, &audience) ||
        !qa_source_save_vec3(io, &sound->origin) || !qa_vec_finite(sound->origin) ||
        !qa_source_save_i32(io, &sound->channel) || !qa_source_save_f32(io, &sound->volume) ||
        !isfinite(sound->volume) || sound->volume < 0 || sound->volume > 1 ||
        !qa_source_save_f32(io, &sound->attenuation) || !isfinite(sound->attenuation) || sound->attenuation < 0 ||
        !qa_source_save_u64(io, &sound->resource_id) ||
        !qa_source_save_f64(io, &sound->delay_seconds) || !isfinite(sound->delay_seconds) ||
        !qa_source_save_f64(io, &sound->server_milliseconds) || !isfinite(sound->server_milliseconds) ||
        !qa_source_save_bool(io, &sound->has_server_time)) return false;
    if (entry->recipient.registry) {
        /* A saved emission can outlive its client. The typed historical actor
         * is remapped without replay; publication discards a changed seat. */
        if (sound->family != QA_AUDIO_Q2 || origin_kind != QA_AUDIO_FIXED ||
            audience >= frontend->options.seats || entry->actor.registry || sound->actor != QA_AUDIO_NO_ACTOR)
            return false;
    } else if (!audio_audience_matches(frontend, entry->actor, sound, origin_kind, audience)) return false;
    if (!reading && (sound->origin_actor != sound->actor ||
        sound->sample != qa_audio_asset_sample(sound->asset) || sound->name != entry->name)) return false;
    if (reading) {
        sound->sample = qa_audio_asset_sample(sound->asset); sound->name = entry->name;
        sound->origin_kind = (qa_audio_origin_kind)origin_kind;
        sound->origin_actor = sound->actor; sound->audience = audience;
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
static bool q1_fog_fields(qa_source_save_io *io, qa_frontend *frontend, unsigned seat, frontend_q1_fog *fog)
{
    if (!frontend_save_provider(io, frontend->application, &fog->owner) ||
        !qa_source_save_actor(io, &fog->recipient) ||
        !qa_source_save_vec3(io, &fog->start_color) || !qa_vec_finite(fog->start_color) ||
        !qa_source_save_vec3(io, &fog->target_color) || !qa_vec_finite(fog->target_color) ||
        !qa_source_save_f32(io, &fog->start_density) || !isfinite(fog->start_density) ||
        !qa_source_save_f32(io, &fog->target_density) || !isfinite(fog->target_density) ||
        !qa_source_save_f32(io, &fog->sky_factor) || !isfinite(fog->sky_factor) || fog->sky_factor < 0 || fog->sky_factor > 1 ||
        !qa_source_save_u64(io, &fog->time) ||
        !qa_source_save_f64(io, &fog->duration) || !isfinite(fog->duration) || fog->duration < 0) return false;
    if (!fog->owner)
        return !fog->recipient.registry && !fog->time && fog->duration == 0.0 && fog->start_density == 0.0f && fog->target_density == 0.0f && fog->sky_factor == 0.0f &&
            fog->start_color.x == 0.0f && fog->start_color.y == 0.0f && fog->start_color.z == 0.0f &&
            fog->target_color.x == 0.0f && fog->target_color.y == 0.0f && fog->target_color.z == 0.0f;
    qa_actor_owner selected; qa_actor_id recipient; qa_clock_state clock;
    return qa_application_q1_fog_owner(frontend->application, &selected) && selected == fog->owner &&
        frontend_seat_actor_read(frontend,seat,&recipient) && qa_actor_id_equal(recipient, fog->recipient) &&
        qa_session_clock(qa_application_session(frontend->application), fog->owner, &clock) && fog->time <= clock.frame.time_ns;
}
static bool view_fields(qa_source_save_io *io, qa_frontend *frontend, frontend_scene_namespace *space,
    unsigned seat, frontend_event_view *view)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    for (unsigned i = 0; i < FRONTEND_STYLES; ++i)
        if (!qa_source_save_owned_text(io, &view->q1_patterns[i]) || !qa_source_save_owned_text(io, &view->q2_patterns[i]) ||
            !qa_source_save_f32(io, &view->q1_styles[i]) || !isfinite(view->q1_styles[i]) ||
            !qa_source_save_vec3(io, &view->q2_styles[i]) || !qa_vec_finite(view->q2_styles[i])) return false;
    if (!q1_fog_fields(io, frontend, seat, &view->q1_fog) ||
        !fog_fields(io, &view->fog_start) || !fog_fields(io, &view->fog_target) ||
        !qa_source_save_u64(io, &view->fog_time) || !qa_source_save_f64(io, &view->fog_duration) || !isfinite(view->fog_duration) ||
        !qa_source_save_bool(io, &view->fog_received) || !qa_source_save_bool(io, &view->sky_received) ||
        !frontend_save_provider(io, frontend->application, &view->sky_owner) ||
        !qa_source_save_vec3(io, &view->sky_axis) || !qa_vec_finite(view->sky_axis) ||
        !qa_source_save_f32(io, &view->sky_rotation) || !isfinite(view->sky_rotation) || !qa_source_save_bool(io, &view->sky_auto)) return false;
    if (view->sky_received && !view->sky_owner) return false;
    if ((!qa_source_save_owned_text(io, &view->sky_name) ||
        (!view->sky_received && view->sky_name))) return false;
    for (unsigned i = 0; i < 6; ++i) {
        uint64_t reference = 0;
        char *name = reading ? NULL : view->sky[i] ? (char *)view->sky[i]->name : NULL;
        bool ok = (reading || !view->sky[i] || frontend_scene_image_encode(space, view->sky[i], &reference, io->error)) &&
            qa_source_save_u64(io, &reference) && qa_source_save_owned_text(io, &name) &&
            ((reference != 0) == (name != NULL)) && (view->sky_received ? reference != 0 : reference == 0);
        if (ok && reference && reading) {
            const qa_scene_image *image = NULL;
            ok = frontend_scene_image_decode(space, reference, &image, io->error);
            if (ok) { qa_scene_image_retain(image); view->sky[i] = image; }
        }
        const frontend_event_resources *resources = frontend->events ? frontend->events->resources : NULL;
        while (resources && (resources->owner != view->sky_owner || resources->family != QA_AUDIO_Q2)) resources = resources->next;
        size_t index = 0;
        const qa_scene_resources *owners[1] = {resources ? resources->images : NULL};
        if (ok && reference) ok = resources && view->sky[i] && view->sky[i]->name && !strcmp(name, view->sky[i]->name) &&
            qa_scene_image_owner_index(owners, 1, view->sky[i], &index) && sky_image_fields(io, view->sky[i]);
        if (reading) free(name);
        if (!ok) return false;
    }
    return true;
}
static bool sound_fields(qa_source_save_io *io, qa_frontend *frontend, const qa_audio_asset_inventory *inventory,
    frontend_scene_identity_scope *scope, size_t ordinal, frontend_retained_sound *entry)
{
    qa_audio_play *sound = &entry->loop.sound; bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t origin_kind = sound->origin_kind, audience = sound->audience;
    bool is_static = entry->static_key != 0; uint32_t seats = entry->restored_static_seats;
    uint64_t static_key=entry->static_key;
    if (!reading && is_static &&
        (!frontend_scene_static_audio_owner_ready(scope,ordinal,entry->static_key,io->error) ||
         !frontend_scene_static_audio_saved(scope,ordinal,entry->static_key,&static_key,io->error))) return false;
    if (!reading) for (unsigned i = 0; i < frontend->options.seats; ++i) if (entry->static_mixers[i]) seats |= 1u << i;
    if (!qa_source_save_actor(io, &entry->actor) || !qa_source_save_u64(io, &sound->actor) ||
        !audio_identity_matches(frontend, entry->actor, sound->actor) ||
        !retained_audio_asset(io, frontend, inventory, &sound->owner, &sound->family, &sound->asset) ||
        !qa_source_save_bool(io, &is_static) || !qa_source_save_u64(io, &static_key) ||
        is_static != (static_key != 0) || !qa_source_save_u32(io, &seats) || seats >= (1u << frontend->options.seats) ||
        !qa_source_save_u32(io, &origin_kind) || !qa_source_save_u32(io, &audience) ||
        !qa_source_save_vec3(io, &sound->origin) || !qa_vec_finite(sound->origin) ||
        !qa_source_save_i32(io, &sound->channel) || !qa_source_save_f32(io, &sound->volume) ||
        !qa_source_save_f32(io, &sound->attenuation) || !isfinite(sound->volume) || sound->volume < 0 || sound->volume > 1 ||
        !isfinite(sound->attenuation) || sound->attenuation < 0 || !qa_source_save_vec3(io, &entry->loop.velocity) ||
        !qa_vec_finite(entry->loop.velocity) || !qa_source_save_i32(io, &entry->loop.frame_number) ||
        !qa_source_save_bool(io, &entry->loop.persistent) ||
        !qa_source_save_u64(io, &entry->renewed_frame) ||
        !audio_audience_matches(frontend, entry->actor, sound, origin_kind, audience)) return false;
    const frontend_event_resources *resources = frontend->events ? frontend->events->resources : NULL;
    while (resources && (resources->owner != sound->owner || resources->family != sound->family)) resources = resources->next;
    if (!resources || resources->gear == entry->loop.persistent ||
        (entry->loop.persistent ? entry->renewed_frame != 0 :
            is_static || origin_kind != QA_AUDIO_FIXED || audience != QA_AUDIO_WORLD ||
            entry->renewed_frame > frontend->frame_number ||
            entry->loop.frame_number != (int32_t)(entry->renewed_frame & INT32_MAX))) return false;
    if (sound->channel < 0 && !(sound->family == QA_AUDIO_Q1 && sound->channel == -1)) return false;
    if (is_static ? entry->actor.registry != 0 || sound->family != QA_AUDIO_Q1 || audience != QA_AUDIO_WORLD :
        !entry->actor.registry || seats != 0) return false;
    if (!reading && (sound->origin_actor != sound->actor || sound->sample != qa_audio_asset_sample(sound->asset))) return false;
    if (reading) {
        entry->static_key=static_key;
        sound->sample = qa_audio_asset_sample(sound->asset); sound->audience = audience;
        sound->origin_actor = sound->actor;
        sound->origin_kind = (qa_audio_origin_kind)origin_kind;
        if (is_static && (origin_kind != QA_AUDIO_FIXED || sound->sample->loop_start == QA_AUDIO_NO_LOOP)) return false;
        if (is_static && !frontend_scene_static_audio_install(scope, ordinal, entry->static_key, &entry->static_key, io->error)) return false;
        entry->restored_static_seats = seats;
    }
    return true;
}
static bool light_fields(qa_source_save_io *io, frontend_scene_identity_scope *scope, size_t ordinal, frontend_q1_light *entry)
{
    qa_scene_light *light = &entry->light;
    uint64_t identity=light->identity;
    if (io->direction==QA_SOURCE_SAVE_WRITE && identity &&
        (!frontend_scene_light_owner_ready(scope,ordinal,identity,io->error) ||
         !frontend_scene_light_saved(scope,ordinal,identity,&identity,io->error))) return false;
    if (!qa_source_save_actor(io, &entry->actor) || !qa_source_save_f64(io, &entry->die) || !isfinite(entry->die) ||
        !qa_source_save_vec3(io, &light->origin) || !qa_vec_finite(light->origin) ||
        !qa_source_save_vec3(io, &light->color) || !qa_vec_finite(light->color) ||
        !qa_source_save_f32(io, &light->radius) || !isfinite(light->radius) || light->radius < 0 ||
        !qa_source_save_f32(io, &light->minimum) || !isfinite(light->minimum) || light->minimum < 0 ||
        !qa_source_save_vec3(io, &light->direction) || !qa_vec_finite(light->direction) ||
        !qa_source_save_f32(io, &light->scale) || !isfinite(light->scale) ||
        !qa_source_save_f32(io, &light->cos_half_angle) || !isfinite(light->cos_half_angle) ||
        !qa_source_save_bool(io, &light->additive) || !qa_source_save_bool(io, &light->spot) ||
        !qa_source_save_bool(io, &light->casts_shadow) || !qa_source_save_u64(io, &identity) ||
        !qa_source_save_u64(io, &light->revision) || !qa_source_save_u32(io, &light->shadow_resolution)) return false;
    uint32_t family = light->family;
    if (!qa_source_save_u32(io, &family) || family > QA_SCENE_Q3) return false;
    light->family = (qa_scene_family)family;
    if (io->direction==QA_SOURCE_SAVE_READ) light->identity=identity;
    if (light->direction.x != 0.0f || light->direction.y != 0.0f || light->direction.z != 0.0f || light->cos_half_angle != 0.0f ||
        light->additive || light->spot || light->casts_shadow || light->shadow_resolution) return false;
    if (!entry->actor.registry)
        return entry->die == 0.0 && !light->identity && !light->revision && !light->family && light->scale == 0.0f &&
            light->origin.x == 0.0f && light->origin.y == 0.0f && light->origin.z == 0.0f &&
            light->color.x == 0.0f && light->color.y == 0.0f && light->color.z == 0.0f && light->radius == 0.0f && light->minimum == 0.0f;
    if (!light->identity || light->revision != 1 || light->family != QA_SCENE_Q1 || light->scale != 1 ||
        light->color.x != 1 || light->color.y != 1 || light->color.z != 1) return false;
    if (io->direction == QA_SOURCE_SAVE_READ &&
        !frontend_scene_light_install(scope, ordinal, light->identity, &light->identity, io->error)) return false;
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
static bool last_step_fields(qa_source_save_io *io, qa_frontend *frontend, const qa_audio_asset_inventory *inventory, frontend_event_state *state)
{
    bool present = state->last_step != NULL;
    if (!qa_source_save_bool(io, &present) || !present) return !io->failed;
    qa_audio_family family = QA_AUDIO_Q2;
    return retained_asset(io, frontend, inventory, &state->last_step_owner, &family, &state->last_step) && family == QA_AUDIO_Q2;
}
static bool footsteps_fields(qa_source_save_io *io, qa_frontend *frontend, const qa_audio_asset_inventory *inventory)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t owners = frontend_event_audio_owner_count(frontend);
    if (!qa_source_save_count(io, &owners, frontend_event_audio_owner_count(frontend)) ||
        owners != frontend_event_audio_owner_count(frontend)) return false;
    for (frontend_event_resources *resource = frontend->events->resources; resource; resource = resource->next) {
        size_t count = 0;
        if (!reading) for (frontend_footsteps *row = resource->footsteps; row; row = row->next) ++count;
        if (!qa_source_save_count(io, &count, reading ? io->input.size / 20 : SIZE_MAX) ||
            (resource->family != QA_AUDIO_Q2 && count)) return false;
        frontend_footsteps **tail = &resource->footsteps;
        for (size_t i = 0; i < count; ++i) {
            if (reading) {
                *tail = calloc(1, sizeof(**tail));
                if (!*tail) return frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring retained footstep cache");
            }
            frontend_footsteps *row = *tail;
            uint32_t assets = row->count;
            if (!qa_source_save_bytes(io, (uint8_t *)row->material, sizeof(row->material)) ||
                !memchr(row->material, 0, sizeof(row->material)) ||
                !qa_source_save_u32(io, &assets) || assets > 16 || !resource->sounds) return false;
            for (size_t j = strlen(row->material) + 1; j < sizeof(row->material); ++j)
                if (row->material[j]) return false;
            for (frontend_footsteps *prior = resource->footsteps; prior != row; prior = prior->next)
                if (material_equal(prior->material, row->material)) return false;
            for (uint32_t j = 0; j < assets; ++j) {
                qa_actor_owner owner = resource->owner; qa_audio_family family = resource->family;
                if (!retained_asset(io, frontend, inventory, &owner, &family, &row->assets[j])) return false;
                if (reading) ++row->count;
                if (owner != resource->owner || family != QA_AUDIO_Q2) return false;
            }
            tail = &row->next;
        }
    }
    return true;
}
static bool event_namespace(qa_frontend *frontend, frontend_scene_identity_scope *scope, bool restoring, qa_error *error)
{
    if (!frontend || !scope || !scope->space || !scope->owner ||
        (restoring ? frontend->capture != NULL : frontend->capture == NULL))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Event namespace requires its actual capture or isolated candidate owner");
    frontend_event_state *state = frontend->events;
    if (!state) return true;
    for (size_t i = 0; i < 32; ++i) {
        uint64_t id = state->q1_lights[i].light.identity;
        if (id && !(restoring ? frontend_scene_namespace_qualify_light(scope->space, scope->owner, i, id, error) :
            frontend_scene_namespace_capture_light(scope->space, scope->owner, i, id, error))) return false;
    }
    size_t ordinal = 32;
    for (frontend_retained_light *light = state->lights; light; light = light->next, ++ordinal)
        if (!(restoring ? frontend_scene_namespace_qualify_light(scope->space, scope->owner, ordinal, light->identity, error) :
            frontend_scene_namespace_capture_light(scope->space, scope->owner, ordinal, light->identity, error))) return false;
    ordinal = 0;
    for (frontend_retained_sound *sound = state->sounds; sound; sound = sound->next) if (sound->static_key) {
        if (!(restoring ? frontend_scene_namespace_qualify_static_audio(scope->space, scope->owner, ordinal, sound->static_key, error) :
            frontend_scene_namespace_capture_static_audio(scope->space, scope->owner, ordinal, sound->static_key, error))) return false;
        ++ordinal;
    }
    return true;
}
bool frontend_event_capture_namespace(qa_frontend *frontend, frontend_scene_identity_scope *scope, qa_error *error)
{ return event_namespace(frontend, scope, false, error); }
bool frontend_event_checkpoint(qa_frontend *frontend, const qa_audio_asset_inventory *inventory,
    frontend_scene_identity_scope *scope, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io = {0};
    if (!frontend || !frontend->application || !frontend->capture || !scope || !scope->space || !scope->owner ||
        !out || out->data || out->size || !qa_source_save_writer(&io, qa_application_session(frontend->application), error)) return false;
    bool present = frontend->events != NULL; uint32_t seats = frontend->options.seats;
    bool ok = event_signature(&io) && qa_source_save_u32(&io, &seats) && qa_source_save_bool(&io, &present);
    frontend_event_state *state = frontend->events;
    if (ok && present) {
        qa_builtin_random random = state->light_random;
        ok = frontend_save_random(&io, &random);
        for (unsigned i = 0; ok && i < 624; ++i) { uint32_t word = state->step_random[i]; ok = qa_source_save_u32(&io, &word); }
        uint32_t cursor = state->step_cursor;
        ok = ok && qa_source_save_u32(&io, &cursor) && last_step_fields(&io, frontend, inventory, state) &&
            footsteps_fields(&io, frontend, inventory);
        size_t pending = 0;
        for (frontend_audio_projection *entry = state->audio_head; entry; entry = entry->next) ++pending;
        bool deferred = state->audio_deferred;
        ok = ok && qa_source_save_bool(&io, &deferred) && qa_source_save_count(&io, &pending, SIZE_MAX);
        for (frontend_audio_projection *entry = state->audio_head; ok && entry; entry = entry->next) {
            frontend_audio_projection copy = *entry; ok = audio_projection_fields(&io, frontend, inventory, &copy);
        }
        for (unsigned i = 0; ok && i < seats; ++i) { frontend_event_view copy = state->views[i]; ok = view_fields(&io, frontend, scope->space, i, &copy); }
        for (unsigned i = 0; ok && i < 32; ++i) { frontend_q1_light copy = state->q1_lights[i]; ok = light_fields(&io, scope, i, &copy); }
        size_t count = 0;
        for (frontend_retained_sound *entry = state->sounds; entry; entry = entry->next) ++count;
        ok = ok && qa_source_save_count(&io, &count, SIZE_MAX);
        size_t ordinal = 0;
        for (frontend_retained_sound *entry = state->sounds; ok && entry; entry = entry->next) {
            frontend_retained_sound copy = *entry; ok = sound_fields(&io, frontend, inventory, scope, ordinal, &copy);
            if (entry->static_key) ++ordinal;
        }
        count = 0; for (frontend_retained_light *entry = state->lights; entry; entry = entry->next) ++count;
        ok = ok && qa_source_save_count(&io, &count, SIZE_MAX);
        ordinal = 32;
        for (frontend_retained_light *entry = state->lights; ok && entry; entry = entry->next, ++ordinal) {
            frontend_retained_light copy = *entry;
            uint64_t identity=copy.identity;
            ok = frontend_scene_light_owner_ready(scope,ordinal,copy.identity,error) &&
                frontend_scene_light_saved(scope,ordinal,copy.identity,&identity,error) &&
                frontend_save_provider(&io, frontend->application, &copy.owner) && copy.owner &&
                frontend_save_q2_event(&io, &copy.event) && copy.event.kind == QA_Q2_MAP_DYNAMIC_LIGHT &&
                qa_source_save_u64(&io, &identity) && identity && qa_source_save_u64(&io, &copy.revision) && copy.revision;
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
static bool event_empty(const frontend_event_state *state)
{
    if (!state) return true;
    const uint8_t *bytes = (const uint8_t *)state + sizeof(state->resources);
    for (size_t i = 0; i < sizeof(*state) - sizeof(state->resources); ++i) if (bytes[i]) return false;
    for (const frontend_event_resources *entry = state->resources; entry; entry = entry->next)
        if (entry->footsteps) return false;
    return true;
}
bool frontend_event_restore(qa_frontend *frontend, const qa_audio_asset_inventory *inventory,
    frontend_scene_identity_scope *scope, qa_bytes bytes, qa_error *error)
{
    if (!frontend || !frontend->application || !frontend->source_restoring || frontend->capture ||
        !scope || !scope->space || !scope->owner || !event_empty(frontend->events))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Event restore requires its empty prepared candidate and genuine shared namespace");
    qa_source_save_io io = {0}; bool present = false; uint32_t seats = 0;
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) && event_signature(&io) &&
        qa_source_save_u32(&io, &seats) && seats == frontend->options.seats && qa_source_save_bool(&io, &present) &&
        present == (frontend->events != NULL);
    frontend_event_state *state = frontend->events;
    if (ok && present) {
        ok = frontend_save_random(&io, &state->light_random);
        uint32_t any = 0;
        for (unsigned i = 0; ok && i < 624; ++i) { ok = qa_source_save_u32(&io, &state->step_random[i]); any |= state->step_random[i]; }
        ok = ok && any && qa_source_save_u32(&io, &state->step_cursor) && state->step_cursor <= 624 &&
            last_step_fields(&io, frontend, inventory, state) && footsteps_fields(&io, frontend, inventory);
        size_t pending = 0;
        ok = ok && qa_source_save_bool(&io, &state->audio_deferred) && qa_source_save_count(&io, &pending, bytes.size / 8);
        for (size_t i = 0; ok && i < pending; ++i) {
            frontend_audio_projection *entry = calloc(1, sizeof(*entry));
            if (!entry) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Restoring pending sound projection"); break; }
            if (state->audio_tail) state->audio_tail->next = entry;
            else state->audio_head = entry;
            state->audio_tail = entry;
            ok = audio_projection_fields(&io, frontend, inventory, entry);
        }
        for (unsigned i = 0; ok && i < seats; ++i) ok = view_fields(&io, frontend, scope->space, i, &state->views[i]);
        for (unsigned i = 0; ok && i < 32; ++i) ok = light_fields(&io, scope, i, &state->q1_lights[i]);
        size_t count = 0; frontend_retained_sound **sounds = &state->sounds;
        size_t ordinal = 0;
        ok = ok && qa_source_save_count(&io, &count, bytes.size / 8);
        for (size_t i = 0; ok && i < count; ++i) {
            *sounds = calloc(1, sizeof(**sounds));
            if (!*sounds) { ok = frontend_fail(error, QA_ERROR_MEMORY, "restoring builtin sound continuation"); break; }
            frontend_retained_sound *entry = *sounds;
            ok = sound_fields(&io, frontend, inventory, scope, ordinal, entry);
            if (entry->static_key) ++ordinal;
            for (frontend_retained_sound *prior = state->sounds; ok && prior != entry; prior = prior->next)
                if (!entry->static_key && !prior->static_key && prior->loop.sound.owner == entry->loop.sound.owner &&
                    prior->loop.sound.family == entry->loop.sound.family &&
                    prior->loop.sound.audience == entry->loop.sound.audience && qa_actor_id_equal(prior->actor, entry->actor)) ok = false;
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
                qa_source_save_u64(&io, &entry->identity) && entry->identity &&
                qa_source_save_u64(&io, &entry->revision) && entry->revision &&
                frontend_scene_light_install(scope, 32 + i, entry->identity, &entry->identity, error);
            for (frontend_retained_light *prior = state->lights; ok && prior != entry; prior = prior->next)
                if (prior->owner == entry->owner && qa_actor_id_equal(prior->event.actor, entry->event.actor)) ok = false;
            lights = &entry->next;
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
    if (ok) ok = qa_source_save_finish(&io, NULL) && event_namespace(frontend, scope, true, error);
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

typedef struct event_policy_view {
    qa_actor_owner owner;
    const char *name;
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
        while (resources && (resources->owner != view->sky_owner || resources->family != QA_AUDIO_Q2)) resources = resources->next;
        if (!resources) goto invalid;
        for (size_t i = 0; i < count; ++i)
            if (qa_scene_resource_policy_source(banks[i]) == resources->images) saved->bank = banks[i];
        qa_scene_resources *destination = qa_scene_resource_policy_destination(saved->bank);
        size_t length = strlen(view->sky_name);
        if (!destination || length > SIZE_MAX - 7) goto invalid;
        char *path = malloc(length + 7);
        if (!path) { frontend_fail(error, QA_ERROR_MEMORY, "Preparing authored sky requests"); goto failed; }
        qa_scene_image_options options = {.family = QA_SCENE_Q2, .wrap = QA_SCENE_CLAMP,
            .filter = QA_SCENE_LINEAR, .usage = QA_IMAGE_USAGE_SKY, .transparent_index = -1};
        bool ok = true;
        for (unsigned face = 0; ok && face < 6; ++face) {
            qa_scene_image *image = NULL; qa_error observed = {0};
            snprintf(path, length + 7, "env/%s%s", view->sky_name, suffixes[face]);
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
