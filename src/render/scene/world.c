#include "world/internal.h"
#include "world/legacy/internal.h"
#include "world/boxed_sky.h"
#include "world/q3/internal.h"
#include "resources_internal.h"
#include "qa/scene_effects.h"
#include "qa/scene_world_save.h"
#include "qa/material_library_save.h"
#include "qa/binary.h"
#include "../controls_private.h"

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool world_error(qa_error *error, qa_status status, const char *message)
{
    qa_error_set(error, status, 0, "%s", message);
    return false;
}
static bool valid_input(const qa_scene_world *, const qa_scene_world_input *, qa_error *);
static bool surface_culled(const qa_scene_world *, const qaw_surface *, const qa_scene_world_input *,
    const qa_material_context *, const qa_model_transform *, const qa_scene_plane *, size_t);
static uint32_t surface_light_mask(const qaw_surface *, uint32_t, const qa_scene_light *, size_t);
bool qaw_world_owners_retain(qa_scene_world *world,qa_scene_resources *resources,
    qa_material_library *materials,qa_error *error)
{
    if (!world || world->references || !resources || !materials ||
        qa_material_library_resource_owner(materials)!=resources)
        return world_error(error,QA_ERROR_ARGUMENT,"World retention requires its actual resource and material owners");
    world->references=1;
    world->resources=resources;
    world->materials=materials;
    if (!qa_scene_resources_retain(resources,error)) return false;
    world->retained_resources=resources;
    if (!qa_material_library_retain(materials,error)) return false;
    world->retained_materials=materials;
    return true;
}
bool qa_scene_world_retain(qa_scene_world *world,qa_error *error)
{
    if (!world || !world->references || world->references==SIZE_MAX || !world->identity ||
        world->resources!=world->retained_resources || world->materials!=world->retained_materials ||
        qa_material_library_resource_owner(world->materials)!=world->resources)
        return world_error(error,QA_ERROR_ARGUMENT,"World retention requires its actual live owning world");
    ++world->references;
    return true;
}
void qa_scene_world_release(qa_scene_world *world) { qa_scene_world_destroy(world); }
uint64_t qa_scene_world_identity(const qa_scene_world *world)
{ return world ? world->identity : 0; }
bool qa_scene_world_source_resource_bind(qa_scene_world *world, const qa_resource *resource, qa_error *error)
{
    if (!qa_scene_world_idle(world) || !resource ||
        (world->source_resource && world->source_resource != resource))
        return world_error(error, QA_ERROR_ARGUMENT, "World source binding requires its actual idle map owner and resource");
    qa_bytes bytes = qa_resource_bytes(resource);
    if (bytes.size != world->bytes.size || (bytes.size &&
        (!bytes.data || memcmp(bytes.data, world->bytes.data, bytes.size))))
        return world_error(error, QA_ERROR_ARGUMENT, "World source resource differs from its immutable BSP bytes");
    if (world->bsp.family == QA_BSP_Q1 && world->legacy_data) {
        qawl_world *legacy = world->legacy_data;
        for (size_t i = 0; i < legacy->texture_count; ++i) {
            qawl_texture *texture = legacy->textures + i;
            if (!scene_image_asset_source_bind(texture->image, resource, error) ||
                !scene_image_asset_source_bind(texture->fullbright, resource, error) ||
                !scene_image_asset_source_bind(texture->sky[0], resource, error) ||
                !scene_image_asset_source_bind(texture->sky[1], resource, error)) return false;
        }
    } else if (world->bsp.family == QA_BSP_Q3 && world->q3_data) {
        q3_data *data = world->q3_data;
        for (size_t i = 0; i < data->lightmap_count; ++i)
            if (!scene_image_asset_source_bind(data->lightmaps[i], resource, error)) return false;
    }
    if (!world->source_resource) {
        qa_resource_retain((qa_resource *)resource);
        world->source_resource = resource;
    }
    return true;
}
bool qa_scene_world_source_light_mask_read(const qa_scene_world *world, uint32_t surface,
    uint32_t *out, qa_error *error)
{
    if (!qa_scene_world_observation_ready(world) || !out || world->bsp.family != QA_BSP_Q3 ||
        surface >= world->surface_count || !world->source_dlight_masks ||
        world->surfaces[surface].source_index != surface)
        return world_error(error, QA_ERROR_ARGUMENT, "Source light read requires its actual retained world surface");
    *out = world->source_dlight_masks[surface];
    return true;
}
bool qa_scene_world_idle(const qa_scene_world *world)
{ return qa_scene_world_observation_ready(world) && !world->capture; }
bool qa_scene_world_observation_ready(const qa_scene_world *world)
{ return world && !world->transaction_depth && !world->admission_change_count && !world->checkpoint_active && !world->image_policy; }
bool qa_scene_world_remap_source(qa_scene_world *world, const char *original, const char *replacement,
    float offset, qa_material_source_remap_status *status, qa_error *error)
{
    if (!qa_scene_world_idle(world) || !status)
        return world_error(error, QA_ERROR_ARGUMENT, "Source shader remap requires its actual idle world");
    if (world->revision == UINT64_MAX)
        return world_error(error, QA_ERROR_MEMORY, "World material revision space exhausted");
    if (!qa_material_remap_source(world->materials, original, replacement, offset, status, error)) return false;
    if (*status == QA_MATERIAL_SOURCE_REMAP_APPLIED) ++world->revision;
    return true;
}
bool qa_scene_world_options_read(const qa_scene_world *world, qa_scene_world_options *out)
{
    if (!out || !qa_scene_world_observation_ready(world)) return false;
    *out = world->options;
    return true;
}
struct qa_scene_world_capture { qa_scene_world *world; };
bool qa_scene_world_capture_begin(const qa_scene_world *world, qa_scene_world_capture **out, qa_error *error)
{
    if (!out || *out || !qa_scene_world_idle(world) || world->restore_pending)
        return world_error(error,QA_ERROR_ARGUMENT,"World aggregate capture requires an idle actual owner and empty token");
    qa_scene_world_capture *capture=malloc(sizeof(*capture));
    if (!capture) return world_error(error,QA_ERROR_MEMORY,"Retaining the world owner capture lease");
    capture->world=(qa_scene_world *)world; capture->world->capture=capture; *out=capture; return true;
}
void qa_scene_world_capture_end(qa_scene_world_capture *capture)
{
    if (!capture) return;
    if (capture->world->capture==capture) capture->world->capture=NULL;
    free(capture);
}
size_t qa_scene_world_material_binding_count(const qa_scene_world *world)
{ return world ? world->surface_count : 0; }
bool qa_scene_world_material_binding_at(const qa_scene_world *world, size_t index, qa_scene_world_material_binding *out)
{
    if (!world || !out || index >= world->surface_count) return false;
    const qaw_surface *surface = &world->surfaces[index];
    *out = (qa_scene_world_material_binding){.current = surface->material, .base_current = surface->base_material};
    return true;
}
static bool world_material_member(const qa_material_library *library, const qa_material *material, uint64_t world)
{
    if (!material) return true;
    qa_material_library_record_view record;
    return qa_material_library_record_read(library, material->sorted_index, &record) && record.material == material &&
        material->order_entry && (!record.world_identity || record.world_identity == world);
}
bool qa_scene_world_materials_rebind_ready(const qa_scene_world *world, const qa_material_library *current,
    const qa_material_library *destination, const qa_scene_world_material_binding *bindings, size_t count, qa_error *error)
{
    if (!qa_scene_world_idle(world) || !current || !destination || world->materials != current ||
        !qa_material_library_order_ready(current) || !qa_material_library_order_ready(destination) ||
        qa_material_library_resource_owner(destination) != world->resources ||
        count != world->surface_count || (count && !bindings))
        return world_error(error, QA_ERROR_ARGUMENT, "world material publication requires idle qualified owners");
    for (size_t i = 0; i < count; ++i) {
        const qaw_surface *surface = &world->surfaces[i]; const qa_scene_world_material_binding *binding = &bindings[i];
        if (binding->current != surface->material || binding->base_current != surface->base_material ||
            (binding->current != NULL) != (binding->destination != NULL) ||
            (binding->base_current != NULL) != (binding->base_destination != NULL) ||
            !world_material_member(current, binding->current, world->identity) ||
            !world_material_member(current, binding->base_current, world->identity) ||
            !world_material_member(destination, binding->destination, world->identity) ||
            !world_material_member(destination, binding->base_destination, world->identity))
            return world_error(error, QA_ERROR_ARGUMENT, "world material binding differs from its actual surface owners");
    }
    return true;
}
void qa_scene_world_materials_rebind(qa_scene_world *world, qa_material_library *destination,
    const qa_scene_world_material_binding *bindings)
{
    if (!qa_scene_world_idle(world) || !destination ||
        qa_material_library_resource_owner(destination)!=world->resources) return;
    qa_material_library *previous=world->retained_materials;
    if (destination!=previous && !qa_material_library_retain(destination,NULL)) return;
    for (size_t i = 0; i < world->surface_count; ++i) {
        world->surfaces[i].material = bindings[i].destination;
        world->surfaces[i].base_material = bindings[i].base_destination;
    }
    world->materials = destination;
    world->retained_materials=destination;
    if (previous!=destination) qa_material_library_destroy(previous);
}

