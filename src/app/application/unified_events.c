#include "internal.h"
#include "unified_events.h"
#include "unified_output_json.h"
#include "map_players_private.h"
#include "unified_output.h"
#include "qa/json.h"
#include "guest_q3_components.h"
#include "qa/application_native_q2_presentation.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool payload_actor_read(qa_application *, qa_bytes, bool,
    application_unified_json *, qa_error *);

static application_provider *source_provider(qa_application *app, qa_actor_owner owner)
{
    for (size_t i = 0; app && i < app->routing_provider_count; ++i) {
        application_provider *p = app->routing_providers[i];
        if (p->owner == owner) return p->close_pending ? NULL : p;
    }
    for (size_t i = 0; app && i < app->provider_count; ++i) {
        application_provider *p = app->providers[i];
        if (p->owner == owner) return p->close_pending ? NULL : p;
    }
    for (application_provider *p = app ? app->live_providers : NULL; p; p = p->next_live)
        if (p->owner == owner && !p->close_pending) return p;
    return NULL;
}

bool application_unified_event_source_read(qa_application *app, qa_actor_owner owner,
    application_unified_event_source *out, qa_error *error)
{
    if (!app || !owner || !out) return application_fail(error, QA_ERROR_ARGUMENT, "Source event receipt has no actual owner");
    application_provider *provider = source_provider(app, owner);
    if (provider && provider->launch && provider->product) {
        *out = (application_unified_event_source){owner, provider->launch, provider->product,
            provider->launch->content, provider->launch->selection.clock.kind, false};
        return true;
    }
    application_q3_component_publication component;
    if (!application_q3_components_event_source_read(app, owner, &component, error)) return false;
    if (!component.descriptor || !component.product || !component.content || !component.catalog)
        return application_fail(error, QA_ERROR_ARGUMENT, "Component Source event lost its retained content owner");
    *out = (application_unified_event_source){owner, component.descriptor, component.product,
        component.content, QA_CLOCK_Q3, true};
    return true;
}

size_t application_unified_event_resource_count(const qa_application *app)
{ return app ? app->unified_event_resource_count : 0; }

static bool bind_owner(qa_application *app, qa_actor_owner owner, const qa_product *product,
    bool unowned, qa_error *error)
{
    if (!product || !product->identity || app->unified_persistent_revision == UINT64_MAX ||
        (!unowned && app->unified_event_owner_generation >= QA_UNIFIED_SAFE_INTEGER - 1))
        return application_fail(error, QA_ERROR_ARGUMENT, "Presentation activation lost its actual product or generation");
    qa_string_id content;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), product->identity, &content, error)) return false;
    size_t index = 0;
    while (index < app->unified_event_owner_count && app->unified_event_owners[index].provider != owner) ++index;
    if (index < app->unified_event_owner_count && app->unified_event_owners[index].active)
        return application_fail(error, QA_ERROR_ARGUMENT, "Presentation Source is already activated");
    if (index == app->unified_event_owner_capacity) {
        size_t capacity = index ? index * 2 : 16;
        if (capacity < index || capacity > SIZE_MAX / sizeof(*app->unified_event_owners))
            return application_fail(error, QA_ERROR_MEMORY, "Presentation owner extent exhausted");
        void *rows = realloc(app->unified_event_owners, capacity * sizeof(*app->unified_event_owners));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Presentation Source activation");
        app->unified_event_owners = rows; app->unified_event_owner_capacity = capacity;
    }
    uint64_t generation = unowned ? 0 : app->unified_event_owner_generation + 1;
    app->unified_event_owners[index] = (application_unified_event_owner){owner, content, generation, true};
    if (index == app->unified_event_owner_count) ++app->unified_event_owner_count;
    if (!unowned) app->unified_event_owner_generation = generation;
    ++app->unified_persistent_revision;
    return true;
}

bool application_unified_event_owner_bind(qa_application *app, application_provider *provider,
    bool restoring, qa_error *error)
{
    if (!app || !provider || provider->application != app || !provider->owner || !provider->launch ||
        source_provider(app, provider->owner) != provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Presentation activation requires its actual constructing Source");
    /* The actual no-Init restore imports its original tokens later. */
    if (restoring) return true;
    const qa_product *product = provider->product;
    qa_catalog *catalog = qa_launch_instance_catalog(provider->launch);
    if (!product && catalog) product = qa_catalog_product(catalog, provider->launch->selection.product);
    /* Ordinary selected gameplay emits through the shared Source stream.
     * Only the actual component constructor creates a PresentationOwner. */
    if (!bind_owner(app, provider->owner, product, true, error)) return false;
    provider->event_activation_bound = true;
    return true;
}

bool application_unified_event_owner_bound_is(const qa_application *app, const application_provider *provider)
{
    if (!app || !provider || provider->application != app || !provider->product || !provider->product->identity ||
        provider->event_activation_deferred) return false;
    bool published = false;
    for (size_t i = 0; i < app->provider_count; ++i) published |= app->providers[i] == provider;
    if (!published) return false;
    for (size_t i = 0; i < app->unified_event_owner_count; ++i) {
        const application_unified_event_owner *activation = app->unified_event_owners + i;
        if (activation->provider != provider->owner || !activation->active || activation->generation) continue;
        const char *content = qa_strings_cstr(qa_session_strings(app->session), activation->content);
        return content && !strcmp(content, provider->product->identity);
    }
    return false;
}

bool application_unified_event_owner_prepare(qa_application *app, application_provider *provider,
    application_provider *previous, qa_error *error)
{
    if (!previous) return application_unified_event_owner_bind(app, provider, false, error);
    if (!previous->event_activation_bound && application_unified_event_owner_bound_is(app, previous))
        previous->event_activation_bound = true;
    if (!app || !provider || provider == previous || provider->application != app ||
        previous->application != app || !provider->owner || provider->owner != previous->owner ||
        !provider->launch || !previous->launch || provider->constructed || previous->close_pending ||
        !previous->constructed || !previous->attached || !previous->event_activation_bound ||
        provider->event_activation_deferred || provider->event_activation_bound ||
        source_provider(app, provider->owner) != provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source activation replacement lost its actual provider pair");
    bool published = false;
    for (size_t i = 0; i < app->provider_count; ++i) published |= app->providers[i] == previous;
    if (!published || provider->launch->selection.runtime != QA_PROGRAM_BUILTIN ||
        previous->launch->selection.runtime != QA_PROGRAM_BUILTIN)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Deferred Source activation requires an actual builtin replacement");
    const qa_product *product = qa_catalog_product(qa_launch_instance_catalog(provider->launch),
        provider->launch->selection.product);
    const application_unified_event_owner *activation = NULL;
    for (size_t i = 0; i < app->unified_event_owner_count; ++i)
        if (app->unified_event_owners[i].provider == previous->owner && app->unified_event_owners[i].active)
            activation = app->unified_event_owners + i;
    const char *content = activation ? qa_strings_cstr(qa_session_strings(app->session), activation->content) : NULL;
    qa_string_id next_content;
    if (!product || !product->identity || !previous->product || !content || activation->generation ||
        strcmp(content, previous->product->identity) || app->unified_persistent_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source activation replacement lost its published content receipt");
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), product->identity, &next_content, error)) return false;
    provider->event_activation_deferred = true;
    return true;
}

bool application_unified_event_owner_publish(qa_application *app, application_provider *provider,
    application_provider *previous, qa_error *error)
{
    if (!app || !provider || !previous || !provider->event_activation_deferred ||
        provider->application != app || previous->application != app || provider->owner != previous->owner ||
        !provider->constructed || previous->constructed || previous->attached || previous->component_attached ||
        previous->policy_attached || previous->event_activation_bound)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source activation handoff requires its genuinely retired previous provider");
    if (!application_unified_event_owner_bind(app, provider, false, error)) return false;
    provider->event_activation_deferred = false;
    return true;
}

bool application_unified_event_component_owner_bind(qa_application *app, qa_actor_owner owner,
    bool restoring, qa_error *error)
{
    application_unified_event_source source;
    if (!application_unified_event_source_read(app, owner, &source, error)) return false;
    if (!source.component)
        return application_fail(error, QA_ERROR_ARGUMENT, "PresentationOwner requires its actual component Source");
    return restoring || bind_owner(app, owner, source.product, false, error);
}

const application_unified_event_resource *application_unified_event_resource_at(
    const qa_application *app, size_t index)
{
    return app && index < app->unified_event_resource_count ? app->unified_event_resources + index : NULL;
}

const qa_resource *application_unified_event_resource_read(const qa_application *app, const char *id)
{
    for (size_t i = 0; app && id && i < app->unified_event_resource_count; ++i)
        if (!strcmp(app->unified_event_resources[i].id, id)) return app->unified_event_resources[i].resource;
    return NULL;
}

bool application_unified_event_resource_lookup_receipt(qa_application *app, qa_actor_owner owner,
    qa_native_host_resource_kind kind, const char *path, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], uint64_t *custody,
    bool *found, qa_error *error)
{
    application_unified_event_source source;
    if (!application_unified_event_source_read(app, owner, &source, error) ||
        (unsigned)kind > QA_NATIVE_HOST_IMAGE || !path || !id || !custody || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source sound lookup lost its actual registration owner");
    id[0] = 0; *custody = 0; *found = false;
    for (size_t i = 0; i < app->unified_event_registration_count; ++i) {
        const application_unified_event_registration *row = app->unified_event_registrations + i;
        const char *registered = qa_strings_cstr(qa_session_strings(app->session), row->path);
        if (row->provider != owner || row->kind != kind || !registered || strcmp(registered, path)) continue;
        if (row->resource >= app->unified_event_resource_count ||
            row->custody > app->unified_event_resources[row->resource].custody_count)
            return application_fail(error, QA_ERROR_FORMAT, "Source sound registration lost its retained resource");
        memcpy(id, app->unified_event_resources[row->resource].id, QA_APPLICATION_RESOURCE_KEY_CAPACITY);
        *custody = row->custody;
        *found = true; return true;
    }
    return true;
}

bool application_unified_event_resource_lookup_kind(qa_application *app, qa_actor_owner owner,
    qa_native_host_resource_kind kind, const char *path, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], bool *found, qa_error *error)
{
    uint64_t custody;
    return application_unified_event_resource_lookup_receipt(app, owner, kind, path, id, &custody, found, error);
}

bool application_unified_event_resource_receipt_read(const qa_application *app, const char *id,
    uint64_t custody, const qa_resource **resource, const qa_vfs **view,
    const qa_vfs_acquisition **opening, qa_error *error)
{
    if (!app || !id || !resource || !view || !opening)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source resource receipt has no actual output");
    for (size_t i = 0; i < app->unified_event_resource_count; ++i) {
        const application_unified_event_resource *row = app->unified_event_resources + i;
        if (strcmp(row->id, id)) continue;
        if (custody > row->custody_count)
            return application_fail(error, QA_ERROR_FORMAT, "Source resource receipt has no captured opening");
        if (custody) {
            const application_unified_event_resource_custody *held = row->custodies + (size_t)custody - 1;
            *resource = held->resource; *view = held->view; *opening = &held->opening;
        } else {
            *resource = row->resource; *view = row->view; *opening = &row->opening;
        }
        return *resource && *view && (*opening)->opening_present &&
            (*opening)->resource_id == qa_resource_id(*resource) &&
            qa_resource_pool_find(qa_vfs_resources(*view), qa_resource_id(*resource)) == *resource &&
            qa_vfs_acquisition_retained(*view, *opening, error);
    }
    return application_fail(error, QA_ERROR_FORMAT, "Source resource receipt lost its immutable key");
}

bool application_unified_event_resource_lookup(qa_application *app, qa_actor_owner owner,
    const char *path, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], bool *found, qa_error *error)
{ return application_unified_event_resource_lookup_kind(app, owner, QA_NATIVE_HOST_SOUND, path, id, found, error); }

