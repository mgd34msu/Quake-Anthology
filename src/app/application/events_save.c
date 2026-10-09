#include "events_save.h"
#include "match_intents.h"
#include "rankings.h"
#include "qa/source_save.h"
#include "unified_output.h"
#include "save_content.h"
#include "qa/json.h"
#include "qa/application_network_q2.h"
#include "native_q1_wire.h"
#include "guest_native_q2_private.h"
#include "../../network/unified/frame_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct event_store {
    application_event_pages *pages;
    application_unified_persistent_event *persistent;
    size_t persistent_count, persistent_capacity;
    application_unified_event_owner *owners;
    size_t owner_count, owner_capacity;
    uint64_t owner_generation;
    application_unified_event_resource *resources;
    size_t resource_count, resource_capacity;
    application_unified_event_registration *registrations;
    size_t registration_count, registration_capacity;
    application_unified_world_text *world_text;
    size_t world_text_count, world_text_capacity;
    uint64_t world_text_map;
    qa_application *application;
} event_store;

static const uint8_t event_magic[8] = {'Q','A','E','V','T','S',0,0};

static bool event_fail(qa_source_save_io *io, qa_status status, const char *text)
{
    if (!io->failed) qa_error_set(io->error, status, io->offset, "%s", text);
    io->failed = true;
    return false;
}

static bool signature(qa_source_save_io *io)
{
    uint8_t magic[sizeof(event_magic)];
    memcpy(magic, event_magic, sizeof(magic));
    return qa_source_save_bytes(io, magic, sizeof(magic)) &&
        (!memcmp(magic, event_magic, sizeof(magic)) ||
         event_fail(io, QA_ERROR_FORMAT, "Invalid application event signature"));
}

static bool state_extent(qa_source_save_io *io, size_t *count, size_t *capacity, size_t width)
{
    if (io->direction == QA_SOURCE_SAVE_WRITE && *count > *capacity)
        return event_fail(io, QA_ERROR_FORMAT, "Application event count exceeds its storage");
    if (!qa_source_save_count(io, count, SIZE_MAX / width)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) *capacity = *count;
    return true;
}

static bool prefix(qa_source_save_io *io, event_store *store)
{
    if (!signature(io) ||
        !state_extent(io, &store->persistent_count, &store->persistent_capacity, sizeof(*store->persistent)) ||
        !state_extent(io, &store->owner_count, &store->owner_capacity, sizeof(*store->owners)) ||
        !qa_source_save_u64(io, &store->owner_generation) || store->owner_generation >= QA_UNIFIED_SAFE_INTEGER ||
        !state_extent(io, &store->resource_count, &store->resource_capacity, sizeof(*store->resources)) ||
        !state_extent(io, &store->registration_count, &store->registration_capacity, sizeof(*store->registrations)) ||
        !state_extent(io, &store->world_text_count, &store->world_text_capacity, sizeof(*store->world_text)) ||
        !qa_source_save_u64(io, &store->world_text_map))
        return event_fail(io, QA_ERROR_FORMAT, "Invalid retained Source state extent");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        size_t remaining = io->input.size - io->offset;
        if (store->persistent_count > remaining / 60)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated persistent Source presentation");
        remaining -= store->persistent_count * 60;
        if (store->owner_count > remaining / 27)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated Source activation owners");
        remaining -= store->owner_count * 27;
        if (store->resource_count > remaining / 100)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated Source resource dictionary");
        remaining -= store->resource_count * 100;
        if (store->registration_count > remaining / 13)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated Source registration owner");
        remaining -= store->registration_count * 13;
        if (store->world_text_count > remaining / 60)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated retained world text");
    }
    return true;
}

static bool enum_field(qa_source_save_io *io, uint32_t *value, uint32_t maximum)
{
    return qa_source_save_u32(io, value) &&
        (*value <= maximum || event_fail(io, QA_ERROR_FORMAT, "Invalid application event enum"));
}

static bool finite_field(qa_source_save_io *io, float *value)
{
    return qa_source_save_f32(io, value) &&
        (isfinite(*value) || event_fail(io, QA_ERROR_FORMAT, "Nonfinite application event scalar"));
}

static bool vector_field(qa_source_save_io *io, qa_vec3 *value)
{
    return qa_source_save_vec3(io, value) &&
        (qa_vec_finite(*value) || event_fail(io, QA_ERROR_FORMAT, "Nonfinite application event vector"));
}

static bool provider_field(qa_source_save_io *io, qa_actor_owner *value, bool required)
{
    /* Names stay in the session table after a provider retires. This retains
     * that source identity without manufacturing an active provider binding. */
    return qa_source_save_string(io, value) &&
        (!required || *value || event_fail(io, QA_ERROR_FORMAT, "Application event has no source provider"));
}

static bool resource_key_field(qa_source_save_io *io,
                               char key[QA_APPLICATION_RESOURCE_KEY_CAPACITY])
{
    size_t size = QA_APPLICATION_RESOURCE_KEY_CAPACITY;
    return qa_source_save_bytes(io, key, size) &&
        (!key[size - 1] || event_fail(io, QA_ERROR_FORMAT, "Unterminated Source resource key"));
}

static event_store borrow_store(qa_application *app)
{
    return (event_store){
        .persistent = app->unified_persistent, .persistent_count = app->unified_persistent_count,
        .persistent_capacity = app->unified_persistent_capacity,
        .owners = app->unified_event_owners, .owner_count = app->unified_event_owner_count,
        .owner_capacity = app->unified_event_owner_capacity, .owner_generation = app->unified_event_owner_generation,
        .resources = app->unified_event_resources, .resource_count = app->unified_event_resource_count,
        .resource_capacity = app->unified_event_resource_capacity,
        .registrations = app->unified_event_registrations,
        .registration_count = app->unified_event_registration_count,
        .registration_capacity = app->unified_event_registration_capacity,
        .world_text = app->unified_world_text, .world_text_count = app->unified_world_text_count,
        .world_text_capacity = app->unified_world_text_capacity, .world_text_map = app->unified_world_text_map,
        .application = app
    };
}

static void dispose_store(event_store *store)
{
    for (size_t i = 0; store->persistent && i < store->persistent_count && i < store->persistent_capacity; ++i)
        if (store->persistent[i].lease) application_event_lease_release(store->persistent[i].lease);
    free(store->persistent);
    application_event_pages_destroy(&store->pages);
    free(store->owners);
    for (size_t i = 0; store->resources && i < store->resource_count; ++i) {
        qa_buffer_free(&store->resources[i].key);
        qa_resource_release(store->resources[i].resource);
        qa_launch_instance_lease_release(store->resources[i].descriptor);
        qa_vfs_acquisition_dispose(&store->resources[i].opening);
        qa_vfs_destroy(store->resources[i].view);
        qa_resource_pool_destroy(store->resources[i].pool);
        for (size_t j = 0; store->resources[i].custodies && j < store->resources[i].custody_count; ++j) {
            application_unified_event_resource_custody *held = store->resources[i].custodies + j;
            qa_resource_release(held->resource); qa_vfs_acquisition_dispose(&held->opening);
            qa_vfs_destroy(held->view); qa_resource_pool_destroy(held->pool);
        }
        free(store->resources[i].custodies);
    }
    free(store->resources);
    free(store->registrations);
    free(store->world_text);
    *store = (event_store){0};
}

