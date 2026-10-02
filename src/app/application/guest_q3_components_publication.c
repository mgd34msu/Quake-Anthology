#include "guest_q3_components_private.h"

struct application_q3_component_publication_lease {
    component_game_row *owner;
    application_q3_component_view view;
    application_q3_scene_source source;
    application_q3_scene_context context;
};
bool application_q3_components_publication_borrow(application_q3_components *owner,size_t index,
    qa_actor_id actor,const qa_vec3 *origin,const qa_vec3 axis[3],int32_t time,int32_t elapsed,
    application_q3_component_publication_lease **out,application_q3_scene_context *context,qa_error *e)
{
    application_q3_component_publication publication;
    if(!out||*out||!context||!origin||!axis||!qa_vec_finite(*origin)||!qa_vec_finite(axis[0])||!qa_vec_finite(axis[1])||!qa_vec_finite(axis[2])||
        !application_q3_components_publication_at(owner,index,&publication,e)) return false;
    application_q3_component_publication_lease *lease=calloc(1,sizeof(*lease));
    if(!lease) return application_fail(e,QA_ERROR_MEMORY,"Retaining actual per-viewer component publication");
    lease->owner=owner->rows[index];
    lease->view=(application_q3_component_view){.source=publication.source,.viewer=actor,.origin=*origin,
        .axis={axis[0],axis[1],axis[2]},.time_ms=time,.frame_ms=elapsed,
        .weapon_presented=owner->options.weapon_presented,.weapon_context=owner->options.context};
    lease->source=application_q3_component_view_services(&lease->view);
    if(!lease->source.acquire(lease->source.context,false,&lease->context,e)) { free(lease); return false; }
    *context=lease->context; *out=lease; return true;
}
bool application_q3_components_publication_current(const application_q3_component_publication_lease *lease)
{ return lease&&q3components_current(lease->owner)&&lease->source.current(lease->source.context,&lease->context); }
void application_q3_components_publication_return(application_q3_component_publication_lease **slot)
{
    if(!slot||!*slot) return;
    application_q3_component_publication_lease *lease=*slot;
    lease->source.release(lease->source.context,&lease->context); free(lease); *slot=NULL;
}
