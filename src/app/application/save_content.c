#include "save_content.h"
#include "internal.h"
#include "qa/catalog_save.h"
#include "qa/source_save.h"
#include "qa/vfs_save.h"
#include "qa/application_q3_factory.h"

#include <stdlib.h>
#include <string.h>

typedef struct content_pool { qa_resource_pool *value; bool owned; qa_buffer bytes; } content_pool;
typedef struct content_catalog { qa_catalog *value; uint64_t pool; qa_buffer bytes; } content_catalog;
typedef struct content_view { qa_vfs *value; uint64_t pool, catalog; bool owned, qualified; qa_buffer bytes, baseline; } content_view;
typedef struct content_resource { qa_launch_resource value; uint64_t pool, resource; bool retained; } content_resource;
typedef struct content_instance {
    application_saved_instance_content value;
    uint64_t catalog, product_catalog, artifact_pool, artifact, declaration_pool, declaration;
    qa_product_id product;
    content_resource *interfaces;
    qa_launch_resource *interface_values;
    uint64_t *behaviors;
    const qa_catalog_weapon_behavior **behavior_values;
    qa_buffer options;
    qa_vfs_acquisition artifact_acquisition;
    bool artifact_retained, declaration_retained;
} content_instance;
struct qa_application_content_graph {
    bool restored, pending;
    content_pool *pools; size_t pool_count;
    content_catalog *catalogs; size_t catalog_count;
    content_view *views; size_t view_count;
    content_instance *instances; size_t instance_count;
    content_resource *resources; size_t resource_count;
    uint64_t application_pool, application_catalog, launch_catalog, launch_view;
};
static bool copy_bytes(const qa_buffer *, qa_buffer *, qa_error *);

static bool fail(qa_error *error, qa_status status, const char *message)
{ return application_fail(error, status, message); }
static bool append(void **values, size_t *count, size_t width, qa_error *error)
{
    if (*count == SIZE_MAX || *count + 1 > SIZE_MAX / width)
        return fail(error, QA_ERROR_MEMORY, "Content graph inventory extent is exhausted");
    void *grown = realloc(*values, (*count + 1) * width);
    if (!grown) return fail(error, QA_ERROR_MEMORY, "Retaining actual content graph inventory");
    *values = grown; memset((uint8_t *)grown + *count * width, 0, width); ++*count; return true;
}
static char *copy_text(const char *text, qa_error *error)
{
    if (!text) { fail(error, QA_ERROR_FORMAT, "Content graph text has no actual owner"); return NULL; }
    size_t length = strlen(text);
    if (length == SIZE_MAX) { fail(error, QA_ERROR_MEMORY, "Content graph text extent is exhausted"); return NULL; }
    char *copy = malloc(length + 1);
    if (!copy) { fail(error, QA_ERROR_MEMORY, "Retaining content graph text"); return NULL; }
    memcpy(copy, text, length + 1); return copy;
}

uint64_t qa_application_content_pool_id(const qa_application_content_graph *g, const qa_resource_pool *pool)
{ if (g && pool) for (size_t i = 0; i < g->pool_count; ++i) if (g->pools[i].value == pool) return i + 1; return 0; }
uint64_t qa_application_content_catalog_id(const qa_application_content_graph *g, const qa_catalog *catalog)
{ if (g && catalog) for (size_t i = 0; i < g->catalog_count; ++i) if (g->catalogs[i].value == catalog) return i + 1; return 0; }
uint64_t qa_application_content_view_id(const qa_application_content_graph *g, const qa_vfs *view)
{ if (g && view) for (size_t i = 0; i < g->view_count; ++i) if (g->views[i].value == view) return i + 1; return 0; }
qa_resource_pool *qa_application_content_pool(const qa_application_content_graph *g, uint64_t id)
{ return g && id && id <= g->pool_count ? g->pools[id - 1].value : NULL; }
qa_catalog *qa_application_content_catalog(const qa_application_content_graph *g, uint64_t id)
{ return g && id && id <= g->catalog_count ? g->catalogs[id - 1].value : NULL; }
qa_vfs *qa_application_content_view(const qa_application_content_graph *g, uint64_t id)
{ return g && id && id <= g->view_count ? g->views[id - 1].value : NULL; }
const qa_resource *qa_application_content_resource(const qa_application_content_graph *g, uint64_t pool, uint64_t resource)
{ return resource ? qa_resource_pool_find(qa_application_content_pool(g, pool), resource) : NULL; }
bool qa_application_content_resource_id(const qa_application_content_graph *g,
    const qa_resource *value, uint64_t *pool, uint64_t *resource)
{
    if (!g || !value || !pool || !resource || pool == resource) return false;
    uint64_t id = qa_resource_id(value);
    if (!id) return false;
    for (size_t i = 0; i < g->pool_count; ++i) {
        if (qa_resource_pool_find(g->pools[i].value, id) != value) continue;
        *pool = i + 1; *resource = id; return true;
    }
    return false;
}

