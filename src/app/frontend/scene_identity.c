#include "scene_identity.h"
#include "image_inventory.h"
#include "native_q3_client.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "network_initial_graph.h"
#include "remote_q1_client.h"
#include "remote_q2_client.h"
#include "renderer_materials.h"
#include "renderer_worlds.h"
#include "unified_media_inventory.h"
#include "qa/material_library_save.h"
#include "qa/scene_geometry_save.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

typedef enum scene_row_kind {
    SCENE_IMAGE, SCENE_GEOMETRY, SCENE_MATERIAL, SCENE_FRAME,
    SCENE_WORLD, SCENE_MODEL, SCENE_MESH, SCENE_SHADOW, SCENE_LIGHT, SCENE_STATIC_AUDIO
} scene_row_kind;
typedef enum scene_origin { SCENE_IMAGES, SCENE_LIBRARY, SCENE_WORLD_OWNER,
    SCENE_MODEL_OWNER, SCENE_FRAME_OWNER, SCENE_LIGHT_OWNER, SCENE_STATIC_AUDIO_OWNER,
    SCENE_RENDERER_OWNER, SCENE_LIBRARY_WORLD_OWNER } scene_origin;
typedef struct scene_row {
    scene_row_kind kind;
    scene_origin origin;
    uint64_t owner, node, ordinal, saved, installed;
    const void *pointer;
    bool qualified;
} scene_row;
struct frontend_scene_namespace {
    scene_row *rows;
    size_t count, capacity, image_count;
    bool restoring, sealed, images_captured;
};
static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
static bool open(const frontend_scene_namespace *space, bool restoring, qa_error *error)
{
    return (space && !space->sealed && space->restoring == restoring) ||
        fail(error, QA_ERROR_ARGUMENT, "Scene namespace is outside its actual preparation phase");
}
static scene_row *position(frontend_scene_namespace *space, scene_row_kind kind,
    scene_origin origin, uint64_t owner, uint64_t node, uint64_t ordinal)
{
    if (space) for (size_t i = 0; i < space->count; ++i) {
        scene_row *row = &space->rows[i];
        if (row->kind == kind && row->origin == origin && row->owner == owner &&
            row->node == node && row->ordinal == ordinal) return row;
    }
    return NULL;
}
static bool append(frontend_scene_namespace *space, scene_row row, qa_error *error)
{
    if (space->count == space->capacity) {
        size_t capacity = space->capacity ? space->capacity * 2 : 32;
        if (capacity < space->capacity || capacity > SIZE_MAX / sizeof(*space->rows))
            return fail(error, QA_ERROR_MEMORY, "Scene namespace exceeds addressable storage");
        scene_row *rows = realloc(space->rows, capacity * sizeof(*rows));
        if (!rows) return fail(error, QA_ERROR_MEMORY, "Allocating shared scene namespace");
        space->rows = rows; space->capacity = capacity;
    }
    space->rows[space->count++] = row; return true;
}
static bool pointer_add(frontend_scene_namespace *space, scene_row row, qa_error *error)
{
    if (!row.pointer || position(space, row.kind, row.origin, row.owner, row.node, row.ordinal))
        return fail(error, QA_ERROR_FORMAT, "Scene namespace holder is absent or repeated");
    for (size_t i = 0; i < space->count; ++i) if (space->rows[i].pointer == row.pointer) {
        if (space->rows[i].kind != row.kind)
            return fail(error, QA_ERROR_FORMAT, "Scene pointer has conflicting owner kinds");
        if (row.kind == SCENE_IMAGE || row.kind == SCENE_MATERIAL || row.kind == SCENE_FRAME)
            return fail(error, QA_ERROR_FORMAT, "Scene holder has duplicate destructor authority");
        return true;
    }
    if (!append(space, row, error)) return false;
    if (row.kind == SCENE_IMAGE) qa_scene_image_retain(row.pointer);
    if (row.kind == SCENE_GEOMETRY) qa_scene_geometry_retain(row.pointer);
    return true;
}
static bool number_add(frontend_scene_namespace *space, scene_row_kind kind, scene_origin origin,
    uint64_t owner, size_t node, size_t ordinal, uint64_t identity, qa_error *error)
{
    if (!identity || position(space, kind, origin, owner, node, ordinal))
        return fail(error, QA_ERROR_FORMAT, "Scene producer identity is absent or repeated");
    for (size_t i = 0; i < space->count; ++i) if (space->rows[i].saved == identity &&
        space->rows[i].kind != SCENE_GEOMETRY && space->rows[i].kind != SCENE_FRAME)
        return fail(error, QA_ERROR_FORMAT, "Scene producer identity aliases a different actual owner");
    return append(space, (scene_row){kind, origin, owner, node, ordinal, identity, identity, NULL, true}, error);
}
static bool geometry_add(frontend_scene_namespace *space, scene_origin origin,
    uint64_t owner, size_t node, size_t ordinal, const qa_scene_geometry *geometry, qa_error *error)
{
    if (!qa_scene_geometry_active(geometry))
        return fail(error, QA_ERROR_FORMAT, "Scene geometry has no genuine active allocation");
    return pointer_add(space, (scene_row){.kind = SCENE_GEOMETRY, .origin = origin,
        .owner = owner, .node = node, .ordinal = ordinal, .pointer = geometry, .qualified = true}, error);
}
bool frontend_scene_namespace_create(frontend_scene_namespace **out, qa_error *error)
{
    if (!out || *out) return fail(error, QA_ERROR_ARGUMENT, "Scene namespace requires an empty output");
    frontend_scene_namespace *space = calloc(1, sizeof(*space));
    if (!space) return fail(error, QA_ERROR_MEMORY, "Allocating scene namespace owner");
    *out = space; return true;
}
void frontend_scene_namespace_destroy(frontend_scene_namespace *space)
{
    if (!space) return;
    for (size_t i = 0; i < space->count; ++i) {
        scene_row *row = &space->rows[i];
        if (row->kind == SCENE_IMAGE && row->pointer) qa_scene_image_release(row->pointer);
        if (row->kind == SCENE_GEOMETRY && row->pointer) qa_scene_geometry_release(row->pointer);
    }
    free(space->rows); free(space);
}
static bool images_owner(frontend_scene_namespace *space, qa_frontend *frontend,
    const qa_scene_resources *owner, qa_arena *scratch, qa_error *error)
{
    if (!owner) return true;
    const qa_scene_image *const *images = NULL; size_t count = 0;
    if (!qa_scene_resources_images(owner, scratch, &images, &count, error)) return false;
    for (size_t i = count; i; --i) {
        const qa_scene_image *image = images[i - 1]; uint64_t ordinal = 0;
        if (!image || !image->identity || !frontend_image_index(frontend, image, &ordinal, error) ||
            ordinal != space->image_count || !pointer_add(space, (scene_row){.kind = SCENE_IMAGE,
                .origin = SCENE_IMAGES, .ordinal = ordinal, .saved = image->identity,
                .installed = image->identity, .pointer = image, .qualified = true}, error)) return false;
        ++space->image_count;
    }
    return true;
}
bool frontend_scene_namespace_capture_images(frontend_scene_namespace *space, qa_frontend *f, qa_error *error)
{
    if (!open(space, false, error) || !f || !f->application || f->stepping || f->preparing ||
        !frontend_native_q2_callbacks_idle(f) || !qa_application_content_graph_read(f->application) || space->images_captured)
        return fail(error, QA_ERROR_ARGUMENT, "Scene images require one idle actual owner inventory");
    qa_arena scratch; qa_arena_init(&scratch, 16384);
    bool ok = images_owner(space, f, f->ui_images, &scratch, error) && images_owner(space, f, f->images, &scratch, error);
    for (size_t i = 0; ok && i < frontend_source_group_count(f); ++i) {
        frontend_source_group_view group;
        ok = frontend_source_group_read(f, i, &group) && group.images && images_owner(space, f, group.images, &scratch, error);
    }
    for (unsigned kind = 0; ok && kind < 3; ++kind) for (size_t i = 0; ok; ++i) {
        const qa_scene_resources *images = kind == 0 ? frontend_event_images_at(f, i) :
            kind == 1 ? frontend_visual_images_at(f, i) : frontend_native_q2_images_at(f, i);
        if (!images) break;
        ok = images_owner(space, f, images, &scratch, error);
    }
    for (size_t i=0;ok && i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view owner;
        ok=frontend_native_q3_read(f,i,&owner,error) && owner.images &&
            images_owner(space,f,owner.images,&scratch,error);
    }
    for(size_t i=0;ok && i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources owner;
        ok=frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&owner,error) && owner.images &&
            images_owner(space,f,owner.images,&scratch,error);
    }
    frontend_network_initial_graph_view initial;
    if(ok) ok=frontend_network_initial_graph_read(f,&initial,error);
    if(ok && initial.present) {
        frontend_remote_q3_initial_view owner;
        ok=initial.parent && frontend_remote_q3_initial_read(initial.parent,&owner,error) && owner.images &&
            images_owner(space,f,owner.images,&scratch,error);
    }
    for(size_t i=0;ok && i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1_media owner;
        ok=frontend_remote_q1_media_read(frontend_remote_q1_at(f,i),&owner,error) &&
            images_owner(space,f,owner.images,&scratch,error);
    }
    for(size_t i=0;ok && i<frontend_remote_q2_count(f);++i) {
        frontend_remote_q2_view owner;
        ok=frontend_remote_q2_metadata_read(frontend_remote_q2_at(f,i),&owner,error) &&
            images_owner(space,f,owner.images,&scratch,error);
    }
    frontend_renderer_materials_view retained; bool present=false;
    if(ok) ok=frontend_renderer_materials_read(f,&retained,&present,error);
    if(ok && present) ok=images_owner(space,f,retained.images,&scratch,error);
    if(ok && present) ok=images_owner(space,f,retained.lightmap_images,&scratch,error);
    frontend_renderer_worlds_view world; bool has_world=false;
    if(ok) ok=frontend_renderer_worlds_read(f,&world,&has_world,error);
    if(ok && has_world && world.private_heaps) ok=images_owner(space,f,world.images,&scratch,error);
    size_t unified_count=0;
    if(ok) ok=frontend_unified_media_inventory_count(f,&unified_count,error);
    for(size_t i=0;ok && i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        ok=frontend_unified_media_inventory_at(f,i,&media,error);
        for(size_t j=0;ok && media && j<frontend_unified_media_bank_count(media);++j) {
            frontend_unified_bank_view bank;
            ok=frontend_unified_media_bank_read(media,j,&bank) && images_owner(space,f,bank.images,&scratch,error);
        }
    }
    qa_arena_destroy(&scratch);
    if (ok) space->images_captured = true;
    if (!ok && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Actual scene image owner graph is incomplete");
    return ok;
}
bool frontend_scene_namespace_capture_library(frontend_scene_namespace *space, uint64_t owner,
    const qa_material_library *library, qa_error *error)
{
    if (!open(space, false, error) || !owner || !library)
        return fail(error, QA_ERROR_ARGUMENT, "Scene material inventory requires its actual library");
    for (size_t i = 0; i < qa_material_library_record_count(library); ++i) {
        const qa_material *material = qa_material_library_record_at(library, i);
        if (!material || !material->identity || !pointer_add(space, (scene_row){.kind = SCENE_MATERIAL,
            .origin = SCENE_LIBRARY, .owner = owner, .ordinal = i, .saved = material->identity,
            .installed = material->identity, .pointer = material, .qualified = true}, error)) return false;
    }
    return true;
}
bool frontend_scene_namespace_capture_library_worlds(frontend_scene_namespace *space,uint64_t owner,
    const qa_material_library *library,qa_error *error)
{
    if(!open(space,false,error) || !owner || !library)
        return fail(error,QA_ERROR_ARGUMENT,"Material world history requires its actual retained library");
    for(size_t i=0;i<qa_material_library_record_count(library);++i) {
        qa_material_library_record_view record;
        if(!qa_material_library_record_read(library,i,&record)) return false;
        if(!record.world_identity) continue;
        bool found=false;
        for(size_t j=0;j<space->count;++j) if(space->rows[j].kind==SCENE_WORLD &&
            space->rows[j].installed==record.world_identity) { found=true; break; }
        if(!found && !number_add(space,SCENE_WORLD,SCENE_LIBRARY_WORLD_OWNER,owner,0,i,
            record.world_identity,error)) return false;
    }
    return true;
}
static bool world_rows(frontend_scene_namespace *space, uint64_t owner, const qa_scene_world *world,
    bool capture, qa_error *error);
