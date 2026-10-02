#include "qa/scene_frame_save.h"
#include "qa/source_save.h"
#include "qa/material.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FIELD(type, object, name) do { if (!qa_source_save_##type(io, &(object)->name)) return false; } while (0)
#define ENUM(type, object, name, maximum) do { uint32_t enum_value_=(object)->name; \
    if (!qa_source_save_u32(io,&enum_value_) || enum_value_>(maximum)) return false; \
    if (io->direction==QA_SOURCE_SAVE_READ) (object)->name=(type)enum_value_; } while (0)
static bool failure(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }
static bool vec4(qa_source_save_io *io, qa_scene_vec4 *value)
{ FIELD(f32,value,x); FIELD(f32,value,y); FIELD(f32,value,z); FIELD(f32,value,w); return true; }
static bool matrix(qa_source_save_io *io, qa_scene_matrix *value)
{ for (size_t i=0;i<16;++i) if (!qa_source_save_f32(io,&value->m[i])) return false; return true; }
static bool plane(qa_source_save_io *io, qa_scene_plane *value)
{ FIELD(vec3,value,normal); FIELD(f32,value,distance); return true; }
static bool view(qa_source_save_io *io, qa_scene_view *value)
{
    FIELD(i32,&value->viewport,x); FIELD(i32,&value->viewport,y); FIELD(u32,&value->viewport,width); FIELD(u32,&value->viewport,height);
    FIELD(vec3,value,origin);
    for (size_t i=0;i<3;++i) if (!qa_source_save_vec3(io,&value->axis[i])) return false;
    if (!matrix(io,&value->projection)) return false;
    FIELD(bool,value,clear_color); FIELD(bool,value,clear_depth); FIELD(bool,value,clear_stencil);
    FIELD(bool,value,clip_enabled); FIELD(bool,value,mirror);
    if (!vec4(io,&value->color) || !plane(io,&value->clip_plane)) return false;
    FIELD(f32,value,depth); FIELD(u32,value,seat); return true;
}
static bool fog(qa_source_save_io *io, qa_scene_fog *value)
{
    ENUM(qa_scene_fog_kind,value,kind,QA_FOG_Q2); ENUM(qa_scene_fog_effect,value,effect,QA_FOG_NO_EFFECT);
    FIELD(vec3,value,color); FIELD(vec3,value,height_color); FIELD(vec3,value,height_end_color);
    FIELD(f32,value,density); FIELD(f32,value,amount); FIELD(f32,value,sky_factor); FIELD(f32,value,height_density);
    FIELD(f32,value,height_start); FIELD(f32,value,height_end); FIELD(f32,value,height_falloff); FIELD(f32,value,far_depth);
    FIELD(bool,value,sky_drawn); return true;
}
static bool state(qa_source_save_io *io, qa_scene_state *value)
{
    ENUM(qa_scene_blend,value,blend_source,QA_BLEND_SRC_ALPHA_SATURATE); ENUM(qa_scene_blend,value,blend_destination,QA_BLEND_SRC_ALPHA_SATURATE);
    ENUM(qa_scene_depth,value,depth_test,QA_DEPTH_DISABLED); ENUM(qa_scene_alpha,value,alpha_test,QA_ALPHA_GE128);
    ENUM(qa_scene_cull,value,cull,QA_CULL_BACK);
    FIELD(bool,value,depth_write); FIELD(bool,value,color_write); FIELD(bool,value,polygon_offset); FIELD(bool,value,wireframe);
    FIELD(f32,value,depth_near); FIELD(f32,value,depth_far); FIELD(f32,value,offset_factor); FIELD(f32,value,offset_units); FIELD(f32,value,line_width);
    FIELD(bool,value,stencil_enabled); ENUM(qa_scene_stencil_test,value,stencil_test,QA_STENCIL_NOTEQUAL);
    FIELD(u32,value,stencil_reference); FIELD(u32,value,stencil_compare_mask); FIELD(u32,value,stencil_write_mask);
    ENUM(qa_scene_stencil_op,value,stencil_fail,QA_STENCIL_INVERT);
    ENUM(qa_scene_stencil_op,value,stencil_depth_fail,QA_STENCIL_INVERT); ENUM(qa_scene_stencil_op,value,stencil_depth_pass,QA_STENCIL_INVERT); return true;
}
static bool identity(qa_source_save_io *io, const qa_scene_frame_checkpoint_refs *refs, bool light, uint64_t *value)
{
    bool present=*value!=0; uint64_t key=0;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) { if (io->direction==QA_SOURCE_SAVE_READ) *value=0; return true; }
    if (io->direction==QA_SOURCE_SAVE_WRITE && !(light?refs->light_identity_encode:refs->mesh_identity_encode)(refs->context,*value,&key,io->error)) return false;
    if (!qa_source_save_u64(io,&key)) return false;
    return io->direction!=QA_SOURCE_SAVE_READ || ((light?refs->light_identity_decode:refs->mesh_identity_decode)(refs->context,key,value,io->error) && *value);
}
static bool shadow_light(qa_source_save_io *io, const qa_scene_frame_checkpoint_refs *refs, qa_scene_shadow_light *value)
{
    qa_scene_light *light=&value->light;
    FIELD(vec3,light,origin); FIELD(vec3,light,color); FIELD(vec3,light,direction);
    FIELD(f32,light,radius); FIELD(f32,light,minimum); FIELD(f32,light,scale); FIELD(f32,light,cos_half_angle);
    FIELD(bool,light,additive); FIELD(bool,light,spot); FIELD(bool,light,casts_shadow);
    if (!identity(io,refs,true,&light->identity)) return false;
    FIELD(u64,light,revision); FIELD(u32,light,shadow_resolution); ENUM(qa_scene_family,light,family,QA_SCENE_Q3);
    if (!vec4(io,&value->atlas_rect) || !matrix(io,&value->shadow_matrix)) return false;
    FIELD(bool,value,point_shadow); FIELD(bool,value,shadow_valid); FIELD(vec3,value,model_fraction); return true;
}
static bool vertex(qa_source_save_io *io, qa_scene_vertex *value)
{
    FIELD(vec3,value,position); FIELD(vec3,value,normal);
    FIELD(f32,&value->texcoord,x); FIELD(f32,&value->texcoord,y); FIELD(f32,&value->lightmap,x); FIELD(f32,&value->lightmap,y);
    return vec4(io,&value->color);
}
static bool allocate(qa_source_save_io *io, void **out, size_t count, size_t stride)
{
    if (count>SIZE_MAX/stride) return false;
    *out=count?calloc(count,stride):NULL;
    return !count || *out || failure(io->error,QA_ERROR_MEMORY,"Allocating saved scene frame");
}
static bool owners(qa_source_save_io *io, qa_scene_frame *frame, const qa_scene_frame_checkpoint_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; size_t images=frame->image_count, geometries=frame->geometry_count;
    size_t maximum=reading?io->input.size/8:SIZE_MAX;
    if (!qa_source_save_count(io,&images,maximum) || !qa_source_save_count(io,&geometries,maximum)) return false;
    if (reading) {
        if (!allocate(io,(void **)&frame->images,images,sizeof(*frame->images))) return false;
        frame->image_count=frame->image_capacity=images;
        if (!allocate(io,(void **)&frame->geometries,geometries,sizeof(*frame->geometries))) return false;
        frame->geometry_count=frame->geometry_capacity=geometries;
    }
    for (size_t i=0;i<images;++i) {
        uint64_t key=0;
        if (!reading && (!frame->images[i] || !refs->image_encode(refs->context,frame->images[i],&key,io->error))) return false;
        if (!qa_source_save_u64(io,&key)) return false;
        if (reading) {
            const qa_scene_image *image=NULL;
            if (!refs->image_decode(refs->context,key,&image,io->error) || !image) return false;
            qa_scene_image_retain(image); frame->images[i]=image;
        }
        for (size_t j=0;j<i;++j) if (frame->images[j]==frame->images[i]) return false;
    }
    for (size_t i=0;i<geometries;++i) {
        uint64_t key=0;
        if (!reading && (!qa_scene_geometry_active(frame->geometries[i]) || !refs->geometry_encode(refs->context,frame->geometries[i],&key,io->error))) return false;
        if (!qa_source_save_u64(io,&key)) return false;
        if (reading) {
            const qa_scene_geometry *geometry=NULL;
            if (!refs->geometry_decode(refs->context,key,&geometry,io->error) || !qa_scene_geometry_active(geometry)) return false;
            qa_scene_geometry_retain(geometry); frame->geometries[i]=geometry;
        }
        if (i && frame->geometries[i-1]==frame->geometries[i]) return false;
    }
    return true;
}
static bool image(qa_source_save_io *io, const qa_scene_frame *frame, const qa_scene_image **value)
{
    uint64_t index=UINT64_MAX;
    if (io->direction==QA_SOURCE_SAVE_WRITE && *value) {
        for (size_t i=0;i<frame->image_count;++i) if (frame->images[i]==*value) { index=i; break; }
        if (index==UINT64_MAX) return false;
    }
    if (!qa_source_save_u64(io,&index) || (index!=UINT64_MAX && index>=frame->image_count)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) *value=index==UINT64_MAX?NULL:frame->images[index];
    return true;
}
static bool allocation_slice(const void *base, size_t allocation, const void *pointer, size_t count, size_t stride, size_t *offset)
{
    if (!base || !pointer || allocation>SIZE_MAX/stride) return false;
    uintptr_t first=(uintptr_t)base, position=(uintptr_t)pointer;
    if (position<first || position-first>allocation*stride || (position-first)%stride) return false;
    *offset=(size_t)((position-first)/stride); return count<=allocation-*offset;
}
static bool mesh(qa_source_save_io *io, qa_scene_frame *frame, const qa_scene_frame_checkpoint_refs *refs,
    size_t source_storage, qa_scene_mesh *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; uint64_t geometry=UINT64_MAX;
    if (!reading && value->geometry) {
        for (size_t i=0;i<frame->geometry_count;++i) if (frame->geometries[i]==value->geometry) { geometry=i; break; }
        if (geometry==UINT64_MAX) return false;
    }
    if (!qa_source_save_u64(io,&geometry) || (geometry!=UINT64_MAX && geometry>=frame->geometry_count)) return false;
    if (reading) value->geometry=geometry==UINT64_MAX?NULL:frame->geometries[geometry];
    qa_scene_geometry_view retained={0};
    if (value->geometry && !qa_scene_geometry_read(value->geometry,&retained)) return false;
    if (!identity(io,refs,false,&value->identity) || (value->identity && !value->geometry)) return false;
    FIELD(u64,value,revision); ENUM(qa_scene_primitive,value,primitive,QA_SCENE_LINES);
    FIELD(vec3,&value->bounds,mins); FIELD(vec3,&value->bounds,maxs);
    if (!qa_source_save_count(io,&value->vertex_count,SIZE_MAX/sizeof(*value->vertices)) ||
        !qa_source_save_count(io,&value->index_count,SIZE_MAX/sizeof(*value->indices))) return false;
    if (source_storage && value->vertex_count>source_storage) return false;
    size_t vertex_storage=source_storage?source_storage:value->vertex_count;
    bool retained_vertices=false, retained_indices=false; size_t vertex_offset=0, index_offset=0;
    if (!reading) {
        retained_vertices=allocation_slice(retained.vertices,retained.vertex_count,value->vertices,vertex_storage,sizeof(*value->vertices),&vertex_offset);
        retained_indices=allocation_slice(retained.indices,retained.index_count,value->indices,value->index_count,sizeof(*value->indices),&index_offset);
    }
    if (!qa_source_save_bool(io,&retained_vertices) || !qa_source_save_bool(io,&retained_indices)) return false;
    if (retained_vertices) {
        if (!value->geometry || !retained.vertices || !qa_source_save_count(io,&vertex_offset,retained.vertex_count) || vertex_storage>retained.vertex_count-vertex_offset) return false;
        if (reading) value->vertices=retained.vertices+vertex_offset;
    } else {
        if (reading && vertex_storage) {
            if (vertex_storage>(io->input.size-io->offset)/56) return false;
            value->vertices=qa_arena_alloc(&frame->storage,vertex_storage*sizeof(*value->vertices),_Alignof(qa_scene_vertex),io->error);
            if (!value->vertices) return false;
        }
        if (vertex_storage && !value->vertices) return false;
        for (size_t i=0;i<vertex_storage;++i) {
            qa_scene_vertex item=reading?(qa_scene_vertex){0}:value->vertices[i];
            if (!vertex(io,&item)) return false;
            if (reading) ((qa_scene_vertex *)value->vertices)[i]=item;
        }
    }
    if (retained_indices) {
        if (!value->geometry || !retained.indices || !qa_source_save_count(io,&index_offset,retained.index_count) || value->index_count>retained.index_count-index_offset) return false;
        if (reading) value->indices=retained.indices+index_offset;
    } else {
        if (reading && value->index_count) {
            if (value->index_count>(io->input.size-io->offset)/4) return false;
            value->indices=qa_arena_alloc(&frame->storage,value->index_count*sizeof(*value->indices),_Alignof(uint32_t),io->error);
            if (!value->indices) return false;
        }
        if (value->index_count && !value->indices) return false;
        for (size_t i=0;i<value->index_count;++i) {
            uint32_t item=reading?0:value->indices[i];
            if (!qa_source_save_u32(io,&item)) return false;
            if (reading) ((uint32_t *)value->indices)[i]=item;
        }
    }
    for (size_t i=0;i<value->index_count;++i) if (value->indices[i]>=vertex_storage) return false;
    return true;
}
static bool draw(qa_source_save_io *io, qa_scene_frame *frame, const qa_scene_frame_checkpoint_refs *refs,
    uint32_t schema, qa_scene_draw *value)
{
    if (schema>=10) {
        FIELD(u32,value,source_vertex_storage);
        if (value->source_vertex_storage && value->source_vertex_storage!=1000) return false;
    }
    if (!mesh(io,frame,refs,value->source_vertex_storage,&value->mesh) || !matrix(io,&value->model) || !matrix(io,&value->mvp)) return false;
    FIELD(u8,value,texture_count); if (value->texture_count>2) return false;
    for (size_t i=0;i<2;++i) {
        if (!qa_source_save_bool(io,&value->retain_texture[i]) || (i<value->texture_count && !image(io,frame,&value->textures[i]))) return false;
    }
    ENUM(qa_scene_texture_environment,value,environment,QA_TEXTURE_REPLACE);
    if (!state(io,&value->state) || !fog(io,&value->fog)) return false;
    ENUM(qa_scene_lighting_kind,value,lighting,QA_LIGHT_Q2_MODEL_SHADOW); ENUM(qa_scene_light_pass,value,light_pass,QA_LIGHT_PASS_MODEL);
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!qa_source_save_count(io,&value->light_count,reading?io->input.size/64:SIZE_MAX/sizeof(*value->lights))) return false;
    if (reading && value->light_count) {
        if (value->light_count>SIZE_MAX/sizeof(*value->lights)) return false;
        value->lights=qa_arena_alloc(&frame->storage,value->light_count*sizeof(*value->lights),_Alignof(qa_scene_shadow_light),io->error);
        if (!value->lights) return false;
    }
    if (value->light_count && !value->lights) return false;
    for (size_t i=0;i<value->light_count;++i) {
        qa_scene_shadow_light light=reading?(qa_scene_shadow_light){0}:value->lights[i];
        if (!shadow_light(io,refs,&light)) return false;
        if (reading) ((qa_scene_shadow_light *)value->lights)[i]=light;
    }
    if (!image(io,frame,&value->shadow_atlas)) return false;
    FIELD(f32,value,shadow_near); FIELD(f32,value,shade_scale); FIELD(bool,value,model_shade_scale); FIELD(bool,value,luminance_alpha);
    if (schema>=3) { FIELD(bool,value,source_primitives); }
    if (schema>=5) {
        ENUM(qa_scene_source_direct,value,source_direct,QA_SOURCE_DIRECT_RAW);
        FIELD(bool,value,source_retain_depth_range);
    }
    if (schema>=6) {
        FIELD(bool,value,source_retain_polygon_offset);
        FIELD(bool,value,source_stage_state);
    }
    if (schema>=8) { FIELD(bool,value,source_arrays); }
    if (value->source_vertex_storage && !value->source_arrays) return false;
    FIELD(u64,value,sort_key); FIELD(u32,value,entity); FIELD(u32,value,fog_index); FIELD(u32,value,light_mask); return true;
}
static bool command(qa_source_save_io *io, qa_scene_frame *frame, const qa_scene_frame_checkpoint_refs *refs,
    uint32_t schema,qa_scene_command *value)
{
    ENUM(qa_scene_command_kind,value,kind,QA_SCENE_COMMAND_PREBLEND_GAMMA);
    if (schema<4 && value->kind==QA_SCENE_COMMAND_OUTPUT_DOMAIN) return false;
    if (schema<9 && value->kind==QA_SCENE_COMMAND_PREBLEND_GAMMA) return false;
    switch (value->kind) {
    case QA_SCENE_COMMAND_VIEW: return view(io,&value->data.view);
    case QA_SCENE_COMMAND_DRAW: return draw(io,frame,refs,schema,&value->data.draw);
    case QA_SCENE_COMMAND_TARGET:
        return image(io,frame,&value->data.target.image) && (!value->data.target.image || value->data.target.image->kind==QA_SCENE_DEPTH32F);
    case QA_SCENE_COMMAND_OPACITY_BEGIN: return qa_source_save_f32(io,&value->data.opacity.value);
    case QA_SCENE_COMMAND_OPACITY_END: case QA_SCENE_COMMAND_SWAP: return true;
    case QA_SCENE_COMMAND_FOG: return fog(io,&value->data.fog.fog) && view(io,&value->data.fog.view);
    case QA_SCENE_COMMAND_DRAW_BUFFER:
        ENUM(qa_scene_draw_buffer,&value->data.draw_buffer,buffer,QA_DRAW_BACK_RIGHT);
        return qa_source_save_bool(io,&value->data.draw_buffer.clear);
    case QA_SCENE_COMMAND_IMAGE: return image(io,frame,&value->data.image) && value->data.image;
    case QA_SCENE_COMMAND_PREBLEND_GAMMA: return qa_source_save_bool(io,&value->data.preblend_gamma.enabled);
    case QA_SCENE_COMMAND_OUTPUT_DOMAIN:
        FIELD(i32,&value->data.output_domain.rect,x); FIELD(i32,&value->data.output_domain.rect,y);
        FIELD(u32,&value->data.output_domain.rect,width); FIELD(u32,&value->data.output_domain.rect,height);
        return qa_source_save_bool(io,&value->data.output_domain.source) &&
            value->data.output_domain.rect.x>=0 && value->data.output_domain.rect.y>=0 &&
            value->data.output_domain.rect.width && value->data.output_domain.rect.height;
    }
    return false;
}
static bool groups(qa_source_save_io *io, qa_scene_frame *frame, const qa_scene_frame_checkpoint_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; size_t count=frame->group_count;
    if (!qa_source_save_count(io,&count,reading?io->input.size/49:SIZE_MAX)) return false;
    if (reading) {
        if (!allocate(io,(void **)&frame->groups,count,sizeof(*frame->groups))) return false;
        frame->group_count=frame->group_capacity=count;
    }
    for (size_t i=0;i<count;++i) {
        qa_scene_group *value=&frame->groups[i]; bool present=value->material!=NULL; uint64_t key=0;
        if (!qa_source_save_count(io,&value->first,frame->command_count) ||
            !qa_source_save_count(io,&value->count,frame->command_count-value->first) ||
            !qa_source_save_count(io,&value->ordinal,count) || value->ordinal!=i ||
            (i && value->first<frame->groups[i-1].first+frame->groups[i-1].count)) return false;
        ENUM(qa_scene_group_kind,value,kind,QA_SCENE_GROUP_SEQUENCE);
        if (!qa_source_save_bool(io,&present)) return false;
        if (present) {
            if (!reading && !refs->material_encode(refs->context,value->material,&key,io->error)) return false;
            if (!qa_source_save_u64(io,&key)) return false;
            if (reading && (!refs->material_decode(refs->context,key,&value->material,io->error) || !value->material)) return false;
            if (value->kind==QA_SCENE_GROUP_SOURCE &&
                !qa_material_order_has_record(frame->material_order,value->material)) return false;
        } else if (reading) value->material=NULL;
        FIELD(f32,value,priority); FIELD(u32,value,entity); FIELD(u32,value,fog); FIELD(u32,value,dlight); FIELD(u32,value,source_sort);
        if (!isfinite(value->priority) || (value->kind==QA_SCENE_GROUP_SOURCE &&
            (!value->material || value->entity>1022 || value->fog>31 || value->dlight>3))) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io, qa_scene_frame *frame, uint64_t qualified_owner, const qa_scene_frame_checkpoint_refs *refs)
{
    uint8_t magic[4]={'Q','F','R','M'}; uint32_t schema=11;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFRM",4) || !qa_source_save_u32(io,&schema) ||
        (schema<2 || schema>11)) return false;
    FIELD(u64,frame,owner); FIELD(u64,frame,sequence);
    if (schema>=3) {
        FIELD(bool,frame,source_backend); FIELD(bool,frame,source_skip_backend);
        FIELD(bool,frame,source_clear_draw_buffer);
        if (!frame->source_backend && (frame->source_skip_backend || frame->source_clear_draw_buffer)) return false;
    } else {
        frame->source_backend=false; frame->source_skip_backend=false; frame->source_clear_draw_buffer=false;
    }
    if (schema>=7) {
        FIELD(bool,frame,source_begin_frame); FIELD(i32,frame,source_stereo_frame);
        if (frame->source_stereo_frame<0 || frame->source_stereo_frame>2 ||
            (frame->source_begin_frame ? !frame->source_backend : frame->source_stereo_frame!=0)) return false;
    }
    if (schema>=11) {
        FIELD(bool,frame,source_front_buffer);
        if (frame->source_front_buffer && !frame->source_backend) return false;
    } else frame->source_front_buffer=false;
    if (frame->owner!=qualified_owner || !owners(io,frame,refs)) return false;
    bool reading=io->direction==QA_SOURCE_SAVE_READ; size_t count=frame->command_count;
    if (!qa_source_save_count(io,&count,reading?io->input.size/4:SIZE_MAX)) return false;
    if (reading) {
        if (!allocate(io,(void **)&frame->commands,count,sizeof(*frame->commands))) return false;
        frame->command_count=frame->command_capacity=count;
    }
    for (size_t i=0;i<count;++i) {
        qa_scene_command value=reading?(qa_scene_command){0}:frame->commands[i];
        if (!command(io,frame,refs,schema,&value)) return false;
        if (reading) frame->commands[i]=value;
    }
    return groups(io,frame,refs);
}
static bool refs_ready(const qa_scene_frame_checkpoint_refs *refs)
{
    return refs && refs->image_encode && refs->image_decode && refs->geometry_encode && refs->geometry_decode &&
        refs->material_encode && refs->material_decode && refs->mesh_identity_encode && refs->mesh_identity_decode &&
        refs->light_identity_encode && refs->light_identity_decode;
}
bool qa_scene_frame_checkpoint(const qa_scene_frame *frame, const qa_scene_frame_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!frame || frame->source_pending || !out || !refs_ready(refs)) return failure(error,QA_ERROR_ARGUMENT,"Scene frame capture requires completed work and actual owner resolvers");
    qa_source_save_io io; qa_scene_frame saved=*frame;
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    bool ok=fields(&io,&saved,frame->owner,refs) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) failure(error,QA_ERROR_FORMAT,"Invalid retained scene frame state");
    qa_source_save_dispose(&io); return ok;
}
bool qa_scene_frame_restore(qa_scene_frame *frame, qa_bytes bytes, const qa_scene_frame_checkpoint_refs *refs, qa_error *error)
{
    if (!frame || frame->source_pending || !refs_ready(refs)) return failure(error,QA_ERROR_ARGUMENT,"Scene frame restore requires completed work and actual candidate owner resolvers");
    qa_scene_frame saved; qa_scene_frame_init(&saved,frame->owner); saved.material_order=frame->material_order;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) { qa_scene_frame_destroy(&saved); return false; }
    bool ok=fields(&io,&saved,frame->owner,refs) && qa_source_save_finish(&io,NULL);
    if (ok) {
        qa_scene_frame_destroy(frame); *frame=saved; memset(&saved,0,sizeof(saved));
    } else if (error && error->code==QA_OK) failure(error,QA_ERROR_FORMAT,"Invalid saved scene frame storage or owner references");
    qa_scene_frame_destroy(&saved); qa_source_save_dispose(&io); return ok;
}
#undef ENUM
#undef FIELD