static bool add_pool(void *opaque, const qa_resource_pool *pool, qa_error *error)
{
    qa_application_content_graph *g = opaque;
    if (!pool) return fail(error, QA_ERROR_ARGUMENT, "Content visitor supplied no actual pool");
    if (qa_application_content_pool_id(g, pool)) return true;
    if (!append((void **)&g->pools, &g->pool_count, sizeof(*g->pools), error)) return false;
    g->pools[g->pool_count - 1].value = (qa_resource_pool *)pool; return true;
}
static bool add_view(void *opaque, const qa_vfs *view, qa_error *error)
{
    qa_application_content_graph *g = opaque;
    if (!view) return fail(error, QA_ERROR_ARGUMENT, "Content visitor supplied no actual VFS");
    if (qa_application_content_view_id(g, view)) return true;
    qa_resource_pool *pool = qa_vfs_resources(view);
    if (!add_pool(g, pool, error) || !append((void **)&g->views, &g->view_count, sizeof(*g->views), error)) return false;
    g->views[g->view_count - 1] = (content_view){.value = (qa_vfs *)view, .pool = qa_application_content_pool_id(g, pool)};
    return true;
}
static bool add_catalog(void *opaque, const qa_catalog *catalog, qa_error *error)
{
    qa_application_content_graph *g = opaque;
    if (!catalog) return fail(error, QA_ERROR_ARGUMENT, "Content visitor supplied no actual catalog");
    if (qa_application_content_catalog_id(g, catalog)) return true;
    qa_resource_pool *pool = qa_catalog_resources(catalog);
    const qa_vfs *view = qa_catalog_files(catalog);
    if (!pool || qa_vfs_resources(view) != pool || !add_pool(g, pool, error) || !add_view(g, view, error) ||
        !append((void **)&g->catalogs, &g->catalog_count, sizeof(*g->catalogs), error)) return false;
    uint64_t id = g->catalog_count;
    g->catalogs[id - 1] = (content_catalog){.value = (qa_catalog *)catalog, .pool = qa_application_content_pool_id(g, pool)};
    content_view *row = &g->views[qa_application_content_view_id(g, view) - 1];
    if (row->catalog) return fail(error, QA_ERROR_FORMAT, "Two catalogs claim the same private VFS destructor");
    row->catalog = id; return true;
}
static bool copy_resource(qa_application_content_graph *g, qa_resource_pool *pool,
    const qa_launch_resource *source, content_resource *out, qa_error *error)
{
    if (!source || !source->resource || !source->path || !add_pool(g, pool, error)) return false;
    uint64_t id = qa_resource_id(source->resource);
    if (!id || qa_resource_pool_find(pool, id) != source->resource)
        return fail(error, QA_ERROR_FORMAT, "Source content resource is outside its actual pool");
    out->value.product = source->product; out->value.resource = source->resource;
    out->pool = qa_application_content_pool_id(g, pool); out->resource = id;
    out->value.path = copy_text(source->path, error);
    if (!out->value.path) return false;
    qa_resource_retain((qa_resource *)out->value.resource); out->retained = true; return true;
}
static bool instance_collect(qa_application_content_graph *g, const qa_launch_instance *source,
    const application_provider *provider, qa_error *error)
{
    qa_catalog *catalog = qa_launch_instance_catalog(source);
    if (!catalog || !provider || !provider->product_catalog || !provider->product ||
        !add_catalog(g, catalog, error) || !add_catalog(g, provider->product_catalog, error) ||
        !add_view(g, source->content, error) ||
        !append((void **)&g->instances, &g->instance_count, sizeof(*g->instances), error)) return false;
    content_instance *row = &g->instances[g->instance_count - 1];
    row->catalog = qa_application_content_catalog_id(g, catalog);
    row->product_catalog = qa_application_content_catalog_id(g, provider->product_catalog);
    row->product = provider->product->id;
    if (qa_catalog_product(provider->product_catalog, row->product) != provider->product)
        return fail(error, QA_ERROR_FORMAT, "Provider product has no actual retained catalog");
    row->value.view = qa_application_content_view_id(g, source->content);
    qa_launch_restored_instance *value = &row->value.source;
    value->catalog = catalog; value->content = source->content; value->identity = source->identity;
    value->selection = source->selection;
    value->selection.instance = value->selection.implementation = value->selection.artifact = value->selection.component = NULL;
    value->selection.options = (qa_bytes){0};
    if (!(value->selection.instance = copy_text(source->selection.instance, error)) ||
        !(value->selection.implementation = copy_text(source->selection.implementation, error)) ||
        !(value->selection.artifact = copy_text(source->selection.artifact, error)) ||
        !(value->selection.component = copy_text(source->selection.component, error))) return false;
    if (source->selection.options.size) {
        row->options.data = malloc(source->selection.options.size);
        if (!row->options.data) return fail(error, QA_ERROR_MEMORY, "Retaining original provider configuration bytes");
        row->options.size = source->selection.options.size;
        memcpy(row->options.data, source->selection.options.data, row->options.size);
    }
    value->selection.options = (qa_bytes){row->options.data, row->options.size};
    qa_resource_pool *pool = qa_vfs_resources(source->content);
    if (source->artifact) { row->artifact_pool = qa_application_content_pool_id(g, pool); row->artifact = qa_resource_id(source->artifact);
        if (qa_resource_pool_find(pool, row->artifact) != source->artifact) return fail(error, QA_ERROR_FORMAT, "Provider artifact is outside its actual pool"); }
    if (source->declaration) { row->declaration_pool = qa_application_content_pool_id(g, pool); row->declaration = qa_resource_id(source->declaration);
        if (qa_resource_pool_find(pool, row->declaration) != source->declaration) return fail(error, QA_ERROR_FORMAT, "Provider declaration is outside its actual pool"); }
    value->artifact = source->artifact; value->declaration = source->declaration;
    if (source->artifact) {
        const qa_vfs_acquisition *receipt = source->artifact_acquisition;
        if (!receipt || receipt->resource_id != qa_resource_id(source->artifact) ||
            !qa_vfs_acquisition_valid(source->content, receipt, error))
            return fail(error, QA_ERROR_FORMAT, "Provider artifact lost its actual opening receipt");
        row->artifact_acquisition = (qa_vfs_acquisition){.mount = receipt->mount, .resource_id = receipt->resource_id};
        row->artifact_acquisition.path = copy_text(receipt->path, error);
        row->artifact_acquisition.lookup_path = copy_text(receipt->lookup_path, error);
        if (receipt->link_source) row->artifact_acquisition.link_source = copy_text(receipt->link_source, error);
        if (receipt->link_target) row->artifact_acquisition.link_target = copy_text(receipt->link_target, error);
        if (!row->artifact_acquisition.path || !row->artifact_acquisition.lookup_path ||
            (receipt->link_source && !row->artifact_acquisition.link_source) ||
            (receipt->link_target && !row->artifact_acquisition.link_target)) return false;
        value->artifact_acquisition = &row->artifact_acquisition;
    }
    if (value->artifact) { qa_resource_retain((qa_resource *)value->artifact); row->artifact_retained = true; }
    if (value->declaration) { qa_resource_retain((qa_resource *)value->declaration); row->declaration_retained = true; }
    if (source->interface_count > SIZE_MAX / sizeof(*row->interfaces) ||
        source->behavior_count > SIZE_MAX / sizeof(*row->behaviors)) return fail(error, QA_ERROR_MEMORY, "Provider metadata extent is exhausted");
    row->interfaces = source->interface_count ? calloc(source->interface_count, sizeof(*row->interfaces)) : NULL;
    row->behaviors = source->behavior_count ? calloc(source->behavior_count, sizeof(*row->behaviors)) : NULL;
    if ((source->interface_count && !row->interfaces) || (source->behavior_count && !row->behaviors))
        return fail(error, QA_ERROR_MEMORY, "Retaining actual provider interface and behavior order");
    value->interface_count = source->interface_count; value->behavior_count = source->behavior_count;
    for (size_t i = 0; i < source->interface_count; ++i)
        if (!copy_resource(g, pool, &source->interfaces[i], &row->interfaces[i], error)) return false;
    for (size_t i = 0; i < source->behavior_count; ++i) {
        size_t ordinal = 0;
        while (ordinal < qa_catalog_weapon_behavior_count(catalog) &&
            qa_catalog_weapon_behavior_at(catalog, ordinal) != source->behaviors[i]) ++ordinal;
        if (ordinal == qa_catalog_weapon_behavior_count(catalog)) return fail(error, QA_ERROR_FORMAT, "Provider behavior is outside its actual catalog");
        row->behaviors[i] = ordinal + 1;
    }
    row->value.product_catalog = provider->product_catalog; row->value.product = provider->product;
    row->interface_values = value->interface_count ? calloc(value->interface_count, sizeof(*row->interface_values)) : NULL;
    row->behavior_values = value->behavior_count ? calloc(value->behavior_count, sizeof(*row->behavior_values)) : NULL;
    if ((value->interface_count && !row->interface_values) || (value->behavior_count && !row->behavior_values))
        return fail(error, QA_ERROR_MEMORY, "Retaining provider descriptor arrays");
    for (size_t i = 0; i < value->interface_count; ++i) row->interface_values[i] = row->interfaces[i].value;
    for (size_t i = 0; i < value->behavior_count; ++i) row->behavior_values[i] = source->behaviors[i];
    value->interfaces = row->interface_values; value->behaviors = row->behavior_values;
    return true;
}

