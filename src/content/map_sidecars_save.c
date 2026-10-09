#include "map_sidecars_private.h"

static bool text(qa_source_save_io *io, char **value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t length = reading ? 0 : *value ? strlen(*value) : 0;
    if ((!reading && !*value) || !qa_source_save_count(io, &length, 65536) || length == SIZE_MAX) return false;
    if (reading) {
        *value = malloc(length + 1);
        if (!*value) return map_sidecars_fail(io->error, QA_ERROR_MEMORY, "Restoring map sidecar text");
    }
    if (!qa_source_save_bytes(io, *value, length) || memchr(*value, 0, length)) return false;
    if (reading) (*value)[length] = 0;
    return true;
}
static bool resource(qa_source_save_io *io, const qa_application_content_graph *graph,
    qa_resource_pool *expected_pool, const qa_resource **value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t pool = 0, id = 0;
    if (!reading && !qa_application_content_resource_id(graph, *value, &pool, &id)) return false;
    if (!qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &id) || !pool || !id) return false;
    qa_resource_pool *actual = qa_application_content_pool(graph, pool);
    const qa_resource *held = qa_application_content_resource(graph, pool, id);
    if (!held || (expected_pool && actual != expected_pool) || qa_resource_pool_find(actual, qa_resource_id(held)) != held) return false;
    if (reading) { qa_resource_retain((qa_resource *)held); *value = held; }
    return reading || held == *value;
}
static bool fields(qa_source_save_io *io, qa_application_content_graph *graph, qa_map_sidecars *owner)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t magic = UINT32_C(0x43534d51);
    uint64_t catalog = reading ? 0 : qa_application_content_catalog_id(graph, owner->catalog);
    uint64_t view = reading ? 0 : qa_application_content_view_id(graph, owner->view);
    uint64_t pool = reading ? 0 : qa_application_content_pool_id(graph, owner->map_pool);
    if (!qa_source_save_u32(io, &magic) || magic != UINT32_C(0x43534d51) ||
        !qa_source_save_u64(io, &catalog) || !catalog || !qa_source_save_u64(io, &view) || !view ||
        !qa_source_save_u64(io, &pool) || !pool) return false;
    if (reading) {
        if (!qa_application_content_retain_catalog(graph, catalog, &owner->catalog, io->error) ||
            !qa_application_content_claim_view(graph, view, &owner->view, io->error)) return false;
        owner->map_pool = qa_application_content_pool(graph, pool);
        if (!owner->map_pool) return false;
        qa_resource_pool_retain(owner->map_pool);
    }
    const qa_product *product = reading ? NULL : qa_catalog_product(owner->catalog, owner->product);
    char *identity = reading ? NULL : product ? (char *)product->identity : NULL;
    if (!text(io, &identity)) { if (reading) free(identity); return false; }
    if (reading) {
        product = qa_catalog_find(owner->catalog, identity); free(identity);
        if (!product) return false;
        owner->product = product->id;
    }
    uint32_t family = reading ? 0 : owner->family;
    if (!text(io, &owner->map_path) ||
        !resource(io, graph, owner->map_pool, (const qa_resource **)&owner->map) ||
        !qa_source_save_u32(io, &family) || family < QA_BSP_Q1 || family > QA_BSP_Q3 ||
        !qa_source_save_count(io, &owner->count, MAP_SIDECAR_LIMIT)) return false;
    if (reading) owner->family = (qa_bsp_family)family;
    if (reading && owner->count) {
        owner->rows = calloc(owner->count, sizeof(*owner->rows));
        if (!owner->rows) return map_sidecars_fail(io->error, QA_ERROR_MEMORY, "Restoring map sidecar inventory");
    }
    qa_resource_pool *sidecar_pool = qa_vfs_resources(owner->view);
    for (size_t i = 0; i < owner->count; ++i) {
        map_sidecar_row *row = owner->rows + i;
        bool found = reading ? false : row->value.resource != NULL;
        if (!text(io, (char **)&row->value.path) || !qa_source_save_count(io, &row->value.observation, MAP_SIDECAR_LIMIT) ||
            !qa_source_save_bool(io, &found)) return false;
        if (!found) continue;
        qa_vfs_acquisition *receipt = &row->acquisition;
        if (!resource(io, graph, sidecar_pool, &row->value.resource) ||
            !qa_source_save_u64(io, &receipt->mount) || !qa_source_save_u64(io, &receipt->resource_id) ||
            !text(io, &receipt->path) || !text(io, &receipt->lookup_path) ||
            !text(io, &receipt->link_source) || !text(io, &receipt->link_target) ||
            !qa_vfs_acquisition_opening_codec(io, owner->view, receipt) || !receipt->opening_present) return false;
        if (reading) {
            receipt->resource_id = qa_resource_id(row->value.resource);
            row->value.acquisition = receipt;
        }
    }
    return map_sidecars_inventory(owner, io->error) && qa_map_sidecars_current(owner);
}
bool qa_map_sidecars_checkpoint(const qa_map_sidecars *owner, const qa_application_content_graph *graph,
    qa_buffer *out, qa_error *error)
{
    if (!qa_map_sidecars_current(owner) || !graph || !out || out->data || out->size)
        return map_sidecars_fail(error, QA_ERROR_ARGUMENT, "Sidecar checkpoint requires its actual content graph");
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) && fields(&io, (qa_application_content_graph *)graph, (qa_map_sidecars *)owner) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!okay && (!error || error->code == QA_OK)) map_sidecars_fail(error, QA_ERROR_FORMAT, "Map sidecar checkpoint disagrees with its retained owners");
    return okay;
}
bool qa_map_sidecars_create_restored(qa_application_content_graph *graph, qa_bytes bytes,
    qa_map_sidecars **out, qa_error *error)
{
    if (!graph || !out || *out) return map_sidecars_fail(error, QA_ERROR_ARGUMENT, "Restored map sidecars require an isolated content graph");
    qa_map_sidecars *owner = calloc(1, sizeof(*owner));
    if (!owner) return map_sidecars_fail(error, QA_ERROR_MEMORY, "Restoring map sidecar owner");
    owner->references = 1;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, graph, owner) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!okay) {
        qa_map_sidecars_release(owner);
        if (!error || error->code == QA_OK) map_sidecars_fail(error, QA_ERROR_FORMAT, "Invalid retained map sidecar admission");
        return false;
    }
    *out = owner; return true;
}