typedef struct world_policy_surface {
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    float sort;
    bool sky;
    const qa_material *material;
} world_policy_surface;
struct qa_scene_world_image_policy {
    qa_scene_world *owner;
    qa_scene_resource_policy *resources;
    const qa_scene_material_image_policy *materials;
    qawl_world textures;
    world_policy_surface *surfaces;
    size_t count;
    bool sealed, published;
};
static bool world_policy_current(const qa_scene_world_image_policy *ticket)
{
    return ticket && ticket->owner->image_policy == ticket &&
        !ticket->owner->transaction_depth && !ticket->owner->admission_change_count &&
        !ticket->owner->checkpoint_active && !ticket->owner->capture &&
        ticket->owner->resources == qa_scene_resource_policy_source(ticket->resources) &&
        ticket->owner->surface_count == ticket->count;
}
const qa_resource *qa_scene_world_source_resource_read(const qa_scene_world *world)
{
    return world && (qa_scene_world_observation_ready(world) ||
        (world->image_policy && world->image_policy->owner == world &&
         !world->image_policy->published && world_policy_current(world->image_policy))) ?
        world->source_resource : NULL;
}
static void world_policy_dispose(qa_scene_world_image_policy *ticket)
{
    qawl_textures_destroy(&ticket->textures);
    for (size_t i = 0; i < ticket->count; ++i)
        if (ticket->surfaces[i].mesh.geometry) qa_scene_geometry_release(ticket->surfaces[i].mesh.geometry);
    free(ticket->surfaces); ticket->owner->image_policy = NULL; free(ticket);
}
bool qa_scene_world_image_policy_prepare(qa_scene_world *world, qa_scene_resource_policy *resources,
    qa_scene_world_image_policy **out, qa_error *error)
{
    qa_scene_resources *destination = qa_scene_resource_policy_destination(resources);
    if (!out || *out || !qa_scene_world_idle(world) || !destination ||
        world->resources != qa_scene_resource_policy_source(resources) ||
        world->surface_count > SIZE_MAX / sizeof(world_policy_surface))
        return world_error(error, QA_ERROR_ARGUMENT, "World image preparation requires its actual resource policy");
    qa_scene_world_image_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return world_error(error, QA_ERROR_MEMORY, "Retaining world image preparation");
    ticket->surfaces = world->surface_count ? calloc(world->surface_count, sizeof(*ticket->surfaces)) : NULL;
    if (world->surface_count && !ticket->surfaces) {
        free(ticket); return world_error(error, QA_ERROR_MEMORY, "Preparing actual world surface image bindings");
    }
    ticket->owner = world; ticket->resources = resources; ticket->count = world->surface_count;
    world->image_policy = ticket;
    if (world->legacy_data) {
        qa_scene_world staging = *world;
        staging.resources = destination; staging.legacy_data = &ticket->textures;
        if (!qawl_textures_build(&staging, error)) { world_policy_dispose(ticket); return false; }
        const qawl_world *current = world->legacy_data;
        if (current->texture_count != ticket->textures.texture_count) {
            world_policy_dispose(ticket);
            return world_error(error, QA_ERROR_ARGUMENT, "Prepared brush textures differ from the actual BSP roster");
        }
        for (size_t i = 0; i < ticket->count; ++i) {
            const qaw_surface *surface = &world->surfaces[i];
            world_policy_surface *prepared = &ticket->surfaces[i];
            prepared->sort = surface->sort; prepared->sky = surface->sky;
            if (!surface->legacy || surface->legacy->warp) continue;
            size_t index = surface->legacy->texture;
            const qawl_texture *old = &current->textures[index], *next = &ticket->textures.textures[index];
            if (old->width == next->width && old->height == next->height) continue;
            if (!old->width || !old->height || !next->width || !next->height ||
                surface->mesh.revision == UINT64_MAX ||
                surface->mesh.vertex_count > SIZE_MAX / sizeof(*prepared->vertices) ||
                surface->mesh.index_count > SIZE_MAX / sizeof(*prepared->indices)) {
                world_policy_dispose(ticket);
                return world_error(error, QA_ERROR_FORMAT, "Prepared brush image dimensions cannot preserve its texture projection");
            }
            qa_bsp_face face; qa_bsp_texinfo texinfo;
            if (!qa_bsp_read_face(&world->bsp, surface->source_index, &face, error) ||
                !qa_bsp_read_texinfo(&world->bsp, face.texinfo, &texinfo, error)) {
                world_policy_dispose(ticket); return false;
            }
            prepared->vertices = surface->mesh.vertex_count ? malloc(surface->mesh.vertex_count * sizeof(*prepared->vertices)) : NULL;
            prepared->indices = surface->mesh.index_count ? malloc(surface->mesh.index_count * sizeof(*prepared->indices)) : NULL;
            if ((surface->mesh.vertex_count && !prepared->vertices) || (surface->mesh.index_count && !prepared->indices)) {
                free(prepared->vertices); free(prepared->indices); prepared->vertices = NULL; prepared->indices = NULL;
                world_policy_dispose(ticket);
                return world_error(error, QA_ERROR_MEMORY, "Preparing brush texture coordinate geometry");
            }
            if (surface->mesh.vertex_count) memcpy(prepared->vertices, surface->vertices,
                surface->mesh.vertex_count * sizeof(*prepared->vertices));
            if (surface->mesh.index_count) memcpy(prepared->indices, surface->indices,
                surface->mesh.index_count * sizeof(*prepared->indices));
            for (size_t v = 0; v < surface->mesh.vertex_count; ++v) {
                qa_vec3 point = prepared->vertices[v].position;
                float scale = next->quake64_shift && !surface->sky ? 2.0f * (float)next->quake64_shift : 1.0f;
                const float *s = texinfo.projection[0], *t = texinfo.projection[1];
                prepared->vertices[v].texcoord.x = (point.x * s[0] + point.y * s[1] + point.z * s[2] + s[3]) / ((float)next->width * scale);
                prepared->vertices[v].texcoord.y = (point.x * t[0] + point.y * t[1] + point.z * t[2] + t[3]) / ((float)next->height * scale);
                if (!isfinite(prepared->vertices[v].texcoord.x) || !isfinite(prepared->vertices[v].texcoord.y)) {
                    free(prepared->vertices); free(prepared->indices); prepared->vertices = NULL; prepared->indices = NULL;
                    world_policy_dispose(ticket);
                    return world_error(error, QA_ERROR_FORMAT, "Prepared brush texture coordinates overflow");
                }
            }
            qa_scene_geometry *geometry = qa_scene_geometry_adopt(prepared->vertices, surface->mesh.vertex_count,
                prepared->indices, surface->mesh.index_count, error);
            if (!geometry) {
                free(prepared->vertices); free(prepared->indices); prepared->vertices = NULL; prepared->indices = NULL;
                world_policy_dispose(ticket); return false;
            }
            prepared->mesh = surface->mesh; ++prepared->mesh.revision;
            prepared->mesh.geometry = geometry; prepared->mesh.vertices = prepared->vertices;
            prepared->mesh.indices = prepared->indices;
        }
    }
    *out = ticket; return true;
}
bool qa_scene_world_image_policy_base(const qa_scene_world_image_policy *ticket, uint64_t world,
    const char *name, const qa_scene_image *current, const qa_scene_image **destination)
{
    if (!destination || !name || !world_policy_current(ticket) || ticket->published ||
        ticket->owner->identity != world || !ticket->owner->legacy_data) return false;
    const qawl_world *source = ticket->owner->legacy_data;
    for (size_t i = 0; i < source->texture_count; ++i)
        if (source->textures[i].image == current && !strcmp(source->textures[i].name, name)) {
            *destination = ticket->textures.textures[i].image; return true;
        }
    return false;
}
bool qa_scene_world_image_policy_ready(qa_scene_world_image_policy *ticket,
    const qa_scene_material_image_policy *materials, qa_error *error)
{
    if (!world_policy_current(ticket) || ticket->published || !materials)
        return world_error(error, QA_ERROR_ARGUMENT, "Prepared world images lost their actual owners");
    if (ticket->sealed) return ticket->materials == materials;
    qa_bsp_materials source_materials = {0};
    if (ticket->owner->bsp.family == QA_BSP_Q3 && !qa_bsp_build_materials(&ticket->owner->bsp, &source_materials, error)) return false;
    for (size_t i = 0; i < ticket->count; ++i) {
        const qaw_surface *surface = &ticket->owner->surfaces[i];
        world_policy_surface *prepared = &ticket->surfaces[i];
        prepared->sky = surface->sky; prepared->sort = surface->sort;
        prepared->material = surface->material;
        if (surface->material) {
            const qa_material *material = NULL;
            if (!qa_scene_material_image_policy_read(materials, surface->material, &material)) {
                qa_bsp_materials_free(&source_materials);
                return world_error(error, QA_ERROR_ARGUMENT, "Prepared world lacks its actual registered material");
            }
            if (ticket->owner->bsp.family == QA_BSP_Q3) {
                qa_bsp_surface source;
                if (!qa_bsp_read_surface(&ticket->owner->bsp, surface->source_index, &source, error) ||
                    surface->source_index >= source_materials.surface_count ||
                    source_materials.surfaces[surface->source_index] >= source_materials.shader_count) {
                    qa_bsp_materials_free(&source_materials); return false;
                }
                const qa_bsp_shader *shader = source_materials.shaders + source_materials.surfaces[surface->source_index];
                char *name = qaw_string(shader->name, error);
                const qa_material *registered = NULL, *pending = NULL;
                int32_t lightmap = source.type == QA_BSP_SURFACE_PLANAR || source.type == QA_BSP_SURFACE_PATCH ? source.lightmap : -3;
                bool found = name && qa_scene_material_image_policy_world(materials, ticket->owner->identity,
                    lightmap, surface->lightmap != NULL, name, &ticket->owner->options.images, &registered, &pending, error);
                free(name);
                if (!found) { qa_bsp_materials_free(&source_materials); return false; }
                const qa_material *fallback = qa_material_find(ticket->owner->materials, "*default");
                if (surface->material == registered || surface->material == fallback) {
                    prepared->material = pending->default_shader ? fallback : registered;
                    if (!prepared->material || !qa_scene_material_image_policy_read(materials, prepared->material, &material)) {
                        qa_bsp_materials_free(&source_materials);
                        return world_error(error, QA_ERROR_ARGUMENT, "World shader fallback lacks its real prepared default");
                    }
                }
            }
            prepared->sort = material->sort;
            if (ticket->owner->bsp.family == QA_BSP_Q3) prepared->sky = material->sky;
        }
    }
    qa_bsp_materials_free(&source_materials);
    ticket->materials = materials; ticket->sealed = true; return true;
}
bool qa_scene_world_image_policy_ready_is(const qa_scene_world_image_policy *ticket)
{
    return world_policy_current(ticket) && ticket->sealed && !ticket->published &&
        qa_scene_resource_policy_ready_is(ticket->resources) &&
        qa_scene_material_image_policy_ready_is(ticket->materials);
}
void qa_scene_world_image_policy_publish(qa_scene_world_image_policy *ticket)
{
    if (!world_policy_current(ticket) || !ticket->sealed || ticket->published) return;
    qa_scene_world *world = ticket->owner;
    if (world->legacy_data) {
        qawl_world *current = world->legacy_data;
        qawl_texture *textures = current->textures; size_t count = current->texture_count;
        current->textures = ticket->textures.textures; current->texture_count = ticket->textures.texture_count;
        ticket->textures.textures = textures; ticket->textures.texture_count = count;
        for (size_t i = 0; i < 6; ++i) {
            qa_scene_image *image = current->sky[i]; current->sky[i] = ticket->textures.sky[i]; ticket->textures.sky[i] = image;
        }
    }
    for (size_t i = 0; i < ticket->count; ++i) {
        qaw_surface *surface = &world->surfaces[i]; world_policy_surface *prepared = &ticket->surfaces[i];
        surface->sky = prepared->sky; surface->sort = prepared->sort;
        surface->material = prepared->material;
        if (prepared->mesh.geometry) {
            qa_scene_mesh mesh = surface->mesh; surface->mesh = prepared->mesh; prepared->mesh = mesh;
            surface->vertices = (qa_scene_vertex *)surface->mesh.vertices;
            surface->indices = (uint32_t *)surface->mesh.indices;
        }
    }
    ticket->published = true;
}
bool qa_scene_world_image_policy_finish(qa_scene_world_image_policy **owner, qa_error *error)
{
    if (!owner || !world_policy_current(*owner) || !(*owner)->published)
        return world_error(error, QA_ERROR_ARGUMENT, "World image retirement requires its published owner");
    world_policy_dispose(*owner); *owner = NULL; return true;
}
bool qa_scene_world_image_policy_abort(qa_scene_world_image_policy **owner, qa_error *error)
{
    if (!owner || !world_policy_current(*owner) || (*owner)->published)
        return world_error(error, QA_ERROR_ARGUMENT, "World image abort requires its unpublished owner");
    world_policy_dispose(*owner); *owner = NULL; return true;
}

static void *world_array(size_t count, size_t stride, qa_error *error)
{
    if (count == 0) return NULL;
    if (count > SIZE_MAX / stride) {
        world_error(error, QA_ERROR_MEMORY, "world array exceeds address space");
        return NULL;
    }
    void *result = calloc(count, stride);
    if (result == NULL) world_error(error, QA_ERROR_MEMORY, "cannot allocate world array");
    return result;
}

static bool world_copy(qa_bytes source, qa_buffer *out, qa_error *error)
{
    if (source.size == 0) return true;
    if (source.data == NULL) return world_error(error, QA_ERROR_ARGUMENT, "missing world byte span");
    out->data = world_array(source.size, 1, error);
    if (out->data == NULL) return false;
    out->size = source.size;
    memcpy(out->data, source.data, source.size);
    return true;
}

char *qaw_string(qa_bytes bytes, qa_error *error)
{
    if (bytes.size == SIZE_MAX || (bytes.size != 0 && bytes.data == NULL)) {
        world_error(error, QA_ERROR_ARGUMENT, "invalid world string");
        return NULL;
    }
    char *result = world_array(bytes.size + 1, 1, error);
    if (result != NULL && bytes.size != 0) memcpy(result, bytes.data, bytes.size);
    return result;
}

bool qaw_mesh_allocate(qa_scene_world *world, qaw_surface *surface,
                       size_t vertices, size_t indices, qa_error *error)
{
    if (vertices > UINT32_MAX || indices > UINT32_MAX)
        return world_error(error, QA_ERROR_FORMAT, "world surface exceeds mesh index range");
    surface->vertices = world_array(vertices, sizeof(*surface->vertices), error);
    if (vertices != 0 && surface->vertices == NULL) return false;
    surface->indices = world_array(indices, sizeof(*surface->indices), error);
    if (indices != 0 && surface->indices == NULL) return false;
    qa_scene_geometry *geometry = qa_scene_geometry_adopt(surface->vertices, vertices, surface->indices, indices, error);
    if (geometry == NULL) return false;
    surface->mesh = (qa_scene_mesh){
        .identity = qa_scene_identity(),
        .revision = 1, .vertices = surface->vertices, .indices = surface->indices,
        .vertex_count = vertices, .index_count = indices, .primitive = QA_SCENE_TRIANGLES,
        .geometry = geometry
    };
    (void)world;
    return true;
}

void qaw_mesh_bounds(qaw_surface *surface)
{
    qa_bounds bounds = {qa_v3(FLT_MAX, FLT_MAX, FLT_MAX), qa_v3(-FLT_MAX, -FLT_MAX, -FLT_MAX)};
    for (size_t i = 0; i < surface->mesh.vertex_count; ++i) {
        qa_vec3 point = surface->vertices[i].position;
        bounds.mins = qa_v3(fminf(bounds.mins.x, point.x), fminf(bounds.mins.y, point.y), fminf(bounds.mins.z, point.z));
        bounds.maxs = qa_v3(fmaxf(bounds.maxs.x, point.x), fmaxf(bounds.maxs.y, point.y), fmaxf(bounds.maxs.z, point.z));
    }
    surface->mesh.bounds = surface->mesh.vertex_count == 0 ? (qa_bounds){0} : bounds;
}

qa_vec3 qaw_local_point(const qa_model_transform *transform, qa_vec3 point)
{
    qa_model_transform inverse;
    if (!qa_model_transform_inverse(transform, &inverse)) return qa_v3(0, 0, 0);
    float source[3] = {point.x, point.y, point.z}, result[3];
    qa_model_transform_point(&inverse, source, result);
    return qa_v3(result[0], result[1], result[2]);
}

qa_vec3 qaw_local_vector(const qa_model_transform *transform, qa_vec3 vector)
{
    qa_model_transform inverse;
    if (!qa_model_transform_inverse(transform, &inverse)) return qa_v3(0, 0, 0);
    float source[3] = {vector.x, vector.y, vector.z}, result[3];
    qa_model_transform_direction(&inverse, source, result);
    return qa_v3(result[0], result[1], result[2]);
}

qa_bounds qaw_transformed_bounds(qa_bounds bounds, const qa_model_transform *transform)
{
    qa_bounds result = {qa_v3(FLT_MAX, FLT_MAX, FLT_MAX), qa_v3(-FLT_MAX, -FLT_MAX, -FLT_MAX)};
    for (unsigned corner = 0; corner < 8; ++corner) {
        float local[3] = {corner & 1 ? bounds.maxs.x : bounds.mins.x,
                          corner & 2 ? bounds.maxs.y : bounds.mins.y,
                          corner & 4 ? bounds.maxs.z : bounds.mins.z}, point[3];
        qa_model_transform_point(transform, local, point);
        result.mins = qa_v3(fminf(result.mins.x, point[0]), fminf(result.mins.y, point[1]), fminf(result.mins.z, point[2]));
        result.maxs = qa_v3(fmaxf(result.maxs.x, point[0]), fmaxf(result.maxs.y, point[1]), fmaxf(result.maxs.z, point[2]));
    }
    return result;
}