static bool allocate_store(qa_source_save_io *io, event_store *store)
{
    qa_application *staging = store->application;
    bool created = application_event_stream_create(staging,
        qa_actors_capacity(qa_session_actors(staging->session)), io->error);
    store->pages = staging->event_pages;
    store->persistent = staging->unified_persistent;
    store->persistent_capacity = staging->unified_persistent_capacity;
    if (!created) return false;
    if (store->persistent_count > store->persistent_capacity)
        return event_fail(io, QA_ERROR_FORMAT, "Persistent Source state exceeds its loaded slots");
    if (store->owner_capacity) store->owners = calloc(store->owner_capacity, sizeof(*store->owners));
    if (store->resource_capacity) store->resources = calloc(store->resource_capacity, sizeof(*store->resources));
    if (store->registration_capacity) store->registrations = calloc(store->registration_capacity, sizeof(*store->registrations));
    if (store->world_text_capacity) store->world_text = calloc(store->world_text_capacity, sizeof(*store->world_text));
    if ((store->resource_capacity && !store->resources) || (store->owner_capacity && !store->owners) ||
        (store->registration_capacity && !store->registrations) ||
        (store->world_text_capacity && !store->world_text))
        return event_fail(io, QA_ERROR_MEMORY, "Allocating retained Source state");
    return true;
}

typedef struct saved_payload_context { qa_source_save_io *io; bool checkpoint; } saved_payload_context;
static bool save_payload_actor(void *context, qa_actor_id actor, qa_actor_id *out, qa_error *e)
{
    saved_payload_context *c = context; qa_saved_actor_id saved;
    if (c->checkpoint) {
        qa_actor_id actual;
        saved = (qa_saved_actor_id){actor.generation, actor.slot};
        if (!qa_actors_reference_saved(qa_session_actors(c->io->session), saved, true, &actual, e)) return false;
    } else if (!qa_actors_save_reference(qa_session_actors(c->io->session), actor, &saved, e)) return false;
    *out = actor; out->generation = saved.generation; out->slot = saved.slot; return true;
}
static bool payload_field(qa_source_save_io *io, void **owned, const qa_unified_record_layout *layout,
    bool checkpoint)
{
    qa_buffer encoded = {0}; void *copy = NULL; bool ok = true;
    if (io->direction == QA_SOURCE_SAVE_WRITE && *owned) {
        copy = calloc(1, layout->size);
        saved_payload_context context = {.io = io, .checkpoint = checkpoint};
        ok = copy && qa_unified_record_clone(layout, *owned, copy, io->error) &&
            qa_unified_record_actor_remap(layout, copy, save_payload_actor, &context, io->error) &&
            qa_unified_record_delta_encode(layout, copy, NULL, 16u * 1024u * 1024u, &encoded, io->error);
        if (!ok && (!io->error || io->error->code == QA_OK)) event_fail(io, QA_ERROR_MEMORY, "Capturing actual typed Source event");
    }
    size_t size = encoded.size;
    if (ok) ok = qa_source_save_count(io, &size, 16u * 1024u * 1024u);
    if (ok && io->direction == QA_SOURCE_SAVE_READ && size) {
        qa_bytes bytes;
        ok = qa_source_save_span(io, size, &bytes);
        if (ok) {
            *owned = calloc(1, layout->size);
            ok = *owned && qa_unified_record_delta_decode(layout, bytes, NULL, *owned, NULL, io->error);
            if (!ok && (!io->error || io->error->code == QA_OK)) event_fail(io, QA_ERROR_MEMORY, "Restoring actual typed Source event");
        }
    } else if (ok && io->direction == QA_SOURCE_SAVE_WRITE) ok = qa_source_save_bytes(io, encoded.data, encoded.size);
    if (copy) { qa_unified_record_dispose(layout, copy); free(copy); }
    qa_buffer_free(&encoded); return ok;
}

static bool normalized_actor_field(qa_source_save_io *io, application_unified_event_record *row, bool simulation)
{
    qa_actor_id *actor = simulation ? &row->simulation_recipient : &row->recipient;
    qa_saved_actor_id *receipt = simulation ? &row->simulation_recipient_saved : &row->recipient_saved;
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && actor->registry != 0;
    qa_saved_actor_id saved = {0};
    if (present) {
        if (row->payload_checkpoint) {
            qa_actor_id actual;
            saved = *receipt;
            if (!qa_actors_reference_saved(qa_session_actors(io->session), saved, true, &actual, io->error)) return false;
        } else if (!qa_actors_save_reference(qa_session_actors(io->session), *actor, &saved, io->error)) return false;
    }
    if (!qa_source_save_bool(io, &present) || !qa_source_save_u64(io, &saved.generation) ||
        !qa_source_save_u32(io, &saved.slot)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!present) {
            if (saved.generation || saved.slot) return event_fail(io, QA_ERROR_FORMAT, "Absent normalized recipient contains actor history");
            *actor = (qa_actor_id){0};
        } else if (!qa_actors_reference_saved(qa_session_actors(io->session), saved, true, actor, io->error)) return false;
        *receipt = saved;
    }
    return true;
}

static bool persistent_field(qa_source_save_io *io, event_store *store, application_unified_event_record *row)
{
    uint32_t clock = row->clock, presentation_clock = row->presentation_clock, family = row->family;
    if (!qa_source_save_u64(io, &row->owner_generation) ||
        !qa_source_save_u8(io, &row->q2_source_profile) || row->q2_source_profile > 2 ||
        !qa_source_save_u64(io, &row->q2_source_interval_ns) ||
        ((row->q2_source_profile != 0) != (row->q2_source_interval_ns != 0)) ||
        !qa_source_save_u64(io, &row->time_ns) ||
        !enum_field(io, &clock, QA_RULESET_Q3) || !enum_field(io, &presentation_clock, QA_RULESET_Q3) ||
        !enum_field(io, &family, QA_GAME_Q3) ||
        !normalized_actor_field(io, row, false) ||
        !provider_field(io, &row->provider, true) || !qa_source_save_string(io, &row->content) ||
        !qa_source_save_i32(io, &row->source_entity) || !qa_source_save_bool(io, &row->has_source_entity)) return false;
    void *payload = row->presentation;
    bool ok = payload_field(io, &payload, &qa_unified_presentation_payload_layout, row->payload_checkpoint);
    row->presentation = payload;
    if (!ok) return false;
    row->clock = clock; row->presentation_clock = presentation_clock; row->family = family;
    if (io->direction == QA_SOURCE_SAVE_READ) row->payload_checkpoint = true;
    return row->content && row->presentation &&
        application_unified_event_actors_valid(store->application, row, io->error);
}

