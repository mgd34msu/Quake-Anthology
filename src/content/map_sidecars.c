#include "map_sidecars_private.h"
#include <stdio.h>

bool map_sidecars_fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
static char *copy_text(const char *text, qa_error *error)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (!copy) { map_sidecars_fail(error, QA_ERROR_MEMORY, "Retaining map sidecar identity"); return NULL; }
    memcpy(copy, text, size); return copy;
}
bool map_sidecars_texture_path(qa_bytes name, char path[1040], qa_error *error)
{
    if (name.size > 1024 || (name.size && (!name.data || memchr(name.data, 0, name.size))))
        return map_sidecars_fail(error, QA_ERROR_FORMAT, "Invalid Q2 material texture identity");
    int length = snprintf(path, 1040, "textures/%.*s.mat", (int)name.size, name.data ? (const char *)name.data : "");
    if (length < 0 || length >= 1040) return map_sidecars_fail(error, QA_ERROR_FORMAT, "Q2 material identity exceeds its path bound");
    char *normalized = qa_vfs_normalize_path(path, error);
    bool okay = normalized && !strcmp(normalized, path); free(normalized);
    return okay;
}
static char *q1_path(const char *map, const char *extension, qa_error *error)
{
    size_t size = strlen(map), base = size >= 4 && !strcmp(map + size - 4, ".bsp") ? size - 4 : size;
    size_t suffix = base == size ? 0 : strlen(extension);
    char *path = malloc(base + suffix + 1);
    if (!path) { map_sidecars_fail(error, QA_ERROR_MEMORY, "Retaining Q1 sidecar path"); return NULL; }
    memcpy(path, map, base); memcpy(path + base, extension, suffix); path[base + suffix] = 0; return path;
}
static int row_order(const void *left, const void *right)
{ return strcmp(((const map_sidecar_row *)left)->value.path, ((const map_sidecar_row *)right)->value.path); }
static bool observe(qa_map_sidecars *owner, const char *path, qa_error *error)
{
    for (size_t i = 0; i < owner->count; ++i) if (!strcmp(owner->rows[i].value.path, path)) return true;
    if (owner->count == MAP_SIDECAR_LIMIT) return map_sidecars_fail(error, QA_ERROR_FORMAT, "Map sidecar inventory exceeds its bound");
    map_sidecar_row *rows = realloc(owner->rows, (owner->count + 1) * sizeof(*rows));
    if (!rows) return map_sidecars_fail(error, QA_ERROR_MEMORY, "Retaining actual sidecar observations");
    owner->rows = rows;
    map_sidecar_row *row = rows + owner->count;
    *row = (map_sidecar_row){.value = {.observation = owner->count}};
    row->value.path = copy_text(path, error);
    if (!row->value.path) return false;
    ++owner->count;
    qa_resource *resource = NULL; qa_error reason = {0};
    if (!qa_vfs_acquire_receipt(owner->view, path, &resource, &row->acquisition, &reason)) {
        if (reason.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = reason;
        return false;
    }
    row->value.resource = resource;
    row->value.acquisition = &row->acquisition;
    return true;
}
static bool has_path(const qa_map_sidecars *owner, const char *path)
{
    for (size_t i = 0; i < owner->count; ++i) if (!strcmp(owner->rows[i].value.path, path)) return true;
    return false;
}
bool map_sidecars_inventory(const qa_map_sidecars *owner, qa_error *error)
{
    if (!owner || !owner->map_path || !owner->map || (owner->count && !owner->rows)) return false;
    char *normalized = qa_vfs_normalize_path(owner->map_path, error);
    bool valid = normalized && !strcmp(normalized, owner->map_path); free(normalized);
    if (!valid) return false;
    qa_bsp_view bsp;
    if (!qa_bsp_open(qa_resource_bytes(owner->map), &bsp, error) || bsp.family != owner->family) return false;
    size_t expected = 0;
    if (bsp.family == QA_BSP_Q1) {
        char *ent = q1_path(owner->map_path, ".ent", error), *lit = q1_path(owner->map_path, ".lit", error);
        bool okay = ent && lit && has_path(owner, ent) && has_path(owner, lit);
        expected = ent && lit && !strcmp(ent, lit) ? 1 : 2;
        const qa_resource *light = NULL;
        for (size_t i = 0; lit && i < owner->count; ++i) if (!strcmp(owner->rows[i].value.path, lit)) light = owner->rows[i].value.resource;
        qa_bsp_lighting lighting;
        if (okay && light) okay = qa_resource_bytes(light).size >= 8 && qa_bsp_select_lighting(&bsp, qa_resource_bytes(light), &lighting, error);
        free(ent); free(lit);
        if (!okay) return map_sidecars_fail(error, QA_ERROR_FORMAT, "Q1 sidecar inventory lacks valid entity or lighting observations");
    } else if (bsp.family == QA_BSP_Q2) {
        for (size_t i = 0; i < qa_bsp_record_count(&bsp, QA_BSP_TEXINFO); ++i) {
            qa_bsp_texinfo texture; char path[1040];
            if (!qa_bsp_read_texinfo(&bsp, i, &texture, error) || !map_sidecars_texture_path(texture.name, path, error) || !has_path(owner, path)) return false;
        }
        for (size_t i = 0; i < owner->count; ++i) {
            bool used = false;
            for (size_t j = 0; !used && j < qa_bsp_record_count(&bsp, QA_BSP_TEXINFO); ++j) {
                qa_bsp_texinfo texture; char path[1040];
                if (!qa_bsp_read_texinfo(&bsp, j, &texture, error) || !map_sidecars_texture_path(texture.name, path, error)) return false;
                used = !strcmp(path, owner->rows[i].value.path);
            }
            if (!used) return map_sidecars_fail(error, QA_ERROR_FORMAT, "Q2 sidecar observation has no actual map texture");
            ++expected;
        }
    }
    if (expected != owner->count) return map_sidecars_fail(error, QA_ERROR_FORMAT, "Map sidecar observation inventory differs from the actual map");
    for (size_t i = 0; i < owner->count; ++i) {
        if (owner->rows[i].value.observation >= owner->count ||
            (i && strcmp(owner->rows[i - 1].value.path, owner->rows[i].value.path) >= 0)) return false;
        for (size_t j = 0; j < i; ++j) if (owner->rows[j].value.observation == owner->rows[i].value.observation) return false;
    }
    return true;
}
bool qa_map_sidecars_create(qa_catalog *catalog, qa_product_id product, const char *path,
    qa_resource_pool *map_pool, qa_resource *map, qa_map_sidecars **out, qa_error *error)
{
    if (!catalog || !product || !path || !map_pool || !map || !out || *out ||
        !qa_catalog_product(catalog, product) || qa_resource_pool_find(map_pool, qa_resource_id(map)) != map)
        return map_sidecars_fail(error, QA_ERROR_ARGUMENT, "Map sidecar admission requires its actual catalog and immutable map owner");
    char *normalized = qa_vfs_normalize_path(path, error);
    bool okay = normalized && !strcmp(normalized, path); free(normalized);
    if (!okay) return false;
    qa_map_sidecars *owner = calloc(1, sizeof(*owner));
    if (!owner) return map_sidecars_fail(error, QA_ERROR_MEMORY, "Allocating map sidecar owner");
    owner->references = 1; owner->catalog = catalog; owner->product = product;
    qa_catalog_retain(catalog); owner->map_pool = map_pool; qa_resource_pool_retain(map_pool);
    owner->map = map; qa_resource_retain(map); owner->map_path = copy_text(path, error);
    qa_bsp_view bsp;
    okay = owner->map_path && qa_catalog_open(catalog, product, &owner->view, error) && qa_bsp_open(qa_resource_bytes(map), &bsp, error);
    if (okay) owner->family = bsp.family;
    if (okay && bsp.family == QA_BSP_Q1) {
        char *ent = q1_path(path, ".ent", error), *lit = q1_path(path, ".lit", error);
        okay = ent && lit && observe(owner, ent, error) && observe(owner, lit, error);
        free(ent); free(lit);
    } else if (okay && bsp.family == QA_BSP_Q2) {
        for (size_t i = 0; okay && i < qa_bsp_record_count(&bsp, QA_BSP_TEXINFO); ++i) {
            qa_bsp_texinfo texture; char material[1040];
            okay = qa_bsp_read_texinfo(&bsp, i, &texture, error) && map_sidecars_texture_path(texture.name, material, error) && observe(owner, material, error);
        }
    }
    if (okay) {
        if (owner->count) qsort(owner->rows, owner->count, sizeof(*owner->rows), row_order);
        for (size_t i = 0; i < owner->count; ++i) if (owner->rows[i].value.resource) owner->rows[i].value.acquisition = &owner->rows[i].acquisition;
        okay = map_sidecars_inventory(owner, error) && qa_map_sidecars_current(owner);
    }
    if (!okay) { qa_map_sidecars_release(owner); return false; }
    *out = owner; return true;
}
void qa_map_sidecars_retain(qa_map_sidecars *owner) { if (owner) ++owner->references; }
void qa_map_sidecars_release(qa_map_sidecars *owner)
{
    if (!owner || --owner->references) return;
    for (size_t i = 0; owner->rows && i < owner->count; ++i) {
        free((char *)owner->rows[i].value.path); qa_resource_release((qa_resource *)owner->rows[i].value.resource);
        qa_vfs_acquisition_dispose(&owner->rows[i].acquisition);
    }
    free(owner->rows); free(owner->map_path); qa_resource_release(owner->map);
    qa_vfs_destroy(owner->view); qa_catalog_release(owner->catalog); qa_resource_pool_destroy(owner->map_pool); free(owner);
}
bool qa_map_sidecars_current(const qa_map_sidecars *owner)
{
    if (!owner || !owner->references || !owner->catalog || !owner->view || !owner->map ||
        !qa_catalog_product_view_current(owner->catalog, owner->product, owner->view) ||
        qa_resource_pool_find(owner->map_pool, qa_resource_id(owner->map)) != owner->map) return false;
    qa_resource_pool *pool = qa_vfs_resources(owner->view);
    for (size_t i = 0; i < owner->count; ++i) {
        const map_sidecar_row *row = owner->rows + i;
        if (row->value.resource && (row->value.acquisition != &row->acquisition ||
            qa_resource_pool_find(pool, qa_resource_id(row->value.resource)) != row->value.resource ||
            row->acquisition.resource_id != qa_resource_id(row->value.resource) ||
            !row->acquisition.opening_present || !row->acquisition.path || !row->value.path ||
            strcmp(row->value.path, row->acquisition.path) ||
            !qa_vfs_acquisition_retained(owner->view, &row->acquisition, NULL))) return false;
        if (row->value.resource) {
            const qa_vfs_acquisition *receipt = &row->acquisition;
            if (receipt->opening.prefix || receipt->opening.user_overlay || receipt->opening.rank < 0 ||
                receipt->opening.order_count != qa_vfs_mount_count(owner->view) ||
                (receipt->opening.order_count && !receipt->opening.order) ||
                *receipt->link_source || *receipt->link_target || strcmp(receipt->path, receipt->lookup_path)) return false;
            for (size_t j = 0; j < receipt->opening.order_count; ++j) {
                qa_vfs_mount_info mount;
                if (!qa_vfs_mount_at(owner->view, j, &mount) || receipt->opening.order[j] != mount.id ||
                    ((uint64_t)receipt->opening.rank == j && receipt->mount != mount.id)) return false;
            }
            if ((uint64_t)receipt->opening.rank >= receipt->opening.order_count) return false;
        }
        if (!row->value.resource && (row->value.acquisition || row->acquisition.path)) return false;
    }
    return true;
}
qa_catalog *qa_map_sidecars_catalog(const qa_map_sidecars *owner) { return owner ? owner->catalog : NULL; }
qa_product_id qa_map_sidecars_product(const qa_map_sidecars *owner) { return owner ? owner->product : QA_PRODUCT_NONE; }
const char *qa_map_sidecars_map_path(const qa_map_sidecars *owner) { return owner ? owner->map_path : NULL; }
qa_resource *qa_map_sidecars_map(const qa_map_sidecars *owner) { return owner ? owner->map : NULL; }
const qa_vfs *qa_map_sidecars_view(const qa_map_sidecars *owner) { return owner ? owner->view : NULL; }
size_t qa_map_sidecars_count(const qa_map_sidecars *owner) { return owner ? owner->count : 0; }
const qa_map_sidecar *qa_map_sidecars_at(const qa_map_sidecars *owner, size_t index) { return owner && index < owner->count ? &owner->rows[index].value : NULL; }
static const qa_resource *extension_resource(const qa_map_sidecars *owner, const char *extension)
{
    if (!owner || owner->family != QA_BSP_Q1) return NULL;
    size_t length = strlen(owner->map_path);
    size_t base = length >= 4 && !strcmp(owner->map_path + length - 4, ".bsp") ? length - 4 : length;
    for (size_t i = 0; i < owner->count; ++i) {
        const char *path = owner->rows[i].value.path;
        if (base == length ? !strcmp(path, owner->map_path) :
            strlen(path) == base + strlen(extension) && !strncmp(path, owner->map_path, base) && !strcmp(path + base, extension))
            return owner->rows[i].value.resource;
    }
    return NULL;
}
bool qa_map_sidecars_apply_entities(const qa_map_sidecars *owner, qa_bsp_view *bsp, qa_error *error)
{
    qa_bytes bytes = owner ? qa_resource_bytes(owner->map) : (qa_bytes){0};
    if (!bsp || !qa_map_sidecars_current(owner) || bsp->source.data != bytes.data || bsp->source.size != bytes.size)
        return map_sidecars_fail(error, QA_ERROR_ARGUMENT, "Entity sidecars require the exact held map view");
    const qa_resource *resource = bsp->family == QA_BSP_Q1 ? extension_resource(owner, ".ent") : NULL;
    if (resource) bsp->lumps[QA_BSP_ENTITIES] = (qa_bsp_lump){.bytes = qa_resource_bytes(resource), .present = true};
    return true;
}
qa_bytes qa_map_sidecars_external_lit(const qa_map_sidecars *owner)
{ const qa_resource *resource = extension_resource(owner, ".lit"); return resource ? qa_resource_bytes(resource) : (qa_bytes){0}; }
bool qa_map_sidecars_external_entities(const qa_map_sidecars *owner, qa_bytes *out)
{
    const qa_resource *resource = extension_resource(owner, ".ent");
    if (out) *out = resource ? qa_resource_bytes(resource) : (qa_bytes){0};
    return resource != NULL;
}
static bool material_name_equal(qa_bytes left, qa_bytes right)
{
    if (left.size > 31) left.size = 31;
    if (right.size > 31) right.size = 31;
    if (left.size != right.size) return false;
    for (size_t i = 0; i < left.size; ++i) {
        uint8_t a = left.data[i], b = right.data[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
    }
    return true;
}
bool qa_map_sidecars_material_path(const qa_bsp_view *bsp, size_t index, char path[1040], qa_error *error)
{
    qa_bsp_texinfo texture;
    if (!bsp || bsp->family != QA_BSP_Q2 || !path || !qa_bsp_read_texinfo(bsp, index, &texture, error)) return false;
    for (size_t i = 0; i < index; ++i) {
        qa_bsp_texinfo earlier;
        if (!qa_bsp_read_texinfo(bsp, i, &earlier, error)) return false;
        if (material_name_equal(earlier.name, texture.name)) { texture = earlier; break; }
    }
    if (texture.name.size > 31) texture.name.size = 31;
    return map_sidecars_texture_path(texture.name, path, error);
}
qa_bytes qa_map_sidecars_material_input(qa_bytes input)
{
    if (input.size && !input.data) return (qa_bytes){0};
    size_t count = input.size < 15 ? input.size : 15, length = 0;
    while (length < count && input.data[length]) {
        uint8_t value = input.data[length];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '_' || value == '-')) return (qa_bytes){0};
        ++length;
    }
    return (qa_bytes){input.data, length};
}
bool qa_map_sidecars_apply_materials(const qa_map_sidecars *owner, qa_collision_geometry *geometry, qa_strings *strings, qa_error *error)
{
    const qa_bsp_view *bsp = qa_collision_bsp(geometry); qa_bytes bytes = owner ? qa_resource_bytes(owner->map) : (qa_bytes){0};
    if (!bsp || !qa_map_sidecars_current(owner) || bsp->source.data != bytes.data || bsp->source.size != bytes.size)
        return map_sidecars_fail(error, QA_ERROR_ARGUMENT, "Material sidecars require the exact retained collision map");
    if (bsp->family != QA_BSP_Q2) return true;
    for (size_t i = 0; i < qa_bsp_record_count(bsp, QA_BSP_TEXINFO); ++i) {
        char path[1040];
        if (!qa_map_sidecars_material_path(bsp, i, path, error)) return false;
        for (size_t j = 0; j < owner->count; ++j) if (!strcmp(path, owner->rows[j].value.path) && owner->rows[j].value.resource)
            if (i > UINT32_MAX || !qa_collision_set_surface_material(geometry, (uint32_t)i,
                qa_map_sidecars_material_input(qa_resource_bytes(owner->rows[j].value.resource)), strings, error)) return false;
    }
    return true;
}
bool qa_map_sidecars_content_visit(const qa_map_sidecars *owner, const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!qa_map_sidecars_current(owner) || !visitor || !visitor->pool || !visitor->catalog || !visitor->view)
        return map_sidecars_fail(error, QA_ERROR_ARGUMENT, "Sidecar graph requires its retained admission owner");
    return visitor->pool(visitor->context, owner->map_pool, error) && visitor->catalog(visitor->context, owner->catalog, error) &&
        visitor->view(visitor->context, owner->view, error) && qa_map_sidecars_current(owner);
}