static qa_bounds bsp_bounds(qa_bsp_bounds bounds)
{
    return (qa_bounds){bounds.min, bounds.max};
}

static double precise_dot(qa_vec3 a, qa_vec3 b)
{
    return (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z;
}

static bool world_topology(qa_scene_world *world, qa_error *error)
{
    world->plane_count = qa_bsp_record_count(&world->bsp, QA_BSP_PLANES);
    world->node_count = qa_bsp_record_count(&world->bsp, QA_BSP_NODES);
    world->leaf_count = qa_bsp_record_count(&world->bsp, QA_BSP_LEAVES);
    world->leaf_surface_count = qa_bsp_record_count(&world->bsp, QA_BSP_LEAF_FACES);
    world->model_count = qa_bsp_record_count(&world->bsp, QA_BSP_MODELS);
    world->surface_count = qa_bsp_record_count(&world->bsp,
        world->bsp.family == QA_BSP_Q3 ? QA_BSP_SURFACES : QA_BSP_FACES);
    if (world->node_count > INT32_MAX || world->leaf_count > INT32_MAX || world->surface_count >= UINT32_MAX)
        return world_error(error, QA_ERROR_FORMAT, "world topology exceeds signed BSP indices");
#define WORLD_ALLOC(member, count) do { \
    world->member = world_array((count), sizeof(*world->member), error); \
    if ((count) != 0 && world->member == NULL) return false; \
} while (0)
    WORLD_ALLOC(planes, world->plane_count);
    WORLD_ALLOC(nodes, world->node_count);
    WORLD_ALLOC(leaves, world->leaf_count);
    if (world->bsp.family == QA_BSP_Q3) WORLD_ALLOC(source_leaf_marks, world->leaf_count);
    WORLD_ALLOC(leaf_surfaces, world->leaf_surface_count);
    WORLD_ALLOC(models, world->model_count);
    WORLD_ALLOC(surfaces, world->surface_count);
    WORLD_ALLOC(surface_marks, world->surface_count);
    WORLD_ALLOC(visible_surfaces, world->surface_count);
    WORLD_ALLOC(surface_lights, world->surface_count);
    if (world->bsp.family == QA_BSP_Q3) WORLD_ALLOC(source_dlight_masks, world->surface_count);
    WORLD_ALLOC(admitted_surfaces, world->surface_count);
    WORLD_ALLOC(admission_changes, world->surface_count);
    world->admission_change_capacity = world->surface_count;
    world->pending_capacity = world->node_count + 1;
    WORLD_ALLOC(pending, world->pending_capacity);
#undef WORLD_ALLOC
    for (size_t i = 0; i < world->plane_count; ++i)
        if (!qa_bsp_read_plane(&world->bsp, i, &world->planes[i], error)) return false;
    for (size_t i = 0; i < world->node_count; ++i)
        if (!qa_bsp_read_node(&world->bsp, i, &world->nodes[i], error)) return false;
    for (size_t i = 0; i < world->leaf_count; ++i) {
        if (!qa_bsp_read_leaf(&world->bsp, i, &world->leaves[i], error)) return false;
        int64_t cluster = world->leaves[i].cluster;
        if (cluster > INT32_MAX) return world_error(error, QA_ERROR_FORMAT, "world cluster exceeds signed visibility index");
        if (cluster >= 0 && (uint64_t)cluster >= world->cluster_count) world->cluster_count = (uint32_t)cluster + 1;
    }
    for (size_t i = 0; i < world->leaf_surface_count; ++i) {
        int64_t index;
        if (!qa_bsp_read_index(&world->bsp, QA_BSP_LEAF_FACES, i, &index, error)) return false;
        world->leaf_surfaces[i] = (uint32_t)index;
    }
    for (size_t i = 0; i < world->surface_count; ++i) world->surfaces[i].source_index = (uint32_t)i;
    for (size_t i = 0; i < world->model_count; ++i) {
        qaw_model *model = &world->models[i];
        model->identity = qa_scene_identity();
        if (!qa_bsp_read_model(&world->bsp, i, &model->source, error)) return false;
        size_t capacity = model->source.membership_from_tree ? world->surface_count : model->source.faces.count;
        model->surface_capacity = capacity;
        model->surfaces = world_array(capacity, sizeof(*model->surfaces), error);
        if (capacity != 0 && model->surfaces == NULL) return false;
        if (world->bsp.family == QA_BSP_Q3) {
            if (!qa_bsp_model_members(&world->bsp, i, false, model->surfaces, capacity, &model->surface_count, error)) return false;
        } else {
            model->surface_count = capacity;
            for (size_t j = 0; j < capacity; ++j) model->surfaces[j] = model->source.faces.first + (uint32_t)j;
        }
    }
    world->bounds = world->model_count != 0 ? bsp_bounds(world->models[0].source.bounds) : (qa_bounds){0};
    size_t capacity = world->bsp.family == QA_BSP_Q1 ? (world->leaf_count + 7) / 8
        : ((size_t)world->cluster_count + 7) / 8;
    qa_bytes vis = world->bsp.lumps[QA_BSP_VISIBILITY].bytes;
    if (vis.size != 0 && world->bsp.family != QA_BSP_Q1) {
        size_t row = world->bsp.family == QA_BSP_Q3 ? qa_load_u32le(vis.data + 4)
            : ((size_t)qa_load_u32le(vis.data) + 7) / 8;
        if (row > capacity) capacity = row;
    }
    world->pvs_capacity = capacity;
    world->pvs = world_array(capacity, 1, error);
    if (capacity != 0 && world->pvs == NULL) return false;
    world->secondary_pvs = world_array(capacity, 1, error);
    return capacity == 0 || world->secondary_pvs != NULL;
}

bool qa_scene_world_create(const qa_bsp_view *bsp, qa_scene_resources *resources,
                           qa_material_library *materials, const qa_scene_world_options *options,
                           qa_scene_world **out, qa_error *error)
{
    if (out != NULL) *out = NULL;
    if (bsp == NULL || resources == NULL || materials == NULL || out == NULL)
        return world_error(error, QA_ERROR_ARGUMENT, "world creation requires BSP, resources, materials and output");
    qa_scene_world *world = calloc(1, sizeof(*world));
    if (world == NULL) return world_error(error, QA_ERROR_MEMORY, "cannot allocate scene world");
    if (!qaw_world_owners_retain(world,resources,materials,error)) goto fail;
    world->options = options != NULL ? *options : (qa_scene_world_options){
        .subdivisions = 4, .q1_water_alpha = 1, .q2_light_modulate = 1, .q3_overbright = 2,
        .images = {.mipmap = true, .transparent_index = -1, .filter = QA_SCENE_LINEAR_MIPMAP_NEAREST}
    };
    world->options.images.family = bsp->family == QA_BSP_Q1 ? QA_SCENE_Q1
        : bsp->family == QA_BSP_Q2 ? QA_SCENE_Q2 : QA_SCENE_Q3;
    world->identity = qa_scene_identity();
    world->revision = 1;
    if (!world_copy(bsp->source, &world->bytes, error)
        || !qa_bsp_open((qa_bytes){world->bytes.data, world->bytes.size}, &world->bsp, error)
        || !qa_bsp_validate(&world->bsp, error)
        || !world_copy(world->options.external_lit, &world->lit_bytes, error)
        || !world_copy(world->options.images.palette_rgb, &world->palette_bytes, error)
        || !world_copy(world->options.images.translation, &world->translation_bytes, error)) goto fail;
    const qa_bsp_lump *incoming = &bsp->lumps[QA_BSP_ENTITIES];
    const qa_bsp_lump *embedded = &world->bsp.lumps[QA_BSP_ENTITIES];
    bool substituted = incoming->present && (incoming->bytes.size != embedded->bytes.size ||
        incoming->offset != embedded->offset || incoming->bytes.data != bsp->source.data + embedded->offset);
    if (substituted || world->options.has_external_entities) {
        qa_bytes entities = world->options.has_external_entities ? world->options.external_entities : incoming->bytes;
        if (bsp->family != QA_BSP_Q1 || (entities.size && !entities.data) ||
            (substituted && world->options.has_external_entities &&
                (entities.size != incoming->bytes.size || (entities.size && memcmp(entities.data, incoming->bytes.data, entities.size))))) {
            world_error(error, QA_ERROR_ARGUMENT, "Scene entity override differs from its actual Q1 map admission"); goto fail;
        }
        if (!world_copy(entities, &world->entity_bytes, error)) goto fail;
        world->options.has_external_entities = true;
        world->bsp.lumps[QA_BSP_ENTITIES] = (qa_bsp_lump){.bytes = {world->entity_bytes.data, world->entity_bytes.size}, .present = true};
    } else if (world->options.external_entities.size || world->options.external_entities.data) {
        world_error(error, QA_ERROR_ARGUMENT, "Scene entity bytes lack an actual override admission"); goto fail;
    }
    world->options.external_entities = (qa_bytes){world->entity_bytes.data, world->entity_bytes.size};
    world->options.external_lit = (qa_bytes){world->lit_bytes.data, world->lit_bytes.size};
    world->options.images.palette_rgb = (qa_bytes){world->palette_bytes.data, world->palette_bytes.size};
    world->options.images.translation = (qa_bytes){world->translation_bytes.data, world->translation_bytes.size};
    if (world->options.q2_sky != NULL) {
        world->sky_name = qaw_string((qa_bytes){(const uint8_t *)world->options.q2_sky, strlen(world->options.q2_sky)}, error);
        if (world->sky_name == NULL) goto fail;
        world->options.q2_sky = world->sky_name;
    }
    if (!world_topology(world, error)) goto fail;
    if (world->bsp.family != QA_BSP_Q3) {
        if (!qa_bsp_select_lighting(&world->bsp, world->options.external_lit, &world->lighting, error)) goto fail;
        qa_bsp_extension extension;
        if (qa_bsp_find_extension(&world->bsp, "LIGHTGRID_OCTREE", &extension, NULL)
            && !qa_bsp_read_lightgrid(&world->bsp, &world->lightgrid, error)) goto fail;
    }
    if (world->bsp.family == QA_BSP_Q3 ? !qaw_build_q3(world, error) : !qaw_build_legacy(world, error)) goto fail;
    *out = world;
    return true;
fail:
    qa_scene_world_destroy(world);
    return false;
}

void qa_scene_world_destroy(qa_scene_world *world)
{
    if (world && world->references>1) { --world->references; return; }
    if (!qa_scene_world_idle(world)) return;
    if (world->bsp.family == QA_BSP_Q3) qaw_destroy_q3(world);
    else qaw_destroy_legacy(world);
    for (size_t i = 0; i < world->surface_count && world->surfaces != NULL; ++i) {
        if (world->surfaces[i].mesh.geometry != NULL)
            qa_scene_geometry_release(world->surfaces[i].mesh.geometry);
        else {
            free(world->surfaces[i].vertices);
            free(world->surfaces[i].indices);
        }
        qa_scene_image_release(world->surfaces[i].lightmap);
    }
    for (size_t i = 0; i < world->model_count && world->models != NULL; ++i) free(world->models[i].surfaces);
    qa_bsp_lightgrid_free(&world->lightgrid);
    free(world->planes); free(world->nodes); free(world->leaves); free(world->leaf_surfaces);
    free(world->source_leaf_marks);
    free(world->source_dlight_masks);
    free(world->models); free(world->surfaces); free(world->surface_marks);
    free(world->visible_surfaces); free(world->surface_lights); free(world->admitted_surfaces);
    free(world->admission_changes); free(world->pending);
    free(world->pvs); free(world->secondary_pvs); free(world->sky_name);
    qa_buffer_free(&world->bytes); qa_buffer_free(&world->lit_bytes); qa_buffer_free(&world->entity_bytes);
    qa_buffer_free(&world->palette_bytes); qa_buffer_free(&world->translation_bytes);
    qa_resource_release((qa_resource *)world->source_resource);
    qa_material_library_destroy(world->retained_materials);
    qa_scene_resources_destroy(world->retained_resources);
    free(world);
}

int32_t qa_scene_world_leaf(const qa_scene_world *world, qa_vec3 point)
{
    if (world == NULL || !qa_vec_finite(point) || world->leaf_count == 0) return -1;
    if (world->node_count == 0) return 0;
    int32_t child = 0;
    while (child >= 0) {
        const qa_bsp_node *node = &world->nodes[child];
        const qa_bsp_plane *plane = &world->planes[node->plane];
        child = node->children[precise_dot(point, plane->normal) > plane->distance ? 0 : 1];
    }
    return (int32_t)(-1 - (int64_t)child);
}

bool qa_scene_world_q1_contents(const qa_scene_world *world,qa_vec3 origin,int32_t *out,qa_error *error)
{
    if (!world || !out || world->bsp.family!=QA_BSP_Q1 || !qa_vec_finite(origin))
        return world_error(error,QA_ERROR_ARGUMENT,"Q1 camera contents require its actual BSP world and finite eye");
    int32_t leaf=qa_scene_world_leaf(world,origin);
    if (leaf<0 || (size_t)leaf>=world->leaf_count)
        return world_error(error,QA_ERROR_FORMAT,"Q1 camera eye has no actual BSP leaf");
    *out=world->leaves[leaf].contents; return true;
}
bool qa_scene_world_source_begin_scene(qa_scene_world *world,
    const qa_scene_world_input *input, qa_error *error)
{
    if (!qa_scene_world_idle(world) || world->restore_pending || !input || !input->source_order ||
        (input->visible_area_bytes && !input->visible_areas) ||
        input->visible_area_bytes > sizeof(world->source_area_mask))
        return world_error(error, QA_ERROR_ARGUMENT, "Source scene area mask requires its idle actual world");
    if (world->bsp.family != QA_BSP_Q3) return true;
    world->source_area_mask_modified = false;
    if (input->no_world) return true;
    for (size_t i = 0; i < sizeof(world->source_area_mask); ++i) {
        uint8_t mask = input->visible_areas && i < input->visible_area_bytes ?
            (uint8_t)~input->visible_areas[i] : 0;
        world->source_area_mask_modified |= mask != world->source_area_mask[i];
        world->source_area_mask[i] = mask;
    }
    return true;
}

