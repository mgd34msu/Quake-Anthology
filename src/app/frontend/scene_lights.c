#include "scene_lights.h"

bool frontend_scene_lights_merge(qa_arena *storage,qa_scene_world_input *world,
    size_t bound,frontend_scene_light_read read,void *context,qa_error *error)
{
    if(!bound) return true;
    if((world->light_count && !world->lights) || world->projected_light_count>QA_SCENE_PROJECTED_LIGHTS ||
        (world->projected_light_count && !world->projected_lights) ||
        bound>SIZE_MAX/sizeof(qa_scene_light) || world->light_count>SIZE_MAX/sizeof(qa_scene_light)-bound)
        return frontend_fail(error,QA_ERROR_FORMAT,"Scene lights exceed the recipient spans");
    size_t projected=world->projected_light_count;
    size_t projected_bound=bound<QA_SCENE_PROJECTED_LIGHTS-projected?bound:QA_SCENE_PROJECTED_LIGHTS-projected;
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
    const qa_scene_light *lights,size_t count,qa_error *error)
{
    light_span span={.lights=lights,.count=count};
    return frontend_scene_lights_merge(storage,world,count,span_read,&span,error);
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
        *light=row->light;*definition=&row->definition;*view=row->view;*present=true;
        break;
    }
    return true;
}
bool frontend_scene_lights_companion_merge(qa_arena *storage,qa_scene_world_input *world,
    const frontend_source_companion_selection *selection,bool skip_view,qa_error *error)
{
    companion_lights cursor={.selection=selection,.skip_view=skip_view};
    return frontend_scene_lights_merge(storage,world,selection->light_count,companion_read,&cursor,error);
}
