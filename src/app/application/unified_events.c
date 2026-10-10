#include "internal.h"
#include "unified_events.h"
#include "unified_output_json.h"
#include "map_players_private.h"
#include "unified_output.h"
#include "unified_frame_private.h"
#include "../../network/unified/frame_internal.h"
#include "guest_q3_components.h"
#include "qa/application_native_q2_presentation.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

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
        component.content, QA_RULESET_Q3, true};
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
    if (!published)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source activation replacement lost its actual published provider");
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
    for (size_t i = 0; i < app->unified_event_resource_count; ++i) {
        application_unified_event_resource *old = app->unified_event_resources + i;
        bool same = old->resource == resource && opening_equal(&old->opening, opening);
        for (size_t j = 0; !same && j < old->custody_count; ++j)
            same = old->custodies[j].resource == resource && opening_equal(&old->custodies[j].opening, opening);
        if (same && !strcmp(qa_strings_cstr(qa_session_strings(app->session), old->content), source.product->identity) &&
            !strcmp(qa_strings_cstr(qa_session_strings(app->session), old->path), registration_path)) {
            uint64_t custody;
            bool ok = custody_retain(app->unified_event_resources + i, view, resource, opening, &custody, error) &&
                registration_bind(app, owner, kind, path, i, custody, error);
            if (ok) memcpy(id, old->id, QA_APPLICATION_RESOURCE_KEY_CAPACITY);
            return ok;
        }
    }
    uint64_t serial = 0;
    if (app->unified_event_resource_count)
        (void)qa_unified_resource_serial(
            app->unified_event_resources[app->unified_event_resource_count - 1].id, &serial);
    if (serial == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "Source resource dictionary serial exhausted");
    qa_unified_document *key = NULL;
    char actual_id[QA_APPLICATION_RESOURCE_KEY_CAPACITY];
    if (!application_unified_resource_key(serial + 1,
        source.product, registration_path, resource, &key, actual_id, error)) return false;
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

static char *event_alias(qa_application *app, qa_string_id id)
{ return id ? (char *)qa_strings_cstr(qa_session_strings(app->session), id) : NULL; }

bool application_unified_builtin_read(qa_application *app, const qa_builtin_event *v,
    qa_unified_builtin_event *out, qa_error *e)
{
    *out = (qa_unified_builtin_event){.kind = v->kind, .family = v->family,
        .actor = v->actor, .other = v->other, .resource = event_alias(app, v->resource),
        .text = event_alias(app, v->text), .item = event_alias(app, v->item),
        .origin = v->origin, .end = v->end, .direction = v->direction,
        .muzzle_angles = v->muzzle_angles, .muzzle_scale = v->muzzle_scale,
        .has_muzzle_pose = v->has_muzzle_pose, .volume = v->volume,
        .attenuation = v->attenuation, .value = v->value, .code = v->code,
        .channel = v->channel, .count = v->count, .frame = v->frame, .flags = v->flags,
        .ctf_red = v->ctf_status.red, .ctf_blue = v->ctf_status.blue,
        .ctf_flags = v->ctf_status.flags, .ctf_rune_items = v->ctf_status.rune_items,
        .ctf_capture_total = v->ctf_capture.total, .ctf_capture_blue = v->ctf_capture.blue,
        .q1_power = v->q1_powerup.power, .q1_power_expires = v->q1_powerup.expires};
    if (v->argument_count) {
        out->arguments = application_event_stream_alloc(app, v->argument_count * sizeof(*out->arguments),
            _Alignof(qa_unified_message_arg), e);
        if (!out->arguments) return false;
        out->argument_count = v->argument_count;
        for (size_t i = 0; i < v->argument_count; ++i) {
            const qa_builtin_message_arg *arg = v->arguments + i;
            out->arguments[i] = (qa_unified_message_arg){.kind = arg->kind};
            if (arg->kind == QA_BUILTIN_MESSAGE_STRING) out->arguments[i].text = event_alias(app, arg->value.text);
            else out->arguments[i].number = arg->value.number;
        }
    }
    if (v->prompt_choice_count) {
        out->prompt_choices = application_event_stream_alloc(app, v->prompt_choice_count * sizeof(*out->prompt_choices),
            _Alignof(qa_unified_prompt_choice), e);
        if (!out->prompt_choices) return false;
        out->prompt_choice_count = v->prompt_choice_count;
        for (size_t i = 0; i < v->prompt_choice_count; ++i)
            out->prompt_choices[i] = (qa_unified_prompt_choice){
                .label = event_alias(app, v->prompt_choices[i].label), .impulse = v->prompt_choices[i].impulse};
    }
    return true;
}
void application_unified_builtin_read_dispose(qa_unified_builtin_event *v)
{ *v = (qa_unified_builtin_event){0}; }

void application_unified_events_consume(qa_application *app, uint64_t next)
{
    app->event_peer_cursor = next;
    uint64_t retired = app->event_local_cursor < next ? app->event_local_cursor : next;
    qa_event_ring_retire(app->event_ring, retired);
}
void application_unified_events_clear(qa_application *app)
{
    app->event_local_cursor = qa_application_events_next(app);
    application_unified_events_consume(app, UINT64_MAX);
}