static bool update_pvs(qa_scene_world *world, int32_t eye, qa_vec3 origin,
                        const qa_scene_world_input *input, qa_error *error)
{
    int32_t selector = world->bsp.family == QA_BSP_Q1 ? eye : (int32_t)world->leaves[eye].cluster;
    int32_t second = -1;
    if (world->bsp.family == QA_BSP_Q2) {
        if (input->use_secondary_cluster) second = input->secondary_cluster;
        else {
            origin.z += world->leaves[eye].contents == 0 ? -16.0f : 16.0f;
            int32_t probe = qa_scene_world_leaf(world, origin);
            if (probe >= 0 && (world->leaves[probe].contents & 1) == 0
                && world->leaves[probe].cluster != selector) second = (int32_t)world->leaves[probe].cluster;
        }
    }
    if (world->pvs_cached && selector == world->pvs_selector && second == world->pvs_secondary) return true;
    world->pvs_cached = false;
    world->pvs_all = selector < 0 || world->bsp.lumps[QA_BSP_VISIBILITY].bytes.size == 0;
    qa_bytes vis = world->bsp.lumps[QA_BSP_VISIBILITY].bytes;
    if (!world->pvs_all && world->bsp.family == QA_BSP_Q2) {
        uint32_t clusters = qa_load_u32le(vis.data);
        if ((uint32_t)selector >= clusters || (second >= 0 && (uint32_t)second >= clusters))
            return world_error(error, QA_ERROR_ARGUMENT, "scene PVS cluster is outside map visibility");
        world->pvs_all = qa_load_i32le(vis.data + 4 + (size_t)selector * 8) < 0
            || (second >= 0 && qa_load_i32le(vis.data + 4 + (size_t)second * 8) < 0);
    }
    if (!world->pvs_all && world->bsp.family == QA_BSP_Q3
        && (uint32_t)selector >= qa_load_u32le(vis.data)) world->pvs_all = true;
    if (!world->pvs_all) {
        if (!qa_bsp_visibility(&world->bsp, selector, false, world->cluster_count,
            world->pvs, world->pvs_capacity, &world->pvs_size, error)) return false;
        if (world->bsp.family == QA_BSP_Q2 && second >= 0) {
            size_t bytes;
            if (!qa_bsp_visibility(&world->bsp, second, false, world->cluster_count,
                world->secondary_pvs, world->pvs_capacity, &bytes, error)) return false;
            if (bytes != world->pvs_size) return world_error(error, QA_ERROR_FORMAT, "secondary PVS width differs");
            for (size_t i = 0; i < bytes; ++i) world->pvs[i] |= world->secondary_pvs[i];
        }
    }
    world->pvs_selector = selector;
    world->pvs_secondary = second;
    world->pvs_cached = true;
    return true;
}

static bool remaining_planes(qa_bounds bounds, const qa_scene_plane *planes,
                              size_t count, uint32_t *bits)
{
    for (size_t i = 0; i < count; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        if ((*bits & bit) == 0) continue;
        qa_vec3 n = planes[i].normal;
        qa_vec3 front = qa_v3(n.x < 0 ? bounds.mins.x : bounds.maxs.x,
            n.y < 0 ? bounds.mins.y : bounds.maxs.y, n.z < 0 ? bounds.mins.z : bounds.maxs.z);
        if ((float)(precise_dot(front, n) - planes[i].distance) < 0) return false;
        qa_vec3 back = qa_v3(n.x < 0 ? bounds.maxs.x : bounds.mins.x,
            n.y < 0 ? bounds.maxs.y : bounds.mins.y, n.z < 0 ? bounds.maxs.z : bounds.mins.z);
        if ((float)(precise_dot(back, n) - planes[i].distance) >= 0) *bits &= ~bit;
    }
    return true;
}

static uint32_t all_lights(size_t count)
{
    return count >= 32 ? UINT32_MAX : (UINT32_C(1) << count) - 1;
}

static bool source_mark_leaves(qa_scene_world *world, int32_t eye, qa_vec3 origin,
    const qa_scene_world_input *input, qa_error *error)
{
    if (input->lock_pvs) return true;
    int32_t cluster = (int32_t)world->leaves[eye].cluster;
    bool modified = input->source_show_cluster_modified;
    if (input->source_cluster_modified &&
        !input->source_cluster_modified(input->source_cluster_context, &modified, error)) return false;
    if (world->source_view_cluster == cluster && !world->source_area_mask_modified &&
        !modified) return true;
    if (modified || input->source_show_cluster) {
        if (input->source_cluster_clear && !input->source_cluster_clear(input->source_cluster_context, error)) return false;
        if (input->source_show_cluster && input->source_cluster_print) {
            char message[96];
            snprintf(message, sizeof(message), "cluster:%d  area:%" PRId64 "\n", cluster, world->leaves[eye].area);
            input->source_cluster_print(input->source_cluster_print_context, message);
        }
    }
    if (++world->source_vis_generation == 0) {
        memset(world->source_leaf_marks, 0, world->leaf_count * sizeof(*world->source_leaf_marks));
        world->source_vis_generation = 1;
    }
    world->source_view_cluster = cluster;
    bool all = input->no_vis || cluster == -1;
    if (!all && !update_pvs(world, eye, origin, input, error)) return false;
    for (size_t i = 0; i < world->leaf_count; ++i) {
        const qa_bsp_leaf *leaf = &world->leaves[i];
        if (!all) {
            int64_t bit = leaf->cluster;
            if (bit < 0 || (uint64_t)bit >= world->cluster_count) continue;
            if (!world->pvs_all && ((uint64_t)bit / 8 >= world->pvs_size ||
                !(world->pvs[(size_t)bit / 8] & (1u << ((unsigned)bit & 7))))) continue;
            if (leaf->area < 0 || (uint64_t)leaf->area / 8 >= sizeof(world->source_area_mask) ||
                (world->source_area_mask[(size_t)leaf->area / 8] & (1u << ((unsigned)leaf->area & 7)))) continue;
        }
        /* Q3 render leaves have zero contents, including cluster -1. The
         * source novis branch marks all non-solid nodes without area checks. */
        if (leaf->contents != 1) world->source_leaf_marks[i] = world->source_vis_generation;
    }
    return true;
}

static const qa_scene_light *projected_lights(const qa_scene_world *world,
                                              const qa_scene_world_input *input, size_t *count)
{
    if (input->use_projected_lights) {
        *count = input->projected_light_count;
        return input->projected_lights;
    }
    *count = world->bsp.family == QA_BSP_Q3 ? input->light_count : 0;
    return *count != 0 ? input->lights : NULL;
}

static bool world_visible(qa_scene_world *world, const qa_scene_world_input *input,
    qa_bounds *visible_bounds, qa_error *error)
{
    world->visible_count = 0;
    qa_vec3 origin = input->use_pvs_origin ? input->pvs_origin : input->view.origin;
    int32_t eye = qa_scene_world_leaf(world, origin);
    if (eye < 0) return true;
    bool source = world->bsp.family == QA_BSP_Q3 && input->source_order;
    if (source) {
        if (input->source_scratch) input->source_scratch->owner->counters.view_cluster=(int32_t)world->leaves[eye].cluster;
        if (!source_mark_leaves(world, eye, origin, input, error)) return false;
    } else if (!input->no_vis && !update_pvs(world, eye, origin, input, error)) return false;
    if (++world->visibility_generation == 0) {
        memset(world->surface_marks, 0, world->surface_count * sizeof(*world->surface_marks));
        world->visibility_generation = 1;
    }
    qa_scene_plane planes[6];
    size_t plane_count = input->no_cull ? 0 : qa_scene_frustum(&input->view, planes);
    if (world->bsp.family == QA_BSP_Q3 && plane_count > 4) plane_count = 4;
    size_t pending = 1;
    size_t light_count;
    const qa_scene_light *lights = projected_lights(world, input, &light_count);
    world->pending[0] = (qaw_pending){world->node_count == 0 ? -1 : 0,
        all_lights(light_count), (UINT32_C(1) << plane_count) - 1};
    while (pending != 0) {
        qaw_pending item = world->pending[--pending];
        if (item.child >= 0) {
            const qa_bsp_node *node = &world->nodes[item.child];
            if (!remaining_planes(bsp_bounds(node->bounds), planes, plane_count, &item.planes)) continue;
            uint32_t masks[2] = {item.lights, item.lights};
            unsigned front = 0;
            const qa_bsp_plane *plane = &world->planes[node->plane];
            if (world->bsp.family == QA_BSP_Q3) {
                masks[0] = masks[1] = 0;
                for (size_t i = 0; i < light_count; ++i) {
                    uint32_t bit = UINT32_C(1) << i;
                    if ((item.lights & bit) == 0) continue;
                    float distance = (float)(precise_dot(lights[i].origin, plane->normal) - plane->distance);
                    if (distance > -lights[i].radius) masks[0] |= bit;
                    if (distance < lights[i].radius) masks[1] |= bit;
                }
            } else front = precise_dot(input->view.origin, plane->normal) >= plane->distance ? 0u : 1u;
            world->pending[pending++] = (qaw_pending){node->children[front ^ 1], masks[front ^ 1], item.planes};
            world->pending[pending++] = (qaw_pending){node->children[front], masks[front], item.planes};
            continue;
        }
        size_t index = (size_t)(-1 - (int64_t)item.child);
        const qa_bsp_leaf *leaf = &world->leaves[index];
        if (world->bsp.family == QA_BSP_Q1 && index == 0) continue;
        if (source && world->source_leaf_marks[index] != world->source_vis_generation) continue;
        if (!source && world->bsp.family != QA_BSP_Q1 && input->visible_areas != NULL
            && (leaf->area < 0 || (uint64_t)leaf->area / 8 >= input->visible_area_bytes
                || (input->visible_areas[(size_t)leaf->area / 8] & (1u << ((unsigned)leaf->area & 7))) == 0)) continue;
        if (!source && world->bsp.family == QA_BSP_Q3 && (input->no_vis || world->leaves[eye].cluster < 0) && leaf->cluster == -1) continue;
        if (!source && !input->no_vis && !world->pvs_all) {
            int64_t bit = world->bsp.family == QA_BSP_Q1 ? (int64_t)index - 1 : leaf->cluster;
            if (bit < 0 || (uint64_t)bit / 8 >= world->pvs_size
                || (world->pvs[(size_t)bit / 8] & (1u << ((unsigned)bit & 7))) == 0) continue;
        }
        if (!remaining_planes(bsp_bounds(leaf->bounds), planes, plane_count, &item.planes)) continue;
        if (source && input->source_scratch) ++input->source_scratch->owner->counters.leaves;
        if (visible_bounds) *visible_bounds = qa_bounds_union(*visible_bounds, bsp_bounds(leaf->bounds));
        for (size_t i = leaf->faces.first; i < (size_t)leaf->faces.first + leaf->faces.count; ++i) {
            uint32_t surface = world->leaf_surfaces[i];
            if (world->surface_marks[surface] == world->visibility_generation) continue;
            world->surface_marks[surface] = world->visibility_generation;
            world->surface_lights[surface] = item.lights;
            world->visible_surfaces[world->visible_count++] = surface;
        }
    }
    return true;
}

static bool source_view_current(const qa_scene_source_world_view *view,
    const qa_scene_world *world, const qa_scene_world_input *input, const qa_scene_frame *frame)
{
    return view && view->world == world && view->frame == frame && view->sequence == frame->sequence &&
        !memcmp(&view->origin, &input->view.origin, sizeof(view->origin)) &&
        !memcmp(view->axis, input->view.axis, sizeof(view->axis)) &&
        view->projection_x == input->view.projection.m[0] && view->projection_y == input->view.projection.m[5];
}

