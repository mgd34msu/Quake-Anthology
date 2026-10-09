#include "save_content.h"
#include "bots_save_private.h"
#include "guest_q3_components.h"
#include "internal.h"
#include "qa/source_save.h"
#include "qa/source_qw_files.h"
#include "qa/application_q3_factory.h"
#include "equipment_runtime.h"
#include "events_save.h"
#include "qa/map_sidecars.h"

#include <stdlib.h>
#include <string.h>

typedef struct content_pool { qa_resource_pool *value; bool owned; } content_pool;
typedef struct content_catalog {
    qa_catalog *value;
    uint64_t pool, generation;
    bool restricted;
} content_catalog;
typedef enum content_view_kind {
    CONTENT_VIEW_UNUSED, CONTENT_VIEW_CATALOG, CONTENT_VIEW_PRODUCT,
    CONTENT_VIEW_LAUNCH, CONTENT_VIEW_QW_CONTENT, CONTENT_VIEW_QW_BASE,
    CONTENT_VIEW_QW_AUTHORITY
} content_view_kind;
typedef struct content_view {
    qa_vfs *value;
    uint64_t pool, catalog, source_catalog, source;
    qa_catalog_mount_selection selection;
    qa_product_id product;
    content_view_kind kind;
    bool owned, needed;
} content_view;
typedef struct content_product {
    uint64_t catalog;
    qa_product_id saved, current;
    const char *identity;
} content_product;
typedef struct content_reference {
    uint64_t pool, resource, view, package;
    const char *path;
    const qa_resource *value;
    bool retained;
} content_reference;
typedef struct content_package {
    const char *name;
} content_package;
typedef struct content_source {
    uint64_t catalog, content, base, authority;
    qa_launch_source_files value;
} content_source;
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
    bool restored;
    content_pool *pools; size_t pool_count;
    content_catalog *catalogs; size_t catalog_count;
    content_view *views; size_t view_count;
    content_instance *instances; size_t instance_count;
    content_resource *resources; size_t resource_count;
    content_product *products; size_t product_count;
    content_reference *references; size_t reference_count;
    content_source *sources; size_t source_count;
    content_package *packages; size_t package_count;
    size_t ownership;
    uint64_t application_pool, application_catalog, launch_catalog, launch_view;
};

static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }
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
{
    if (g && view) for (size_t i = 0; i < g->view_count; ++i) if (g->views[i].value == view) {
        ((qa_application_content_graph *)g)->views[i].needed = true;
        return i + 1;
    }
    return 0;
}
qa_resource_pool *qa_application_content_pool(const qa_application_content_graph *g, uint64_t id)
{ return g && id && id <= g->pool_count ? g->pools[id - 1].value : NULL; }
qa_catalog *qa_application_content_catalog(const qa_application_content_graph *g, uint64_t id)
{ return g && id && id <= g->catalog_count ? g->catalogs[id - 1].value : NULL; }
qa_vfs *qa_application_content_view(const qa_application_content_graph *g, uint64_t id)
{ return g && id && id <= g->view_count ? g->views[id - 1].value : NULL; }
const qa_resource *qa_application_content_resource(const qa_application_content_graph *g, uint64_t pool, uint64_t resource)
{
    if (!g || !resource) return NULL;
    if (g->restored) {
        for (size_t i = 0; i < g->reference_count; ++i)
            if (g->references[i].pool == pool && g->references[i].resource == resource)
                return g->references[i].value;
        return NULL;
    }
    return qa_resource_pool_find(qa_application_content_pool(g, pool), resource);
}
static bool reference_add(qa_application_content_graph *, uint64_t,
    const qa_resource *, uint64_t, const char *, qa_error *);
bool qa_application_content_resource_id(const qa_application_content_graph *g,
    const qa_resource *value, uint64_t *pool, uint64_t *resource)
{
    if (!g || !value || !pool || !resource || pool == resource) return false;
    uint64_t id = qa_resource_id(value);
    if (!id) return false;
    for (size_t i = 0; i < g->pool_count; ++i) {
        if (qa_resource_pool_find(g->pools[i].value, id) != value) continue;
        if (!reference_add((qa_application_content_graph *)g, i + 1, value, 0, NULL, NULL)) return false;
        for (size_t j = 0; j < g->reference_count; ++j) if (g->references[j].value == value && g->references[j].pool == i + 1) {
            *pool = i + 1; *resource = g->references[j].resource; return true;
        }
        return false;
    }
    return false;
}

