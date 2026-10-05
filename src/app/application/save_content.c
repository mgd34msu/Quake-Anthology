#include "save_content.h"
#include "bots_save_private.h"
#include "guest_q3_components.h"
#include "internal.h"
#include "qa/catalog_save.h"
#include "qa/source_save.h"
#include "qa/vfs_save.h"
#include "qa/application_q3_factory.h"
#include "equipment_runtime.h"
#include "events_save.h"
#include "qa/map_sidecars.h"

#include <stdlib.h>
#include <string.h>

typedef struct content_pool { qa_resource_pool *value; bool owned; qa_buffer bytes; } content_pool;
typedef struct content_catalog { qa_catalog *value; uint64_t pool; qa_buffer bytes; } content_catalog;
typedef struct content_view { qa_vfs *value; uint64_t pool, catalog; bool owned; qa_buffer bytes; } content_view;
typedef struct content_resource {
    qa_launch_resource value;
    uint64_t pool, resource, origin_view;
    qa_product_id origin_product;
    qa_mount_id catalog_mount;
    qa_launch_resource_origin_kind origin_kind;
    qa_launch_source_files source;
    uint64_t source_base, source_authority;
    qa_vfs_acquisition acquisition;
    bool retained;
} content_resource;
typedef struct content_instance {
    application_saved_instance_content value;
    uint64_t catalog, product_catalog, artifact_pool, artifact, declaration_pool, declaration;
    qa_product_id product;
    content_resource *interfaces;
    qa_launch_resource *interface_values;
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
            !qa_vfs_acquisition_retained(source->content, receipt, error))
            return fail(error, QA_ERROR_FORMAT, "Provider artifact lost its actual opening receipt");
        if (!qa_vfs_acquisition_copy(receipt, &row->artifact_acquisition, error)) return false;
        value->artifact_acquisition = &row->artifact_acquisition;
    }
    if (value->artifact) { qa_resource_retain((qa_resource *)value->artifact); row->artifact_retained = true; }
    if (value->declaration) { qa_resource_retain((qa_resource *)value->declaration); row->declaration_retained = true; }
    if (source->interface_count > SIZE_MAX / sizeof(*row->interfaces))
        return fail(error, QA_ERROR_MEMORY, "Provider interface extent is exhausted");
    row->interfaces = source->interface_count ? calloc(source->interface_count, sizeof(*row->interfaces)) : NULL;
    if (source->interface_count && !row->interfaces)
        return fail(error, QA_ERROR_MEMORY, "Retaining actual provider interface order");
    value->interface_count = source->interface_count;
    for (size_t i = 0; i < source->interface_count; ++i)
        if (!copy_resource(g, pool, &source->interfaces[i], &row->interfaces[i], error)) return false;
    row->value.product_catalog = provider->product_catalog; row->value.product = provider->product;
    row->interface_values = value->interface_count ? calloc(value->interface_count, sizeof(*row->interface_values)) : NULL;
    if (value->interface_count && !row->interface_values)
        return fail(error, QA_ERROR_MEMORY, "Retaining provider interface descriptors");
    for (size_t i = 0; i < value->interface_count; ++i) row->interface_values[i] = row->interfaces[i].value;
    value->interfaces = row->interface_values;
    return true;
}