bool qa_scene_world_source_prepare_view(qa_scene_world *world, qa_scene_world_input *input,
    qa_scene_frame *frame, qa_error *error)
{
    if (!qa_scene_world_idle(world) || world->restore_pending || !input || !frame || !input->source_order)
        return world_error(error, QA_ERROR_ARGUMENT, "Source visibility preparation requires its actual idle world and frame");
    if (!valid_input(world, input, error)) return false;
    qa_scene_source_world_view *view = qa_arena_alloc(&frame->storage, sizeof(*view),
        _Alignof(qa_scene_source_world_view), error);
    if (!view) return false;
    *view = (qa_scene_source_world_view){.world = world, .frame = frame, .sequence = frame->sequence,
        .origin = input->view.origin, .axis = {input->view.axis[0], input->view.axis[1], input->view.axis[2]},
        .projection_x = input->view.projection.m[0], .projection_y = input->view.projection.m[5],
        .no_cull = input->no_cull, .no_curves = input->no_curves,
        .disable_face_plane_cull = input->disable_face_plane_cull};
    if (input->skip_world && input->source_visibility && input->source_visibility->world == world &&
        input->source_visibility->frame == frame && input->source_visibility->sequence == frame->sequence)
        view->bounds = input->source_visibility->bounds;
    if (!input->no_world && !input->skip_world) {
        view->bounds = (qa_bounds){qa_v3(99999,99999,99999), qa_v3(-99999,-99999,-99999)};
        if (!world_visible(world, input, &view->bounds, error)) return false;
        size_t count = world->visible_count;
        if (count) {
            view->surfaces = qa_arena_alloc(&frame->storage, count * sizeof(*view->surfaces),
                _Alignof(uint32_t), error);
            view->lights = qa_arena_alloc(&frame->storage, count * sizeof(*view->lights),
                _Alignof(uint32_t), error);
            if (world->bsp.family == QA_BSP_Q3)
                view->culls = qa_arena_alloc(&frame->storage, count * sizeof(*view->culls),
                    _Alignof(qa_scene_cull), error);
            if (!view->surfaces || !view->lights || (world->bsp.family == QA_BSP_Q3 && !view->culls)) return false;
            qa_scene_plane planes[6];
            size_t plane_count = input->no_cull ? 0 : qa_scene_frustum(&input->view, planes);
            if (world->bsp.family == QA_BSP_Q3 && plane_count > 4) plane_count = 4;
            qa_material_context context = {.local_view_origin = input->view.origin};
            size_t light_count;
            const qa_scene_light *lights = projected_lights(world, input, &light_count);
            for (size_t i = 0; i < count; ++i) {
                uint32_t index = world->visible_surfaces[i];
                const qaw_surface *surface = &world->surfaces[index];
                if (surface_culled(world, surface, input, &context, NULL, planes, plane_count)) continue;
                uint32_t incoming = world->surface_lights[index];
                uint32_t mask = surface_light_mask(surface, incoming, lights, light_count);
                if (input->source_scratch) {
                    if (mask) ++input->source_scratch->owner->counters.dlight_surfaces;
                    else if (incoming) ++input->source_scratch->owner->counters.dlight_culled;
                }
                if (world->bsp.family == QA_BSP_Q3 && incoming) world->source_dlight_masks[index] = mask;
                view->surfaces[view->count] = index;
                if (view->culls)
                    view->culls[view->count] = surface->material ? surface->material->cull : QA_CULL_NONE;
                view->lights[view->count++] = mask;
            }
        }
    }
    float maximum = 0;
    for (unsigned i = 0; i < 8; ++i) {
        qa_vec3 corner = qa_v3(i & 1 ? view->bounds.mins.x : view->bounds.maxs.x,
            i & 2 ? view->bounds.mins.y : view->bounds.maxs.y,
            i & 4 ? view->bounds.mins.z : view->bounds.maxs.z);
        qa_vec3 delta = qa_vec_sub(corner, input->view.origin);
        float distance = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        if (distance > maximum) maximum = distance;
    }
    input->source_far_clip = input->no_world ? 2048 : sqrtf(maximum);
    if (input->source_scratch) input->source_scratch->owner->counters.far_clip=input->source_far_clip;
    input->source_visibility = view;
    return true;
}

qa_bounds qa_scene_world_bounds(const qa_scene_world *world)
{
    return world != NULL ? world->bounds : (qa_bounds){0};
}

bool qa_scene_world_sample_light(const qa_scene_world *world, qa_vec3 point,
                                 qa_vec3 *ambient, qa_vec3 *directed, qa_vec3 *direction)
{
    if (world == NULL || ambient == NULL || directed == NULL || direction == NULL || !qa_vec_finite(point)) return false;
    *ambient = qa_v3(1, 1, 1);
    *directed = qa_v3(0, 0, 0);
    *direction = qa_v3(0, 0, 1);
    return world->bsp.family == QA_BSP_Q3 ? qaw_sample_q3_light(world, point, ambient, directed, direction)
        : qaw_sample_legacy_light(world, point, ambient, directed, direction);
}

static bool valid_input(const qa_scene_world *world, const qa_scene_world_input *input, qa_error *error)
{
    if (world == NULL || input == NULL)
        return world_error(error, QA_ERROR_ARGUMENT, "world submission requires world and input");
    if (world->restore_pending || world->checkpoint_active || world->capture || world->image_policy)
        return world_error(error, QA_ERROR_ARGUMENT, "world continuation callback is active");
    if (!qa_vec_finite(input->view.origin) || !isfinite(input->seconds)
        || input->legacy_phase < QA_LEGACY_WORLD_ALL || input->legacy_phase > QA_LEGACY_WORLD_ALPHA
        || (input->use_pvs_origin && !qa_vec_finite(input->pvs_origin))
        || (input->light_count != 0 && input->lights == NULL)
        || (input->shadow_light_count != 0 && input->shadow_lights == NULL)
        || (input->render_text_count != 0 && input->render_texts == NULL))
        return world_error(error, QA_ERROR_ARGUMENT, "invalid world view or light inputs");
    size_t projected_count;
    const qa_scene_light *projected = projected_lights(world, input, &projected_count);
    if (projected_count > 32 || (projected_count != 0 && projected == NULL))
        return world_error(error, QA_ERROR_ARGUMENT, "projected world lights exceed source 32-light mask or are absent");
    for (size_t i = 0; i < projected_count; ++i)
        if (!qa_vec_finite(projected[i].origin) || !qa_vec_finite(projected[i].color)
            || !isfinite(projected[i].radius) || projected[i].radius < 0)
            return world_error(error, QA_ERROR_ARGUMENT, "invalid projected world light");
    for (size_t i = 0; i < input->light_count; ++i) {
        const qa_scene_light *light = &input->lights[i];
        if (!qa_vec_finite(light->origin) || !qa_vec_finite(light->color) || !isfinite(light->radius)
            || light->radius < 0 || !isfinite(light->minimum))
            return world_error(error, QA_ERROR_ARGUMENT, "invalid world dynamic light");
    }
    return true;
}

static bool begin_admission(qa_scene_world *world, qa_scene_frame *frame, qa_error *error)
{
    size_t view = frame->command_count;
    while (view != 0 && frame->commands[view - 1].kind != QA_SCENE_COMMAND_VIEW) --view;
    if (world->admission_frame == frame && world->admission_sequence == frame->sequence
        && world->admission_view == view && world->admission_generation != 0) return true;
    if (world->admission_generation == UINT64_MAX)
        return world_error(error, QA_ERROR_MEMORY, "world view admission identity space exhausted");
    world->admission_frame = frame;
    world->admission_sequence = frame->sequence;
    world->admission_view = view;
    ++world->admission_generation;
    return true;
}

static bool admit_surface(qa_scene_world *world, uint32_t index, bool *admitted, qa_error *error)
{
    *admitted = false;
    if (world->admitted_surfaces[index] == world->admission_generation) return true;
    if (world->admission_change_count == world->admission_change_capacity) {
        size_t capacity = world->admission_change_capacity;
        if (capacity > SIZE_MAX / 2 / sizeof(*world->admission_changes))
            return world_error(error, QA_ERROR_MEMORY, "surface admission journal exceeds address space");
        capacity = capacity == 0 ? 1 : capacity * 2;
        qaw_admission_change *replacement = realloc(world->admission_changes, capacity * sizeof(*replacement));
        if (replacement == NULL) return world_error(error, QA_ERROR_MEMORY, "cannot grow surface admission journal");
        world->admission_changes = replacement;
        world->admission_change_capacity = capacity;
    }
    world->admission_changes[world->admission_change_count++] = (qaw_admission_change){index, world->admitted_surfaces[index]};
    world->admitted_surfaces[index] = world->admission_generation;
    *admitted = true;
    return true;
}

static const qa_material *effective_material(const qa_material *material)
{
    return material && material->remapped ? material->remapped : material;
}

static bool local_bounds_visible(qa_bounds bounds, const qa_model_transform *transform,
                                  const qa_scene_plane *planes, size_t count)
{
    for (size_t plane = 0; plane < count; ++plane) {
        bool visible = false;
        for (unsigned corner = 0; corner < 8; ++corner) {
            float local[3] = {corner & 1 ? bounds.maxs.x : bounds.mins.x,
                corner & 2 ? bounds.maxs.y : bounds.mins.y, corner & 4 ? bounds.maxs.z : bounds.mins.z};
            float point[3] = {local[0], local[1], local[2]};
            if (transform != NULL) qa_model_transform_point(transform, local, point);
            if (precise_dot(qa_v3(point[0], point[1], point[2]), planes[plane].normal) > planes[plane].distance) {
                visible = true;
                break;
            }
        }
        if (!visible) return false;
    }
    return true;
}

static bool surface_culled(const qa_scene_world *world, const qaw_surface *surface,
                           const qa_scene_world_input *input, const qa_material_context *context,
                           const qa_model_transform *transform, const qa_scene_plane *planes, size_t count)
{
    if (input->no_cull) return false;
    if (world->bsp.family != QA_BSP_Q3)
        return transform == NULL && !qa_scene_bounds_visible(surface->mesh.bounds, planes, count);
    if (surface->skip || surface->flare) return false;
    if (surface->type == QA_BSP_SURFACE_TRIANGLES)
        return !local_bounds_visible(surface->mesh.bounds, transform, planes, count);
    if (surface->type == QA_BSP_SURFACE_PATCH) {
        if (input->source_order && input->no_curves) return true;
        qa_vec3 center = qa_vec_scale(qa_vec_add(surface->mesh.bounds.mins, surface->mesh.bounds.maxs), 0.5f);
        float radius = qa_vec_length(qa_vec_sub(surface->mesh.bounds.mins, center));
        if (transform != NULL) {
            float source[3] = {center.x, center.y, center.z}, result[3];
            qa_model_transform_point(transform, source, result);
            center = qa_v3(result[0], result[1], result[2]);
            radius *= fmaxf(fabsf(transform->scale[0]), fmaxf(fabsf(transform->scale[1]), fabsf(transform->scale[2])));
        }
        bool clipped = false;
        for (size_t i = 0; i < count; ++i) {
            float distance = (float)(precise_dot(center, planes[i].normal) - planes[i].distance);
            if (distance < -radius) {
                if (input->source_scratch) ++input->source_scratch->owner->counters.patch_sphere[2];
                return true;
            }
            if (distance <= radius) clipped = true;
        }
        if (input->source_scratch) ++input->source_scratch->owner->counters.patch_sphere[clipped?1:0];
        if (!clipped) return false;
        bool visible=local_bounds_visible(surface->mesh.bounds, transform, planes, count),inside=true;
        if (visible) for (size_t plane=0;plane<count;++plane) for (unsigned corner=0;corner<8;++corner) {
            float local[3]={corner&1?surface->mesh.bounds.maxs.x:surface->mesh.bounds.mins.x,
                corner&2?surface->mesh.bounds.maxs.y:surface->mesh.bounds.mins.y,
                corner&4?surface->mesh.bounds.maxs.z:surface->mesh.bounds.mins.z};
            float point[3]={local[0],local[1],local[2]};
            if (transform) qa_model_transform_point(transform,local,point);
            if (precise_dot(qa_v3(point[0],point[1],point[2]),planes[plane].normal)<=planes[plane].distance) inside=false;
        }
        if (input->source_scratch) ++input->source_scratch->owner->counters.patch_box[!visible?2:inside?0:1];
        return !visible;
    }
    if (input->source_order && input->disable_face_plane_cull) return false;
    const qa_material *material = input->source_order ? surface->material : effective_material(surface->material);
    if (!surface->has_plane || material == NULL || material->cull == QA_CULL_NONE) return false;
    double viewer = precise_dot(context->local_view_origin, surface->plane.normal);
    return material->cull == QA_CULL_FRONT ? viewer < (float)(surface->plane.distance - 8.0f)
        : viewer > (float)(surface->plane.distance + 8.0f);
}

static uint32_t surface_light_mask(const qaw_surface *surface, uint32_t mask,
                                    const qa_scene_light *lights, size_t count)
{
    if (surface->skip || surface->flare) return 0;
    if (surface->type == QA_BSP_SURFACE_TRIANGLES) return mask;
    for (size_t i = 0; i < count && i < 32; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        if ((mask & bit) == 0) continue;
        const qa_scene_light *light = &lights[i];
        if (surface->has_plane && surface->type != QA_BSP_SURFACE_PATCH) {
            float distance = (float)(precise_dot(light->origin, surface->plane.normal) - surface->plane.distance);
            if (distance < -light->radius || distance > light->radius) mask &= ~bit;
        } else {
            qa_vec3 o = light->origin, min = surface->mesh.bounds.mins, max = surface->mesh.bounds.maxs;
            float r = light->radius;
            if (o.x - r > max.x || o.x + r < min.x || o.y - r > max.y || o.y + r < min.y
                || o.z - r > max.z || o.z + r < min.z) mask &= ~bit;
        }
    }
    return mask;
}

static qa_material_context world_context(const qa_scene_world *world, const qa_scene_world_input *input)
{
    qa_material_context context = {
        .view = input->view, .entity_color = {1, 1, 1, 1}, .local_view_origin = input->view.origin,
        .identity_light = input->identity_light, .seconds = input->seconds, .milliseconds = input->milliseconds,
        .fog = input->fog,
        .entity = 1022,
        .mirror = input->view.mirror, .texts = input->render_texts,
        .text_count = input->render_text_count, .video_frame = input->video_frame,
        .video_context = input->video_context, .source_primitives = input->source_order,
        .source_scratch = input->source_scratch, .source_diagnostics = input->source_diagnostics,
        .source_diagnostics_read = input->source_diagnostics_read,
        .source_diagnostics_context = input->source_diagnostics_context,
        .source_white = qa_scene_white(world->resources),
        .source_recipient_image = input->source_recipient_image,
        .source_recipient_context = input->source_recipient_context
    };
    if (context.source_primitives) context.seconds = (float)input->milliseconds * .001f;
    context.lights = projected_lights(world, input, &context.light_count);
    qa_scene_matrix_identity(&context.model);
    return context;
}

