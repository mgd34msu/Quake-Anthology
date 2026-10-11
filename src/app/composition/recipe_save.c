#include "recipe_private.h"
#include "qa/executable_recipe_save.h"
#include "qa/map_sidecars.h"
#include "qa/source_save.h"
#include "qa/persistence_fields.h"

static bool text(qa_source_save_io *io, qa_executable_recipe *recipe, const char **value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t size = reading ? 0 : *value ? strlen(*value) : 0;
    if (!qa_source_save_count(io, &size, RECIPE_MAX_BYTES)) return false;
    if (reading) {
        if (size > io->input.size - io->offset) return false;
        char *owned = qa_arena_alloc(&recipe->arena, size + 1, 1, io->error);
        if (!owned) return false;
        if (!qa_source_save_bytes(io, owned, size) || memchr(owned, 0, size)) return false;
        owned[size] = 0; *value = owned; return true;
    }
    return (!size || *value) && qa_source_save_bytes(io, (void *)*value, size);
}
static bool receipt_text(qa_source_save_io *io, char **value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t size = reading ? 0 : *value ? strlen(*value) : 0;
    if (!qa_source_save_count(io, &size, RECIPE_MAX_BYTES)) return false;
    if (reading) {
        if (size > io->input.size - io->offset) return false;
        *value = malloc(size + 1);
        if (!*value) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring recipe acquisition strings"); return false; }
        if (!qa_source_save_bytes(io, *value, size) || memchr(*value, 0, size)) return false;
        (*value)[size] = 0; return true;
    }
    return (!size || *value) && qa_source_save_bytes(io, *value, size);
}
static bool buffer(qa_source_save_io *io, qa_buffer *value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t size = value->size;
    if (!qa_source_save_count(io, &size, RECIPE_MAX_BYTES)) return false;
    if (reading) {
        if (size > io->input.size - io->offset) return false;
        value->data = size ? malloc(size) : NULL; value->size = size;
        if (size && !value->data) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring canonical recipe bytes"); return false; }
    }
    return qa_source_save_bytes(io, value->data, size);
}
static bool acquisition(qa_source_save_io *io, qa_vfs *files, qa_vfs_acquisition *value)
{
    return qa_source_save_u64(io, &value->mount) && qa_source_save_u64(io, &value->resource_id) &&
        receipt_text(io, &value->path) && receipt_text(io, &value->lookup_path) &&
        receipt_text(io, &value->link_source) && receipt_text(io, &value->link_target) &&
        qa_vfs_acquisition_opening_codec(io, files, value) && value->opening_present &&
        qa_vfs_acquisition_retained(files, value, io->error);
}
static bool portal_fields(qa_source_save_io *io, qa_collision_portal_checkpoint *state)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t family = qa_persistence_family_tag(state->family), format = state->format;
    if (!qa_source_save_u32(io, &family) || family > 3u ||
        !qa_source_save_u32(io, &format) || !qa_source_save_u64(io, &state->map_identity) ||
        !qa_source_save_u32(io, &state->area_count) || !qa_source_save_bool(io, &state->no_areas) ||
        !qa_source_save_count(io, &state->portal_count, reading ?
            (io->input.size - io->offset) / 9 : SIZE_MAX / sizeof(*state->portals))) return false;
    state->family = qa_persistence_family_from_tag(family); state->format = (qa_bsp_format)format;
    if (reading && state->portal_count) {
        state->portals = calloc(state->portal_count, sizeof(*state->portals));
        if (!state->portals) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring recipe portal contributions"); return false; }
    }
    for (size_t i = 0; i < state->portal_count; ++i)
        if (!qa_source_save_u32(io, &state->portals[i].portal) ||
            !qa_source_save_u32(io, &state->portals[i].contributions) ||
            !qa_source_save_bool(io, &state->portals[i].primary) ||
            (i && state->portals[i - 1].portal >= state->portals[i].portal)) return false;
    if (!qa_source_save_count(io, &state->area_pair_count, reading ?
        (io->input.size - io->offset) / sizeof(uint32_t) : SIZE_MAX / sizeof(uint32_t))) return false;
    if (reading && state->area_pair_count) {
        state->area_pairs = calloc(state->area_pair_count, sizeof(*state->area_pairs));
        if (!state->area_pairs) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring recipe area-pair contributions"); return false; }
    }
    for (size_t i = 0; i < state->area_pair_count; ++i)
        if (!qa_source_save_u32(io, state->area_pairs + i)) return false;
    return true;
}
static bool graph_fields(qa_source_save_io *io, qa_executable_recipe *recipe,
    qa_application_content_graph *graph, qa_collision_portal_checkpoint *portals)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','E','R','C'};
    uint64_t catalog = reading ? 0 : qa_application_content_catalog_id(graph, recipe->catalog);
    uint64_t pool = reading ? 0 : qa_application_content_pool_id(graph, recipe->pool);
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QERC", sizeof(magic)) ||
        !qa_source_save_u64(io, &catalog) || !catalog ||
        !qa_source_save_u64(io, &pool) || !pool || !qa_source_save_u64(io, &recipe->catalog_generation)) return false;
    if (reading && (!qa_application_content_retain_catalog(graph, catalog, &recipe->catalog, io->error) ||
        !qa_application_content_retain_pool(graph, pool, &recipe->pool, io->error))) return false;
    if (qa_catalog_generation(recipe->catalog) != recipe->catalog_generation ||
        qa_catalog_resources(recipe->catalog) != recipe->pool) return false;
    if (!qa_source_save_u32(io, &recipe->epoch) || !recipe->epoch ||
        !qa_source_save_u32(io, &recipe->max_clients) || !recipe->max_clients || recipe->max_clients > 256 ||
        !text(io, recipe, &recipe->mode) || (strcmp(recipe->mode, "singleplayer") &&
            strcmp(recipe->mode, "coop") && strcmp(recipe->mode, "deathmatch")) ||
        !qa_source_save_u64(io, &recipe->generation) || !recipe->generation ||
        !buffer(io, &recipe->composition) || !recipe->composition.size) return false;
    qa_unified_composition canonical = {0};
    bool matched = qa_unified_composition_create((qa_bytes){recipe->composition.data, recipe->composition.size},
        &canonical, io->error) &&
        canonical.canonical.size == recipe->composition.size &&
        !memcmp(canonical.canonical.data, recipe->composition.data, recipe->composition.size);
    qa_unified_composition_free(&canonical);
    if (!matched) return false;
    size_t views = reading ? 0 : recipe->view_count;
    if (!qa_source_save_count(io, &views, RECIPE_MAX_RECORDS) || !views ||
        (reading && views > (io->input.size - io->offset) / 33)) return false;
    if (reading) {
        recipe->views = calloc(views, sizeof(*recipe->views));
        if (!recipe->views) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring recipe private view owners"); return false; }
    }
    for (size_t i = 0; i < views; ++i) {
        recipe_view *view = recipe->views + i;
        if (reading) recipe->view_count = i + 1;
        uint64_t owner_catalog = reading ? 0 : qa_application_content_catalog_id(graph, view->catalog);
        uint64_t files = reading ? 0 : qa_application_content_view_id(graph, view->files);
        uint64_t policy = reading ? 0 : qa_application_content_view_id(graph, view->admitted_policy);
        if (!text(io, recipe, &view->owner) || !*view->owner ||
            !qa_source_save_u64(io, &owner_catalog) || !owner_catalog ||
            !qa_source_save_u64(io, &files) || !files || !qa_source_save_u64(io, &policy) || !policy ||
            files == policy || !qa_source_save_u32(io, &view->product)) return false;
        if (reading) {
            qa_catalog *actual = NULL;
            view->kind = recipe_view_owner_kind(view->owner);
            if (!qa_application_content_retain_catalog(graph, owner_catalog, &actual, io->error)) return false;
            view->catalog = actual; view->owns_files = true;
            if (!qa_application_content_claim_view(graph, files, &view->files, io->error) ||
                !qa_application_content_claim_view(graph, policy, &view->admitted_policy, io->error)) return false;
        } else if (!view->owns_files) return false;
        if (view->catalog != recipe->catalog || qa_vfs_resources(view->files) != recipe->pool ||
            qa_vfs_resources(view->admitted_policy) != recipe->pool ||
            !qa_vfs_lookup_equal(view->files, view->admitted_policy) ||
            (view->product && !qa_catalog_product(recipe->catalog, view->product))) return false;
        for (size_t j = 0; j < i; ++j) if (!strcmp(recipe->views[j].owner, view->owner)) return false;
    }
    if (strcmp(recipe->views[0].owner, "main")) return false;
    size_t resources = reading ? 0 : recipe->resource_count;
    if (!qa_source_save_count(io, &resources, RECIPE_MAX_RECORDS) || !resources ||
        (reading && resources > (io->input.size - io->offset) / 52)) return false;
    if (reading) {
        recipe->resources = calloc(resources, sizeof(*recipe->resources));
        if (!recipe->resources) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring immutable recipe resource owners"); return false; }
    }
    for (size_t i = 0; i < resources; ++i) {
        recipe_resource *entry = reading ? calloc(1, sizeof(*entry)) : recipe->resources[i];
        if (!entry) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring retained recipe resource"); return false; }
        if (reading) { recipe->resources[i] = entry; recipe->resource_count = i + 1; }
        uint64_t resource_pool = 0, resource = 0;
        if (!reading && !qa_application_content_resource_id(graph, entry->value.resource,
            &resource_pool, &resource)) return false;
        if (!qa_source_save_u32(io, &entry->value.product) ||
            !qa_catalog_product(recipe->catalog, entry->value.product) ||
            !text(io, recipe, &entry->value.path) || !recipe_path(entry->value.path, io->error) ||
            !qa_source_save_count(io, &entry->view, views - 1) ||
            !qa_source_save_u64(io, &resource_pool) || resource_pool != pool ||
            !qa_source_save_u64(io, &resource) || !resource) return false;
        if (reading) {
            entry->value.resource = qa_application_content_resource(graph, resource_pool, resource);
            if (!entry->value.resource) return false;
            qa_resource_retain((qa_resource *)entry->value.resource); entry->owns_resource = true;
        }
        if (!acquisition(io, recipe->views[entry->view].files, &entry->acquisition) ||
            qa_resource_pool_find(recipe->pool, entry->acquisition.resource_id) != entry->value.resource ||
            strcmp(entry->value.path, entry->acquisition.path)) return false;
    }
    return qa_source_save_count(io, &recipe->map_index, resources - 1) && portal_fields(io, portals);
}
static bool geometry_restore(qa_executable_recipe *recipe,
    const qa_collision_portal_checkpoint *portals, qa_error *error)
{
    const qa_resource *map = recipe->resources[recipe->map_index]->value.resource;
    if (!qa_bsp_open(qa_resource_bytes(map), &recipe->bsp, error) ||
        !recipe_metadata_restore(recipe, error) || !qa_collision_create(&recipe->bsp, &recipe->geometry, error) ||
        !qa_collision_bind_resource(recipe->geometry, (qa_resource *)map, error)) return false;
    if (recipe->bsp.family == QA_BSP_Q2)
        for (size_t i = 0; i < qa_bsp_record_count(&recipe->bsp, QA_BSP_TEXINFO); ++i) {
            char path[1040];
            if (!qa_map_sidecars_material_path(&recipe->bsp, i, path, error)) return false;
            for (size_t j = 0; j < recipe->sidecar_count; ++j)
                if (recipe->sidecars[j].resource && !strcmp(recipe->sidecars[j].path, path) &&
                    (i > UINT32_MAX || !qa_collision_set_surface_material(recipe->geometry, (uint32_t)i,
                        qa_map_sidecars_material_input(qa_resource_bytes(recipe->sidecars[j].resource)), error))) return false;
        }
    return qa_collision_restore_portals(recipe->geometry, portals, error);
}
bool qa_executable_recipe_checkpoint(const qa_executable_recipe *source,
    qa_application_content_graph *graph, qa_buffer *out, qa_error *error)
{
    if (!source || source->visiting || !graph || !out || out->data || out->size ||
        !qa_executable_recipe_current(source, source->catalog))
        return recipe_fail(error, "Recipe capture requires its actual idle retained content graph");
    qa_executable_recipe *recipe = (qa_executable_recipe *)source;
    recipe->visiting = true;
    qa_source_save_io io = {0}; qa_collision_portal_checkpoint portals = {0};
    bool okay = qa_collision_capture_portals(recipe->geometry, &portals, error) &&
        qa_source_save_writer(&io, NULL, error) && graph_fields(&io, recipe, graph, &portals) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_collision_portal_checkpoint_free(&portals); recipe->visiting = false;
    if (!okay && (!error || error->code == QA_OK)) recipe_fail(error, "Invalid retained executable recipe graph");
    return okay;
}
bool qa_executable_recipe_restore(qa_application_content_graph *graph, qa_bytes bytes,
    qa_strings *strings, qa_executable_recipe **out, qa_error *error)
{
    if (!graph || !strings || !out || *out) return recipe_fail(error, "Recipe import requires its actual restored graph and empty output");
    qa_executable_recipe *recipe = calloc(1, sizeof(*recipe));
    if (!recipe) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring executable recipe owner"); return false; }
    recipe->strings = strings; qa_strings_retain(strings);
    qa_source_save_io io = {0}; qa_collision_portal_checkpoint portals = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && graph_fields(&io, recipe, graph, &portals) &&
        qa_source_save_finish(&io, NULL) && geometry_restore(recipe, &portals, error) &&
        qa_executable_recipe_current(recipe, recipe->catalog);
    qa_source_save_dispose(&io); qa_collision_portal_checkpoint_free(&portals);
    if (!okay) {
        (void)qa_executable_recipe_close(recipe, NULL);
        if (!error || error->code == QA_OK) recipe_fail(error, "Invalid saved executable recipe ownership");
        return false;
    }
    *out = recipe; return true;
}
