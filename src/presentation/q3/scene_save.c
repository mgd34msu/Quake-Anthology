#include "internal.h"
#include "qa/q3_presentation_save.h"
#include "scene_fields_save.h"
#include "qa/hash.h"

#define FIELD(type, object, name) do { if (!qa_source_save_##type(io, &(object)->name)) return false; } while (0)
static bool vec4(qa_source_save_io *io, qa_scene_vec4 *v)
{
    FIELD(f32,v,x); FIELD(f32,v,y); FIELD(f32,v,z); FIELD(f32,v,w); return true;
}
static bool vec2(qa_source_save_io *io, qa_scene_vec2 *v)
{
    FIELD(f32,v,x); FIELD(f32,v,y); return true;
}
static bool plane(qa_source_save_io *io, qa_scene_plane *p)
{
    FIELD(vec3,p,normal); FIELD(f32,p,distance); return true;
}
static bool view(qa_source_save_io *io, qa_scene_view *v)
{
    FIELD(i32,&v->viewport,x); FIELD(i32,&v->viewport,y);
    FIELD(u32,&v->viewport,width); FIELD(u32,&v->viewport,height); FIELD(vec3,v,origin);
    for (size_t i=0;i<3;++i) if (!qa_source_save_vec3(io,&v->axis[i])) return false;
    for (size_t i=0;i<16;++i) if (!qa_source_save_f32(io,&v->projection.m[i])) return false;
    FIELD(bool,v,clear_color); FIELD(bool,v,clear_depth); FIELD(bool,v,clear_stencil);
    FIELD(bool,v,clip_enabled); FIELD(bool,v,mirror);
    if (!vec4(io,&v->color) || !plane(io,&v->clip_plane)) return false;
    FIELD(f32,v,depth); FIELD(u32,v,seat); return true;
}
static bool fog(qa_source_save_io *io, qa_scene_fog_volume *v)
{
    uint32_t kind=v->fog.kind, effect=v->fog.effect;
    FIELD(u32,v,index);
    if (!qa_source_save_u32(io,&kind) || kind>QA_FOG_Q2 || !qa_source_save_u32(io,&effect) || effect>QA_FOG_NO_EFFECT) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) { v->fog.kind=(qa_scene_fog_kind)kind; v->fog.effect=(qa_scene_fog_effect)effect; }
    FIELD(vec3,&v->fog,color); FIELD(vec3,&v->fog,height_color); FIELD(vec3,&v->fog,height_end_color);
    FIELD(f32,&v->fog,density); FIELD(f32,&v->fog,amount); FIELD(f32,&v->fog,sky_factor);
    FIELD(f32,&v->fog,height_density); FIELD(f32,&v->fog,height_start); FIELD(f32,&v->fog,height_end);
    FIELD(f32,&v->fog,height_falloff); FIELD(f32,&v->fog,far_depth); FIELD(bool,&v->fog,sky_drawn);
    FIELD(f32,v,tc_scale); FIELD(bool,v,has_surface); return plane(io,&v->surface);
}
static bool entity(qa_source_save_io *io, qa_q3_ref_entity *e)
{
    uint32_t kind=e->kind;
    if (!qa_source_save_u32(io,&kind) || kind>QA_Q3_REF_PORTAL) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) e->kind=(qa_q3_ref_kind)kind;
    FIELD(i32,e,flags); FIELD(i32,e,model); FIELD(i32,e,frame); FIELD(i32,e,old_frame);
    FIELD(i32,e,skin); FIELD(i32,e,custom_skin); FIELD(i32,e,custom_shader); FIELD(vec3,e,lighting_origin);
    for (size_t i=0;i<3;++i) if (!qa_source_save_vec3(io,&e->axis[i])) return false;
    FIELD(vec3,e,origin); FIELD(vec3,e,old_origin); FIELD(f32,e,shadow_plane); FIELD(f32,e,back_lerp);
    FIELD(f32,e,shader_time); FIELD(f32,e,radius); FIELD(f32,e,rotation);
    return vec2(io,&e->shader_texcoord) && qa_source_save_bytes(io,e->color,4) && qa_source_save_bool(io,&e->non_normalized_axes);
}
static bool vertex(qa_source_save_io *io, qa_scene_vertex *v)
{
    FIELD(vec3,v,position); FIELD(vec3,v,normal);
    return vec2(io,&v->texcoord) && vec2(io,&v->lightmap) && vec4(io,&v->color);
}
static bool light(qa_source_save_io *io, qa_scene_light *v)
{
    uint32_t family=v->family;
    FIELD(vec3,v,origin); FIELD(vec3,v,color); FIELD(vec3,v,direction);
    FIELD(f32,v,radius); FIELD(f32,v,minimum); FIELD(f32,v,scale); FIELD(f32,v,cos_half_angle);
    FIELD(bool,v,additive); FIELD(bool,v,spot); FIELD(bool,v,casts_shadow);
    FIELD(u64,v,identity); FIELD(u64,v,revision); FIELD(u32,v,shadow_resolution);
    if (!qa_source_save_u32(io,&family) || family!=QA_SCENE_Q3 || v->identity || v->revision) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) v->family=(qa_scene_family)family;
    return true;
}
static bool parser(qa_source_save_io *io, qa_common_parser *p)
{
    if (!qa_source_save_count(io,&p->token_length,QA_COMMON_TOKEN_CAPACITY) ||
        !qa_source_save_bytes(io,p->token,p->token_length) ||
        !qa_source_save_count(io,&p->name_length,QA_COMMON_TOKEN_CAPACITY-1) ||
        !qa_source_save_bytes(io,p->name,p->name_length) || !qa_source_save_i32(io,&p->line)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) { p->token[p->token_length]=0; p->name[p->name_length]=0; }
    return true;
}
static bool cursor(qa_source_save_io *io, qa_q3_presentation *p)
{
    bool entities=p->cursor.source.data==p->entity_text.data && p->cursor.source.size==p->entity_text.size && (p->entity_text.data || p->entity_text.size);
    if (io->direction==QA_SOURCE_SAVE_WRITE && !entities && (p->cursor.source.data || p->cursor.source.size))
        return q3p_fail(io->error,QA_ERROR_UNSUPPORTED,"Q3 parser retains an unqualified entity source");
    if (!qa_source_save_bool(io,&entities)) return false;
    qa_bytes source=entities?p->entity_text:(qa_bytes){0};
    qa_sha256_digest actual, saved; qa_sha256(source,&actual); saved=actual;
    if (!qa_source_save_bytes(io,saved.bytes,sizeof(saved.bytes)) || memcmp(saved.bytes,actual.bytes,sizeof(saved.bytes))) return false;
    uint32_t end=p->cursor.end;
    if (!qa_source_save_u32(io,&end) || end!=QA_COMMON_TERMINATED) return false;
    size_t terminator=p->cursor.terminator, offset=p->cursor.offset; bool ended=p->cursor.ended;
    if (!qa_source_save_count(io,&terminator,source.size) || !qa_source_save_count(io,&offset,source.size) ||
        !qa_source_save_bool(io,&ended)) return false;
    qa_common_cursor qualified;
    if (!qa_common_cursor_init(&qualified,source,(qa_common_end)end,io->error) || qualified.terminator!=terminator || offset>terminator || (ended && offset)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) { qualified.offset=offset; qualified.ended=ended; p->cursor=qualified; }
    return true;
}
static bool allocation(qa_source_save_io *io, void **data, size_t *count,
    size_t *capacity, size_t width, size_t maximum)
{
    if ((count && !qa_source_save_count(io,count,maximum)) ||
        !qa_source_save_count(io,capacity,SIZE_MAX/width) || (count && *count>*capacity)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return (*data!=NULL)==(*capacity!=0);
    *data=*capacity?calloc(*capacity,width):NULL;
    if (*capacity && !*data) return q3p_fail(io->error,QA_ERROR_MEMORY,"Restoring Q3 scene allocation");
    return true;
}
static bool arrays(qa_source_save_io *io, qa_q3_presentation *p)
{
    size_t maximum=io->direction==QA_SOURCE_SAVE_READ?io->input.size:SIZE_MAX;
    if (!allocation(io,(void**)&p->entities,&p->entity_count,&p->entity_capacity,sizeof(*p->entities),maximum/128) ||
        !allocation(io,(void**)&p->polygons,&p->polygon_count,&p->polygon_capacity,sizeof(*p->polygons),maximum/64) ||
        !allocation(io,(void**)&p->vertices,&p->vertex_count,&p->vertex_capacity,sizeof(*p->vertices),maximum/56) ||
        !allocation(io,(void**)&p->lights,&p->light_count,&p->light_capacity,sizeof(*p->lights),maximum/64) ||
        !allocation(io,(void**)&p->portals,NULL,&p->portal_capacity,sizeof(*p->portals),0)) return false;
    for (size_t i=0;i<p->entity_count;++i) {
        qa_q3_ref_entity e=io->direction==QA_SOURCE_SAVE_WRITE?p->entities[i]:(qa_q3_ref_entity){0};
        if (!entity(io,&e)) return false;
        const qa_material *shader; const qa_model_skin_map *skin; const q3p_model *model;
        bool source_scene=p->options.source_state || p->options.source_scene_membership;
        if (!source_scene && e.kind!=QA_Q3_REF_POLY && e.kind!=QA_Q3_REF_PORTAL && !q3p_shader_get(p->options.assets,e.custom_shader,&shader,io->error)) return false;
        if (!source_scene && e.kind==QA_Q3_REF_MODEL && (!q3p_model_get(p->options.assets,e.model,&model,io->error) || !q3p_skin_get(p->options.assets,e.custom_skin,&skin,io->error))) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) p->entities[i]=e;
    }
    size_t next=0;
    for (size_t i=0;i<p->polygon_count;++i) {
        q3p_polygon v=io->direction==QA_SOURCE_SAVE_WRITE?p->polygons[i]:(q3p_polygon){0}; const qa_material *shader;
        if (!qa_source_save_i32(io,&v.shader) || !v.shader || !qa_source_save_count(io,&v.first,p->vertex_count) ||
            !qa_source_save_count(io,&v.count,p->vertex_count) || v.first!=next || v.count>p->vertex_count-next ||
            !fog(io,&v.fog) || !q3p_shader_get(p->options.assets,v.shader,&shader,io->error)) return false;
        next+=v.count; if (io->direction==QA_SOURCE_SAVE_READ) p->polygons[i]=v;
    }
    if (next!=p->vertex_count) return false;
    for (size_t i=0;i<p->vertex_count;++i) {
        qa_scene_vertex v=io->direction==QA_SOURCE_SAVE_WRITE?p->vertices[i]:(qa_scene_vertex){0};
        if (!vertex(io,&v)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) p->vertices[i]=v;
    }
    for (size_t i=0;i<p->light_count;++i) {
        qa_scene_light v=io->direction==QA_SOURCE_SAVE_WRITE?p->lights[i]:(qa_scene_light){0};
        if (!light(io,&v)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) p->lights[i]=v;
    }
    return true;
}
static bool fields(qa_source_save_io *io, qa_q3_presentation *p)
{
    uint8_t magic[4]={'Q','3','P','S'}; if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q3PS",4)) return false;
    if ((!qa_source_save_u32(io,&p->source_entity_first) || p->source_entity_first>1022)) return false;
    FIELD(bool,p,world_loaded); FIELD(bool,p,material_view_valid);
    if (p->world_loaded && (!p->world || !p->geometry)) return false;
    FIELD(i32,p,render_milliseconds);
    if (!vec4(io,&p->color) || !view(io,&p->material_view) || !parser(io,&p->parser) || !cursor(io,p)) return false;
    FIELD(i32,&p->options.viewport,x); FIELD(i32,&p->options.viewport,y);
    FIELD(u32,&p->options.viewport,width); FIELD(u32,&p->options.viewport,height);
    if (!p->options.viewport.width || !p->options.viewport.height) return false;
    return arrays(io,p);
}
#undef FIELD
bool q3p_packet_entity_fields(qa_source_save_io *io,qa_q3_ref_entity *value) { return entity(io,value); }
bool q3p_packet_vertex_fields(qa_source_save_io *io,qa_scene_vertex *value) { return vertex(io,value); }
bool q3p_packet_view_fields(qa_source_save_io *io,qa_scene_view *value) { return view(io,value); }
bool q3p_packet_fog_fields(qa_source_save_io *io,qa_scene_fog_volume *value) { return fog(io,value); }
bool qa_q3_presentation_scene_checkpoint(const qa_q3_presentation *p, qa_buffer *out, qa_error *error)
{
    if (!p || !out || out->data || out->size) return q3p_fail(error,QA_ERROR_ARGUMENT,"Q3 scene capture requires an empty output");
    bool owned_assets = false;
    if (!q3p_capture_begin((qa_q3_presentation *)p, &owned_assets, error)) return false;
    qa_source_save_io io; qa_q3_presentation saved=*p;
    if (!qa_source_save_writer(&io,NULL,error)) { q3p_capture_end((qa_q3_presentation *)p, owned_assets); return false; }
    bool ok=fields(&io,&saved) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) q3p_fail(error,QA_ERROR_FORMAT,"Q3 retained scene state is inconsistent");
    qa_source_save_dispose(&io); q3p_capture_end((qa_q3_presentation *)p, owned_assets); return ok;
}
bool qa_q3_presentation_scene_restore(qa_q3_presentation *p, qa_bytes bytes, qa_error *error)
{
    if (!p || p->entity_count || p->polygon_count || p->vertex_count || p->light_count)
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Q3 scene restore requires an empty idle candidate presentation");
    bool owned_assets = false;
    if (!q3p_capture_begin(p, &owned_assets, error)) return false;
    qa_q3_presentation saved=*p;
    saved.entities=NULL; saved.polygons=NULL; saved.vertices=NULL; saved.lights=NULL; saved.portals=NULL;
    saved.entity_capacity=saved.polygon_capacity=saved.vertex_capacity=saved.light_capacity=saved.portal_capacity=0;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) { q3p_capture_end(p, owned_assets); return false; }
    bool ok=fields(&io,&saved) && qa_source_save_finish(&io,NULL);
    if (ok) {
        free(p->entities); free(p->polygons); free(p->vertices); free(p->lights); free(p->portals);
        p->entities=saved.entities; p->polygons=saved.polygons; p->vertices=saved.vertices; p->lights=saved.lights;
        p->portals=saved.portals;
        p->entity_count=saved.entity_count; p->polygon_count=saved.polygon_count; p->vertex_count=saved.vertex_count; p->light_count=saved.light_count;
        p->source_entity_first=saved.source_entity_first;
        p->entity_capacity=saved.entity_capacity; p->polygon_capacity=saved.polygon_capacity; p->vertex_capacity=saved.vertex_capacity; p->light_capacity=saved.light_capacity;
        p->portal_capacity=saved.portal_capacity;
        p->parser=saved.parser; p->cursor=saved.cursor; p->color=saved.color; p->material_view=saved.material_view;
        p->world_loaded=saved.world_loaded; p->material_view_valid=saved.material_view_valid;
        p->render_milliseconds=saved.render_milliseconds; p->options.viewport=saved.options.viewport;
    } else {
        free(saved.entities); free(saved.polygons); free(saved.vertices); free(saved.lights); free(saved.portals);
        if (error && error->code==QA_OK) q3p_fail(error,QA_ERROR_FORMAT,"saved Q3 retained scene state is inconsistent");
    }
    qa_source_save_dispose(&io); q3p_capture_end(p, owned_assets); return ok;
}