static bool fragment_context(const qa_scene_world *world, const qa_scene_world_input *input,
                               qa_scene_frame *frame, qa_material_context *context, qa_error *error)
{
    if (world->bsp.family != QA_BSP_Q3 && input->legacy_policy.present &&
        (input->legacy_policy.fullbright || !input->legacy_policy.dynamic)) {
        context->fragment_lighting = false;
        context->fragment_light_count = 0;
        return true;
    }
    context->fragment_lighting = input->shadow_lights != NULL;
    context->fragment_light_count = input->shadow_light_count;
    context->shadow_atlas = input->shadow_atlas;
    context->shadow_near = 4;
    if (input->shadow_light_count == 0) return true;
    if (input->shadow_light_count > SIZE_MAX / sizeof(qa_scene_shadow_light))
        return world_error(error, QA_ERROR_MEMORY, "fragment lights exceed address space");
    qa_scene_shadow_light *lights = qa_arena_alloc(&frame->storage,
        input->shadow_light_count * sizeof(*lights), _Alignof(qa_scene_shadow_light), error);
    if (lights == NULL) return false;
    for (size_t i = 0; i < input->shadow_light_count; ++i) {
        lights[i] = input->shadow_lights[i];
        lights[i].light.scale *= world->bsp.family != QA_BSP_Q3 && input->legacy_policy.present ?
            input->legacy_policy.modulate : world->options.q2_light_modulate;
    }
    context->fragment_lights = lights;
    return true;
}

typedef struct surface_order { uint32_t surface; size_t ordinal; float sort; } surface_order;

struct qa_scene_q1_mirror {
    const qa_scene_world *world;
    const qa_scene_frame *frame;
    uint64_t identity, revision, sequence;
    size_t texture, count;
    const uint32_t *surfaces;
    qa_scene_view parent, reflected;
};
typedef struct mirror_walk { int32_t child; bool faces; } mirror_walk;

static bool mirror_current(const qa_scene_world *world, const qa_scene_q1_mirror *mirror,
                            const qa_scene_frame *frame)
{
    return mirror && mirror->world == world && mirror->frame == frame &&
        mirror->identity == world->identity && mirror->revision == world->revision &&
        mirror->sequence == frame->sequence && world->bsp.family == QA_BSP_Q1;
}

bool qa_scene_world_q1_mirror_scope(const qa_scene_world *world, const qa_scene_world_input *input,
                                    const qa_scene_frame *frame)
{
    return world && input && frame && mirror_current(world, input->q1_mirror, frame) &&
        input->view.mirror && input->view.seat == input->q1_mirror->reflected.seat &&
        !memcmp(&input->view.viewport, &input->q1_mirror->reflected.viewport, sizeof(input->view.viewport)) &&
        !memcmp(&input->view.origin, &input->q1_mirror->reflected.origin,
                                      sizeof(input->view.origin)) &&
        !memcmp(input->view.axis, input->q1_mirror->reflected.axis, sizeof(input->view.axis));
}

bool qa_scene_world_q1_mirror(qa_scene_world *world, const qa_scene_world_input *input,
                              qa_scene_frame *frame, const qa_scene_q1_mirror **out,
                              qa_scene_view *reflected, bool *found, qa_error *error)
{
    if (!frame || !out || !reflected || !found || !valid_input(world, input, error)) return false;
    *out = NULL; *found = false;
    if (world->bsp.family != QA_BSP_Q1 || input->no_world || input->view.mirror) return true;
    qawl_world *data = world->legacy_data;
    size_t texture = SIZE_MAX;
    for (size_t i = 0; i < data->texture_count; ++i)
        if (!strncmp(data->textures[i].name, "window02_1", 10)) texture = i;
    if (texture == SIZE_MAX || !world_visible(world, input, NULL, error)) return texture == SIZE_MAX;
    if (world->surface_count > SIZE_MAX / sizeof(uint32_t) ||
        world->node_count > (SIZE_MAX / sizeof(mirror_walk) - 1) / 2)
        return world_error(error, QA_ERROR_MEMORY, "Mirror traversal exceeds addressable storage");
    uint32_t *chain = world->surface_count ? qa_arena_alloc(&frame->storage,
        world->surface_count * sizeof(*chain), _Alignof(uint32_t), error) : NULL;
    if (world->surface_count && !chain) return false;
    size_t capacity = world->node_count * 2 + 1;
    mirror_walk *pending = qa_arena_alloc(&frame->storage, capacity * sizeof(*pending),
        _Alignof(mirror_walk), error);
    if (!pending) return false;
    qa_scene_plane frustum[6];
    size_t plane_count = input->no_cull ? 0 : qa_scene_frustum(&input->view, frustum), count = 0;
    qa_scene_plane plane = {0};
    qa_bsp_range faces = world->model_count ? world->models[0].source.faces : (qa_bsp_range){0};
    size_t queued = world->node_count ? 1 : 0;
    if (queued) pending[0] = (mirror_walk){.child = 0};
    while (queued) {
        mirror_walk item = pending[--queued];
        if (item.child < 0) continue;
        const qa_bsp_node *node = &world->nodes[item.child];
        if (!item.faces) {
            if (!qa_scene_bounds_visible(bsp_bounds(node->bounds), frustum, plane_count)) continue;
            const qa_bsp_plane *split = &world->planes[node->plane];
            unsigned near = qa_vec_dot(input->view.origin, split->normal) >= split->distance ? 0 : 1;
            pending[queued++] = (mirror_walk){.child = node->children[near ^ 1]};
            pending[queued++] = (mirror_walk){.child = item.child, .faces = true};
            pending[queued++] = (mirror_walk){.child = node->children[near]};
            continue;
        }
        /* The genuine texture chain prepends each face between the near and
         * far child walks. Its eventual head determines the mirror plane. */
        for (size_t i = node->faces.first; i < (size_t)node->faces.first + node->faces.count; ++i) {
            const qaw_surface *surface = &world->surfaces[i];
            if (i < faces.first || i - faces.first >= faces.count ||
                world->surface_marks[i] != world->visibility_generation ||
                !surface->legacy || surface->legacy->texture != texture ||
                (!surface->legacy->underwater &&
                 qa_vec_dot(input->view.origin, surface->plane.normal) - surface->plane.distance < -.01f) ||
                !qa_scene_bounds_visible(surface->mesh.bounds, frustum, plane_count)) continue;
            if (count == world->surface_count)
                return world_error(error, QA_ERROR_FORMAT, "Mirror BSP face ownership repeats");
            chain[count++] = (uint32_t)i;
            plane = surface->plane;
        }
    }
    if (!count) return true;
    for (size_t i = 0; i < count / 2; ++i) {
        uint32_t index = chain[i]; chain[i] = chain[count - 1 - i]; chain[count - 1 - i] = index;
    }
    qa_scene_q1_mirror *mirror = qa_arena_alloc(&frame->storage, sizeof(*mirror),
        _Alignof(qa_scene_q1_mirror), error);
    if (!mirror) return false;
    *reflected = input->view;
    reflected->origin = qa_vec_sub(input->view.origin, qa_vec_scale(plane.normal,
        2 * (qa_vec_dot(input->view.origin, plane.normal) - plane.distance)));
    for (unsigned i = 0; i < 3; ++i)
        reflected->axis[i] = qa_vec_sub(input->view.axis[i], qa_vec_scale(plane.normal,
            2 * qa_vec_dot(input->view.axis[i], plane.normal)));
    reflected->mirror = true;
    reflected->clear_color = reflected->clear_depth = reflected->clear_stencil = false;
    *mirror = (qa_scene_q1_mirror){world, frame, world->identity, world->revision,
        frame->sequence, texture, count, chain, input->view, *reflected};
    *out = mirror; *found = true;
    return true;
}

typedef struct q2_alpha_batch {
    qa_scene_world *world;
    qa_material_context context;
    qa_scene_world_input input;
    qa_scene_world_entity entity_material;
} q2_alpha_batch;

typedef struct q2_alpha_surface {
    struct q2_alpha_surface *next;
    q2_alpha_batch *batch;
    qaw_surface *surface;
    uint32_t light_mask;
    bool source_dlighted;
} q2_alpha_surface;

struct qa_scene_q2_alpha {
    qa_scene_frame *frame;
    uint64_t sequence;
    qa_scene_view view;
    q2_alpha_surface *head;
    bool finished;
};

bool qa_scene_world_q2_alpha_begin(const qa_scene_world_input *input,
    qa_scene_frame *frame, qa_scene_q2_alpha **out, qa_error *error)
{
    if (!input || !frame || !out || *out)
        return world_error(error, QA_ERROR_ARGUMENT, "Q2 alpha chain requires its actual view and frame");
    if (input->source_order || input->source_scratch || frame->source_pending) return true;
    qa_scene_q2_alpha *alpha = qa_arena_alloc(&frame->storage, sizeof(*alpha),
        _Alignof(qa_scene_q2_alpha), error);
    if (!alpha) return false;
    *alpha = (qa_scene_q2_alpha){.frame = frame, .sequence = frame->sequence, .view = input->view};
    *out = alpha;
    return true;
}

static bool q2_alpha_current(const qa_scene_q2_alpha *alpha,
    const qa_scene_world_input *input, const qa_scene_frame *frame)
{
    return alpha && alpha->frame == frame && alpha->sequence == frame->sequence &&
        !alpha->finished && alpha->view.seat == input->view.seat &&
        !memcmp(&alpha->view.viewport, &input->view.viewport, sizeof(alpha->view.viewport)) &&
        !memcmp(&alpha->view.origin, &input->view.origin, sizeof(alpha->view.origin)) &&
        !memcmp(alpha->view.axis, input->view.axis, sizeof(alpha->view.axis)) &&
        !memcmp(&alpha->view.projection, &input->view.projection, sizeof(alpha->view.projection));
}

static bool q2_alpha_surface_deferred(const qa_scene_world *world,
    const qaw_surface *surface, const qa_scene_world_input *input)
{
    return input->q2_alpha && world->bsp.family == QA_BSP_Q2 &&
        surface->legacy && !surface->sky && surface->legacy->alpha < 1;
}

static bool q2_alpha_prepend(qa_scene_world *world, qaw_surface *surface,
    const qa_material_context *context, const qa_scene_world_input *input,
    qa_scene_frame *frame, q2_alpha_batch **batch, qa_error *error)
{
    qa_scene_q2_alpha *alpha = input->q2_alpha;
    if (!q2_alpha_current(alpha, input, frame))
        return world_error(error, QA_ERROR_ARGUMENT, "Q2 alpha surface belongs to another view");
    if (!*batch) {
        *batch = qa_arena_alloc(&frame->storage, sizeof(**batch), _Alignof(q2_alpha_batch), error);
        if (!*batch) return false;
        **batch = (q2_alpha_batch){.world = world, .context = *context, .input = *input};
        if (input->entity_material) {
            (*batch)->entity_material = *input->entity_material;
            (*batch)->input.entity_material = &(*batch)->entity_material;
        }
    }
    q2_alpha_surface *record = qa_arena_alloc(&frame->storage, sizeof(*record),
        _Alignof(q2_alpha_surface), error);
    if (!record) return false;
    *record = (q2_alpha_surface){alpha->head, *batch, surface,
        context->light_mask, context->source_dlighted};
    alpha->head = record;
    return true;
}

static int compare_surface_priority(const void *a, const void *b)
{
    const surface_order *first = a, *second = b;
    if (first->sort < second->sort) return -1;
    if (first->sort > second->sort) return 1;
    return first->ordinal < second->ordinal ? -1 : first->ordinal > second->ordinal;
}

static bool submit_surface(qa_scene_world *world, qaw_surface *surface, qa_material_context *context,
                            const qa_scene_world_input *input, qa_scene_frame *frame, qa_error *error)
{
    context->lightmap = surface->lightmap;
    context->fog_index = surface->fog_index;
    context->time_offset = surface->material_time_offset +
        (input->entity_material ? input->entity_material->shader_time : 0.0f);
    size_t first = frame->command_count;
    bool result = world->bsp.family == QA_BSP_Q3 ? qaw_submit_q3(world, surface, context, input, frame, error)
        : qaw_submit_legacy(world, surface, context, input, frame, error);
    if (!result) return false;
    if (frame->command_count == first) return true;
    float priority = surface->material != NULL ? surface->material->sort : surface->sort;
    if (surface->material == NULL && !surface->sky && context->entity_color.w < 1) priority = 9;
    const qa_material *effective = effective_material(surface->material);
    if (frame->command_count != first && (effective != NULL ? effective->sky : surface->sky)) world->sky_drawn = true;
    return qa_scene_frame_group(frame, first,
        surface->material == NULL ? QA_SCENE_GROUP_SEQUENCE
        : input->source_order ? QA_SCENE_GROUP_SOURCE : QA_SCENE_GROUP_COMPILED,
        surface->material, priority, context->entity, context->fog_index,
        context->source_primitives ? context->source_dlighted : context->light_mask != 0, error);
}

static bool q2_alpha_finish(const qa_scene_world_input *input,
    qa_scene_frame *frame, qa_error *error)
{
    qa_scene_q2_alpha *alpha = input->q2_alpha;
    if (!q2_alpha_current(alpha, input, frame))
        return world_error(error, QA_ERROR_ARGUMENT, "Q2 alpha finish lost its actual view");
    for (q2_alpha_surface *record = alpha->head; record; record = record->next) {
        q2_alpha_batch *batch = record->batch;
        qa_material_context context = batch->context;
        context.light_mask = record->light_mask;
        context.source_dlighted = record->source_dlighted;
        size_t first_group = frame->group_count;
        if (!submit_surface(batch->world, record->surface, &context,
            &batch->input, frame, error)) return false;
        for (size_t i = first_group; i < frame->group_count; ++i) {
            frame->groups[i].kind = QA_SCENE_GROUP_SEQUENCE;
            frame->groups[i].material = NULL;
            frame->groups[i].priority = 9;
        }
    }
    alpha->head = NULL;
    alpha->finished = true;
    return true;
}