bool application_save_content_collect(const qa_application *app, qa_application_content_visit_fn visit,
    void *context, qa_application_content_graph **out, qa_error *error)
{
    const qa_launch_snapshot *launch = app ? qa_application_launch(app) : NULL;
    if (!app || !app->resources || !app->catalog || !launch || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Content graph capture requires actual committed owners and empty output");
    qa_application_content_graph *g = calloc(1, sizeof(*g));
    if (!g) return fail(error, QA_ERROR_MEMORY, "Retaining actual application content graph");
    bool ok = add_pool(g, app->resources, error) && add_catalog(g, app->catalog, error) &&
        add_catalog(g, qa_launch_snapshot_catalog(launch), error) && add_view(g, qa_launch_snapshot_mounts(launch), error);
    if (ok) {
        g->application_pool = qa_application_content_pool_id(g, app->resources);
        g->application_catalog = qa_application_content_catalog_id(g, app->catalog);
        g->launch_catalog = qa_application_content_catalog_id(g, qa_launch_snapshot_catalog(launch));
        g->launch_view = qa_application_content_view_id(g, qa_launch_snapshot_mounts(launch));
    }
    for (size_t i = 0; ok && i < qa_launch_snapshot_instance_count(launch); ++i) {
        const qa_launch_instance *instance = qa_launch_snapshot_instance(launch, i);
        const application_provider *provider = instance ? instance->state : NULL;
        ok = instance_collect(g, instance, provider, error);
    }
    g->resource_count = qa_launch_snapshot_resource_count(launch);
    if (ok && g->resource_count) {
        if (g->resource_count > SIZE_MAX / sizeof(*g->resources) ||
            !(g->resources = calloc(g->resource_count, sizeof(*g->resources))))
            ok = fail(error, QA_ERROR_MEMORY, "Retaining physical launch resource order");
    }
    for (size_t i = 0; ok && i < g->resource_count; ++i)
        ok = copy_resource(g, qa_vfs_resources(qa_launch_snapshot_mounts(launch)),
            qa_launch_snapshot_resource(launch, i), &g->resources[i], error);
    const qa_application_content_visitor visitor = {.context = g, .pool = add_pool, .catalog = add_catalog, .view = add_view};
    if (ok) ok = qa_application_q3_content_visit(app, &visitor, error);
    if (ok && visit) ok = visit(context, app, &visitor, error);
    /* Preserve portable native admission only for the very same installed
     * view. Every encoding still checks its complete current native snapshot
     * against that admission baseline before emitting its original record. */
    const qa_application_content_graph *prepared = app->content_graph;
    if (ok && prepared && prepared->pending) {
        ok = application_save_content_ready(prepared, error);
        if (ok && (g->pool_count != prepared->pool_count || g->catalog_count != prepared->catalog_count || g->view_count != prepared->view_count))
            ok = fail(error, QA_ERROR_FORMAT, "Actual candidate content inventory differs from its prepared graph");
        for (size_t i = 0; ok && i < g->pool_count; ++i) if (g->pools[i].value != prepared->pools[i].value)
            ok = fail(error, QA_ERROR_FORMAT, "Actual candidate pool identity/order differs from saved graph");
        for (size_t i = 0; ok && i < g->catalog_count; ++i) if (g->catalogs[i].value != prepared->catalogs[i].value)
            ok = fail(error, QA_ERROR_FORMAT, "Actual candidate catalog identity/order differs from saved graph");
        for (size_t i = 0; ok && i < g->view_count; ++i) if (g->views[i].value != prepared->views[i].value)
            ok = fail(error, QA_ERROR_FORMAT, "Actual candidate VFS alias/order differs from saved graph");
    }
    for (size_t i = 0; ok && prepared && prepared->pending && i < g->view_count; ++i) {
        uint64_t id = qa_application_content_view_id(prepared, g->views[i].value);
        if (!id) continue;
        const content_view *saved = &prepared->views[id - 1];
        ok = copy_bytes(&saved->bytes, &g->views[i].bytes, error) &&
            copy_bytes(&saved->baseline, &g->views[i].baseline, error);
        if (ok) g->views[i].qualified = true;
    }
    if (!ok) {
        application_save_content_destroy(g);
        if (error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Actual content holders have unqualified graph edges");
        return false;
    }
    *out = g; return true;
}

void application_save_content_destroy(qa_application_content_graph *g)
{
    if (!g) return;
    for (size_t i = 0; g->instances && i < g->instance_count; ++i) {
        content_instance *row = &g->instances[i];
        qa_launch_restored_instance *v = &row->value.source;
        free((void *)v->selection.instance); free((void *)v->selection.implementation);
        free((void *)v->selection.artifact); free((void *)v->selection.component);
        for (size_t j = 0; row->interfaces && j < v->interface_count; ++j) {
            free((void *)row->interfaces[j].value.path);
            if (row->interfaces[j].retained) qa_resource_release((qa_resource *)row->interfaces[j].value.resource);
        }
        if (row->artifact_retained) qa_resource_release((qa_resource *)v->artifact);
        if (row->declaration_retained) qa_resource_release((qa_resource *)v->declaration);
        qa_vfs_acquisition_dispose(&row->artifact_acquisition);
        free(row->interfaces); free(row->interface_values); free(row->behaviors); free(row->behavior_values);
        qa_buffer_free(&row->options);
    }
    for (size_t i = 0; g->resources && i < g->resource_count; ++i) {
        free((void *)g->resources[i].value.path);
        if (g->resources[i].retained) qa_resource_release((qa_resource *)g->resources[i].value.resource);
    }
    /* Catalogs retain their pool and destroy their private view. Standalone
     * views and pools are destroyed only while their real ownership is here. */
    for (size_t i = 0; g->catalogs && i < g->catalog_count; ++i) {
        if (g->restored) qa_catalog_release(g->catalogs[i].value);
        qa_buffer_free(&g->catalogs[i].bytes);
    }
    for (size_t i = 0; g->views && i < g->view_count; ++i) {
        if (g->views[i].owned) qa_vfs_destroy(g->views[i].value);
        qa_buffer_free(&g->views[i].bytes); qa_buffer_free(&g->views[i].baseline);
    }
    for (size_t i = 0; g->pools && i < g->pool_count; ++i) {
        if (g->pools[i].owned) qa_resource_pool_destroy(g->pools[i].value);
        qa_buffer_free(&g->pools[i].bytes);
    }
    free(g->pools); free(g->catalogs); free(g->views); free(g->instances); free(g->resources); free(g);
}

bool qa_application_content_claim_pool(qa_application_content_graph *g, uint64_t id,
    qa_resource_pool **out, qa_error *error)
{
    if (!g || !g->pending || !out || *out || !id || id > g->pool_count || !g->pools[id - 1].owned)
        return fail(error, QA_ERROR_STATE, "Saved content pool has no unclaimed owning reference");
    *out = g->pools[id - 1].value; g->pools[id - 1].owned = false; return true;
}
bool qa_application_content_claim_view(qa_application_content_graph *g, uint64_t id,
    qa_vfs **out, qa_error *error)
{
    if (!g || !g->pending || !out || *out || !id || id > g->view_count ||
        g->views[id - 1].catalog || !g->views[id - 1].owned)
        return fail(error, QA_ERROR_STATE, "Saved private VFS has no unclaimed destructor ownership");
    *out = g->views[id - 1].value; g->views[id - 1].owned = false; return true;
}
bool qa_application_content_retain_catalog(qa_application_content_graph *g, uint64_t id,
    qa_catalog **out, qa_error *error)
{
    qa_catalog *catalog = qa_application_content_catalog(g, id);
    if (!g || !g->pending || !catalog || !out || *out)
        return fail(error, QA_ERROR_STATE, "Saved content catalog has no actual retained snapshot");
    qa_catalog_retain(catalog); *out = catalog; return true;
}
bool application_save_content_ready(const qa_application_content_graph *g, qa_error *error)
{
    if (!g || !g->pending) return fail(error, QA_ERROR_STATE, "Content graph has no isolated pending publication");
    for (size_t i = 0; i < g->pool_count; ++i) if (g->pools[i].owned)
        return fail(error, QA_ERROR_FORMAT, "Saved pool was not adopted by its actual consumer owner");
    for (size_t i = 0; i < g->view_count; ++i) if (g->views[i].owned)
        return fail(error, QA_ERROR_FORMAT, "Saved private VFS was not adopted by its actual destructor owner");
    return true;
}
void application_save_content_publish(qa_application_content_graph *g)
{
    if (!g) return;
    g->pending = false;
    for (size_t i = 0; i < g->view_count; ++i) g->views[i].qualified = false;
}
uint64_t application_save_content_application_pool(const qa_application_content_graph *g) { return g ? g->application_pool : 0; }
uint64_t application_save_content_application_catalog(const qa_application_content_graph *g) { return g ? g->application_catalog : 0; }
uint64_t application_save_content_launch_catalog(const qa_application_content_graph *g) { return g ? g->launch_catalog : 0; }
uint64_t application_save_content_launch_view(const qa_application_content_graph *g) { return g ? g->launch_view : 0; }
bool application_save_content_instance(const qa_application_content_graph *g, const char *name,
    application_saved_instance_content *out, qa_error *error)
{
    if (!g || !name || !out) return fail(error, QA_ERROR_ARGUMENT, "Saved provider lookup has no actual graph/name/output");
    for (size_t i = 0; i < g->instance_count; ++i)
        if (!strcmp(g->instances[i].value.source.selection.instance, name)) {
            *out = g->instances[i].value;
            out->source.artifact_acquisition = g->instances[i].artifact ?
                &g->instances[i].artifact_acquisition : NULL;
            return true;
        }
    return fail(error, QA_ERROR_FORMAT, "Provider is absent from saved content inventory");
}
size_t application_save_content_launch_resource_count(const qa_application_content_graph *g) { return g ? g->resource_count : 0; }
bool application_save_content_launch_resource_at(const qa_application_content_graph *g, size_t i,
    qa_launch_resource *out, qa_error *error)
{
    if (!g || !out || i >= g->resource_count) return fail(error, QA_ERROR_ARGUMENT, "Saved launch resource ordinal is outside physical inventory");
    *out = g->resources[i].value; return true;
}
bool application_save_content_launch_resource(const qa_application_content_graph *g, qa_product_id product,
    const char *path, const qa_resource **out, qa_error *error)
{
    if (!g || !path || !out || *out) return fail(error, QA_ERROR_ARGUMENT, "Saved launch resource lookup requires empty output");
    for (size_t i = 0; i < g->resource_count; ++i) if (g->resources[i].value.product == product && !strcmp(g->resources[i].value.path, path)) {
        *out = g->resources[i].value.resource; return true;
    }
    return fail(error, QA_ERROR_FORMAT, "Launch resource is absent from saved physical inventory");
}

static bool text_field(qa_source_save_io *io, const char **value)
{
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*value) : 0;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX - 1;
    if (!qa_source_save_count(io, &length, maximum) || length == SIZE_MAX) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        char *copy = malloc(length + 1);
        if (!copy) return fail(io->error, QA_ERROR_MEMORY, "Allocating saved graph text");
        *value = copy;
        if (!qa_source_save_bytes(io, copy, length)) return false;
        copy[length] = 0;
        if (memchr(copy, 0, length)) return fail(io->error, QA_ERROR_FORMAT, "Saved graph text contains embedded NUL");
        return true;
    }
    return qa_source_save_bytes(io, (void *)*value, length);
}
static bool blob_field(qa_source_save_io *io, qa_buffer *value)
{
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &value->size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && value->size) {
        value->data = malloc(value->size);
        if (!value->data) return fail(io->error, QA_ERROR_MEMORY, "Allocating saved graph owner bytes");
    }
    return qa_source_save_bytes(io, value->data, value->size);
}
static bool table_field(qa_source_save_io *io, void **values, size_t *count, size_t width, size_t minimum)
{
    size_t maximum = SIZE_MAX / width;
    if (io->direction == QA_SOURCE_SAVE_READ && maximum > (io->input.size - io->offset) / minimum)
        maximum = (io->input.size - io->offset) / minimum;
    if (!qa_source_save_count(io, count, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && *count) {
        *values = calloc(*count, width);
        if (!*values) return fail(io->error, QA_ERROR_MEMORY, "Allocating saved content inventory");
    }
    return true;
}
#define FIELD(type, object, field) do { if (!qa_source_save_##type(io, &(object)->field)) return false; } while (0)
static bool resource_fields(qa_source_save_io *io, content_resource *row)
{
    FIELD(u32, &row->value, product); FIELD(u64, row, pool); FIELD(u64, row, resource);
    return text_field(io, &row->value.path);
}

static bool acquisition_text(qa_source_save_io *io, char **value)
{
    bool present = *value != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    const char *text = *value;
    bool ok = text_field(io, &text);
    if (io->direction == QA_SOURCE_SAVE_READ) *value = (char *)text;
    return ok;
}

static bool acquisition_fields(qa_source_save_io *io, content_instance *row)
{
    qa_vfs_acquisition *receipt = &row->artifact_acquisition;
    bool present = row->artifact != 0;
    if (!qa_source_save_bool(io, &present) || present != (row->artifact != 0)) return false;
    if (!present) return true;
    if (!qa_source_save_u64(io, &receipt->mount) || !qa_source_save_u64(io, &receipt->resource_id) ||
        !acquisition_text(io, &receipt->path) || !acquisition_text(io, &receipt->lookup_path) ||
        !acquisition_text(io, &receipt->link_source) || !acquisition_text(io, &receipt->link_target) ||
        !receipt->mount || receipt->resource_id != row->artifact || !receipt->path || !*receipt->path ||
        !receipt->lookup_path || !*receipt->lookup_path ||
        ((receipt->link_source != NULL) != (receipt->link_target != NULL))) return false;
    row->value.source.artifact_acquisition = receipt;
    return true;
}
static bool instance_fields(qa_source_save_io *io, content_instance *row)
{
    qa_launch_restored_instance *v = &row->value.source; qa_launch_provider *p = &v->selection;
    FIELD(u64, row, catalog); FIELD(u64, row, product_catalog); FIELD(u32, row, product); FIELD(u64, &row->value, view);
    if (!text_field(io, &p->instance) || !text_field(io, &p->implementation) || !text_field(io, &p->artifact) || !text_field(io, &p->component)) return false;
    FIELD(u32, p, product);
    uint32_t runtime = p->runtime, clock = p->clock.kind;
    if (!qa_source_save_u32(io, &runtime) || runtime > QA_PROGRAM_NATIVE || !qa_source_save_u32(io, &clock) || clock > QA_CLOCK_Q3) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) { p->runtime = runtime; p->clock.kind = clock; }
    FIELD(u64, &p->clock, initial_time_ns); FIELD(u64, &p->clock, interval_ns);
    FIELD(u64, &p->clock, minimum_frame_ns); FIELD(u64, &p->clock, maximum_frame_ns);
    FIELD(u64, &p->clock, initial_lead_ns); FIELD(u32, &p->clock, maximum_steps);
    if (!blob_field(io, &row->options)) return false;
    p->options = (qa_bytes){row->options.data, row->options.size};
    FIELD(u64, row, artifact_pool); FIELD(u64, row, artifact); FIELD(u64, row, declaration_pool); FIELD(u64, row, declaration);
    if (!acquisition_fields(io, row)) return false;
    if (!qa_source_save_bytes(io, &v->identity, sizeof(v->identity)) ||
        !table_field(io, (void **)&row->interfaces, &v->interface_count, sizeof(*row->interfaces), 28)) return false;
    for (size_t i = 0; i < v->interface_count; ++i) if (!resource_fields(io, &row->interfaces[i])) return false;
    if (!table_field(io, (void **)&row->behaviors, &v->behavior_count, sizeof(*row->behaviors), 8)) return false;
    for (size_t i = 0; i < v->behavior_count; ++i) if (!qa_source_save_u64(io, &row->behaviors[i])) return false;
    return true;
}
static bool graph_fields(qa_source_save_io *io, qa_application_content_graph *g)
{
    uint8_t magic[8] = {'Q','A','C','G',0,0,0,0}; uint32_t version = 2;
    const uint8_t expected[8] = {'Q','A','C','G',0,0,0,0};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, expected, sizeof(magic)) ||
        !qa_source_save_u32(io, &version) || version != 2) return false;
    FIELD(u64, g, application_pool); FIELD(u64, g, application_catalog); FIELD(u64, g, launch_catalog); FIELD(u64, g, launch_view);
    if (!table_field(io, (void **)&g->pools, &g->pool_count, sizeof(*g->pools), 8)) return false;
    for (size_t i = 0; i < g->pool_count; ++i) if (!blob_field(io, &g->pools[i].bytes)) return false;
    if (!table_field(io, (void **)&g->catalogs, &g->catalog_count, sizeof(*g->catalogs), 16)) return false;
    for (size_t i = 0; i < g->catalog_count; ++i) {
        FIELD(u64, &g->catalogs[i], pool); if (!blob_field(io, &g->catalogs[i].bytes)) return false;
    }
    if (!table_field(io, (void **)&g->views, &g->view_count, sizeof(*g->views), 24)) return false;
    for (size_t i = 0; i < g->view_count; ++i) {
        FIELD(u64, &g->views[i], pool); FIELD(u64, &g->views[i], catalog); if (!blob_field(io, &g->views[i].bytes)) return false;
    }
    if (!table_field(io, (void **)&g->instances, &g->instance_count, sizeof(*g->instances), 188)) return false;
    for (size_t i = 0; i < g->instance_count; ++i) if (!instance_fields(io, &g->instances[i])) return false;
    if (!table_field(io, (void **)&g->resources, &g->resource_count, sizeof(*g->resources), 28)) return false;
    for (size_t i = 0; i < g->resource_count; ++i) if (!resource_fields(io, &g->resources[i])) return false;
    return true;
}
#undef FIELD

