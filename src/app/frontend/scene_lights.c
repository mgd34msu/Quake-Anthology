#include "scene_lights.h"
#include "component_scene.h"

bool frontend_scene_lights_merge(qa_arena *storage,qa_scene_world_input *world,
    size_t bound,frontend_scene_light_read read,void *context,bool project,qa_error *error)
{
    if(!bound) return true;
    if((world->light_count && !world->lights) || (project && (world->projected_light_count>QA_SCENE_PROJECTED_LIGHTS ||
        (world->projected_light_count && !world->projected_lights))) ||
        bound>SIZE_MAX/sizeof(qa_scene_light) || world->light_count>SIZE_MAX/sizeof(qa_scene_light)-bound)
        return frontend_fail(error,QA_ERROR_FORMAT,"Scene lights exceed the recipient spans");
    size_t projected=world->projected_light_count;
    size_t projected_bound=project?(bound<QA_SCENE_PROJECTED_LIGHTS-projected?bound:QA_SCENE_PROJECTED_LIGHTS-projected):0;
    qa_scene_light *lights=qa_arena_alloc(storage,(world->light_count+bound)*sizeof(*lights),_Alignof(qa_scene_light),error);
    qa_scene_light *projected_lights=projected_bound?qa_arena_alloc(storage,
        (projected+projected_bound)*sizeof(*projected_lights),_Alignof(qa_scene_light),error):NULL;
    if(!lights || (projected_bound && !projected_lights)) return false;
    if(world->light_count) memcpy(lights,world->lights,world->light_count*sizeof(*lights));
    if(projected_bound && projected) memcpy(projected_lights,world->projected_lights,projected*sizeof(*projected_lights));
    size_t added=0;
    for(;added<bound;++added) {
        qa_scene_light light;const qa_q3_refdef *definition;bool view,present;
        if(!read(context,&light,&definition,&view,&present,error)) return false;
        if(!present) break;
        if(view) light.origin=qa_axis_remap_point(definition->origin,definition->axis,world->view.origin,world->view.axis,light.origin);
        lights[world->light_count+added]=light;
        if(added<projected_bound) projected_lights[projected+added]=light;
    }
    if(!added) return true;
    world->lights=lights;world->light_count+=added;
    if(projected_bound) {
        world->projected_lights=projected_lights;
        world->projected_light_count+=added<projected_bound?added:projected_bound;
    }
    return true;
}

typedef struct light_span { const qa_scene_light *lights;size_t count,next; } light_span;
static bool span_read(void *context,qa_scene_light *light,const qa_q3_refdef **definition,
    bool *view,bool *present,qa_error *error)
{
    (void)error;light_span *span=context;
    *present=span->next<span->count;*view=false;*definition=NULL;
    if(*present) *light=span->lights[span->next++];
    return true;
}
bool frontend_scene_lights_span_merge(qa_arena *storage,qa_scene_world_input *world,
    const qa_scene_light *lights,size_t count,bool project,qa_error *error)
{
    light_span span={.lights=lights,.count=count};
    return frontend_scene_lights_merge(storage,world,count,span_read,&span,project,error);
}

typedef struct companion_lights {
    const frontend_source_companion_selection *selection;size_t next;bool skip_view;
} companion_lights;
static bool companion_read(void *context,qa_scene_light *light,const qa_q3_refdef **definition,
    bool *view,bool *present,qa_error *error)
{
    (void)error;companion_lights *cursor=context;*present=false;
    while(cursor->next<cursor->selection->light_count) {
        const frontend_source_companion_light *row=cursor->selection->lights+cursor->next++;
        if(row->view && cursor->skip_view) continue;
        *light=row->light;*definition=row->definition;*view=row->view;*present=true;
        break;
    }
    return true;
}
bool frontend_scene_lights_companion_merge(qa_arena *storage,qa_scene_world_input *world,
    const frontend_source_companion_selection *selection,bool skip_view,qa_error *error)
{
    companion_lights cursor={.selection=selection,.skip_view=skip_view};
    return frontend_scene_lights_merge(storage,world,selection->light_count,companion_read,&cursor,true,error);
}