static bool world_submit(qa_scene_world *world, const qa_scene_world_input *input,
                           qa_scene_frame *frame, qa_error *error)
{
    if (frame == NULL) return world_error(error, QA_ERROR_ARGUMENT, "world submission requires frame");
    if (!valid_input(world, input, error)) return false;
    bool deferred = input->legacy_phase == QA_LEGACY_WORLD_WATER ||
        input->legacy_phase == QA_LEGACY_WORLD_ALPHA;
    if (!deferred) world->sky_drawn = false;
    qa_scene_view scene_view = input->view;
    if (deferred)
        scene_view.clear_color = scene_view.clear_depth = scene_view.clear_stencil = false;
    qa_scene_command view = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = scene_view};
    if (!qa_scene_frame_emit(frame, &view, error)) return false;
    if (input->legacy_phase == QA_LEGACY_WORLD_ALPHA)
        return q2_alpha_finish(input, frame, error);
    if (world->bsp.family != QA_BSP_Q3 && !qawl_light_styles(world, input, error)) return false;
    world->admission_frame = NULL;
    if (!begin_admission(world, frame, error)) return false;
    if (input->no_world || (input->source_order && input->skip_world)) return true;
    const qa_scene_source_world_view *prepared = input->source_order ? input->source_visibility : NULL;
    if (prepared && !source_view_current(prepared, world, input, frame))
        return world_error(error, QA_ERROR_ARGUMENT, "Source world draw lost its captured view receipt");
    if (!prepared && !world_visible(world, input, NULL, error)) return false;
    size_t visible_count = prepared ? prepared->count : world->visible_count;
    if (visible_count == 0)
        return !input->boxed_sky || qaw_boxed_sky_world_end(input->boxed_sky,
            world, frame, frame->command_count, error);
    surface_order *order = qa_arena_alloc(&frame->storage, visible_count * sizeof(*order),
        _Alignof(surface_order), error);
    if (order == NULL) return false;
    size_t count = 0;
    bool raw_surfaces = false;
    for (size_t i = 0; i < visible_count; ++i) {
        uint32_t index = prepared ? prepared->surfaces[i] : world->visible_surfaces[i];
        if (world->bsp.family != QA_BSP_Q3 && world->model_count != 0) {
            qa_bsp_range faces = world->models[0].source.faces;
            if (index < faces.first || index - faces.first >= faces.count) continue;
        }
        const qaw_surface *surface = &world->surfaces[index];
        if (world->bsp.family == QA_BSP_Q1 && input->legacy_phase != QA_LEGACY_WORLD_ALL) {
            bool deferred_water = surface->legacy && surface->legacy->warp &&
                (!input->legacy_texture_sort || surface->legacy->alpha < 1);
            if ((input->legacy_phase == QA_LEGACY_WORLD_WATER) != deferred_water) continue;
        }
        if (input->q1_mirror && mirror_current(world, input->q1_mirror, frame) &&
            surface->legacy && surface->legacy->texture == input->q1_mirror->texture) continue;
        const qa_material *material = effective_material(surface->material);
        raw_surfaces |= world->bsp.family != QA_BSP_Q3 && surface->base_material == NULL;
        order[count++] = (surface_order){index, i, q2_alpha_surface_deferred(world, surface, input)
            ? 9 : material != NULL ? material->sort : surface->sort};
    }
    if (raw_surfaces) qsort(order, count, sizeof(*order), compare_surface_priority);
    qa_scene_plane planes[6];
    size_t plane_count = input->no_cull ? 0 : qa_scene_frustum(&input->view, planes);
    if (world->bsp.family == QA_BSP_Q3 && plane_count > 4) plane_count = 4;
    qa_material_context context = world_context(world, input);
    q2_alpha_batch *alpha_batch = NULL;
    if (!fragment_context(world, input, frame, &context, error)) return false;
    bool prepared_cull = prepared && world->bsp.family == QA_BSP_Q3 &&
        prepared->no_cull == input->no_cull && prepared->no_curves == input->no_curves &&
        prepared->disable_face_plane_cull == input->disable_face_plane_cull;
    if (world->bsp.family != QA_BSP_Q3) {
        qa_material_context baked = context;
        baked.lights = input->lights;
        baked.light_count = input->light_count;
        size_t admitted_count = 0;
        for (size_t i = 0; i < count; ++i) {
            qaw_surface *surface = &world->surfaces[order[i].surface];
            if (surface_culled(world, surface, input, &context, NULL, planes, plane_count)) continue;
            if (!qawl_light_update(world, surface, &baked, input, frame, error)) return false;
            order[admitted_count++] = order[i];
        }
        count = admitted_count;
        if (!qawl_light_flush(world, frame, error)) return false;
    }
    size_t opaque_end = frame->command_count;
    for (size_t i = 0; i < count; ++i) {
        qaw_surface *surface = &world->surfaces[order[i].surface];
        bool admitted = true;
        if (input->source_order && !admit_surface(world, surface->source_index, &admitted, error)) return false;
        bool cull_current = prepared_cull && prepared->culls[order[i].ordinal] ==
            (surface->material ? surface->material->cull : QA_CULL_NONE);
        if (!admitted || (world->bsp.family == QA_BSP_Q3 && !cull_current &&
            surface_culled(world, surface, input, &context, NULL, planes, plane_count))) continue;
        uint32_t incoming = prepared ? prepared->lights[order[i].ordinal] : world->surface_lights[surface->source_index];
        uint32_t mask = prepared ? incoming : surface_light_mask(surface, incoming, context.lights, context.light_count);
        context.source_dlighted = incoming != 0 && mask != 0;
        if (input->source_order && world->bsp.family == QA_BSP_Q3) {
            if (!prepared && incoming) world->source_dlight_masks[surface->source_index] = mask;
            context.light_mask = world->source_dlight_masks[surface->source_index];
            if (context.source_scratch) {
                context.source_light_world = world;
                context.source_light_surface = surface->source_index;
            }
        } else context.light_mask = mask;
        if (q2_alpha_surface_deferred(world, surface, input)) {
            if (!q2_alpha_prepend(world, surface, &context, input, frame, &alpha_batch, error)) return false;
        } else if (!submit_surface(world, surface, &context, input, frame, error)) return false;
        if (order[i].sort < 9) opaque_end = frame->command_count;
    }
    return !input->boxed_sky || qaw_boxed_sky_world_end(input->boxed_sky,
        world, frame, opaque_end, error);
}

static bool world_submit_model(qa_scene_world *world, uint32_t model_index,
                                 const qa_model_transform *transform, const qa_scene_world_input *input,
                                 uint32_t entity, qa_scene_vec4 color, qa_scene_frame *frame, qa_error *error)
{
    if (frame == NULL) return world_error(error, QA_ERROR_ARGUMENT, "inline model submission requires frame");
    if (!valid_input(world, input, error)) return false;
    if (transform == NULL || model_index >= world->model_count || (input->source_order && entity >= 1022)
        || !isfinite(color.x) || !isfinite(color.y) || !isfinite(color.z) || !isfinite(color.w))
        return world_error(error, QA_ERROR_ARGUMENT, "invalid inline model or source entity");
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(transform->origin[axis]) || !isfinite(transform->scale[axis]))
            return world_error(error, QA_ERROR_ARGUMENT, "nonfinite inline model origin");
    qa_model_transform inverse;
    if (!qa_model_transform_inverse(transform, &inverse))
        return world_error(error, QA_ERROR_ARGUMENT, "inline model transform is singular");
    const qaw_model *model = &world->models[model_index];
    qa_scene_plane planes[6];
    size_t plane_count = input->no_cull ? 0 : qa_scene_frustum(&input->view, planes);
    if (plane_count && !qa_scene_bounds_visible(qaw_transformed_bounds(bsp_bounds(model->source.bounds), transform),
        planes, plane_count)) return true;
    if (world->bsp.family == QA_BSP_Q3 && plane_count > 4) plane_count = 4;
    if (world->bsp.family != QA_BSP_Q3 && !qawl_light_styles(world, input, error)) return false;
    if (!begin_admission(world, frame, error)) return false;
    qa_material_context context = world_context(world, input);
    q2_alpha_batch *alpha_batch = NULL;
    if (!fragment_context(world, input, frame, &context, error)) return false;
    context.entity = entity;
    context.source_entity_cell = input->source_entity_cells;
    context.entity_color = color;
    if (input->entity_material) {
        const qa_scene_world_entity *material = input->entity_material;
        context.ambient = material->ambient;
        context.ambient_alpha = 1.0f;
        context.directed = material->directed;
        context.light_direction = material->light_direction;
        context.entity_texcoord = material->shader_texcoord;
        context.shadow_plane = material->shadow_plane;
        context.projection_shadow = material->projection_shadow;
    }
    context.model = qa_scene_model_matrix(transform);
    context.local_view_origin = qaw_local_point(transform, input->view.origin);
    context.non_normalized_axis = transform->scale[0] != 1 || transform->scale[1] != 1 ||
        transform->scale[2] != 1 ||
        (input->entity_material && input->entity_material->non_normalized_axis);
    float scale = fminf(fabsf(transform->scale[0]), fminf(fabsf(transform->scale[1]), fabsf(transform->scale[2])));
    if (!(scale > 0) || !isfinite(scale)) return world_error(error, QA_ERROR_ARGUMENT, "invalid inline model scale");
    qa_scene_world_input local_input = *input;
    qa_scene_light *legacy_lights = NULL;
    if (input->light_count != 0) {
        if (input->light_count > SIZE_MAX / sizeof(*legacy_lights)) return world_error(error, QA_ERROR_MEMORY, "inline lights exceed address space");
        legacy_lights = qa_arena_alloc(&frame->storage, input->light_count * sizeof(*legacy_lights), _Alignof(qa_scene_light), error);
        if (legacy_lights == NULL) return false;
        for (size_t i = 0; i < input->light_count; ++i) {
            legacy_lights[i] = input->lights[i];
            legacy_lights[i].origin = qaw_local_point(transform, legacy_lights[i].origin);
            legacy_lights[i].direction = qa_vec_normalize(qaw_local_vector(transform, legacy_lights[i].direction));
            legacy_lights[i].radius /= scale;
            legacy_lights[i].minimum /= scale;
            legacy_lights[i].color = qa_vec_scale(legacy_lights[i].color, scale);
        }
    }
    local_input.lights = legacy_lights;
    const qa_scene_light *source_lights = context.lights;
    qa_scene_light *lights = NULL;
    if (context.light_count != 0) {
        lights = qa_arena_alloc(&frame->storage, context.light_count * sizeof(*lights), _Alignof(qa_scene_light), error);
        if (lights == NULL) return false;
        for (size_t i = 0; i < context.light_count; ++i) {
            lights[i] = source_lights[i];
            lights[i].origin = qaw_local_point(transform, lights[i].origin);
            lights[i].direction = qa_vec_normalize(qaw_local_vector(transform, lights[i].direction));
            lights[i].radius /= scale;
            lights[i].minimum /= scale;
        }
    }
    context.lights = lights;
    uint32_t incoming = all_lights(context.light_count);
    bool source_inline = input->source_order && world->bsp.family == QA_BSP_Q3 && !context.non_normalized_axis;
    if (source_inline) {
        incoming = 0;
        qa_bounds bounds = bsp_bounds(model->source.bounds);
        for (size_t i = 0; i < context.light_count; ++i) {
            qa_vec3 o = lights[i].origin;
            float r = lights[i].radius;
            if (o.x - bounds.maxs.x > r || bounds.mins.x - o.x > r
                || o.y - bounds.maxs.y > r || bounds.mins.y - o.y > r
                || o.z - bounds.maxs.z > r || bounds.mins.z - o.z > r) continue;
            incoming = 1;
            break;
        }
    }
    if (world->bsp.family != QA_BSP_Q3) {
        qa_material_context baked = context;
        baked.lights = local_input.lights;
        baked.light_count = local_input.light_count;
        for (size_t i = 0; i < model->surface_count; ++i) {
            qaw_surface *surface = &world->surfaces[model->surfaces[i]];
            if (surface_culled(world, surface, input, &context, transform, planes, plane_count)) continue;
            if (!qawl_light_update(world, surface, &baked, &local_input, frame, error)) return false;
        }
        if (!qawl_light_flush(world, frame, error)) return false;
    }
    for (size_t i = 0; i < model->surface_count; ++i) {
        qaw_surface *surface = &world->surfaces[model->surfaces[i]];
        bool admitted = true;
        if (input->source_order && !admit_surface(world, surface->source_index, &admitted, error)) return false;
        if (!admitted || surface_culled(world, surface, input, &context, transform, planes, plane_count)) continue;
        uint32_t mask = surface_light_mask(surface, incoming, source_inline ? source_lights : lights, context.light_count);
        context.source_dlighted = incoming != 0 && mask != 0;
        if (input->source_order && world->bsp.family == QA_BSP_Q3) {
            if (incoming) world->source_dlight_masks[surface->source_index] = mask;
            context.light_mask = world->source_dlight_masks[surface->source_index];
            if (context.source_scratch) {
                context.source_light_world = world;
                context.source_light_surface = surface->source_index;
            }
        } else context.light_mask = mask;
        if (q2_alpha_surface_deferred(world, surface, &local_input)) {
            if (!q2_alpha_prepend(world, surface, &context, &local_input,
                frame, &alpha_batch, error)) return false;
        } else if (!submit_surface(world, surface, &context, &local_input, frame, error)) return false;
    }
    return true;
}