bool application_unified_event_append(qa_application *app,
    const application_unified_event_record *source, qa_error *error)
{
    application_event_write *write = app->event_write;
    application_event_view *view = application_event_stream_alloc(app, sizeof(*view),
        _Alignof(application_event_view), error);
    if (!view) return false;
    *view = (application_event_view){.event = *source};
    application_unified_event_record *record = &view->event;
    record->order = write->envelope->id;
    record->presentation = NULL;
    record->simulation = NULL;
    if (source->presentation) {
        record->presentation = application_event_stream_alloc(app, sizeof(*record->presentation),
            _Alignof(qa_unified_presentation_payload), error);
        if (!record->presentation) return false;
        *record->presentation = (qa_unified_presentation_payload){0};
        if (!qa_unified_record_clone_alloc(&qa_unified_presentation_payload_layout,
            source->presentation, record->presentation, qa_event_ring_alloc,
            &write->transaction, error)) return false;
        record->presentation_sequence = app->presentation_event_sequence++;
    } else record->presentation_sequence = 0;
    if (source->simulation) {
        record->simulation = application_event_stream_alloc(app, sizeof(*record->simulation),
            _Alignof(qa_unified_simulation_payload), error);
        if (!record->simulation) return false;
        *record->simulation = (qa_unified_simulation_payload){0};
        if (!qa_unified_record_clone_alloc(&qa_unified_simulation_payload_layout,
            source->simulation, record->simulation, qa_event_ring_alloc,
            &write->transaction, error)) return false;
        record->simulation_sequence = app->simulation_event_sequence++;
    } else record->simulation_sequence = 0;
    if (write->envelope->last_view) write->envelope->last_view->next = view;
    else write->envelope->views = view;
    write->envelope->last_view = view;
    return true;
}

typedef enum persistent_domain { PERSIST_NONE, PERSIST_UNIQUE, PERSIST_SOUND, PERSIST_STYLE, PERSIST_MUSIC, PERSIST_FINALE } persistent_domain;
bool application_unified_persistent_key(qa_application *app, const application_unified_event_record *row,
    application_persistent_key *out, bool *remove, qa_error *e)
{
    *out = (application_persistent_key){0}; *remove = false;
    if (!row->presentation) return true;
    application_persistent_key key = {0}; const char *path = NULL;
    const qa_unified_presentation_payload *p = row->presentation;
    if (p->kind == QA_UNIFIED_PRESENTATION_BUILTIN) {
        const qa_unified_builtin_event *v = &p->value.builtin;
        if (v->family == QA_GAME_Q1 && ((v->kind == QA_BUILTIN_SOUND && (v->flags & 1u)) ||
            (v->kind == QA_BUILTIN_EFFECT && !v->actor.registry && !(v->flags & UINT32_C(0x80000000)) &&
                v->resource && strcmp(v->resource, "colored-explosion") && strcmp(v->resource, "developer-message") &&
                strcmp(v->resource, "music") && strcmp(v->resource, "cutscene") && strcmp(v->resource, "sell-screen")))) {
            key.domain = PERSIST_UNIQUE; key.selector = row->presentation_sequence;
        } else if (v->family == QA_GAME_Q2 && (v->kind == QA_BUILTIN_STOP_SOUND ||
            (v->kind == QA_BUILTIN_SOUND && (v->flags & 1u)))) {
            key.domain = PERSIST_SOUND; path = v->resource;
            key.actor_registry = v->actor.registry; key.actor_generation = v->actor.generation;
            key.actor_slot = v->actor.slot; key.channel = v->channel;
            *remove = v->kind == QA_BUILTIN_STOP_SOUND;
        } else if (v->kind == QA_BUILTIN_LIGHT) {
            key.domain = PERSIST_STYLE; key.selector = (uint32_t)v->code;
        } else if (v->family == QA_GAME_Q1 && v->kind == QA_BUILTIN_EFFECT) {
            if (v->resource && !strcmp(v->resource, "music")) key.domain = PERSIST_MUSIC;
            else if ((v->flags & UINT32_C(0x80000000)) ||
                (v->resource && (!strcmp(v->resource, "cutscene") || !strcmp(v->resource, "sell-screen")))) key.domain = PERSIST_FINALE;
        }
    } else if (p->kind == QA_UNIFIED_PRESENTATION_Q2_MAP) {
        const qa_unified_q2_map_event *v = &p->value.q2_map;
        if (v->kind == QA_Q2_MAP_LIGHTSTYLE) { key.domain = PERSIST_STYLE; key.selector = (uint32_t)v->style; }
        else if (v->kind == QA_Q2_MAP_MUSIC) key.domain = PERSIST_MUSIC;
    }
    if (!key.domain) return true;
    key.provider = row->owner_generation ? row->provider : 0; key.generation = row->owner_generation;
    key.recipient_registry = row->recipient.registry; key.recipient_generation = row->recipient.generation;
    key.recipient_slot = row->recipient.slot;
    if (path && !qa_strings_intern_cstr(qa_session_strings(app->session), path, &key.resource, e)) return false;
    *out = key; return true;
}
bool application_unified_persistent_key_equal(const application_persistent_key *a,
    const application_persistent_key *b)
{
    return a->generation == b->generation && a->selector == b->selector &&
        a->actor_registry == b->actor_registry && a->actor_generation == b->actor_generation &&
        a->recipient_registry == b->recipient_registry && a->recipient_generation == b->recipient_generation &&
        a->provider == b->provider && a->domain == b->domain && a->actor_slot == b->actor_slot &&
        a->recipient_slot == b->recipient_slot && a->channel == b->channel && a->resource == b->resource;
}
void application_unified_persistent_dispose(qa_application *app)
{
    for (size_t i = 0; i < app->unified_persistent_count; ++i) {
        qa_event_lease_release(app->unified_persistent[i].lease);
    }
    free(app->unified_persistent); app->unified_persistent = NULL;
    app->unified_persistent_count = app->unified_persistent_capacity = 0;
}
bool application_unified_persistent_retire(qa_application *app, qa_actor_owner owner,
    qa_actor_id recipient, qa_error *e)
{
    if (!app || (!owner && !recipient.registry)) return application_fail(e, QA_ERROR_ARGUMENT, "Presentation retirement requires its actual owner or actor");
    application_unified_event_owner *activation = NULL;
    for (size_t i = 0; owner && i < app->unified_event_owner_count; ++i)
        if (app->unified_event_owners[i].provider == owner && app->unified_event_owners[i].active) activation = app->unified_event_owners + i;
    bool changed = activation != NULL;
    for (size_t i = 0; i < app->unified_persistent_count; ++i)
        changed |= owner ? app->unified_persistent[i].event.provider == owner : qa_actor_id_equal(app->unified_persistent[i].event.recipient, recipient);
    if (!changed) return true;
    if (app->unified_persistent_revision == UINT64_MAX) return application_fail(e, QA_ERROR_FORMAT, "Presentation revision exhausted");
    size_t kept = 0;
    for (size_t i = 0; i < app->unified_persistent_count; ++i) {
        application_unified_persistent_event row = app->unified_persistent[i];
        if (owner ? row.event.provider == owner : qa_actor_id_equal(row.event.recipient, recipient)) {
            qa_event_lease_release(row.lease);
        } else app->unified_persistent[kept++] = row;
    }
    app->unified_persistent_count = kept;
    if (activation) activation->active = false;
    ++app->unified_persistent_revision; return true;
}
bool application_unified_persistent_prepare(qa_application *app,
    application_event_envelope *envelope, qa_error *error)
{
    size_t count = app->unified_persistent_count;
    for (application_event_view *view = envelope->views; view; view = view->next) {
        if (!application_unified_persistent_key(app, &view->event, &view->persistent_key,
            &view->persistent_remove, error)) return false;
        if (!view->persistent_key.domain) continue;
        bool present = false;
        for (size_t i = 0; i < app->unified_persistent_count; ++i)
            if (application_unified_persistent_key_equal(&view->persistent_key,
                &app->unified_persistent[i].key)) { present = true; break; }
        for (application_event_view *prior = envelope->views; prior != view; prior = prior->next)
            if (application_unified_persistent_key_equal(&view->persistent_key,
                &prior->persistent_key)) present = !prior->persistent_remove;
        if (view->persistent_remove) { if (present) --count; }
        else if (!present) ++count;
        if (count > app->unified_persistent_capacity) {
            app->event_write->transaction.blocked = true;
            return false;
        }
    }
    return true;
}

