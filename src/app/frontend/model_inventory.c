#include "model_inventory.h"
#include "qa/source_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct model_holder {
    frontend_model_source source;
    qa_model owned;
    uint64_t pool, resource, view;
    uint64_t parent;
    size_t references, dependents;
} model_holder;
typedef struct animation_holder {
    frontend_animation_source source;
    qa_model_animation owned;
    uint64_t pool, resource, view, scale_pool, scale_resource;
    size_t references;
} animation_holder;
struct frontend_model_inventory {
    qa_application_content_graph *graph;
    model_holder *models;
    animation_holder *animations;
    size_t model_count, animation_count;
    size_t references;
    bool owns_holders, staging, owner_retired;
};
struct frontend_model_lease { frontend_model_inventory *inventory; size_t index; };
struct frontend_animation_lease { frontend_model_inventory *inventory; size_t index; };
static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
static bool same_bytes(qa_bytes a, qa_bytes b)
{ return a.size == b.size && (!a.size || (a.data && b.data && !memcmp(a.data, b.data, a.size))); }
static bool product(size_t a, size_t b, size_t *out)
{ if (a && b > SIZE_MAX / a) return false; *out = a * b; return true; }
static bool floats(qa_source_save_io *io, float *values, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (!qa_source_save_f32(io, &values[i]) || !isfinite(values[i])) return false;
    return true;
}
static bool fixed_name(qa_source_save_io *io, char *name, size_t count)
{ return qa_source_save_bytes(io, name, count) && memchr(name, 0, count); }
static bool bounds(qa_source_save_io *io, qa_model_bounds *value)
{ return floats(io, value->min, 3) && floats(io, value->max, 3); }
static bool pose(qa_source_save_io *io, qa_model_pose *value)
{ return floats(io, value->position, 3) && floats(io, value->orientation, 4) && floats(io, &value->scale, 1); }
static bool array(qa_source_save_io *io, void **values, size_t count, size_t size, size_t wire_size, bool required)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, present = *values != NULL;
    if (!qa_source_save_bool(io, &present) || (count && required && !present) ||
        (size && count > SIZE_MAX / size)) return false;
    if (!present) return true;
    if (reading) {
        if (io->offset > io->input.size || (wire_size && count > (io->input.size - io->offset) / wire_size)) return false;
        *values = calloc(count ? count : 1, size);
        if (!*values) return fail(io->error, QA_ERROR_MEMORY, "Allocating immutable parsed model arrays");
    }
    return true;
}
static bool source_buffer(qa_source_save_io *io, qa_buffer *value, const qa_resource *resource)
{
    qa_bytes actual = qa_resource_bytes(resource);
    if (!resource || !actual.data || actual.size < 4) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        value->data = malloc(actual.size); value->size = actual.size;
        if (!value->data) return fail(io->error, QA_ERROR_MEMORY, "Retaining immutable model source bytes");
        memcpy(value->data, actual.data, actual.size);
        return true;
    }
    return value->data && same_bytes((qa_bytes){value->data, value->size}, actual);
}
/* All borrowed strings, pixels and packed records retain their original offset
 * in the one owned source buffer. Zero-length present views remain present. */