static bool structure(const qa_application_content_graph *g, qa_error *error)
{
    if (!g->application_pool || g->application_pool > g->pool_count ||
        !g->application_catalog || g->application_catalog > g->catalog_count ||
        !g->launch_catalog || g->launch_catalog > g->catalog_count ||
        !g->launch_view || g->launch_view > g->view_count ||
        g->catalogs[g->application_catalog - 1].pool != g->application_pool ||
        g->views[g->launch_view - 1].pool != g->catalogs[g->launch_catalog - 1].pool ||
        g->views[g->launch_view - 1].catalog)
        return fail(error, QA_ERROR_FORMAT, "Saved content roots disagree with actual owning graph");
    for (size_t i = 0; i < g->view_count; ++i) {
        const content_view *v = &g->views[i];
        if (!v->pool || v->pool > g->pool_count || v->catalog > g->catalog_count ||
            (v->catalog && g->catalogs[v->catalog - 1].pool != v->pool))
            return fail(error, QA_ERROR_FORMAT, "Saved VFS has an invalid actual pool/catalog edge");
    }
    for (size_t i = 0; i < g->catalog_count; ++i) {
        size_t owners = 0;
        for (size_t j = 0; j < g->view_count; ++j) if (g->views[j].catalog == i + 1) ++owners;
        if (!g->catalogs[i].pool || g->catalogs[i].pool > g->pool_count || owners != 1)
            return fail(error, QA_ERROR_FORMAT, "Saved catalog must own exactly its one private VFS");
    }
    for (size_t i = 0; i < g->instance_count; ++i) {
        const content_instance *r = &g->instances[i]; const qa_launch_restored_instance *v = &r->value.source;
        const qa_launch_provider *p = &v->selection;
        if (!r->catalog || r->catalog > g->catalog_count || !r->product_catalog || r->product_catalog > g->catalog_count ||
            !r->product || !p->product || !p->instance || !*p->instance || !p->implementation || !*p->implementation ||
            !p->artifact || !p->component || !r->value.view || r->value.view > g->view_count ||
            r->value.view == g->launch_view || g->views[r->value.view - 1].catalog ||
            g->views[r->value.view - 1].pool != g->catalogs[r->catalog - 1].pool ||
            ((!r->artifact) != (!r->artifact_pool)) || ((!r->declaration) != (!r->declaration_pool)) ||
            (r->artifact && r->artifact_pool != g->views[r->value.view - 1].pool) ||
            (r->declaration && r->declaration_pool != g->views[r->value.view - 1].pool) ||
            p->runtime > QA_PROGRAM_NATIVE || p->clock.kind > QA_CLOCK_Q3)
            return fail(error, QA_ERROR_FORMAT, "Saved provider selection has invalid actual content edges");
        for (size_t j = 0; j < i; ++j) if (r->value.view == g->instances[j].value.view ||
            !strcmp(p->instance, g->instances[j].value.source.selection.instance))
            return fail(error, QA_ERROR_FORMAT, "Saved provider name or private VFS ownership is duplicated");
        for (size_t j = 0; j < v->interface_count; ++j) {
            const content_resource *f = &r->interfaces[j];
            if (!f->value.path || !*f->value.path || f->value.product != p->product ||
                !f->resource || f->pool != g->views[r->value.view - 1].pool)
                return fail(error, QA_ERROR_FORMAT, "Saved interface has invalid provider resource ownership");
            for (size_t k = 0; k < j; ++k) if (!strcmp(f->value.path, r->interfaces[k].value.path))
                return fail(error, QA_ERROR_FORMAT, "Saved provider interface path is duplicated");
        }
        for (size_t j = 0; j < v->behavior_count; ++j) {
            if (!r->behaviors[j]) return fail(error, QA_ERROR_FORMAT, "Saved behavior lacks catalog authority");
            for (size_t k = 0; k < j; ++k) if (r->behaviors[k] == r->behaviors[j])
                return fail(error, QA_ERROR_FORMAT, "Saved provider behavior is duplicated");
        }
    }
    for (size_t i = 0; i < g->resource_count; ++i) {
        const content_resource *r = &g->resources[i];
        if (!r->value.product || !r->value.path || !*r->value.path || !r->resource ||
            r->pool != g->views[g->launch_view - 1].pool)
            return fail(error, QA_ERROR_FORMAT, "Saved launch resource has invalid actual root ownership");
        for (size_t j = 0; j < i; ++j) if (r->value.product == g->resources[j].value.product &&
            !strcmp(r->value.path, g->resources[j].value.path))
            return fail(error, QA_ERROR_FORMAT, "Saved launch resource identity is duplicated");
    }
    return true;
}
static bool resource_resolve(qa_application_content_graph *g, content_resource *r, qa_error *error)
{
    const qa_resource *actual = qa_application_content_resource(g, r->pool, r->resource);
    if (!actual || (r->value.resource && r->value.resource != actual))
        return fail(error, QA_ERROR_FORMAT, "Saved resource ID does not resolve to its actual immutable pool owner");
    r->value.resource = actual;
    if (!r->retained) { qa_resource_retain((qa_resource *)actual); r->retained = true; }
    return true;
}
static bool resolve(qa_application_content_graph *g, qa_error *error)
{
    if (!structure(g, error)) return false;
    for (size_t i = 0; i < g->instance_count; ++i) {
        content_instance *r = &g->instances[i]; qa_launch_restored_instance *v = &r->value.source;
        v->catalog = qa_application_content_catalog(g, r->catalog);
        v->content = qa_application_content_view(g, r->value.view);
        r->value.product_catalog = qa_application_content_catalog(g, r->product_catalog);
        r->value.product = qa_catalog_product(r->value.product_catalog, r->product);
        const qa_product *source_product = qa_catalog_product(v->catalog, v->selection.product);
        if (!source_product || !r->value.product || strcmp(source_product->identity, r->value.product->identity))
            return fail(error, QA_ERROR_FORMAT, "Saved provider product disagrees across retained catalogs");
        v->artifact = qa_application_content_resource(g, r->artifact_pool, r->artifact);
        v->artifact_acquisition = r->artifact ? &r->artifact_acquisition : NULL;
        v->declaration = qa_application_content_resource(g, r->declaration_pool, r->declaration);
        if ((r->artifact && !v->artifact) || (r->declaration && !v->declaration))
            return fail(error, QA_ERROR_FORMAT, "Saved provider artifact/declaration lacks immutable resource authority");
        if (v->artifact && (!v->artifact_acquisition ||
            v->artifact_acquisition->resource_id != qa_resource_id(v->artifact) ||
            !qa_vfs_acquisition_valid(v->content, v->artifact_acquisition, error)))
            return fail(error, QA_ERROR_FORMAT, "Saved provider artifact receipt leaves its actual restored view");
        if (v->artifact && !r->artifact_retained) { qa_resource_retain((qa_resource *)v->artifact); r->artifact_retained = true; }
        if (v->declaration && !r->declaration_retained) { qa_resource_retain((qa_resource *)v->declaration); r->declaration_retained = true; }
        if (!r->interface_values && v->interface_count) r->interface_values = calloc(v->interface_count, sizeof(*r->interface_values));
        if (!r->behavior_values && v->behavior_count) r->behavior_values = calloc(v->behavior_count, sizeof(*r->behavior_values));
        if ((v->interface_count && !r->interface_values) || (v->behavior_count && !r->behavior_values))
            return fail(error, QA_ERROR_MEMORY, "Resolving saved provider descriptor arrays");
        for (size_t j = 0; j < v->interface_count; ++j) {
            if (!resource_resolve(g, &r->interfaces[j], error)) return false;
            r->interface_values[j] = r->interfaces[j].value;
        }
        for (size_t j = 0; j < v->behavior_count; ++j) {
            if (r->behaviors[j] > qa_catalog_weapon_behavior_count(v->catalog))
                return fail(error, QA_ERROR_FORMAT, "Saved behavior is outside its retained catalog");
            r->behavior_values[j] = qa_catalog_weapon_behavior_at(v->catalog, r->behaviors[j] - 1);
        }
        v->interfaces = r->interface_values; v->behaviors = r->behavior_values;
    }
    qa_catalog *launch = qa_application_content_catalog(g, g->launch_catalog);
    for (size_t i = 0; i < g->resource_count; ++i)
        if (!qa_catalog_product(launch, g->resources[i].value.product) || !resource_resolve(g, &g->resources[i], error)) return false;
    return true;
}