bool application_unified_event_registration_clear(qa_application *app, qa_actor_owner owner, qa_error *error)
{
    if (!app || !owner) return application_fail(error, QA_ERROR_ARGUMENT, "Source registration retirement has no actual owner");
    bool changed = false;
    for (size_t i = 0; i < app->unified_event_registration_count; ++i)
        if (app->unified_event_registrations[i].provider == owner) { changed = true; break; }
    if (!changed) return true;
    if (app->unified_event_registration_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Source registration revision is exhausted");
    size_t kept = 0;
    for (size_t i = 0; i < app->unified_event_registration_count; ++i)
        if (app->unified_event_registrations[i].provider != owner)
            app->unified_event_registrations[kept++] = app->unified_event_registrations[i];
    app->unified_event_registration_count = kept;
    ++app->unified_event_registration_revision;
    return true;
}

static bool registration_bind(qa_application *app, qa_actor_owner owner, qa_native_host_resource_kind kind,
    const char *path, size_t resource, uint64_t custody, qa_error *error)
{
    qa_string_id name;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), path, &name, error)) return false;
    size_t index = 0;
    while (index < app->unified_event_registration_count &&
        (app->unified_event_registrations[index].provider != owner ||
         app->unified_event_registrations[index].path != name ||
         app->unified_event_registrations[index].kind != kind)) ++index;
    if (index < app->unified_event_registration_count &&
        app->unified_event_registrations[index].resource == resource &&
        app->unified_event_registrations[index].custody == custody) return true;
    if (app->unified_event_registration_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Source registration revision is exhausted");
    if (index == app->unified_event_registration_capacity) {
        size_t capacity = index ? index * 2 : 32;
        if (capacity < index || capacity > SIZE_MAX / sizeof(*app->unified_event_registrations))
            return application_fail(error, QA_ERROR_MEMORY, "Source registration extent overflows");
        void *rows = realloc(app->unified_event_registrations, capacity * sizeof(*app->unified_event_registrations));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Source registrations");
        app->unified_event_registrations = rows; app->unified_event_registration_capacity = capacity;
    }
    app->unified_event_registrations[index] = (application_unified_event_registration){owner, name, resource, kind, custody};
    if (index == app->unified_event_registration_count) ++app->unified_event_registration_count;
    ++app->unified_event_registration_revision;
    return true;
}

void application_unified_events_resources_dispose(qa_application *app)
{
    if (!app) return;
    for (size_t i = 0; i < app->unified_event_resource_count; ++i) {
        application_unified_event_resource *row = app->unified_event_resources + i;
        qa_buffer_free(&row->key);
        qa_resource_release(row->resource);
        qa_launch_instance_lease_release(row->descriptor);
        qa_vfs_acquisition_dispose(&row->opening); qa_vfs_destroy(row->view);
        for (size_t j = 0; row->custodies && j < row->custody_count; ++j) {
            application_unified_event_resource_custody *held = row->custodies + j;
            qa_resource_release(held->resource); qa_vfs_acquisition_dispose(&held->opening);
            qa_vfs_destroy(held->view); qa_resource_pool_destroy(held->pool);
        }
        free(row->custodies);
        qa_resource_pool_destroy(row->pool);
    }
    free(app->unified_event_resources);
    free(app->unified_event_registrations);
    app->unified_event_registrations = NULL;
    app->unified_event_registration_count = app->unified_event_registration_capacity = 0;
    app->unified_event_resources = NULL;
    app->unified_event_resource_count = app->unified_event_resource_capacity = 0;
}

bool application_unified_event_resource_register(qa_application *app, qa_actor_owner owner,
    const char *path, const qa_resource *resource, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], qa_error *error)
{
    application_unified_event_source source;
    if (!application_unified_event_source_read(app, owner, &source, error) || !path || !*path || !resource || !id)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source registration has no genuine held resource");
    const qa_vfs *view = source.content;
    for (size_t i = 0; i < qa_vfs_read_count(view); ++i) {
        qa_vfs_read_reference actual;
        if (qa_vfs_read_at(view, i, &actual) && actual.resource == resource && actual.path &&
            (!strcmp(actual.path, path) || (!strncmp(actual.path, "sound/", 6) && !strcmp(actual.path + 6, path)) ||
             (path[0] == '#' && !strcmp(actual.path, path + 1)))) {
            qa_vfs_acquisition opening = {.mount = actual.mount, .resource_id = qa_resource_id(resource),
                .path = (char *)actual.path, .lookup_path = (char *)actual.lookup_path,
                .link_source = (char *)actual.link_source, .link_target = (char *)actual.link_target,
                .opening = actual.opening, .opening_present = true};
            return application_unified_event_resource_register_acquired(app, owner, QA_NATIVE_HOST_SOUND,
                path, view, resource, &opening, id, error);
        }
    }
    return application_fail(error, QA_ERROR_FORMAT, "Source registration is outside its actual precache opening");
}

static bool opening_equal(const qa_vfs_acquisition *a, const qa_vfs_acquisition *b)
{
    const char *left[] = {a->path, a->lookup_path, a->link_source, a->link_target, a->opening.prefix};
    const char *right[] = {b->path, b->lookup_path, b->link_source, b->link_target, b->opening.prefix};
    if (a->mount != b->mount || a->resource_id != b->resource_id ||
        a->opening_present != b->opening_present || a->opening.rank != b->opening.rank ||
        a->opening.user_overlay != b->opening.user_overlay || a->opening.order_count != b->opening.order_count) return false;
    for (size_t i = 0; i < 5; ++i)
        if ((left[i] != NULL) != (right[i] != NULL) || (left[i] && strcmp(left[i], right[i]))) return false;
    return !a->opening.order_count ||
        !memcmp(a->opening.order, b->opening.order, a->opening.order_count * sizeof(*a->opening.order));
}

static bool custody_retain(application_unified_event_resource *row, const qa_vfs *view,
    const qa_resource *resource, const qa_vfs_acquisition *opening, uint64_t *index, qa_error *error)
{
    if (row->view == view && row->resource == resource && opening_equal(&row->opening, opening)) {
        *index = 0; return true;
    }
    for (size_t i = 0; i < row->custody_count; ++i)
        if (row->custodies[i].view == view && row->custodies[i].resource == resource &&
            opening_equal(&row->custodies[i].opening, opening)) { *index = i + 1; return true; }
    if (row->custody_count == row->custody_capacity) {
        size_t capacity = row->custody_capacity ? row->custody_capacity * 2 : 4;
        if (capacity <= row->custody_capacity || capacity > SIZE_MAX / sizeof(*row->custodies)) {
            application_fail(error, QA_ERROR_MEMORY, "Source resource opening extent overflows");
            return false;
        }
        void *values = realloc(row->custodies, capacity * sizeof(*row->custodies));
        if (!values) {
            application_fail(error, QA_ERROR_MEMORY, "Retaining Source resource openings");
            return false;
        }
        row->custodies = values; row->custody_capacity = capacity;
    }
    application_unified_event_resource_custody held = {0};
    if (!qa_vfs_acquisition_copy(opening, &held.opening, error) || !qa_vfs_retain((qa_vfs *)view, error)) {
        qa_vfs_acquisition_dispose(&held.opening); return false;
    }
    held.view = (qa_vfs *)view; held.resource = (qa_resource *)resource; held.pool = qa_vfs_resources(view);
    qa_resource_retain(held.resource); qa_resource_pool_retain(held.pool);
    row->custodies[row->custody_count++] = held; *index = row->custody_count;
    return true;
}

bool application_unified_event_resource_register_acquired(qa_application *app, qa_actor_owner owner,
    qa_native_host_resource_kind kind, const char *path, const qa_vfs *view, const qa_resource *resource,
    const qa_vfs_acquisition *opening, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], qa_error *error)
{
    application_unified_event_source source;
    if (!application_unified_event_source_read(app, owner, &source, error) || (unsigned)kind > QA_NATIVE_HOST_IMAGE ||
        !path || !*path || !view || !resource || !opening || !id ||
        opening->resource_id != qa_resource_id(resource) || !opening->opening_present ||
        !qa_vfs_lookup_equal(view, source.content) || !qa_vfs_acquisition_retained(view, opening, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source acquired registration lost its genuine scoped opening");
    qa_resource_pool *pool = qa_vfs_resources(view);
    const char *registration_path = opening->path;
    if (!pool || qa_resource_pool_find(pool, qa_resource_id(resource)) != resource)
        return application_fail(error, QA_ERROR_FORMAT, "Source acquired registration is outside its retained resource pool");
    qa_unified_document *key = NULL;
    char actual_id[QA_APPLICATION_RESOURCE_KEY_CAPACITY];
    if (!application_unified_resource_key(source.product, registration_path, resource, &key, actual_id, error)) return false;
    for (size_t i = 0; i < app->unified_event_resource_count; ++i) {
        if (!strcmp(app->unified_event_resources[i].id, actual_id)) {
            uint64_t custody;
            bool ok = custody_retain(app->unified_event_resources + i, view, resource, opening, &custody, error) &&
                registration_bind(app, owner, kind, path, i, custody, error);
            if (ok) memcpy(id, actual_id, QA_APPLICATION_RESOURCE_KEY_CAPACITY);
            qa_unified_document_destroy(key);
            return ok;
        }
    }
    application_unified_event_resource row = {.provider = owner, .resource = (qa_resource *)resource, .pool = pool};
    qa_bytes bytes = qa_json_source(qa_unified_document_json(key), qa_unified_document_root(key));
    row.key.data = malloc(bytes.size);
    bool ok = row.key.data != NULL &&
        qa_strings_intern_cstr(qa_session_strings(app->session), registration_path, &row.path, error) &&
        qa_strings_intern_cstr(qa_session_strings(app->session), source.product->identity, &row.content, error) &&
        qa_launch_instance_retain_metadata(source.descriptor, &row.descriptor, error);
    if (!row.key.data) application_fail(error, QA_ERROR_MEMORY, "Retaining Source resource key");
    if (ok && app->unified_event_resource_count == app->unified_event_resource_capacity) {
        size_t capacity = app->unified_event_resource_capacity ? app->unified_event_resource_capacity * 2 : 32;
        if (capacity < app->unified_event_resource_capacity || capacity > SIZE_MAX / sizeof(row))
            ok = application_fail(error, QA_ERROR_MEMORY, "Source resource dictionary extent overflows");
        else {
            void *rows = realloc(app->unified_event_resources, capacity * sizeof(row));
            if (!rows) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining Source resource dictionary");
            else { app->unified_event_resources = rows; app->unified_event_resource_capacity = capacity; }
        }
    }
    if (ok) {
        ok = qa_vfs_acquisition_copy(opening, &row.opening, error) && qa_vfs_retain((qa_vfs *)view, error);
        if (ok) row.view = (qa_vfs *)view;
    }
    if (ok) {
        memcpy(row.key.data, bytes.data, bytes.size); row.key.size = bytes.size;
        memcpy(row.id, actual_id, QA_APPLICATION_RESOURCE_KEY_CAPACITY);
        qa_resource_retain(row.resource); qa_resource_pool_retain(pool);
        size_t index = app->unified_event_resource_count++;
        app->unified_event_resources[index] = row;
        ok = registration_bind(app, owner, kind, path, index, 0, error);
        if (ok) memcpy(id, actual_id, QA_APPLICATION_RESOURCE_KEY_CAPACITY);
    } else {
        qa_buffer_free(&row.key); qa_launch_instance_lease_release(row.descriptor);
        qa_vfs_acquisition_dispose(&row.opening); qa_vfs_destroy(row.view);
    }
    qa_unified_document_destroy(key);
    return ok;
}

bool application_unified_world_text_emit(qa_application *app, qa_actor_owner owner,
    const qa_q2_map_event *event, qa_error *error)
{
    application_provider *provider = source_provider(app, owner);
    qa_clock_state clock;
    if (!provider || !event || event->kind != QA_Q2_MAP_WORLD_TEXT ||
        !qa_session_clock(app->session, owner, &clock) || !event->text ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->direction) || !qa_vec_finite(event->color) ||
        !isfinite(event->alpha) || !isfinite(event->value) || event->value <= 0 ||
        !isfinite(event->duration) || event->duration < 0 || app->unified_world_text_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "World text has no actual Source clock or valid geometry");
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (primary) {
        qa_clock_state source_clock;
        if (!qa_session_clock(app->session, primary->owner, &source_clock))
            return application_fail(error, QA_ERROR_ARGUMENT, "World text lost its primary Source clock");
        clock = source_clock;
    }
    if (app->unified_world_text_count == app->unified_world_text_capacity) {
        size_t capacity = app->unified_world_text_capacity ? app->unified_world_text_capacity * 2 : 32;
        if (capacity < app->unified_world_text_capacity || capacity > SIZE_MAX / sizeof(*app->unified_world_text))
            return application_fail(error, QA_ERROR_MEMORY, "World text extent overflows");
        void *rows = realloc(app->unified_world_text, capacity * sizeof(*app->unified_world_text));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining genuine world text");
        app->unified_world_text = rows; app->unified_world_text_capacity = capacity;
    }
    application_unified_world_text row = {.provider = owner, .text = event->text,
        .origin = event->origin, .angles = event->direction, .color = event->color,
        .alpha = event->alpha, .cell_size = event->value, .timed = event->duration > 0,
        .expires = (double)clock.frame.time_ns / 1e9 + event->duration,
        .billboard = (event->flags & 2u) != 0, .depth_test = (event->flags & 1u) != 0};
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), provider->product->identity, &row.content, error)) return false;
    double now = (double)clock.frame.time_ns / 1e9;
    size_t count = app->unified_world_text_map == app->map_revision ? app->unified_world_text_count : 0;
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i)
        if (!app->unified_world_text[i].timed || app->unified_world_text[i].expires > now)
            app->unified_world_text[kept++] = app->unified_world_text[i];
    app->unified_world_text_count = kept;
    app->unified_world_text_map = app->map_revision;
    app->unified_world_text[app->unified_world_text_count++] = row;
    ++app->unified_world_text_revision;
    return true;
}

