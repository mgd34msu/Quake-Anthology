#include "network_q2_private.h"
#include "qa/persistence_content.h"
#include "qa/source_save.h"
#include <stdio.h>

static bool acquisition_copy(const qa_vfs_acquisition *source, qa_vfs_acquisition *out, qa_error *error)
{
    *out = *source;
    out->path = out->lookup_path = out->link_source = out->link_target = NULL;
    out->opening.order = NULL; out->opening.prefix = NULL;
    const char *texts[] = {source->path, source->lookup_path, source->link_source, source->link_target, source->opening.prefix};
    char **copies[] = {&out->path, &out->lookup_path, &out->link_source, &out->link_target};
    for (size_t i = 0; i < 5; ++i) {
        if (!texts[i]) continue;
        char *copy = application_network_q2_copy(texts[i], error);
        if (!copy) { qa_vfs_acquisition_dispose(out); return false; }
        if (i < 4) *copies[i] = copy;
        else out->opening.prefix = copy;
    }
    if (source->opening.order_count) {
        if (!source->opening.order || source->opening.order_count > SIZE_MAX / sizeof(qa_mount_id)) {
            qa_vfs_acquisition_dispose(out);
            return application_fail(error, QA_ERROR_FORMAT, "Q2 resource acquisition lost its genuine opening order");
        }
        qa_mount_id *order = malloc(source->opening.order_count * sizeof(*order));
        if (!order) { qa_vfs_acquisition_dispose(out); return application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 resource opening order"); }
        memcpy(order, source->opening.order, source->opening.order_count * sizeof(*order));
        out->opening.order = order;
    }
    return true;
}

static void held_free(application_q2_held_resource *held)
{
    qa_resource_release(held->resource); qa_vfs_acquisition_dispose(&held->opening);
    qa_vfs_destroy(held->view);
    free(held->instance); free(held->path); free(held->wire_path);
    *held = (application_q2_held_resource){0};
}

void application_network_q2_resources_free(qa_application_network_q2 *owner)
{
    for (size_t i = 0; i < owner->held_resource_count; ++i) held_free(&owner->held_resources[i]);
    free(owner->held_resources); owner->held_resources = NULL;
    owner->held_resource_count = owner->held_resource_capacity = 0;
}

static application_provider *provider_at(qa_application_network_q2 *owner, qa_actor_owner id)
{
    application_provider *found = NULL;
    for (size_t i = 0; i < owner->app->provider_count; ++i) {
        application_provider *provider = owner->app->providers[i];
        if (provider->owner != id) continue;
        if (found) return NULL;
        found = provider;
    }
    return found;
}

static bool held_append(qa_application_network_q2 *owner, application_q2_held_resource *held, qa_error *error)
{
    if (owner->held_resource_count == owner->held_resource_capacity) {
        size_t capacity = owner->held_resource_capacity ? owner->held_resource_capacity * 2 : 16;
        if (capacity < owner->held_resource_capacity || capacity > SIZE_MAX / sizeof(*owner->held_resources))
            return application_fail(error, QA_ERROR_MEMORY, "Q2 resource holder extent overflows");
        application_q2_held_resource *rows = realloc(owner->held_resources, capacity * sizeof(*rows));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 actual content holders");
        owner->held_resources = rows; owner->held_resource_capacity = capacity;
    }
    owner->held_resources[owner->held_resource_count++] = *held;
    *held = (application_q2_held_resource){0}; return true;
}

bool application_network_q2_visual_resource(qa_application_network_q2 *owner,
    const qa_application_visual_view *visual, unsigned model, uint32_t *out, qa_error *error)
{
    if (!visual || model > 3 || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 model acquisition requires its real BODY visual receipt");
    const char *path = visual->models[model];
    if (!path || !*path || *path == '*' || *path == '#' || visual->provider == owner->host.source.source_owner)
        return application_network_q2_resource(owner, 0, path, out, error);
    application_provider *provider = provider_at(owner, visual->provider);
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->launch || provider->product->id != visual->content)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 model acquisition lost its selected BODY content owner");
    for (size_t i = 0; i < owner->held_resource_count; ++i) {
        const application_q2_held_resource *held = &owner->held_resources[i];
        if (held->provider == visual->provider && !strcmp(held->path, path) &&
            (!visual->model_resources[model] || held->resource == visual->model_resources[model]) &&
            qa_sha256_equal(&held->identity, &provider->launch->identity))
            return application_network_q2_resource(owner, 0, held->wire_path, out, error);
    }
    qa_command_context request = {.owner = visual->provider, .origin = QA_COMMAND_SERVER,
        .dialect = visual->family == QA_GAME_Q2 ? QA_CONSOLE_Q2 :
            visual->family == QA_GAME_Q3 ? QA_CONSOLE_Q3 : QA_CONSOLE_Q1}, captured;
    if (!qa_application_capture_command_context(owner->app, &request, &captured, error)) return false;
    qa_vfs *files = qa_application_context_files(owner->app, &captured, NULL);
    if (!files) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 selected model has no actual provider file scope");
    application_q2_held_resource held = {.provider = provider->owner, .identity = provider->launch->identity};
    held.view = qa_vfs_clone(files, error);
    held.instance = application_network_q2_copy(provider->launch->selection.instance, error);
    held.path = application_network_q2_copy(path, error);
    bool ok = held.view && held.instance && held.path;
    if (ok && visual->model_resources[model]) {
        const qa_vfs_acquisition *opening = visual->model_openings[model];
        ok = opening && opening->resource_id == qa_resource_id(visual->model_resources[model]) &&
            qa_vfs_acquisition_retained(held.view, opening, error) && acquisition_copy(opening, &held.opening, error);
        if (ok) { held.resource = (qa_resource *)visual->model_resources[model]; qa_resource_retain(held.resource); }
    } else if (ok) ok = qa_vfs_acquire_receipt(held.view, path, &held.resource, &held.opening, error);
    if (ok) {
        qa_sha256_context hash; qa_sha256_digest digest; char hex[65], alias[64];
        qa_sha256_init(&hash);
        qa_sha256_update(&hash, (qa_bytes){held.identity.bytes, sizeof(held.identity.bytes)});
        qa_sha256_update(&hash, (qa_bytes){qa_resource_digest(held.resource)->bytes, 32});
        qa_sha256_update(&hash, (qa_bytes){(const uint8_t *)path, strlen(path)});
        qa_sha256_final(&hash, &digest); qa_sha256_hex(&digest, hex);
        const char *extension = strrchr(path, '.'), *separator = strrchr(path, '/');
        if (!extension || (separator && extension < separator) || strlen(extension) > 8)
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 mixed model has no genuine bounded file extension");
        else {
            snprintf(alias, sizeof(alias), "models/qa/%.32s%s", hex, extension);
            held.wire_path = application_network_q2_copy(alias, error);
            ok = held.wire_path != NULL;
        }
        for (size_t i = 0; ok && i < owner->held_resource_count; ++i) {
            const application_q2_held_resource *old = &owner->held_resources[i];
            if (!strcmp(old->wire_path, held.wire_path) &&
                (!qa_sha256_equal(qa_resource_digest(old->resource), qa_resource_digest(held.resource)) ||
                 strcmp(old->path, held.path) || !qa_sha256_equal(&old->identity, &held.identity)))
                ok = application_fail(error, QA_ERROR_FORMAT, "Q2 qualified resource name collides with another actual acquisition");
        }
    }
    if (ok && (!qa_application_command_context_active(owner->app, &captured) ||
        qa_application_context_files(owner->app, &captured, NULL) != files ||
        provider_at(owner, visual->provider) != provider))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 model acquisition changed its actual BODY file owner");
    if (ok) ok = application_network_q2_resource(owner, 0, held.wire_path, out, error) && held_append(owner, &held, error);
    held_free(&held); return ok;
}

size_t qa_application_network_q2_resource_count(const qa_application_network_q2 *owner)
{ return owner ? owner->held_resource_count : 0; }

bool qa_application_network_q2_resource_read(const qa_application_network_q2 *owner, size_t index,
    qa_application_network_q2_resource_view *out, qa_error *error)
{
    if (!owner || !out || index >= owner->held_resource_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 content inventory index leaves its actual holders");
    const application_q2_held_resource *held = &owner->held_resources[index];
    if (!held->view || !held->resource || !held->instance || !held->path || !held->wire_path ||
        held->opening.resource_id != qa_resource_id(held->resource) ||
        !qa_vfs_acquisition_retained(held->view, &held->opening, error)) return false;
    *out = (qa_application_network_q2_resource_view){held->provider, held->instance, held->path,
        held->wire_path, held->view, held->resource, &held->opening}; return true;
}

bool application_network_q2_download_resource(void *context, const char *path, const qa_vfs **view,
    qa_resource **resource, qa_vfs_acquisition *opening, bool *present, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    if (!owner || !path || !view || !resource || *resource || !opening || opening->path ||
        !present || !application_network_q2_current(owner, error)) return false;
    *view = NULL; *present = false;
    for (size_t i = 0; i < owner->held_resource_count; ++i) {
        application_q2_held_resource *held = &owner->held_resources[i];
        if (strcmp(held->wire_path, path)) continue;
        if (!qa_vfs_acquisition_retained(held->view, &held->opening, error) ||
            !acquisition_copy(&held->opening, opening, error)) return false;
        qa_resource_retain(held->resource); *resource = held->resource; *view = held->view; *present = true;
        return true;
    }
    return true;
}

static bool text_field(qa_source_save_io *io, char **field)
{
    const char *text = *field;
    if (!qa_source_save_text(io, &text)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && text) {
        *field = application_network_q2_copy(text, io->error); return *field != NULL;
    }
    return true;
}

static bool holder_fields(qa_application_network_q2 *owner, qa_source_save_io *io,
    application_q2_held_resource *held)
{
    qa_application_content_graph *graph = qa_application_content_graph_read(owner->app);
    uint64_t view = 0, pool = 0, resource = 0;
    if (!graph) return application_fail(io->error, QA_ERROR_ARGUMENT, "Q2 holder cold codec requires its actual content graph");
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        view = qa_application_content_view_id(graph, held->view);
        if (!view || !qa_application_content_resource_id(graph, held->resource, &pool, &resource))
            return application_fail(io->error, QA_ERROR_ARGUMENT, "Q2 holder is absent from the real immutable content inventory");
    }
    if (!text_field(io, &held->instance) || !text_field(io, &held->path) || !text_field(io, &held->wire_path) ||
        !qa_source_save_bytes(io, held->identity.bytes, 32) || !qa_source_save_u64(io, &view) ||
        !qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &resource)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        application_provider *provider = NULL;
        for (size_t i = 0; i < owner->app->provider_count; ++i) {
            application_provider *candidate = owner->app->providers[i];
            if (candidate->launch && held->instance && !strcmp(candidate->launch->selection.instance, held->instance)) {
                if (provider) return application_fail(io->error, QA_ERROR_FORMAT, "Q2 restored holder aliases Source instances");
                provider = candidate;
            }
        }
        const qa_resource *actual = qa_application_content_resource(graph, pool, resource);
        if (!provider || !qa_sha256_equal(&held->identity, &provider->launch->identity) || !actual ||
            !held->path || !*held->path || !held->wire_path || strncmp(held->wire_path, "models/qa/", 10) ||
            !qa_application_content_claim_view(graph, view, &held->view, io->error))
            return application_fail(io->error, QA_ERROR_FORMAT, "Q2 restored holder lost its real provider or immutable bytes");
        held->provider = provider->owner; held->resource = (qa_resource *)actual; qa_resource_retain(held->resource);
    }
    qa_vfs_acquisition *opening = &held->opening;
    if (!qa_source_save_u64(io, &opening->mount) || !qa_source_save_u64(io, &opening->resource_id) ||
        !text_field(io, &opening->path) || !text_field(io, &opening->lookup_path) ||
        !text_field(io, &opening->link_source) || !text_field(io, &opening->link_target) ||
        !qa_vfs_acquisition_opening_codec(io, held->view, opening) ||
        opening->resource_id != qa_resource_id(held->resource) ||
        !qa_vfs_acquisition_retained(held->view, opening, io->error)) return false;
    return true;
}