void application_unified_persistent_publish(qa_application *app,
    application_event_envelope *envelope)
{
    for (application_event_view *view = envelope->views; view; view = view->next) {
        if (!view->persistent_key.domain) continue;
        size_t index = 0;
        while (index < app->unified_persistent_count &&
            !application_unified_persistent_key_equal(&view->persistent_key,
                &app->unified_persistent[index].key)) ++index;
        if (view->persistent_remove && index == app->unified_persistent_count) continue;
        if (index < app->unified_persistent_count) {
            qa_event_lease_release(app->unified_persistent[index].lease);
            memmove(app->unified_persistent + index, app->unified_persistent + index + 1,
                (--app->unified_persistent_count - index) * sizeof(*app->unified_persistent));
        }
        if (!view->persistent_remove) {
            application_unified_event_record record = view->event;
            record.simulation = NULL;
            record.simulation_sequence = 0;
            record.link_presentation = false;
            app->unified_persistent[app->unified_persistent_count++] =
                (application_unified_persistent_event){.event = record, .key = view->persistent_key,
                    .lease = qa_event_ring_retain(app->event_ring, envelope->id)};
        }
        ++app->unified_persistent_revision;
    }
}
static bool persistent_domain_equal(const application_persistent_key *a, const application_persistent_key *b, bool slot)
{
    if (a->domain == PERSIST_UNIQUE || a->domain == PERSIST_SOUND || a->domain != b->domain || a->selector != b->selector) return false;
    return !slot || (a->recipient_registry == b->recipient_registry && a->recipient_generation == b->recipient_generation && a->recipient_slot == b->recipient_slot);
}
bool application_unified_event_owner_retire(qa_application *app, qa_actor_owner owner,
    const qa_source_frame *clock, qa_error *error)
{
    if (!app || !owner) return application_fail(error, QA_ERROR_ARGUMENT, "Presentation retirement requires its actual owner");
    application_unified_event_owner *activation = NULL;
    for (size_t i = 0; i < app->unified_event_owner_count; ++i)
        if (app->unified_event_owners[i].provider == owner && app->unified_event_owners[i].active)
            activation = app->unified_event_owners + i;
    if (!activation || !activation->generation)
        return application_unified_persistent_retire(app, owner, (qa_actor_id){0}, error);
    if (!clock || !clock->provider || (unsigned)clock->kind > QA_RULESET_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Presentation retirement requires the retained actual primary clock");
    application_event_write write;
    if (!application_event_stream_begin(app, QA_APPLICATION_EVENT_UNIFIED, &write, error)) return false;
    size_t count = app->unified_persistent_count;
    size_t *winners = count ? application_event_stream_alloc(app, count * sizeof(*winners),
        _Alignof(size_t), error) : NULL;
    if (count && !winners) goto abort;
    size_t winner_count = 0;
    for (size_t i = 0; i < count; ++i) {
        const application_unified_persistent_event *row = app->unified_persistent + i;
        if (row->event.provider == owner) continue;
        bool affected = false;
        for (size_t n = 0; !affected && n < count; ++n)
            if (app->unified_persistent[n].event.provider == owner)
                affected = persistent_domain_equal(&row->key, &app->unified_persistent[n].key, false);
        if (!affected) continue;
        size_t match = 0;
        while (match < winner_count && !persistent_domain_equal(&row->key,
            &app->unified_persistent[winners[match]].key, true)) ++match;
        if (match == winner_count) ++winner_count;
        winners[match] = i;
    }
    for (size_t i = 1; i < winner_count; ++i) {
        size_t value = winners[i], n = i;
        while (n && app->unified_persistent[winners[n - 1]].event.presentation_sequence >
            app->unified_persistent[value].event.presentation_sequence) {
            winners[n] = winners[n - 1]; --n;
        }
        winners[n] = value;
    }
    qa_unified_presentation_payload retired = {.kind = QA_UNIFIED_PRESENTATION_OWNER,
        .value.owner = {.kind = QA_UNIFIED_OWNER_RETIRED,
            .owner = {.provider = event_alias(app, owner), .generation = activation->generation}}};
    const qa_ruleset_descriptor *ruleset = qa_ruleset_read(clock->kind);
    application_unified_event_record first = {.presentation = &retired, .provider = owner,
        .content = activation->content, .family = ruleset ? ruleset->family : QA_GAME_Q1,
        .clock = clock->kind, .presentation_clock = clock->kind,
        .time_ns = clock->time_ns, .simulation_time_ns = clock->time_ns};
    if (!application_unified_event_append(app, &first, error)) goto abort;
    for (size_t i = 0; i < winner_count; ++i)
        if (!application_unified_event_append(app, &app->unified_persistent[winners[i]].event, error)) goto abort;
    if (!application_event_stream_commit(app, &write, error)) return false;
    return application_unified_persistent_retire(app, owner, (qa_actor_id){0}, error);
abort:
    application_event_stream_abort(app, &write, error);
    return false;
}

bool application_unified_event_emit(qa_application *app, qa_actor_owner owner,
    const qa_unified_presentation_payload *presentation, const qa_unified_simulation_payload *simulation,
    qa_actor_id recipient, qa_actor_id simulation_recipient, uint64_t time_ns,
    int32_t source_entity, bool has_source_entity, bool link_presentation, qa_error *e)
{
    application_unified_event_source source;
    if (!app || !app->session || app->destroy_requested || !application_unified_event_source_read(app, owner, &source, e) ||
        !source.product->identity || (!presentation && !simulation) ||
        (link_presentation && (!presentation || !simulation || simulation->kind != QA_UNIFIED_SIMULATION_MESSAGE)) ||
        (presentation && app->presentation_event_sequence == UINT64_MAX) ||
        (simulation && app->simulation_event_sequence == UINT64_MAX))
        return application_fail(e, QA_ERROR_ARGUMENT, "Source emission lost its actual owner, payload or sequence domain");
    application_provider *provider = source_provider(app, owner);
    if (provider && provider->event_activation_deferred) return application_fail(e, QA_ERROR_ARGUMENT, "Source emission awaits its prepared activation handoff");
    application_unified_event_record borrowed = {.presentation = (qa_unified_presentation_payload *)presentation,
        .simulation = (qa_unified_simulation_payload *)simulation, .recipient = recipient,
        .simulation_recipient = simulation_recipient, .provider = owner, .family = source.product->family,
        .clock = source.clock, .presentation_clock = source.clock, .time_ns = time_ns,
        .simulation_time_ns = time_ns, .source_entity = has_source_entity ? source_entity : 0,
        .has_source_entity = has_source_entity, .link_presentation = link_presentation};
    qa_q2_edition q2_edition; bool q2_profile; uint64_t q2_interval;
    if (!qa_application_native_q2_source_clock_read(app, owner, &q2_edition, &q2_interval, &q2_profile, e)) return false;
    if (q2_profile) {
        qa_ruleset_id actual = q2_edition == QA_Q2_CLASSIC ? QA_RULESET_Q2_CLASSIC : QA_RULESET_Q2_RERELEASE;
        if (source.clock != actual) return application_fail(e, QA_ERROR_FORMAT, "Q2 Source rules differ from its emitted clock receipt");
        borrowed.q2_source_profile = q2_edition == QA_Q2_CLASSIC ? 1 : 2; borrowed.q2_source_interval_ns = q2_interval;
    }
    bool activated = false;
    for (size_t i = 0; i < app->unified_event_owner_count; ++i) {
        const application_unified_event_owner *receipt = app->unified_event_owners + i;
        if (receipt->provider != owner || !receipt->active) continue;
        const char *identity = event_alias(app, receipt->content);
        if (!identity || strcmp(identity, source.product->identity) || source.component != (receipt->generation != 0))
            return application_fail(e, QA_ERROR_ARGUMENT, "Source presentation activation changed its actual content");
        borrowed.owner_generation = receipt->generation; activated = true;
    }
    if (!activated) return application_fail(e, QA_ERROR_ARGUMENT, "Source event has no actual presentation activation receipt");
    if (simulation && simulation_recipient.registry) {
        bool found = false;
        for (size_t i = 0; app->players && i < app->players->count; ++i) {
            const application_player_record *player = app->players->records + i;
            if (!player->retiring && qa_actor_id_equal(player->actor, simulation_recipient)) {
                borrowed.client = player->remote_client; found = true; break;
            }
        }
        if (!found) borrowed.simulation = NULL;
    }
    if (!borrowed.simulation) borrowed.link_presentation = false;
    if (!presentation && !borrowed.simulation) return true;
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, ""); qa_clock_state clock;
    if (primary && qa_session_clock(app->session, primary->owner, &clock)) {
        borrowed.clock = clock.frame.kind; borrowed.simulation_time_ns = clock.frame.time_ns;
    }
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), source.product->identity, &borrowed.content, e)) return false;
    application_event_write write;
    bool own = app->event_write == NULL;
    if (own && !application_event_stream_begin(app, QA_APPLICATION_EVENT_UNIFIED, &write, e)) return false;
    if (!application_unified_event_append(app, &borrowed, e)) {
        if (own) application_event_stream_abort(app, &write, e);
        return false;
    }
    return !own || application_event_stream_commit(app, &write, e);
}