bool application_unified_event_payload_valid(qa_bytes bytes, bool presentation,
    bool link_presentation, qa_error *error)
{
    if (!bytes.size) return true;
    qa_unified_document *document = NULL;
    if (!qa_unified_document_create(QA_UNIFIED_CHECKPOINT, bytes, &document, error)) return false;
    const qa_json_document *json = qa_unified_document_json(document);
    qa_json_id root = qa_unified_document_root(document);
    bool okay = qa_json_type(json, root) == QA_JSON_OBJECT &&
        qa_json_type(json, qa_json_get(json, root, "kind")) == QA_JSON_STRING;
    if (presentation) {
        static const char *const reserved[] = {"sequence", "seconds", "content", "recipient", "sourceEntity", "owner", "source"};
        for (size_t i = 0; okay && i < sizeof(reserved) / sizeof(*reserved); ++i)
            okay = qa_json_get(json, root, reserved[i]) == QA_JSON_NONE;
    } else {
        qa_json_id kind = qa_json_get(json, root, "kind");
        okay = okay && (qa_json_string_equal(json, kind, "sound") || qa_json_string_equal(json, kind, "damage") ||
            qa_json_string_equal(json, kind, "transition") || qa_json_string_equal(json, kind, "message")) &&
            qa_json_get(json, root, "sourcePresentationSequence") == QA_JSON_NONE &&
            (!link_presentation || qa_json_string_equal(json, kind, "message"));
    }
    qa_unified_document_destroy(document);
    return okay || application_fail(error, QA_ERROR_FORMAT, "Source event payload has no genuine kind or replaces its owner envelope");
}

static bool retain_payload(qa_application *app, qa_bytes input, qa_bytes *out, qa_error *error)
{
    if (!input.size) { *out = (qa_bytes){0}; return true; }
    uint8_t *copy = qa_arena_alloc(&app->event_arena, input.size, 1, error);
    if (!copy) return false;
    memcpy(copy, input.data, input.size);
    *out = (qa_bytes){copy, input.size};
    return true;
}

bool application_unified_persistent_key(qa_application *app,
    const application_unified_event_record *row, qa_buffer *out, bool *remove, qa_error *error)
{
    *remove = false;
    if (!row->presentation.size) return true;
    qa_json_document *json = NULL;
    if (!qa_json_parse(row->presentation, &json, error)) return false;
    qa_json_id root = qa_json_root(json), kind = qa_json_get(json, root, "kind");
    qa_json_id event = qa_json_get(json, root, "event"), action = qa_json_get(json, event, "kind");
    bool q1 = qa_json_string_equal(json, kind, "q1"), q2 = qa_json_string_equal(json, kind, "q2");
    const char *domain = NULL;
    bool unique = q1 && (qa_json_string_equal(json, action, "ambient") || qa_json_string_equal(json, action, "static-model"));
    bool loop = q2 && qa_json_string_equal(json, action, "sound") && !qa_json_string_equal(json, qa_json_get(json, event, "loop"), "once");
    bool client = qa_json_string_equal(json, kind, "q1-client");
    bool style = (q1 || q2) && qa_json_string_equal(json, action, "lightstyle");
    if (qa_json_string_equal(json, kind, "q1-sky")) domain = "sky";
    else if (client) domain = "client";
    else if (style) domain = "style";
    else if ((q1 || qa_json_string_equal(json, kind, "q1-level")) && qa_json_string_equal(json, action, "finale")) domain = "finale";
    else if (qa_json_string_equal(json, kind, "music")) domain = qa_json_string_equal(json, action, "pause") ? "music:pause" : "music:track";
    else if (q2 && qa_json_string_equal(json, action, "music")) domain = "music:track";
    if (!domain && !unique && !loop) { qa_json_destroy(json); return true; }
    application_unified_json key = {0};
    bool ok = application_unified_json_text(&key, "[", error) &&
        (row->owner_generation ? application_unified_json_string(&key, qa_strings_cstr(qa_session_strings(app->session), row->provider), error) :
            application_unified_json_text(&key, "null", error)) &&
        application_unified_json_text(&key, ",", error) && application_unified_json_natural(&key, row->owner_generation, error) &&
        application_unified_json_text(&key, ",", error) &&
        application_unified_json_string(&key, unique ? "unique" : loop ? "sound" : domain, error);
    if (ok && unique) ok = application_unified_json_text(&key, ",", error) &&
        application_unified_json_natural(&key, row->presentation_sequence, error);
    if (ok && client) ok = application_unified_json_text(&key, ",", error) &&
        application_unified_json_string(&key, qa_strings_cstr(qa_session_strings(app->session), row->content), error) &&
        application_unified_json_text(&key, ",", error) &&
        application_unified_json_append(&key, qa_json_source(json, qa_json_get(json, event, "slot")), error) &&
        application_unified_json_text(&key, ",", error) && application_unified_json_append(&key, qa_json_source(json, action), error);
    if (ok && style) ok = application_unified_json_text(&key, ",", error) &&
        application_unified_json_append(&key, qa_json_source(json, qa_json_get(json, event, "style")), error);
    if (ok && loop) {
        const char *fields[] = {"loopOwner", "actor", "channel", "path"};
        for (size_t i = 0; ok && i < 4; ++i) {
            qa_json_id value = qa_json_get(json, event, fields[i]);
            ok = application_unified_json_text(&key, ",", error) &&
                (value == QA_JSON_NONE ? application_unified_json_text(&key, "null", error) :
                 application_unified_json_append(&key, qa_json_source(json, value), error));
        }
        *remove = qa_json_string_equal(json, qa_json_get(json, event, "loop"), "stop");
    }
    if (ok) ok = application_unified_json_text(&key, ",", error) &&
        (row->recipient.registry ? application_unified_json_actor(&key, row->payload_checkpoint ?
            (qa_actor_id){row->recipient.registry, row->recipient_saved.generation, row->recipient_saved.slot} : row->recipient, error) :
         application_unified_json_text(&key, "null", error)) && application_unified_json_text(&key, "]", error);
    qa_json_destroy(json);
    qa_json_document *admitted = NULL;
    if (ok) ok = qa_json_parse((qa_bytes){key.bytes.data, key.bytes.size}, &admitted, error);
    qa_json_destroy(admitted);
    if (ok) { *out = key.bytes; key.bytes = (qa_buffer){0}; }
    application_unified_json_dispose(&key);
    return ok;
}

void application_unified_persistent_dispose(qa_application *app)
{
    for (size_t i = 0; i < app->unified_persistent_count; ++i) {
        qa_buffer_free(&app->unified_persistent[i].key);
        qa_buffer_free(&app->unified_persistent[i].payload);
    }
    free(app->unified_persistent);
    app->unified_persistent = NULL;
    app->unified_persistent_count = app->unified_persistent_capacity = 0;
}

bool application_unified_persistent_retire(qa_application *app, qa_actor_owner owner,
    qa_actor_id recipient, qa_error *error)
{
    if (!app || (!owner && !recipient.registry)) return application_fail(error, QA_ERROR_ARGUMENT, "Presentation retirement requires its actual owner or actor");
    size_t matches = 0;
    for (size_t i = 0; i < app->unified_persistent_count; ++i) {
        qa_actor_id actual;
        if (!application_unified_event_recipient(app, &app->unified_persistent[i].event, false, &actual, error)) return false;
        matches += owner ? app->unified_persistent[i].event.provider == owner : qa_actor_id_equal(actual, recipient);
    }
    application_unified_event_owner *activation = NULL;
    for (size_t i = 0; owner && i < app->unified_event_owner_count; ++i)
        if (app->unified_event_owners[i].provider == owner && app->unified_event_owners[i].active)
            activation = app->unified_event_owners + i;
    if (!matches && !activation) return true;
    if (app->unified_persistent_revision == UINT64_MAX) return application_fail(error, QA_ERROR_FORMAT, "Presentation revision exhausted");
    size_t kept = 0;
    for (size_t i = 0; i < app->unified_persistent_count; ++i) {
        application_unified_persistent_event row = app->unified_persistent[i];
        qa_actor_id actual;
        if (!application_unified_event_recipient(app, &row.event, false, &actual, error)) return false;
        if (owner ? row.event.provider == owner : qa_actor_id_equal(actual, recipient)) {
            qa_buffer_free(&row.key); qa_buffer_free(&row.payload);
        } else app->unified_persistent[kept++] = row;
    }
    app->unified_persistent_count = kept;
    if (activation) activation->active = false;
    ++app->unified_persistent_revision;
    return true;
}

static bool persistent_current_key(qa_application *app,
    const application_unified_persistent_event *row, qa_buffer *key, qa_error *error)
{
    application_unified_event_record current = row->event;
    application_unified_json payload = {0};
    bool remove = false, ok = true;
    if (current.payload_checkpoint) {
        ok = payload_actor_read(app, current.presentation, true, &payload, error) &&
            application_unified_event_recipient(app, &current, false, &current.recipient, error);
        current.presentation = (qa_bytes){payload.bytes.data, payload.bytes.size};
        current.payload_checkpoint = false;
    }
    if (ok) ok = application_unified_persistent_key(app, &current, key, &remove, error);
    application_unified_json_dispose(&payload);
    return ok;
}