bool frontend_scene_namespace_capture_world(frontend_scene_namespace *space, uint64_t owner,
    const qa_scene_world *world, qa_error *error)
{
    if (!open(space, false, error) || !owner || !qa_scene_world_observation_ready(world))
        return fail(error, QA_ERROR_ARGUMENT, "Scene world inventory requires its actual idle owner");
    return world_rows(space, owner, world, true, error);
}
typedef struct scene_models { const qa_scene_model **nodes; size_t count; } scene_models;
static bool model_nodes(scene_models *nodes, const qa_scene_model *root, qa_error *error)
{
    if (!qa_scene_model_observation_ready(root) || qa_scene_model_replacement_next(root))
        return fail(error, QA_ERROR_ARGUMENT, "Scene model inventory requires an idle graph root");
    nodes->nodes = malloc(sizeof(*nodes->nodes));
    if (!nodes->nodes) return fail(error, QA_ERROR_MEMORY, "Indexing actual scene model graph");
    nodes->nodes[nodes->count++] = root;
    for (size_t i = 0; i < nodes->count; ++i) {
        const qa_scene_model *parent = nodes->nodes[i];
        for (const qa_scene_model *child = qa_scene_model_replacement_first(parent); child;
            child = qa_scene_model_replacement_next(child)) {
            for (size_t j = 0; j < nodes->count; ++j) if (nodes->nodes[j] == child)
                return fail(error, QA_ERROR_FORMAT, "Actual scene model graph aliases or cycles");
            if (nodes->count >= SIZE_MAX / sizeof(*nodes->nodes))
                return fail(error, QA_ERROR_MEMORY, "Scene model graph exceeds addressable storage");
            const qa_scene_model **grown = realloc(nodes->nodes, (nodes->count + 1) * sizeof(*grown));
            if (!grown) return fail(error, QA_ERROR_MEMORY, "Indexing scene model replacement graph");
            nodes->nodes = grown; nodes->nodes[nodes->count++] = child;
        }
    }
    return true;
}
static bool model_rows(frontend_scene_namespace *space, uint64_t owner, const qa_scene_model *root,
    bool capture, qa_error *error);