static bool source_view(qa_source_save_io *io, const qa_buffer *source, qa_bytes *view)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, present = view->data != NULL;
    uint64_t offset = 0, size = view->size;
    if (!reading && present) {
        uintptr_t base = (uintptr_t)source->data, pointer = (uintptr_t)view->data;
        if (pointer < base || pointer - base > source->size || view->size > source->size - (size_t)(pointer - base)) return false;
        offset = pointer - base;
    }
    if (!qa_source_save_bool(io, &present) || !qa_source_save_u64(io, &offset) ||
        !qa_source_save_u64(io, &size) || offset > source->size || size > source->size - (size_t)offset ||
        (!present && (size || offset))) return false;
    if (reading) *view = present ? (qa_bytes){source->data + (size_t)offset, (size_t)size} : (qa_bytes){0};
    return true;
}
static bool bone(qa_source_save_io *io, const qa_buffer *source, qa_model_bone *value, size_t parent_count)
{
    return fixed_name(io, value->name, sizeof(value->name)) && qa_source_save_i32(io, &value->parent) &&
        value->parent >= -1 && (value->parent < 0 || (uint32_t)value->parent < parent_count) &&
        source_view(io, source, &value->text_name);
}
static bool groups(qa_source_save_io *io, qa_model_group **values, uint32_t count, uint32_t target)
{
    if (!array(io, (void **)values, count, sizeof(**values), 33, true)) return false;
    for (size_t i = 0; i < count; ++i) {
        qa_model_group *value = &(*values)[i];
        if (!qa_source_save_u32(io, &value->first) || !qa_source_save_u32(io, &value->count) ||
            value->first > target || !value->count || value->count > target - value->first ||
            !bounds(io, &value->bounds) ||
            !array(io, (void **)&value->intervals, value->count, sizeof(*value->intervals), 4, false)) return false;
        if (value->intervals) {
            if (!floats(io, value->intervals, value->count)) return false;
            for (size_t j = 0; j < value->count; ++j)
                if (value->intervals[j] <= 0) return false;
        }
    }
    return true;
}
static bool mesh(qa_source_save_io *io, qa_model *model, qa_model_mesh *value)
{
    if (!fixed_name(io, value->name, sizeof(value->name)) || !qa_source_save_i32(io, &value->flags) ||
        !qa_source_save_u32(io, &value->vertex_count) || !qa_source_save_u32(io, &value->texcoord_count) ||
        !qa_source_save_u32(io, &value->triangle_count) || !qa_source_save_u32(io, &value->frame_count) ||
        !qa_source_save_u32(io, &value->shader_count) || !qa_source_save_u32(io, &value->weight_count) ||
        !qa_source_save_u32(io, &value->bone_reference_count)) return false;
    bool skeletal = model->format == QA_MODEL_MD4 || model->format == QA_MODEL_MD5;
    size_t vertices;
    if (!product(value->vertex_count, skeletal ? 1 : value->frame_count, &vertices) ||
        !array(io, (void **)&value->vertices, vertices, sizeof(*value->vertices), 24, true)) return false;
    for (size_t i = 0; i < vertices; ++i)
        if (!floats(io, value->vertices[i].position, 3) || !floats(io, value->vertices[i].normal, 3)) return false;
    if (!array(io, (void **)&value->texcoords, value->texcoord_count, sizeof(*value->texcoords), 17, true)) return false;
    for (size_t i = 0; i < value->texcoord_count; ++i) {
        qa_model_texcoord *uv = &value->texcoords[i];
        if (!floats(io, uv->uv, 2) || !qa_source_save_i32(io, &uv->s) || !qa_source_save_i32(io, &uv->t) ||
            !qa_source_save_bool(io, &uv->on_seam)) return false;
    }
    if (!array(io, (void **)&value->triangles, value->triangle_count, sizeof(*value->triangles), 25, true)) return false;
    for (size_t i = 0; i < value->triangle_count; ++i) {
        qa_model_triangle *triangle = &value->triangles[i];
        for (size_t j = 0; j < 3; ++j)
            if (!qa_source_save_u32(io, &triangle->vertex[j]) || triangle->vertex[j] >= value->vertex_count ||
                !qa_source_save_u32(io, &triangle->texcoord[j]) || triangle->texcoord[j] >= value->texcoord_count) return false;
        if (!qa_source_save_bool(io, &triangle->front)) return false;
    }
    if (!array(io, (void **)&value->shaders, value->shader_count, sizeof(*value->shaders), 86, true)) return false;
    for (size_t i = 0; i < value->shader_count; ++i)
        if (!fixed_name(io, value->shaders[i].name, sizeof(value->shaders[i].name)) ||
            !qa_source_save_i32(io, &value->shaders[i].index) ||
            !source_view(io, &model->source, &value->shaders[i].text_name)) return false;
    if (!array(io, (void **)&value->weights, value->weight_count, sizeof(*value->weights), 20, true)) return false;
    for (size_t i = 0; i < value->weight_count; ++i)
        if (!qa_source_save_u32(io, &value->weights[i].bone) || value->weights[i].bone >= model->bone_count ||
            !floats(io, &value->weights[i].bias, 1) || !floats(io, value->weights[i].offset, 3)) return false;
    if (!array(io, (void **)&value->vertex_weights, value->vertex_count, sizeof(*value->vertex_weights), 8, skeletal)) return false;
    if (value->vertex_weights) for (size_t i = 0; i < value->vertex_count; ++i) {
        qa_model_weight_range *range = &value->vertex_weights[i];
        if (!qa_source_save_u32(io, &range->first) || !qa_source_save_u32(io, &range->count) ||
            range->first > value->weight_count || range->count > value->weight_count - range->first) return false;
    }
    if (!array(io, (void **)&value->bone_references, value->bone_reference_count, sizeof(*value->bone_references), 4, true)) return false;
    for (size_t i = 0; i < value->bone_reference_count; ++i)
        if (!qa_source_save_u32(io, &value->bone_references[i]) || value->bone_references[i] >= model->bone_count) return false;
    return value->frame_count == model->frame_count && (!skeletal ?
        !value->weights && !value->vertex_weights && !value->bone_references : true);
}
static bool model_shape(const qa_model *value)
{
    const char *magic = NULL;
    switch (value->format) {
    case QA_MODEL_MDL: magic = "IDPO"; break;
    case QA_MODEL_MD2: magic = "IDP2"; break;
    case QA_MODEL_MD3: magic = "IDP3"; break;
    case QA_MODEL_MD4: magic = "IDP4"; break;
    case QA_MODEL_MD5: break;
    case QA_MODEL_SPR: magic = "IDSP"; break;
    case QA_MODEL_SP2: magic = "IDS2"; break;
    }
    if (magic && (value->source.size < 4 || memcmp(value->source.data, magic, 4))) return false;
    bool sprite = value->format == QA_MODEL_SPR || value->format == QA_MODEL_SP2;
    bool alias = value->format == QA_MODEL_MDL || value->format == QA_MODEL_MD2;
    if (sprite ? (value->mesh_count || value->frame_count || !value->sprite_count || !value->frame_group_count) :
        (!value->frame_count || value->sprite_count)) return false;
    if ((value->skin_count || value->skin_group_count) && !alias) return false;
    if (value->tag_count && value->format != QA_MODEL_MD3) return false;
    if (value->lod_count && value->format != QA_MODEL_MD4) return false;
    if (value->gl_command_count && value->format != QA_MODEL_MD2) return false;
    if (value->bone_count && value->format != QA_MODEL_MD4 && value->format != QA_MODEL_MD5) return false;
    if (value->frame_groups && !sprite && value->format != QA_MODEL_MDL) return false;
    if (alias && (value->mesh_count != 1 || !value->skin_width || !value->skin_height)) return false;
    if (value->format == QA_MODEL_MD5 && (value->frame_count != 1 || !value->bone_count || !value->command_line.data)) return false;
    if ((value->frames != NULL) != (value->format <= QA_MODEL_MD5)) return false;
    for (size_t i = 0; i < value->skin_count; ++i) {
        size_t pixels;
        if (value->format == QA_MODEL_MDL && (!product(value->skin_width, value->skin_height, &pixels) ||
            !value->skins[i].pixels.data || value->skins[i].pixels.size != pixels)) return false;
        if (value->format == QA_MODEL_MD2 && (value->skins[i].pixels.data || value->skins[i].pixels.size)) return false;
    }
    for (size_t i = 0; i < value->sprite_count; ++i) {
        size_t pixels; const qa_model_sprite *sprite_value = &value->sprites[i];
        if (!sprite_value->width || !sprite_value->height) return false;
        if (value->format == QA_MODEL_SPR && (!product(sprite_value->width, sprite_value->height, &pixels) ||
            !sprite_value->pixels.data || sprite_value->pixels.size != pixels)) return false;
        if (value->format == QA_MODEL_SP2 && (sprite_value->pixels.data || sprite_value->pixels.size)) return false;
    }
    if (value->format == QA_MODEL_MD2) {
        size_t packed;
        if (!product(value->meshes[0].vertex_count, 4, &packed)) return false;
        for (size_t i = 0; i < value->frame_count; ++i)
            if (!value->frames[i].packed_vertices.data || value->frames[i].packed_vertices.size != packed) return false;
    }
    return true;
}
static bool subset_aliases(const qa_model *a, const qa_model *b)
{
    if (!a || !b || a==b || a->format!=QA_MODEL_MDL || b->format!=QA_MODEL_MDL ||
        a->mesh_count!=1 || b->mesh_count!=1 || !a->meshes || !b->meshes || a->meshes==b->meshes ||
        memcmp(a->name,b->name,sizeof(a->name)) || a->flags!=b->flags || a->sync!=b->sync ||
        a->orientation!=b->orientation || memcmp(&a->radius,&b->radius,sizeof(a->radius)) ||
        memcmp(&a->size,&b->size,sizeof(a->size)) || memcmp(&a->beam_length,&b->beam_length,sizeof(a->beam_length)) ||
        memcmp(a->scale,b->scale,sizeof(a->scale)) || memcmp(a->translation,b->translation,sizeof(a->translation)) ||
        memcmp(a->eye_position,b->eye_position,sizeof(a->eye_position)) ||
        a->skin_width!=b->skin_width || a->skin_height!=b->skin_height || a->declared_skin_count!=b->declared_skin_count ||
        memcmp(&a->bounds,&b->bounds,sizeof(a->bounds)) ||
        a->frame_count!=b->frame_count || a->frame_group_count!=b->frame_group_count ||
        a->skin_count!=b->skin_count || a->skin_group_count!=b->skin_group_count ||
        a->tag_count!=b->tag_count || a->sprite_count!=b->sprite_count || a->lod_count!=b->lod_count ||
        a->bone_count!=b->bone_count || a->gl_command_count!=b->gl_command_count ||
        a->frames!=b->frames || a->frame_groups!=b->frame_groups || a->skin_groups!=b->skin_groups ||
        a->skins!=b->skins || a->tags!=b->tags || a->sprites!=b->sprites || a->lods!=b->lods ||
        a->bones!=b->bones || a->bone_matrices!=b->bone_matrices || a->bind_pose!=b->bind_pose ||
        a->gl_commands!=b->gl_commands || a->command_line.data!=b->command_line.data ||
        a->command_line.size!=b->command_line.size || a->source.data!=b->source.data || a->source.size!=b->source.size)
        return false;
    const qa_model_mesh *x=a->meshes,*y=b->meshes;
    return !memcmp(x->name,y->name,sizeof(x->name)) && x->flags==y->flags &&
        x->vertex_count==y->vertex_count && x->texcoord_count==y->texcoord_count &&
        x->frame_count==y->frame_count && x->shader_count==y->shader_count &&
        x->weight_count==y->weight_count && x->bone_reference_count==y->bone_reference_count &&
        x->vertices==y->vertices && x->texcoords==y->texcoords && x->shaders==y->shaders &&
        x->weights==y->weights && x->vertex_weights==y->vertex_weights && x->bone_references==y->bone_references &&
        x->triangles && x->triangles!=y->triangles && x->triangle_count && x->triangle_count<=y->triangle_count;
}
static bool subset_fields(qa_source_save_io *io, model_holder *holder, const model_holder *parent)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    const qa_model *base=parent->source.model;
    const qa_model_mesh *source=base?base->meshes:NULL;
    uint32_t count=reading?0:holder->source.model->meshes->triangle_count;
    if (!base || base->format!=QA_MODEL_MDL || base->mesh_count!=1 || !source ||
        !qa_source_save_u32(io,&count) || !count || count>source->triangle_count ||
        (reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/25))) return false;
    if (reading) {
        qa_model_mesh *owned=calloc(1,sizeof(*owned));
        qa_model_triangle *triangles=calloc(count,sizeof(*triangles));
        if (!owned || !triangles) {
            free(owned); free(triangles);
            return fail(io->error,QA_ERROR_MEMORY,"Retaining actual borrowed held-model subset");
        }
        holder->owned=*base; *owned=*source;
        owned->triangles=triangles; owned->triangle_count=count;
        holder->owned.meshes=owned;
        holder->source.parent=base;
    } else if (!subset_aliases(holder->source.model,base)) return false;
    qa_model_triangle *triangles=reading?holder->owned.meshes->triangles:holder->source.model->meshes->triangles;
    size_t source_at=0;
    for (size_t i=0;i<count;++i) {
        qa_model_triangle *triangle=triangles+i;
        for (size_t j=0;j<3;++j)
            if (!qa_source_save_u32(io,&triangle->vertex[j]) || triangle->vertex[j]>=source->vertex_count ||
                !qa_source_save_u32(io,&triangle->texcoord[j]) || triangle->texcoord[j]>=source->texcoord_count) return false;
        if (!qa_source_save_bool(io,&triangle->front)) return false;
        bool found=false;
        while (source_at<source->triangle_count) {
            const qa_model_triangle *actual=source->triangles+source_at++;
            if (actual->front==triangle->front && !memcmp(actual->vertex,triangle->vertex,sizeof(actual->vertex)) &&
                !memcmp(actual->texcoord,triangle->texcoord,sizeof(actual->texcoord))) { found=true; break; }
        }
        if (!found) return false;
    }
    return true;
}
static bool model_fields(qa_source_save_io *io, qa_model *value, const qa_resource *resource)
{
    uint32_t format = value->format;
    if (!source_buffer(io, &value->source, resource) || !qa_source_save_u32(io, &format) || format > QA_MODEL_SP2 ||
        !fixed_name(io, value->name, sizeof(value->name)) || !qa_source_save_i32(io, &value->flags) ||
        !qa_source_save_i32(io, &value->sync) || !qa_source_save_i32(io, &value->orientation) ||
        !floats(io, &value->radius, 1) || !floats(io, &value->size, 1) || !floats(io, &value->beam_length, 1) ||
        !floats(io, value->scale, 3) || !floats(io, value->translation, 3) || !floats(io, value->eye_position, 3) ||
        !qa_source_save_u32(io, &value->skin_width) || !qa_source_save_u32(io, &value->skin_height) ||
        !qa_source_save_u32(io, &value->declared_skin_count) || !bounds(io, &value->bounds)) return false;
    value->format = (qa_model_format)format;
    uint32_t *counts[] = {&value->mesh_count, &value->frame_count, &value->frame_group_count, &value->skin_count,
        &value->skin_group_count, &value->tag_count, &value->sprite_count, &value->lod_count, &value->bone_count, &value->gl_command_count};
    for (size_t i = 0; i < sizeof(counts) / sizeof(*counts); ++i) if (!qa_source_save_u32(io, counts[i])) return false;
    if (!source_view(io, &value->source, &value->command_line) ||
        !array(io, (void **)&value->meshes, value->mesh_count, sizeof(*value->meshes), 102, true)) return false;
    for (size_t i = 0; i < value->mesh_count; ++i) if (!mesh(io, value, &value->meshes[i])) return false;
    bool has_frames = value->format <= QA_MODEL_MD5;
    if (!array(io, (void **)&value->frames, value->frame_count, sizeof(*value->frames), 98, has_frames)) return false;
    if (value->frames) for (size_t i = 0; i < value->frame_count; ++i) {
        qa_model_frame *frame = &value->frames[i];
        if (!fixed_name(io, frame->name, sizeof(frame->name)) || !bounds(io, &frame->bounds) ||
            !floats(io, frame->origin, 3) || !floats(io, &frame->radius, 1) ||
            !floats(io, frame->scale, 3) || !floats(io, frame->translation, 3) ||
            !source_view(io, &value->source, &frame->packed_vertices)) return false;
    }
    uint32_t group_target = value->format == QA_MODEL_SPR || value->format == QA_MODEL_SP2 ? value->sprite_count : value->frame_count;
    if (!groups(io, &value->frame_groups, value->frame_group_count, group_target) ||
        !groups(io, &value->skin_groups, value->skin_group_count, value->skin_count) ||
        !array(io, (void **)&value->skins, value->skin_count, sizeof(*value->skins), 82, true)) return false;
    for (size_t i = 0; i < value->skin_count; ++i)
        if (!fixed_name(io, value->skins[i].name, sizeof(value->skins[i].name)) ||
            !source_view(io, &value->source, &value->skins[i].pixels)) return false;
    size_t tags, matrices;
    if (!product(value->frame_count, value->tag_count, &tags) ||
        !array(io, (void **)&value->tags, tags, sizeof(*value->tags), 113, true)) return false;
    for (size_t i = 0; i < tags; ++i) {
        if (!fixed_name(io, value->tags[i].name, sizeof(value->tags[i].name)) ||
            !floats(io, value->tags[i].origin, 3)) return false;
        for (size_t axis = 0; axis < 3; ++axis) if (!floats(io, value->tags[i].axes[axis], 3)) return false;
    }
    if (!array(io, (void **)&value->sprites, value->sprite_count, sizeof(*value->sprites), 98, true)) return false;
    for (size_t i = 0; i < value->sprite_count; ++i) {
        qa_model_sprite *sprite = &value->sprites[i];
        if (!qa_source_save_u32(io, &sprite->width) || !qa_source_save_u32(io, &sprite->height) ||
            !qa_source_save_i32(io, &sprite->origin_x) || !qa_source_save_i32(io, &sprite->origin_y) ||
            !fixed_name(io, sprite->image, sizeof(sprite->image)) || !source_view(io, &value->source, &sprite->pixels)) return false;
    }
    if (!array(io, (void **)&value->lods, value->lod_count, sizeof(*value->lods), 8, true)) return false;
    for (size_t i = 0; i < value->lod_count; ++i)
        if (!qa_source_save_u32(io, &value->lods[i].first_mesh) || !qa_source_save_u32(io, &value->lods[i].mesh_count) ||
            value->lods[i].first_mesh > value->mesh_count || value->lods[i].mesh_count > value->mesh_count - value->lods[i].first_mesh) return false;
    if (!array(io, (void **)&value->bones, value->bone_count, sizeof(*value->bones), 86, true)) return false;
    for (size_t i = 0; i < value->bone_count; ++i)
        if (!bone(io, &value->source, &value->bones[i], value->bone_count)) return false;
    if (!product(value->frame_count, value->bone_count, &matrices) || !product(matrices, 12, &matrices) ||
        !array(io, (void **)&value->bone_matrices, matrices, sizeof(*value->bone_matrices), 4, value->format == QA_MODEL_MD4)) return false;
    if (value->bone_matrices && !floats(io, value->bone_matrices, matrices)) return false;
    if (!array(io, (void **)&value->bind_pose, value->bone_count, sizeof(*value->bind_pose), 32, value->format == QA_MODEL_MD5)) return false;
    if (value->bind_pose) for (size_t i = 0; i < value->bone_count; ++i) if (!pose(io, &value->bind_pose[i])) return false;
    if (!array(io, (void **)&value->gl_commands, value->gl_command_count, sizeof(*value->gl_commands), 4, true)) return false;
    for (size_t i = 0; i < value->gl_command_count; ++i) if (!qa_source_save_i32(io, &value->gl_commands[i])) return false;
    return (!value->bone_matrices || value->format == QA_MODEL_MD4) &&
        (!value->bind_pose || value->format == QA_MODEL_MD5) && model_shape(value);
}
static bool animation_fields(qa_source_save_io *io, qa_model_animation *value, const qa_resource *resource)
{
    if (!source_buffer(io, &value->source, resource) || !qa_source_save_u32(io, &value->frame_count) ||
        !qa_source_save_u32(io, &value->joint_count) || !qa_source_save_u32(io, &value->component_count) ||
        !qa_source_save_u32(io, &value->frame_rate) || !value->frame_count || !value->joint_count || !value->frame_rate ||
        !source_view(io, &value->source, &value->command_line) ||
        !array(io, (void **)&value->joints, value->joint_count, sizeof(*value->joints), 95, true)) return false;
    for (size_t i = 0; i < value->joint_count; ++i) {
        qa_model_animation_joint *joint = &value->joints[i]; unsigned components = 0;
        if (!bone(io, &value->source, &joint->bone, i) || !qa_source_save_u32(io, &joint->flags) || joint->flags > 63 ||
            !qa_source_save_u32(io, &joint->first_component) || !qa_source_save_bool(io, &joint->scale_positions)) return false;
        for (unsigned j = 0; j < 6; ++j) if (joint->flags & (1u << j)) ++components;
        if (joint->first_component > value->component_count || components > value->component_count - joint->first_component) return false;
    }
    size_t poses, components;
    if (!product(value->frame_count, value->joint_count, &poses) ||
        !product(value->frame_count, value->component_count, &components)) return false;
    qa_model_pose **arrays[] = {&value->base_pose, &value->local_poses, &value->poses};
    const size_t counts[] = {value->joint_count, poses, poses};
    for (size_t i = 0; i < 3; ++i) {
        if (!array(io, (void **)arrays[i], counts[i], sizeof(**arrays[i]), 32, true)) return false;
        for (size_t j = 0; j < counts[i]; ++j) if (!pose(io, &(*arrays[i])[j])) return false;
    }
    if (!array(io, (void **)&value->bounds, value->frame_count, sizeof(*value->bounds), 24, true)) return false;
    for (size_t i = 0; i < value->frame_count; ++i) if (!bounds(io, &value->bounds[i])) return false;
    return array(io, (void **)&value->components, components, sizeof(*value->components), 4, true) &&
        (!value->components || floats(io, value->components, components));
}
static bool source_identity(qa_application_content_graph *graph, const qa_resource *resource,
    const qa_vfs *files, uint64_t *pool, uint64_t *version, uint64_t *view, qa_error *error)
{
    if (!graph || !resource || !files || !qa_application_content_resource_id(graph, resource, pool, version) ||
        !(*view = qa_application_content_view_id(graph, files)) ||
        qa_vfs_resources(files) != qa_application_content_pool(graph, *pool))
        return fail(error, QA_ERROR_FORMAT, "Parsed model source is outside the retained content graph");
    return true;
}
static void dispose(frontend_model_inventory *inventory)
{
    if (!inventory) return;
    for (size_t i = 0; inventory->models && i < inventory->model_count; ++i) {
        if (inventory->owns_holders) {
            model_holder *holder=&inventory->models[i];
            if (holder->parent) {
                if (holder->owned.meshes) free(holder->owned.meshes->triangles);
                free(holder->owned.meshes);
            } else qa_model_free(&holder->owned);
        }
        qa_resource_release((qa_resource *)inventory->models[i].source.resource);
    }
    for (size_t i = 0; inventory->animations && i < inventory->animation_count; ++i) {
        if (inventory->owns_holders) qa_model_animation_free(&inventory->animations[i].owned);
        qa_resource_release((qa_resource *)inventory->animations[i].source.resource);
        qa_resource_release((qa_resource *)inventory->animations[i].source.scale_resource);
    }
    free(inventory->models); free(inventory->animations); free(inventory);
}
static void model_retire(frontend_model_inventory *inventory, size_t index)
{
    model_holder *holder = &inventory->models[index];
    if (!inventory->owns_holders || inventory->staging || holder->references || holder->dependents || !holder->source.model) return;
    uint64_t parent=holder->parent;
    if (parent) {
        if (holder->owned.meshes) free(holder->owned.meshes->triangles);
        free(holder->owned.meshes); holder->owned=(qa_model){0};
    } else qa_model_free(&holder->owned);
    qa_resource_release((qa_resource *)holder->source.resource);
    holder->source = (frontend_model_source){0};
    if (parent) {
        --inventory->models[parent-1].dependents;
        model_retire(inventory,(size_t)parent-1);
    }
}
static void animation_retire(frontend_model_inventory *inventory, size_t index)
{
    animation_holder *holder = &inventory->animations[index];
    if (!inventory->owns_holders || inventory->staging || holder->references || !holder->source.animation) return;
    qa_model_animation_free(&holder->owned);
    qa_resource_release((qa_resource *)holder->source.resource);
    qa_resource_release((qa_resource *)holder->source.scale_resource);
    holder->source = (frontend_animation_source){0};
}
void frontend_models_destroy(frontend_model_inventory *inventory)
{
    if (!inventory || inventory->owner_retired) return;
    inventory->owner_retired = true;
    inventory->staging = false;
    if (inventory->models) for (size_t i = 0; i < inventory->model_count; ++i) model_retire(inventory, i);
    if (inventory->animations) for (size_t i = 0; i < inventory->animation_count; ++i) animation_retire(inventory, i);
    if (--inventory->references == 0) dispose(inventory);
}
bool frontend_model_retain(frontend_model_inventory *inventory, const qa_model *source,
    frontend_model_lease **out, qa_error *error)
{
    if (!inventory || !inventory->owns_holders || inventory->owner_retired || !source || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Model claim requires an actual decoded holder and empty token output");
    for (size_t i = 0; i < inventory->model_count; ++i) if (inventory->models[i].source.model == source) {
        if (inventory->references == SIZE_MAX || inventory->models[i].references == SIZE_MAX)
            return fail(error, QA_ERROR_MEMORY, "Immutable model claims overflow their actual counters");
        frontend_model_lease *lease = malloc(sizeof(*lease));
        if (!lease) return fail(error, QA_ERROR_MEMORY, "Retaining actual immutable model consumer");
        *lease = (frontend_model_lease){inventory, i};
        ++inventory->references; ++inventory->models[i].references;
        *out = lease; return true;
    }
    return fail(error, QA_ERROR_FORMAT, "Model claim does not identify a live decoded holder");
}
bool frontend_model_lease_clone(const frontend_model_lease *held,frontend_model_lease **out,qa_error *error)
{
    frontend_model_source source;
    frontend_model_inventory *inventory=held?held->inventory:NULL;
    if(!out || *out || !inventory || !inventory->owns_holders || !inventory->references ||
        !frontend_model_lease_source(held,&source))
        return fail(error,QA_ERROR_ARGUMENT,"Model clone requires its actual live owning token and empty output");
    model_holder *row=&inventory->models[held->index];
    if(inventory->references==SIZE_MAX || row->references==SIZE_MAX)
        return fail(error,QA_ERROR_MEMORY,"Cloned immutable model claims overflow their actual counters");
    frontend_model_lease *lease=malloc(sizeof(*lease));
    if(!lease) return fail(error,QA_ERROR_MEMORY,"Cloning actual immutable model consumer");
    *lease=*held; ++inventory->references; ++row->references; *out=lease; return true;
}
void frontend_model_release(frontend_model_lease *lease)
{
    if (!lease) return;
    frontend_model_inventory *inventory = lease->inventory;
    --inventory->models[lease->index].references;
    model_retire(inventory, lease->index);
    free(lease);
    if (--inventory->references == 0) dispose(inventory);
}
bool frontend_animation_retain(frontend_model_inventory *inventory, const qa_model_animation *source,
    frontend_animation_lease **out, qa_error *error)
{
    if (!inventory || !inventory->owns_holders || inventory->owner_retired || !source || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Animation claim requires an actual decoded holder and empty token output");
    for (size_t i = 0; i < inventory->animation_count; ++i) if (inventory->animations[i].source.animation == source) {
        if (inventory->references == SIZE_MAX || inventory->animations[i].references == SIZE_MAX)
            return fail(error, QA_ERROR_MEMORY, "Immutable animation claims overflow their actual counters");
        frontend_animation_lease *lease = malloc(sizeof(*lease));
        if (!lease) return fail(error, QA_ERROR_MEMORY, "Retaining actual immutable animation consumer");
        *lease = (frontend_animation_lease){inventory, i};
        ++inventory->references; ++inventory->animations[i].references;
        *out = lease; return true;
    }
    return fail(error, QA_ERROR_FORMAT, "Animation claim does not identify a live decoded holder");
}
bool frontend_animation_lease_clone(const frontend_animation_lease *held,frontend_animation_lease **out,qa_error *error)
{
    frontend_animation_source source;
    frontend_model_inventory *inventory=held?held->inventory:NULL;
    if(!out || *out || !inventory || !inventory->owns_holders || !inventory->references ||
        !frontend_animation_lease_source(held,&source))
        return fail(error,QA_ERROR_ARGUMENT,"Animation clone requires its actual live owning token and empty output");
    animation_holder *row=&inventory->animations[held->index];
    if(inventory->references==SIZE_MAX || row->references==SIZE_MAX)
        return fail(error,QA_ERROR_MEMORY,"Cloned immutable animation claims overflow their actual counters");
    frontend_animation_lease *lease=malloc(sizeof(*lease));
    if(!lease) return fail(error,QA_ERROR_MEMORY,"Cloning actual immutable animation consumer");
    *lease=*held; ++inventory->references; ++row->references; *out=lease; return true;
}
void frontend_animation_release(frontend_animation_lease *lease)
{
    if (!lease) return;
    frontend_model_inventory *inventory = lease->inventory;
    --inventory->animations[lease->index].references;
    animation_retire(inventory, lease->index);
    free(lease);
    if (--inventory->references == 0) dispose(inventory);
}
bool frontend_model_lease_source(const frontend_model_lease *lease, frontend_model_source *out)
{
    if (!lease || !out || !lease->inventory || lease->index >= lease->inventory->model_count) return false;
    const model_holder *holder = &lease->inventory->models[lease->index];
    if (!holder->references || !holder->source.model || !holder->source.resource || !holder->source.files) return false;
    *out = holder->source; return true;
}
bool frontend_animation_lease_source(const frontend_animation_lease *lease, frontend_animation_source *out)
{
    if (!lease || !out || !lease->inventory || lease->index >= lease->inventory->animation_count) return false;
    const animation_holder *holder = &lease->inventory->animations[lease->index];
    if (!holder->references || !holder->source.animation || !holder->source.resource || !holder->source.files) return false;
    *out = holder->source; return true;
}
bool frontend_models_install(frontend_model_inventory *inventory, qa_error *error)
{
    if (!inventory || !inventory->owns_holders || inventory->owner_retired || !inventory->staging || inventory->graph)
        return fail(error, QA_ERROR_ARGUMENT, "Model install requires the actual staged inventory after pure decode");
    size_t references = 1;
    for (size_t i = 0; i < inventory->model_count; ++i) {
        if (!inventory->models[i].source.model || inventory->models[i].references > SIZE_MAX - references)
            return fail(error, QA_ERROR_FORMAT, "Staged immutable model claims have inconsistent topology");
        references += inventory->models[i].references;
    }
    for (size_t i = 0; i < inventory->animation_count; ++i) {
        if (!inventory->animations[i].source.animation || inventory->animations[i].references > SIZE_MAX - references)
            return fail(error, QA_ERROR_FORMAT, "Staged immutable animation claims have inconsistent topology");
        references += inventory->animations[i].references;
    }
    if (references != inventory->references)
        return fail(error, QA_ERROR_FORMAT, "Immutable holder claims disagree with the real owning tokens");
    inventory->staging = false;
    for (size_t i = 0; i < inventory->model_count; ++i) model_retire(inventory, i);
    for (size_t i = 0; i < inventory->animation_count; ++i) animation_retire(inventory, i);
    return true;
}
bool frontend_models_capture(qa_application_content_graph *graph,
    const frontend_model_source *models, size_t model_count,
    const frontend_animation_source *animations, size_t animation_count,
    frontend_model_inventory **out, qa_error *error)
{
    if (!graph || !out || *out || (model_count && !models) || (animation_count && !animations) ||
        model_count > SIZE_MAX / sizeof(model_holder) || animation_count > SIZE_MAX / sizeof(animation_holder))
        return fail(error, QA_ERROR_ARGUMENT, "Model inventory requires actual graph, source rows and empty output");
    frontend_model_inventory *inventory = calloc(1, sizeof(*inventory));
    if (!inventory) return fail(error, QA_ERROR_MEMORY, "Allocating immutable model inventory");
    inventory->graph = graph; inventory->references = 1;
    inventory->models = model_count ? calloc(model_count, sizeof(*inventory->models)) : NULL;
    inventory->animations = animation_count ? calloc(animation_count, sizeof(*inventory->animations)) : NULL;
    bool ok = (!model_count || inventory->models) && (!animation_count || inventory->animations);
    if (!ok) fail(error, QA_ERROR_MEMORY, "Allocating parsed model source rows");
    for (size_t i = 0; ok && i < model_count; ++i) {
        const frontend_model_source *source = &models[i]; bool alias = false;
        if (!source->model || !same_bytes((qa_bytes){source->model->source.data, source->model->source.size}, qa_resource_bytes(source->resource))) {
            ok = fail(error, QA_ERROR_FORMAT, "Parsed model lacks its exact original resource version"); break;
        }
        for (size_t j = 0; j < inventory->model_count; ++j) if (inventory->models[j].source.model == source->model) {
            alias = true;
            if (inventory->models[j].source.resource != source->resource || inventory->models[j].source.files != source->files ||
                inventory->models[j].source.parent!=source->parent)
                ok = fail(error, QA_ERROR_FORMAT, "Aliased model holder has conflicting source provenance");
            break;
        }
        if (!ok || alias) continue;
        model_holder *holder = &inventory->models[inventory->model_count];
        ok = source_identity(graph, source->resource, source->files, &holder->pool, &holder->resource, &holder->view, error);
        if (ok && source->parent) {
            for (size_t j=0;j<inventory->model_count;++j)
                if (inventory->models[j].source.model==source->parent) { holder->parent=j+1; break; }
            if (!holder->parent || inventory->models[holder->parent-1].source.resource!=source->resource ||
                inventory->models[holder->parent-1].source.files!=source->files || !subset_aliases(source->model,source->parent))
                ok=fail(error,QA_ERROR_FORMAT,"Held subset lacks its actual prior parsed parent and shared allocations");
        }
        if (ok) { holder->source = *source; qa_resource_retain((qa_resource *)source->resource); ++inventory->model_count; }
    }
    for (size_t i = 0; ok && i < animation_count; ++i) {
        const frontend_animation_source *source = &animations[i]; bool alias = false;
        if (!source->animation || !same_bytes((qa_bytes){source->animation->source.data, source->animation->source.size}, qa_resource_bytes(source->resource))) {
            ok = fail(error, QA_ERROR_FORMAT, "Parsed animation lacks its exact original resource version"); break;
        }
        for (size_t j = 0; j < inventory->animation_count; ++j) if (inventory->animations[j].source.animation == source->animation) {
            alias = true;
            const frontend_animation_source *prior = &inventory->animations[j].source;
            if (prior->resource != source->resource || prior->files != source->files || prior->scale_resource != source->scale_resource)
                ok = fail(error, QA_ERROR_FORMAT, "Aliased animation holder has conflicting scale/source provenance");
            break;
        }
        if (!ok || alias) continue;
        animation_holder *holder = &inventory->animations[inventory->animation_count];
        ok = source_identity(graph, source->resource, source->files, &holder->pool, &holder->resource, &holder->view, error);
        if (ok && source->scale_resource && !qa_application_content_resource_id(graph, source->scale_resource,
            &holder->scale_pool, &holder->scale_resource)) ok = fail(error, QA_ERROR_FORMAT, "Animation scale policy is outside the actual content graph");
        if (ok && source->scale_resource && holder->scale_pool != holder->pool)
            ok = fail(error, QA_ERROR_FORMAT, "Animation scale policy does not belong to its source content view");
        if (ok) {
            holder->source = *source; qa_resource_retain((qa_resource *)source->resource);
            if (source->scale_resource) qa_resource_retain((qa_resource *)source->scale_resource);
            ++inventory->animation_count;
        }
    }
    if (!ok) { frontend_models_destroy(inventory); return false; }
    *out = inventory; return true;
}
static bool identity_fields(qa_source_save_io *io, uint64_t *pool, uint64_t *resource, uint64_t *view)
{ return qa_source_save_u64(io, pool) && *pool && qa_source_save_u64(io, resource) && *resource && qa_source_save_u64(io, view) && *view; }
static bool digest_fields(qa_source_save_io *io, const qa_resource *resource)
{
    const qa_sha256_digest *actual = qa_resource_digest(resource);
    if (!actual) return false;
    qa_sha256_digest saved = *actual;
    return qa_source_save_bytes(io, &saved, sizeof(saved)) && !memcmp(&saved, actual, sizeof(saved));
}
static bool inventory_fields(qa_source_save_io *io, frontend_model_inventory *inventory)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; uint8_t magic[4] = {'Q','F','M','I'}; if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFMI", 4) || !qa_source_save_count(io, &inventory->model_count, reading ? io->input.size / 64 : SIZE_MAX / sizeof(model_holder)) ||
        !qa_source_save_count(io, &inventory->animation_count, reading ? io->input.size / 64 : SIZE_MAX / sizeof(animation_holder)) ||
        inventory->model_count > SIZE_MAX / sizeof(model_holder) || inventory->animation_count > SIZE_MAX / sizeof(animation_holder)) return false;
    if (reading) {
        inventory->models = inventory->model_count ? calloc(inventory->model_count, sizeof(*inventory->models)) : NULL;
        inventory->animations = inventory->animation_count ? calloc(inventory->animation_count, sizeof(*inventory->animations)) : NULL;
        if ((inventory->model_count && !inventory->models) || (inventory->animation_count && !inventory->animations))
            return fail(io->error, QA_ERROR_MEMORY, "Restoring immutable model owner rows");
    }
    for (size_t i = 0; i < inventory->model_count; ++i) {
        model_holder *holder = &inventory->models[i];
        if (!identity_fields(io, &holder->pool, &holder->resource, &holder->view)) return false;
        if (reading) {
            const qa_resource *resource = qa_application_content_resource(inventory->graph, holder->pool, holder->resource);
            const qa_vfs *files = qa_application_content_view(inventory->graph, holder->view);
            if (!resource || !files || qa_vfs_resources(files) != qa_application_content_pool(inventory->graph, holder->pool)) return false;
            qa_resource_retain((qa_resource *)resource);
            holder->source = (frontend_model_source){.model=&holder->owned,.resource=resource,.files=files};
        }
        uint64_t parent_key=holder->parent;
        if (!digest_fields(io, holder->source.resource) || !qa_source_save_u64(io,&parent_key) || parent_key>i) return false;
        if (parent_key) {
            model_holder *parent=&inventory->models[parent_key-1];
            if (parent->pool!=holder->pool || parent->resource!=holder->resource || parent->view!=holder->view ||
                parent->dependents==SIZE_MAX) return false;
            if (reading) { holder->parent=parent_key; ++parent->dependents; }
            if (!subset_fields(io,holder,parent)) return false;
        } else {
            qa_model local = *holder->source.model;
            if (!model_fields(io, reading ? &holder->owned : &local, holder->source.resource)) return false;
        }
    }
    for (size_t i = 0; i < inventory->animation_count; ++i) {
        animation_holder *holder = &inventory->animations[i];
        if (!identity_fields(io, &holder->pool, &holder->resource, &holder->view) ||
            !qa_source_save_u64(io, &holder->scale_pool) || !qa_source_save_u64(io, &holder->scale_resource) ||
            ((!holder->scale_pool) != (!holder->scale_resource))) return false;
        if (reading) {
            const qa_resource *resource = qa_application_content_resource(inventory->graph, holder->pool, holder->resource);
            const qa_vfs *files = qa_application_content_view(inventory->graph, holder->view);
            const qa_resource *scale_resource = holder->scale_pool ? qa_application_content_resource(inventory->graph,
                holder->scale_pool, holder->scale_resource) : NULL;
            if (!resource || !files || (holder->scale_pool && !scale_resource) ||
                qa_vfs_resources(files) != qa_application_content_pool(inventory->graph, holder->pool) ||
                (scale_resource && holder->scale_pool != holder->pool)) return false;
            qa_resource_retain((qa_resource *)resource);
            if (scale_resource) qa_resource_retain((qa_resource *)scale_resource);
            holder->source = (frontend_animation_source){&holder->owned, resource, files, scale_resource};
        }
        qa_model_animation local = *holder->source.animation;
        if (!digest_fields(io, holder->source.resource) ||
            (holder->source.scale_resource && !digest_fields(io, holder->source.scale_resource)) ||
            !animation_fields(io, reading ? &holder->owned : &local, holder->source.resource)) return false;
    }
    return true;
}
bool frontend_models_checkpoint(const frontend_model_inventory *inventory, qa_buffer *out, qa_error *error)
{
    if (!inventory || inventory->owns_holders || inventory->owner_retired || !inventory->graph || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "Model checkpoint requires actual inventory and empty output");
    frontend_model_inventory local = *inventory; qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && inventory_fields(&io, &local) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Parsed model graph is not completely qualified");
    return ok;
}
bool frontend_models_restore(qa_application_content_graph *graph, qa_bytes bytes,
    frontend_model_inventory **out, qa_error *error)
{
    if (!graph || !out || *out || !bytes.data)
        return fail(error, QA_ERROR_ARGUMENT, "Model restore requires actual graph, saved inventory and empty output");
    frontend_model_inventory *inventory = calloc(1, sizeof(*inventory));
    if (!inventory) return fail(error, QA_ERROR_MEMORY, "Allocating restored model inventory");
    inventory->graph = graph; inventory->owns_holders = true; inventory->staging = true;
    inventory->references = 1; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && inventory_fields(&io, inventory) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) {
        frontend_models_destroy(inventory);
        if (error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Saved parsed model graph differs from qualified immutable content");
        return false;
    }
    inventory->graph = NULL;
    *out = inventory; return true;
}
size_t frontend_model_count(const frontend_model_inventory *inventory)
{ return inventory ? inventory->model_count : 0; }
size_t frontend_animation_count(const frontend_model_inventory *inventory)
{ return inventory ? inventory->animation_count : 0; }
bool frontend_model_source_at(const frontend_model_inventory *inventory, size_t index, frontend_model_source *out)
{ if (!inventory || inventory->owner_retired || !out || index >= inventory->model_count || !inventory->models[index].source.model) return false; *out = inventory->models[index].source; return true; }
bool frontend_animation_source_at(const frontend_model_inventory *inventory, size_t index, frontend_animation_source *out)
{ if (!inventory || inventory->owner_retired || !out || index >= inventory->animation_count || !inventory->animations[index].source.animation) return false; *out = inventory->animations[index].source; return true; }
bool frontend_model_encode(void *context, const qa_model *model, uint64_t *out, qa_error *error)
{
    const frontend_model_inventory *inventory = context;
    if (inventory && !inventory->owner_retired && model && out) for (size_t i = 0; i < inventory->model_count; ++i)
        if (inventory->models[i].source.model == model) { *out = i; return true; }
    return fail(error, QA_ERROR_FORMAT, "Scene model is absent from the actual parsed holder inventory");
}
bool frontend_model_decode(void *context, uint64_t id, qa_bytes bytes, const qa_model **out, qa_error *error)
{
    const frontend_model_inventory *inventory = context;
    if (inventory && !inventory->owner_retired && out && id < inventory->model_count) {
        const qa_model *model = inventory->models[id].source.model;
        if (model && same_bytes(bytes, (qa_bytes){model->source.data, model->source.size})) { *out = model; return true; }
    }
    return fail(error, QA_ERROR_FORMAT, "Scene model reference differs from the restored source-qualified holder");
}
bool frontend_animation_encode(void *context, const qa_model_animation *animation, uint64_t *out, qa_error *error)
{
    const frontend_model_inventory *inventory = context;
    if (inventory && !inventory->owner_retired && animation && out) for (size_t i = 0; i < inventory->animation_count; ++i)
        if (inventory->animations[i].source.animation == animation) { *out = i; return true; }
    return fail(error, QA_ERROR_FORMAT, "Replacement animation is absent from the actual installed holder inventory");
}
bool frontend_animation_decode(void *context, uint64_t id, qa_bytes bytes,
    const qa_model_animation **out, qa_error *error)
{
    const frontend_model_inventory *inventory = context;
    if (inventory && !inventory->owner_retired && out && id < inventory->animation_count) {
        const qa_model_animation *animation = inventory->animations[id].source.animation;
        if (animation && same_bytes(bytes, (qa_bytes){animation->source.data, animation->source.size})) { *out = animation; return true; }
    }
    return fail(error, QA_ERROR_FORMAT, "Replacement animation reference differs from the installed immutable holder");
}
bool frontend_model_source_qualify(void *context, const qa_model *model, qa_scene_resources *images,
    qa_material_library *materials, const qa_scene_image_options *options, qa_error *error)
{
    const frontend_model_inventory *inventory = context;
    if (!inventory || inventory->owner_retired || !model || !images || !options || options->family > QA_SCENE_Q3 ||
        (materials && qa_material_library_resource_owner(materials) != images))
        return fail(error, QA_ERROR_ARGUMENT, "Scene model qualification requires actual content and resource owners");
    for (size_t i = 0; i < inventory->model_count; ++i) if (inventory->models[i].source.model == model) {
        if (qa_scene_resources_files(images) != inventory->models[i].source.files)
            return fail(error, QA_ERROR_FORMAT, "Model image owner does not use its original source content view");
        return true;
    }
    return fail(error, QA_ERROR_FORMAT, "Scene model source is outside the genuine parsed holder inventory");
}