static double source_time(uint64_t ns, qa_ruleset_id clock, bool milliseconds)
{
    if (clock == QA_RULESET_Q3) {
        uint32_t bits = (uint32_t)(ns / UINT64_C(1000000)); int32_t signed_bits;
        memcpy(&signed_bits, &bits, sizeof(bits)); return milliseconds ? signed_bits : (double)signed_bits / 1000;
    }
    return (double)ns / (milliseconds ? 1e6 : 1e9);
}
static qa_unified_armor_state armor_read(qa_application *app, const qa_armor *a)
{
    qa_unified_armor_state result = {.kind = a->regular.kind,
        .item = event_alias(app, a->regular.item), .points = a->regular.points,
        .powered_kind = a->powered.kind, .cells = a->powered.cells};
    if (a->regular.kind == QA_ARMOR_Q1) result.absorption = a->regular.protection.q1_absorption;
    else if (a->regular.kind == QA_ARMOR_Q2) { result.normal = a->regular.protection.q2.normal; result.energy = a->regular.protection.q2.energy; }
    else if (a->regular.kind == QA_ARMOR_Q3) result.protection = a->regular.protection.q3_protection;
    return result;
}
bool application_unified_damage_emit(qa_application *app, const qa_damage_outcome *outcome, qa_error *e)
{
    if (!app || !outcome) return application_fail(e, QA_ERROR_ARGUMENT, "Damage event has no actual outcome");
    const qa_damage_request *request = &outcome->request; const qa_attack *attack = &request->attack;
    application_provider *source = source_provider(app, attack->weapon_provider);
    if (!source || !source->launch) return application_fail(e, QA_ERROR_FORMAT, "Damage attack lost its genuine source weapon provider");
    qa_ruleset_id clock = source->launch->selection.clock.kind;
    bool ms = clock == QA_RULESET_Q2_RERELEASE || clock == QA_RULESET_Q3;
    application_event_write write;
    bool own = app->event_write == NULL;
    if (own && !application_event_stream_begin(app, QA_APPLICATION_EVENT_UNIFIED, &write, e)) return false;
    qa_unified_simulation_payload payload = {.kind = QA_UNIFIED_SIMULATION_DAMAGE};
    qa_unified_damage_event *d = &payload.value.damage;
    *d = (qa_unified_damage_event){.stale = outcome->stale, .survived = outcome->survived,
        .result = outcome->result, .inflictor_center = outcome->inflictor_center,
        .request = {.attack = {.sequence = attack->sequence, .time = source_time(attack->time_ns, clock, ms), .milliseconds = ms,
            .attacker = attack->attacker, .inflictor = attack->inflictor, .projectile = attack->projectile,
            .weapon = event_alias(app, attack->weapon), .weapon_provider = event_alias(app, attack->weapon_provider),
            .combat_provider = event_alias(app, attack->combat_provider), .inventory_provider = event_alias(app, attack->inventory_provider),
            .movement_provider = event_alias(app, attack->movement_provider),
            .powerup_owner = attack->powerup_applied ? event_alias(app, attack->powerup_owner) : NULL,
            .cause = attack->cause, .q1_death_type = attack->cause.kind == QA_CAUSE_Q1 ? event_alias(app, attack->cause.source.q1.death_type) : NULL},
            .target = request->target, .amount = request->amount, .knockback = request->knockback,
            .direction = request->direction, .point = request->point, .normal = request->normal, .radius = request->radius}};
    /* The q1 text is the wire identity; its session-local ordinal has no wire meaning. */
    if (attack->cause.kind == QA_CAUSE_Q1) d->request.attack.cause.source.q1.death_type = 0;
    if (outcome->mutation_count) {
        d->mutations = application_event_stream_alloc(app, outcome->mutation_count * sizeof(*d->mutations),
            _Alignof(qa_unified_damage_mutation), e);
        if (!d->mutations) {
            if (own) application_event_stream_abort(app, &write, e);
            return false;
        }
        d->mutation_count = outcome->mutation_count;
        for (size_t i = 0; i < outcome->mutation_count; ++i) {
            const qa_damage_mutation *m = outcome->mutations + i; qa_unified_damage_mutation *v = d->mutations + i;
            *v = (qa_unified_damage_mutation){.kind = m->kind};
            switch (m->kind) {
            case QA_MUTATION_HEALTH: v->health_before = m->value.health.before; v->health_after = m->value.health.after; break;
            case QA_MUTATION_ARMOR: v->armor_before = armor_read(app, &m->value.armor.before); v->armor_after = armor_read(app, &m->value.armor.after); break;
            case QA_MUTATION_SOURCE_VELOCITY: v->before = m->value.velocity.before; v->after = m->value.velocity.after; v->movement_provider = event_alias(app, m->value.velocity.movement); break;
            case QA_MUTATION_IMPULSE: v->impulse = m->value.impulse.value; v->movement_provider = event_alias(app, m->value.impulse.movement); break;
            }
        }
    }
    bool ok = application_unified_event_emit(app, attack->weapon_provider, NULL, &payload,
        (qa_actor_id){0}, (qa_actor_id){0}, attack->time_ns, 0, false, false, e);
    if (!own) return ok;
    if (!ok) { application_event_stream_abort(app, &write, e); return false; }
    return application_event_stream_commit(app, &write, e);
}