bool frontend_scene_namespace_capture_model(frontend_scene_namespace *space, uint64_t owner,
    const qa_scene_model *model, qa_error *error)
{
    if (!open(space, false, error) || !owner)
        return fail(error, QA_ERROR_ARGUMENT, "Scene model inventory requires its actual owner ordinal");
    return model_rows(space, owner, model, true, error);
}
bool frontend_scene_namespace_capture_light(frontend_scene_namespace *space, uint64_t owner,
    size_t ordinal, uint64_t identity, qa_error *error)
{
    if (!open(space, false, error) || !owner)
        return fail(error, QA_ERROR_ARGUMENT, "Scene light inventory requires its actual producer owner");
    return number_add(space, SCENE_LIGHT, SCENE_LIGHT_OWNER, owner, 0, ordinal, identity, error);
}
bool frontend_scene_namespace_capture_static_audio(frontend_scene_namespace *space, uint64_t owner,
    size_t ordinal, uint64_t key, qa_error *error)
{
    if (!open(space, false, error) || !owner)
        return fail(error, QA_ERROR_ARGUMENT, "Static audio inventory requires its actual event producer owner");
    return number_add(space, SCENE_STATIC_AUDIO, SCENE_STATIC_AUDIO_OWNER, owner, 0, ordinal, key, error);
}
static bool frame_image(frontend_scene_namespace *space, const qa_scene_image *image, qa_error *error)
{
    uint64_t key = 0;
    return !image || frontend_scene_image_encode(space, image, &key, error);
}
static bool frame_references(frontend_scene_namespace *space, const qa_scene_frame *frame, qa_error *error)
{
    if (!frame || (frame->command_count && !frame->commands) || (frame->image_count && !frame->images) ||
        (frame->geometry_count && !frame->geometries) || (frame->group_count && !frame->groups))
        return fail(error, QA_ERROR_FORMAT, "Scene frame owner storage is incomplete");
    uint64_t key = 0;
    for (size_t i = 0; i < frame->image_count; ++i)
        if (!frontend_scene_image_encode(space, frame->images[i], &key, error)) return false;
    for (size_t i = 0; i < frame->geometry_count; ++i)
        if (!frontend_scene_geometry_encode(space, frame->geometries[i], &key, error)) return false;
    for (size_t i = 0; i < frame->group_count; ++i) if (frame->groups[i].material &&
        !frontend_scene_material_encode(space, frame->groups[i].material, &key, error)) return false;
    for (size_t i = 0; i < frame->command_count; ++i) {
        const qa_scene_command *command = &frame->commands[i];
        if (command->kind > QA_SCENE_COMMAND_PREBLEND_GAMMA)
            return fail(error, QA_ERROR_FORMAT, "Scene command has an unknown kind");
        if (command->kind == QA_SCENE_COMMAND_TARGET && !frame_image(space, command->data.target.image, error)) return false;
        if (command->kind == QA_SCENE_COMMAND_IMAGE && !frame_image(space, command->data.image, error)) return false;
        if (command->kind != QA_SCENE_COMMAND_DRAW) continue;
        const qa_scene_draw *draw = &command->data.draw;
        if (draw->texture_count > 2 || !frame_image(space, draw->shadow_atlas, error)) return false;
        for (size_t j = 0; j < draw->texture_count; ++j)
            if (!frame_image(space, draw->textures[j], error)) return false;
        if (draw->mesh.geometry && !frontend_scene_geometry_encode(space, draw->mesh.geometry, &key, error)) return false;
        if (draw->mesh.identity && !draw->mesh.geometry)
            return fail(error, QA_ERROR_FORMAT, "Retained scene mesh has no active geometry owner");
        if (draw->mesh.identity && !frontend_scene_mesh_identity_encode(space, draw->mesh.identity, &key, error)) return false;
        if (draw->light_count && !draw->lights) return fail(error, QA_ERROR_FORMAT, "Scene light span is absent");
        for (size_t j = 0; j < draw->light_count; ++j) if (draw->lights[j].light.identity &&
            !frontend_scene_light_identity_encode(space, draw->lights[j].light.identity, &key, error)) return false;
    }
    return true;
}
bool frontend_scene_namespace_capture_frame(frontend_scene_namespace *space, uint64_t owner,
    const qa_scene_frame *frame, qa_error *error)
{
    if (!open(space, false, error) || !owner || !frame || (frame->geometry_count && !frame->geometries))
        return fail(error, QA_ERROR_ARGUMENT, "Scene frame inventory requires its actual frame owner");
    for (size_t i = 0; i < frame->geometry_count; ++i)
        if (!geometry_add(space, SCENE_FRAME_OWNER, owner, 0, i, frame->geometries[i], error)) return false;
    return frame_references(space, frame, error) && pointer_add(space, (scene_row){.kind = SCENE_FRAME,
        .origin = SCENE_FRAME_OWNER, .owner = owner, .saved = frame->owner, .installed = frame->owner,
        .pointer = frame, .qualified = true}, error);
}
bool frontend_scene_namespace_capture_renderer_geometry(frontend_scene_namespace *space,uint64_t owner,
    size_t ordinal,const qa_scene_geometry *geometry,qa_error *error)
{
    if (!open(space,false,error) || !owner)
        return fail(error,QA_ERROR_ARGUMENT,"Renderer geometry capture requires its actual physical owner row");
    return geometry_add(space,SCENE_RENDERER_OWNER,owner,0,ordinal,geometry,error);
}
bool frontend_scene_namespace_capture_renderer_mesh(frontend_scene_namespace *space,uint64_t owner,
    size_t ordinal,uint64_t identity,qa_error *error)
{
    if (!open(space,false,error) || !owner || !identity)
        return fail(error,QA_ERROR_ARGUMENT,"Renderer mesh capture requires its genuine live cache row");
    for (size_t i=0;i<space->count;++i)
        if (space->rows[i].kind==SCENE_MESH && space->rows[i].installed==identity) return true;
    return number_add(space,SCENE_MESH,SCENE_RENDERER_OWNER,owner,0,ordinal,identity,error);
}
static bool pointer_encode(frontend_scene_namespace *space, scene_row_kind kind,
    const void *pointer, uint64_t *out, qa_error *error)
{
    if (!space || !pointer || !out) return fail(error, QA_ERROR_ARGUMENT, "Scene reference requires a real holder and output");
    for (size_t i = 0; i < space->count; ++i) if (space->rows[i].kind == kind && space->rows[i].pointer == pointer) {
        if (kind == SCENE_GEOMETRY && !qa_scene_geometry_active(pointer)) break;
        *out = (uint64_t)i + 1; return true;
    }
    return fail(error, QA_ERROR_FORMAT, "Scene reference is outside the qualified shared namespace");
}
static bool pointer_decode(frontend_scene_namespace *space, scene_row_kind kind,
    uint64_t key, const void **out, qa_error *error)
{
    if (!space || !out || !key || key > space->count)
        return fail(error, QA_ERROR_FORMAT, "Scene reference key is outside the shared namespace");
    scene_row *row = &space->rows[(size_t)key - 1];
    if (row->kind != kind || !row->pointer || (kind == SCENE_GEOMETRY && !qa_scene_geometry_active(row->pointer)))
        return fail(error, QA_ERROR_FORMAT, "Scene reference has no matching installed holder");
    *out = row->pointer; return true;
}
#define POINTER_RESOLVERS(name, type, kind) \
bool frontend_scene_##name##_encode(void *context, const type *value, uint64_t *out, qa_error *error) \
{ return pointer_encode(context, kind, value, out, error); } \
bool frontend_scene_##name##_decode(void *context, uint64_t key, const type **out, qa_error *error) \
{ const void *value = NULL; if (!out || !pointer_decode(context, kind, key, &value, error)) return false; *out = value; return true; }
POINTER_RESOLVERS(image, qa_scene_image, SCENE_IMAGE)
POINTER_RESOLVERS(geometry, qa_scene_geometry, SCENE_GEOMETRY)
POINTER_RESOLVERS(material, qa_material, SCENE_MATERIAL)
POINTER_RESOLVERS(frame, qa_scene_frame, SCENE_FRAME)
POINTER_RESOLVERS(world, qa_scene_world, SCENE_WORLD)
#undef POINTER_RESOLVERS
bool frontend_scene_material_mutable_decode(void *context, uint64_t key, qa_material **out, qa_error *error)
{
    const qa_material *value = NULL;
    if (!out || !frontend_scene_material_decode(context, key, &value, error)) return false;
    *out = (qa_material *)value; return true;
}
static bool number_encode(frontend_scene_namespace *space, scene_row_kind kind,
    uint64_t value, uint64_t *out, qa_error *error)
{
    if (!space || !value || !out) return fail(error, QA_ERROR_ARGUMENT, "Scene identity requires an actual value and output");
    for (size_t i = 0; i < space->count; ++i) {
        scene_row *row = &space->rows[i];
        if (row->kind == kind && row->installed == value) { *out = (uint64_t)i + 1; return true; }
    }
    return fail(error, QA_ERROR_FORMAT, "Scene identity has no actual producer in the shared namespace");
}
static bool number_decode(frontend_scene_namespace *space, scene_row_kind kind,
    uint64_t key, uint64_t *out, qa_error *error)
{
    if (!space || !out || !key || key > space->count)
        return fail(error, QA_ERROR_FORMAT, "Scene identity key is outside the shared namespace");
    scene_row *row = &space->rows[(size_t)key - 1];
    if (row->kind != kind || !row->installed)
        return fail(error, QA_ERROR_FORMAT, "Scene identity has no preallocated matching owner");
    *out = row->installed; return true;
}
#define NUMBER_RESOLVERS(name, kind) \
bool frontend_scene_##name##_identity_encode(void *context, uint64_t value, uint64_t *out, qa_error *error) \
{ return number_encode(context, kind, value, out, error); } \
bool frontend_scene_##name##_identity_decode(void *context, uint64_t key, uint64_t *out, qa_error *error) \
{ return number_decode(context, kind, key, out, error); }
NUMBER_RESOLVERS(image, SCENE_IMAGE)
NUMBER_RESOLVERS(world, SCENE_WORLD)
NUMBER_RESOLVERS(mesh, SCENE_MESH)
NUMBER_RESOLVERS(light, SCENE_LIGHT)
#undef NUMBER_RESOLVERS
static bool install(frontend_scene_identity_scope *scope, scene_row_kind kind,
    scene_origin origin, size_t node, size_t ordinal, uint64_t saved, uint64_t *out, qa_error *error)
{
    scene_row *row = scope ? position(scope->space, kind, origin, scope->owner, node, ordinal) : NULL;
    if (!row || !out || !saved || row->saved != saved || !row->installed)
        return fail(error, QA_ERROR_FORMAT, "Scene owner prefix differs from its preallocated namespace");
    *out = row->installed; return true;
}
bool frontend_scene_world_install(void *context, qa_scene_world_identity_kind kind, size_t ordinal,
    uint64_t saved, uint64_t *out, qa_error *error)
{
    if (kind > QA_SCENE_WORLD_IDENTITY_MESH)
        return fail(error, QA_ERROR_FORMAT, "Unknown world namespace identity kind");
    scene_row_kind row_kind = kind == QA_SCENE_WORLD_IDENTITY_WORLD ? SCENE_WORLD :
        kind == QA_SCENE_WORLD_IDENTITY_MODEL ? SCENE_MODEL : SCENE_MESH;
    return install(context, row_kind, SCENE_WORLD_OWNER, 0, ordinal, saved, out, error);
}
bool frontend_scene_model_install(void *context, qa_scene_model_identity_kind kind, size_t node,
    size_t ordinal, uint64_t saved, uint64_t *out, qa_error *error)
{
    if (kind > QA_SCENE_MODEL_IDENTITY_SHADOW)
        return fail(error, QA_ERROR_FORMAT, "Unknown model namespace identity kind");
    scene_row_kind row_kind = kind == QA_SCENE_MODEL_IDENTITY_MODEL ? SCENE_MODEL :
        kind == QA_SCENE_MODEL_IDENTITY_MESH ? SCENE_MESH : SCENE_SHADOW;
    return install(context, row_kind, SCENE_MODEL_OWNER, node, ordinal, saved, out, error);
}
bool frontend_scene_light_install(frontend_scene_identity_scope *scope, size_t ordinal,
    uint64_t saved, uint64_t *out, qa_error *error)
{ return install(scope, SCENE_LIGHT, SCENE_LIGHT_OWNER, 0, ordinal, saved, out, error); }
bool frontend_scene_static_audio_install(frontend_scene_identity_scope *scope, size_t ordinal,
    uint64_t saved, uint64_t *out, qa_error *error)
{ return install(scope, SCENE_STATIC_AUDIO, SCENE_STATIC_AUDIO_OWNER, 0, ordinal, saved, out, error); }
static bool producer_ready(const frontend_scene_identity_scope *scope, scene_row_kind kind,
    scene_origin origin, size_t ordinal, uint64_t actual, qa_error *error)
{
    if (!scope || !scope->space || !scope->owner)
        return fail(error, QA_ERROR_ARGUMENT, "Scene producer observation requires its actual namespace scope");
    const scene_row *row = position(scope->space, kind, origin, scope->owner, 0, ordinal);
    if (!row || !actual || actual != row->installed)
        return fail(error, QA_ERROR_FORMAT, "Scene producer differs from its admitted physical owner and identity");
    return true;
}
bool frontend_scene_light_owner_ready(const frontend_scene_identity_scope *scope, size_t ordinal,
    uint64_t actual, qa_error *error)
{ return producer_ready(scope, SCENE_LIGHT, SCENE_LIGHT_OWNER, ordinal, actual, error); }
bool frontend_scene_static_audio_owner_ready(const frontend_scene_identity_scope *scope, size_t ordinal,
    uint64_t actual, qa_error *error)
{ return producer_ready(scope, SCENE_STATIC_AUDIO, SCENE_STATIC_AUDIO_OWNER, ordinal, actual, error); }
static bool saved_identity(frontend_scene_identity_scope *scope,scene_row_kind kind,scene_origin origin,
    size_t node,size_t ordinal,uint64_t actual,uint64_t *out,qa_error *error)
{
    const scene_row *row=scope?position(scope->space,kind,origin,scope->owner,node,ordinal):NULL;
    if (!row || !row->qualified || !actual || row->installed!=actual || !row->saved || !out)
        return fail(error,QA_ERROR_FORMAT,"Canonical scene identity leaves its real installed producer");
    *out=row->saved; return true;
}
bool frontend_scene_world_saved(void *context,qa_scene_world_identity_kind kind,size_t ordinal,
    uint64_t actual,uint64_t *out,qa_error *error)
{
    if (kind>QA_SCENE_WORLD_IDENTITY_MESH) return fail(error,QA_ERROR_FORMAT,"Unknown canonical world identity domain");
    return saved_identity(context,kind==QA_SCENE_WORLD_IDENTITY_WORLD?SCENE_WORLD:
        kind==QA_SCENE_WORLD_IDENTITY_MODEL?SCENE_MODEL:SCENE_MESH,SCENE_WORLD_OWNER,0,ordinal,actual,out,error);
}
bool frontend_scene_model_saved(void *context,qa_scene_model_identity_kind kind,size_t node,size_t ordinal,
    uint64_t actual,uint64_t *out,qa_error *error)
{
    if (kind>QA_SCENE_MODEL_IDENTITY_SHADOW) return fail(error,QA_ERROR_FORMAT,"Unknown canonical model identity domain");
    return saved_identity(context,kind==QA_SCENE_MODEL_IDENTITY_MODEL?SCENE_MODEL:
        kind==QA_SCENE_MODEL_IDENTITY_MESH?SCENE_MESH:SCENE_SHADOW,SCENE_MODEL_OWNER,node,ordinal,actual,out,error);
}
bool frontend_scene_light_saved(frontend_scene_identity_scope *scope,size_t ordinal,uint64_t actual,uint64_t *out,qa_error *error)
{ return saved_identity(scope,SCENE_LIGHT,SCENE_LIGHT_OWNER,0,ordinal,actual,out,error); }
bool frontend_scene_static_audio_saved(frontend_scene_identity_scope *scope,size_t ordinal,uint64_t actual,uint64_t *out,qa_error *error)
{ return saved_identity(scope,SCENE_STATIC_AUDIO,SCENE_STATIC_AUDIO_OWNER,0,ordinal,actual,out,error); }
static bool number_qualify(frontend_scene_namespace *space, scene_row_kind kind, scene_origin origin,
    uint64_t owner, size_t node, size_t ordinal, uint64_t installed, qa_error *error)
{
    scene_row *row = position(space, kind, origin, owner, node, ordinal);
    if (!row || !installed || row->installed != installed)
        return fail(error, QA_ERROR_FORMAT, "Decoded scene owner differs from its namespace prefix");
    row->qualified = true; return true;
}
static bool graph_number(frontend_scene_namespace *space, bool capture, scene_row_kind kind,
    scene_origin origin, uint64_t owner, size_t node, size_t ordinal, uint64_t value, qa_error *error)
{
    return capture ? number_add(space, kind, origin, owner, node, ordinal, value, error) :
        number_qualify(space, kind, origin, owner, node, ordinal, value, error);
}
static bool graph_geometry(frontend_scene_namespace *space, bool capture, scene_origin origin,
    uint64_t owner, size_t node, size_t ordinal, const qa_scene_geometry *geometry, qa_error *error)
{
    if (capture) return geometry_add(space, origin, owner, node, ordinal, geometry, error);
    uint64_t key = 0;
    if (!frontend_scene_geometry_encode(space, geometry, &key, error)) return false;
    scene_row *row = position(space, SCENE_GEOMETRY, origin, owner, node, ordinal);
    if (row && row->pointer != geometry)
        return fail(error, QA_ERROR_FORMAT, "Decoded geometry breaks a retained producer alias");
    if (row) row->qualified = true;
    return true;
}
bool frontend_scene_namespace_qualify_renderer_geometry(frontend_scene_namespace *space,uint64_t owner,
    size_t ordinal,const qa_scene_geometry *geometry,qa_error *error)
{
    if (!open(space,true,error) || !owner)
        return fail(error,QA_ERROR_ARGUMENT,"Renderer geometry qualification requires its actual decoded holder row");
    return graph_geometry(space,false,SCENE_RENDERER_OWNER,owner,0,ordinal,geometry,error);
}
bool frontend_scene_namespace_qualify_renderer_mesh(frontend_scene_namespace *space,uint64_t owner,
    size_t ordinal,uint64_t identity,qa_error *error)
{
    if (!open(space,true,error) || !owner || !identity)
        return fail(error,QA_ERROR_ARGUMENT,"Renderer mesh qualification requires its actual decoded cache row");
    scene_row *row=position(space,SCENE_MESH,SCENE_RENDERER_OWNER,owner,0,ordinal);
    if (row) return number_qualify(space,SCENE_MESH,SCENE_RENDERER_OWNER,owner,0,ordinal,identity,error);
    uint64_t key=0;
    return frontend_scene_mesh_identity_encode(space,identity,&key,error);
}
static bool world_rows(frontend_scene_namespace *space, uint64_t owner, const qa_scene_world *world,
    bool capture, qa_error *error)
{
    if (!graph_number(space, capture, SCENE_WORLD, SCENE_WORLD_OWNER, owner, 0, 0,
        qa_scene_world_identity(world), error)) return false;
    scene_row *root=position(space,SCENE_WORLD,SCENE_WORLD_OWNER,owner,0,0);
    if(!root || (root->pointer && root->pointer!=world))
        return fail(error,QA_ERROR_FORMAT,"World namespace differs from its actual retained root");
    root->pointer=world;
    for (size_t i = 0; i < qa_scene_world_model_count(world); ++i)
        if (!graph_number(space, capture, SCENE_MODEL, SCENE_WORLD_OWNER, owner, 0, i,
            qa_scene_world_model_identity_at(world, i), error)) return false;
    for (size_t i = 0;; ++i) {
        const qa_scene_mesh *mesh = qa_scene_world_mesh_at(world, i);
        if (!mesh) break;
        if (mesh->geometry && !graph_geometry(space, capture, SCENE_WORLD_OWNER, owner, 0, i, mesh->geometry, error)) return false;
        if (mesh->identity && (!mesh->geometry || !graph_number(space, capture, SCENE_MESH,
            SCENE_WORLD_OWNER, owner, 0, i, mesh->identity, error))) return false;
    }
    return true;
}
static bool model_rows(frontend_scene_namespace *space, uint64_t owner, const qa_scene_model *root,
    bool capture, qa_error *error)
{
    scene_models nodes = {0}; bool ok = model_nodes(&nodes, root, error);
    for (size_t node = 0; ok && node < nodes.count; ++node) {
        const qa_scene_model *model = nodes.nodes[node]; const qa_model *source = qa_scene_model_source(model);
        ok = source && graph_number(space, capture, SCENE_MODEL, SCENE_MODEL_OWNER, owner, node, 0,
            qa_scene_model_identity(model), error);
        for (size_t i = 0; ok && i < source->mesh_count; ++i) {
            const qa_scene_mesh *mesh = qa_scene_model_mesh_at(model, i);
            ok = mesh && graph_number(space, capture, SCENE_MESH, SCENE_MODEL_OWNER, owner, node, i, mesh->identity, error) &&
                graph_geometry(space, capture, SCENE_MODEL_OWNER, owner, node, i, mesh->geometry, error);
        }
        for (size_t i = 0; ok && i < qa_scene_model_shadow_identity_count(model); ++i) {
            uint32_t entity = 0; uint64_t identity = 0;
            ok = qa_scene_model_shadow_identity_at(model, i, &entity, &identity) && graph_number(space, capture,
                SCENE_SHADOW, SCENE_MODEL_OWNER, owner, node, i, identity, error);
        }
    }
    free(nodes.nodes);
    if (!ok && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Actual model owner graph is incomplete");
    return ok;
}
bool frontend_scene_namespace_qualify_world(frontend_scene_namespace *space, uint64_t owner,
    const qa_scene_world *world, qa_error *error)
{
    if (!open(space, true, error) || !owner || !qa_scene_world_observation_ready(world))
        return fail(error, QA_ERROR_ARGUMENT, "Scene world qualification requires its decoded idle owner");
    return world_rows(space, owner, world, false, error);
}
bool frontend_scene_namespace_qualify_model(frontend_scene_namespace *space, uint64_t owner,
    const qa_scene_model *model, qa_error *error)
{
    if (!open(space, true, error) || !owner)
        return fail(error, QA_ERROR_ARGUMENT, "Scene model qualification requires its actual owner ordinal");
    return model_rows(space, owner, model, false, error);
}
bool frontend_scene_namespace_qualify_light(frontend_scene_namespace *space, uint64_t owner,
    size_t ordinal, uint64_t installed, qa_error *error)
{
    return open(space, true, error) && number_qualify(space, SCENE_LIGHT, SCENE_LIGHT_OWNER, owner, 0, ordinal, installed, error);
}
bool frontend_scene_namespace_qualify_static_audio(frontend_scene_namespace *space, uint64_t owner,
    size_t ordinal, uint64_t installed, qa_error *error)
{
    return open(space, true, error) && number_qualify(space, SCENE_STATIC_AUDIO, SCENE_STATIC_AUDIO_OWNER,
        owner, 0, ordinal, installed, error);
}
bool frontend_scene_namespace_bind_library(frontend_scene_namespace *space, uint64_t owner,
    const qa_material_library *library, qa_error *error)
{
    if (!open(space, true, error) || !owner || !library)
        return fail(error, QA_ERROR_ARGUMENT, "Scene material binding requires its decoded actual library");
    size_t count = qa_material_library_record_count(library), expected = 0;
    for (size_t i = 0; i < space->count; ++i) if (space->rows[i].kind == SCENE_MATERIAL && space->rows[i].owner == owner) ++expected;
    if (count != expected) return fail(error, QA_ERROR_FORMAT, "Decoded material library has a different physical record inventory");
    for (size_t i = 0; i < count; ++i) {
        scene_row *row = position(space, SCENE_MATERIAL, SCENE_LIBRARY, owner, 0, i);
        const qa_material *material = qa_material_library_record_at(library, i);
        if (!row || row->pointer || !material || !material->identity)
            return fail(error, QA_ERROR_FORMAT, "Material namespace row is absent or already bound");
        for (size_t j = 0; j < space->count; ++j) if (space->rows[j].pointer == material)
            return fail(error, QA_ERROR_FORMAT, "Decoded libraries share duplicate material destructor authority");
        row->pointer = material; row->installed = material->identity; row->qualified = true;
        qa_material_library_record_view record;
        if(!qa_material_library_record_read(library,i,&record)) return false;
        scene_row *history=position(space,SCENE_WORLD,SCENE_LIBRARY_WORLD_OWNER,owner,0,i);
        if(history) {
            if(history->installed!=record.world_identity)
                return fail(error,QA_ERROR_FORMAT,"Decoded material world history differs from its retained record");
            history->qualified=true;
        }
        if(record.world_identity) {
            uint64_t key=0;
            if(!frontend_scene_world_identity_encode(space,record.world_identity,&key,error)) return false;
        }
    }
    return true;
}
bool frontend_scene_namespace_bind_frame(frontend_scene_namespace *space, uint64_t owner,
    const qa_scene_frame *frame, qa_error *error)
{
    if (!open(space, true, error) || !owner || !frame)
        return fail(error, QA_ERROR_ARGUMENT, "Scene frame binding requires the installed frame address");
    scene_row *row = position(space, SCENE_FRAME, SCENE_FRAME_OWNER, owner, 0, 0);
    if (!row || row->pointer || row->saved != frame->owner)
        return fail(error, QA_ERROR_FORMAT, "Prepared frame identity differs from the actual saved owner");
    for (size_t i = 0; i < space->count; ++i) if (space->rows[i].pointer == frame)
        return fail(error, QA_ERROR_FORMAT, "Prepared frame has duplicate destructor authority");
    row->pointer = frame; row->installed = frame->owner; return true;
}
bool frontend_scene_namespace_qualify_frame(frontend_scene_namespace *space, uint64_t owner,
    const qa_scene_frame *frame, qa_error *error)
{
    if (!open(space, true, error)) return false;
    scene_row *row = position(space, SCENE_FRAME, SCENE_FRAME_OWNER, owner, 0, 0);
    if (!row || row->pointer != frame || row->saved != frame->owner || !frame_references(space, frame, error))
        return fail(error, QA_ERROR_FORMAT, "Decoded frame differs from its qualified namespace holder");
    for (size_t i = 0; i < frame->geometry_count; ++i)
        if (!graph_geometry(space, false, SCENE_FRAME_OWNER, owner, 0, i, frame->geometries[i], error)) return false;
    row->qualified = true; return true;
}
bool frontend_scene_namespace_seal(frontend_scene_namespace *space, qa_error *error)
{
    if (!space || space->sealed || !space->images_captured)
        return fail(error, QA_ERROR_ARGUMENT, "Scene namespace requires its complete actual image inventory");
    for (size_t i = 0; i < space->count; ++i) if (!space->rows[i].qualified)
        return fail(error, QA_ERROR_FORMAT, "Scene namespace contains an unqualified producer or holder");
    space->sealed = true; return true;
}
static bool row_valid(const scene_row *row)
{
    switch (row->kind) {
    case SCENE_IMAGE: return row->origin == SCENE_IMAGES && !row->owner && !row->node && row->saved;
    case SCENE_GEOMETRY: return (row->origin == SCENE_WORLD_OWNER || row->origin == SCENE_MODEL_OWNER ||
        row->origin == SCENE_FRAME_OWNER || row->origin == SCENE_RENDERER_OWNER) && row->owner && !row->saved &&
        (row->origin != SCENE_RENDERER_OWNER || !row->node);
    case SCENE_MATERIAL: return row->origin == SCENE_LIBRARY && row->owner && !row->node && row->saved;
    case SCENE_FRAME: return row->origin == SCENE_FRAME_OWNER && row->owner && !row->node && !row->ordinal;
    case SCENE_WORLD: return (row->origin == SCENE_WORLD_OWNER || row->origin == SCENE_LIBRARY_WORLD_OWNER) &&
        row->owner && !row->node && row->saved && (row->origin == SCENE_LIBRARY_WORLD_OWNER || !row->ordinal);
    case SCENE_MODEL: return (row->origin == SCENE_WORLD_OWNER || row->origin == SCENE_MODEL_OWNER) && row->owner && row->saved &&
        (row->origin == SCENE_WORLD_OWNER ? !row->node : !row->ordinal);
    case SCENE_MESH: return (row->origin == SCENE_WORLD_OWNER || row->origin == SCENE_MODEL_OWNER ||
        row->origin == SCENE_RENDERER_OWNER) && row->owner && row->saved &&
        (row->origin == SCENE_MODEL_OWNER || !row->node);
    case SCENE_SHADOW: return row->origin == SCENE_MODEL_OWNER && row->owner && row->saved;
    case SCENE_LIGHT: return row->origin == SCENE_LIGHT_OWNER && row->owner && !row->node && row->saved;
    case SCENE_STATIC_AUDIO: return row->origin == SCENE_STATIC_AUDIO_OWNER && row->owner && !row->node && row->saved;
    }
    return false;
}
static bool prefix(qa_source_save_io *io, frontend_scene_namespace *space, const qa_scene_image_set *images)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','F','S','I'}; uint32_t version = 5;
    size_t count = space->count, image_count = space->image_count;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFSI", 4) ||
        !qa_source_save_u32(io, &version) || version != 5 ||
        !qa_source_save_count(io, &count, reading ? io->input.size / 48 : SIZE_MAX / sizeof(scene_row)) ||
        !qa_source_save_count(io, &image_count, count) || (reading && image_count != qa_scene_image_set_count(images))) return false;
    if (reading) { space->image_count = image_count; space->images_captured = true; }
    size_t image_at = 0;
    for (size_t i = 0; i < count; ++i) {
        scene_row row = reading ? (scene_row){0} : space->rows[i];
        uint32_t kind = row.kind, origin = row.origin;
        if (!qa_source_save_u32(io, &kind) || kind > SCENE_STATIC_AUDIO || !qa_source_save_u32(io, &origin) || origin > SCENE_LIBRARY_WORLD_OWNER ||
            !qa_source_save_u64(io, &row.owner) || !qa_source_save_u64(io, &row.node) ||
            !qa_source_save_u64(io, &row.ordinal) || !qa_source_save_u64(io, &row.saved)) return false;
        row.kind = (scene_row_kind)kind; row.origin = (scene_origin)origin;
        if (!row_valid(&row)) return false;
        if (reading) {
            if (position(space, row.kind, row.origin, row.owner, row.node, row.ordinal)) return false;
            for (size_t j = 0; j < space->count; ++j) if (row.saved && row.kind != SCENE_FRAME &&
                space->rows[j].kind != SCENE_FRAME && space->rows[j].saved == row.saved &&
                !(row.kind == SCENE_IMAGE && space->rows[j].kind == SCENE_IMAGE)) return false;
        }
        qa_buffer geometry = {0};
        if (!reading && row.kind == SCENE_GEOMETRY && !qa_scene_geometry_checkpoint(row.pointer, &geometry, io->error)) return false;
        size_t bytes = geometry.size;
        bool ok = qa_source_save_count(io, &bytes, reading ? io->input.size - io->offset : SIZE_MAX);
        if (ok && reading) {
            if (bytes > io->input.size - io->offset || (row.kind != SCENE_GEOMETRY && bytes)) ok = false;
            else if (row.kind == SCENE_GEOMETRY) {
                qa_scene_geometry *installed = NULL;
                ok = qa_scene_geometry_restore((qa_bytes){io->input.data + io->offset, bytes}, &installed, io->error);
                if (ok) { row.pointer = installed; io->offset += bytes; }
            }
        } else if (ok) ok = qa_source_save_bytes(io, geometry.data, bytes);
        qa_buffer_free(&geometry);
        if (!ok) return false;
        if (row.kind == SCENE_IMAGE) {
            if (row.ordinal != image_at++) return false;
            if (reading) {
                const qa_scene_image *image = qa_scene_image_set_at(images, (size_t)row.ordinal);
                if (!image || !image->identity) return false;
                for (size_t j = 0; j < space->count; ++j) if (space->rows[j].kind == SCENE_IMAGE &&
                    ((space->rows[j].saved == row.saved) != (space->rows[j].installed == image->identity))) return false;
                row.pointer = image; row.installed = image->identity; row.qualified = true;
                qa_scene_image_retain(image);
            }
        } else if (reading && row.kind >= SCENE_WORLD) {
            row.installed = qa_scene_identity();
            if (!row.installed) return false;
        }
        if (reading && !append(space, row, io->error)) {
            if (row.kind == SCENE_GEOMETRY && row.pointer) qa_scene_geometry_release(row.pointer);
            if (row.kind == SCENE_IMAGE && row.pointer) qa_scene_image_release(row.pointer);
            return false;
        }
    }
    return image_at == image_count;
}
bool frontend_scene_namespace_checkpoint(const frontend_scene_namespace *space, qa_buffer *out, qa_error *error)
{
    if (!space || !space->sealed || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "Scene prefix capture requires a sealed graph and empty output");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && prefix(&io, (frontend_scene_namespace *)space, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Scene namespace prefix is incomplete");
    return ok;
}
bool frontend_scene_namespace_rebase_capture(frontend_scene_namespace *fresh,const frontend_scene_namespace *installed,qa_error *error)
{
    if (!fresh || !installed || fresh==installed || fresh->restoring || !fresh->sealed ||
        !installed->restoring || !installed->sealed || fresh->count!=installed->count || fresh->image_count!=installed->image_count)
        return fail(error,QA_ERROR_ARGUMENT,"Canonical recapture needs complete fresh and installed scene dictionaries");
    for (size_t i=0;i<fresh->count;++i) {
        const scene_row *a=fresh->rows+i,*b=installed->rows+i;
        if (!a->qualified || !b->qualified || a->kind!=b->kind || a->origin!=b->origin ||
            a->owner!=b->owner || a->node!=b->node || a->ordinal!=b->ordinal || a->pointer!=b->pointer || a->installed!=b->installed)
            return fail(error,QA_ERROR_FORMAT,"Fresh scene producer inventory differs from the fully imported physical graph");
    }
    for (size_t i=0;i<fresh->count;++i) fresh->rows[i].saved=installed->rows[i].saved;
    return true;
}
bool frontend_scene_namespace_restore(qa_bytes bytes, const qa_scene_image_set *images,
    frontend_scene_namespace **out, qa_error *error)
{
    if (!out || *out || (bytes.size && !bytes.data))
        return fail(error, QA_ERROR_ARGUMENT, "Scene prefix restore requires actual bytes and an empty output");
    frontend_scene_namespace *space = NULL; qa_source_save_io io = {0};
    bool ok = frontend_scene_namespace_create(&space, error);
    if (ok) { space->restoring = true; ok = qa_source_save_reader(&io, NULL, bytes, error) &&
        prefix(&io, space, images) && qa_source_save_finish(&io, NULL); }
    if (ok) *out = space;
    else {
        frontend_scene_namespace_destroy(space);
        if (error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Saved scene namespace does not match actual restored holders");
    }
    qa_source_save_dispose(&io); return ok;
}