bool application_save_content_collect(qa_application *app, qa_application_content_visit_fn visit,
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
    for (size_t i = 0; ok && i < g->resource_count; ++i) {
        qa_launch_resource_origin origin = {0};
        content_resource *row = &g->resources[i];
        ok = qa_launch_snapshot_resource_origin(launch, i, &origin) &&
            origin.catalog == qa_launch_snapshot_catalog(launch) &&
            add_view(g, origin.content, error) &&
            copy_resource(g, qa_vfs_resources(origin.content), qa_launch_snapshot_resource(launch, i), row, error) &&
            origin.acquisition && origin.acquisition->opening_present &&
            qa_vfs_acquisition_retained(origin.content, origin.acquisition, error) &&
            qa_vfs_acquisition_copy(origin.acquisition, &row->acquisition, error);
        if (ok) {
            row->origin_view = qa_application_content_view_id(g, origin.content);
            row->origin_product = origin.product; row->catalog_mount = origin.catalog_mount;
            row->origin_kind = origin.kind;
            if (origin.kind == QA_LAUNCH_ORIGIN_SOURCE_QW) {
                ok = origin.source.catalog == origin.catalog && origin.source.content == origin.content &&
                    origin.source.product == row->value.product && origin.product == row->value.product &&
                    !origin.catalog_mount && qa_launch_source_files_current(&origin.source, error) &&
                    add_view(g, origin.source.base, error) && add_view(g, origin.source.authority, error);
                if (ok) {
                    row->source = origin.source;
                    row->source.directory = row->source.home_prefix = NULL;
                    row->source_base = qa_application_content_view_id(g, origin.source.base);
                    row->source_authority = qa_application_content_view_id(g, origin.source.authority);
                    row->source.directory = copy_text(origin.source.directory, error);
                    row->source.home_prefix = copy_text(origin.source.home_prefix, error);
                    ok = row->source.directory && row->source.home_prefix;
                }
            } else if (origin.kind != QA_LAUNCH_ORIGIN_CATALOG) {
                ok = fail(error, QA_ERROR_FORMAT, "Launch opening has an unknown physical origin domain");
            }
        }
    }
    const qa_application_content_visitor visitor = {.context = g, .pool = add_pool, .catalog = add_catalog, .view = add_view};
    if (ok) ok = qa_map_sidecars_content_visit(app->map_sidecars, &visitor, error);
    if (ok) ok = qa_application_q3_content_visit(app, &visitor, error);
    if (ok) ok = application_q3_components_content_visit(app->components,&visitor,error);
    if (ok) ok = application_bots_content_visit(app,&visitor,error);
    if (ok) ok = application_events_save_content_visit(app, &visitor, error);
    for (size_t i = 0; ok && i < application_equipment_runtime_source_count(app->equipment_runtime); ++i) {
        application_equipment_runtime_source source;
        ok = application_equipment_runtime_source_at(app->equipment_runtime, i, &source, error);
        if (!ok || !source.gear) continue;
        qa_resource_pool *pool = qa_vfs_resources(source.content);
        ok = source.descriptor && source.artifact && source.acquisition &&
            source.acquisition->resource_id == qa_resource_id(source.artifact) &&
            qa_resource_pool_find(pool, qa_resource_id(source.artifact)) == source.artifact &&
            qa_vfs_acquisition_retained(source.content, source.acquisition, error) &&
            add_view(g, source.content, error);
        if (!ok && (!error || error->code == QA_OK))
            fail(error, QA_ERROR_FORMAT, "Gear artifact leaves its genuine retained content owner");
    }
    if (ok && visit) {
        qa_application_content_graph *previous = app->capture_content_graph;
        app->capture_content_graph = g;
        ok = visit(context, app, &visitor, error);
        app->capture_content_graph = previous;
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
        free(row->interfaces); free(row->interface_values);
        qa_buffer_free(&row->options);
    }
    for (size_t i = 0; g->resources && i < g->resource_count; ++i) {
        free((void *)g->resources[i].value.path);
        free((void *)g->resources[i].source.directory);
        free((void *)g->resources[i].source.home_prefix);
        qa_vfs_acquisition_dispose(&g->resources[i].acquisition);
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
        qa_buffer_free(&g->views[i].bytes);
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
        return fail(error, QA_ERROR_ARGUMENT, "Saved content pool has no unclaimed owning reference");
    *out = g->pools[id - 1].value; g->pools[id - 1].owned = false; return true;
}
bool qa_application_content_claim_view(qa_application_content_graph *g, uint64_t id,
    qa_vfs **out, qa_error *error)
{
    if (!g || !g->pending || !out || *out || !id || id > g->view_count ||
        g->views[id - 1].catalog || !g->views[id - 1].owned)
        return fail(error, QA_ERROR_ARGUMENT, "Saved private VFS has no unclaimed destructor ownership");
    *out = g->views[id - 1].value; g->views[id - 1].owned = false; return true;
}
bool qa_application_content_retain_catalog(qa_application_content_graph *g, uint64_t id,
    qa_catalog **out, qa_error *error)
{
    qa_catalog *catalog = qa_application_content_catalog(g, id);
    if (!g || !g->pending || !catalog || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Saved content catalog has no actual retained snapshot");
    qa_catalog_retain(catalog); *out = catalog; return true;
}
bool qa_application_content_retain_view(qa_application_content_graph *g, uint64_t id,
    qa_vfs **out, qa_error *error)
{
    qa_vfs *view = qa_application_content_view(g, id);
    if (!g || !g->pending || !view || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Saved VFS has no actual retained snapshot");
    if (g->views[id - 1].owned) g->views[id - 1].owned = false;
    else if (!qa_vfs_retain(view, error)) return false;
    *out = view;
    return true;
}
bool application_save_content_ready(const qa_application_content_graph *g, qa_error *error)
{
    if (!g || !g->pending) return fail(error, QA_ERROR_ARGUMENT, "Content graph has no isolated pending publication");
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
}
uint64_t application_save_content_application_pool(const qa_application_content_graph *g) { return g ? g->application_pool : 0; }
uint64_t application_save_content_application_catalog(const qa_application_content_graph *g) { return g ? g->application_catalog : 0; }
uint64_t application_save_content_launch_catalog(const qa_application_content_graph *g) { return g ? g->launch_catalog : 0; }
uint64_t application_save_content_launch_view(const qa_application_content_graph *g) { return g ? g->launch_view : 0; }
bool qa_application_content_retain_pool(qa_application_content_graph *g, uint64_t id,
    qa_resource_pool **out, qa_error *error)
{
    qa_resource_pool *pool = qa_application_content_pool(g, id);
    if (!g || !g->pending || !pool || !out || *out)
        return fail(error, QA_ERROR_FORMAT, "Saved resource pool has no actual retained owner");
    if (g->pools[id - 1].owned) g->pools[id - 1].owned = false;
    else qa_resource_pool_retain(pool);
    *out = pool;
    return true;
}

bool application_save_content_event_pool(qa_application_content_graph *g, uint64_t id,
    qa_resource_pool **out, qa_error *error)
{ return qa_application_content_retain_pool(g, id, out, error); }
bool application_save_content_event_view(qa_application_content_graph *g, uint64_t id,
    qa_vfs **out, qa_error *error)
{ return qa_application_content_retain_view(g, id, out, error); }
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
bool application_save_content_launch_resource_origin(const qa_application_content_graph *g, size_t i,
    qa_launch_resource_origin *out, qa_error *error)
{
    if (!g || !out || i >= g->resource_count)
        return fail(error, QA_ERROR_ARGUMENT, "Saved launch resource origin is outside physical inventory");
    const content_resource *row = &g->resources[i];
    *out = (qa_launch_resource_origin){.catalog = qa_application_content_catalog(g, g->launch_catalog),
        .content = qa_application_content_view(g, row->origin_view), .product = row->origin_product,
        .catalog_mount = row->catalog_mount, .acquisition = &row->acquisition,
        .kind = row->origin_kind, .source = row->source};
    return true;
}
bool application_save_content_launch_source_claim(qa_application_content_graph *g, size_t i,
    qa_launch_resource_origin *out, qa_error *error)
{
    if (!g || i >= g->resource_count || !out || !g->pending)
        return fail(error, QA_ERROR_ARGUMENT, "Source opening claim requires its actual pending graph");
    const content_resource *row = g->resources + i;
    if (row->origin_kind == QA_LAUNCH_ORIGIN_CATALOG) return out->kind == row->origin_kind;
    if (out->kind != row->origin_kind || out->source.base != row->source.base ||
        out->source.authority != row->source.authority || out->content != row->source.content)
        return fail(error, QA_ERROR_ARGUMENT, "Source opening claim selected another physical owner");
    out->content = out->source.content = NULL;
    out->source.base = out->source.authority = NULL;
    if (!qa_application_content_retain_view(g, row->origin_view, &out->content, error)) return false;
    out->source.content = out->content;
    return qa_application_content_retain_view(g, row->source_base, &out->source.base, error) &&
        qa_application_content_retain_view(g, row->source_authority, &out->source.authority, error);
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

static bool opening_fields(qa_source_save_io *io, qa_vfs_acquisition *receipt)
{
    if (!qa_source_save_bool(io, &receipt->opening_present)) return false;
    if (!receipt->opening_present) return !receipt->opening.order && !receipt->opening.order_count &&
        !receipt->opening.prefix && !receipt->opening.rank && !receipt->opening.user_overlay;
    qa_vfs_read_opening *opening = &receipt->opening;
    char *prefix = (char *)opening->prefix;
    if (!qa_source_save_i64(io, &opening->rank) || opening->rank < -1 ||
        !qa_source_save_bool(io, &opening->user_overlay)) return false;
    bool ok = acquisition_text(io, &prefix);
    if (io->direction == QA_SOURCE_SAVE_READ) opening->prefix = prefix;
    if (!ok) return false;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 8 :
        SIZE_MAX / sizeof(qa_mount_id);
    if (!qa_source_save_count(io, &opening->order_count, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && opening->order_count) {
        qa_mount_id *order = calloc(opening->order_count, sizeof(*order));
        if (!order) return fail(io->error, QA_ERROR_MEMORY, "Restoring actual resource opening order");
        opening->order = order;
    }
    for (size_t i = 0; i < opening->order_count; ++i) {
        qa_mount_id id = io->direction == QA_SOURCE_SAVE_WRITE ? opening->order[i] : 0;
        if (!qa_source_save_u64(io, &id) || !id) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) ((qa_mount_id *)opening->order)[i] = id;
        for (size_t j = 0; j < i; ++j) if (opening->order[j] == id) return false;
    }
    return true;
}

static bool receipt_fields(qa_source_save_io *io, qa_vfs_acquisition *receipt, uint64_t resource)
{
    return qa_source_save_u64(io, &receipt->mount) && qa_source_save_u64(io, &receipt->resource_id) &&
        acquisition_text(io, &receipt->path) && acquisition_text(io, &receipt->lookup_path) &&
        acquisition_text(io, &receipt->link_source) && acquisition_text(io, &receipt->link_target) &&
        opening_fields(io, receipt) && receipt->mount && receipt->resource_id == resource &&
        receipt->path && *receipt->path && receipt->lookup_path && *receipt->lookup_path &&
        ((receipt->link_source != NULL) == (receipt->link_target != NULL));
}

static bool acquisition_fields(qa_source_save_io *io, content_instance *row)
{
    qa_vfs_acquisition *receipt = &row->artifact_acquisition;
    bool present = row->artifact != 0;
    if (!qa_source_save_bool(io, &present) || present != (row->artifact != 0)) return false;
    if (!present) return true;
    if (!receipt_fields(io, receipt, row->artifact)) return false;
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
    /* Selected trajectory identities live in the canonical launch draft.
     * The normal provider constructor derives this list after restoration. */
    size_t count = 0;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 8 : 0;
    if (!qa_source_save_count(io, &count, maximum)) return false;
    for (size_t i = 0; i < count; ++i) {
        uint64_t ignored = 0;
        if (!qa_source_save_u64(io, &ignored)) return false;
    }
    return true;
}
static bool graph_fields(qa_source_save_io *io, qa_application_content_graph *g)
{
    uint8_t magic[8] = {'Q','A','C','G',0,0,0,0};
    const uint8_t expected[8] = {'Q','A','C','G',0,0,0,0};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, expected, sizeof(magic))) return false;
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
    for (size_t i = 0; i < g->resource_count; ++i) {
        content_resource *row = &g->resources[i];
        if (!resource_fields(io, row)) return false;
        FIELD(u64, row, origin_view); FIELD(u32, row, origin_product); FIELD(u64, row, catalog_mount);
        {
            uint32_t kind = row->origin_kind;
            if (!qa_source_save_u32(io, &kind) || kind > QA_LAUNCH_ORIGIN_SOURCE_QW) return false;
            row->origin_kind = (qa_launch_resource_origin_kind)kind;
            if (kind == QA_LAUNCH_ORIGIN_SOURCE_QW) {
                FIELD(u64, row, source_base); FIELD(u64, row, source_authority);
                FIELD(u32, &row->source, base_product); FIELD(u64, &row->source, family);
                FIELD(u64, &row->source, home); FIELD(bool, &row->source, changed);
                if (!text_field(io, &row->source.directory) || !text_field(io, &row->source.home_prefix)) return false;
            }
        }
        if (!receipt_fields(io, &row->acquisition, row->resource) || !row->acquisition.opening_present) return false;
    }
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
    }
    for (size_t i = 0; i < g->resource_count; ++i) {
        const content_resource *r = &g->resources[i];
        if (!r->value.product || !r->value.path || !*r->value.path || !r->resource ||
            r->pool != g->views[g->launch_view - 1].pool || !r->origin_view || r->origin_view > g->view_count ||
            r->origin_view == g->launch_view ||
            g->views[r->origin_view - 1].catalog || g->views[r->origin_view - 1].pool != r->pool ||
            !r->origin_product || r->origin_kind > QA_LAUNCH_ORIGIN_SOURCE_QW ||
            r->acquisition.resource_id != r->resource ||
            !r->acquisition.opening_present)
            return fail(error, QA_ERROR_FORMAT, "Saved launch resource has invalid actual root ownership");
        if (r->origin_kind == QA_LAUNCH_ORIGIN_CATALOG) {
            if (!r->catalog_mount || r->source_base || r->source_authority ||
                r->source.directory || r->source.home_prefix)
                return fail(error, QA_ERROR_FORMAT, "Catalog opening carries a foreign Source filesystem domain");
        } else {
            if (r->catalog_mount || r->origin_product != r->value.product ||
                !r->source_base || r->source_base > g->view_count ||
                !r->source_authority || r->source_authority > g->view_count ||
                r->source_base == r->source_authority || r->source_base == r->origin_view ||
                r->source_authority == r->origin_view || r->source_base == g->launch_view ||
                r->source_authority == g->launch_view ||
                g->views[r->source_base - 1].catalog || g->views[r->source_authority - 1].catalog ||
                g->views[r->source_base - 1].pool != r->pool ||
                g->views[r->source_authority - 1].pool != r->pool || !r->source.base_product ||
                !r->source.family || !r->source.home || r->source.family == r->source.home ||
                !r->source.directory || !*r->source.directory || !r->source.home_prefix)
                return fail(error, QA_ERROR_FORMAT, "Source filesystem opening has invalid retained authority roots");
            for (size_t j = 0; j < g->instance_count; ++j)
                if (r->source_base == g->instances[j].value.view ||
                    r->source_authority == g->instances[j].value.view)
                    return fail(error, QA_ERROR_FORMAT, "Source filesystem authority aliases a private provider view");
            for (size_t j = 0; j < i; ++j) {
                const content_resource *prior = g->resources + j;
                if (r->source_base == prior->origin_view || r->source_authority == prior->origin_view ||
                    r->origin_view == prior->source_base || r->origin_view == prior->source_authority ||
                    r->source_base == prior->source_base || r->source_base == prior->source_authority ||
                    r->source_authority == prior->source_base || r->source_authority == prior->source_authority)
                    return fail(error, QA_ERROR_FORMAT, "Source filesystem opening aliases independent acquisition custody");
            }
        }
        for (size_t j = 0; j < g->instance_count; ++j)
            if (r->origin_view == g->instances[j].value.view)
                return fail(error, QA_ERROR_FORMAT, "Saved resource opening aliases a private provider view");
        for (size_t j = 0; j < i; ++j)
            if (r->origin_view == g->resources[j].origin_view ||
                r->origin_view == g->resources[j].source_base || r->origin_view == g->resources[j].source_authority)
                return fail(error, QA_ERROR_FORMAT, "Saved launch openings alias separate acquisition owners");
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
            !qa_vfs_acquisition_retained(v->content, v->artifact_acquisition, error)))
            return fail(error, QA_ERROR_FORMAT, "Saved provider artifact receipt leaves its actual restored view");
        if (v->artifact && !r->artifact_retained) { qa_resource_retain((qa_resource *)v->artifact); r->artifact_retained = true; }
        if (v->declaration && !r->declaration_retained) { qa_resource_retain((qa_resource *)v->declaration); r->declaration_retained = true; }
        if (!r->interface_values && v->interface_count) r->interface_values = calloc(v->interface_count, sizeof(*r->interface_values));
        if (v->interface_count && !r->interface_values)
            return fail(error, QA_ERROR_MEMORY, "Resolving saved provider descriptor arrays");
        for (size_t j = 0; j < v->interface_count; ++j) {
            if (!resource_resolve(g, &r->interfaces[j], error)) return false;
            r->interface_values[j] = r->interfaces[j].value;
        }
        v->interfaces = r->interface_values;
    }
    qa_catalog *launch = qa_application_content_catalog(g, g->launch_catalog);
    for (size_t i = 0; i < g->resource_count; ++i) {
        content_resource *r = &g->resources[i];
        qa_vfs *view = qa_application_content_view(g, r->origin_view);
        qa_product_id content = 0; qa_mount_id physical = 0;
        if (!qa_catalog_product(launch, r->value.product) || !resource_resolve(g, r, error)) return false;
        if (r->origin_kind == QA_LAUNCH_ORIGIN_CATALOG) {
            if (!qa_catalog_product_acquisition_origin(launch, r->value.product, view, &r->acquisition, &content, &physical, error) ||
                content != r->origin_product || physical != r->catalog_mount)
                return fail(error, QA_ERROR_FORMAT, "Saved launch resource origin leaves its actual catalog opening");
        } else {
            r->source.catalog = launch; r->source.product = r->value.product; r->source.content = view;
            r->source.base = qa_application_content_view(g, r->source_base);
            r->source.authority = qa_application_content_view(g, r->source_authority);
            if (!qa_launch_source_files_current(&r->source, error) ||
                !qa_vfs_acquisition_retained(view, &r->acquisition, error))
                return fail(error, QA_ERROR_FORMAT, "Saved Source filesystem opening leaves its declared retained authority");
        }
        char *requested = qa_vfs_normalize_path(r->value.path, error);
        bool same = requested && !strcmp(requested, r->acquisition.path);
        free(requested);
        if (!same) return fail(error, QA_ERROR_FORMAT, "Saved launch opening differs from its actual requested resource path");
    }
    return true;
}