bool application_network_q2_resources_capture(qa_application_network_q2 *owner, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, owner->app->session, error)) return false;
    uint32_t version = 1; size_t count = owner->held_resource_count;
    bool ok = qa_source_save_u32(&io, &version) && qa_source_save_count(&io, &count, UINT32_MAX);
    for (size_t i = 0; ok && i < count; ++i) ok = holder_fields(owner, &io, &owner->held_resources[i]);
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}

bool application_network_q2_resources_restore(qa_application_network_q2 *owner, qa_bytes bytes, qa_error *error)
{
    if (owner->held_resource_count) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 holders restore requires an empty owner");
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, owner->app->session, bytes, error)) return false;
    uint32_t version = 0; size_t count = 0;
    bool ok = qa_source_save_u32(&io, &version) && version == 1 && qa_source_save_count(&io, &count, UINT32_MAX);
    for (size_t i = 0; ok && i < count; ++i) {
        application_q2_held_resource held = {0};
        ok = holder_fields(owner, &io, &held);
        for (size_t j = 0; ok && j < owner->held_resource_count; ++j)
            if (!strcmp(held.wire_path, owner->held_resources[j].wire_path))
                ok = application_fail(error, QA_ERROR_FORMAT, "Q2 restored holders alias a qualified wire path");
        if (ok) ok = held_append(owner, &held, error);
        held_free(&held);
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io); return ok;
}