static bool persistent_record(qa_application *app, const application_unified_event_record *event, qa_error *error)
{
    qa_buffer key = {0}; bool remove = false;
    if (!application_unified_persistent_key(app, event, &key, &remove, error)) return false;
    if (!key.size) return true;
    size_t index = 0;
    while (index < app->unified_persistent_count) {
        const application_unified_persistent_event *previous = app->unified_persistent + index;
        qa_buffer current_key = {0};
        bool ok = !previous->event.payload_checkpoint || persistent_current_key(app, previous, &current_key, error);
        const qa_buffer *candidate = previous->event.payload_checkpoint ? &current_key : &previous->key;
        bool same = ok && key.size == candidate->size && !memcmp(key.data, candidate->data, key.size);
        qa_buffer_free(&current_key);
        if (!ok) { qa_buffer_free(&key); return false; }
        if (same) break;
        ++index;
    }
    if (remove && index == app->unified_persistent_count) { qa_buffer_free(&key); return true; }
    if (app->unified_persistent_revision == UINT64_MAX) { qa_buffer_free(&key); return application_fail(error, QA_ERROR_FORMAT, "Presentation revision exhausted"); }
    qa_buffer payload = {0};
    if (!remove) {
        payload.data = malloc(event->presentation.size); payload.size = event->presentation.size;
        if (!payload.data) { qa_buffer_free(&key); return application_fail(error, QA_ERROR_MEMORY, "Retaining persistent Source presentation"); }
        memcpy(payload.data, event->presentation.data, payload.size);
        if (index == app->unified_persistent_capacity) {
            size_t capacity = index ? index * 2 : 32;
            if (capacity < index || capacity > SIZE_MAX / sizeof(*app->unified_persistent)) { qa_buffer_free(&payload); qa_buffer_free(&key); return application_fail(error, QA_ERROR_MEMORY, "Persistent presentation extent exhausted"); }
            void *rows = realloc(app->unified_persistent, capacity * sizeof(*app->unified_persistent));
            if (!rows) { qa_buffer_free(&payload); qa_buffer_free(&key); return application_fail(error, QA_ERROR_MEMORY, "Retaining persistent presentation slots"); }
            app->unified_persistent = rows; app->unified_persistent_capacity = capacity;
        }
    }
    if (index < app->unified_persistent_count) {
        qa_buffer_free(&app->unified_persistent[index].key); qa_buffer_free(&app->unified_persistent[index].payload);
        memmove(app->unified_persistent + index, app->unified_persistent + index + 1,
            (--app->unified_persistent_count - index) * sizeof(*app->unified_persistent));
    }
    if (remove) qa_buffer_free(&key);
    else {
        application_unified_persistent_event row = {.event = *event, .key = key, .payload = payload};
        row.event.presentation = (qa_bytes){payload.data, payload.size};
        row.event.simulation = (qa_bytes){0}; row.event.simulation_sequence = 0; row.event.link_presentation = false;
        app->unified_persistent[app->unified_persistent_count++] = row;
    }
    ++app->unified_persistent_revision;
    return true;
}

/* Compare actual persistent domains, excluding the owning activation token.
 * Slot comparison additionally includes the real addressed recipient. */
static bool persistent_domain_equal(const qa_buffer *a, const qa_buffer *b, bool slot,
    bool *equal, qa_error *error)
{
    qa_json_document *left = NULL, *right = NULL;
    bool ok = qa_json_parse((qa_bytes){a->data, a->size}, &left, error) &&
        qa_json_parse((qa_bytes){b->data, b->size}, &right, error);
    *equal = false;
    if (ok) {
        qa_json_id x = qa_json_root(left), y = qa_json_root(right);
        size_t count = qa_json_size(left, x);
        if (count >= 4 && count == qa_json_size(right, y) &&
            !qa_json_string_equal(left, qa_json_at(left, x, 2), "unique") &&
            !qa_json_string_equal(left, qa_json_at(left, x, 2), "sound")) {
            *equal = true;
            for (size_t n = 2; *equal && n < count - (slot ? 0 : 1); ++n) {
                qa_bytes p = qa_json_source(left, qa_json_at(left, x, n));
                qa_bytes q = qa_json_source(right, qa_json_at(right, y, n));
                *equal = p.size == q.size && !memcmp(p.data, q.data, p.size);
            }
        }
    }
    qa_json_destroy(left); qa_json_destroy(right);
    return ok;
}