static bool files_encode(void *context, const qa_vfs *view, qa_buffer *out, qa_error *error)
{
    const qa_application_content_graph *g = context; uint64_t id = qa_application_content_view_id(g, view);
    if (!id || !g->views[id - 1].catalog || !out || out->data || out->size)
        return fail(error, QA_ERROR_FORMAT, "Catalog files are outside actual content graph");
    return true;
}
static bool refresh(qa_application_content_graph *g, qa_error *error)
{
    if (!resolve(g, error)) return false;
    const qa_vfs **views = g->view_count ? calloc(g->view_count, sizeof(*views)) : NULL;
    if (g->view_count && !views) return fail(error, QA_ERROR_MEMORY, "Retaining linked archive view custody");
    for (size_t i = 0; i < g->view_count; ++i) views[i] = g->views[i].value;
    for (size_t i = 0; i < g->pool_count; ++i) {
        qa_buffer bytes = {0};
        bool ok = qa_resource_pool_checkpoint_linked(g->pools[i].value, views, g->view_count, &bytes, error);
        if (!ok) { free(views); return false; }
        qa_buffer_free(&g->pools[i].bytes); g->pools[i].bytes = bytes;
    }
    free(views);
    for (size_t i = 0; i < g->view_count; ++i) {
        content_view *v = &g->views[i]; qa_buffer bytes = {0};
        if (!qa_vfs_checkpoint(v->value, &bytes, error)) return false;
        qa_buffer_free(&v->bytes); v->bytes = bytes;
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
static bool files_decode(void *context, qa_resource_pool *pool, qa_bytes bytes, qa_vfs **out, qa_error *error)
{
    catalog_admission *a = context; content_view *v = a->view;
    (void)bytes;
    if (!out || *out || !v->owned || !v->value || qa_vfs_resources(v->value) != pool)
        return fail(error, QA_ERROR_FORMAT, "Catalog files require their one decoded graph view owner");
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
    for (size_t i = 0; ok && i < g->pool_count; ++i) {
        content_pool *p = &g->pools[i]; p->value = qa_resource_pool_create(error); p->owned = p->value != NULL;
        ok = p->value && qa_resource_pool_restore_linked(p->value, files,
            (qa_bytes){p->bytes.data, p->bytes.size}, error);
    }
    for (size_t i = 0; ok && i < g->view_count; ++i) {
        content_view *v = &g->views[i];
        ok = qa_vfs_create_restored(qa_application_content_pool(g, v->pool), files,
            (qa_bytes){v->bytes.data, v->bytes.size}, &v->value, error);
        v->owned = v->value != NULL;
    }
    for (size_t i = 0; ok && i < g->catalog_count; ++i) {
        content_view *view = NULL;
        for (size_t j = 0; j < g->view_count; ++j) if (g->views[j].catalog == i + 1) { view = &g->views[j]; break; }
        catalog_admission admission = {.graph = g, .view = view};
        qa_catalog_checkpoint_refs refs = {.context = &admission, .files_decode = files_decode};
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