static void decoded_payload_dispose(application_unified_event_record *row)
{
    if (!row->presentation) return;
    qa_unified_record_dispose(&qa_unified_presentation_payload_layout, row->presentation);
    free(row->presentation);
    row->presentation = NULL;
}

static bool persistent_import(qa_source_save_io *io, event_store *store,
    const application_unified_event_record *decoded, application_unified_persistent_event *out)
{
    qa_application *staging = store->application;
    application_event_write write = {0};
    if (!application_event_stream_begin(staging, QA_APPLICATION_EVENT_UNIFIED, &write, io->error))
        return event_fail(io, QA_ERROR_MEMORY, "Restored persistent state exceeds its event pages");
    if (!application_unified_event_append(staging, decoded, io->error)) {
        bool blocked = write.transaction.blocked;
        application_event_stream_abort(staging, &write, io->error);
        if (blocked) return event_fail(io, QA_ERROR_MEMORY, "Restored persistent state exceeds its event pages");
        return false;
    }
    application_event_view *view = write.envelope->views;
    bool remove = false;
    application_persistent_key key = {0};
    if (!application_unified_persistent_key(staging, &view->event, &key, &remove, io->error) ||
        !key.domain || remove) {
        application_event_stream_abort(staging, &write, io->error);
        return event_fail(io, QA_ERROR_FORMAT, "Saved presentation has no persistent state domain");
    }
    uint64_t id = application_event_pages_commit(&write.transaction, write.envelope);
    staging->event_write = NULL;
    if (!id) {
        staging->presentation_event_sequence = write.presentation_before;
        staging->simulation_event_sequence = write.simulation_before;
        return event_fail(io, QA_ERROR_MEMORY, "Restored persistent state exceeds its event pages");
    }
    *out = (application_unified_persistent_event){.event = view->event, .key = key,
        .lease = application_event_pages_retain(staging->event_pages, id)};
    return true;
}

static bool derived_key_field(qa_source_save_io *io)
{
    size_t size = 0;
    qa_bytes unused;
    return qa_source_save_count(io, &size, 16u * 1024u * 1024u) &&
        (io->direction != QA_SOURCE_SAVE_READ || qa_source_save_span(io, size, &unused));
}

static bool persistent_rows(qa_source_save_io *io, event_store *store)
{
    for (size_t i = 0; i < store->persistent_count; ++i) {
        application_unified_persistent_event *row = store->persistent + i;
        if (io->direction == QA_SOURCE_SAVE_WRITE) {
            if (!persistent_field(io, store, &row->event)) return false;
        } else {
            application_unified_event_record decoded = {0};
            bool ok = persistent_field(io, store, &decoded) && persistent_import(io, store, &decoded, row);
            decoded_payload_dispose(&decoded);
            if (!ok) return false;
        }
    }
    for (size_t i = 0; i < store->owner_count; ++i) {
        application_unified_event_owner *owner = store->owners + i;
        if (!provider_field(io, &owner->provider, true) || !qa_source_save_string(io, &owner->content) || !owner->content ||
            !qa_source_save_u64(io, &owner->generation) || owner->generation > store->owner_generation ||
            !qa_source_save_bool(io, &owner->active)) return event_fail(io, QA_ERROR_FORMAT, "Invalid genuine Source activation token");
        for (size_t n = 0; n < i; ++n)
            if (store->owners[n].provider == owner->provider ||
                (owner->generation && store->owners[n].generation == owner->generation))
                return event_fail(io, QA_ERROR_FORMAT, "Source activation owner is duplicated");
    }
    for (size_t i = 0; i < store->persistent_count; ++i) {
        const application_unified_event_record *event = &store->persistent[i].event;
        bool found = false;
        for (size_t n = 0; n < store->owner_count; ++n) {
            const application_unified_event_owner *owner = store->owners + n;
            if (owner->active && owner->provider == event->provider &&
                owner->generation == event->owner_generation && owner->content == event->content)
                found = true;
        }
        if (!found) return event_fail(io, QA_ERROR_FORMAT, "Persistent presentation lost its genuine active Source token");
    }
    return true;
}

static bool custody_field(qa_source_save_io *io, const qa_application_content_graph *graph,
    application_unified_event_resource_custody *held)
{
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        held->saved_view = qa_application_content_view_id(graph, held->view);
        if (!held->saved_view || !qa_application_content_resource_id(graph, held->resource,
            &held->saved_pool, &held->saved_resource))
            return event_fail(io, QA_ERROR_FORMAT, "Source custody is outside its captured CONTENT graph");
    }
    if (!qa_source_save_u64(io, &held->saved_pool) || !qa_source_save_u64(io, &held->saved_resource) ||
        !qa_source_save_u64(io, &held->saved_view) || !held->saved_pool || !held->saved_resource || !held->saved_view)
        return event_fail(io, QA_ERROR_FORMAT, "Source custody lost its actual graph owner");
    const qa_vfs *files = io->direction == QA_SOURCE_SAVE_READ ?
        qa_application_content_view(graph, held->saved_view) : held->view;
    const qa_resource *resource=io->direction==QA_SOURCE_SAVE_READ?
        qa_application_content_resource(graph,held->saved_pool,held->saved_resource):held->resource;
    return qa_application_content_acquisition(io,graph,files,resource,&held->opening);
}