typedef struct actor_check { qa_application *app; bool checkpoint, rebind; } actor_check;
static bool actor_reference(void *context, qa_actor_id in, qa_actor_id *out, qa_error *e)
{
    actor_check *check = context; qa_actor_id current;
    if (!check->checkpoint) {
        qa_saved_actor_id saved;
        if (!qa_actors_save_reference(qa_session_actors(check->app->session), in, &saved, e)) return false;
        *out = in; return true;
    }
    if (!qa_actors_reference_saved(qa_session_actors(check->app->session),
        (qa_saved_actor_id){in.generation, in.slot}, true, &current, e)) return false;
    *out = check->rebind ? current : in; return true;
}
bool application_unified_event_actors_valid(qa_application *app, const application_unified_event_record *r, qa_error *e)
{
    actor_check check = {.app = app, .checkpoint = r->payload_checkpoint};
    return (!r->presentation || qa_unified_record_actor_remap(&qa_unified_presentation_payload_layout, r->presentation, actor_reference, &check, e)) &&
        (!r->simulation || qa_unified_record_actor_remap(&qa_unified_simulation_payload_layout, r->simulation, actor_reference, &check, e));
}
bool application_unified_event_recipient(qa_application *app, const application_unified_event_record *row,
    bool simulation, qa_actor_id *out, qa_error *e)
{
    qa_actor_id actor = simulation ? row->simulation_recipient : row->recipient;
    if (!actor.registry || !row->payload_checkpoint) { *out = actor; return true; }
    return qa_actors_reference_saved(qa_session_actors(app->session), simulation ?
        row->simulation_recipient_saved : row->recipient_saved, true, out, e);
}
bool application_unified_events_restore_finish(qa_application *app, qa_error *error)
{
    actor_check check = {.app = app, .checkpoint = true, .rebind = true};
    for (size_t i = 0; i < app->unified_persistent_count; ++i) {
        application_unified_persistent_event *slot = app->unified_persistent + i;
        application_unified_event_record *record = &slot->event;
        if (!record->payload_checkpoint) continue;
        if (!application_unified_event_recipient(app, record, false, &record->recipient, error) ||
            !application_unified_event_recipient(app, record, true, &record->simulation_recipient, error) ||
            (record->presentation && !qa_unified_record_actor_remap(&qa_unified_presentation_payload_layout,
                record->presentation, actor_reference, &check, error)) ||
            (record->simulation && !qa_unified_record_actor_remap(&qa_unified_simulation_payload_layout,
                record->simulation, actor_reference, &check, error))) return false;
        record->payload_checkpoint = false;
        record->recipient_saved = record->simulation_recipient_saved = (qa_saved_actor_id){0};
        bool remove;
        if (!application_unified_persistent_key(app, record, &slot->key, &remove, error)) return false;
    }
    return true;
}

