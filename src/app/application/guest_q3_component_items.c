#include "guest_q3_component_private.h"
#include "internal.h"
#include <limits.h>

static bool selected(void *context,qa_actor_id actor)
{
    application_q3_component *c=context;
    return c->options.equipment&&qa_equipment_weapon_selected(c->options.equipment,actor,c->options.host.owner);
}
static bool posture(void *context,qa_actor_id actor,qa_bounds *bounds,double *height,int32_t *ground,qa_error *e)
{
    application_q3_component *c=context;
    qa_application *app=c->options.application;
    qa_body_state body;
    qa_application_control_view control;
    qa_application_camera_view camera;
    if(!app||!q3component_current(c,e)||!qa_world_body_read(c->options.host.world,actor,&body,e)) return false;
    if(!qa_application_control_read(app,actor,&control)||!qa_application_control_camera(app,actor,&camera))
        return q3records_fail(e,QA_ERROR_NOT_FOUND,"Component weapon posture lost its authoritative client view");
    *bounds=body.bounds; *height=camera.view_offset.z;
    qa_actor_id ground_actor=qa_actor_reference_resolve(qa_session_actors(app->session),body.ground);
    bool grounded=control.ground.hit!=QA_TRACE_HIT_NONE;
    bool world=grounded&&qa_actor_reference_present(body.ground)&&app->physics&&qa_actor_id_equal(ground_actor,app->physics->world_actor);
    if(grounded&&qa_actor_reference_present(body.ground)&&!world) {
        if(c->entity_record==SIZE_MAX) return q3records_fail(e,QA_ERROR_FORMAT,"Weapon ground actor has no declared source entity array");
        component_record *record=c->records->records+c->entity_record;
        uint32_t pointer;
        if(!application_q3_component_records_pointer(c->records,ground_actor,record->id,&pointer,e)) return false;
        uint32_t slot=(pointer-record->address)/record->stride;
        if(slot>INT32_MAX) return q3records_fail(e,QA_ERROR_FORMAT,"Weapon ground slot exceeds its Source ABI");
        *ground=(int32_t)slot;
    } else *ground=control.ground.hit!=QA_TRACE_HIT_NONE?1022:1023;
    return q3component_current(c,e);
}
bool q3component_pickup_write(void *context,qa_actor_id actor,qa_item_id item,bool count,bool capacity,qa_error *e)
{
    application_q3_component *c=context;
    const qa_pickup_execution *execution=NULL; bool found=false;
    if(!c->options.application||!c->options.application->pickups)
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component inventory lost its canonical pickup service");
    if(!qa_pickups_execution_read(c->options.application->pickups,actor,c->options.host.owner,&execution,&found,e)) return false;
    if(!found) return true;
    if(!qa_pickup_current(execution)||!qa_pickup_recipient_is(execution,actor))
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component item write lost its actual original pickup execution");
    size_t size=0; const qa_pickup_write *writes=qa_pickup_writes(execution,&size);
    for(size_t i=0;i<size;++i) {
        const qa_pickup_write *write=writes+i;
        if(write->resource.kind==QA_PICKUP_INVENTORY&&write->resource.item==item&&
            (!count||write->fields!=QA_PICKUP_CAPACITY)&&(!capacity||write->fields!=QA_PICKUP_COUNT)) return true;
    }
    return q3records_fail(e,QA_ERROR_ARGUMENT,"Original component pickup changed an undeclared item dimension");
}
static bool weapon_current(void *context,qa_actor_id actor)
{ return application_q3_mod_items_actor_current(context,actor); }
static bool weapon_read(void *context,qa_actor_id actor,qa_weapon_presentation *out,qa_error *e)
{
    application_q3_items_weapon_view view;
    if(!application_q3_mod_items_weapon_read(context,actor,&view,e)) return false;
    *out=(qa_weapon_presentation){.provider=view.provider,.active=view.active,.pending=view.pending,.context=context};
    return true;
}
static bool accepts(void *context,qa_actor_id actor,qa_item_id item,bool *out,qa_error *e)
{ return application_q3_mod_items_weapon_accepts(context,actor,item,out,e); }
static bool declares(void *context,qa_actor_id actor,qa_item_id item,bool *out,qa_error *e)
{ return application_q3_mod_items_weapon_declares(context,actor,item,out,e); }
static bool request(void *context,qa_actor_id actor,qa_item_id item,bool *out,uint64_t *id,qa_error *e)
{
    application_q3_item_request receipt; application_q3_item_request_status state;
    if(!application_q3_mod_items_request(context,actor,item,&receipt,e)||
        !application_q3_mod_items_request_status(context,&receipt,&state,e)) return false;
    *out=state!=Q3_ITEM_REQUEST_REFUSED; *id=receipt.id; return true;
}
static bool select_weapon(void *context,qa_actor_id actor,qa_item_id item,bool *out,qa_error *e)
{
    if(!accepts(context,actor,item,out,e)) return false;
    if(!*out) return true;
    uint64_t id;
    return request(context,actor,item,out,&id,e);
}
static bool holster(void *context,qa_actor_id actor,qa_error *e)
{ return application_q3_mod_items_weapon_holster(context,actor,e); }
static bool holstered(void *context,qa_actor_id actor,bool *out,qa_error *e)
{ return application_q3_mod_items_weapon_holstered(context,actor,out,e); }
static bool status(void *context,qa_actor_id actor,uint64_t id,qa_item_id item,qa_weapon_request_status *out,qa_error *e)
{
    application_q3_item_request receipt={actor,id,item}; application_q3_item_request_status state;
    if(!application_q3_mod_items_request_status(context,&receipt,&state,e)) return false;
    *out=state==Q3_ITEM_REQUEST_PENDING?QA_WEAPON_REQUEST_PENDING:
        state==Q3_ITEM_REQUEST_ACCEPTED?QA_WEAPON_REQUEST_ACCEPTED:QA_WEAPON_REQUEST_REFUSED;
    return true;
}
static bool cancel(void *context,qa_actor_id actor,uint64_t id,qa_item_id item,qa_error *e)
{ application_q3_item_request receipt={actor,id,item}; return application_q3_mod_items_request_cancel(context,&receipt,e); }
static bool restore_request(void *context,qa_actor_id actor,uint64_t id,qa_item_id item,qa_error *e)
{ application_q3_item_request receipt; return application_q3_mod_items_request_restore(context,actor,id,item,&receipt,e); }
static bool weapon_bind(void *context,qa_actor_id actor,application_q3_mod_items *items,qa_error *e)
{
    application_q3_component *c=context;
    qa_equipment_weapon_binding binding={.provider=c->options.host.owner,.context=items,.source_input=true,
        .current=weapon_current,.read=weapon_read,.declares=declares,.accepts=accepts,.select=select_weapon,
        .holster=holster,.holstered=holstered,.resume=request,.status=status,.cancel=cancel,.restore_request=restore_request};
    return qa_equipment_weapon_bind(c->options.equipment,actor,&binding,e);
}
static bool weapon_unbind(void *context,qa_actor_id actor,application_q3_mod_items *items,qa_error *e)
{
    application_q3_component *c=context;
    return qa_equipment_weapon_unbind(c->options.equipment,actor,c->options.host.owner,items,e);
}
static bool weapon_bound(void *context,qa_actor_id actor,const application_q3_mod_items *items)
{
    application_q3_component *c=context;
    return qa_equipment_weapon_binding_is(c->options.equipment,actor,c->options.host.owner,items);
}
bool q3component_items_create(application_q3_component *c,qa_error *e)
{
    if(!application_q3_mod_items_profile_create(c->profile,qa_session_strings(c->options.host.session),&c->items_profile,e)) return false;
    if(!c->items_profile) return true;
    if(!c->options.application)
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Declared component items require their actual application weapon controller");
    application_q3_mod_items_services services={.context=c,.selected=selected,.posture=posture,.pickup_write=q3component_pickup_write,
        .weapon_bind=weapon_bind,.weapon_unbind=weapon_unbind,.weapon_current=weapon_bound};
    return application_q3_mod_items_create(c->items_profile,c->mod,c->options.inventory,&services,&c->items,e);
}
application_q3_mod_items *application_q3_component_items(application_q3_component *c)
{ return c?c->items:NULL; }
size_t application_q3_component_item_definition_count(const application_q3_component *c)
{ return c?application_q3_mod_items_definition_count(c->items_profile):0; }
bool application_q3_component_item_read(application_q3_component *c,qa_actor_id actor,qa_item_id item,
    qa_item_admission *out,qa_bytes *icon,qa_bytes *held,bool *found,qa_error *e)
{
    if(!c||!out||!icon||!held||!found||!q3component_current(c,e)) return false;
    *found=false;
    if(!c->items) return true;
    return application_q3_mod_items_item_read(c->items,actor,item,out,icon,held,found,e);
}
bool application_q3_component_item_definition(const application_q3_component *c,size_t index,qa_item_admission *out,
    qa_bytes *icon,qa_bytes *held,qa_error *e)
{
    if(!c||!q3component_storage((void *)c,e)||!application_q3_mod_items_definition(c->items_profile,index,out,icon,held,e)) return false;
    out->definition.owner=c->options.host.owner; return true;
}
bool application_q3_component_inventory_group(application_q3_component *c,qa_actor_id actor,uint64_t serial,
    const qa_inventory_source_group *saved,qa_inventory_items *out,qa_error *e)
{ return c&&c->items&&application_q3_mod_items_inventory_group(c->items,actor,serial,saved,out,e); }