static bool equal_bytes(const qa_buffer *a, const qa_buffer *b)
{ return a->size == b->size && (!a->size || !memcmp(a->data, b->data, a->size)); }
static bool copy_bytes(const qa_buffer *source, qa_buffer *out, qa_error *error)
{
    out->data = source->size ? malloc(source->size) : NULL;
    if (source->size && !out->data) return fail(error, QA_ERROR_MEMORY, "Retaining qualified graph owner bytes");
    out->size = source->size; if (source->size) memcpy(out->data, source->data, source->size); return true;
}
static bool files_encode(void *context, const qa_vfs *view, qa_buffer *out, qa_error *error)
{
    const qa_application_content_graph *g = context; uint64_t id = qa_application_content_view_id(g, view);
    if (!id || !out || out->data || out->size) return fail(error, QA_ERROR_FORMAT, "Catalog files are outside actual content graph");
    return copy_bytes(&g->views[id - 1].bytes, out, error);
}
static bool refresh(qa_application_content_graph *g, qa_error *error)
{
    if (!resolve(g, error)) return false;
    for (size_t i = 0; i < g->pool_count; ++i) {
        qa_buffer bytes = {0};
        if (!qa_resource_pool_checkpoint(g->pools[i].value, &bytes, error)) return false;
        qa_buffer_free(&g->pools[i].bytes); g->pools[i].bytes = bytes;
    }
    for (size_t i = 0; i < g->view_count; ++i) {
        content_view *v = &g->views[i]; qa_buffer bytes = {0};
        if (!qa_vfs_checkpoint(v->value, &bytes, error)) return false;
        if (v->qualified) {
            bool unchanged = equal_bytes(&bytes, &v->baseline); qa_buffer_free(&bytes);
            if (!unchanged) return fail(error, QA_ERROR_FORMAT, "Qualified saved VFS changed after native content admission");
        } else { qa_buffer_free(&v->bytes); v->bytes = bytes; }
    }
    qa_catalog_checkpoint_refs refs = {.context = g, .files_encode = files_encode};
    for (size_t i = 0; i < g->catalog_count; ++i) {
        qa_buffer bytes = {0};
        if (!qa_catalog_checkpoint(g->catalogs[i].value, &refs, &bytes, error)) return false;
        qa_buffer_free(&g->catalogs[i].bytes); g->catalogs[i].bytes = bytes;
    }
    return true;
}