static bool normalized_rows(qa_source_save_io *io, event_store *store)
{
    const qa_application_content_graph *graph = store->application ?
        qa_application_content_graph_read(store->application) : NULL;
    uint64_t previous_serial = 0;
    for (size_t i = 0; i < store->resource_count; ++i) {
        application_unified_event_resource *row = store->resources + i;
        uint64_t pool = row->saved_pool, resource = row->saved_resource, view = row->saved_view;
        uint64_t serial;
        if (io->direction == QA_SOURCE_SAVE_WRITE) {
            view = qa_application_content_view_id(graph, row->view);
            if (!view || !qa_application_content_resource_id(graph, row->resource, &pool, &resource))
                return event_fail(io, QA_ERROR_FORMAT, "Source dictionary resource is outside its captured CONTENT graph");
        }
        if (!provider_field(io, &row->provider, true) || !qa_source_save_string(io, &row->content) ||
            !qa_source_save_string(io, &row->path) || !qa_source_save_u64(io, &pool) ||
            !qa_source_save_u64(io, &resource) || !qa_source_save_u64(io, &view) ||
            !pool || !resource || !view || !row->content || !row->path ||
            !resource_key_field(io, row->id) || row->id[QA_APPLICATION_RESOURCE_KEY_CAPACITY - 1] ||
            !qa_unified_resource_serial(row->id, &serial) || serial <= previous_serial)
            return event_fail(io, QA_ERROR_FORMAT, "Source resource dictionary has invalid ownership");
        previous_serial = serial;
        const qa_vfs *files = io->direction == QA_SOURCE_SAVE_READ ?
            qa_application_content_view(graph, view) : row->view;
        const qa_resource *actual=io->direction==QA_SOURCE_SAVE_READ?
            qa_application_content_resource(graph,pool,resource):row->resource;
        if (!qa_application_content_acquisition(io,graph,files,actual,&row->opening))
            return event_fail(io,QA_ERROR_FORMAT,"Source dictionary cannot reopen its installed resource");
        if (!derived_key_field(io)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            row->saved_pool = pool; row->saved_resource = resource; row->saved_view = view;
        }
        if (!state_extent(io, &row->custody_count, &row->custody_capacity, sizeof(*row->custodies))) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            if (row->custody_count > (io->input.size - io->offset) / 34)
                return event_fail(io, QA_ERROR_FORMAT, "Truncated Source opening receipts");
            row->custodies = row->custody_capacity ? calloc(row->custody_capacity, sizeof(*row->custodies)) : NULL;
            if (row->custody_capacity && !row->custodies)
                return event_fail(io, QA_ERROR_MEMORY, "Retaining restored Source opening receipts");
        } else if ((row->custody_capacity != 0) != (row->custodies != NULL))
            return event_fail(io, QA_ERROR_FORMAT, "Source custody allocation lost its backing");
        for (size_t j = 0; j < row->custody_count; ++j)
            if (!custody_field(io, graph, row->custodies + j)) return false;
    }
    for (size_t i = 0; i < store->world_text_count; ++i) {
        application_unified_world_text *row = store->world_text + i;
        if (!provider_field(io, &row->provider, true) || !qa_source_save_string(io, &row->content) ||
            !qa_source_save_string(io, &row->text) || !vector_field(io, &row->origin) ||
            !vector_field(io, &row->angles) || !vector_field(io, &row->color) ||
            !finite_field(io, &row->alpha) || !finite_field(io, &row->cell_size) || row->cell_size <= 0 ||
            !qa_source_save_f64(io, &row->expires) || !isfinite(row->expires) ||
            !qa_source_save_u64(io, &row->first_frame) || !qa_source_save_bool(io, &row->timed) ||
            !qa_source_save_bool(io, &row->observed) || !qa_source_save_bool(io, &row->billboard) ||
            !qa_source_save_bool(io, &row->depth_test) || !row->content || !row->text ||
            (row->timed && row->observed) || (!row->observed && row->first_frame))
            return event_fail(io, QA_ERROR_FORMAT, "World text continuation lost its Source geometry or lifetime");
    }
    for (size_t i = 0; i < store->registration_count; ++i) {
        application_unified_event_registration *row = store->registrations + i;
        uint32_t kind = (uint32_t)row->kind;
        if (!provider_field(io, &row->provider, true) || !qa_source_save_string(io, &row->path) ||
            !row->path || !store->resource_count ||
            !qa_source_save_count(io, &row->resource, store->resource_count - 1) ||
            !qa_source_save_u32(io, &kind) || kind > QA_NATIVE_HOST_IMAGE ||
            !qa_source_save_u64(io, &row->custody) || row->custody > store->resources[row->resource].custody_count)
            return event_fail(io, QA_ERROR_FORMAT, "Source registration lost its actual retained resource");
        row->kind = (qa_native_host_resource_kind)kind;
        for (size_t j = 0; j < i; ++j)
            if (row->provider == store->registrations[j].provider && row->path == store->registrations[j].path &&
                row->kind == store->registrations[j].kind)
                return event_fail(io, QA_ERROR_FORMAT, "Source registration repeats a current path");
    }
    return true;
}

static bool custody_valid(const application_unified_event_resource *row, qa_application *app,
    uint64_t pool, uint64_t resource, uint64_t view_id, const qa_vfs_acquisition *opening, qa_error *error)
{
    const qa_resource *actual = qa_application_content_resource(app->content_graph, pool, resource);
    const qa_vfs *view = qa_application_content_view(app->content_graph, view_id);
    const char *content = qa_strings_cstr(qa_session_strings(app->session), row->content);
    const char *path = qa_strings_cstr(qa_session_strings(app->session), row->path);
    qa_bytes content_bytes = qa_strings_text(qa_session_strings(app->session), row->content);
    qa_bytes path_bytes = qa_strings_text(qa_session_strings(app->session), row->path);
    if (!actual || !view || !content || !path || strlen(content) != content_bytes.size ||
        strlen(path) != path_bytes.size || !opening->path || strcmp(opening->path, path) ||
        opening->resource_id != qa_resource_id(actual) ||
        qa_resource_pool_find(qa_vfs_resources(view), qa_resource_id(actual)) != actual ||
        !opening->opening_present || !qa_vfs_acquisition_retained(view, opening, error))
        return application_fail(error, QA_ERROR_FORMAT, "Source dictionary cannot bind its decoded immutable CONTENT opening");
    return true;
}

