#include "equipment_requests.h"
#include "equipment_runtime.h"
#include "guest_q3_private.h"
#include "guest_q3_weapons_services.h"
#include "guest_q3_catalog.h"
#include "native_maps.h"
#include "qa/application_qc_presentation.h"
#include <stdlib.h>

static bool current(qa_application *app,application_provider *provider,qa_actor_id actor)
{
    return app&&provider&&provider->constructed&&provider->attached&&!provider->close_pending&&
        qa_actors_get(qa_session_actors(app->session),actor)&&
        application_provider_for(app,actor,QA_ROLE_ARSENAL,"")==provider;
}
bool application_equipment_primary_accepts(void *context,qa_actor_id actor,qa_actor_owner owner,
    qa_item_id item,bool *accepted,qa_error *e)
{
    qa_application *app=context;
    application_provider *provider=app?application_provider_for(app,actor,QA_ROLE_ARSENAL,""):NULL;
    if(!accepted||!owner||!item||!current(app,provider,actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Weapon request requires its genuine selected arsenal and full actor");
    *accepted=false;
    if(provider->owner!=owner) return true;
    bool declared=false;
    if(provider->kind==APPLICATION_PROVIDER_QVM) {
        struct application_q3_guest *engine=q3g_engine(provider);
        const application_q3_catalog_weapon *weapons=NULL; size_t n=0;
        if(!engine||!engine->game||!engine->game->catalog)
            return application_fail(e,QA_ERROR_NOT_FOUND,"Original weapon request lost its actual Source catalog");
        if(!application_q3_catalog_weapons(engine->game->catalog,&weapons,&n,e)) return false;
        for(size_t i=0;i<n;++i) if(weapons[i].item==item) declared=true;
    } else if(provider->kind==APPLICATION_PROVIDER_QC) {
        qa_application_qc_player_ui view;
        if(!qa_application_qc_selected_player_ui_read(app,actor,QA_ROLE_ARSENAL,&view,e)) return false;
        for(size_t i=0;i<view.binding_count;++i) {
            qa_application_qc_weapon_ui_binding binding;
            if(!qa_application_qc_message_player_ui_binding(app,&view,i,&binding,e)) return false;
            if(binding.item==item) declared=true;
        }
        if(!qa_application_qc_message_player_ui_current(app,&view))
            return application_fail(e,QA_ERROR_NOT_FOUND,"QC weapon request replaced its true declaration owner");
    } else if(provider->kind==APPLICATION_PROVIDER_Q1) {
        for(int i=0;i<QA_Q1_WEAPON_COUNT;++i) if(qa_q1_weapon_item(provider->state.q1,(qa_q1_weapon)i)==item) declared=true;
    } else if(provider->kind==APPLICATION_PROVIDER_Q2) {
        const char *name=qa_strings_cstr(qa_session_strings(app->session),item);
        for(int i=1;i<QA_Q2_WEAPON_COUNT;++i) {
            const qa_q2_weapon_definition *definition=qa_q2_weapon_definition_at(provider->state.q2,(qa_q2_weapon)i);
            if(name&&definition&&definition->item&&!strcmp(definition->item,name)) declared=true;
        }
    } else if(provider->kind==APPLICATION_PROVIDER_Q3) {
        for(int i=1;i<QA_Q3_WEAPON_COUNT;++i) if(qa_q3_weapon_item(provider->state.q3,(qa_q3_weapon)i,false)==item) declared=true;
    } else {
    size_t count=0;
    if(!qa_inventory_item_definitions(app->inventory,actor,NULL,0,&count,e)) return false;
    if(count>SIZE_MAX/sizeof(qa_item_definition))
        return application_fail(e,QA_ERROR_MEMORY,"Weapon declaration inventory exceeds native extent");
    qa_item_definition *definitions=count?malloc(count*sizeof(*definitions)):NULL;
    if(count&&!definitions) return application_fail(e,QA_ERROR_MEMORY,"Reading genuine weapon request declarations");
    size_t written=0;
    bool ok=qa_inventory_item_definitions(app->inventory,actor,definitions,count,&written,e);
    if(ok&&written>count) ok=application_fail(e,QA_ERROR_ARGUMENT,"Weapon declaration extent changed during request");
    for(size_t i=0;ok&&i<written;++i) if(definitions[i].item==item&&definitions[i].owner==owner&&definitions[i].weapon) declared=true;
    free(definitions);
    if(!ok) return false;
    }
    if(!declared) return true;
    double quantity=0;
    if(!qa_inventory_count_read(app->inventory,actor,item,&quantity,e)) return false;
    if(!current(app,provider,actor)) return application_fail(e,QA_ERROR_NOT_FOUND,"Weapon request source changed during its quantity read");
    if(quantity>0) *accepted=true;
    else if(provider->kind==APPLICATION_PROVIDER_NATIVE||provider->kind==APPLICATION_PROVIDER_QVM) {
        qa_item_id active=0;
        if(!qa_application_weapon_read(app,actor,&active,e)) return false;
        if(!current(app,provider,actor)) return application_fail(e,QA_ERROR_NOT_FOUND,"Weapon request source changed during its active read");
        *accepted=active==item;
    }
    return true;
}
bool application_equipment_primary_select(void *context,qa_actor_id actor,qa_actor_owner owner,
    qa_item_id item,bool *accepted,qa_error *e)
{
    qa_application *app=context;
    if(!application_equipment_primary_accepts(app,actor,owner,item,accepted,e)) return false;
    if(!*accepted) return true;
    application_provider *provider=application_provider_for(app,actor,QA_ROLE_ARSENAL,"");
    bool ok;
    if(provider->kind==APPLICATION_PROVIDER_QC)
        ok=qa_application_qc_weapon_request(app,actor,item,accepted,e);
    else if(provider->kind==APPLICATION_PROVIDER_QVM) {
        struct application_q3_guest *engine=q3g_engine(provider);
        if(!engine||!engine->game||!engine->game->weapon_services)
            return application_fail(e,QA_ERROR_UNSUPPORTED,"Original weapon request has no admitted Source request owner");
        ok=application_q3_weapons_services_select_intent(engine->game->weapon_services,actor,owner,item,accepted,e);
    } else if(provider->kind==APPLICATION_PROVIDER_Q1||provider->kind==APPLICATION_PROVIDER_Q2||provider->kind==APPLICATION_PROVIDER_Q3)
        ok=application_native_mode_select_weapon(app,actor,item,e);
    else return application_fail(e,QA_ERROR_UNSUPPORTED,"Native external weapon request requires its genuine entered Source command adapter");
    if(!ok) return false;
    if(!current(app,provider,actor)) return application_fail(e,QA_ERROR_NOT_FOUND,"Weapon selection retired its actual source or full actor");
    return true;
}
bool application_equipment_request_weapon(qa_application *app,qa_actor_id actor,qa_actor_owner owner,
    qa_item_id item,bool *accepted,qa_error *e)
{
    if(!app||!accepted||!owner||!item||!app->session||!qa_actors_get(qa_session_actors(app->session),actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Weapon request requires a genuine current application actor and source");
    *accepted=false;
    qa_equipment_weapon_view slot; bool found=false;
    if(app->equipment&&!qa_equipment_weapon_view_read(app->equipment,actor,&slot,&found,e)) return false;
    if(found&&slot.source.owner==owner&&slot.item==item) {
        if(!application_equipment_runtime_owner_current(app->equipment_runtime,owner))
            return application_fail(e,QA_ERROR_NOT_FOUND,"Weapon request lost its real equipment source");
        if(!qa_equipment_select_grapple(app->equipment,actor,true,e)||!qa_equipment_reconcile(app->equipment,actor,e)) return false;
        qa_equipment_weapon_view reached; bool present=false;
        if(!qa_equipment_weapon_view_read(app->equipment,actor,&reached,&present,e)) return false;
        *accepted=present&&reached.source.owner==owner&&reached.item==item&&
            application_equipment_runtime_owner_current(app->equipment_runtime,owner);
        return true;
    }
    qa_equipment_state state;
    bool coordinated=app->equipment&&qa_equipment_read(app->equipment,actor,&state)&&
        state.selection.binding==QA_EQUIPMENT_WEAPON_SLOT&&state.selection.grapple!=QA_GRAPPLE_DISABLED;
    if(!coordinated) return application_equipment_primary_select(app,actor,owner,item,accepted,e);
    if(!qa_equipment_request_primary(app->equipment,actor,owner,item,accepted,e)) return false;
    if(!*accepted) return true;
    if(!qa_equipment_reconcile(app->equipment,actor,e)) return false;
    application_provider *provider=application_provider_for(app,actor,QA_ROLE_ARSENAL,"");
    if(!current(app,provider,actor)||provider->owner!=owner)
        return application_fail(e,QA_ERROR_NOT_FOUND,"Coordinated weapon request replaced its actual primary source");
    return true;
}