typedef struct component_packet_cursor {
    size_t child,packet,count;
    bool entered;
} component_packet_cursor;
static bool component_packet_next(const frontend_scene_component_span *span,
    component_packet_cursor *cursor,const qa_application_q3_component_draw **draw,
    frontend_component_scene_packet *packet,bool *present,qa_error *error)
{
    *present=false;
    while(cursor->child<span->count) {
        *draw=(const qa_application_q3_component_draw *)((const uint8_t *)span->draws+cursor->child*span->stride);
        if(!cursor->entered) {
            if(!frontend_component_scene_packet_count(span->frontend,(*draw)->frontend_identity,
                (*draw)->sequence,&cursor->count,error)) return false;
            cursor->entered=true;
        }
        if(cursor->packet<cursor->count) {
            if(!frontend_component_scene_packet_read(span->frontend,(*draw)->frontend_identity,
                (*draw)->sequence,cursor->packet++,packet,error)) return false;
            *present=true;return true;
        }
        ++cursor->child;cursor->packet=0;cursor->entered=false;
    }
    return true;
}
typedef struct component_light_cursor {
    const frontend_scene_component_span *span;
    component_packet_cursor packets;
    frontend_component_scene_packet current;
    size_t light;
} component_light_cursor;
static bool component_light_read(void *context,qa_scene_light *light,const qa_q3_refdef **definition,
    bool *view,bool *present,qa_error *error)
{
    component_light_cursor *cursor=context;
    *view=false;*definition=NULL;
    while(cursor->light==cursor->current.light_count) {
        const qa_application_q3_component_draw *draw;
        if(!component_packet_next(cursor->span,&cursor->packets,&draw,&cursor->current,present,error)) return false;
        if(!*present) return true;
        cursor->light=0;
    }
    *light=cursor->current.lights[cursor->light++];*present=true;
    return true;
}
bool frontend_scene_components_prepare(qa_arena *storage,const frontend_scene_component_span *span,
    qa_q3_scene_options *options,qa_error *error)
{
    size_t entities=0,lights=0;component_packet_cursor cursor={0};
    for(;;) {
        frontend_component_scene_packet packet;const qa_application_q3_component_draw *draw;bool present;
        if(!component_packet_next(span,&cursor,&draw,&packet,&present,error)) return false;
        if(!present) break;
        if(packet.entity_count>SIZE_MAX-entities || packet.light_count>SIZE_MAX-lights)
            return frontend_fail(error,QA_ERROR_MEMORY,"Component scene extent overflow");
        entities+=packet.entity_count;lights+=packet.light_count;
    }
    if(options->first_entity>1022 || entities>1022-options->first_entity)
        return frontend_fail(error,QA_ERROR_MEMORY,"Component scene leaves primary submission limits");
    options->first_entity+=(uint32_t)entities;
    component_light_cursor reader={.span=span};
    return frontend_scene_lights_merge(storage,&options->world,lights,component_light_read,&reader,true,error);
}
bool frontend_scene_components_submit(const frontend_scene_component_span *span,qa_q3_presentation *presentation,
    const qa_q3_scene_options *options,uint32_t order,qa_scene_frame *frame,qa_error *error)
{
    component_packet_cursor cursor={0};
    for(;;) {
        frontend_component_scene_packet packet;const qa_application_q3_component_draw *draw;bool present;
        if(!component_packet_next(span,&cursor,&draw,&packet,&present,error)) return false;
        if(!present) return true;
        for(size_t i=0;i<packet.entity_count;++i,++order)
            if(!qa_q3_presentation_source_component_entity(presentation,draw->assets,packet.entities+i,
                packet.definition.time,options,order,frame,error)) return false;
        for(size_t i=0;i<packet.polygon_count;++i) {
            const qa_q3_scene_polygon *polygon=packet.polygons+i;
            if(polygon->first>packet.vertex_count || polygon->count>packet.vertex_count-polygon->first)
                return frontend_fail(error,QA_ERROR_FORMAT,"Component polygon leaves its retained vertices");
            if(!qa_q3_presentation_source_component_poly(presentation,draw->assets,polygon->shader,
                packet.vertices+polygon->first,polygon->count,&polygon->fog,packet.definition.time,options,frame,error)) return false;
        }
    }
}