static bool resource_bindings(event_store *store, qa_application *app, qa_error *error)
{
    for (size_t i = 0; i < store->owner_count; ++i) {
        const application_unified_event_owner *owner = store->owners + i;
        if (!owner->active) continue;
        application_unified_event_source source;
        const char *content = qa_strings_cstr(qa_session_strings(app->session), owner->content);
        if (!application_unified_event_source_read(app, owner->provider, &source, error)) return false;
        if (!content || !source.product || strcmp(source.product->identity, content) ||
            source.component != (owner->generation != 0))
            return application_fail(error, QA_ERROR_FORMAT, "Source activation differs from its actual restored content owner");
    }
    for (size_t i = 0; i < store->resource_count; ++i) {
        application_unified_event_resource *row = store->resources + i;
        if (!custody_valid(row, app, row->saved_pool, row->saved_resource, row->saved_view, &row->opening, error)) return false;
        for (size_t j = 0; j < row->custody_count; ++j) {
            application_unified_event_resource_custody *held = row->custodies + j;
            if (!custody_valid(row, app, held->saved_pool, held->saved_resource, held->saved_view, &held->opening, error)) return false;
        }
        qa_product product = {.identity = qa_strings_cstr(qa_session_strings(app->session), row->content)};
        const char *path = qa_strings_cstr(qa_session_strings(app->session), row->path);
        const qa_resource *actual = qa_application_content_resource(app->content_graph, row->saved_pool, row->saved_resource);
        qa_unified_document *key = NULL;
        char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY];
        uint64_t serial = 0;
        (void)qa_unified_resource_serial(row->id, &serial);
        if (!application_unified_resource_key(serial, &product, path, actual, &key, id, error)) return false;
        qa_bytes bytes = qa_json_source(qa_unified_document_json(key), qa_unified_document_root(key));
        row->key.data = malloc(bytes.size); row->key.size = bytes.size;
        if (row->key.data) memcpy(row->key.data, bytes.data, bytes.size);
        qa_unified_document_destroy(key);
        if (!row->key.data)
            return application_fail(error, QA_ERROR_MEMORY, "Rebuilding Source resource key");
    }
    for (size_t i = 0; i < store->registration_count; ++i) {
        const application_unified_event_registration *row = store->registrations + i;
        const application_unified_event_resource *resource = store->resources + row->resource;
        application_unified_event_source source;
        if (!application_unified_event_source_read(app, row->provider, &source, error)) return false;
        const char *path = qa_strings_cstr(qa_session_strings(app->session), row->path);
        const char *content = qa_strings_cstr(qa_session_strings(app->session), resource->content);
        qa_bytes path_bytes = qa_strings_text(qa_session_strings(app->session), row->path);
        if (!source.product || !path || !*path || !content ||
            strlen(path) != path_bytes.size || strcmp(source.product->identity, content) ||
            (unsigned)row->kind > QA_NATIVE_HOST_IMAGE)
            return application_fail(error, QA_ERROR_FORMAT, "Source registration differs from its actual restored source and resource path");
    }
    /* Validate the entire dictionary before transferring any graph owner. */
    for (size_t i = 0; i < store->resource_count; ++i) {
        application_unified_event_resource *row = store->resources + i;
        if (!application_save_content_event_pool(app->content_graph, row->saved_pool, &row->pool, error)) return false;
        if (!application_save_content_event_view(app->content_graph, row->saved_view, &row->view, error)) return false;
        row->resource = (qa_resource *)qa_application_content_resource(app->content_graph,
            row->saved_pool, row->saved_resource);
        qa_resource_retain(row->resource);
        for (size_t j = 0; j < row->custody_count; ++j) {
            application_unified_event_resource_custody *held = row->custodies + j;
            if (!application_save_content_event_pool(app->content_graph, held->saved_pool, &held->pool, error) ||
                !application_save_content_event_view(app->content_graph, held->saved_view, &held->view, error)) return false;
            held->resource = (qa_resource *)qa_application_content_resource(app->content_graph,
                held->saved_pool, held->saved_resource);
            qa_resource_retain(held->resource);
        }
    }
    return true;
}

static bool rows(qa_source_save_io *io, event_store *store)
{
    return normalized_rows(io, store) && persistent_rows(io, store);
}

/* Older files share this signature but retain delivery queues and counters.
 * Normalize their wire records once, then read the current durable rows. */
typedef struct event_wire_layout {
    uint8_t words, durable[7], queues, journal, unified;
} event_wire_layout;
static const event_wire_layout event_wire_layouts[] = {
    {7, {0,1,2,3,4,5,6}, 0, 0, 0},
    {22, {12,14,15,16,17,19,21}, 1, 8, 11},
    {20, {10,12,13,14,15,17,19}, 1, 0, 9}
};
enum { WIRE_TEXT = -1, WIRE_BLOB = -2, WIRE_ARGUMENTS = -3 };