bool application_unified_event_owner_retire(qa_application *app, qa_actor_owner owner,
    const qa_source_frame *clock, qa_error *error)
{
    if (!app || !owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Presentation retirement requires its actual owner");
    application_unified_event_owner *activation = NULL;
    for (size_t i = 0; i < app->unified_event_owner_count; ++i)
        if (app->unified_event_owners[i].provider == owner && app->unified_event_owners[i].active)
            activation = app->unified_event_owners + i;
    if (!activation || !activation->generation)
        return application_unified_persistent_retire(app, owner, (qa_actor_id){0}, error);
    if (!clock || !clock->provider || (unsigned)clock->kind > QA_CLOCK_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Presentation retirement requires the retained actual primary clock");
    size_t count = app->unified_persistent_count;
    size_t *winners = count ? malloc(count * sizeof(*winners)) : NULL;
    qa_buffer *keys = count ? calloc(count, sizeof(*keys)) : NULL;
    if (count && (!winners || !keys)) {
        free(winners); free(keys);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining actual presentation replacement slots");
    }
    size_t winner_count = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i)
        ok = persistent_current_key(app, app->unified_persistent + i, keys + i, error);
    for (size_t i = 0; ok && i < count; ++i) {
        const application_unified_persistent_event *row = app->unified_persistent + i;
        if (row->event.provider == owner) continue;
        bool affected = false;
        for (size_t n = 0; ok && !affected && n < count; ++n)
            if (app->unified_persistent[n].event.provider == owner)
                ok = persistent_domain_equal(keys + i, keys + n, false, &affected, error);
        if (!affected) continue;
        size_t match = 0;
        for (; ok && match < winner_count; ++match) {
            bool same;
            ok = persistent_domain_equal(keys + i, keys + winners[match], true, &same, error);
            if (ok && same) break;
        }
        if (ok) {
            if (match == winner_count) ++winner_count;
            winners[match] = i;
        }
    }
    /* The retained rows are chronological; replacing a slot can change its
     * position in winners, so emit the actual winners by original sequence. */
    for (size_t i = 1; i < winner_count; ++i) {
        size_t value = winners[i], n = i;
        while (n && app->unified_persistent[winners[n - 1]].event.presentation_sequence >
            app->unified_persistent[value].event.presentation_sequence) { winners[n] = winners[n - 1]; --n; }
        winners[n] = value;
    }
    size_t additions = winner_count + 1;
    if (ok && (additions > QA_UNIFIED_SAFE_INTEGER - app->unified_event_sequence ||
        additions > QA_UNIFIED_SAFE_INTEGER - app->presentation_event_sequence ||
        app->unified_persistent_revision == UINT64_MAX || additions > SIZE_MAX - app->unified_event_count))
        ok = application_fail(error, QA_ERROR_FORMAT, "Presentation retirement sequence domain exhausted");
    size_t required = ok ? app->unified_event_count + additions : 0;
    if (ok && required > app->unified_event_capacity) {
        if (required > SIZE_MAX / sizeof(*app->unified_events)) ok = application_fail(error, QA_ERROR_MEMORY, "Presentation retirement extent overflows");
        else {
            void *rows = realloc(app->unified_events, required * sizeof(*app->unified_events));
            if (!rows) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining actual Source retirement chronology");
            else { app->unified_events = rows; app->unified_event_capacity = required; }
        }
    }
    application_unified_json payload = {0};
    qa_bytes retired = {0};
    if (ok) ok = application_unified_json_text(&payload, "{\"kind\":\"presentation-owner\",\"event\":{\"kind\":\"retired\",\"owner\":{\"provider\":", error) &&
        application_unified_json_string(&payload, qa_strings_cstr(qa_session_strings(app->session), owner), error) &&
        application_unified_json_text(&payload, ",\"generation\":", error) &&
        application_unified_json_natural(&payload, activation->generation, error) &&
        application_unified_json_text(&payload, "}}}", error) &&
        retain_payload(app, (qa_bytes){payload.bytes.data, payload.bytes.size}, &retired, error);
    application_unified_event_record *prepared = ok ? calloc(additions, sizeof(*prepared)) : NULL;
    if (ok && !prepared) ok = application_fail(error, QA_ERROR_MEMORY, "Preparing atomic Source presentation retirement");
    if (ok) prepared[0] = (application_unified_event_record){.presentation = retired,
        .provider = owner, .content = activation->content, .clock = clock->kind,
        .presentation_clock = clock->kind, .time_ns = clock->time_ns, .simulation_time_ns = clock->time_ns};
    for (size_t i = 0; ok && i < winner_count; ++i) {
        prepared[i + 1] = app->unified_persistent[winners[i]].event;
        prepared[i + 1].simulation = (qa_bytes){0}; prepared[i + 1].simulation_sequence = 0;
        ok = retain_payload(app, prepared[i + 1].presentation, &prepared[i + 1].presentation, error);
    }
    if (ok) ok = application_unified_persistent_retire(app, owner, (qa_actor_id){0}, error);
    if (ok) for (size_t i = 0; i < additions; ++i) {
        prepared[i].order = app->unified_event_sequence++;
        prepared[i].presentation_sequence = app->presentation_event_sequence++;
        app->unified_events[app->unified_event_count++] = prepared[i];
    }
    for (size_t i = 0; i < count; ++i) qa_buffer_free(keys + i);
    free(keys); free(prepared); free(winners); application_unified_json_dispose(&payload);
    return ok;
}

bool application_unified_event_emit(qa_application *app, qa_actor_owner owner,
    qa_bytes presentation, qa_bytes simulation, qa_actor_id recipient,
    qa_actor_id simulation_recipient, uint64_t time_ns, int32_t source_entity,
    bool has_source_entity, bool link_presentation, qa_error *error)
{
    application_unified_event_source source;
    if (!app || !app->session || app->destroy_requested || !application_unified_event_source_read(app, owner, &source, error) ||
        !source.product->identity ||
        (!presentation.size && !simulation.size) || (presentation.size && !presentation.data) ||
        (simulation.size && !simulation.data) ||
        (link_presentation && (!presentation.size || !simulation.size)) ||
        app->unified_event_sequence >= QA_UNIFIED_SAFE_INTEGER ||
        (presentation.size && app->presentation_event_sequence >= QA_UNIFIED_SAFE_INTEGER) ||
        (simulation.size && app->simulation_event_sequence >= QA_UNIFIED_SAFE_INTEGER))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source emission lost its actual owner, payload or sequence domain");
    application_provider *provider = source_provider(app, owner);
    if (provider && provider->event_activation_deferred)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source emission awaits its prepared activation handoff");
    application_unified_event_record record = {.recipient = recipient,
        .simulation_recipient = simulation_recipient, .provider = owner,
        .clock = source.clock, .time_ns = time_ns,
        .presentation_clock = source.clock,
        .simulation_time_ns = time_ns, .source_entity = source_entity,
        .has_source_entity = has_source_entity, .link_presentation = link_presentation};
    qa_q2_edition q2_edition; bool q2_profile;
    uint64_t q2_interval;
    if (!qa_application_native_q2_source_clock_read(app,owner,&q2_edition,&q2_interval,&q2_profile,error)) return false;
    if (q2_profile) {
        qa_clock_kind actual=q2_edition==QA_Q2_CLASSIC ? QA_CLOCK_Q2_CLASSIC : QA_CLOCK_Q2_RERELEASE;
        if (source.clock!=actual)
            return application_fail(error,QA_ERROR_FORMAT,"Q2 Source rules differ from its emitted clock receipt");
        record.q2_source_profile=(uint8_t)(q2_edition==QA_Q2_CLASSIC ? 1 : 2);
        record.q2_source_interval_ns=q2_interval;
    }
    bool activated = false;
    for (size_t i = 0; i < app->unified_event_owner_count; ++i)
        if (app->unified_event_owners[i].provider == owner && app->unified_event_owners[i].active) {
            const char *identity = qa_strings_cstr(qa_session_strings(app->session), app->unified_event_owners[i].content);
            if (!identity || strcmp(identity, source.product->identity) ||
                source.component != (app->unified_event_owners[i].generation != 0))
                return application_fail(error, QA_ERROR_ARGUMENT, "Source presentation activation changed its actual content");
            record.owner_generation = app->unified_event_owners[i].generation;
            activated = true;
        }
    if (!activated) return application_fail(error, QA_ERROR_ARGUMENT, "Source event has no actual presentation activation receipt");
    if (!has_source_entity) record.source_entity = 0;
    if (simulation.size && simulation_recipient.registry) {
        bool found = false;
        for (size_t i = 0; app->players && i < app->players->count; ++i) {
            const application_player_record *player = &app->players->records[i];
            if (!player->retiring && qa_actor_id_equal(player->actor, simulation_recipient)) {
                record.client = player->remote_client; found = true; break;
            }
        }
        if (!found) simulation = (qa_bytes){0};
    }
    if (!simulation.size) record.link_presentation = false;
    if (!presentation.size && !simulation.size) return true;
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    qa_clock_state clock;
    if (primary && qa_session_clock(app->session, primary->owner, &clock)) {
        record.clock = clock.frame.kind;
        record.simulation_time_ns = clock.frame.time_ns;
    }
    if (!application_unified_event_payload_valid(presentation, true, false, error) ||
        !application_unified_event_payload_valid(simulation, false, record.link_presentation, error) ||
        !qa_strings_intern_cstr(qa_session_strings(app->session), source.product->identity,
            &record.content, error) ||
        !retain_payload(app, presentation, &record.presentation, error) ||
        !retain_payload(app, simulation, &record.simulation, error)) return false;
    if (!application_unified_event_actors_valid(app, &record, error)) return false;
    if (app->unified_event_count == app->unified_event_capacity) {
        size_t capacity = app->unified_event_capacity ? app->unified_event_capacity : 64;
        if (app->unified_event_capacity) {
            if (capacity > SIZE_MAX / 2)
                return application_fail(error, QA_ERROR_MEMORY, "Source projection capacity is exhausted");
            capacity *= 2;
        }
        if (capacity > SIZE_MAX / sizeof(*app->unified_events))
            return application_fail(error, QA_ERROR_MEMORY, "Source projection extent overflows");
        void *rows = realloc(app->unified_events, capacity * sizeof(*app->unified_events));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining Source event projection");
        app->unified_events = rows; app->unified_event_capacity = capacity;
    }
    record.order = app->unified_event_sequence;
    if (presentation.size) record.presentation_sequence = app->presentation_event_sequence;
    if (simulation.size) record.simulation_sequence = app->simulation_event_sequence;
    if (!persistent_record(app, &record, error)) return false;
    ++app->unified_event_sequence;
    if (presentation.size) ++app->presentation_event_sequence;
    if (simulation.size) ++app->simulation_event_sequence;
    app->unified_events[app->unified_event_count++] = record;
    return true;
}

static bool string_id(application_unified_json *j, qa_application *app, qa_string_id id, qa_error *error)
{
    qa_bytes value = qa_strings_text(qa_session_strings(app->session), id);
    qa_buffer quoted = {0};
    bool ok = (!id || value.data) && qa_json_quote(value, &quoted, error) &&
        application_unified_json_append(j, (qa_bytes){quoted.data, quoted.size}, error);
    qa_buffer_free(&quoted);
    return ok || application_fail(error, QA_ERROR_FORMAT, "Source damage identity is outside its real session table");
}

static bool optional_actor(application_unified_json *j, qa_actor_id actor, qa_error *error)
{ return actor.registry ? application_unified_json_actor(j, actor, error) : application_unified_json_text(j, "null", error); }

#define TEXT(s) application_unified_json_text(j, (s), error)
#define NUMBER(n) application_unified_json_number(j, (n), error)
#define ID(n) string_id(j, app, (n), error)
#define VECTOR(v) application_unified_json_vector(j, (v), error)

static bool cause_write(application_unified_json *j, qa_application *app,
    const qa_damage_cause *cause, qa_error *error)
{
    if (cause->kind == QA_CAUSE_Q1) {
        bool ok = TEXT("{\"kind\":\"q1\",\"deathType\":") && ID(cause->source.q1.death_type);
        if (ok && cause->source.q1.armor != QA_Q1_ARMOR_NORMAL)
            ok = TEXT(cause->source.q1.armor == QA_Q1_ARMOR_BYPASS ?
                ",\"armorEffect\":\"bypass\"" : ",\"armorEffect\":\"half-effectiveness\"");
        return ok && TEXT("}");
    }
    if (cause->kind == QA_CAUSE_Q2 || cause->kind == QA_CAUSE_Q3) {
        bool q2 = cause->kind == QA_CAUSE_Q2;
        bool ok = TEXT(q2 ? "{\"kind\":\"q2\",\"meansOfDeath\":" : "{\"kind\":\"q3\",\"meansOfDeath\":") &&
            NUMBER(q2 ? cause->source.q2.means_of_death : cause->source.q3.means_of_death) &&
            TEXT(",\"damageFlags\":") && NUMBER(q2 ? cause->source.q2.flags : cause->source.q3.flags);
        if (ok && q2 && cause->source.q2.native == QA_Q2_CAUSE_CLASSIC) {
            static const char *const games[] = {"base", "xatrix", "rogue", "ctf"};
            if (cause->source.q2.classic_product >= sizeof(games) / sizeof(*games))
                return application_fail(error, QA_ERROR_FORMAT, "Damage cause has no actual classic Q2 game");
            ok = TEXT(",\"native\":{\"edition\":\"classic\",\"game\":") &&
                application_unified_json_string(j, games[cause->source.q2.classic_product], error) &&
                TEXT(",\"value\":") && NUMBER(cause->source.q2.native_value) && TEXT("}");
        } else if (ok && q2 && cause->source.q2.native == QA_Q2_CAUSE_RERELEASE) {
            ok = TEXT(",\"native\":{\"edition\":\"rerelease\",\"id\":") && NUMBER(cause->source.q2.native_value) &&
                TEXT(cause->source.q2.friendly_fire ? ",\"friendlyFire\":true" : ",\"friendlyFire\":false") &&
                TEXT(cause->source.q2.no_point_loss ? ",\"noPointLoss\":true}" : ",\"noPointLoss\":false}");
        }
        return ok && TEXT("}");
    }
    static const char *const hazards[] = {"fall", "drown", "lava", "slime", "crush", "trigger"};
    if (cause->kind != QA_CAUSE_ENVIRONMENT || (unsigned)cause->source.hazard >= sizeof(hazards) / sizeof(*hazards))
        return application_fail(error, QA_ERROR_FORMAT, "Damage cause lost its actual source variant");
    return TEXT("{\"kind\":\"environment\",\"hazard\":") &&
        application_unified_json_string(j, hazards[cause->source.hazard], error) && TEXT("}");
}

static bool request_write(application_unified_json *j, qa_application *app,
    const qa_damage_request *request, qa_error *error)
{
    const qa_attack *attack = &request->attack;
    application_provider *source = source_provider(app, attack->weapon_provider);
    if (!source || !source->launch)
        return application_fail(error, QA_ERROR_FORMAT, "Damage attack lost its genuine source weapon provider");
    qa_clock_kind clock = source->launch->selection.clock.kind;
    bool ms = clock == QA_CLOCK_Q2_RERELEASE || clock == QA_CLOCK_Q3;
    double time = (double)attack->time_ns / (ms ? 1e6 : 1e9);
    if (clock == QA_CLOCK_Q3) {
        uint32_t word = (uint32_t)(attack->time_ns / UINT64_C(1000000));
        int32_t signed_word; memcpy(&signed_word, &word, sizeof(word));
        time = signed_word;
    }
    bool ok = TEXT("{\"attack\":{\"sequence\":") && application_unified_json_natural(j, attack->sequence, error) &&
        TEXT(ms ? ",\"time\":{\"kind\":\"milliseconds\",\"value\":" : ",\"time\":{\"kind\":\"seconds\",\"value\":") &&
        NUMBER(time) && TEXT("},\"attacker\":") &&
        optional_actor(j, attack->attacker, error) && TEXT(",\"inflictor\":") && optional_actor(j, attack->inflictor, error);
    if (ok && attack->projectile.registry)
        ok = TEXT(",\"originatingProjectile\":") && application_unified_json_actor(j, attack->projectile, error);
    if (ok) ok = TEXT(",\"weapon\":") && (attack->weapon ? ID(attack->weapon) : TEXT("null")) &&
        TEXT(",\"weaponProvider\":") && ID(attack->weapon_provider);
    if (ok && attack->powerup_applied && attack->powerup_owner)
        ok = TEXT(",\"damagePowerupOwner\":") && ID(attack->powerup_owner);
    if (ok) ok = TEXT(",\"combatProvider\":") && ID(attack->combat_provider) &&
        TEXT(",\"inventoryProvider\":") && ID(attack->inventory_provider) && TEXT(",\"movementProvider\":") && ID(attack->movement_provider) &&
        TEXT(",\"cause\":") && cause_write(j, app, &attack->cause, error) && TEXT("},\"target\":") &&
        application_unified_json_actor(j, request->target, error) && TEXT(",\"amount\":") && NUMBER(request->amount) &&
        TEXT(",\"knockback\":") && NUMBER(request->knockback) && TEXT(",\"direction\":") && VECTOR(request->direction) &&
        TEXT(",\"point\":") && VECTOR(request->point) && TEXT(",\"normal\":") && VECTOR(request->normal) &&
        TEXT(request->radius ? ",\"delivery\":\"radius\"}" : ",\"delivery\":\"direct\"}");
    return ok;
}

static bool armor_write(application_unified_json *j, qa_application *app, const qa_armor *armor, qa_error *error)
{
    const qa_regular_armor *regular = &armor->regular;
    static const char *const kinds[] = {"none", "q1", "q2", "q3", "source"};
    if ((unsigned)regular->kind >= sizeof(kinds) / sizeof(*kinds) || armor->powered.kind > QA_POWER_SHIELD)
        return application_fail(error, QA_ERROR_FORMAT, "Damage journal armor has no actual source variant");
    bool ok = TEXT("{\"regular\":{\"kind\":") && application_unified_json_string(j, kinds[regular->kind], error);
    if (ok && regular->kind != QA_ARMOR_NONE) ok = TEXT(",\"points\":") && NUMBER(regular->points);
    if (ok && regular->kind == QA_ARMOR_Q1) ok = TEXT(",\"absorption\":") && NUMBER(regular->protection.q1_absorption);
    if (ok && regular->kind == QA_ARMOR_Q2) ok = TEXT(",\"normalProtection\":") && NUMBER(regular->protection.q2.normal) &&
        TEXT(",\"energyProtection\":") && NUMBER(regular->protection.q2.energy);
    if (ok && regular->kind == QA_ARMOR_Q3) ok = TEXT(",\"protection\":") && NUMBER(regular->protection.q3_protection);
    if (ok && (regular->kind == QA_ARMOR_Q1 || regular->kind == QA_ARMOR_Q2 || regular->kind == QA_ARMOR_SOURCE))
        ok = TEXT(",\"item\":") && (regular->item ? ID(regular->item) : TEXT("null"));
    if (ok) ok = TEXT("},\"powered\":{\"kind\":") && application_unified_json_string(j,
        armor->powered.kind == QA_POWER_NONE ? "none" : armor->powered.kind == QA_POWER_SCREEN ? "screen" : "shield", error);
    if (ok && armor->powered.kind != QA_POWER_NONE) ok = TEXT(",\"cells\":") && NUMBER(armor->powered.cells);
    return ok && TEXT("}}");
}

static bool mutation_write(application_unified_json *j, qa_application *app,
    const qa_damage_mutation *mutation, qa_error *error)
{
    switch (mutation->kind) {
    case QA_MUTATION_HEALTH:
        return TEXT("{\"kind\":\"health\",\"before\":") && NUMBER(mutation->value.health.before) &&
            TEXT(",\"after\":") && NUMBER(mutation->value.health.after) && TEXT("}");
    case QA_MUTATION_ARMOR:
        return TEXT("{\"kind\":\"armor\",\"before\":") && armor_write(j, app, &mutation->value.armor.before, error) &&
            TEXT(",\"after\":") && armor_write(j, app, &mutation->value.armor.after, error) && TEXT("}");
    case QA_MUTATION_SOURCE_VELOCITY:
        return TEXT("{\"kind\":\"source-velocity\",\"before\":") && VECTOR(mutation->value.velocity.before) &&
            TEXT(",\"after\":") && VECTOR(mutation->value.velocity.after) && TEXT(",\"movementProvider\":") &&
            ID(mutation->value.velocity.movement) && TEXT("}");
    case QA_MUTATION_IMPULSE:
        return TEXT("{\"kind\":\"impulse\",\"impulse\":") && VECTOR(mutation->value.impulse.value) &&
            TEXT(",\"movementProvider\":") && ID(mutation->value.impulse.movement) && TEXT("}");
    }
    return application_fail(error, QA_ERROR_FORMAT, "Damage journal mutation has no actual source variant");
}

bool application_unified_damage_emit(qa_application *app, const qa_damage_outcome *outcome, qa_error *error)
{
    if (!app || !outcome) return application_fail(error, QA_ERROR_ARGUMENT, "Damage event has no actual outcome");
    application_unified_json buffer = {0};
    application_unified_json *j = &buffer;
    bool ok = TEXT("{\"kind\":\"damage\",\"outcome\":") &&
        TEXT(outcome->stale ? "{\"kind\":\"stale-target\",\"request\":" : "{\"kind\":\"committed\",\"decision\":{\"request\":") &&
        request_write(j, app, &outcome->request, error);
    if (ok && !outcome->stale) {
        ok = TEXT(",\"mutations\":[");
        for (size_t i = 0; ok && i < outcome->mutation_count; ++i)
            ok = (!i || TEXT(",")) && mutation_write(j, app, outcome->mutations + i, error);
        static const char *const reactions[] = {"none", "pain", "death"};
        if ((unsigned)outcome->result.reaction >= sizeof(reactions) / sizeof(*reactions))
            ok = application_fail(error, QA_ERROR_FORMAT, "Damage result has no actual reaction");
        if (ok) ok = TEXT("],\"appliedDamage\":") && NUMBER(outcome->result.applied_damage) && TEXT(",\"reaction\":") &&
            application_unified_json_string(j, reactions[outcome->result.reaction], error);
        if (ok && outcome->result.has_feedback && outcome->result.feedback_family == QA_GAME_Q2)
            ok = TEXT(",\"feedback\":{\"kind\":\"q2\",\"powerArmor\":") && NUMBER(outcome->result.power_saved) &&
                TEXT(",\"armor\":") && NUMBER(outcome->result.armor_saved) && TEXT(",\"blood\":") && NUMBER(outcome->result.blood) &&
                TEXT(",\"knockback\":") && NUMBER(outcome->result.knockback) && TEXT("}");
        if (ok && outcome->result.has_feedback && outcome->result.feedback_family == QA_GAME_Q3)
            ok = TEXT(",\"feedback\":{\"kind\":\"q3\",\"knockback\":") && NUMBER(outcome->result.knockback) &&
                TEXT(outcome->result.battlesuit ? ",\"battlesuit\":true}" : ",\"battlesuit\":false}");
        if (ok) ok = TEXT(outcome->survived ? "},\"survived\":true" : "},\"survived\":false");
    }
    if (ok) ok = TEXT("}}") && application_unified_event_emit(app, outcome->request.attack.weapon_provider,
        (qa_bytes){0}, (qa_bytes){buffer.bytes.data, buffer.bytes.size}, (qa_actor_id){0}, (qa_actor_id){0},
        outcome->request.attack.time_ns, 0, false, false, error);
    application_unified_json_dispose(&buffer);
    return ok;
}
#undef TEXT
#undef NUMBER
#undef ID
#undef VECTOR

bool application_event_journal_reserve(qa_application *app, qa_error *error)
{
    if (!app || app->event_sequence >= QA_UNIFIED_SAFE_INTEGER)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source event sequence is exhausted");
    if (app->event_journal_count < app->event_journal_capacity) return true;
    size_t capacity = app->event_journal_capacity ? app->event_journal_capacity : 64;
    if (app->event_journal_capacity) {
        if (capacity > SIZE_MAX / 2)
            return application_fail(error, QA_ERROR_MEMORY, "Source event journal capacity is exhausted");
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*app->event_journal))
        return application_fail(error, QA_ERROR_MEMORY, "Source event journal extent overflows");
    void *rows = realloc(app->event_journal, capacity * sizeof(*app->event_journal));
    if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining Source event order");
    app->event_journal = rows;
    app->event_journal_capacity = capacity;
    return true;
}

