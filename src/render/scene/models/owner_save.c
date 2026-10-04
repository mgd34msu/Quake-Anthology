#include "internal.h"
#include "qa/scene_model_save.h"
#include "qa/material_library_save.h"
#include "qa/scene_save.h"
#include "qa/source_save.h"
#include "../image_options_save.h"

typedef struct model_node {
    qa_scene_model *model;
    size_t parent;
} model_node;
typedef struct model_inventory {
    model_node *nodes;
    size_t count, capacity;
    qa_scene_model_saved_identity *identities;
    size_t identity_count;
} model_inventory;

static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
struct qa_scene_model_capture { qa_scene_model *root; };
static bool model_idle(const qa_scene_model *model, bool include_capture)
{
    if (!model) return false;
    while (model->replacement_parent) model = model->replacement_parent;
    const qa_scene_model *root = model;
    for (;;) {
        if (model->active_submissions || model->checkpoint_active || (include_capture && model->capture)) return false;
        if (model->replacement) { model = model->replacement; continue; }
        while (model != root && !model->replacement_next) model = model->replacement_parent;
        if (model == root) return true;
        model = model->replacement_next;
    }
}
bool qa_scene_model_idle(const qa_scene_model *model) { return model_idle(model,true); }
bool qa_scene_model_observation_ready(const qa_scene_model *model) { return model_idle(model,false); }
bool qa_scene_model_capture_begin(const qa_scene_model *model, qa_scene_model_capture **out, qa_error *error)
{
    if (!model || !out || *out || model->replacement_parent || model->replacement_next || !qa_scene_model_idle(model))
        return fail(error,QA_ERROR_ARGUMENT,"Model aggregate capture requires an idle owned root and empty token");
    qa_scene_model_capture *capture=malloc(sizeof(*capture));
    if (!capture) return fail(error,QA_ERROR_MEMORY,"Retaining the model owner capture lease");
    capture->root=(qa_scene_model *)model; capture->root->capture=capture; *out=capture; return true;
}
void qa_scene_model_capture_end(qa_scene_model_capture *capture)
{
    if (!capture) return;
    if (capture->root->capture==capture) capture->root->capture=NULL;
    free(capture);
}
const qa_model *qa_scene_model_source(const qa_scene_model *model) { return model ? model->source : NULL; }
const qa_scene_image_options *qa_scene_model_image_options(const qa_scene_model *model) { return model ? &model->options : NULL; }
qa_scene_resources *qa_scene_model_resource_owner(const qa_scene_model *model) { return model ? model->resources : NULL; }
qa_material_library *qa_scene_model_material_owner(const qa_scene_model *model) { return model ? model->materials : NULL; }
bool qa_scene_model_content_read(const qa_scene_model *model, qa_scene_model_content_kind kind,
    qa_scene_model_content_lease *out)
{
    if (!model || !out || !qa_scene_model_observation_ready(model)) return false;
    qa_scene_model_content_lease lease;
    switch (kind) {
    case QA_SCENE_MODEL_CONTENT_SOURCE: lease=model->source_lease; break;
    case QA_SCENE_MODEL_CONTENT_REPLACEMENT_SOURCE: lease=model->replacement_source_lease; break;
    case QA_SCENE_MODEL_CONTENT_ANIMATION: lease=model->animation_lease; break;
    default: return false;
    }
    if ((lease.context!=NULL)!=(lease.release!=NULL)) return false;
    *out=lease; return true;
}
const qa_scene_mesh *qa_scene_model_mesh_at(const qa_scene_model *model, size_t index)
{ return model && index < model->source->mesh_count ? &model->meshes[index].retained : NULL; }
const qa_scene_model *qa_scene_model_replacement_first(const qa_scene_model *model) { return model ? model->replacement : NULL; }
const qa_scene_model *qa_scene_model_replacement_next(const qa_scene_model *model) { return model ? model->replacement_next : NULL; }
const qa_model_replacement *qa_scene_model_replacement_description(const qa_scene_model *model) { return model ? model->replacement_source : NULL; }
uint64_t qa_scene_model_identity(const qa_scene_model *model) { return model ? model->identity : 0; }
size_t qa_scene_model_shadow_identity_count(const qa_scene_model *model)
{
    size_t count = 0;
    if (model) for (const scene_model_shadow_identity *entry = model->shadow_identities; entry; entry = entry->next) ++count;
    return count;
}
bool qa_scene_model_shadow_identity_at(const qa_scene_model *model, size_t index, uint32_t *entity, uint64_t *identity)
{
    if (!model || !entity || !identity) return false;
    const scene_model_shadow_identity *entry = model->shadow_identities;
    while (entry && index) { entry = entry->next; --index; }
    if (!entry) return false;
    *entity = entry->entity; *identity = entry->identity; return true;
}
static bool refs_ready(const qa_scene_model_owner_refs *refs)
{
    return refs && refs->model_encode && refs->model_decode &&
        refs->source_qualify && refs->geometry_encode && refs->geometry_decode && refs->image_encode && refs->image_decode &&
        refs->material_encode && refs->material_decode && refs->identity_decode;
}
static bool append(model_inventory *inventory, qa_scene_model *model, size_t parent, qa_error *error)
{
    for (size_t i = 0; i < inventory->count; ++i)
        if (inventory->nodes[i].model == model) return fail(error, QA_ERROR_FORMAT, "Retained model graph aliases or cycles");
    if (inventory->count == inventory->capacity) {
        size_t capacity = inventory->capacity ? inventory->capacity * 2 : 8;
        if (capacity < inventory->capacity || capacity > SIZE_MAX / sizeof(*inventory->nodes))
            return fail(error, QA_ERROR_MEMORY, "Retained model inventory exceeds addressable storage");
        model_node *nodes = realloc(inventory->nodes, capacity * sizeof(*nodes));
        if (!nodes) return fail(error, QA_ERROR_MEMORY, "Indexing retained model nodes");
        inventory->nodes = nodes; inventory->capacity = capacity;
    }
    inventory->nodes[inventory->count++] = (model_node){model, parent}; return true;
}
static bool collect(model_inventory *inventory, qa_scene_model *model, qa_error *error)
{
    if (model->image_policy || model->replacement_parent || model->replacement_next ||
        !qa_scene_model_observation_ready(model) || !append(inventory, model, SIZE_MAX, error))
        return fail(error, QA_ERROR_ARGUMENT, "Model capture requires an idle owned graph root");
    for (size_t i = 0; i < inventory->count; ++i) {
        qa_scene_model *parent = inventory->nodes[i].model;
        for (qa_scene_model *child = parent->replacement; child; child = child->replacement_next) {
            if (child->replacement_parent != parent || child->resources != model->resources || child->materials != model->materials ||
                !append(inventory, child, i, error)) return false;
        }
    }
    size_t count = 0;
    for (size_t i = 0; i < inventory->count; ++i) {
        qa_scene_model *node = inventory->nodes[i].model;
        size_t shadows = qa_scene_model_shadow_identity_count(node);
        if (!node->source || count == SIZE_MAX || node->source->mesh_count > SIZE_MAX - count - 1 ||
            shadows > SIZE_MAX - count - 1 - node->source->mesh_count)
            return fail(error, QA_ERROR_FORMAT, "Retained model namespace extent overflow");
        count += 1 + node->source->mesh_count + shadows;
    }
    if (count > SIZE_MAX / sizeof(*inventory->identities)) return fail(error, QA_ERROR_MEMORY, "Model namespace exceeds addressable storage");
    inventory->identities = malloc(count * sizeof(*inventory->identities));
    if (!inventory->identities) return fail(error, QA_ERROR_MEMORY, "Indexing retained model namespace");
    for (size_t i = 0; i < inventory->count; ++i) {
        qa_scene_model *node = inventory->nodes[i].model;
        inventory->identities[inventory->identity_count++] = (qa_scene_model_saved_identity){QA_SCENE_MODEL_IDENTITY_MODEL, i, 0, node->identity};
        for (size_t mesh = 0; mesh < node->source->mesh_count; ++mesh)
            inventory->identities[inventory->identity_count++] = (qa_scene_model_saved_identity){QA_SCENE_MODEL_IDENTITY_MESH, i, mesh, node->meshes[mesh].retained.identity};
        size_t ordinal = 0;
        for (scene_model_shadow_identity *entry = node->shadow_identities; entry; entry = entry->next, ++ordinal)
            inventory->identities[inventory->identity_count++] = (qa_scene_model_saved_identity){QA_SCENE_MODEL_IDENTITY_SHADOW, i, ordinal, entry->identity};
    }
    return true;
}
static void inventory_dispose(model_inventory *inventory)
{ free(inventory->nodes); free(inventory->identities); memset(inventory, 0, sizeof(*inventory)); }
static bool blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t count = bytes->size;
    if (!qa_source_save_count(io, &count, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)bytes->data, count);
    if (count > io->input.size - io->offset) return false;
    *bytes = (qa_bytes){io->input.data + io->offset, count}; io->offset += count; return true;
}
static bool name(qa_source_save_io *io, char **text)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = reading ? 0 : strlen(*text);
    if (!qa_source_save_count(io, &count, reading ? io->input.size - io->offset : SIZE_MAX) || count == SIZE_MAX) return false;
    if (reading) {
        *text = calloc(count + 1, 1);
        if (!*text) return fail(io->error, QA_ERROR_MEMORY, "Copying retained model image name");
    }
    return qa_source_save_bytes(io, *text, count) && !memchr(*text, 0, count);
}
static bool model_ref(qa_source_save_io *io, const qa_scene_model_owner_refs *refs, const qa_model **source,
    qa_scene_model_content_lease *lease)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; uint64_t key = UINT64_MAX;
    if ((!reading && (!*source || !refs->model_encode(refs->context, *source, &key, io->error))) ||
        !qa_source_save_u64(io, &key) || key == UINT64_MAX) return false;
    if (reading && (!refs->model_decode(refs->context, key, source, io->error) || !*source)) return false;
    if (reading && lease && (!refs->model_retain ||
        !refs->model_retain(refs->context,*source,lease,io->error) || !lease->context || !lease->release)) return false;
    return true;
}
static bool animation_ref(qa_source_save_io *io, const qa_scene_model_owner_refs *refs, const qa_model_animation **source,
    qa_scene_model_content_lease *lease)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; uint64_t key = UINT64_MAX;
    if (reading ? !refs->animation_decode : !refs->animation_encode)
        return fail(io->error, QA_ERROR_ARGUMENT, "Retained replacement animation namespace is absent");
    if ((!reading && (!*source || !refs->animation_encode(refs->context, *source, &key, io->error))) ||
        !qa_source_save_u64(io, &key) || key == UINT64_MAX) return false;
    if (reading && (!refs->animation_decode(refs->context, key, source, io->error) || !*source)) return false;
    return !reading || (refs->animation_retain &&
        refs->animation_retain(refs->context,*source,lease,io->error) && lease->context && lease->release);
}
static bool image_ref(qa_source_save_io *io, const qa_scene_model_owner_refs *refs, const qa_scene_image **image)
{
    bool present = *image != NULL, reading = io->direction == QA_SOURCE_SAVE_READ; uint64_t key = UINT64_MAX;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) { if (reading) *image = NULL; return true; }
    if ((!reading && !refs->image_encode(refs->context, *image, &key, io->error)) ||
        !qa_source_save_u64(io, &key) || key == UINT64_MAX) return false;
    if (reading) {
        const qa_scene_image *decoded = NULL;
        if (!refs->image_decode(refs->context, key, &decoded, io->error) || !decoded) return false;
        qa_scene_image_retain(decoded); *image = decoded;
    }
    return true;
}
static bool material_ref(qa_source_save_io *io, const qa_scene_model_owner_refs *refs, const qa_material **material)
{
    bool present = *material != NULL, reading = io->direction == QA_SOURCE_SAVE_READ; uint64_t key = UINT64_MAX;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) { if (reading) *material = NULL; return true; }
    return (!reading ? refs->material_encode(refs->context, *material, &key, io->error) : true) &&
        qa_source_save_u64(io, &key) && key != UINT64_MAX &&
        (!reading || (refs->material_decode(refs->context, key, material, io->error) && *material));
}
static bool options(qa_source_save_io *io, qa_scene_model *model)
{
    qa_scene_image_options *value = &model->options; bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t family = value->family, wrap = value->wrap, filter = value->filter, usage = value->usage;
    int32_t transparent_index = value->transparent_index;
    if (!qa_source_save_u32(io, &family) || family > QA_SCENE_Q3 || !qa_source_save_u32(io, &wrap) || wrap > QA_SCENE_CLAMP ||
        !qa_source_save_u32(io, &filter) || filter > QA_SCENE_LINEAR_MIPMAP_LINEAR ||
        !qa_source_save_u32(io, &usage) || usage != (uint32_t)(model->source->format == QA_MODEL_SPR || model->source->format == QA_MODEL_SP2 ? QA_IMAGE_USAGE_SPRITE : QA_IMAGE_USAGE_SKIN) ||
        !qa_source_save_i32(io, &transparent_index) || !qa_source_save_bool(io, &value->mipmap) ||
        !qa_source_save_bool(io, &value->transparent) || !qa_source_save_bool(io, &value->fullbright_only)) return false;
    if (reading) {
        value->family = (qa_scene_family)family; value->wrap = (qa_scene_wrap)wrap; value->filter = (qa_scene_filter)filter;
        value->usage = (qa_scene_image_usage)usage; value->transparent_index = transparent_index;
    }
    bool palette = value->palette_rgb.size != 0, translation = value->translation.size != 0;
    if (!qa_source_save_bytes(io, model->palette, sizeof(model->palette)) ||
        !qa_source_save_bytes(io, model->translation, sizeof(model->translation)) ||
        !qa_source_save_bool(io, &palette) || !qa_source_save_bool(io, &translation)) return false;
    if (reading) {
        value->palette_rgb = palette ? (qa_bytes){model->palette, sizeof(model->palette)} : (qa_bytes){0};
        value->translation = translation ? (qa_bytes){model->translation, sizeof(model->translation)} : (qa_bytes){0};
    } else if ((palette && (value->palette_rgb.data != model->palette || value->palette_rgb.size != sizeof(model->palette))) ||
        (translation && (value->translation.data != model->translation || value->translation.size != sizeof(model->translation)))) return false;
    if (!qa_scene_source_upload_precision_fields(io, value)) return false;
    return (!palette || value->palette_rgb.size == 768) &&
        (!(model->source->format == QA_MODEL_MDL || model->source->format == QA_MODEL_SPR || family == QA_SCENE_Q2) || palette) &&
        (model->source->format != QA_MODEL_SP2 || (value->transparent && value->transparent_index == 255 && !value->mipmap));
}
static bool allocate(qa_source_save_io *io, size_t count, size_t size, void **out)
{
    size_t physical = count ? count : 1;
    if (physical > SIZE_MAX / size) return fail(io->error, QA_ERROR_FORMAT, "Saved model allocation extent overflow");
    *out = calloc(physical, size);
    return *out != NULL || fail(io->error, QA_ERROR_MEMORY, "Allocating detached model arrays");
}
static bool image_slot(qa_source_save_io *io, scene_model_image **images, size_t count, scene_model_image **slot)
{
    size_t key = SIZE_MAX;
    if (io->direction == QA_SOURCE_SAVE_WRITE && *slot) {
        for (size_t i = 0; i < count; ++i) if (images[i] == *slot) { key = i; break; }
        if (key == SIZE_MAX) return false;
    }
    uint64_t encoded = key == SIZE_MAX ? UINT64_MAX : key;
    if (!qa_source_save_u64(io, &encoded) || (encoded != UINT64_MAX && encoded >= count)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) *slot = encoded == UINT64_MAX ? NULL : images[(size_t)encoded];
    return true;
}
static bool images(qa_source_save_io *io, qa_scene_model *model, const qa_scene_model_owner_refs *refs,
    scene_model_image ***table, size_t *out_count)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; size_t count = 0;
    if (!reading) for (scene_model_image *entry = model->images; entry; entry = entry->next) ++count;
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 11 : SIZE_MAX) || count > SIZE_MAX / sizeof(**table)) return false;
    *table = count ? calloc(count, sizeof(**table)) : NULL; *out_count = count;
    if (count && !*table) return fail(io->error, QA_ERROR_MEMORY, "Indexing model image aliases");
    scene_model_image *entry = model->images; scene_model_image **tail = &model->images;
    for (size_t i = 0; i < count; ++i) {
        if (reading) {
            entry = calloc(1, sizeof(*entry));
            if (!entry) return fail(io->error, QA_ERROR_MEMORY, "Restoring model image holder");
            *tail = entry; tail = &entry->next;
        }
        (*table)[i] = entry;
        if (!name(io, &entry->name) || !material_ref(io, refs, &entry->material) ||
            !image_ref(io, refs, &entry->base) || !image_ref(io, refs, &entry->fullbright)) return false;
        if (!qa_source_save_bool(io, &entry->indexed_override)) return false;
        if (entry->indexed_override) {
            size_t pixels = reading ? 0 : entry->indexed_pixels.size;
            if (model->source->format != QA_MODEL_MDL || model->options.family != QA_SCENE_Q1 ||
                entry->material || !qa_source_save_u32(io, &entry->indexed_width) ||
                !qa_source_save_u32(io, &entry->indexed_height) || !entry->indexed_width || !entry->indexed_height ||
                entry->indexed_width > SIZE_MAX / entry->indexed_height ||
                !qa_source_save_count(io, &pixels, reading ? io->input.size : SIZE_MAX) ||
                pixels != (size_t)entry->indexed_width * entry->indexed_height) return false;
            if (reading) {
                entry->indexed_pixels.data = malloc(pixels); entry->indexed_pixels.size = pixels;
                if (!entry->indexed_pixels.data) return fail(io->error, QA_ERROR_MEMORY, "Restoring indexed override pixels");
            }
            if (!entry->indexed_pixels.data || !qa_source_save_bytes(io, entry->indexed_pixels.data, pixels)) return false;
        } else if (entry->indexed_pixels.data || entry->indexed_pixels.size || entry->indexed_width || entry->indexed_height) return false;
        for (size_t j = 0; j < i; ++j) if (!strcmp((*table)[j]->name, entry->name)) return false;
        if (entry->material ? model->options.family != QA_SCENE_Q3 || entry->base || entry->fullbright : !entry->base) return false;
        if (entry->material) {
            bool found = false;
            for (size_t material = 0; material < qa_material_library_record_count(model->materials); ++material)
                if (qa_material_library_record_at(model->materials, material) == entry->material) { found = true; break; }
            if (!found) return false;
        }
        const qa_scene_resources *owners[1] = {model->resources}; size_t owner_index;
        if ((entry->base && !qa_scene_image_owner_index(owners, 1, entry->base, &owner_index)) ||
            (entry->fullbright && !qa_scene_image_owner_index(owners, 1, entry->fullbright, &owner_index))) return false;
        if (!reading) entry = entry->next;
    }
    return true;
}
static bool mesh_source_ready(const qa_scene_model *model, size_t index)
{
    const qa_model_mesh *source = &model->source->meshes[index];
    const scene_model_mesh *mesh = &model->meshes[index];
    const qa_scene_vec4 white = {1, 1, 1, 1}; const qa_scene_vec2 zero = {0};
    if (model->source_topology && (mesh->retained.vertex_count != source->vertex_count ||
        source->texcoord_count != source->vertex_count)) return false;
    qa_bounds bounds = model_bounds_empty();
    for (size_t i = 0; i < mesh->retained.vertex_count; ++i) {
        const qa_scene_vertex *vertex = &mesh->vertices[i];
        const qa_model_vertex *original = &source->vertices[mesh->sources[i]];
        if (model->source_topology && (mesh->sources[i] != i ||
            memcmp(&vertex->texcoord, source->texcoords[i].uv, sizeof(vertex->texcoord)))) return false;
        if (memcmp(&vertex->position, original->position, sizeof(original->position)) ||
            memcmp(&vertex->normal, original->normal, sizeof(original->normal)) ||
            memcmp(&vertex->lightmap, &zero, sizeof(zero)) || memcmp(&vertex->color, &white, sizeof(white))) return false;
        model_bounds_add(&bounds, vertex->position);
    }
    if (!mesh->retained.vertex_count) bounds = (qa_bounds){0};
    if (memcmp(&bounds.mins, &mesh->retained.bounds.mins, sizeof(bounds.mins)) ||
        memcmp(&bounds.maxs, &mesh->retained.bounds.maxs, sizeof(bounds.maxs))) return false;
    for (uint32_t triangle = 0; triangle < source->triangle_count; ++triangle)
        for (uint32_t corner = 0; corner < 3; ++corner) {
            uint32_t retained = mesh->indices[(size_t)triangle * 3 + corner]; float uv[2];
            if (mesh->sources[retained] != source->triangles[triangle].vertex[corner] ||
                !qa_model_corner_uv(model->source, (uint32_t)index, triangle, corner, uv) ||
                memcmp(&mesh->vertices[retained].texcoord, uv, sizeof(uv))) return false;
        }
    return true;
}
static bool identity_field(qa_source_save_io *io,const qa_scene_model_owner_refs *refs,
    qa_scene_model_identity_kind kind,size_t node,size_t ordinal,uint64_t *identity)
{
    uint64_t saved=*identity;
    if (io->direction==QA_SOURCE_SAVE_WRITE && refs->identity_encode &&
        !refs->identity_encode(refs->context,kind,node,ordinal,*identity,&saved,io->error)) return false;
    if (!qa_source_save_u64(io,&saved) || !saved) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) *identity=saved;
    return true;
}
static bool mesh(qa_source_save_io *io, qa_scene_model *model, size_t node, size_t index,
    const qa_scene_model_owner_refs *refs, scene_model_image **image_table, size_t image_count)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    scene_model_mesh *value = &model->meshes[index]; const qa_model_mesh *source = &model->source->meshes[index];
    if (source->triangle_count > UINT32_MAX / 3) return false;
    size_t corners = (size_t)source->triangle_count * 3;
    size_t vertices = model->source_topology ? source->vertex_count : corners;
    size_t physical = vertices ? vertices : 1, physical_indices = corners ? corners : 1;
    uint64_t key = UINT64_MAX; qa_scene_geometry_view view = {0};
    if ((!reading && (!value->retained.geometry || !refs->geometry_encode(refs->context, value->retained.geometry, &key, io->error))) ||
        !qa_source_save_u64(io, &key) || key == UINT64_MAX) return false;
    if (reading) {
        const qa_scene_geometry *geometry = NULL;
        if (!refs->geometry_decode(refs->context, key, &geometry, io->error) || !geometry || !qa_scene_geometry_read(geometry, &view)) return false;
        qa_scene_geometry_retain(geometry); value->retained.geometry = geometry;
        value->vertices = (qa_scene_vertex *)view.vertices; value->indices = (uint32_t *)view.indices;
    } else if (!qa_scene_geometry_read(value->retained.geometry, &view) || view.vertices != value->vertices || view.indices != value->indices) return false;
    if (view.vertex_count != physical || view.index_count != physical_indices) return false;
    qa_scene_mesh *retained = &value->retained; uint32_t primitive = retained->primitive;
    if (!identity_field(io,refs,QA_SCENE_MODEL_IDENTITY_MESH,node,index,&retained->identity) ||
        !qa_source_save_u64(io, &retained->revision) || retained->revision != 1 ||
        !qa_source_save_count(io, &retained->vertex_count, vertices) ||
        !qa_source_save_count(io, &retained->index_count, corners) || retained->index_count != corners ||
        !qa_source_save_vec3(io, &retained->bounds.mins) || !qa_source_save_vec3(io, &retained->bounds.maxs) ||
        !qa_source_save_u32(io, &primitive) || primitive != QA_SCENE_TRIANGLES) return false;
    if (reading) {
        retained->vertices = view.vertices; retained->indices = view.indices; retained->primitive = (qa_scene_primitive)primitive;
        if (!allocate(io, physical, sizeof(*value->sources), (void **)&value->sources) ||
            !allocate(io, source->shader_count, sizeof(*value->shaders), (void **)&value->shaders)) return false;
    } else if (retained->vertices != view.vertices || retained->indices != view.indices || !value->sources || !value->shaders) return false;
    for (size_t i = 0; i < physical; ++i) if (!qa_source_save_u32(io, &value->sources[i]) ||
        (i < retained->vertex_count && value->sources[i] >= source->vertex_count)) return false;
    for (size_t i = 0; i < corners; ++i) if (value->indices[i] >= retained->vertex_count) return false;
    size_t normals = 0;
    if (model->source->format == QA_MODEL_MDL || model->source->format == QA_MODEL_MD2) {
        if (source->frame_count && source->vertex_count > SIZE_MAX / source->frame_count) return false;
        normals = (size_t)source->vertex_count * source->frame_count;
        if (reading && !allocate(io, normals, 1, (void **)&value->normal_indices)) return false;
        if (!value->normal_indices) return false;
        if (reading) for (size_t i = 0; i < normals; ++i)
            value->normal_indices[i] = scene_model_normal_index(source->vertices[i].normal);
    } else if (value->normal_indices) return false;
    for (size_t i = 0; i < (source->shader_count ? source->shader_count : 1); ++i)
        if (!image_slot(io, image_table, image_count, &value->shaders[i])) return false;
    return mesh_source_ready(model, index);
}
static bool node_fields(qa_source_save_io *io, qa_scene_model *model, size_t node,
    const qa_scene_model_owner_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!model_ref(io, refs, &model->source, &model->source_lease) || !options(io, model) ||
        !refs->source_qualify(refs->context, model->source, model->resources, model->materials, &model->options, io->error) ||
        !identity_field(io,refs,QA_SCENE_MODEL_IDENTITY_MODEL,node,0,&model->identity)) return false;
    if (!qa_source_save_bool(io, &model->source_topology)) return false;
    if (model->source_topology && (!qa_material_library_has_source_profile(model->materials) ||
        (model->source->format != QA_MODEL_MD3 && model->source->format != QA_MODEL_MD4))) return false;
    if (reading && (!allocate(io, model->source->mesh_count, sizeof(*model->meshes), (void **)&model->meshes) ||
        !allocate(io, model->source->skin_count, sizeof(*model->skins), (void **)&model->skins) ||
        !allocate(io, model->source->sprite_count, sizeof(*model->sprites), (void **)&model->sprites))) return false;
    if (!model->meshes || !model->skins || !model->sprites) return false;
    scene_model_image **image_table = NULL; size_t image_count = 0;
    bool ok = images(io, model, refs, &image_table, &image_count);
    for (size_t i = 0; ok && i < model->source->mesh_count; ++i) ok = mesh(io, model, node, i, refs, image_table, image_count);
    for (size_t i = 0; ok && i < (model->source->skin_count ? model->source->skin_count : 1); ++i)
        ok = image_slot(io, image_table, image_count, &model->skins[i]);
    for (size_t i = 0; ok && i < (model->source->sprite_count ? model->source->sprite_count : 1); ++i)
        ok = image_slot(io, image_table, image_count, &model->sprites[i]);
    if (ok && reading) for (scene_model_image *entry = model->images; entry; entry = entry->next) {
        bool indexed = entry->indexed_override;
        if (model->source->format == QA_MODEL_MDL)
            for (size_t i = 0; !indexed && i < model->source->skin_count; ++i) indexed = model->skins[i] == entry;
        if (model->source->format == QA_MODEL_SPR)
            for (size_t i = 0; !indexed && i < model->source->sprite_count; ++i) indexed = model->sprites[i] == entry;
        if (!indexed) continue;
        const qa_scene_image *images[2] = {entry->base, entry->fullbright};
        for (unsigned i = 0; i < 2; ++i)
            if (images[i] && images[i]->recipient_upload_pixels)
                ((qa_scene_image *)images[i])->recipient_mipmap = model->options.mipmap && model->source->format != QA_MODEL_SPR;
    }
    bool replacement = model->replacement_source != NULL;
    ok = ok && qa_source_save_bool(io, &replacement);
    if (ok && replacement) {
        qa_model_replacement *description = &model->replacement_description;
        if (!reading && model->replacement_source != description) ok = false;
        ok = ok && model_ref(io, refs, &description->source, &model->replacement_source_lease) &&
            model_ref(io, refs, &description->mesh, NULL) &&
            animation_ref(io, refs, &description->animation, &model->animation_lease) && qa_source_save_i32(io, &description->flags) &&
            qa_source_save_bool(io, &description->elapsed_animation) &&
            qa_source_save_count(io, &model->replacement_skin_count, description->source->skin_count) &&
            model->replacement_skin_count == description->source->skin_count && description->mesh == model->source &&
            model->source->format == QA_MODEL_MD5 &&
            (description->source->format == QA_MODEL_MDL || description->source->format == QA_MODEL_MD2) &&
            description->animation->joint_count == model->source->bone_count && description->animation->frame_count;
        if (ok && reading) {
            model->replacement_source = description;
            ok = allocate(io, model->source->mesh_count, sizeof(*model->replacement_skins), (void **)&model->replacement_skins);
        }
        for (size_t i = 0; ok && i < model->source->mesh_count; ++i) {
            if (reading) ok = allocate(io, model->replacement_skin_count, sizeof(*model->replacement_skins[i]), (void **)&model->replacement_skins[i]);
            else ok = model->replacement_skins && model->replacement_skins[i];
            for (size_t skin = 0; ok && skin < (model->replacement_skin_count ? model->replacement_skin_count : 1); ++skin)
                ok = image_slot(io, image_table, image_count, &model->replacement_skins[i][skin]);
        }
    } else if (ok && (model->replacement_skins || model->replacement_skin_count)) ok = false;
    size_t count = reading ? 0 : qa_scene_model_shadow_identity_count(model);
    ok = ok && qa_source_save_count(io, &count, reading ? io->input.size / 12 : SIZE_MAX);
    scene_model_shadow_identity *entry = model->shadow_identities, **tail = &model->shadow_identities;
    for (size_t i = 0; ok && i < count; ++i) {
        if (reading) {
            entry = calloc(1, sizeof(*entry));
            if (!entry) { ok = fail(io->error, QA_ERROR_MEMORY, "Restoring model shadow owner"); break; }
            *tail = entry; tail = &entry->next;
        }
        ok = qa_source_save_u32(io, &entry->entity) && identity_field(io,refs,QA_SCENE_MODEL_IDENTITY_SHADOW,node,i,&entry->identity);
        for (scene_model_shadow_identity *prior = model->shadow_identities; ok && prior != entry; prior = prior->next)
            if (prior->entity == entry->entity) ok = false;
        if (!reading) entry = entry->next;
    }
    free(image_table); return ok;
}
static bool prefix(qa_source_save_io *io, model_inventory *inventory, qa_bytes *body)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; uint8_t magic[4] = {'Q','M','O','N'}; if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QMON", 4) ||
        !qa_source_save_count(io, &inventory->identity_count, reading ? io->input.size / 28 : SIZE_MAX) || !inventory->identity_count) return false;
    if (reading) {
        if (inventory->identity_count > SIZE_MAX / sizeof(*inventory->identities)) return false;
        inventory->identities = calloc(inventory->identity_count, sizeof(*inventory->identities));
        if (!inventory->identities) return fail(io->error, QA_ERROR_MEMORY, "Reading model identity inventory");
    }
    for (size_t i = 0; i < inventory->identity_count; ++i) {
        qa_scene_model_saved_identity *row = &inventory->identities[i]; uint32_t kind = row->kind;
        if (!qa_source_save_u32(io, &kind) || kind > QA_SCENE_MODEL_IDENTITY_SHADOW ||
            !qa_source_save_count(io, &row->node, inventory->identity_count - 1) ||
            !qa_source_save_count(io, &row->ordinal, SIZE_MAX) ||
            !qa_source_save_u64(io, &row->saved) || !row->saved) return false;
        if (reading) row->kind = (qa_scene_model_identity_kind)kind;
        if (kind == QA_SCENE_MODEL_IDENTITY_MODEL && row->ordinal) return false;
        for (size_t j = 0; j < i; ++j) if (inventory->identities[j].saved == row->saved ||
            (inventory->identities[j].node == row->node && inventory->identities[j].kind == row->kind &&
             inventory->identities[j].ordinal == row->ordinal)) return false;
    }
    return blob(io, body);
}
static bool replacement_policy_fields(qa_source_save_io *io, model_inventory *inventory)
{
    qa_scene_model *root = inventory->nodes[0].model;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t selected = UINT64_MAX;
    if (!reading && root->selected_replacement) {
        for (size_t i = 1; i < inventory->count; ++i)
            if (inventory->nodes[i].model == root->selected_replacement) { selected = i; break; }
        if (selected == UINT64_MAX) return false;
    }
    if (!qa_source_save_bool(io, &root->replacement_policy_set) ||
        !qa_source_save_bool(io, &root->replacement_policy_enabled) ||
        !qa_source_save_f64(io, &root->replacement_distance) || !qa_source_save_u64(io, &selected)) return false;
    if (!root->replacement_policy_set) {
        if (root->replacement_policy_enabled || root->replacement_distance != 0 || selected != UINT64_MAX) return false;
    } else if (root->source->format != QA_MODEL_MDL && root->source->format != QA_MODEL_MD2) return false;
    if (selected != UINT64_MAX) {
        if (selected >= inventory->count || inventory->nodes[selected].parent != 0 ||
            !inventory->nodes[selected].model->replacement_source ||
            inventory->nodes[selected].model->replacement_source->source != root->source) return false;
        if (reading) root->selected_replacement = inventory->nodes[selected].model;
    }
    for (size_t i = 1; i < inventory->count; ++i) {
        const qa_scene_model *child = inventory->nodes[i].model;
        if (child->replacement_policy_set || child->replacement_policy_enabled ||
            child->replacement_distance != 0 || child->selected_replacement) return false;
    }
    return true;
}
static bool identity_install(model_inventory *inventory, const qa_scene_model_owner_refs *refs, qa_error *error)
{
    size_t at = 0;
    for (size_t i = 0; i < inventory->count; ++i) {
        qa_scene_model *model = inventory->nodes[i].model;
        size_t shadows = qa_scene_model_shadow_identity_count(model);
        if (model->source->mesh_count > SIZE_MAX - 1 - shadows) return false;
        size_t count = 1 + model->source->mesh_count + shadows;
        scene_model_shadow_identity *shadow = model->shadow_identities;
        for (size_t j = 0; j < count; ++j) {
            qa_scene_model_identity_kind kind = j == 0 ? QA_SCENE_MODEL_IDENTITY_MODEL :
                j <= model->source->mesh_count ? QA_SCENE_MODEL_IDENTITY_MESH : QA_SCENE_MODEL_IDENTITY_SHADOW;
            size_t ordinal = j == 0 ? 0 : j <= model->source->mesh_count ? j - 1 : j - 1 - model->source->mesh_count;
            uint64_t *identity = j == 0 ? &model->identity : j <= model->source->mesh_count ?
                &model->meshes[ordinal].retained.identity : &shadow->identity;
            if (at >= inventory->identity_count) return false;
            qa_scene_model_saved_identity *row = &inventory->identities[at];
            if (row->kind != kind || row->node != i || row->ordinal != ordinal || row->saved != *identity ||
                !refs->identity_decode(refs->context, kind, i, ordinal, row->saved, identity, error) || !*identity) return false;
            for (size_t k = 0; k < at; ++k) if (inventory->identities[k].saved == *identity) return false;
            row->saved = *identity; ++at;
            if (kind == QA_SCENE_MODEL_IDENTITY_SHADOW) shadow = shadow->next;
        }
    }
    return at == inventory->identity_count;
}
bool qa_scene_model_owner_checkpoint(const qa_scene_model *model, const qa_scene_model_owner_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!model || !refs_ready(refs) || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "Model capture requires qualified references and empty output");
    model_inventory inventory = {0}; qa_source_save_io io = {0}, state = {0}; qa_buffer owned_body = {0};
    bool ok = collect(&inventory, (qa_scene_model *)model, error);
    for (size_t i=0;ok && refs->identity_encode && i<inventory.identity_count;++i) {
        qa_scene_model_saved_identity *row=inventory.identities+i;
        ok=refs->identity_encode(refs->context,row->kind,row->node,row->ordinal,row->saved,&row->saved,error) && row->saved;
    }
    if (ok) ((qa_scene_model *)model)->checkpoint_active = true;
    ok = ok && qa_source_save_writer(&state, NULL, error) && qa_source_save_count(&state, &inventory.count, SIZE_MAX);
    for (size_t i = 0; ok && i < inventory.count; ++i) {
        uint64_t parent = inventory.nodes[i].parent == SIZE_MAX ? UINT64_MAX : inventory.nodes[i].parent;
        ok = qa_source_save_u64(&state, &parent) && node_fields(&state, inventory.nodes[i].model, i, refs);
    }
    ok = ok && replacement_policy_fields(&state, &inventory) && qa_source_save_finish(&state, &owned_body);
    qa_bytes body = {owned_body.data, owned_body.size};
    ok = ok && qa_source_save_writer(&io, NULL, error) && prefix(&io, &inventory, &body) && qa_source_save_finish(&io, out);
    if (inventory.count) ((qa_scene_model *)model)->checkpoint_active = false;
    if (!ok && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Invalid complete model owner graph");
    qa_source_save_dispose(&state); qa_source_save_dispose(&io); qa_buffer_free(&owned_body); inventory_dispose(&inventory); return ok;
}
bool qa_scene_model_owner_identities_read(qa_bytes bytes, qa_scene_model_saved_identity **out, size_t *count, qa_error *error)
{
    if (!out || *out || !count || *count) return fail(error, QA_ERROR_ARGUMENT, "Model namespace read requires empty outputs");
    model_inventory inventory = {0}; qa_source_save_io io = {0}; qa_bytes body = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && prefix(&io, &inventory, &body) && qa_source_save_finish(&io, NULL);
    if (ok) { *out = inventory.identities; *count = inventory.identity_count; inventory.identities = NULL; }
    else if (error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Invalid saved model namespace prefix");
    qa_source_save_dispose(&io); inventory_dispose(&inventory); return ok;
}
bool qa_scene_model_owner_restore(const qa_model *qualified_source, qa_scene_resources *resources,
    qa_material_library *materials, qa_bytes bytes, const qa_scene_model_owner_refs *refs, qa_scene_model **out, qa_error *error)
{
    if (!qualified_source || !resources || !refs_ready(refs) || !refs->model_retain || !out || *out ||
        (materials && (qa_material_library_resource_owner(materials) != resources || !qa_material_library_order_ready(materials))))
        return fail(error, QA_ERROR_ARGUMENT, "Model restore requires installed qualified owners and empty output");
    model_inventory inventory = {0}; qa_source_save_io io = {0}, state = {0}; qa_bytes body = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && prefix(&io, &inventory, &body) && qa_source_save_finish(&io, NULL) &&
        qa_source_save_reader(&state, NULL, body, error) && qa_source_save_count(&state, &inventory.count, body.size / 20) && inventory.count &&
        inventory.count <= SIZE_MAX / sizeof(*inventory.nodes);
    if (ok) {
        inventory.nodes = calloc(inventory.count, sizeof(*inventory.nodes));
        if (!inventory.nodes) ok = fail(error, QA_ERROR_MEMORY, "Allocating detached model graph");
    }
    for (size_t i = 0; ok && i < inventory.count; ++i) {
        uint64_t parent = UINT64_MAX;
        ok = qa_source_save_u64(&state, &parent) && (!i ? parent == UINT64_MAX : parent < i);
        if (!ok) break;
        inventory.nodes[i].parent = parent == UINT64_MAX ? SIZE_MAX : (size_t)parent;
        qa_scene_model *model = calloc(1, sizeof(*model)); inventory.nodes[i].model = model;
        if (!model) { ok = fail(error, QA_ERROR_MEMORY, "Allocating detached retained model"); break; }
        model->resources = resources; model->materials = materials;
        ok = node_fields(&state, model, i, refs) && (i || model->source == qualified_source) &&
            (model->options.family != QA_SCENE_Q3 || materials);
        if (ok && i) {
            const qa_scene_model *parent_model = inventory.nodes[(size_t)parent].model;
            ok = model->replacement_source && (model->replacement_description.source == parent_model->source ||
                model->replacement_description.mesh == parent_model->source) &&
                (i == 1 || parent >= inventory.nodes[i - 1].parent);
        }
    }
    ok = ok && replacement_policy_fields(&state, &inventory) &&
        qa_source_save_finish(&state, NULL) && identity_install(&inventory, refs, error);
    if (ok) {
        for (size_t i = 1; i < inventory.count; ++i) {
            qa_scene_model *model = inventory.nodes[i].model, *parent = inventory.nodes[inventory.nodes[i].parent].model;
            qa_scene_model **tail = &parent->replacement;
            while (*tail) tail = &(*tail)->replacement_next;
            *tail = model; model->replacement_parent = parent;
        }
        *out = inventory.nodes[0].model;
    } else {
        if (inventory.nodes) for (size_t i = 0; i < inventory.count; ++i) qa_scene_model_destroy(inventory.nodes[i].model);
        if (error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Complete model owner differs from qualified content or references");
    }
    qa_source_save_dispose(&state); qa_source_save_dispose(&io); inventory_dispose(&inventory); return ok;
}