bool application_unified_world_text_read(qa_application *app, const application_unified_source *source,
    qa_unified_frame_lease *lease, qa_unified_frame_visuals *out, qa_error *e)
{
    double now = (double)source->frame.time_ns / 1e9;
    bool changed = app->unified_world_text_map != source->map_revision;
    size_t count = changed ? 0 : app->unified_world_text_count, kept = 0;
    for (size_t i = 0; i < count; ++i) {
        const application_unified_world_text *row = app->unified_world_text + i;
        changed |= (row->timed && row->expires <= now) || (!row->timed && (!row->observed || row->first_frame != source->frame.number));
    }
    if (changed && app->unified_world_text_revision == UINT64_MAX) return application_fail(e, QA_ERROR_FORMAT, "World text revision is exhausted");
    for (size_t i = 0; i < count; ++i) {
        application_unified_world_text row = app->unified_world_text[i];
        if ((row.timed && row.expires <= now) || (!row.timed && row.observed && row.first_frame != source->frame.number)) continue;
        if (!row.timed && !row.observed) { row.observed = true; row.first_frame = source->frame.number; }
        app->unified_world_text[kept++] = row;
    }
    if (changed) ++app->unified_world_text_revision;
    app->unified_world_text_count = kept; app->unified_world_text_map = source->map_revision;
    if (!kept) return true;
    out->world_text = application_unified_frame_alloc(lease, kept, sizeof(*out->world_text), e);
    if (!out->world_text) return application_fail(e, QA_ERROR_MEMORY, "Projecting retained Source world text");
    out->world_text_count = kept;
    for (size_t i = 0; i < kept; ++i) {
        const application_unified_world_text *s = app->unified_world_text + i;
        qa_unified_world_text *r = out->world_text + i;
        *r = (qa_unified_world_text){.origin = s->origin, .angles = s->angles,
            .color = {s->color.x, s->color.y, s->color.z, s->alpha}, .cell_size = s->cell_size,
            .distance_cull_factor = .004f, .billboard = s->billboard, .depth_test = s->depth_test};
        if (!application_unified_frame_string(lease, &r->content, event_alias(app, s->content), e) ||
            !application_unified_frame_string(lease, &r->text, event_alias(app, s->text), e)) return false;
    }
    return true;
}