void application_event_journal_append(qa_application *app, application_event_queue queue,
    size_t index, qa_actor_owner owner)
{
    application_event_journal_record record = {.sequence = app->event_sequence++,
        .queue = queue, .index = index};
    qa_clock_state clock;
    if (owner && qa_session_clock(app->session, owner, &clock)) {
        record.frame = clock.frame;
        record.has_frame = true;
    }
    app->event_journal[app->event_journal_count++] = record;
}

static bool actor_property(const qa_json_document *json, qa_json_id key)
{
    const char *names[] = {"actor", "player", "attacker", "inflictor", "originatingProjectile",
        "target", "killer", "victim", "other", "activator", "ground", "projectile", "entity", "owner"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (qa_json_string_equal(json, key, names[i])) return true;
    return false;
}

static bool payload_actors(qa_application *app, const qa_json_document *json, qa_json_id value,
    bool actor_value, bool checkpoint, unsigned depth, application_unified_json *out, qa_error *error)
{
    if (depth > 128) return application_fail(error, QA_ERROR_FORMAT, "Source actor payload nesting exceeds its admitted extent");
    qa_json_kind kind = qa_json_type(json, value);
    if (actor_value && kind == QA_JSON_OBJECT && qa_json_get(json, value, "slot") != QA_JSON_NONE &&
        qa_json_get(json, value, "generation") != QA_JSON_NONE) {
        uint64_t slot, generation;
        qa_actor_id actor;
        if (qa_json_size(json, value) != 2 || !qa_json_u64(json, qa_json_get(json, value, "slot"), &slot, error) ||
            slot > UINT32_MAX || !qa_json_u64(json, qa_json_get(json, value, "generation"), &generation, error) ||
            !qa_actors_reference_saved(qa_session_actors(app->session),
                (qa_saved_actor_id){generation, (uint32_t)slot}, checkpoint, &actor, error))
            return application_fail(error, QA_ERROR_FORMAT, "Source payload ActorId is outside its actual retained actor history");
        return !out || application_unified_json_actor(out, actor, error);
    }
    if (kind != QA_JSON_OBJECT && kind != QA_JSON_ARRAY)
        return !out || application_unified_json_append(out, qa_json_source(json, value), error);
    bool object = kind == QA_JSON_OBJECT;
    if (out && !application_unified_json_text(out, object ? "{" : "[", error)) return false;
    for (size_t i = 0; i < qa_json_size(json, value); ++i) {
        qa_json_id key = object ? qa_json_key_at(json, value, i) : QA_JSON_NONE;
        if (out && ((i && !application_unified_json_text(out, ",", error)) ||
            (object && (!application_unified_json_append(out, qa_json_source(json, key), error) ||
                !application_unified_json_text(out, ":", error))))) return false;
        if (!payload_actors(app, json, qa_json_at(json, value, i), object && actor_property(json, key),
            checkpoint, depth + 1, out, error)) return false;
    }
    return !out || application_unified_json_text(out, object ? "}" : "]", error);
}

static bool payload_actor_read(qa_application *app, qa_bytes bytes, bool checkpoint,
    application_unified_json *out, qa_error *error)
{
    if (!bytes.size) return true;
    qa_json_document *json = NULL;
    if (!qa_json_parse(bytes, &json, error)) return false;
    bool ok = payload_actors(app, json, qa_json_root(json), false, checkpoint, 0, out, error);
    qa_json_destroy(json);
    return ok;
}

bool application_unified_event_actors_valid(qa_application *app,
    const application_unified_event_record *row, qa_error *error)
{
    return payload_actor_read(app, row->presentation, row->payload_checkpoint, NULL, error) &&
        payload_actor_read(app, row->simulation, row->payload_checkpoint, NULL, error);
}

bool application_unified_event_recipient(qa_application *app, const application_unified_event_record *row,
    bool simulation, qa_actor_id *out, qa_error *error)
{
    qa_actor_id actor = simulation ? row->simulation_recipient : row->recipient;
    if (!actor.registry || !row->payload_checkpoint) { *out = actor; return true; }
    return qa_actors_reference_saved(qa_session_actors(app->session), simulation ?
        row->simulation_recipient_saved : row->recipient_saved, true, out, error);
}

static bool payload_begin(application_unified_json *json, qa_bytes payload, qa_error *error)
{
    while (payload.size && (payload.data[payload.size - 1] == ' ' || payload.data[payload.size - 1] == '\n' ||
        payload.data[payload.size - 1] == '\r' || payload.data[payload.size - 1] == '\t')) --payload.size;
    if (!payload.size || payload.data[payload.size - 1] != '}')
        return application_fail(error, QA_ERROR_FORMAT, "Source event lost its retained object");
    --payload.size;
    return application_unified_json_append(json, payload, error);
}

/* Presentation events use JSON.stringify, whereas SimulationEvents use the
 * checkpoint value codec. Preserve that genuine distinction at the wire. */
static bool presentation_wire(application_unified_json *out, const qa_json_document *json,
    qa_json_id value, unsigned depth, qa_error *error)
{
    if (depth > 128) return application_fail(error, QA_ERROR_FORMAT, "Source presentation nesting exceeds its wire extent");
    qa_json_kind kind = qa_json_type(json, value);
    if (kind == QA_JSON_OBJECT && qa_json_string_equal(json, qa_json_get(json, value, "$qts"), "number")) {
        qa_json_id number = qa_json_get(json, value, "value");
        return application_unified_json_text(out, qa_json_string_equal(json, number, "-0") ? "0" : "null", error);
    }
    if (kind != QA_JSON_ARRAY && kind != QA_JSON_OBJECT)
        return application_unified_json_append(out, qa_json_source(json, value), error);
    bool object = kind == QA_JSON_OBJECT;
    if (!application_unified_json_text(out, object ? "{" : "[", error)) return false;
    for (size_t i = 0; i < qa_json_size(json, value); ++i) {
        if (i && !application_unified_json_text(out, ",", error)) return false;
        if (object && (!application_unified_json_append(out, qa_json_source(json, qa_json_key_at(json, value, i)), error) ||
            !application_unified_json_text(out, ":", error))) return false;
        if (!presentation_wire(out, json, qa_json_at(json, value, i), depth + 1, error)) return false;
    }
    return application_unified_json_text(out, object ? "}" : "]", error);
}

static bool presentation_write(application_unified_json *j, qa_application *app,
    const application_unified_event_record *row, qa_error *error)
{
    const char *content = qa_strings_cstr(qa_session_strings(app->session), row->content);
    double seconds = (double)row->time_ns / 1e9;
    if (row->presentation_clock == QA_CLOCK_Q3) {
        uint32_t word = (uint32_t)(row->time_ns / 1000000);
        int32_t signed_word; memcpy(&signed_word, &word, sizeof(word));
        seconds = (double)signed_word / 1000;
    }
    application_unified_json payload = {0};
    qa_actor_id recipient;
    bool ok = application_unified_event_recipient(app, row, false, &recipient, error) &&
        payload_actor_read(app, row->presentation, row->payload_checkpoint, &payload, error) &&
        payload_begin(j, (qa_bytes){payload.bytes.data, payload.bytes.size}, error) &&
        application_unified_json_text(j, ",\"sequence\":", error) &&
        application_unified_json_natural(j, row->presentation_sequence, error) &&
        application_unified_json_text(j, ",\"content\":", error) &&
        application_unified_json_string(j, content, error) &&
        application_unified_json_text(j, ",\"seconds\":", error) &&
        application_unified_json_number(j, seconds, error);
    if (ok && recipient.registry) ok = application_unified_json_text(j, ",\"recipient\":", error) &&
        application_unified_json_actor(j, recipient, error);
    if (ok && row->owner_generation) ok = application_unified_json_text(j, ",\"owner\":{\"provider\":", error) &&
        application_unified_json_string(j, qa_strings_cstr(qa_session_strings(app->session), row->provider), error) &&
        application_unified_json_text(j, ",\"generation\":", error) &&
        application_unified_json_natural(j, row->owner_generation, error) && application_unified_json_text(j, "}", error);
    if (ok && row->q2_source_profile) ok =
        application_unified_json_text(j,",\"source\":{\"provider\":",error) &&
        application_unified_json_string(j,qa_strings_cstr(qa_session_strings(app->session),row->provider),error) &&
        application_unified_json_text(j,",\"profile\":",error) &&
        application_unified_json_string(j,row->q2_source_profile==1 ? "classic" : "rerelease",error) &&
        application_unified_json_text(j,",\"frameMilliseconds\":",error) &&
        application_unified_json_number(j,(double)row->q2_source_interval_ns/1000000.0,error) &&
        application_unified_json_text(j,"}",error);
    if (ok && !row->q2_source_profile) {
        qa_json_document *declaration=NULL;
        ok=qa_json_parse((qa_bytes){payload.bytes.data,payload.bytes.size},&declaration,error);
        if (ok && qa_json_string_equal(declaration,
                qa_json_get(declaration,qa_json_root(declaration),"kind"),"q3-source"))
            ok=application_unified_json_text(j,",\"source\":{\"provider\":",error) &&
                application_unified_json_string(j,
                    qa_strings_cstr(qa_session_strings(app->session),row->provider),error) &&
                application_unified_json_text(j,"}",error);
        qa_json_destroy(declaration);
    }
    if (ok) ok = application_unified_json_text(j, ",\"sourceEntity\":", error) &&
        (row->has_source_entity ? application_unified_json_number(j, row->source_entity, error) :
            application_unified_json_text(j, "null", error)) && application_unified_json_text(j, "}", error);
    application_unified_json_dispose(&payload);
    return ok;
}

static bool simulation_write(application_unified_json *j, qa_application *app,
    const application_unified_event_record *row, qa_error *error)
{
    bool seconds = row->clock != QA_CLOCK_Q2_RERELEASE;
    double time = (double)row->simulation_time_ns / (seconds ? 1e9 : 1e6);
    if (row->clock == QA_CLOCK_Q3) {
        uint32_t word = (uint32_t)(row->simulation_time_ns / 1000000);
        int32_t signed_word; memcpy(&signed_word, &word, sizeof(word));
        time = (double)signed_word / 1000;
    }
    bool ok = application_unified_json_text(j, "{\"sequence\":", error) &&
        application_unified_json_natural(j, row->simulation_sequence, error) &&
        application_unified_json_text(j, seconds ? ",\"time\":{\"kind\":\"seconds\",\"value\":" :
            ",\"time\":{\"kind\":\"milliseconds\",\"value\":", error) &&
        application_unified_json_number(j, time, error) &&
        application_unified_json_text(j, "},\"audience\":", error);
    if (ok && row->simulation_recipient.registry) ok =
        application_unified_json_text(j, "{\"kind\":\"client\",\"client\":{\"slot\":", error) &&
        application_unified_json_natural(j, row->client.slot, error) &&
        application_unified_json_text(j, ",\"generation\":", error) &&
        application_unified_json_natural(j, row->client.generation, error) &&
        application_unified_json_text(j, "}}", error);
    else if (ok) ok = application_unified_json_text(j, "{\"kind\":\"world\"}", error);
    if (ok) ok = application_unified_json_text(j, ",\"payload\":", error);
    application_unified_json payload = {0};
    if (ok) ok = payload_actor_read(app, row->simulation, row->payload_checkpoint, &payload, error);
    if (ok && row->link_presentation) ok = payload_begin(j, (qa_bytes){payload.bytes.data, payload.bytes.size}, error) &&
        application_unified_json_text(j, ",\"sourcePresentationSequence\":", error) &&
        application_unified_json_natural(j, row->presentation_sequence, error) &&
        application_unified_json_text(j, "}", error);
    else if (ok) ok = application_unified_json_append(j, (qa_bytes){payload.bytes.data, payload.bytes.size}, error);
    application_unified_json_dispose(&payload);
    return ok && application_unified_json_text(j, "}", error);
}

static bool world_text_read(qa_application *app, const application_unified_source *source,
    qa_unified_document **out, qa_error *error)
{
    double now = (double)source->frame.time_ns / 1e9;
    bool changed = app->unified_world_text_map != source->map_revision;
    size_t count = changed ? 0 : app->unified_world_text_count, kept = 0;
    for (size_t i = 0; !changed && i < count; ++i) {
        const application_unified_world_text *row = app->unified_world_text + i;
        changed = (row->timed && row->expires <= now) ||
            (!row->timed && (!row->observed || row->first_frame != source->frame.number));
    }
    if (changed && app->unified_world_text_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "World text revision is exhausted");
    for (size_t i = 0; i < count; ++i) {
        application_unified_world_text row = app->unified_world_text[i];
        if ((row.timed && row.expires <= now) ||
            (!row.timed && row.observed && row.first_frame != source->frame.number)) { changed = true; continue; }
        if (!row.timed && !row.observed) {
            row.observed = true; row.first_frame = source->frame.number; changed = true;
        }
        app->unified_world_text[kept++] = row;
    }
    if (changed) ++app->unified_world_text_revision;
    app->unified_world_text_count = kept; app->unified_world_text_map = source->map_revision;
    application_unified_json j = {0};
    bool ok = application_unified_json_text(&j, "[", error);
    for (size_t i = 0; ok && i < kept; ++i) {
        const application_unified_world_text *row = app->unified_world_text + i;
        const char *content = qa_strings_cstr(qa_session_strings(app->session), row->content);
        qa_bytes value = qa_strings_text(qa_session_strings(app->session), row->text);
        qa_buffer quoted = {0};
        ok = qa_json_quote(value, &quoted, error) && (!i || application_unified_json_text(&j, ",", error)) &&
            application_unified_json_text(&j, "{\"content\":", error) &&
            application_unified_json_string(&j, content, error) &&
            application_unified_json_text(&j, ",\"text\":", error) &&
            application_unified_json_append(&j, (qa_bytes){quoted.data, quoted.size}, error) &&
            application_unified_json_text(&j, ",\"origin\":", error) &&
            application_unified_json_vector(&j, row->origin, error) &&
            application_unified_json_text(&j, ",\"color\":{\"x\":", error) &&
            application_unified_json_number(&j, row->color.x, error) &&
            application_unified_json_text(&j, ",\"y\":", error) && application_unified_json_number(&j, row->color.y, error) &&
            application_unified_json_text(&j, ",\"z\":", error) && application_unified_json_number(&j, row->color.z, error) &&
            application_unified_json_text(&j, ",\"w\":", error) && application_unified_json_number(&j, row->alpha, error) &&
            application_unified_json_text(&j, "},\"cellSize\":", error) &&
            application_unified_json_number(&j, row->cell_size, error) &&
            application_unified_json_text(&j, ",\"distanceCullFactor\":0.004,\"orientation\":", error);
        if (ok) ok = row->billboard ? application_unified_json_text(&j, "{\"kind\":\"billboard\"}", error) :
            (application_unified_json_text(&j, "{\"kind\":\"fixed\",\"angles\":", error) &&
             application_unified_json_vector(&j, row->angles, error) && application_unified_json_text(&j, "}", error));
        if (ok) ok = application_unified_json_text(&j, row->depth_test ?
            ",\"depthTest\":true,\"font\":\"classic\"}" : ",\"depthTest\":false,\"font\":\"classic\"}", error);
        qa_buffer_free(&quoted);
    }
    if (ok) ok = application_unified_json_text(&j, "]", error) && qa_unified_document_create(QA_UNIFIED_CHECKPOINT,
        (qa_bytes){j.bytes.data, j.bytes.size}, out, error);
    application_unified_json_dispose(&j);
    return ok;
}

static bool presentation_kind(const qa_json_document *json,qa_json_id object,const char *kind)
{ return qa_json_string_equal(json,qa_json_get(json,object,"kind"),kind); }

static qa_json_id presentation_target(const qa_json_document *json,qa_json_id object,const char *key,bool *targeted)
{ *targeted=true; return qa_json_get(json,object,key); }

static bool presentation_own(qa_application *app,const application_unified_event_record *row,
    const qa_json_document *json,qa_json_id value,qa_actor_id player,bool *allowed,qa_error *error)
{
    if (qa_json_type(json,value)==QA_JSON_NULL) { *allowed=true; return true; }
    uint64_t slot,generation; qa_actor_id actor;
    if (qa_json_type(json,value)!=QA_JSON_OBJECT || qa_json_size(json,value)!=2 ||
        !qa_json_u64(json,qa_json_get(json,value,"slot"),&slot,error) || slot>UINT32_MAX ||
        !qa_json_u64(json,qa_json_get(json,value,"generation"),&generation,error) ||
        !qa_actors_reference_saved(qa_session_actors(app->session),(qa_saved_actor_id){generation,(uint32_t)slot},
            row->payload_checkpoint,&actor,error)) {
        if (!error || error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Private Source presentation lost its full actor");
        return false;
    }
    *allowed=qa_actor_id_equal(actor,player); return true;
}

static bool presentation_for(qa_application *app,const application_unified_event_record *row,
    qa_net_client_id client,qa_actor_id player,bool *allowed,qa_error *error)
{
    qa_json_document *json=NULL;
    if (!qa_json_parse(row->presentation,&json,error)) return false;
    qa_json_id root=qa_json_root(json),event=qa_json_get(json,root,"event"),target=QA_JSON_NONE;
    bool ok=true,targeted=false; *allowed=true;
    if (presentation_kind(json,root,"view-reset")) target=presentation_target(json,root,"actor",&targeted);
    else if (presentation_kind(json,root,"q1-fog")) target=presentation_target(json,event,"player",&targeted);
    else if (presentation_kind(json,root,"q1")) {
        if (presentation_kind(json,event,"server-command")) *allowed=false;
        else if (qa_json_get(json,event,"player")!=QA_JSON_NONE) target=presentation_target(json,event,"player",&targeted);
    } else if (presentation_kind(json,root,"q1-composition")) {
        if (presentation_kind(json,event,"source-log") || presentation_kind(json,event,"developer-message")) *allowed=false;
        else if (presentation_kind(json,event,"addon")) {
            qa_json_id nested=qa_json_get(json,event,"event");
            if (qa_json_get(json,nested,"player")!=QA_JSON_NONE) target=presentation_target(json,nested,"player",&targeted);
            else if (presentation_kind(json,nested,"developer-message")) *allowed=false;
        } else if (presentation_kind(json,event,"ctf-status") || presentation_kind(json,event,"prompt") ||
            presentation_kind(json,event,"clear-prompt")) target=presentation_target(json,event,"actor",&targeted);
    } else if (presentation_kind(json,root,"q2")) {
        if (presentation_kind(json,event,"pickup")) target=presentation_target(json,event,"player",&targeted);
        else if (presentation_kind(json,event,"centerprint") || presentation_kind(json,event,"print") ||
            presentation_kind(json,event,"damage-indicator")) target=presentation_target(json,event,"actor",&targeted);
    } else if (presentation_kind(json,root,"q2-player")) {
        if (presentation_kind(json,event,"stufftext") || presentation_kind(json,event,"load-menu") ||
            presentation_kind(json,event,"trail")) *allowed=false;
        else if (!presentation_kind(json,event,"userinfo"))
            target=presentation_target(json,event,presentation_kind(json,event,"print")?"target":"actor",&targeted);
    } else if (presentation_kind(json,root,"q2-composition")) {
        qa_json_id nested=qa_json_get(json,event,"event");
        if (presentation_kind(json,event,"kick")) *allowed=false;
        else if (presentation_kind(json,event,"missionpack-entity")) *allowed=true;
        else if (presentation_kind(json,event,"missionpack-player")) target=presentation_target(json,nested,"actor",&targeted);
        else if (presentation_kind(json,event,"grapple-prediction")) target=presentation_target(json,event,"actor",&targeted);
        else if (presentation_kind(json,nested,"score-log")) *allowed=false;
        else if (!presentation_kind(json,nested,"grapple-cable") && !presentation_kind(json,nested,"match-status"))
            target=presentation_target(json,nested,"actor",&targeted);
    } else if (presentation_kind(json,root,"q2-rerelease")) {
        if (presentation_kind(json,event,"autosave") || presentation_kind(json,event,"restart-level")) *allowed=false;
        else if (!presentation_kind(json,event,"alpha") && !presentation_kind(json,event,"dynamic-light") &&
            !presentation_kind(json,event,"player-dogtag") && !presentation_kind(json,event,"flashlight") &&
            qa_json_get(json,event,"actor")!=QA_JSON_NONE) target=presentation_target(json,event,"actor",&targeted);
    } else if (presentation_kind(json,root,"q3-source")) {
        if (presentation_kind(json,event,"console-command") || presentation_kind(json,event,"drop-client") ||
            presentation_kind(json,event,"log")) *allowed=false;
        else if (presentation_kind(json,event,"server-command")) {
            int64_t addressed;
            ok=qa_json_i64(json,qa_json_get(json,event,"client"),&addressed,error);
            if (ok) *allowed=addressed<0 || (uint64_t)addressed==client.slot;
        }
    } else if (presentation_kind(json,root,"q3-ballistics") && presentation_kind(json,event,"rail-award"))
        target=presentation_target(json,event,"actor",&targeted);
    else if (presentation_kind(json,root,"q2-weapon") && presentation_kind(json,event,"view-weapon"))
        target=presentation_target(json,event,"actor",&targeted);
    if (ok && *allowed && targeted) ok=presentation_own(app,row,json,target,player,allowed,error);
    qa_json_destroy(json); return ok;
}

static bool events_project(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player, uint32_t epoch,
    uint64_t after, bool initial, application_unified_events *out, qa_error *error)
{
    if (!app || !source || !player || !out || !epoch || after > app->unified_event_sequence ||
        !application_unified_source_current(app, source) || !application_unified_player_current(app, recipient, player))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source event projection has no current physical recipient");
    application_unified_events result = {.application = app, .source = *source, .recipient = recipient,
        .player = *player, .generation = app->protocol_events_generation,
        .through = app->unified_event_sequence, .count = app->unified_event_count,
        .resource_count = app->unified_event_resource_count,
        .registration_revision = app->unified_event_registration_revision,
        .persistent_revision = app->unified_persistent_revision};
    application_unified_json presentation = {0}, simulation = {0}, control = {0}, wire = {0};
    size_t presentations = 0, simulations = 0;
    size_t *references = result.resource_count ? calloc(result.resource_count, sizeof(*references)) : NULL;
    size_t reference_count = 0;
    bool ok = (!result.resource_count || references) &&
        application_unified_json_text(&presentation, "[", error) && application_unified_json_text(&simulation, "[", error);
    if (result.resource_count && !references)
        application_fail(error, QA_ERROR_MEMORY, "Retaining actual referenced Source sounds");
    size_t rows = initial ? app->unified_persistent_count : result.count;
    for (size_t i = 0; ok && i < rows; ++i) {
        const application_unified_event_record *row = initial ? &app->unified_persistent[i].event : app->unified_events + i;
        if (!initial && row->order < after) continue;
        qa_actor_id presentation_recipient, simulation_recipient;
        ok = application_unified_event_recipient(app, row, false, &presentation_recipient, error) &&
            application_unified_event_recipient(app, row, true, &simulation_recipient, error);
        bool presentation_allowed=false;
        if (ok && row->presentation.size && (!presentation_recipient.registry || qa_actor_id_equal(presentation_recipient, player->actor)))
            ok=presentation_for(app,row,recipient,player->actor,&presentation_allowed,error);
        if (ok && presentation_allowed) {
            ok = (!presentations || application_unified_json_text(&presentation, ",", error)) &&
                presentation_write(&presentation, app, row, error);
            ++presentations;
        }
        if (ok && row->simulation.size && (!simulation_recipient.registry ||
            (qa_actor_id_equal(simulation_recipient, player->actor) && row->client.owner == recipient.owner &&
             row->client.slot == recipient.slot && row->client.generation == recipient.generation))) {
            ok = (!simulations || application_unified_json_text(&simulation, ",", error)) &&
                simulation_write(&simulation, app, row, error);
            ++simulations;
            qa_unified_document *payload = NULL;
            if (ok) ok = qa_unified_document_create(QA_UNIFIED_CHECKPOINT, row->simulation, &payload, error);
            const qa_json_document *json = qa_unified_document_json(payload);
            qa_json_id root = qa_unified_document_root(payload);
            if (ok && qa_json_string_equal(json, qa_json_get(json, root, "kind"), "sound")) {
                bool found = false;
                for (size_t n = 0; n < result.resource_count; ++n) {
                    if (!qa_json_string_equal(json, qa_json_get(json, root, "resource"), app->unified_event_resources[n].id)) continue;
                    found = true;
                    size_t r = 0;
                    while (r < reference_count && references[r] != n) ++r;
                    if (r == reference_count) references[reference_count++] = n;
                    break;
                }
                if (!found) ok = application_fail(error, QA_ERROR_FORMAT, "Source sound lost its previously registered dictionary resource");
            }
            qa_unified_document_destroy(payload);
        }
    }
    qa_unified_document *events = NULL;
    qa_unified_document *presentation_value = NULL;
    qa_buffer encoded = {0}, bytes = {0}, simulation_encoded = {0}, simulation_bytes = {0};
    if (ok) ok = application_unified_json_text(&presentation, "]", error) &&
        application_unified_json_text(&simulation, "]", error) &&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){presentation.bytes.data, presentation.bytes.size}, &presentation_value, error) &&
        presentation_wire(&wire, qa_unified_document_json(presentation_value), qa_unified_document_root(presentation_value), 0, error) &&
        qa_unified_document_create(QA_UNIFIED_EVENTS_DOCUMENT, (qa_bytes){wire.bytes.data, wire.bytes.size}, &events, error) &&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){simulation.bytes.data, simulation.bytes.size}, &result.simulation, error) &&
        (initial || world_text_read(app, source, &result.world_text, error));
    result.world_text_revision = app->unified_world_text_revision;
    if (ok && (reference_count || presentations || simulations)) {
        result.controls = calloc(2, sizeof(*result.controls));
        if (!result.controls) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining real Source prerequisite controls");
    }
    if (ok && reference_count) {
        qa_unified_document **keys = calloc(reference_count, sizeof(*keys));
        if (!keys) ok = application_fail(error, QA_ERROR_MEMORY, "Projecting Source resource dictionary");
        for (size_t i = 0; ok && i < reference_count; ++i) {
            const application_unified_event_resource *row = app->unified_event_resources + references[i];
            ok = qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){row->key.data, row->key.size}, keys + i, error);
        }
        if (ok) ok = application_unified_resource_control(epoch, (const qa_unified_document *const *)keys,
            reference_count, result.controls + result.control_count, error);
        if (ok) ++result.control_count;
        for (size_t i = 0; keys && i < reference_count; ++i) qa_unified_document_destroy(keys[i]);
        free(keys);
    }
    if (ok && (presentations || simulations)) {
        ok = qa_unified_document_encode(events, &encoded, error) &&
            qa_unified_checkpoint_bytes((qa_bytes){encoded.data, encoded.size}, &bytes, error) &&
            qa_unified_document_encode(result.simulation, &simulation_encoded, error) &&
            qa_unified_checkpoint_bytes((qa_bytes){simulation_encoded.data, simulation_encoded.size}, &simulation_bytes, error) &&
            application_unified_json_text(&control, "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"events\",\"epoch\":", error) &&
            application_unified_json_natural(&control, epoch, error) && application_unified_json_text(&control, ",\"frame\":", error) &&
            application_unified_json_natural(&control, initial ? 0 : source->frame.number, error) &&
            application_unified_json_text(&control, ",\"payload\":", error) &&
            application_unified_json_append(&control, (qa_bytes){bytes.data, bytes.size}, error) &&
            application_unified_json_text(&control, ",\"simulation\":", error) &&
            application_unified_json_append(&control, (qa_bytes){simulation_bytes.data, simulation_bytes.size}, error) &&
            application_unified_json_text(&control, "}}", error) &&
            qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT, (qa_bytes){control.bytes.data, control.bytes.size},
                result.controls + result.control_count, error);
        if (ok) ++result.control_count;
    }
    if (ok && !application_unified_events_current(&result))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Source event projection changed its actual owner");
    qa_unified_document_destroy(events); qa_buffer_free(&encoded); qa_buffer_free(&bytes);
    qa_unified_document_destroy(presentation_value); application_unified_json_dispose(&wire);
    qa_buffer_free(&simulation_encoded); qa_buffer_free(&simulation_bytes); free(references);
    application_unified_json_dispose(&presentation); application_unified_json_dispose(&simulation);
    application_unified_json_dispose(&control);
    if (!ok) { application_unified_events_dispose(&result); return false; }
    *out = result;
    return true;
}

