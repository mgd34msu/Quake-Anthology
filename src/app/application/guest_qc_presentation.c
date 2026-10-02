#include "guest_qc_profile.h"
#include "qa/application_qc_presentation.h"
#include "network_q1_signon.h"
#include "qa/qc_observation.h"
#include <limits.h>

static bool source_returned(const application_provider *p)
{
    const struct application_qc_state *engine=p && p->kind==APPLICATION_PROVIDER_QC?p->state.qc.engine:NULL;
    return engine && p->application && p->constructed && p->attached && !p->close_pending &&
        p->launch && p->product && p->state.qc.program && p->state.qc.instance &&
        engine->provider==p && engine->initialized && !engine->loading && !engine->projecting &&
        qa_qc_idle(p->state.qc.instance) && application_qc_input_idle(p) &&
        !p->application->destroy_requested && !qa_session_faulted(p->application->session);
}
static bool source_ready(const application_provider *p)
{ return source_returned(p) && qa_session_safe(p->application->session) && qa_world_idle(p->application->world); }
static application_provider *owner(qa_application *app,qa_actor_owner id)
{
    application_provider *found=NULL;
    for(size_t i=0;app && i<app->provider_count;++i) if(app->providers[i]->owner==id) {
        if(found) return NULL;
        found=app->providers[i];
    }
    return found;
}
static bool scalar(application_provider *p,uint32_t slot,qa_actor_id actor,const char *name,
    const qa_qc_definition *explicit_field,double *out,qa_error *error)
{
    const qa_qc_definition *field=explicit_field?explicit_field:qa_qc_program_find_field(p->state.qc.program,name);
    const struct application_qc_profile *profile=p->state.qc.qualified;
    bool declared=!profile || field==profile->weapon_field;
    for(size_t i=0;profile && i<profile->field_count;++i) declared|=profile->fields[i].definition==field;
    if(!field || field->type!=QA_QC_FLOAT || !declared)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"QC animation continuation has no actual declared scalar field");
    float value;
    if(!qa_qc_actor_observation_float(p->state.qc.instance,slot,actor,field->offset,&value,error)) return false;
    if(!isfinite(value)) return application_fail(error,QA_ERROR_FORMAT,"QC animation continuation is nonfinite");
    *out=(double)value; return true;
}
bool qa_application_qc_animation_read(qa_application *app,qa_actor_id actor,qa_launch_role role,
    qa_application_qc_animation *out,qa_error *error)
{
    if(!app || !out || (role!=QA_ROLE_CHARACTER && role!=QA_ROLE_ARSENAL) ||
        !qa_actors_get(qa_session_actors(app->session),actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC animation requires its actual selected actor and role");
    application_provider *p=application_provider_for(app,actor,role,""); uint32_t slot;
    if(!source_ready(p) || !qa_qc_actor_observation_slot(p->state.qc.instance,actor,&slot,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC animation lost its actual selected source binding");
    qa_application_qc_animation value={.actor=actor,.provider=p->owner,.descriptor=p->launch,
        .program=p->state.qc.program,.instance=p->state.qc.instance,.role=role};
    bool okay=role==QA_ROLE_CHARACTER?
        scalar(p,slot,actor,"frame",NULL,&value.frame,error) && scalar(p,slot,actor,"nextthink",NULL,&value.next_frame_seconds,error):
        scalar(p,slot,actor,"weaponframe",NULL,&value.frame,error) &&
        scalar(p,slot,actor,"attack_finished",NULL,&value.attack_finished_seconds,error) &&
        scalar(p,slot,actor,"weapon",p->state.qc.qualified?p->state.qc.qualified->weapon_field:NULL,&value.source_weapon,error);
    if(okay) *out=value;
    return okay;
}
bool qa_application_qc_animation_current(qa_application *app,const qa_application_qc_animation *view)
{
    qa_application_qc_animation actual;
    return view && qa_application_qc_animation_read(app,view->actor,view->role,&actual,NULL) &&
        actual.provider==view->provider && actual.descriptor==view->descriptor && actual.program==view->program &&
        actual.instance==view->instance && actual.frame==view->frame && actual.next_frame_seconds==view->next_frame_seconds &&
        actual.attack_finished_seconds==view->attack_finished_seconds && actual.source_weapon==view->source_weapon;
}
bool qa_application_qc_message_source_read(qa_application *app,qa_actor_owner id,
    qa_application_qc_message_source *out,bool *found,qa_error *error)
{
    if(!app || !id || !out || !found) return application_fail(error,QA_ERROR_ARGUMENT,"QC message source requires its actual provider");
    *found=false; application_provider *p=owner(app,id);
    if(!p) return application_fail(error,QA_ERROR_ARGUMENT,"Protocol provider is no longer installed");
    if(p->kind!=APPLICATION_PROVIDER_QC) return true;
    if(!source_ready(p) || !p->product->campaign || !p->state.qc.engine->clients)
        return application_fail(error,QA_ERROR_ARGUMENT,"QC message source is not returned and constructed");
    const struct application_qc_state *engine=p->state.qc.engine;
    if(!qa_q1_profile_valid(engine->protocol,error)) return false;
    *out=(qa_application_qc_message_source){.provider=id,.descriptor=p->launch,.program=p->state.qc.program,
        .instance=p->state.qc.instance,.protocol=engine->protocol,.client_slots=engine->max_clients,
        .map_revision=app->map_revision,.options={.standard_quake=strcmp(p->product->campaign,"hipnotic") &&
            strcmp(p->product->campaign,"rogue"),.private_rerelease=engine->profile==QA_QC_RERELEASE}};
    *found=true; return true;
}
bool qa_application_qc_message_source_current(qa_application *app,const qa_application_qc_message_source *view)
{
    qa_application_qc_message_source actual; bool found=false;
    return view && qa_application_qc_message_source_read(app,view->provider,&actual,&found,NULL) && found &&
        actual.descriptor==view->descriptor && actual.program==view->program && actual.instance==view->instance &&
        actual.protocol.kind==view->protocol.kind && actual.protocol.flags==view->protocol.flags &&
        actual.protocol.revision==view->protocol.revision && actual.options.standard_quake==view->options.standard_quake &&
        actual.options.private_rerelease==view->options.private_rerelease && actual.client_slots==view->client_slots &&
        actual.map_revision==view->map_revision;
}
bool qa_application_qc_message_entity(qa_application *app,const qa_application_qc_message_source *view,
    uint32_t slot,qa_actor_id *out,qa_error *error)
{
    qa_qc_slot_binding binding;
    if(!out || !qa_application_qc_message_source_current(app,view) ||
        !qa_qc_slot(view->instance,slot,&binding) || binding.kind==QA_QC_SLOT_FREE)
        return application_fail(error,QA_ERROR_ARGUMENT,"QC message names no current full source entity");
    const qa_actor_record *record=qa_actors_get(qa_session_actors(app->session),binding.actor);
    if(!record || binding.slot!=slot || binding.owner!=record->owner ||
        binding.source_slot!=(record->has_source?record->source_slot:0))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC message entity lost its full generation binding");
    *out=binding.actor; return true;
}
bool qa_application_qc_message_client(qa_application *app,const qa_application_qc_message_source *view,
    qa_actor_id actor,uint32_t *out,qa_error *error)
{
    if(!out || !qa_application_qc_message_source_current(app,view)) return false;
    application_provider *p=owner(app,view->provider); uint32_t found=0;
    for(uint32_t slot=1;slot<=view->client_slots;++slot) {
        const application_qc_client *client=&p->state.qc.engine->clients[slot];
        if(!client->connected || !qa_actor_id_equal(client->actor,actor)) continue;
        qa_actor_id bound;
        if(found || !qa_application_qc_message_entity(app,view,slot,&bound,error) || !qa_actor_id_equal(bound,actor))
            return application_fail(error,QA_ERROR_FORMAT,"QC message client has an ambiguous physical source binding");
        found=slot;
    }
    if(!found) return application_fail(error,QA_ERROR_ARGUMENT,"QC message recipient is not a connected source client");
    *out=found; return true;
}
bool qa_application_qc_message_client_at(qa_application *app,const qa_application_qc_message_source *view,
    uint32_t slot,qa_actor_id *out,bool *found,qa_error *error)
{
    if(!out || !found || !qa_application_qc_message_source_current(app,view) ||
        !slot || slot>view->client_slots)
        return application_fail(error,QA_ERROR_ARGUMENT,"QC message client slot is not an actual source slot");
    *found=false;
    application_provider *p=owner(app,view->provider);
    const application_qc_client *client=&p->state.qc.engine->clients[slot];
    if(!client->connected) return true;
    qa_actor_id bound; uint32_t actual_slot;
    if(!qa_application_qc_message_entity(app,view,slot,&bound,error) ||
        !qa_actor_id_equal(bound,client->actor) ||
        !qa_application_qc_message_client(app,view,bound,&actual_slot,error) || actual_slot!=slot)
        return application_fail(error,QA_ERROR_FORMAT,"QC connected client lost its unique physical source binding");
    *out=bound; *found=true; return true;
}
bool qa_application_qc_message_signon_count(qa_application *app,const qa_application_qc_message_source *view,
    size_t *out,qa_error *error)
{
    if(!out || !qa_application_qc_message_source_current(app,view))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC signon lost its actual retained source");
    *out=application_q1_signon_count(app,view->provider); return true;
}
bool qa_application_qc_message_signon_at(qa_application *app,const qa_application_qc_message_source *view,
    size_t index,qa_application_protocol_event *out,qa_error *error)
{
    return qa_application_qc_message_source_current(app,view) &&
        application_q1_signon_at(app,view->provider,index,out,error);
}
size_t qa_application_qc_message_source_count(const qa_application *app)
{ return app?app->provider_count:0; }
bool qa_application_qc_message_source_at(qa_application *app,size_t ordinal,
    qa_application_qc_message_source *out,bool *found,qa_error *error)
{
    if(!app || ordinal>=app->provider_count)
        return application_fail(error,QA_ERROR_ARGUMENT,"QC protocol source ordinal is absent");
    return qa_application_qc_message_source_read(app,app->providers[ordinal]->owner,out,found,error);
}
bool qa_application_qc_message_angles(qa_application *app,const qa_application_qc_message_source *source,
    qa_actor_id actor,qa_vec3 angles,qa_error *error)
{
    uint32_t slot;
    if(!qa_vec_finite(angles) || !qa_application_qc_message_client(app,source,actor,&slot,error) ||
        actor.slot>=app->control_capacity)
        return application_fail(error,QA_ERROR_ARGUMENT,"QC SETANGLE requires its existing recipient control");
    application_control_record *control=&app->controls[actor.slot];
    if(!control->active || control->moving || control->retired || control->application!=app ||
        !qa_actor_id_equal(control->actor,actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC SETANGLE lost its returned full-generation selected control");
    control->view_angles=angles; control->command_angles=angles; return true;
}
bool qa_application_qc_message_view_offset(qa_application *app,const qa_application_qc_message_source *source,
    qa_actor_id actor,qa_vec3 *out,qa_error *error)
{
    uint32_t slot;
    if(!out || !qa_application_qc_message_client(app,source,actor,&slot,error)) return false;
    application_provider *p=owner(app,source->provider);
    bool qw=qa_q1_is_qw(source->protocol);
    const qa_qc_definition *field=qa_qc_program_find_field(p->state.qc.program,qw?"mins":"view_ofs");
    const struct application_qc_profile *profile=p->state.qc.qualified; bool declared=!profile;
    for(size_t i=0;profile && i<profile->field_count;++i) declared|=profile->fields[i].definition==field;
    qa_vec3 value;
    if(!field || field->type!=QA_QC_VECTOR || !declared)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"QC camera offset has no actual declared source vector");
    if(!qa_qc_actor_observation_vector(p->state.qc.instance,slot,actor,field->offset,&value,error) || !qa_vec_finite(value))
        return application_fail(error,QA_ERROR_FORMAT,"QC camera offset is not a finite source vector");
    if(qw) {
        double health;
        if(!scalar(p,slot,actor,"health",NULL,&health,error)) return false;
        value=qa_v3(0,0,value.z!=-24?8:health<=0?-16:22);
    }
    *out=value; return true;
}

static bool source_word(double value,uint32_t *out,qa_error *error)
{
    if(!isfinite(value))
        return application_fail(error,QA_ERROR_FORMAT,"QC UI item word is nonfinite");
    double word=fmod(trunc(value),4294967296.0);
    if(word<0) word+=4294967296.0;
    *out=(uint32_t)word;
    return true;
}
static bool hipnotic_ui(const qa_qc_program *program)
{
    static const qa_sha256_digest digest={{0x35,0xa2,0xfd,0xc3,0xac,0xb0,0x4b,0xda,
        0xfe,0x8d,0x02,0x69,0xf5,0x32,0x7c,0xd1,0xd5,0xb4,0x79,0x71,0xf1,0xef,
        0x02,0x44,0x29,0xf1,0x57,0x2d,0x32,0x01,0xdc,0x82}};
    qa_qc_program_info actual=qa_qc_program_describe(program);
    return qa_sha256_equal(&actual.digest,&digest);
}
static bool selected_ui_basis(qa_application *app,application_provider *p,qa_actor_id actor,uint32_t slot)
{
    uint32_t actual;
    return app && (app->operation==APPLICATION_IDLE || app->operation==APPLICATION_ADVANCING) &&
        source_returned(p) && p->application==app &&
        application_provider_for(app,actor,QA_ROLE_ARSENAL,"")==p &&
        qa_actors_get(qa_session_actors(app->session),actor) &&
        qa_qc_actor_observation_slot(p->state.qc.instance,actor,&actual,NULL) && actual==slot;
}
static bool player_ui_read(qa_application *app,application_provider *p,
    const qa_application_qc_message_source *source,qa_actor_id actor,uint32_t slot,bool selected,
    qa_application_qc_player_ui *out,qa_error *error)
{
    const struct application_qc_profile *profile=p->state.qc.qualified;
    if(profile) {
        if(!profile->weapon_field || !profile->weapon_count)
            return application_fail(error,QA_ERROR_UNSUPPORTED,"Declared QC UI has no actual source weapon roster");
        for(size_t i=0;i<profile->weapon_count;++i)
            if(!profile->weapon_values[i].ui_declared)
                return application_fail(error,QA_ERROR_UNSUPPORTED,"Declared QC UI requires explicit weapon label, bit and impulse metadata");
    }
    qa_application_qc_player_ui value={.source=*source,.recipient=actor,.source_slot=slot,
        .now_seconds=(double)p->state.qc.engine->source_time_ns/1e9,
        .binding_count=profile?profile->weapon_count:hipnotic_ui(p->state.qc.program)?11:8,.selected_arsenal=selected};
    double items;
    if(!scalar(p,slot,actor,"items",NULL,&items,error) ||
        !scalar(p,slot,actor,"weapon",profile?profile->weapon_field:NULL,&value.weapon,error) ||
        !scalar(p,slot,actor,"currentammo",NULL,&value.current_ammo,error) ||
        !source_word(items,&value.items,error)) return false;
    static const struct { const char *field,*item,*label; } timers[]={
        {"super_damage_finished","q1:item_artifact_super_damage","Quad Damage"},
        {"invincible_finished","q1:item_artifact_invulnerability","Invulnerability"},
        {"invisible_finished","q1:item_artifact_invisibility","Invisibility"},
        {"radsuit_finished","q1:item_artifact_envirosuit","Environment Suit"}};
    for(size_t i=0;i<4;++i) {
        const qa_qc_definition *field=qa_qc_program_find_field(p->state.qc.program,timers[i].field);
        if(!field || field->type!=QA_QC_FLOAT) continue;
        bool declared=!profile;
        for(size_t j=0;profile && j<profile->field_count;++j) declared|=profile->fields[j].definition==field;
        if(!declared) continue;
        double expires;
        if(!scalar(p,slot,actor,timers[i].field,field,&expires,error)) return false;
        value.timers[value.timer_count++]=(qa_application_qc_power_timer){timers[i].item,timers[i].label,expires};
    }
    if(!(selected?selected_ui_basis(app,p,actor,slot):qa_application_qc_message_source_current(app,source)))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC UI source changed during observation");
    *out=value; return true;
}
bool qa_application_qc_message_player_ui_read(qa_application *app,
    const qa_application_qc_message_source *source,qa_actor_id actor,
    qa_application_qc_player_ui *out,qa_error *error)
{
    uint32_t slot;
    return out && qa_application_qc_message_client(app,source,actor,&slot,error) &&
        player_ui_read(app,owner(app,source->provider),source,actor,slot,false,out,error);
}
bool qa_application_qc_selected_player_ui_read(qa_application *app,qa_actor_id actor,qa_launch_role role,
    qa_application_qc_player_ui *out,qa_error *error)
{
    if(!app || !out || role!=QA_ROLE_ARSENAL)
        return application_fail(error,QA_ERROR_ARGUMENT,"QC selected UI requires its actual arsenal role");
    application_provider *p=application_provider_for(app,actor,role,""); uint32_t slot;
    if(!source_returned(p) || !p->product->campaign ||
        !qa_qc_actor_observation_slot(p->state.qc.instance,actor,&slot,error) || !selected_ui_basis(app,p,actor,slot))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC selected UI lost its returned selected actor binding");
    const struct application_qc_state *engine=p->state.qc.engine;
    if(!qa_q1_profile_valid(engine->protocol,error)) return false;
    qa_application_qc_message_source source={.provider=p->owner,.descriptor=p->launch,.program=p->state.qc.program,
        .instance=p->state.qc.instance,.protocol=engine->protocol,.client_slots=engine->max_clients,
        .map_revision=app->map_revision,.options={.standard_quake=strcmp(p->product->campaign,"hipnotic") &&
            strcmp(p->product->campaign,"rogue"),.private_rerelease=engine->profile==QA_QC_RERELEASE}};
    return player_ui_read(app,p,&source,actor,slot,true,out,error);
}
bool qa_application_qc_message_player_ui_current(qa_application *app,const qa_application_qc_player_ui *view)
{
    qa_application_qc_player_ui actual;
    if(!view || !(view->selected_arsenal?
        qa_application_qc_selected_player_ui_read(app,view->recipient,QA_ROLE_ARSENAL,&actual,NULL):
        qa_application_qc_message_player_ui_read(app,&view->source,view->recipient,&actual,NULL)) ||
        actual.source.provider!=view->source.provider || actual.source.descriptor!=view->source.descriptor ||
        actual.source.program!=view->source.program || actual.source.instance!=view->source.instance ||
        actual.source.map_revision!=view->source.map_revision || actual.selected_arsenal!=view->selected_arsenal ||
        actual.source.protocol.kind!=view->source.protocol.kind || actual.source.protocol.flags!=view->source.protocol.flags ||
        actual.source.protocol.revision!=view->source.protocol.revision || actual.source.client_slots!=view->source.client_slots ||
        actual.source.options.standard_quake!=view->source.options.standard_quake ||
        actual.source.options.private_rerelease!=view->source.options.private_rerelease ||
        actual.source_slot!=view->source_slot || actual.items!=view->items || actual.weapon!=view->weapon ||
        actual.current_ammo!=view->current_ammo || actual.now_seconds!=view->now_seconds ||
        actual.binding_count!=view->binding_count || actual.timer_count!=view->timer_count) return false;
    for(size_t i=0;i<actual.timer_count;++i)
        if(actual.timers[i].item!=view->timers[i].item || actual.timers[i].label!=view->timers[i].label ||
            actual.timers[i].expires_seconds!=view->timers[i].expires_seconds) return false;
    return true;
}
bool qa_application_qc_message_player_ui_binding(qa_application *app,const qa_application_qc_player_ui *view,
    size_t ordinal,qa_application_qc_weapon_ui_binding *out,qa_error *error)
{
    if(!out || !qa_application_qc_message_player_ui_current(app,view) || ordinal>=view->binding_count)
        return application_fail(error,QA_ERROR_ARGUMENT,"QC UI binding lost its actual Source client");
    application_provider *p=owner(app,view->source.provider);
    if(p->state.qc.qualified) {
        const application_qc_weapon_value *value=p->state.qc.qualified->weapon_values+ordinal;
        *out=(qa_application_qc_weapon_ui_binding){value->item,value->label,value->bit,value->impulse};
        return true;
    }
    static const struct { qa_q1_weapon weapon; uint32_t bit; int32_t impulse; } original[]={
        {QA_Q1_AXE,4096,1},{QA_Q1_SHOTGUN,1,2},{QA_Q1_SUPER_SHOTGUN,2,3},
        {QA_Q1_NAILGUN,4,4},{QA_Q1_SUPER_NAILGUN,8,5},{QA_Q1_GRENADE,16,6},
        {QA_Q1_ROCKET,32,7},{QA_Q1_LIGHTNING,64,8},{QA_Q1_LASER,8388608,225},
        {QA_Q1_MJOLNIR,128,226},{QA_Q1_PROXIMITY,65536,6}};
    qa_q1_weapon_profile profile;
    if(!qa_q1_weapon_profile_identity(ordinal<8?QA_Q1_ID1:QA_Q1_HIPNOTIC,original[ordinal].weapon,&profile))
        return application_fail(error,QA_ERROR_FORMAT,"QC UI binding has no original SDK identity");
    qa_item_id item=qa_strings_find(qa_session_strings(app->session),
        (qa_bytes){(const uint8_t *)profile.item,strlen(profile.item)});
    if(!item) return application_fail(error,QA_ERROR_NOT_FOUND,"QC UI weapon was not admitted in the actual inventory namespace");
    *out=(qa_application_qc_weapon_ui_binding){item,profile.label,original[ordinal].bit,original[ordinal].impulse};
    return true;
}