static bool own(qa_actor_id actor, qa_actor_id player)
{ return !actor.registry || qa_actor_id_equal(actor, player); }
static bool simulation_for(const application_unified_event_record *row, qa_net_client_id recipient,
    qa_actor_id player)
{
    return row->simulation && (!row->simulation_recipient.registry ||
        (qa_actor_id_equal(row->simulation_recipient, player) && row->client.owner == recipient.owner &&
            row->client.slot == recipient.slot && row->client.generation == recipient.generation));
}
static bool presentation_for(const application_unified_event_record *row, qa_net_client_id recipient, qa_actor_id player)
{
    const qa_unified_presentation_payload *p = row->presentation;
    if (p->kind == QA_UNIFIED_PRESENTATION_BUILTIN) {
        const qa_unified_builtin_event *v = &p->value.builtin;
        if (v->family == QA_GAME_Q1) {
            if (v->kind == QA_BUILTIN_SOURCE_LOG || (v->kind == QA_BUILTIN_EFFECT && v->resource && !strcmp(v->resource, "developer-message"))) return false;
            if (v->kind == QA_BUILTIN_MESSAGE || v->kind == QA_BUILTIN_CENTERPRINT || v->kind == QA_BUILTIN_ANIMATION ||
                v->kind == QA_BUILTIN_ACHIEVEMENT || v->kind == QA_BUILTIN_Q1_POWERUP || v->kind == QA_BUILTIN_CTF_STATUS ||
                v->kind == QA_BUILTIN_SOURCE_PROMPT || v->kind == QA_BUILTIN_CLEAR_PROMPT) return own(v->actor, player);
        } else if (v->family == QA_GAME_Q2 && (v->kind == QA_BUILTIN_MESSAGE || v->kind == QA_BUILTIN_CENTERPRINT ||
            (v->kind == QA_BUILTIN_ITEM && v->code == 0))) return own(v->actor, player);
    } else if (p->kind == QA_UNIFIED_PRESENTATION_Q2_PLAYER) {
        const qa_unified_q2_player_event *v = &p->value.q2_player;
        if (v->kind == QA_Q2_PLAYER_STUFFTEXT || v->kind == QA_Q2_PLAYER_TRAIL || v->kind == QA_Q2_PLAYER_RESTART) return false;
        if (v->kind == QA_Q2_PLAYER_USERINFO || v->kind == QA_Q2_PLAYER_FLASHLIGHT || v->kind == QA_Q2_PLAYER_DOGTAG || v->kind == QA_Q2_PLAYER_ALPHA) return true;
        return own(v->actor, player);
    } else if (p->kind == QA_UNIFIED_PRESENTATION_Q2_MAP) {
        const qa_unified_q2_map_event *v = &p->value.q2_map;
        if (v->kind == QA_Q2_MAP_AUTOSAVE) return false;
        return own(v->recipient, player);
    } else if (p->kind == QA_UNIFIED_PRESENTATION_Q3) {
        const qa_unified_q3_event *v = &p->value.q3;
        if (v->kind == QA_UNIFIED_Q3_LOG || v->kind == QA_UNIFIED_Q3_CONSOLE_COMMAND || v->kind == QA_UNIFIED_Q3_DROP_CLIENT) return false;
        if (v->kind == QA_UNIFIED_Q3_SERVER_COMMAND) return v->client == -1 || (uint32_t)v->client == recipient.slot;
    } else if (p->kind == QA_UNIFIED_PRESENTATION_Q3_BALLISTIC && p->value.q3_ballistic.kind == QA_UNIFIED_Q3_RAIL_AWARD)
        return own(p->value.q3_ballistic.actor, player);
    else if (p->kind == QA_UNIFIED_PRESENTATION_Q2_PROTOCOL && p->value.q2_protocol.kind == QA_Q2_SVC_COMMAND) return false;
    return true;
}
typedef struct event_iterator {
    const qa_application *application;
    const application_event_view *view;
    qa_event_lease *lease;
    uint64_t cursor, end;
    size_t persistent;
    bool initial;
} event_iterator;

static const application_unified_event_record *event_iterator_next(event_iterator *iterator)
{
    if (iterator->initial) {
        if (iterator->persistent == iterator->application->unified_persistent_count) return NULL;
        const application_unified_persistent_event *slot =
            iterator->application->unified_persistent + iterator->persistent++;
        iterator->lease = slot->lease;
        return &slot->event;
    }
    while (!iterator->view) {
        if (iterator->cursor == iterator->end) return NULL;
        const application_event_envelope *envelope =
            application_event_stream_at(iterator->application, iterator->cursor++);
        if (envelope) iterator->view = envelope->views;
    }
    const application_unified_event_record *record = &iterator->view->event;
    iterator->view = iterator->view->next;
    return record;
}

