#include "guest_qc_profile.h"
#include "guest_qc_item_weapons.h"
#include "guest_qc_items.h"
#include "qa/application_qc_presentation.h"
#include "qa/qc_observation.h"

static application_qc_client *client_for(struct application_qc_state *engine,qa_actor_id actor)
{
    application_qc_client *found=NULL;
    for(uint32_t i=1;engine && engine->clients && i<=engine->max_clients;++i) {
        application_qc_client *client=engine->clients+i;
        if(!client->connected || !client->spawned || !qa_actor_id_equal(client->actor,actor)) continue;
        if(found) return NULL;
        found=client;
    }
    return found;
}
static bool binding_for(struct application_qc_state *engine,qa_item_id item,
    qa_application_qc_weapon_ui_binding *binding,uint32_t *via,qa_error *error)
{
    size_t count=engine->provider->state.qc.qualified?
        engine->provider->state.qc.qualified->weapon_count:11;
    for(size_t i=0;i<count;++i) {
        qa_application_qc_weapon_ui_binding row; uint32_t transition=0;
        if(!application_qc_weapon_binding_at(engine,i,&row,&transition,error)) {
            if(error && error->code!=QA_OK) return false;
            break;
        }
        if(row.item==item) { *binding=row; *via=transition; return true; }
    }
    return false;
}
static bool observe(struct application_qc_state *engine,uint32_t slot,qa_actor_id actor,
    const char *name,float *out,qa_error *error)
{
    const qa_qc_definition *field=!strcmp(name,"weapon") && engine->provider->state.qc.qualified?
        engine->provider->state.qc.qualified->weapon_field:
        qa_qc_program_find_field(engine->provider->state.qc.program,name);
    if(!field || field->type!=QA_QC_FLOAT)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"QC weapon transition requires its actual source scalar");
    return qa_qc_actor_observation_float(engine->provider->state.qc.instance,slot,actor,field->offset,out,error) &&
        (isfinite(*out) || application_fail(error,QA_ERROR_FORMAT,"QC weapon transition scalar is nonfinite"));
}
static bool impulse(struct application_qc_state *engine,uint32_t slot,float value,qa_error *error)
{
    int32_t reference;
    return qa_qc_slot_reference(engine->provider->state.qc.instance,slot,&reference,error) &&
        application_qc_set_float(engine,reference,"impulse",value,error);
}
void application_qc_weapon_command(struct application_qc_state *engine,qa_actor_id actor)
{
    application_qc_client *client=client_for(engine,actor);
    if(client) { client->pending_weapon=0; client->pending_weapon_following=false; }
}
bool application_qc_pending_weapon_ready(struct application_qc_state *engine,
    const application_qc_client *client,qa_error *error)
{
    if(!client->pending_weapon) return !client->pending_weapon_following ||
        application_fail(error,QA_ERROR_FORMAT,"QC following weapon has no retained selection");
    qa_application_qc_weapon_ui_binding binding; uint32_t via=0;
    return (client->connected && client->spawned && !client->spectator &&
        binding_for(engine,client->pending_weapon,&binding,&via,error) && via) ||
        application_fail(error,QA_ERROR_FORMAT,"QC pending weapon lost its admitted source transition");
}
bool qa_application_qc_weapon_request(qa_application *app,qa_actor_id actor,qa_item_id item,
    bool *accepted,qa_error *error)
{
    if(!accepted || !item) return application_fail(error,QA_ERROR_ARGUMENT,"QC weapon request requires a canonical item");
    *accepted=false;
    application_provider *declared=application_provider_for(app,actor,QA_ROLE_ARSENAL,"");
    if(declared && declared->kind==APPLICATION_PROVIDER_QC && declared->state.qc.qualified &&
        declared->state.qc.qualified->items && declared->state.qc.qualified->items->weapons) {
        if(!declared->state.qc.engine)return application_fail(error,QA_ERROR_NOT_FOUND,"Declared QC weapon lost its actual Source owner");
        return application_qc_item_weapons_select(declared->state.qc.engine,actor,item,accepted,error);
    }
    qa_application_qc_player_ui view;
    if(!qa_application_qc_selected_player_ui_read(app,actor,QA_ROLE_ARSENAL,&view,error)) return false;
    application_provider *provider=application_provider_for(app,actor,QA_ROLE_ARSENAL,"");
    struct application_qc_state *engine=provider->state.qc.engine;
    application_qc_client *client=client_for(engine,actor);
    if(!client || client->spectator) return true;
    qa_application_qc_weapon_ui_binding binding; uint32_t via=0;
    if(!binding_for(engine,item,&binding,&via,error)) return !error || error->code==QA_OK;
    double owned;
    if(!qa_inventory_count_read(app->inventory,actor,item,&owned,error)) return false;
    if(owned<=0) return true;
    if(!qa_application_qc_message_player_ui_current(app,&view))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC weapon request changed its selected source binding");
    client->pending_weapon=0; client->pending_weapon_following=false;
    if(view.weapon==(double)binding.bit) {
        if(!impulse(engine,view.source_slot,0,error)) return false;
    } else {
        if(via && view.weapon!=(double)via && (view.items & via)!=0) client->pending_weapon=item;
        if(!impulse(engine,view.source_slot,(float)binding.impulse,error)) return false;
    }
    *accepted=true; return true;
}
bool application_qc_weapon_before_postthink(struct application_qc_state *engine,qa_actor_id actor,qa_error *error)
{
    application_qc_client *client=client_for(engine,actor); uint32_t slot;
    if(!client || client->spectator || !client->pending_weapon) return true;
    if(!qa_qc_actor_observation_slot(engine->provider->state.qc.instance,actor,&slot,error)) return false;
    if(client->pending_weapon) {
        qa_application_qc_weapon_ui_binding binding; uint32_t via=0; float current_impulse,health; double owned;
        if(!binding_for(engine,client->pending_weapon,&binding,&via,error) ||
            !observe(engine,slot,actor,"impulse",&current_impulse,error) ||
            !observe(engine,slot,actor,"health",&health,error) ||
            !qa_inventory_count_read(engine->provider->application->inventory,actor,binding.item,&owned,error)) return false;
        if(owned<=0 || health<=0) {
            client->pending_weapon=0; client->pending_weapon_following=false;
            if((double)current_impulse==(double)binding.impulse && !impulse(engine,slot,0,error)) return false;
        } else if(current_impulse!=0 && (double)current_impulse!=(double)binding.impulse) {
            client->pending_weapon=0; client->pending_weapon_following=false;
        }
    }
    return true;
}
bool application_qc_weapon_after_postthink(struct application_qc_state *engine,qa_actor_id actor,qa_error *error)
{
    application_qc_client *client=client_for(engine,actor); uint32_t slot;
    if(!client || !client->pending_weapon) return true;
    if(!qa_qc_actor_observation_slot(engine->provider->state.qc.instance,actor,&slot,error)) return false;
    qa_application_qc_weapon_ui_binding binding; uint32_t via=0; float current_impulse,current; double owned;
    if(!binding_for(engine,client->pending_weapon,&binding,&via,error) ||
        !observe(engine,slot,actor,"impulse",&current_impulse,error)) return false;
    if(current_impulse!=0) return true;
    qa_item_id selection=client->pending_weapon; bool following=client->pending_weapon_following;
    client->pending_weapon=0; client->pending_weapon_following=false;
    if(!following) {
        if(!observe(engine,slot,actor,"weapon",&current,error) ||
            !qa_inventory_count_read(engine->provider->application->inventory,actor,binding.item,&owned,error)) return false;
        if((double)current==(double)via && owned>0) {
            client->pending_weapon=selection; client->pending_weapon_following=true;
            if(!impulse(engine,slot,(float)binding.impulse,error)) return false;
        }
    }
    return true;
}
bool application_qc_client_postthink(struct application_qc_state *engine,qa_actor_id actor,qa_error *error)
{
    return application_qc_weapon_before_postthink(engine,actor,error) &&
        application_qc_named(engine,"PlayerPostThink",actor,error) &&
        application_qc_weapon_after_postthink(engine,actor,error);
}
bool qa_application_qc_weapon_settled(qa_application *app,qa_actor_id actor,bool *out,qa_error *error)
{
    if(!out) return application_fail(error,QA_ERROR_ARGUMENT,"QC weapon stage requires its actual result slot");
    application_provider *declared=application_provider_for(app,actor,QA_ROLE_ARSENAL,"");
    if(declared && declared->kind==APPLICATION_PROVIDER_QC && declared->state.qc.qualified &&
        declared->state.qc.qualified->items && declared->state.qc.qualified->items->weapons) {
        if(!declared->state.qc.engine)return application_fail(error,QA_ERROR_NOT_FOUND,"Declared QC weapon lost its actual Source owner");
        return application_qc_item_weapons_settled(declared->state.qc.engine,actor,out,error);
    }
    qa_application_qc_player_ui view;
    if(!qa_application_qc_selected_player_ui_read(app,actor,QA_ROLE_ARSENAL,&view,error)) return false;
    application_provider *provider=application_provider_for(app,actor,QA_ROLE_ARSENAL,"");
    application_qc_client *client=client_for(provider->state.qc.engine,actor);
    if(!client) return application_fail(error,QA_ERROR_NOT_FOUND,"QC weapon stage has no active full client");
    qa_qc_program_info program=qa_qc_program_describe(provider->state.qc.program);
    bool netquake=program.system_crc==5927u && program.file_crc==24778u &&
        program.statement_count==20940u && program.global_count==4287u &&
        program.field_count==218u && program.function_count==2091u;
    bool quakeworld=program.system_crc==54730u && program.file_crc==21450u &&
        program.statement_count==11738u && program.global_count==2114u &&
        program.field_count==216u && program.function_count==482u;
    if(provider->state.qc.qualified || (!netquake && !quakeworld))
        return application_fail(error,QA_ERROR_UNSUPPORTED,"QC weapon stage lacks an artifact-qualified continuation declaration");
    const qa_qc_definition *field=qa_qc_program_find_field(provider->state.qc.program,"think"); int32_t think;
    if(!field || field->type!=QA_QC_FUNCTION ||
        !qa_qc_actor_observation_int(provider->state.qc.instance,view.source_slot,actor,field->offset,&think,error)) return false;
    if(think<0 || !qa_qc_program_function(provider->state.qc.program,(uint32_t)think))
        return application_fail(error,QA_ERROR_FORMAT,"QC weapon continuation is outside its actual program");
    /* These stock program layouts place the shot/axe/nail/light/rocket
     * continuations consecutively. */
    uint32_t first=netquake?249u:217u;
    *out=(uint32_t)think<first || (uint32_t)think>=first+32u;
    return qa_application_qc_message_player_ui_current(app,&view);
}