bool application_unified_events_read(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player, uint32_t epoch,
    uint64_t after, application_unified_events *out, qa_error *error)
{ return events_project(app, source, recipient, player, epoch, after, false, out, error); }

bool application_unified_events_initial_read(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player, uint32_t epoch,
    application_unified_events *out, qa_error *error)
{ return events_project(app, source, recipient, player, epoch, 0, true, out, error); }

bool application_unified_events_current(const application_unified_events *events)
{
    return events && events->application &&
        application_unified_source_current(events->application, &events->source) &&
        application_unified_player_current(events->application, events->recipient, &events->player) &&
        events->generation == events->application->protocol_events_generation &&
        events->through == events->application->unified_event_sequence &&
        events->count == events->application->unified_event_count &&
        events->resource_count == events->application->unified_event_resource_count &&
        events->registration_revision == events->application->unified_event_registration_revision &&
        events->persistent_revision == events->application->unified_persistent_revision &&
        events->world_text_revision == events->application->unified_world_text_revision;
}

void application_unified_events_dispose(application_unified_events *events)
{
    if (!events) return;
    qa_unified_document_destroy(events->simulation);
    qa_unified_document_destroy(events->world_text);
    for (size_t i = 0; i < events->control_count; ++i)
        qa_unified_document_destroy(events->controls[i]);
    free(events->controls);
    *events = (application_unified_events){0};
}