static bool event_wire_span(qa_source_save_io *io, qa_source_save_io *out, size_t size)
{
    qa_bytes bytes;
    return qa_source_save_span(io, size, &bytes) &&
        (!out || qa_source_save_bytes(out, (void *)bytes.data, bytes.size));
}
static bool event_wire_blob(qa_source_save_io *io)
{
    size_t size = 0; qa_bytes bytes;
    return qa_source_save_count(io, &size, io->input.size - io->offset) &&
        qa_source_save_span(io, size, &bytes);
}
static bool event_wire_text(qa_source_save_io *io)
{
    bool present = false;
    return qa_source_save_bool(io, &present) && (!present || event_wire_blob(io));
}
static bool event_wire_arguments(qa_source_save_io *io)
{
    size_t count = 0;
    if (!qa_source_save_count(io, &count, (io->input.size - io->offset) / 5)) return false;
    for (size_t i = 0; i < count; ++i) {
        uint32_t kind = 0;
        if (!qa_source_save_u32(io, &kind) || kind > QA_BUILTIN_MESSAGE_NUMBER ||
            !(kind == QA_BUILTIN_MESSAGE_STRING ? event_wire_text(io) : event_wire_span(io, NULL, 8))) return false;
    }
    return true;
}
static bool event_wire_fields(qa_source_save_io *io, const int *fields)
{
    for (; *fields; ++fields) {
        if (*fields == WIRE_TEXT) { if (!event_wire_text(io)) return false; }
        else if (*fields == WIRE_BLOB) { if (!event_wire_blob(io)) return false; }
        else if (*fields == WIRE_ARGUMENTS) { if (!event_wire_arguments(io)) return false; }
        else if (!event_wire_span(io, NULL, (size_t)*fields)) return false;
    }
    return true;
}
static bool event_wire_array(qa_source_save_io *io, const int *fields)
{
    size_t count = 0;
    if (!qa_source_save_count(io, &count, io->input.size - io->offset)) return false;
    for (size_t i = 0; i < count; ++i) if (!event_wire_fields(io, fields)) return false;
    return true;
}
static bool event_wire_audience(qa_source_save_io *io)
{
    static const int fields[] = {WIRE_TEXT,WIRE_TEXT,WIRE_TEXT,81,0}, recipient[] = {75,0};
    bool captured = false;
    return qa_source_save_bool(io, &captured) && (!captured ||
        (event_wire_fields(io, fields) && event_wire_array(io, recipient)));
}
static bool event_wire_builtin(qa_source_save_io *io)
{
    static const int common[] = {WIRE_TEXT,13,13,WIRE_TEXT,WIRE_TEXT,12,12,12,4,4,4,4,4,4,4,4,WIRE_ARGUMENTS};
    static const int choice[] = {WIRE_TEXT,4,0};
    uint32_t kind = 0, family = 0, fields = 0, code = 0;
    if (!qa_source_save_u32(io, &kind) || kind > QA_BUILTIN_Q2_ENTITY_EVENT ||
        !qa_source_save_u32(io, &family) || family > QA_GAME_Q3 ||
        !event_wire_span(io, NULL, 8) || !qa_source_save_u32(io, &fields) || (fields & ~UINT32_C(131071))) return false;
    for (unsigned i = 0; i < 17; ++i) if (fields & (UINT32_C(1) << i)) {
        if (i == 11) { if (!qa_source_save_u32(io, &code)) return false; }
        else { int field[] = {common[i],0}; if (!event_wire_fields(io, field)) return false; }
    }
    if (family == QA_GAME_Q2 && kind == QA_BUILTIN_MUZZLE) {
        bool pose = false;
        if (!qa_source_save_bool(io, &pose) || (pose && !event_wire_span(io, NULL, 16))) return false;
    }
    if (family == QA_GAME_Q2 && kind == QA_BUILTIN_ITEM && !code && !event_wire_text(io)) return false;
    if (kind == QA_BUILTIN_CTF_STATUS && !event_wire_span(io, NULL, 32)) return false;
    if (kind == QA_BUILTIN_CTF_CAPTURE && !event_wire_span(io, NULL, 9)) return false;
    if (kind == QA_BUILTIN_Q1_POWERUP && !event_wire_span(io, NULL, 12)) return false;
    if (kind == QA_BUILTIN_SOURCE_PROMPT && !event_wire_array(io, choice)) return false;
    return event_wire_audience(io);
}
static bool event_wire_q2_map(qa_source_save_io *io)
{
    static const int fields[] = {WIRE_TEXT,12,39,WIRE_TEXT,WIRE_TEXT,149,WIRE_ARGUMENTS,0};
    static const int level[] = {WIRE_TEXT,WIRE_TEXT,28,0};
    size_t count = 0;
    if (!event_wire_fields(io, fields) ||
        !qa_source_save_count(io, &count, io->input.size - io->offset) || !event_wire_span(io, NULL, 8)) return false;
    for (size_t i = 0; i < count; ++i) if (!event_wire_fields(io, level)) return false;
    return event_wire_audience(io);
}
static bool event_wire_q2_player(qa_source_save_io *io)
{
    static const int fields[] = {WIRE_TEXT,12,26,WIRE_TEXT,WIRE_TEXT,96,WIRE_TEXT,WIRE_TEXT,20,WIRE_TEXT,WIRE_TEXT,6,0};
    static const int score[] = {4,WIRE_TEXT,13,0}, inventory[] = {WIRE_TEXT,20,0};
    static const int after[] = {32,WIRE_TEXT,34,0}, recipient[] = {54,0};
    size_t count = 0; bool scores = false, items = false;
    if (!event_wire_fields(io, fields) ||
        !qa_source_save_count(io, &count, io->input.size - io->offset) ||
        !qa_source_save_bool(io, &scores) || !qa_source_save_bool(io, &items)) return false;
    for (size_t i = 0; scores && i < count; ++i) if (!event_wire_fields(io, score)) return false;
    for (size_t i = 0; items && i < count; ++i) if (!event_wire_fields(io, inventory)) return false;
    return event_wire_fields(io, after) && event_wire_array(io, recipient);
}
static bool event_wire_protocol(qa_source_save_io *io)
{
    static const int fields[] = {WIRE_TEXT,37,WIRE_BLOB,0}, reference[] = {22,0};
    static const int resource[] = {16,WIRE_TEXT,QA_APPLICATION_RESOURCE_KEY_CAPACITY,8,0};
    bool q2 = false;
    return event_wire_fields(io, fields) && event_wire_array(io, reference) && event_wire_array(io, resource) &&
        event_wire_span(io, NULL, 7) && qa_source_save_bool(io, &q2) &&
        (!q2 || (event_wire_span(io, NULL, 8) && event_wire_audience(io)));
}
static bool event_wire_queues(qa_source_save_io *io, const uint64_t *header, const event_wire_layout *layout)
{
    if (!layout->queues) return true;
    static const int q3[] = {WIRE_TEXT,12,26,WIRE_TEXT,WIRE_TEXT,78,0};
    for (unsigned queue = 0; queue < 5; ++queue) {
        uint64_t count = header[layout->queues + queue];
        if (count > io->input.size - io->offset) return false;
        for (uint64_t i = 0; i < count; ++i) {
            bool ok = queue == 0 ? event_wire_builtin(io) : queue == 1 ? event_wire_q2_map(io) :
                queue == 2 ? event_wire_fields(io, q3) : queue == 3 ? event_wire_q2_player(io) : event_wire_protocol(io);
            if (!ok) return false;
        }
    }
    uint64_t count = layout->journal ? header[layout->journal] : 0;
    if (count > (io->input.size - io->offset) / 2) return false;
    for (uint64_t i = 0; i < count; ++i) {
        uint8_t queue = 0, clock = 0;
        if (!qa_source_save_u8(io, &queue) || queue > 4 ||
            !qa_source_save_u8(io, &clock) || clock > 2 || (clock == 1 && !i)) return false;
        if (clock == 2 && (!event_wire_text(io) || !event_wire_span(io, NULL, 40))) return false;
    }
    return true;
}
static bool event_wire_envelope(qa_source_save_io *io, qa_source_save_io *out, bool retired, bool persistent)
{
    if (retired && !event_wire_span(io, NULL, 24)) return false;
    if (!event_wire_span(io, out, 25)) return false;
    if (retired && !event_wire_span(io, NULL, 8)) return false;
    if (!event_wire_span(io, out, 25)) return false;
    if (retired && !event_wire_span(io, NULL, 13)) return false;
    size_t start = io->offset;
    if (!event_wire_text(io) || !event_wire_text(io) || !event_wire_span(io, NULL, 5)) return false;
    if (out && !qa_source_save_bytes(out, (void *)(io->input.data + start), io->offset - start)) return false;
    if (retired && !event_wire_span(io, NULL, 21)) return false;
    start = io->offset;
    if (!event_wire_blob(io)) return false;
    if (out && !qa_source_save_bytes(out, (void *)(io->input.data + start), io->offset - start)) return false;
    return !retired || (event_wire_blob(io) && (!persistent || event_wire_blob(io)));
}
static bool event_wire_state(qa_source_save_io *io, qa_source_save_io *out,
    const uint64_t *header, const event_wire_layout *layout)
{
    static const int resource[] = {WIRE_TEXT,WIRE_TEXT,WIRE_TEXT,24,QA_APPLICATION_RESOURCE_KEY_CAPACITY,WIRE_TEXT,WIRE_BLOB,0};
    static const int custody[] = {24,WIRE_TEXT,0}, world_text[] = {WIRE_TEXT,WIRE_TEXT,WIRE_TEXT,64,0};
    static const int registration[] = {WIRE_TEXT,WIRE_TEXT,20,0}, owner[] = {WIRE_TEXT,WIRE_TEXT,9,0};
    if (!event_wire_queues(io, header, layout)) return false;
    uint64_t count = layout->unified ? header[layout->unified] : 0;
    if (count > (io->input.size - io->offset) / 60) return false;
    for (uint64_t i = 0; i < count; ++i) if (!event_wire_envelope(io, NULL, true, false)) return false;
    size_t start = io->offset;
    count = header[layout->durable[3]];
    if (count > (io->input.size - io->offset) / 100) return false;
    for (uint64_t i = 0; i < count; ++i)
        if (!event_wire_fields(io, resource) || !event_wire_array(io, custody)) return false;
    count = header[layout->durable[5]];
    if (count > (io->input.size - io->offset) / 60) return false;
    for (uint64_t i = 0; i < count; ++i) if (!event_wire_fields(io, world_text)) return false;
    count = header[layout->durable[4]];
    if (count > (io->input.size - io->offset) / 13) return false;
    for (uint64_t i = 0; i < count; ++i) if (!event_wire_fields(io, registration)) return false;
    if (out && !qa_source_save_bytes(out, (void *)(io->input.data + start), io->offset - start)) return false;
    count = header[layout->durable[0]];
    if (count > (io->input.size - io->offset) / 60) return false;
    for (uint64_t i = 0; i < count; ++i)
        if (!event_wire_envelope(io, out, layout->queues != 0, true)) return false;
    start = io->offset;
    count = header[layout->durable[1]];
    if (count > (io->input.size - io->offset) / 27) return false;
    for (uint64_t i = 0; i < count; ++i) if (!event_wire_fields(io, owner)) return false;
    return (!out || qa_source_save_bytes(out, (void *)(io->input.data + start), io->offset - start)) &&
        qa_source_save_finish(io, NULL);
}
static bool event_wire_import(qa_bytes bytes, const event_wire_layout *layout,
    qa_source_save_io *out, qa_error *error)
{
    qa_source_save_io io = {0}; uint64_t header[22] = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && signature(&io);
    for (unsigned i = 0; ok && i < layout->words; ++i) ok = qa_source_save_u64(&io, header + i);
    if (ok && out) {
        event_store store = {
            .persistent_count = (size_t)header[layout->durable[0]], .persistent_capacity = (size_t)header[layout->durable[0]],
            .owner_count = (size_t)header[layout->durable[1]], .owner_capacity = (size_t)header[layout->durable[1]],
            .owner_generation = header[layout->durable[2]],
            .resource_count = (size_t)header[layout->durable[3]], .resource_capacity = (size_t)header[layout->durable[3]],
            .registration_count = (size_t)header[layout->durable[4]], .registration_capacity = (size_t)header[layout->durable[4]],
            .world_text_count = (size_t)header[layout->durable[5]], .world_text_capacity = (size_t)header[layout->durable[5]],
            .world_text_map = header[layout->durable[6]]
        };
        ok = prefix(out, &store);
    }
    if (ok) ok = event_wire_state(&io, out, header, layout);
    qa_source_save_dispose(&io);
    return ok;
}
static bool event_wire_normalize(qa_bytes bytes, qa_buffer *normalized, qa_error *error)
{
    const event_wire_layout *selected = NULL;
    for (size_t i = 0; i < sizeof(event_wire_layouts) / sizeof(event_wire_layouts[0]); ++i) {
        qa_error probe = {0};
        if (!event_wire_import(bytes, event_wire_layouts + i, NULL, &probe)) continue;
        if (selected) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Ambiguous retained event wire layout"); return false; }
        selected = event_wire_layouts + i;
    }
    if (!selected) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid retained event wire extent"); return false; }
    if (selected == event_wire_layouts) return true;
    qa_source_save_io out = {0};
    bool ok = qa_source_save_writer(&out, NULL, error) && event_wire_import(bytes, selected, &out, error) &&
        qa_source_save_finish(&out, normalized);
    qa_source_save_dispose(&out);
    return ok;
}