bool application_save_content_encode(const qa_application_content_graph *graph, qa_buffer *out, qa_error *error)
{
    if (!graph || !out || out->data || out->size) return fail(error, QA_ERROR_ARGUMENT, "Content graph encoding requires an actual graph and empty output");
    qa_application_content_graph *g = (qa_application_content_graph *)graph;
    qa_source_save_io io = {0};
    bool ok = refresh(g, error) && qa_source_save_writer(&io, NULL, error) && graph_fields(&io, g) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Unqualified actual content graph");
    return ok;
}

typedef struct catalog_admission { qa_application_content_graph *graph; content_view *view; } catalog_admission;
static bool physical_ready(void *context, const qa_catalog_mount *mount,
    const qa_catalog_member_identity *members, size_t count, qa_error *error)
{
    catalog_admission *a = context; qa_vfs *v = a->view->value; qa_vfs_mount_info actual;
    bool found = false;
    for (size_t i = 0; mount && i < qa_vfs_mount_count(v); ++i)
        if (qa_vfs_mount_at(v, i, &actual) && actual.id == mount->id) { found = true; break; }
    if (!mount || !mount->id || !found)
        return fail(error, QA_ERROR_FORMAT, "Catalog physical mount is outside its qualified native view");
    const char *path = qa_vfs_mount_path(v, actual.id);
    if (!path || strcmp(path, mount->path) || actual.format != mount->format || actual.writable != mount->writable ||
        actual.is_archive != (mount->format != QA_ARCHIVE_AUTO) ||
        (actual.is_archive && (!mount->digest || !actual.digest || !qa_sha256_equal(mount->digest, actual.digest))))
        return fail(error, QA_ERROR_FORMAT, "Catalog physical package identity disagrees with its qualified native view");
    if (!actual.is_archive) return (!count && !mount->digest) ||
        fail(error, QA_ERROR_FORMAT, "Catalog directory contains fabricated archive metadata");
    const qa_archive *archive = qa_vfs_archive(v, actual.id); size_t next = 0;
    if (!archive) return fail(error, QA_ERROR_FORMAT, "Catalog package has no actual immutable archive owner");
    for (size_t i = 0; i < qa_archive_count(archive); ++i) {
        const qa_archive_entry *entry = qa_archive_entry_at(archive, i);
        if (entry->is_directory) continue;
        if (next >= count || members[next].ordinal != i || strcmp(members[next].path, entry->path))
            return fail(error, QA_ERROR_FORMAT, "Catalog member inventory disagrees with actual immutable archive bytes");
        ++next;
    }
    return next == count || fail(error, QA_ERROR_FORMAT, "Catalog member inventory omits actual package entries");
}
static bool files_decode(void *context, qa_resource_pool *pool, qa_bytes bytes, qa_vfs **out, qa_error *error)
{
    catalog_admission *a = context; content_view *v = a->view;
    if (!out || *out || !v->owned || !v->value || qa_vfs_resources(v->value) != pool ||
        bytes.size != v->bytes.size || (bytes.size && memcmp(bytes.data, v->bytes.data, bytes.size)))
        return fail(error, QA_ERROR_FORMAT, "Catalog nested files disagree with the one genuine graph view owner");
    *out = v->value; v->value = NULL; v->owned = false; return true;
}