typedef struct world_transaction {
    size_t commands, groups, changes, view;
    const qa_scene_frame *admission_frame;
    uint64_t sequence, generation;
    bool sky_drawn;
} world_transaction;

static world_transaction transaction_begin(qa_scene_world *world, const qa_scene_frame *frame)
{
    ++world->transaction_depth;
    return (world_transaction){frame->command_count, frame->group_count, world->admission_change_count,
        world->admission_view, world->admission_frame, world->admission_sequence,
        world->admission_generation, world->sky_drawn};
}

static bool transaction_end(qa_scene_world *world, qa_scene_frame *frame,
                             const world_transaction *start, bool success)
{
    if (!success) {
        if (world->bsp.family != QA_BSP_Q3) qawl_light_atlases_dirty(world);
        frame->command_count = start->commands;
        frame->group_count = start->groups;
        while (world->admission_change_count > start->changes) {
            qaw_admission_change change = world->admission_changes[--world->admission_change_count];
            world->admitted_surfaces[change.surface] = change.previous;
        }
        world->admission_view = start->view;
        world->admission_frame = start->admission_frame;
        world->admission_sequence = start->sequence;
        world->admission_generation = start->generation;
        world->sky_drawn = start->sky_drawn;
    }
    if (--world->transaction_depth == 0) world->admission_change_count = 0;
    return success;
}

bool qa_scene_world_submit(qa_scene_world *world, const qa_scene_world_input *input,
                           qa_scene_frame *frame, qa_error *error)
{
    if (world == NULL || frame == NULL || world->restore_pending || world->checkpoint_active || world->capture || world->image_policy)
        return world_error(error, QA_ERROR_ARGUMENT, "world submission requires world and frame");
    world_transaction start = transaction_begin(world, frame);
    return transaction_end(world, frame, &start, world_submit(world, input, frame, error));
}

bool qa_scene_world_q1_mirror_overlay(qa_scene_world *world, const qa_scene_world_input *input,
                                      float alpha, qa_scene_frame *frame, qa_error *error)
{
    if (!world || !input || !frame || !isfinite(alpha) ||
        !mirror_current(world, input->q1_mirror, frame) || !valid_input(world, input, error))
        return world_error(error, QA_ERROR_ARGUMENT, "Mirror overlay lost its actual parent surface chain");
    world_transaction start = transaction_begin(world, frame);
    qa_scene_world_input parent = *input;
    parent.view = input->q1_mirror->parent;
    parent.view.clear_color = parent.view.clear_depth = parent.view.clear_stencil = false;
    qa_scene_command view = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = parent.view};
    bool ok = qa_scene_frame_emit(frame, &view, error);
    qa_material_context context = world_context(world, &parent);
    context.entity_color.w = alpha;
    if (ok) {
        qa_material_context baked = context;
        baked.lights = parent.lights;
        baked.light_count = parent.light_count;
        for (size_t i = 0; ok && i < input->q1_mirror->count; ++i)
            ok = qawl_light_update(world, &world->surfaces[input->q1_mirror->surfaces[i]],
                &baked, &parent, frame, error);
        if (ok) ok = qawl_light_flush(world, frame, error);
    }
    for (size_t i = 0; ok && i < input->q1_mirror->count; ++i) {
        size_t first = frame->command_count;
        qaw_surface *surface = &world->surfaces[input->q1_mirror->surfaces[i]];
        ok = submit_surface(world, surface, &context, &parent, frame, error);
        for (size_t j = first; ok && j < frame->command_count; ++j) {
            qa_scene_command *command = &frame->commands[j];
            if (command->kind != QA_SCENE_COMMAND_DRAW) continue;
            command->data.draw.state.depth_near = 0;
            command->data.draw.state.depth_far = .5f;
            command->data.draw.state.blend_source = QA_BLEND_SRC_ALPHA;
            command->data.draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
        }
    }
    return transaction_end(world, frame, &start, ok);
}

bool qa_scene_world_source_model_admission(const qa_scene_world *world, uint32_t model,
    const qa_model_transform *transform, const qa_scene_world_input *input, bool *visible, qa_error *error)
{
    if (!world || !world->references || world->checkpoint_active || world->capture || world->image_policy ||
        world->bsp.family!=QA_BSP_Q3 || !input || !input->source_order || !transform || !visible ||
        model>=world->model_count)
        return world_error(error,QA_ERROR_ARGUMENT,"Source inline admission requires its actual retained model and view");
    if (!valid_input(world,input,error)) return false;
    for (size_t axis=0;axis<3;++axis) {
        if (!isfinite(transform->origin[axis]))
            return world_error(error,QA_ERROR_ARGUMENT,"Source inline model origin is nonfinite");
        for (size_t component=0;component<3;++component)
            if (!isfinite(transform->axes[axis][component]))
                return world_error(error,QA_ERROR_ARGUMENT,"Source inline model axis is nonfinite");
    }
    qa_model_transform inverse;
    if (!qa_model_transform_inverse(transform,&inverse))
        return world_error(error,QA_ERROR_ARGUMENT,"Source inline model transform is singular");
    qa_scene_plane planes[6];
    size_t count=input->no_cull?0:qa_scene_frustum(&input->view,planes);
    if (count>4) count=4;
    *visible=local_bounds_visible(bsp_bounds(world->models[model].source.bounds),transform,planes,count);
    return true;
}

bool qa_scene_world_submit_model(qa_scene_world *world, uint32_t model,
                                 const qa_model_transform *transform, const qa_scene_world_input *input,
                                 uint32_t entity, qa_scene_vec4 color, qa_scene_frame *frame, qa_error *error)
{
    if (world == NULL || frame == NULL || world->restore_pending || world->checkpoint_active || world->capture || world->image_policy)
        return world_error(error, QA_ERROR_ARGUMENT, "inline model submission requires world and frame");
    world_transaction start = transaction_begin(world, frame);
    return transaction_end(world, frame, &start,
        world_submit_model(world, model, transform, input, entity, color, frame, error));
}

bool qa_scene_world_sky_drawn(const qa_scene_world *world)
{
    return world != NULL && world->sky_drawn;
}

static bool world_shadow_caster(qa_scene_world *world, uint32_t model_index,
                                  const qa_model_transform *transform,
                                  const qa_scene_world_input *input, qa_scene_frame *frame,
                                  qa_scene_shadow_caster *out, qa_error *error)
{
    if (frame == NULL) return world_error(error, QA_ERROR_ARGUMENT, "shadow caster requires frame");
    if (!valid_input(world, input, error)) return false;
    if (out == NULL || model_index >= world->model_count)
        return world_error(error, QA_ERROR_ARGUMENT, "invalid brush shadow caster request");
    qa_material_context context = world_context(world, input);
    if (transform != NULL) {
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!isfinite(transform->origin[axis]))
                return world_error(error, QA_ERROR_ARGUMENT, "nonfinite brush shadow origin");
        qa_model_transform inverse;
        if (!qa_model_transform_inverse(transform, &inverse))
            return world_error(error, QA_ERROR_ARGUMENT, "brush shadow transform is singular");
        context.model = qa_scene_model_matrix(transform);
        context.local_view_origin = qaw_local_point(transform, input->view.origin);
        context.non_normalized_axis = transform->scale[0] != 1 || transform->scale[1] != 1 || transform->scale[2] != 1;
    }
    const qaw_model *model = &world->models[model_index];
    qa_scene_shadow_caster caster = {
        .transform = context.model, .identity = model->identity, .revision = world->revision,
        .world_geometry = model_index == 0 && transform == NULL
    };
    qa_scene_mesh *meshes = NULL;
    if (model->surface_count != 0) {
        if (model->surface_count > SIZE_MAX / sizeof(*meshes))
            return world_error(error, QA_ERROR_MEMORY, "brush shadow mesh list exceeds address space");
        meshes = qa_arena_alloc(&frame->storage, model->surface_count * sizeof(*meshes), _Alignof(qa_scene_mesh), error);
        if (meshes == NULL) return false;
    }
    for (size_t i = 0; i < model->surface_count; ++i) {
        const qaw_surface *surface = &world->surfaces[model->surfaces[i]];
        context.time_offset = surface->material_time_offset;
        qa_scene_mesh mesh = surface->mesh;
        if (surface->flare) continue;
        if (surface->material != NULL) {
            if (!qa_material_shadow_mesh(surface->material, &mesh, &context, frame, &mesh, error)) return false;
        } else if (!qaw_legacy_casts_shadow(world, surface, model_index != 0 || transform != NULL)) continue;
        if (mesh.index_count == 0 || mesh.vertex_count == 0) continue;
        if (!qa_scene_frame_geometry(frame, mesh.geometry, error)) return false;
        caster.bounds = caster.mesh_count == 0 ? mesh.bounds : qa_bounds_union(caster.bounds, mesh.bounds);
        meshes[caster.mesh_count++] = mesh;
    }
    caster.meshes = meshes;
    *out = caster;
    return true;
}

bool qa_scene_world_shadow_caster(qa_scene_world *world, uint32_t model_index,
                                  const qa_model_transform *transform,
                                  const qa_scene_world_input *input, qa_scene_frame *frame,
                                  qa_scene_shadow_caster *out, qa_error *error)
{
    if (!world || !frame || world->restore_pending || world->checkpoint_active || world->capture || world->image_policy)
        return world_error(error, QA_ERROR_ARGUMENT, "brush shadow submission requires idle continuation owners");
    world_transaction start = transaction_begin(world, frame);
    return transaction_end(world, frame, &start,
        world_shadow_caster(world, model_index, transform, input, frame, out, error));
}

bool qa_scene_world_portal_view(qa_scene_world *world, const qa_scene_world_input *input,
                                const qa_scene_portal *portals, size_t portal_count,
                                qa_scene_view *view, qa_vec3 *pvs_origin, bool *found, qa_error *error)
{
    if (found != NULL) *found = false;
    if (!valid_input(world, input, error)) return false;
    if (view == NULL || pvs_origin == NULL || found == NULL || (portal_count != 0 && portals == NULL))
        return world_error(error, QA_ERROR_ARGUMENT, "invalid world portal-view outputs or entities");
    *view = input->view;
    *pvs_origin = input->view.origin;
    if (input->no_world || (input->source_order && input->skip_world) || input->view.clip_enabled || portal_count == 0) return true;
    const qa_scene_source_world_view *prepared = input->source_order ? input->source_visibility : NULL;
    if (prepared && !source_view_current(prepared, world, input, prepared->frame))
        return world_error(error, QA_ERROR_ARGUMENT, "Source portal search lost its captured parent visibility");
    if (!prepared && !world_visible(world, input, NULL, error)) return false;
    size_t visible_count = prepared ? prepared->count : world->visible_count;
    for (size_t i = 0; i < visible_count; ++i) {
        const qaw_surface *surface = &world->surfaces[prepared ? prepared->surfaces[i] : world->visible_surfaces[i]];
        const qa_material *material = effective_material(surface->material);
        if (!surface->has_plane || material == NULL || material->sort != 1) continue;
        const qa_scene_portal *portal = NULL;
        for (size_t j = 0; j < portal_count; ++j) {
            float distance = (float)(precise_dot(portals[j].origin, surface->plane.normal) - surface->plane.distance);
            if (fabsf(distance) <= 64) { portal = &portals[j]; break; }
        }
        if (portal == NULL) continue;
        qa_scene_view candidate;
        qa_vec3 candidate_pvs;
        if (!qa_scene_portal_view(&input->view, surface->plane, portal, input->seconds, &candidate, &candidate_pvs)) continue;
        if (!qa_scene_portal_surface_visible(&surface->mesh, &input->view, material->portal_range, candidate.mirror)) continue;
        *view = candidate;
        *pvs_origin = candidate_pvs;
        *found = true;
        break;
    }
    return true;
}

bool qa_scene_world_fog_for_sphere(const qa_scene_world *world, qa_vec3 origin, float radius,
                                  qa_scene_fog_volume *out)
{
    if (out == NULL) return false;
    *out = (qa_scene_fog_volume){0};
    if (world == NULL || world->bsp.family != QA_BSP_Q3 || !qa_vec_finite(origin)
        || !isfinite(radius) || radius < 0) return false;
    return qaw_q3_fog_for_sphere(world, origin, radius, out);
}

bool qa_scene_world_fog_for_bounds(const qa_scene_world *world, qa_bounds bounds,
                                  qa_scene_fog_volume *out)
{
    if (out == NULL) return false;
    *out = (qa_scene_fog_volume){0};
    if (world == NULL || world->bsp.family != QA_BSP_Q3 ||
        !qa_vec_finite(bounds.mins) || !qa_vec_finite(bounds.maxs) ||
        bounds.mins.x > bounds.maxs.x || bounds.mins.y > bounds.maxs.y ||
        bounds.mins.z > bounds.maxs.z) return false;
    return qaw_q3_fog_for_bounds(world, bounds, out);
}

const qa_scene_mesh *qa_scene_world_mesh_at(const qa_scene_world *world, size_t index)
{ return world && index<world->surface_count?&world->surfaces[index].mesh:NULL; }
size_t qa_scene_world_model_count(const qa_scene_world *world) { return world?world->model_count:0; }
uint64_t qa_scene_world_model_identity_at(const qa_scene_world *world, size_t index)
{ return world && index<world->model_count?world->models[index].identity:0; }
qa_scene_resources *qa_scene_world_resource_owner(const qa_scene_world *world) { return world?world->resources:NULL; }
qa_material_library *qa_scene_world_material_owner(const qa_scene_world *world) { return world?world->materials:NULL; }