static bool leased(qa_application *app, qa_error *error)
{
    if (!app || app->operation != APPLICATION_PERSISTING || !app->session ||
        app->destroy_requested || app->finalizing || app->q3_round_active || app->frame_preparing ||
        app->publication_started || !qa_session_safe(app->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Application event persistence requires its operation lease");
    return true;
}

static void record_resource_uses(const event_store *store, const application_unified_event_record *row, bool *used)
{
    if (!row->simulation || row->simulation->kind != QA_UNIFIED_SIMULATION_SOUND) return;
    const char *id = row->simulation->value.sound.resource;
    for (size_t i = 0; i < store->resource_count; ++i)
        if (id && !strcmp(id, store->resources[i].id)) used[i] = true;
}

static bool registration_rebuilt(const event_store *store,
    const application_unified_event_registration *registration)
{
    const qa_application *app = store->application;
    for (size_t i = 0; i < app->provider_count; ++i) {
        const application_provider *provider = app->providers[i];
        if (provider->owner != registration->provider) continue;
        if (provider->kind == APPLICATION_PROVIDER_Q1)
            return registration->kind == QA_NATIVE_HOST_SOUND && qa_q1_wire_enabled(provider->state.q1);
        if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine)
            return provider->state.native.q2_engine->profile != QA_NATIVE_Q2_CGAME_API2023;
        return false;
    }
    return false;
}

static bool retained_resources(event_store *store, qa_error *error)
{
    bool *used = store->resource_count ? calloc(store->resource_count, sizeof(*used)) : NULL;
    size_t *indices = store->resource_count ? malloc(store->resource_count * sizeof(*indices)) : NULL;
    application_unified_event_resource *resources = store->resource_count ?
        malloc(store->resource_count * sizeof(*resources)) : NULL;
    application_unified_event_registration *registrations = store->registration_count ?
        malloc(store->registration_count * sizeof(*registrations)) : NULL;
    if ((store->resource_count && (!used || !indices || !resources)) ||
        (store->registration_count && !registrations)) {
        free(used); free(indices); free(resources); free(registrations);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining referenced Source event openings");
    }
    bool ok = true;
    for (size_t i = 0; i < store->persistent_count; ++i)
        record_resource_uses(store, &store->persistent[i].event, used);
    size_t registration_count = 0, resource_count = 0;
    for (size_t i = 0; ok && i < store->registration_count; ++i) {
        const application_unified_event_registration *row = store->registrations + i;
        if (registration_rebuilt(store, row)) continue;
        if (row->resource >= store->resource_count) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Source registration lost its retained event opening");
            break;
        }
        used[row->resource] = true;
        registrations[registration_count++] = *row;
    }
    if (ok) {
        for (size_t i = 0; i < store->resource_count; ++i)
            if (used[i]) {
                indices[i] = resource_count;
                resources[resource_count++] = store->resources[i];
            }
        for (size_t i = 0; i < registration_count; ++i)
            registrations[i].resource = indices[registrations[i].resource];
        store->resources = resources;
        store->resource_count = store->resource_capacity = resource_count;
        store->registrations = registrations;
        store->registration_count = store->registration_capacity = registration_count;
    } else { free(resources); free(registrations); }
    free(used); free(indices);
    return ok;
}