bool application_save_content_prepare(qa_bytes bytes, const qa_vfs_checkpoint_refs *files,
    qa_application_content_graph **out, qa_error *error)
{
    if (!out || *out || !bytes.data || !bytes.size) return fail(error, QA_ERROR_ARGUMENT, "Content preparation requires complete bytes and empty output");
    qa_application_content_graph *g = calloc(1, sizeof(*g));
    if (!g) return fail(error, QA_ERROR_MEMORY, "Allocating isolated content graph");
    g->restored = true; g->pending = true;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && graph_fields(&io, g) &&
        qa_source_save_finish(&io, NULL) && structure(g, error);
    qa_source_save_dispose(&io);
    /* All enclosing extents and ownership edges are validated before any
     * real native file/root is admitted or any resource holder is created. */
    for (size_t i = 0; ok && i < g->pool_count; ++i) {
        content_pool *p = &g->pools[i]; p->value = qa_resource_pool_create(error); p->owned = p->value != NULL;
        ok = p->value && qa_resource_pool_restore(p->value, (qa_bytes){p->bytes.data, p->bytes.size}, error);
    }
    for (size_t i = 0; ok && i < g->view_count; ++i) {
        content_view *v = &g->views[i];
        ok = qa_vfs_create_restored(qa_application_content_pool(g, v->pool), files,
            (qa_bytes){v->bytes.data, v->bytes.size}, &v->value, error);
        v->owned = v->value != NULL;
        if (ok) ok = qa_vfs_checkpoint(v->value, &v->baseline, error);
        if (ok) v->qualified = true;
    }
    for (size_t i = 0; ok && i < g->catalog_count; ++i) {
        content_view *view = NULL;
        for (size_t j = 0; j < g->view_count; ++j) if (g->views[j].catalog == i + 1) { view = &g->views[j]; break; }
        catalog_admission admission = {.graph = g, .view = view};
        qa_catalog_checkpoint_refs refs = {.context = &admission, .files_decode = files_decode, .physical_ready = physical_ready};
        content_catalog *c = &g->catalogs[i];
        ok = qa_catalog_restore(qa_application_content_pool(g, c->pool), &refs,
            (qa_bytes){c->bytes.data, c->bytes.size}, &c->value, error);
        if (ok) {
            view->value = (qa_vfs *)qa_catalog_files(c->value);
            if (qa_vfs_mount_count(view->value) != qa_catalog_mount_count(c->value))
                ok = fail(error, QA_ERROR_FORMAT, "Catalog native view has extra unqualified physical mounts");
        }
    }
    if (ok) ok = resolve(g, error);
    if (!ok) {
        application_save_content_destroy(g);
        if (error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Unqualified saved content graph");
        return false;
    }
    *out = g; return true;
}