static bool events_project(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player, uint32_t epoch,
    uint64_t after, bool initial, qa_unified_frame_lease *lease, application_unified_events *out, qa_error *e)
{
    if (!app || !source || !player || !out || !epoch || after > qa_application_events_next(app) ||
        !application_unified_source_current(app, source) || !application_unified_player_current(app, recipient, player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Source event projection has no current physical recipient");
    application_unified_events result = {.application = app, .source = *source, .recipient = recipient, .player = *player,
        .generation = app->protocol_events_generation, .through = qa_application_events_next(app),
        .count = (size_t)(qa_application_events_next(app) - qa_application_events_first(app)),
        .resource_count = app->unified_event_resource_count, .registration_revision = app->unified_event_registration_revision,
        .persistent_revision = app->unified_persistent_revision, .world_text_revision = app->unified_world_text_revision};
    uint64_t first = qa_application_events_first(app);
    event_iterator begin = {.application = app, .cursor = after > first ? after : first,
        .end = result.through, .initial = initial};
    event_iterator iterator = begin;
    const application_unified_event_record *r;
    size_t presentation_count = 0, simulation_count = 0, dependency_count = 0;
    uint64_t previous_order = 0;
    while ((r = event_iterator_next(&iterator))) {
        bool presentation = r->presentation && own(r->recipient, player->actor) && presentation_for(r, recipient, player->actor);
        bool simulation = simulation_for(r, recipient, player->actor);
        presentation_count += presentation; simulation_count += simulation;
        if ((presentation || simulation) && (initial || r->order != previous_order)) {
            ++dependency_count; previous_order = r->order;
        }
    }
    if (!presentation_count && !simulation_count) {
        if (!application_unified_events_current(&result))
            return application_fail(e, QA_ERROR_ARGUMENT, "Source event projection changed its actual owner");
        *out = result; return true;
    }
    qa_unified_frame_events *events = qa_unified_frame_lease_alloc(lease, 1, sizeof(*events), _Alignof(qa_unified_frame_events), e);
    if (!events) return false;
    if (!qa_unified_frame_lease_retain(lease, e)) return false;
    events->lease = lease;
    events->strings = qa_session_strings(app->session); qa_strings_retain(events->strings);
    events->epoch = epoch; events->frame = initial ? 0 : source->frame.number;
    events->dependencies = qa_unified_frame_lease_alloc(lease, dependency_count, sizeof(*events->dependencies), _Alignof(qa_event_lease *), e);
    if (presentation_count) events->presentation = qa_unified_frame_lease_alloc(lease, presentation_count, sizeof(*events->presentation), _Alignof(qa_unified_presentation_event), e);
    if (simulation_count) events->simulation = qa_unified_frame_lease_alloc(lease, simulation_count, sizeof(*events->simulation), _Alignof(qa_unified_simulation_event), e);
    bool ok = events->dependencies && (!presentation_count || events->presentation) && (!simulation_count || events->simulation);
    if (!ok) application_fail(e, QA_ERROR_MEMORY, "Projecting actual typed Source event arrays");
    iterator = begin; previous_order = 0;
    while (ok && (r = event_iterator_next(&iterator))) {
        bool presentation = r->presentation && own(r->recipient, player->actor) && presentation_for(r, recipient, player->actor);
        bool simulation = simulation_for(r, recipient, player->actor);
        if ((presentation || simulation) && (initial || r->order != previous_order)) {
            qa_event_lease *dependency;
            if (initial) { dependency = iterator.lease; qa_event_lease_retain(dependency); }
            else dependency = qa_event_ring_retain(app->event_ring, r->order);
            events->dependencies[events->dependency_count++] = dependency;
            previous_order = r->order;
        }
        if (presentation) {
            qa_unified_presentation_event borrowed = {.sequence = r->presentation_sequence,
                .seconds = source_time(r->time_ns, r->presentation_clock, false), .content = event_alias(app, r->content),
                .provider = event_alias(app, r->provider), .family = r->family, .recipient = r->recipient,
                .q2_profile = r->q2_source_profile, .q2_interval_ns = r->q2_source_interval_ns,
                .source_entity = r->source_entity, .has_source_entity = r->has_source_entity, .payload = *r->presentation};
            if (r->owner_generation) borrowed.owner = (qa_unified_presentation_owner){.provider = borrowed.provider, .generation = r->owner_generation};
            qa_unified_presentation_event *target = events->presentation + events->presentation_count++;
            *target = borrowed;
        }
        if (ok && simulation) {
            bool milliseconds = r->clock == QA_RULESET_Q2_RERELEASE;
            qa_unified_simulation_event borrowed = {.sequence = r->simulation_sequence,
                .time = source_time(r->simulation_time_ns, r->clock, milliseconds), .milliseconds = milliseconds,
                .private_audience = r->simulation_recipient.registry != 0, .client_slot = r->client.slot,
                .client_generation = r->client.generation, .payload = *r->simulation};
            borrowed.payload.linked_presentation = r->link_presentation;
            borrowed.payload.source_presentation_sequence = r->link_presentation ? r->presentation_sequence : 0;
            qa_unified_simulation_event *target = events->simulation + events->simulation_count++;
            *target = borrowed;
            if (ok && borrowed.payload.kind == QA_UNIFIED_SIMULATION_SOUND &&
                !application_unified_event_resource_read(app, borrowed.payload.value.sound.resource))
                ok = application_fail(e, QA_ERROR_FORMAT, "Source sound lost its registered dictionary resource");
        }
    }
    if (ok && (events->presentation_count || events->simulation_count)) {
        ok = qa_unified_document_create_events(&events, result.controls, e);
        if (ok) result.control_count = 1;
    }
    if (ok && !application_unified_events_current(&result)) ok = application_fail(e, QA_ERROR_ARGUMENT, "Source event projection changed its actual owner");
    qa_unified_frame_events_destroy(events);
    if (!ok) { application_unified_events_dispose(&result); return false; }
    *out = result; return true;
}
bool application_unified_events_read(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player, uint32_t epoch,
    uint64_t after, qa_unified_frame_lease *lease, application_unified_events *out, qa_error *e)
{ return events_project(app, source, recipient, player, epoch, after, false, lease, out, e); }
bool application_unified_events_initial_read(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player, uint32_t epoch,
    qa_unified_frame_lease *lease, application_unified_events *out, qa_error *e)
{ return events_project(app, source, recipient, player, epoch, 0, true, lease, out, e); }
bool application_unified_events_current(const application_unified_events *events)
{
    return events && events->application && application_unified_source_current(events->application, &events->source) &&
        application_unified_player_current(events->application, events->recipient, &events->player) &&
        events->generation == events->application->protocol_events_generation && events->through == qa_application_events_next(events->application) &&
        events->count == (size_t)(qa_application_events_next(events->application) - qa_application_events_first(events->application)) && events->resource_count == events->application->unified_event_resource_count &&
        events->registration_revision == events->application->unified_event_registration_revision &&
        events->persistent_revision == events->application->unified_persistent_revision && events->world_text_revision == events->application->unified_world_text_revision;
}
void application_unified_events_dispose(application_unified_events *events)
{
    if (!events) return;
    for (size_t i = 0; i < events->control_count; ++i) qa_unified_document_destroy(events->controls[i]);
    *events = (application_unified_events){0};
}
