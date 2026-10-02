#include "qa/q3_scene_packet_save.h"
#include "scene_fields_save.h"
#include <stdlib.h>
#include <string.h>

struct qa_q3_scene_packet_owner {
    qa_q3_scene_packet_view view;
    qa_arena storage;
    uint64_t images[8];
    const char *texts[8];
};
#define FIELD(type,obj,name) do { if(!qa_source_save_##type(io,&(obj)->name)) return false; } while(0)
#define ENUM(type,obj,name,max) do { uint32_t saved_=(obj)->name; \
    if(!qa_source_save_u32(io,&saved_) || saved_>(max)) return false; \
    if(io->direction==QA_SOURCE_SAVE_READ) (obj)->name=(type)saved_; } while(0)
static bool fail(qa_error *e,qa_status status,const char *message)
{ qa_error_set(e,status,0,"%s",message); return false; }
static bool state(qa_source_save_io *io,qa_scene_state *v)
{
    ENUM(qa_scene_blend,v,blend_source,QA_BLEND_SRC_ALPHA_SATURATE);
    ENUM(qa_scene_blend,v,blend_destination,QA_BLEND_SRC_ALPHA_SATURATE);
    ENUM(qa_scene_depth,v,depth_test,QA_DEPTH_DISABLED);
    ENUM(qa_scene_alpha,v,alpha_test,QA_ALPHA_GE128);
    ENUM(qa_scene_cull,v,cull,QA_CULL_BACK);
    ENUM(qa_scene_stencil_test,v,stencil_test,QA_STENCIL_NOTEQUAL);
    ENUM(qa_scene_stencil_op,v,stencil_fail,QA_STENCIL_INVERT);
    ENUM(qa_scene_stencil_op,v,stencil_depth_fail,QA_STENCIL_INVERT);
    ENUM(qa_scene_stencil_op,v,stencil_depth_pass,QA_STENCIL_INVERT);
    FIELD(bool,v,depth_write);
    FIELD(bool,v,color_write);
    FIELD(bool,v,polygon_offset);
    FIELD(bool,v,wireframe);
    FIELD(bool,v,stencil_enabled);
    FIELD(f32,v,depth_near);
    FIELD(f32,v,depth_far);
    FIELD(f32,v,offset_factor);
    FIELD(f32,v,offset_units);
    FIELD(f32,v,line_width);
    FIELD(u32,v,stencil_reference);
    FIELD(u32,v,stencil_compare_mask);
    FIELD(u32,v,stencil_write_mask);
    return true;
}
static bool definition(qa_source_save_io *io,qa_q3_refdef *v)
{
    FIELD(i32,v,x);
    FIELD(i32,v,y);
    FIELD(i32,v,width);
    FIELD(i32,v,height);
    FIELD(i32,v,time);
    FIELD(i32,v,flags);
    FIELD(f32,v,fov_x);
    FIELD(f32,v,fov_y);
    FIELD(vec3,v,origin);
    for(unsigned i=0;i<3;++i) if(!qa_source_save_vec3(io,v->axis+i)) return false;
    return qa_source_save_bytes(io,v->area_mask,sizeof(v->area_mask)) &&
        qa_source_save_bytes(io,v->text,sizeof(v->text));
}
static bool light(qa_source_save_io *io,const qa_scene_frame_checkpoint_refs *refs,qa_scene_light *v)
{
    FIELD(vec3,v,origin);
    FIELD(vec3,v,color);
    FIELD(vec3,v,direction);
    FIELD(f32,v,radius);
    FIELD(f32,v,minimum);
    FIELD(f32,v,scale);
    FIELD(f32,v,cos_half_angle);
    FIELD(bool,v,additive);
    FIELD(bool,v,spot);
    FIELD(bool,v,casts_shadow);
    uint64_t key=v->identity;
    if(io->direction==QA_SOURCE_SAVE_WRITE && key &&
        (!refs->light_identity_encode || !refs->light_identity_encode(refs->context,v->identity,&key,io->error) || !key)) return false;
    if(!qa_source_save_u64(io,&key)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) v->identity=key;
    FIELD(u64,v,revision); FIELD(u32,v,shadow_resolution); ENUM(qa_scene_family,v,family,QA_SCENE_Q3);
    return true;
}
static bool allocate(qa_source_save_io *io,qa_q3_scene_packet_owner *owner,size_t *count,
    size_t width,size_t minimum,const void **data)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!qa_source_save_count(io,count,SIZE_MAX/width)) return false;
    if(!reading) return !*count || *data!=NULL;
    if(io->offset>io->input.size || *count>(io->input.size-io->offset)/minimum) return false;
    void *memory=*count?qa_arena_alloc(&owner->storage,*count*width,_Alignof(max_align_t),io->error):NULL;
    if(*count && !memory) return false;
    *data=memory; return true;
}
static bool image(qa_source_save_io *io,const qa_scene_frame_checkpoint_refs *refs,
    const qa_scene_image *value,uint64_t *key)
{
    uint64_t id=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE && value &&
        (!refs->image_encode || !refs->image_encode(refs->context,value,&id,io->error) || !id)) return false;
    if(!qa_source_save_u64(io,&id)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) *key=id;
    return true;
}
static bool lights(qa_source_save_io *io,qa_q3_scene_packet_owner *owner,
    const qa_scene_frame_checkpoint_refs *refs,size_t *count,const qa_scene_light **values)
{
    if(!allocate(io,owner,count,sizeof(**values),64,(const void **)values)) return false;
    for(size_t i=0;i<*count;++i) {
        qa_scene_light value=io->direction==QA_SOURCE_SAVE_WRITE?(*values)[i]:(qa_scene_light){0};
        if(!light(io,refs,&value)) return false;
        if(io->direction==QA_SOURCE_SAVE_READ) ((qa_scene_light *)*values)[i]=value;
    }
    return true;
}
static bool vertices(qa_source_save_io *io,qa_q3_scene_packet_owner *owner,
    size_t *count,const qa_scene_vertex **values)
{
    if(!allocate(io,owner,count,sizeof(**values),56,(const void **)values)) return false;
    for(size_t i=0;i<*count;++i) {
        qa_scene_vertex value=io->direction==QA_SOURCE_SAVE_WRITE?(*values)[i]:(qa_scene_vertex){0};
        if(!q3p_packet_vertex_fields(io,&value)) return false;
        if(io->direction==QA_SOURCE_SAVE_READ) ((qa_scene_vertex *)*values)[i]=value;
    }
    return true;
}
static bool options(qa_source_save_io *io,qa_q3_scene_packet_owner *owner,const qa_scene_frame_checkpoint_refs *refs)
{
    qa_q3_scene_options *v=&owner->view.options; qa_scene_world_input *w=&v->world;
    if(w->source_visibility || w->source_cluster_modified || w->source_cluster_clear ||
        w->source_cluster_print || w->shadow_lights || w->shadow_light_count ||
        w->entity_material || w->q1_mirror || w->q1_sky_environment || w->q1_sky || w->flare)
        return fail(io->error,QA_ERROR_UNSUPPORTED,"Q3 packet has an unregistered retained pointer domain");
    if(!q3p_packet_view_fields(io,&w->view) || !state(io,&v->state)) return false;
    ENUM(qa_scene_family,v,world_family,QA_SCENE_Q3);
    FIELD(u32,v,first_entity);
    FIELD(u32,v,shadow_mode);
    FIELD(f32,v,lod_scale);
    FIELD(f32,v,lod_bias);
    FIELD(f32,v,ambient_scale);
    FIELD(f32,v,directed_scale);
    FIELD(f32,v,near_clip);
    FIELD(vec3,v,weapon_offset);
    FIELD(bool,v,split_screen);
    FIELD(bool,v,supplemental_weapon);
    FIELD(bool,v,no_entities);
    FIELD(bool,v,no_portals);
    FIELD(bool,v,portal_only);
    FIELD(bool,v,no_refresh);
    FIELD(i32,&v->rail,core_width); FIELD(i32,&v->rail,ring_width); FIELD(f32,&v->rail,segment_length);
    if(!vertices(io,owner,&v->rail.retained_count,&v->rail.retained_vertices)) return false;
    FIELD(f64,w,seconds); FIELD(i64,w,milliseconds);
    FIELD(bool,w,no_world);
    FIELD(bool,w,no_vis);
    FIELD(bool,w,no_cull);
    FIELD(bool,w,alternate_animation);
    FIELD(bool,w,skip_world);
    FIELD(bool,w,no_curves);
    FIELD(bool,w,disable_face_plane_cull);
    FIELD(bool,w,lock_pvs);
    FIELD(bool,w,source_hyperspace);
    FIELD(bool,w,source_show_cluster);
    FIELD(bool,w,source_show_cluster_modified);
    FIELD(bool,w,use_pvs_origin);
    FIELD(bool,w,use_secondary_cluster);
    FIELD(bool,w,use_projected_lights);
    FIELD(bool,w,source_order);
    FIELD(bool,w,source_entity_cells);
    FIELD(bool,w,use_animation_frame);
    FIELD(bool,w,override_sky);
    FIELD(bool,w,sky_auto_rotate);
    FIELD(bool,w,legacy_flashblend);
    FIELD(bool,w,legacy_texture_sort);
    FIELD(i32,w,fast_sky);
    FIELD(i32,w,secondary_cluster);
    FIELD(f32,w,source_far_clip);
    FIELD(f32,w,curve_error);
    FIELD(f32,w,identity_light);
    FIELD(f32,w,sky_rotation);
    FIELD(vec3,w,pvs_origin);
    FIELD(vec3,w,sky_axis);
    FIELD(u32,w,animation_frame);
    qa_scene_fog_volume fog={.fog=w->fog};
    if(!q3p_packet_fog_fields(io,&fog)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) w->fog=fog.fog;
    qa_scene_source_diagnostics *d=&w->source_diagnostics;
    FIELD(i32,d,debug_sort);
    FIELD(i32,d,stencil_bits);
    FIELD(i32,d,fast_sky);
    FIELD(i32,d,lightmap);
    FIELD(i32,d,rail_core_width);
    FIELD(i32,d,rail_width);
    FIELD(f32,d,rail_segment_length);
    FIELD(bool,d,show_triangles);
    FIELD(bool,d,show_normals);
    FIELD(bool,d,show_sky);
    FIELD(bool,d,no_bind);
    FIELD(bool,d,vertex_lighting);
    FIELD(f32,d,polygon_offset_factor);
    FIELD(f32,d,polygon_offset_units);
    qa_scene_legacy_policy *p=&w->legacy_policy;
    ENUM(qa_scene_family,p,source_family,QA_SCENE_Q3);
    FIELD(bool,p,present);
    FIELD(bool,p,fullbright);
    FIELD(bool,p,lightmap);
    FIELD(bool,p,dynamic);
    FIELD(bool,p,saturate);
    FIELD(bool,p,polyblend);
    FIELD(bool,p,cull);
    FIELD(bool,p,clear);
    FIELD(bool,p,flares);
    FIELD(bool,p,planar_shadows);
    FIELD(bool,p,double_eyes);
    FIELD(f32,p,modulate); FIELD(u8,p,monolightmap);
    ENUM(qa_scene_legacy_world_phase,w,legacy_phase,QA_LEGACY_WORLD_WATER);
    if(!allocate(io,owner,&w->visible_area_bytes,1,1,(const void **)&w->visible_areas) ||
        !qa_source_save_bytes(io,(void *)w->visible_areas,w->visible_area_bytes) ||
        !lights(io,owner,refs,&w->light_count,&w->lights) ||
        !lights(io,owner,refs,&w->projected_light_count,&w->projected_lights)) return false;
    bool q1=w->q1_styles!=NULL,q2=w->q2_styles!=NULL;
    if(!qa_source_save_bool(io,&q1) || !qa_source_save_bool(io,&q2) ||
        !qa_source_save_count(io,&w->style_count,SIZE_MAX/sizeof(qa_vec3))) return false;
    if((q1 || q2) && !w->style_count) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(io->offset>io->input.size || (q1 && w->style_count>(io->input.size-io->offset)/4) ||
            (q2 && w->style_count>(io->input.size-io->offset)/12)) return false;
        w->q1_styles=q1?qa_arena_alloc(&owner->storage,(w->style_count?w->style_count:1)*sizeof(float),_Alignof(float),io->error):NULL;
        w->q2_styles=q2?qa_arena_alloc(&owner->storage,(w->style_count?w->style_count:1)*sizeof(qa_vec3),_Alignof(qa_vec3),io->error):NULL;
        if((q1 && !w->q1_styles) || (q2 && !w->q2_styles)) return false;
    }
    for(size_t i=0;(q1 || q2) && i<w->style_count;++i) {
        float a=q1 && io->direction==QA_SOURCE_SAVE_WRITE?w->q1_styles[i]:0;
        qa_vec3 b=q2 && io->direction==QA_SOURCE_SAVE_WRITE?w->q2_styles[i]:(qa_vec3){0};
        if((q1 && !qa_source_save_f32(io,&a)) || (q2 && !qa_source_save_vec3(io,&b))) return false;
        if(io->direction==QA_SOURCE_SAVE_READ) {
            if(q1) ((float *)w->q1_styles)[i]=a;
            if(q2) ((qa_vec3 *)w->q2_styles)[i]=b;
        }
    }
    if(!qa_source_save_count(io,&w->render_text_count,8) ||
        !image(io,refs,w->source_white,owner->images) ||
        !image(io,refs,w->shadow_atlas,owner->images+1)) return false;
    for(unsigned i=0;i<6;++i) if(!image(io,refs,w->sky_images[i],owner->images+2+i)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        for(size_t i=0;i<w->render_text_count;++i) {
            if(!memchr(owner->view.definition.text[i],0,sizeof(owner->view.definition.text[i]))) return false;
            owner->texts[i]=owner->view.definition.text[i];
        }
        w->render_texts=owner->texts;
    } else for(size_t i=0;i<w->render_text_count;++i)
        if(!memchr(owner->view.definition.text[i],0,sizeof(owner->view.definition.text[i])) ||
            !w->render_texts || !w->render_texts[i] ||
            strcmp(w->render_texts[i],owner->view.definition.text[i])) return false;
    return true;
}
static bool fields(qa_source_save_io *io,qa_q3_scene_packet_owner *owner,const qa_scene_frame_checkpoint_refs *refs)
{
    uint8_t magic[4]={'Q','3','P','K'}; uint32_t version=1; qa_q3_scene_packet_view *v=&owner->view;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q3PK",4) ||
        !qa_source_save_u32(io,&version) || version!=1 || !definition(io,&v->definition) ||
        !options(io,owner,refs) || !allocate(io,owner,&v->entity_count,sizeof(*v->entities),128,(const void **)&v->entities) ||
        !allocate(io,owner,&v->polygon_count,sizeof(*v->polygons),64,(const void **)&v->polygons) ||
        !vertices(io,owner,&v->vertex_count,&v->vertices) || !lights(io,owner,refs,&v->light_count,&v->lights)) return false;
    for(size_t i=0;i<v->entity_count;++i) {
        qa_q3_ref_entity value=io->direction==QA_SOURCE_SAVE_WRITE?v->entities[i]:(qa_q3_ref_entity){0};
        if(!q3p_packet_entity_fields(io,&value)) return false;
        if(io->direction==QA_SOURCE_SAVE_READ) ((qa_q3_ref_entity *)v->entities)[i]=value;
    }
    for(size_t i=0;i<v->polygon_count;++i) {
        qa_q3_scene_polygon value=io->direction==QA_SOURCE_SAVE_WRITE?v->polygons[i]:(qa_q3_scene_polygon){0};
        if(!qa_source_save_i32(io,&value.shader) || !qa_source_save_count(io,&value.first,v->vertex_count) ||
            !qa_source_save_count(io,&value.count,v->vertex_count) || value.count>v->vertex_count-value.first ||
            !q3p_packet_fog_fields(io,&value.fog)) return false;
        if(io->direction==QA_SOURCE_SAVE_READ) ((qa_q3_scene_polygon *)v->polygons)[i]=value;
    }
    return true;
}
static bool resolve_lights(const qa_scene_frame_checkpoint_refs *refs,const qa_scene_light *values,size_t count,qa_error *e)
{
    for(size_t i=0;i<count;++i) if(values[i].identity) {
        uint64_t id=0;
        if(!refs->light_identity_decode || !refs->light_identity_decode(refs->context,values[i].identity,&id,e) || !id) return false;
        ((qa_scene_light *)values)[i].identity=id;
    }
    return true;
}
bool qa_q3_scene_packet_checkpoint(const qa_q3_scene_packet_view *v,const qa_scene_frame_checkpoint_refs *refs,qa_buffer *out,qa_error *e)
{
    if(!v || !refs || !out || out->data || out->size) return fail(e,QA_ERROR_ARGUMENT,"Q3 packet capture requires real refs and empty output");
    qa_q3_scene_packet_owner saved={.view=*v}; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,e) && fields(&io,&saved,refs) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_q3_scene_packet_restore(qa_bytes bytes,const qa_scene_frame_checkpoint_refs *refs,qa_q3_scene_packet_owner **out,qa_error *e)
{
    if(!refs || !out || *out) return fail(e,QA_ERROR_ARGUMENT,"Q3 packet restore requires empty owner output");
    qa_q3_scene_packet_owner *owner=calloc(1,sizeof(*owner)); qa_source_save_io io={0};
    if(!owner) return fail(e,QA_ERROR_MEMORY,"Retaining Q3 packet");
    bool ok=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,owner,refs) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    const qa_scene_image **images[]={&owner->view.options.world.source_white,&owner->view.options.world.shadow_atlas,
        owner->view.options.world.sky_images,owner->view.options.world.sky_images+1,owner->view.options.world.sky_images+2,
        owner->view.options.world.sky_images+3,owner->view.options.world.sky_images+4,owner->view.options.world.sky_images+5};
    for(unsigned i=0;ok && i<8;++i) if(owner->images[i])
        ok=refs->image_decode && refs->image_decode(refs->context,owner->images[i],images[i],e) && *images[i];
    ok=ok && resolve_lights(refs,owner->view.lights,owner->view.light_count,e) &&
        resolve_lights(refs,owner->view.options.world.lights,owner->view.options.world.light_count,e) &&
        resolve_lights(refs,owner->view.options.world.projected_lights,owner->view.options.world.projected_light_count,e);
    if(!ok) { qa_q3_scene_packet_destroy(owner); return false; }
    *out=owner; return true;
}
bool qa_q3_scene_packet_read(const qa_q3_scene_packet_owner *owner,qa_q3_scene_packet_view *out)
{ if(!owner || !out) return false; *out=owner->view; return true; }
void qa_q3_scene_packet_destroy(qa_q3_scene_packet_owner *owner)
{ if(owner) { qa_arena_destroy(&owner->storage); free(owner); } }
#undef FIELD
#undef ENUM