bool application_events_save_capture(qa_application *app, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !leased(app, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Application event capture requires an empty output and leased owner");
    event_store store = borrow_store(app);
    if (!retained_resources(&store, error)) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, app->session, error) && prefix(&io, &store) &&
        rows(&io, &store) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    free(store.resources); free(store.registrations);
    return ok;
}

bool application_events_save_content_visit(qa_application *app,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    event_store store = borrow_store(app);
    if (!retained_resources(&store, error)) return false;
    bool ok = true;
    for (size_t i = 0; ok && i < store.resource_count; ++i) {
        const application_unified_event_resource *row = store.resources + i;
        ok = row->view && row->pool && row->resource &&
            qa_vfs_resources(row->view) == row->pool && row->opening.opening_present &&
            row->opening.resource_id == qa_resource_id(row->resource) &&
            qa_vfs_acquisition_retained(row->view, &row->opening, error) &&
            qa_resource_pool_find(row->pool, qa_resource_id(row->resource)) == row->resource &&
            visitor->view(visitor->context, row->view, error);
        if (!ok && (!error || error->code == QA_OK))
            application_fail(error, QA_ERROR_FORMAT, "Source event resource lost its actual immutable opening");
        for (size_t j = 0; ok && j < row->custody_count; ++j) {
            const application_unified_event_resource_custody *held = row->custodies + j;
            ok = held->view && held->pool && held->resource && qa_vfs_resources(held->view) == held->pool &&
                held->opening.opening_present && held->opening.resource_id == qa_resource_id(held->resource) &&
                qa_resource_pool_find(held->pool, qa_resource_id(held->resource)) == held->resource &&
                qa_vfs_acquisition_retained(held->view, &held->opening, error) &&
                visitor->view(visitor->context, held->view, error);
            if (!ok && (!error || error->code == QA_OK))
                application_fail(error, QA_ERROR_FORMAT, "Source event custody lost its actual immutable opening");
        }
    }
    free(store.resources); free(store.registrations);
    return ok;
}

static void install_store(qa_application *app, event_store *store)
{
    application_unified_persistent_dispose(app);
    application_event_pages_destroy(&app->event_pages);
    free(app->unified_event_owners);
    application_unified_events_resources_dispose(app);
    free(app->unified_world_text);
    app->event_pages = store->pages;
    app->event_write = NULL;
    app->event_local_cursor = application_event_pages_next(store->pages);
    app->event_peer_cursor = UINT64_MAX;
    application_event_pages_retire(store->pages, app->event_local_cursor);
    ++app->protocol_events_generation;
    app->simulation_event_sequence = store->application->simulation_event_sequence;
    app->presentation_event_sequence = store->application->presentation_event_sequence;
    app->unified_persistent = store->persistent; app->unified_persistent_count = store->persistent_count;
    app->unified_persistent_capacity = store->persistent_capacity; ++app->unified_persistent_revision;
    app->unified_event_owners = store->owners; app->unified_event_owner_count = store->owner_count;
    app->unified_event_owner_capacity = store->owner_capacity; app->unified_event_owner_generation = store->owner_generation;
    app->unified_event_resources = store->resources; app->unified_event_resource_count = store->resource_count;
    app->unified_event_resource_capacity = store->resource_capacity;
    app->unified_event_registrations = store->registrations;
    app->unified_event_registration_count = store->registration_count;
    app->unified_event_registration_capacity = store->registration_capacity;
    ++app->unified_event_registration_revision;
    app->unified_world_text = store->world_text; app->unified_world_text_count = store->world_text_count;
    app->unified_world_text_capacity = store->world_text_capacity;
    ++app->unified_world_text_revision; app->unified_world_text_map = store->world_text_map;
    *store = (event_store){0};
}

bool application_events_save_restore(qa_application *app, qa_bytes bytes, qa_error *error)
{
    if (!leased(app, error)) return false;
    qa_buffer normalized = {0};
    if (!event_wire_normalize(bytes, &normalized, error)) return false;
    if (normalized.data) bytes = (qa_bytes){normalized.data, normalized.size};
    qa_application staging = *app;
    staging.event_pages = NULL;
    staging.event_write = NULL;
    staging.unified_persistent = NULL;
    staging.unified_persistent_count = staging.unified_persistent_capacity = 0;
    staging.presentation_event_sequence = staging.simulation_event_sequence = 0;
    event_store store = {.application = &staging};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, app->session, bytes, error) && prefix(&io, &store) &&
        allocate_store(&io, &store) && rows(&io, &store) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    qa_buffer_free(&normalized);
    if (ok) ok = resource_bindings(&store, app, error);
    if (ok) install_store(app, &store);
    dispose_store(&store);
    return ok;
}

static bool public_lease(qa_application *app, qa_error *error)
{
    if (!app || app->operation != APPLICATION_IDLE || app->client_preparation || !app->session || !app->world ||
        !app->configuration || !qa_application_launch(app) || app->destroy_requested || app->finalizing ||
        app->q3_round_active || app->frame_preparing || app->publication_started || app->pending_close ||
        app->routing_snapshot || app->routing_providers || app->routing_provider_count ||
        !qa_session_safe(app->session) || !qa_session_destroy_ready(app->session) ||
        !qa_world_idle(app->world) || !qa_combat_idle(app->combat) || !qa_console_idle(app->console) ||
        !application_guests_idle(app) || !application_bots_can_destroy(app) || !application_rankings_idle(app) ||
        (app->modes && !qa_modes_idle(app->modes)) ||
        (app->equipment && !qa_equipment_idle(app->equipment)) ||
        !application_match_intents_idle(app->match_intents) ||
        (app->state != QA_APPLICATION_READY && app->state != QA_APPLICATION_RUNNING))
        return application_fail(error, QA_ERROR_ARGUMENT, "Application event checkpoint requires a committed idle owner");
    app->operation = APPLICATION_PERSISTING;
    return true;
}

bool qa_application_events_checkpoint(qa_application *app, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size)
        return application_fail(error, QA_ERROR_ARGUMENT, "Application event checkpoint output must be empty");
    if (!public_lease(app, error)) return false;
    bool ok = application_events_save_capture(app, out, error);
    app->operation = APPLICATION_IDLE;
    return ok;
}

bool qa_application_events_restore(qa_application *app, qa_bytes bytes, qa_error *error)
{
    if (!public_lease(app, error)) return false;
    bool ok = application_events_save_restore(app, bytes, error) &&
        application_native_q1_wire_reconnect(app, error);
    for (size_t i = 0; ok && i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine)
            ok = application_native_q2_resources_reconnect(provider->state.native.q2_engine, error);
    }
    if (ok) ok = application_unified_events_restore_finish(app, error);
    app->operation = APPLICATION_IDLE;
    return ok;
}