static bool add_pool(void *opaque, const qa_resource_pool *pool, qa_error *error)
{
    qa_application_content_graph *g = opaque;
    if (!pool) return fail(error, QA_ERROR_ARGUMENT, "Content visitor supplied no actual pool");
    if (qa_application_content_pool_id(g, pool)) return true;
    if (!append((void **)&g->pools, &g->pool_count, sizeof(*g->pools), error)) return false;
    g->pools[g->pool_count - 1] = (content_pool){.value=(qa_resource_pool *)pool,.owned=true};
    qa_resource_pool_retain((qa_resource_pool *)pool); return true;
}
static bool add_view(void *opaque, const qa_vfs *view, qa_error *error)
{
    qa_application_content_graph *g = opaque;
    if (!view) return fail(error, QA_ERROR_ARGUMENT, "Content visitor supplied no actual VFS");
    for (size_t i = 0; i < g->view_count; ++i) if (g->views[i].value == view) return true;
    qa_resource_pool *pool = qa_vfs_resources(view);
    if (!add_pool(g, pool, error) || !append((void **)&g->views, &g->view_count, sizeof(*g->views), error)) return false;
    g->views[g->view_count - 1] = (content_view){.value = (qa_vfs *)view, .pool = qa_application_content_pool_id(g, pool)};
    if (!qa_vfs_retain((qa_vfs *)view,error)) return false;
    g->views[g->view_count - 1].owned=true;
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
    g->catalogs[id - 1] = (content_catalog){.value = (qa_catalog *)catalog, .pool = qa_application_content_pool_id(g, pool),
        .generation=qa_catalog_generation(catalog),.restricted=qa_catalog_q3_restricted(catalog)};
    qa_catalog_retain((qa_catalog *)catalog);
    content_view *row = &g->views[qa_application_content_view_id(g, view) - 1];
    if (row->catalog) return fail(error, QA_ERROR_FORMAT, "Two catalogs claim the same private VFS destructor");
    row->catalog = id; row->kind=CONTENT_VIEW_CATALOG;
    if (row->owned) { qa_vfs_destroy(row->value); row->owned=false; }
    return true;
}
static bool reference_add(qa_application_content_graph *g, uint64_t pool,
    const qa_resource *value, uint64_t view, const char *path, qa_error *error)
{
    if (!g || !value || !pool || pool > g->pool_count) return false;
    for (size_t i = 0; i < g->reference_count; ++i) {
        content_reference *r = g->references + i;
        if (r->pool != pool || r->value != value) continue;
        if (view && !r->view) {
            if (path) {
                char *copy=copy_text(path,error);
                if (!copy) return false;
                free((void *)r->path); r->path=copy;
            }
            r->view = view;
        }
        return true;
    }
    if (!append((void **)&g->references, &g->reference_count, sizeof(*g->references), error)) return false;
    content_reference *r = g->references + g->reference_count - 1;
    r->pool = pool; r->resource = qa_resource_id(value); r->view = view;
    for (size_t i = 0; i + 1 < g->reference_count; ++i) {
        if (g->references[i].pool != pool || g->references[i].resource != r->resource) continue;
        r->resource = 1;
        for (size_t j = 0; j + 1 < g->reference_count; ++j) if (g->references[j].pool == pool && g->references[j].resource >= r->resource) {
            if (g->references[j].resource == UINT64_MAX) return fail(error, QA_ERROR_MEMORY, "Content reference handles exhausted");
            r->resource = g->references[j].resource + 1;
        }
        break;
    }
    r->path = copy_text(path ? path : qa_resource_path(value), error);
    if (!r->path) return false;
    r->value = value;
    qa_resource_retain((qa_resource *)value); r->retained = true;
    return true;
}
static bool copy_resource(qa_application_content_graph *g, qa_resource_pool *pool,
    const qa_launch_resource *source, content_resource *out, qa_error *error)
{
    if (!source || !source->resource || !source->path || !add_pool(g, pool, error)) return false;
    uint64_t id = qa_resource_id(source->resource);
    if (!id || qa_resource_pool_find(pool, id) != source->resource)
        return fail(error, QA_ERROR_FORMAT, "Source content resource is outside its actual pool");
    out->value.product = source->product; out->value.resource = source->resource;
    if (!qa_application_content_resource_id(g, source->resource, &out->pool, &out->resource)) return false;
    out->value.path = copy_text(source->path, error);
    if (!out->value.path) return false;
    qa_resource_retain((qa_resource *)out->value.resource); out->retained = true; return true;
}
static bool instance_collect(qa_application_content_graph *g, const qa_launch_instance *source,
    const application_provider *provider, qa_error *error)
{
    for (size_t i=0; source && i<g->instance_count; ++i)
        if (!strcmp(g->instances[i].value.source.selection.instance,source->selection.instance)) return true;
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
    value->catalog = catalog; value->content = source->content;
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
    if (source->artifact && !qa_application_content_resource_id(g, source->artifact, &row->artifact_pool, &row->artifact)) return false;
    if (source->declaration && !qa_application_content_resource_id(g, source->declaration, &row->declaration_pool, &row->declaration)) return false;
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

static void projection_clear(qa_application_content_graph *g)
{
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
    free(g->instances); free(g->resources); g->instances=NULL; g->resources=NULL;
    g->instance_count=g->resource_count=0;
}

static bool source_add(qa_application_content_graph *,content_resource *,qa_error *);
bool application_save_content_collect(qa_application *app, qa_application_content_visit_fn visit,
    void *context, qa_application_content_graph **out, qa_error *error)
{
    const qa_launch_snapshot *launch = app ? qa_application_launch(app) : NULL;
    if (!app || !app->resources || !app->catalog || !launch || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Content graph capture requires actual committed owners and empty output");
    qa_application_content_graph *g = app->content_graph;
    if (g) { ++g->ownership; projection_clear(g); }
    else {
        g=calloc(1,sizeof(*g));
        if (!g) return fail(error,QA_ERROR_MEMORY,"Retaining application content references");
        g->ownership=1;
    }
    bool ok = add_pool(g, app->resources, error) && add_catalog(g, app->catalog, error) &&
        add_catalog(g, qa_launch_snapshot_catalog(launch), error) && add_view(g, qa_launch_snapshot_mounts(launch), error);
    if (ok) {
        g->application_pool = qa_application_content_pool_id(g, app->resources);
        g->application_catalog = qa_application_content_catalog_id(g, app->catalog);
        g->launch_catalog = qa_application_content_catalog_id(g, qa_launch_snapshot_catalog(launch));
        g->launch_view = qa_application_content_view_id(g, qa_launch_snapshot_mounts(launch));
        content_view *v=g->views+g->launch_view-1;
        if (!v->kind) {
            v->kind=CONTENT_VIEW_LAUNCH; v->source_catalog=g->launch_catalog;
            qa_product_id *additional=NULL;
            ok=qa_launch_mount_selection_read(qa_launch_snapshot_catalog(launch),
                qa_launch_snapshot_choices(launch),&v->selection,&additional,error);
            v->selection.additional=additional;
        }
    }
    for (size_t i = 0; ok && i < qa_launch_snapshot_instance_count(launch); ++i) {
        const qa_launch_instance *instance = qa_launch_snapshot_instance(launch, i);
        const application_provider *provider = instance ? instance->state : NULL;
        ok = instance_collect(g, instance, provider, error);
    }
    size_t actual_resources=qa_launch_snapshot_resource_count(launch);
    for (size_t i=0;ok && i<actual_resources;++i) {
        if (!append((void **)&g->resources,&g->resource_count,sizeof(*g->resources),error)) { ok=false; break; }
        qa_launch_resource_origin origin = {0};
        content_resource *row = &g->resources[g->resource_count-1];
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
                    ok = row->source.directory && row->source.home_prefix && source_add(g,row,error);
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
    if (g->ownership && --g->ownership) return;
    projection_clear(g);
    for (size_t i=0;i<g->reference_count;++i) {
        free((void *)g->references[i].path);
        if (g->references[i].retained) qa_resource_release((qa_resource *)g->references[i].value);
    }
    /* Catalogs retain their pool and destroy their private view. Standalone
     * views and pools are destroyed only while their real ownership is here. */
    for (size_t i = 0; g->catalogs && i < g->catalog_count; ++i) {
        qa_catalog_release(g->catalogs[i].value);
    }
    for (size_t i = 0; g->views && i < g->view_count; ++i) {
        if (g->views[i].owned) qa_vfs_destroy(g->views[i].value);
        free((void *)g->views[i].selection.additional);
    }
    for (size_t i = 0; g->pools && i < g->pool_count; ++i) {
        if (g->pools[i].owned) qa_resource_pool_destroy(g->pools[i].value);
    }
    for (size_t i=0;i<g->product_count;++i) free((void *)g->products[i].identity);
    for (size_t i=0;i<g->source_count;++i) { free((void *)g->sources[i].value.directory); free((void *)g->sources[i].value.home_prefix); }
    for (size_t i=0;i<g->package_count;++i) free((void *)g->packages[i].name);
    free(g->products); free(g->references); free(g->sources); free(g->packages);
    free(g->pools); free(g->catalogs); free(g->views); free(g->instances); free(g->resources); free(g);
}

bool qa_application_content_claim_pool(qa_application_content_graph *g, uint64_t id,
    qa_resource_pool **out, qa_error *error)
{
    qa_resource_pool *pool=qa_application_content_pool(g,id);
    if (!pool || !out || *out) return fail(error,QA_ERROR_ARGUMENT,"Content pool reference is absent");
    qa_resource_pool_retain(pool); *out=pool; return true;
}
bool qa_application_content_claim_view(qa_application_content_graph *g, uint64_t id,
    qa_vfs **out, qa_error *error)
{
    qa_vfs *view=qa_application_content_view(g,id);
    if (!view || !out || *out || !qa_vfs_retain(view,error)) return false;
    *out=view; return true;
}
bool qa_application_content_retain_catalog(qa_application_content_graph *g,uint64_t id,
    qa_catalog **out,qa_error *error)
{
    qa_catalog *catalog=qa_application_content_catalog(g,id);
    if (!catalog || !out || *out) return fail(error,QA_ERROR_ARGUMENT,"Content product catalog is absent");
    qa_catalog_retain(catalog); *out=catalog; return true;
}
bool qa_application_content_retain_view(qa_application_content_graph *g,uint64_t id,
    qa_vfs **out,qa_error *error)
{ return qa_application_content_claim_view(g,id,out,error); }
bool application_save_content_ready(const qa_application_content_graph *g,qa_error *error)
{
    return (g && qa_application_content_pool(g,g->application_pool) &&
        qa_application_content_catalog(g,g->application_catalog) &&
        qa_application_content_view(g,g->launch_view)) ||
        fail(error,QA_ERROR_FORMAT,"Installed content references are incomplete");
}
bool application_save_content_retain_current(qa_application *app,
    qa_application_content_visit_fn visit,void *context,qa_application_content_graph **out,
    qa_error *error)
{
    if (!application_save_content_collect(app,visit,context,out,error)) return false;
    if (!app->content_graph) { app->content_graph=*out; ++(*out)->ownership; }
    return true;
}
uint64_t application_save_content_application_pool(const qa_application_content_graph *g) { return g ? g->application_pool : 0; }
uint64_t application_save_content_application_catalog(const qa_application_content_graph *g) { return g ? g->application_catalog : 0; }
uint64_t application_save_content_launch_catalog(const qa_application_content_graph *g) { return g ? g->launch_catalog : 0; }
uint64_t application_save_content_launch_view(const qa_application_content_graph *g) { return g ? g->launch_view : 0; }
bool qa_application_content_retain_pool(qa_application_content_graph *g, uint64_t id,
    qa_resource_pool **out, qa_error *error)
{
    qa_resource_pool *pool = qa_application_content_pool(g, id);
    if (!pool || !out || *out)
        return fail(error,QA_ERROR_FORMAT,"Installed content pool is absent");
    qa_resource_pool_retain(pool); *out=pool;
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
    if (!g || i >= g->resource_count || !out)
        return fail(error, QA_ERROR_ARGUMENT, "Source opening claim requires its actual content references");
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
static bool product_add(qa_application_content_graph *g,uint64_t catalog,
    qa_product_id actual,uint32_t *handle,qa_error *error)
{
    if (!actual) { *handle=0; return true; }
    const qa_product *p=qa_catalog_product(qa_application_content_catalog(g,catalog),actual);
    if (!p) return fail(error,QA_ERROR_FORMAT,"Used content product is absent");
    uint32_t next=1;
    for (size_t i=0;i<g->product_count;++i) {
        content_product *r=g->products+i;
        if (r->catalog!=catalog) continue;
        if (!strcmp(r->identity,p->identity)) { *handle=r->saved; return true; }
        if (r->saved>=next) {
            if (r->saved==UINT32_MAX) return fail(error,QA_ERROR_MEMORY,"Content product handles exhausted");
            next=r->saved+1;
        }
    }
    if (!append((void **)&g->products,&g->product_count,sizeof(*g->products),error)) return false;
    content_product *r=g->products+g->product_count-1;
    r->catalog=catalog; r->saved=next; r->current=actual;
    r->identity=copy_text(p->identity,error); *handle=next; return r->identity!=NULL;
}
qa_product_id qa_application_content_product(const qa_application_content_graph *g,
    const qa_catalog *catalog,uint32_t handle)
{
    uint64_t id=qa_application_content_catalog_id(g,catalog);
    for (size_t i=0;g && i<g->product_count;++i)
        if (g->products[i].catalog==id && g->products[i].saved==handle) return g->products[i].current;
    return 0;
}
static bool product_field(qa_source_save_io *io,qa_application_content_graph *g,
    uint64_t catalog,qa_product_id *value)
{
    uint32_t handle=*value;
    if (io->direction==QA_SOURCE_SAVE_WRITE && !product_add(g,catalog,*value,&handle,io->error)) return false;
    if (!qa_source_save_u32(io,&handle)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) *value=handle;
    return true;
}
static bool selection_fields(qa_source_save_io *io,qa_application_content_graph *g,
    uint64_t catalog,qa_catalog_mount_selection *s)
{
    if (!product_field(io,g,catalog,&s->assets) || !product_field(io,g,catalog,&s->geometry) ||
        !product_field(io,g,catalog,&s->combat) || !qa_source_save_bool(io,&s->explicit_presentation)) return false;
    qa_product_id *p=(qa_product_id *)s->additional;
    if (!table_field(io,(void **)&p,&s->additional_count,sizeof(*p),4)) return false;
    s->additional=p;
    for (size_t i=0;i<s->additional_count;++i) if (!product_field(io,g,catalog,p+i)) return false;
    return true;
}
static bool resource_fields(qa_source_save_io *io,qa_application_content_graph *g,
    uint64_t catalog,content_resource *row)
{
    return product_field(io,g,catalog,&row->value.product) &&
        qa_source_save_u64(io,&row->pool) && qa_source_save_u64(io,&row->resource) &&
        text_field(io,&row->value.path);
}
static bool instance_fields(qa_source_save_io *io,qa_application_content_graph *g,content_instance *row)
{
    qa_launch_restored_instance *v=&row->value.source; qa_launch_provider *p=&v->selection;
    FIELD(u64,row,catalog); FIELD(u64,row,product_catalog); FIELD(u64,&row->value,view);
    if (!product_field(io,g,row->product_catalog,&row->product) ||
        !product_field(io,g,row->catalog,&p->product) ||
        !text_field(io,&p->instance) || !text_field(io,&p->implementation) ||
        !text_field(io,&p->artifact) || !text_field(io,&p->component)) return false;
    uint32_t runtime=p->runtime,clock=p->clock.kind;
    if (!qa_source_save_u32(io,&runtime) || runtime>QA_PROGRAM_NATIVE ||
        !qa_source_save_u32(io,&clock) || clock>QA_RULESET_Q3) return false;
    p->runtime=(qa_program_kind)runtime; p->clock.kind=(qa_ruleset_id)clock;
    FIELD(u64,&p->clock,initial_time_ns); FIELD(u64,&p->clock,interval_ns);
    FIELD(u64,&p->clock,minimum_frame_ns); FIELD(u64,&p->clock,maximum_frame_ns);
    FIELD(u64,&p->clock,initial_lead_ns); FIELD(u32,&p->clock,maximum_steps);
    if (!blob_field(io,&row->options)) return false;
    p->options=(qa_bytes){row->options.data,row->options.size};
    FIELD(u64,row,artifact_pool); FIELD(u64,row,artifact);
    FIELD(u64,row,declaration_pool); FIELD(u64,row,declaration);
    if (!table_field(io,(void **)&row->interfaces,&v->interface_count,sizeof(*row->interfaces),24)) return false;
    for (size_t i=0;i<v->interface_count;++i)
        if (!resource_fields(io,g,row->catalog,row->interfaces+i)) return false;
    return true;
}
static bool source_add(qa_application_content_graph *g,content_resource *r,qa_error *error)
{
    for (size_t i=0;i<g->source_count;++i) if (g->sources[i].content==r->origin_view) return true;
    if (!append((void **)&g->sources,&g->source_count,sizeof(*g->sources),error)) return false;
    content_source *s=g->sources+g->source_count-1;
    s->catalog=qa_application_content_catalog_id(g,r->source.catalog);
    s->content=r->origin_view; s->base=r->source_base; s->authority=r->source_authority;
    s->value=r->source; s->value.directory=copy_text(r->source.directory,error);
    s->value.home_prefix=copy_text(r->source.home_prefix,error);
    if (!s->value.directory || !s->value.home_prefix) return false;
    const uint64_t ids[]={s->content,s->base,s->authority};
    const content_view_kind kinds[]={CONTENT_VIEW_QW_CONTENT,CONTENT_VIEW_QW_BASE,CONTENT_VIEW_QW_AUTHORITY};
    for (size_t i=0;i<3;++i) {
        if (!ids[i] || ids[i]>g->view_count) return false;
        content_view *v=g->views+ids[i]-1;
        v->kind=kinds[i]; v->source=g->source_count; v->source_catalog=s->catalog; v->needed=true;
    }
    return true;
}
static bool reference_scope(qa_application_content_graph *g,content_reference *r,qa_error *error)
{
    if (!r->view) {
        for (size_t i=0;i<g->view_count && !r->view;++i) {
            if (g->views[i].pool!=r->pool || !g->views[i].value) continue;
            size_t count=qa_vfs_retained_read_count(g->views[i].value);
            for (size_t j=0;j<count;++j) {
                qa_vfs_read_reference read;
                if (!qa_vfs_retained_read_at(g->views[i].value,j,&read) || read.resource!=r->value) continue;
                char *path=copy_text(read.path,error);
                if (!path) return false;
                free((void *)r->path); r->path=path; r->view=i+1; break;
            }
        }
    }
    if (!r->view || r->view>g->view_count) return fail(error,QA_ERROR_FORMAT,"Used resource has no installed Source file scope");
    g->views[r->view-1].needed=true;
    const char *name=qa_resource_package_name(r->value);
    if (!name) { r->package=0; return true; }
    for (size_t i=0;i<g->package_count;++i)
        if (!strcmp(g->packages[i].name,name)) {
            r->package=i+1; return true;
        }
    if (!append((void **)&g->packages,&g->package_count,sizeof(*g->packages),error)) return false;
    content_package *p=g->packages+g->package_count-1;
    p->name=copy_text(name,error); r->package=g->package_count;
    return p->name!=NULL;
}
static bool prepare_manifest(qa_application_content_graph *g,qa_error *error)
{
    for (size_t i=0;i<g->instance_count;++i) {
        content_instance *r=g->instances+i;
        qa_launch_restored_instance *s=&r->value.source;
        if (s->artifact && !reference_add(g,r->artifact_pool,s->artifact,r->value.view,s->artifact_acquisition->path,error)) return false;
        if (s->declaration && !reference_add(g,r->declaration_pool,s->declaration,r->value.view,NULL,error)) return false;
        for (size_t j=0;j<s->interface_count;++j)
            if (!reference_add(g,r->interfaces[j].pool,r->interfaces[j].value.resource,r->value.view,r->interfaces[j].value.path,error)) return false;
    }
    for (size_t i=0;i<g->resource_count;++i) {
        content_resource *r=g->resources+i;
        if (!reference_add(g,r->pool,r->value.resource,r->origin_view,r->value.path,error) ||
            (r->origin_kind==QA_LAUNCH_ORIGIN_SOURCE_QW && !source_add(g,r,error))) return false;
    }
    for (size_t i=0;i<g->reference_count;++i) if (!reference_scope(g,g->references+i,error)) return false;
    for (size_t i=0;i<g->view_count;++i) {
        content_view *v=g->views+i;
        if (!v->needed || v->kind) continue;
        for (size_t c=0;c<g->catalog_count && !v->kind;++c) {
            if (g->catalogs[c].pool!=v->pool) continue;
            qa_catalog *catalog=g->catalogs[c].value;
            for (size_t p=0;p<qa_catalog_count(catalog);++p) {
                const qa_product *product=qa_catalog_at(catalog,p);
                if (!qa_catalog_product_view_current(catalog,product->id,v->value)) continue;
                v->kind=CONTENT_VIEW_PRODUCT; v->source_catalog=c+1; v->product=product->id; break;
            }
        }
        if (!v->kind && qa_vfs_lookup_equal(v->value,qa_application_content_view(g,g->launch_view))) {
            content_view *source=g->views+g->launch_view-1;
            v->kind=CONTENT_VIEW_LAUNCH; v->source_catalog=source->source_catalog;
            v->selection=source->selection;
            v->selection.additional=NULL;
            if (source->selection.additional_count) {
                size_t bytes=source->selection.additional_count*sizeof(qa_product_id);
                qa_product_id *copy=malloc(bytes);
                if (!copy) return fail(error,QA_ERROR_MEMORY,"Retaining selected Source products");
                memcpy(copy,source->selection.additional,bytes); v->selection.additional=copy;
            }
        }
        if (!v->kind) return fail(error,QA_ERROR_UNSUPPORTED,"Used content view has no actual product or launch constructor");
    }
    /* Register the products before their table is written. */
    for (size_t i=0;i<g->instance_count;++i) {
        content_instance *r=g->instances+i; uint32_t handle=0;
        if (!product_add(g,r->catalog,r->value.source.selection.product,&handle,error) ||
            !product_add(g,r->product_catalog,r->product,&handle,error)) return false;
        for (size_t j=0;j<r->value.source.interface_count;++j)
            if (!product_add(g,r->catalog,r->interfaces[j].value.product,&handle,error)) return false;
    }
    for (size_t i=0;i<g->resource_count;++i) { uint32_t handle=0;
        if (!product_add(g,g->launch_catalog,g->resources[i].value.product,&handle,error)) return false;
    }
    for (size_t i=0;i<g->view_count;++i) {
        content_view *v=g->views+i; uint32_t handle=0;
        if (v->kind==CONTENT_VIEW_PRODUCT && !product_add(g,v->source_catalog,v->product,&handle,error)) return false;
        if (v->kind==CONTENT_VIEW_LAUNCH) {
            if (!product_add(g,v->source_catalog,v->selection.assets,&handle,error) ||
                !product_add(g,v->source_catalog,v->selection.geometry,&handle,error) ||
                !product_add(g,v->source_catalog,v->selection.combat,&handle,error)) return false;
            for (size_t j=0;j<v->selection.additional_count;++j)
                if (!product_add(g,v->source_catalog,v->selection.additional[j],&handle,error)) return false;
        }
    }
    for (size_t i=0;i<g->source_count;++i) { uint32_t handle=0;
        if (!product_add(g,g->sources[i].catalog,g->sources[i].value.product,&handle,error)) return false;
    }
    return true;
}
static bool manifest_fields(qa_source_save_io *io,qa_application_content_graph *g)
{
    uint8_t magic[4]={'Q','A','C','M'};
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QACM",sizeof(magic))) return false;
    FIELD(u64,g,application_pool); FIELD(u64,g,application_catalog); FIELD(u64,g,launch_catalog); FIELD(u64,g,launch_view);
    if (!table_field(io,(void **)&g->pools,&g->pool_count,sizeof(*g->pools),1)) return false;
    if (!table_field(io,(void **)&g->catalogs,&g->catalog_count,sizeof(*g->catalogs),17)) return false;
    for (size_t i=0;i<g->catalog_count;++i) {
        FIELD(u64,&g->catalogs[i],pool); FIELD(u64,&g->catalogs[i],generation); FIELD(bool,&g->catalogs[i],restricted);
    }
    if (!table_field(io,(void **)&g->products,&g->product_count,sizeof(*g->products),20)) return false;
    for (size_t i=0;i<g->product_count;++i) {
        content_product *p=g->products+i;
        FIELD(u64,p,catalog); FIELD(u32,p,saved);
        if (!text_field(io,&p->identity)) return false;
    }
    if (!table_field(io,(void **)&g->views,&g->view_count,sizeof(*g->views),4)) return false;
    for (size_t i=0;i<g->view_count;++i) {
        content_view *v=g->views+i;
        uint32_t kind=v->needed?(uint32_t)v->kind:CONTENT_VIEW_UNUSED;
        if (!qa_source_save_u32(io,&kind) || kind>CONTENT_VIEW_QW_AUTHORITY) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) {
            v->kind=(content_view_kind)kind; v->needed=kind!=CONTENT_VIEW_UNUSED;
        }
        if (!kind) continue;
        FIELD(u64,v,pool); FIELD(u64,v,catalog); FIELD(u64,v,source_catalog);
        if (kind==CONTENT_VIEW_PRODUCT && !product_field(io,g,v->source_catalog,&v->product)) return false;
        if (kind==CONTENT_VIEW_LAUNCH && !selection_fields(io,g,v->source_catalog,&v->selection)) return false;
        if (kind>=CONTENT_VIEW_QW_CONTENT) FIELD(u64,v,source);
    }
    if (!table_field(io,(void **)&g->sources,&g->source_count,sizeof(*g->sources),45)) return false;
    for (size_t i=0;i<g->source_count;++i) {
        content_source *s=g->sources+i;
        FIELD(u64,s,catalog); FIELD(u64,s,content); FIELD(u64,s,base); FIELD(u64,s,authority);
        if (!product_field(io,g,s->catalog,&s->value.product) ||
            !qa_source_save_bool(io,&s->value.changed) || !text_field(io,&s->value.directory)) return false;
    }
    if (!table_field(io,(void **)&g->packages,&g->package_count,sizeof(*g->packages),8)) return false;
    for (size_t i=0;i<g->package_count;++i) {
        if (!text_field(io,&g->packages[i].name)) return false;
        const char *name=g->packages[i].name;
        if (!name || !*name || strpbrk(name,"/\\:") || !strcmp(name,".") || !strcmp(name,".."))
            return fail(io->error,QA_ERROR_FORMAT,"Used package requires its logical filename");
    }
    if (!table_field(io,(void **)&g->references,&g->reference_count,sizeof(*g->references),40)) return false;
    for (size_t i=0;i<g->reference_count;++i) {
        content_reference *r=g->references+i;
        FIELD(u64,r,pool); FIELD(u64,r,resource); FIELD(u64,r,view); FIELD(u64,r,package);
        if (!text_field(io,&r->path)) return false;
    }
    if (!table_field(io,(void **)&g->instances,&g->instance_count,sizeof(*g->instances),128)) return false;
    for (size_t i=0;i<g->instance_count;++i) if (!instance_fields(io,g,g->instances+i)) return false;
    if (!table_field(io,(void **)&g->resources,&g->resource_count,sizeof(*g->resources),40)) return false;
    for (size_t i=0;i<g->resource_count;++i) {
        content_resource *r=g->resources+i;
        if (!resource_fields(io,g,g->launch_catalog,r)) return false;
        FIELD(u64,r,origin_view);
        uint32_t kind=r->origin_kind;
        if (!qa_source_save_u32(io,&kind) || kind>QA_LAUNCH_ORIGIN_SOURCE_QW) return false;
        r->origin_kind=(qa_launch_resource_origin_kind)kind;
    }
    return true;
}
static qa_product_id current_product(qa_application_content_graph *g,uint64_t catalog,qa_product_id handle)
{ return qa_application_content_product(g,qa_application_content_catalog(g,catalog),handle); }
static bool open_source(qa_application_content_graph *g,content_source *s,
    const qa_application_options *options,qa_error *error)
{
    qa_catalog *catalog=qa_application_content_catalog(g,s->catalog);
    s->value.product=current_product(g,s->catalog,s->value.product);
    if (!catalog || !s->value.product || !s->content || s->content>g->view_count ||
        !s->base || s->base>g->view_count || !s->authority || s->authority>g->view_count ||
        s->base==s->authority || s->base==s->content || s->authority==s->content ||
        g->views[s->content-1].value)
        return fail(error,QA_ERROR_FORMAT,"Source gamedir manifest is incomplete");
    qa_launch_source_files *out=&s->value;
    out->base=g->views[s->base-1].value;
    char *initial=NULL,*prefix=NULL;
    if (!qa_source_qw_files_base(catalog,out->product,&out->base,&out->base_product,&initial,&prefix,error)) {
        free(initial); free(prefix); return false;
    }
    free(initial); out->home_prefix=prefix; out->catalog=catalog;
    const qa_catalog_mount *family=qa_catalog_product_family_mount(catalog,out->product);
    out->authority=g->views[s->authority-1].value;
    bool ok=family!=NULL;
    if (!out->authority) {
        out->authority=qa_vfs_create(qa_catalog_resources(catalog),error);
        ok=ok && out->authority && options->user_root &&
            qa_vfs_mount_retained(out->authority,qa_catalog_files(catalog),family->id,
                QA_ARCHIVE_CASE_INSENSITIVE,false,&out->family,error) &&
            qa_vfs_mount_directory(out->authority,options->user_root,
                QA_ARCHIVE_CASE_INSENSITIVE,true,&out->home,error);
    } else {
        for (const content_source *previous=g->sources;previous<s;++previous)
            if (previous->authority==s->authority) {
                out->family=previous->value.family; out->home=previous->value.home; break;
            }
        ok=ok && out->family && out->home;
    }
    if (ok && out->changed) {
        qa_mount_id writable=0; char *child=NULL;
        ok=qa_source_qw_files_change(out->base,out->authority,out->family,out->home,out->home_prefix,
            out->directory,&out->content,&writable,&child,error); free(child);
    } else if (ok) ok=qa_catalog_open(catalog,out->product,&out->content,error);
    g->views[s->base-1].value=out->base; g->views[s->base-1].owned=out->base!=NULL;
    g->views[s->authority-1].value=out->authority; g->views[s->authority-1].owned=out->authority!=NULL;
    g->views[s->content-1].value=out->content; g->views[s->content-1].owned=out->content!=NULL;
    return ok && qa_launch_source_files_current(out,error);
}
typedef struct acquisition_scope { const qa_vfs *view; qa_fs_identity archive; } acquisition_scope;
static bool acquisition_mount(qa_mount_id id,void *opaque)
{
    acquisition_scope *scope=opaque;
    for (size_t i=0;i<qa_vfs_mount_count(scope->view);++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(scope->view,i,&mount) && mount.id==id)
            return mount.is_archive && mount.identity && qa_fs_identity_equal(mount.identity,&scope->archive);
    }
    return false;
}
bool qa_application_content_acquire(const qa_vfs *view,const qa_resource *expected,const char *path,
    qa_vfs_acquisition *out,qa_error *error)
{
    qa_resource *actual=NULL;
    acquisition_scope scope={.view=view};
    bool archive=qa_resource_archive_origin(expected,&scope.archive,NULL);
    if (!qa_vfs_acquire_filtered_receipt((qa_vfs *)view,path,archive?acquisition_mount:NULL,
        &scope,&actual,out,error)) return false;
    bool ok=actual==expected;
    qa_resource_release(actual);
    if (!ok) qa_vfs_acquisition_dispose(out);
    return ok || fail(error,QA_ERROR_FORMAT,"Installed resource differs from its saved content identity");
}
bool qa_application_content_acquisition(qa_source_save_io *io,
    const qa_application_content_graph *g,const qa_vfs *view,const qa_resource *resource,
    qa_vfs_acquisition *receipt)
{
    (void)g;
    if (!io || !view || !resource || !receipt) return false;
    char *path=io->direction==QA_SOURCE_SAVE_WRITE?receipt->path:NULL;
    if (!qa_source_save_owned_text(io,&path)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_vfs_acquisition_retained(view,receipt,io->error);
    qa_vfs_acquisition_dispose(receipt);
    bool ok=path && *path && qa_application_content_acquire(view,resource,path,receipt,io->error);
    free(path); return ok;
}
typedef struct manifest_scope {
    const qa_application_content_graph *graph;
    const qa_vfs *view;
    uint64_t package;
} manifest_scope;
static bool manifest_mount(qa_mount_id id,void *opaque)
{
    manifest_scope *scope=opaque;
    const qa_archive *archive=qa_vfs_archive(scope->view,id);
    if (!scope->package) return archive==NULL;
    if (!archive) return false;
    const content_package *expected=scope->graph->packages+scope->package-1;
    const char *path=qa_vfs_mount_path(scope->view,id),*leaf=path;
    if (!path) return false;
    for (const char *p=path;*p;++p) if (*p=='/' || *p=='\\') leaf=p+1;
    return !strcmp(leaf,expected->name);
}
static bool resolve_manifest(qa_application_content_graph *g,const qa_application_options *options,qa_error *error)
{
    if (!g->pool_count || !g->catalog_count || !g->application_pool || g->application_pool>g->pool_count ||
        !g->application_catalog || g->application_catalog>g->catalog_count ||
        !g->launch_catalog || g->launch_catalog>g->catalog_count ||
        !g->launch_view || g->launch_view>g->view_count) return false;
    for (size_t i=0;i<g->pool_count;++i) {
        g->pools[i].value=qa_resource_pool_create(error); g->pools[i].owned=g->pools[i].value!=NULL;
        if (!g->pools[i].value) return false;
    }
    for (size_t i=0;i<g->catalog_count;++i) {
        content_catalog *c=g->catalogs+i;
        qa_catalog_options discover={.resources=qa_application_content_pool(g,c->pool),
            .content_root=options->content_root,.user_root=options->user_root,
            .install_roots=options->install_roots,.install_root_count=options->install_root_count,
            .generation=c->generation,.discover_mods=true};
        if (!discover.resources || !discover.generation || !qa_catalog_discover(&discover,&c->value,error) ||
            (c->restricted && !qa_catalog_q3_restrict(c->value,error))) return false;
    }
    for (size_t i=0;i<g->product_count;++i) {
        content_product *p=g->products+i;
        const qa_product *actual=qa_catalog_find(qa_application_content_catalog(g,p->catalog),p->identity);
        if (!actual || !p->saved) return fail(error,QA_ERROR_NOT_FOUND,"A save's selected product is not installed");
        p->current=actual->id;
        for (size_t j=0;j<i;++j) if (g->products[j].catalog==p->catalog &&
            (g->products[j].saved==p->saved || !strcmp(g->products[j].identity,p->identity))) return false;
    }
    for (size_t i=0;i<g->view_count;++i) {
        content_view *v=g->views+i;
        if (!v->kind || v->kind>=CONTENT_VIEW_QW_CONTENT) continue;
        qa_catalog *catalog=qa_application_content_catalog(g,v->catalog?v->catalog:v->source_catalog);
        if (!catalog || v->pool!=qa_application_content_pool_id(g,qa_catalog_resources(catalog))) return false;
        if (v->kind==CONTENT_VIEW_CATALOG) v->value=(qa_vfs *)qa_catalog_files(catalog);
        else if (v->kind==CONTENT_VIEW_PRODUCT) {
            v->product=current_product(g,v->source_catalog,v->product);
            if (!v->product || !qa_catalog_open(catalog,v->product,&v->value,error)) return false;
            v->owned=true;
        } else if (v->kind==CONTENT_VIEW_LAUNCH) {
            v->selection.assets=current_product(g,v->source_catalog,v->selection.assets);
            v->selection.geometry=current_product(g,v->source_catalog,v->selection.geometry);
            v->selection.combat=current_product(g,v->source_catalog,v->selection.combat);
            qa_product_id *additional=(qa_product_id *)v->selection.additional;
            for (size_t j=0;j<v->selection.additional_count;++j) {
                additional[j]=current_product(g,v->source_catalog,additional[j]);
                if (!additional[j]) return false;
            }
            if (!qa_catalog_mount_plan(catalog,&v->selection,&v->value,error)) return false;
            v->owned=true;
        }
    }
    for (size_t i=0;i<g->source_count;++i) if (!open_source(g,g->sources+i,options,error)) return false;
    for (size_t i=0;i<g->reference_count;++i) {
        content_reference *r=g->references+i;
        qa_vfs *view=qa_application_content_view(g,r->view);
        qa_resource *resource=NULL;
        manifest_scope scope={.graph=g,.view=view,.package=r->package};
        char *normal=qa_vfs_normalize_path(r->path,error);
        bool safe=normal && *normal && !strcmp(normal,r->path); free(normal);
        if (!safe || !r->resource || !view || r->package>g->package_count ||
            qa_vfs_resources(view)!=qa_application_content_pool(g,r->pool)) return false;
        if (!qa_vfs_acquire_filtered(view,r->path,manifest_mount,&scope,&resource,NULL,error)) return false;
        r->value=resource; r->retained=true;
        for (size_t j=0;j<i;++j) if (g->references[j].pool==r->pool && g->references[j].resource==r->resource) return false;
    }
    for (size_t i=0;i<g->instance_count;++i) {
        content_instance *r=g->instances+i; qa_launch_restored_instance *v=&r->value.source;
        v->catalog=qa_application_content_catalog(g,r->catalog);
        r->value.product_catalog=qa_application_content_catalog(g,r->product_catalog);
        r->product=current_product(g,r->product_catalog,r->product);
        v->selection.product=current_product(g,r->catalog,v->selection.product);
        r->value.product=qa_catalog_product(r->value.product_catalog,r->product);
        v->content=qa_application_content_view(g,r->value.view);
        if (!v->catalog || !v->content || !r->value.product || !v->selection.product) return false;
        v->artifact=qa_application_content_resource(g,r->artifact_pool,r->artifact);
        v->declaration=qa_application_content_resource(g,r->declaration_pool,r->declaration);
        if ((r->artifact && !v->artifact) || (r->declaration && !v->declaration)) return false;
        if (v->artifact) {
            if (!qa_application_content_acquire(v->content,v->artifact,v->selection.artifact,&r->artifact_acquisition,error)) return false;
            v->artifact_acquisition=&r->artifact_acquisition;
            qa_resource_retain((qa_resource *)v->artifact); r->artifact_retained=true;
        }
        if (v->declaration) { qa_resource_retain((qa_resource *)v->declaration); r->declaration_retained=true; }
        r->interface_values=v->interface_count?calloc(v->interface_count,sizeof(*r->interface_values)):NULL;
        if (v->interface_count && !r->interface_values) return fail(error,QA_ERROR_MEMORY,"Restoring selected interfaces");
        for (size_t j=0;j<v->interface_count;++j) {
            content_resource *f=r->interfaces+j;
            f->value.product=current_product(g,r->catalog,f->value.product);
            f->value.resource=qa_application_content_resource(g,f->pool,f->resource);
            if (!f->value.resource || !f->value.product) return false;
            qa_resource_retain((qa_resource *)f->value.resource); f->retained=true;
            r->interface_values[j]=f->value;
        }
        v->interfaces=r->interface_values;
    }
    qa_catalog *launch=qa_application_content_catalog(g,g->launch_catalog);
    for (size_t i=0;i<g->resource_count;++i) {
        content_resource *r=g->resources+i;
        r->value.product=current_product(g,g->launch_catalog,r->value.product);
        r->value.resource=qa_application_content_resource(g,r->pool,r->resource);
        qa_vfs *view=qa_application_content_view(g,r->origin_view);
        if (!view || !r->value.product || !r->value.resource ||
            !qa_application_content_acquire(view,r->value.resource,r->value.path,&r->acquisition,error)) return false;
        qa_resource_retain((qa_resource *)r->value.resource); r->retained=true;
        if (r->origin_kind==QA_LAUNCH_ORIGIN_CATALOG) {
            if (!qa_catalog_product_acquisition_origin(launch,r->value.product,view,&r->acquisition,
                &r->origin_product,&r->catalog_mount,error)) return false;
        } else {
            content_view *v=g->views+r->origin_view-1;
            if (!v->source || v->source>g->source_count) return false;
            content_source *source=g->sources+v->source-1;
            r->source=source->value; r->origin_product=r->value.product;
            r->source_base=source->base; r->source_authority=source->authority;
            r->source.directory=copy_text(source->value.directory,error);
            r->source.home_prefix=copy_text(source->value.home_prefix,error);
            if (!r->source.directory || !r->source.home_prefix) return false;
        }
    }
    return true;
}
bool application_save_content_encode(const qa_application_content_graph *graph,qa_buffer *out,qa_error *error)
{
    if (!graph || !out || out->data || out->size) return fail(error,QA_ERROR_ARGUMENT,"Manifest encoding requires empty output");
    qa_application_content_graph *g=(qa_application_content_graph *)graph; qa_source_save_io io={0};
    bool ok=prepare_manifest(g,error) && qa_source_save_writer(&io,NULL,error) &&
        manifest_fields(&io,g) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code==QA_OK)) fail(error,QA_ERROR_FORMAT,"Invalid used-content manifest");
    return ok;
}
bool application_save_content_prepare(qa_bytes bytes,const qa_application_options *options,
    qa_application_content_graph **out,qa_error *error)
{
    if (!options || !out || *out || !bytes.data || !bytes.size)
        return fail(error,QA_ERROR_ARGUMENT,"Manifest restore requires current install settings");
    qa_application_content_graph *g=calloc(1,sizeof(*g));
    if (!g) return fail(error,QA_ERROR_MEMORY,"Allocating used-content references");
    g->ownership=1; g->restored=true;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && manifest_fields(&io,g) &&
        qa_source_save_finish(&io,NULL) && resolve_manifest(g,options,error);
    qa_source_save_dispose(&io);
    if (!ok) {
        application_save_content_destroy(g);
        if (!error || error->code==QA_OK) fail(error,QA_ERROR_FORMAT,"Invalid installed-content manifest");
        return false;
    }
    *out=g; return true;
}
