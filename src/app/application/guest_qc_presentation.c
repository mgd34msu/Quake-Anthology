#include "guest_qc_profile.h"
#include "qa/application_qc_presentation.h"
#include "network_q1_signon.h"
#include "qa/qc_observation.h"
#include "unified_q1_events.h"
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
static bool raw_scalar(application_provider *p,uint32_t slot,qa_actor_id actor,
    const qa_qc_definition *field,double *out,qa_error *error)
{
    float value;
    if(!qa_qc_actor_observation_float(p->state.qc.instance,slot,actor,field->offset,&value,error)) return false;
    if(!isfinite(value)) return application_fail(error,QA_ERROR_FORMAT,"QC presentation scalar is nonfinite");
    *out=(double)value; return true;
}
static bool scalar_declared(const application_provider *p,const qa_qc_definition *field)
{
    const struct application_qc_profile *profile=p->state.qc.qualified;
    bool declared=!profile || field==profile->weapon_field;
    for(size_t i=0;profile && i<profile->field_count;++i) declared|=profile->fields[i].definition==field;
    return declared;
}
static bool scalar(application_provider *p,uint32_t slot,qa_actor_id actor,const char *name,
    const qa_qc_definition *explicit_field,double *out,qa_error *error)
{
    const qa_qc_definition *field=explicit_field?explicit_field:qa_qc_program_find_field(p->state.qc.program,name);
    if(!field || field->type!=QA_QC_FLOAT || !scalar_declared(p,field))
        return application_fail(error,QA_ERROR_UNSUPPORTED,"QC animation continuation has no actual declared scalar field");
    return raw_scalar(p,slot,actor,field,out,error);
}
static bool ui_optional_scalar(application_provider *p,uint32_t slot,qa_actor_id actor,
    const char *name,double *out,bool *present,qa_error *error)
{
    const qa_qc_definition *field=qa_qc_program_find_field(p->state.qc.program,name);
    *present=field && scalar_declared(p,field);
    return !*present || scalar(p,slot,actor,name,field,out,error);
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
bool qa_application_qc_selected_character_frame_read(qa_application *app,qa_actor_id actor,
    qa_application_qc_animation *out,qa_error *error)
{
    if(!app || !out || (app->operation!=APPLICATION_IDLE && app->operation!=APPLICATION_ADVANCING) ||
        !qa_actors_get(qa_session_actors(app->session),actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC character frame requires its actual selected actor");
    application_provider *p=application_provider_for(app,actor,QA_ROLE_CHARACTER,""); uint32_t slot;
    if(!source_returned(p) || !qa_qc_actor_observation_slot(p->state.qc.instance,actor,&slot,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC character frame lost its returned selected Source");
    qa_application_qc_animation value={.actor=actor,.provider=p->owner,.descriptor=p->launch,
        .program=p->state.qc.program,.instance=p->state.qc.instance,.role=QA_ROLE_CHARACTER};
    if(!scalar(p,slot,actor,"frame",NULL,&value.frame,error) ||
        application_provider_for(app,actor,QA_ROLE_CHARACTER,"")!=p || !source_returned(p)) return false;
    *out=value; return true;
}
bool qa_application_qc_selected_character_frame_current(qa_application *app,const qa_application_qc_animation *view)
{
    qa_application_qc_animation actual;
    return view && view->role==QA_ROLE_CHARACTER &&
        qa_application_qc_selected_character_frame_read(app,view->actor,&actual,NULL) &&
        actual.provider==view->provider && actual.descriptor==view->descriptor && actual.program==view->program &&
        actual.instance==view->instance && actual.frame==view->frame;
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
bool qa_application_qc_message_model_read(qa_application *app,const qa_application_qc_message_source *view,
    uint32_t index,qa_application_qc_message_model *out,qa_error *error)
{
    if(!out || !index || !qa_application_qc_message_source_current(app,view))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC model lost its current Source precache receipt");
    application_provider *p=owner(app,view->provider);
    const application_qc_resource *found=NULL;
    for(size_t i=0;i<p->state.qc.engine->resource_count;++i) {
        const application_qc_resource *entry=&p->state.qc.engine->resources[i];
        if(entry->kind!=QA_QC_RESOURCE_MODEL || entry->value.index!=index) continue;
        if(found) return application_fail(error,QA_ERROR_FORMAT,"QC MODEL index has ambiguous installed resources");
        found=entry;
    }
    const char *name=found ? qa_strings_cstr(qa_session_strings(p->state.qc.engine->services.session),found->name) : NULL;
    if(!found || !name || !*name || (!found->has_inline_model && !found->source))
        return application_fail(error,QA_ERROR_NOT_FOUND,"QC MODEL index has no retained precache resource");
    *out=(qa_application_qc_message_model){.path=name,.resource=found->source,
        .opening=found->source?&found->acquisition:NULL,.inline_model=found->inline_model,
        .has_inline_model=found->has_inline_model};
    return true;
}
bool qa_application_qc_message_receives(qa_application *app,const qa_application_qc_message_source *view,
    qa_actor_id recipient,const qa_application_protocol_event *event,bool *out,qa_error *error)
{
    uint32_t slot;
    if(!event || !out || !qa_application_qc_message_client(app,view,recipient,&slot,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC routing requires its actual admitted Source recipient");
    *out=false;
    if(event->provider!=view->provider || event->signon)return true;
    bool qw=qa_q1_is_qw(view->protocol);
    if(event->dialect!=(qw?QA_RULESET_QUAKEWORLD:QA_RULESET_NETQUAKE))
        return application_fail(error,QA_ERROR_FORMAT,"QC source message changes its physical dialect");
    if(!event->multicast) {
        if(event->destination<0 || event->destination>3)
            return application_fail(error,QA_ERROR_FORMAT,"QC source message has an invalid destination");
        *out=!event->recipient.registry || qa_actor_id_equal(event->recipient,recipient); return true;
    }
    if(!qw || event->destination<0 || event->destination>5 || !qa_vec_finite(event->origin))
        return application_fail(error,QA_ERROR_FORMAT,"QC source multicast has an invalid destination");
    int32_t mode=event->destination%3;
    if(!mode) { *out=true; return true; }
    const qa_qc_definition *field=qa_qc_program_find_field(view->program,"origin"); qa_vec3 point;
    if(!field || field->type!=QA_QC_VECTOR ||
        !qa_qc_actor_observation_vector(view->instance,slot,recipient,field->offset,&point,error))return false;
    if(!qa_vec_finite(point))return application_fail(error,QA_ERROR_FORMAT,"QC multicast recipient origin is nonfinite");
    qa_vec3 delta=qa_vec_sub(point,event->origin);
    if(mode==1 && qa_vec_dot(delta,delta)<=1024.0f*1024.0f) { *out=true; return true; }
    qa_collision_leaf from,to; qa_collision_geometry *geometry=qa_world_geometry(app->world);
    return qa_collision_point_leaf(geometry,event->origin, QA_LEAF_Q1,&from,error) &&
        qa_collision_point_leaf(geometry,point, QA_LEAF_Q1,&to,error) &&
        qa_collision_cluster_visible(geometry,(int32_t)from.cluster,(int32_t)to.cluster,mode==1,out,error) &&
        qa_application_qc_message_source_current(app,view);
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
    return qa_application_qc_message_client(app,source,actor,&slot,error) &&
        application_control_set_angles(app,actor,angles,error);
}
bool qa_application_qc_message_music(qa_application *app,const qa_application_qc_message_source *source,
    qa_actor_id recipient,uint64_t time_ns,uint8_t track,qa_error *error)
{
    uint32_t slot;
    if(!qa_application_qc_message_source_current(app,source) ||
        (recipient.registry && !qa_application_qc_message_client(app,source,recipient,&slot,error)))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC music cue lost its decoded Source recipient");
    qa_string_id music;
    return qa_strings_intern_cstr(qa_session_strings(app->session),"music",&music,error) &&
        application_unified_q1_event(app,&(qa_builtin_event){.kind=QA_BUILTIN_EFFECT,
            .family=QA_GAME_Q1,.provider=source->provider,.resource=music,
            .time_ns=time_ns,.code=track,.count=track},recipient,error);
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

bool qa_application_qc_client_presentation_read(qa_application *app,qa_actor_owner id,qa_actor_id actor,
    qa_application_qc_client_presentation *out,bool *found,qa_error *error)
{
    if(!app || !id || !out || !found || !qa_actors_get(qa_session_actors(app->session),actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC client presentation requires its actual recipient");
    *found=false;
    application_provider *p=owner(app,id);
    if(!p) return application_fail(error,QA_ERROR_ARGUMENT,"QC client presentation owner is no longer installed");
    const struct application_qc_profile *profile=p->kind==APPLICATION_PROVIDER_QC?p->state.qc.qualified:NULL;
    if(!profile || !profile->presentation.declared) return true;
    if(!source_ready(p) || !p->state.qc.engine->clients)
        return application_fail(error,QA_ERROR_ARGUMENT,"Declared QC client presentation Source is not returned");
    const struct application_qc_state *engine=p->state.qc.engine;
    bool admitted=false;
    for(uint32_t slot=1;slot<=engine->max_clients;++slot)
        admitted|=engine->clients[slot].connected && engine->clients[slot].spawned &&
            qa_actor_id_equal(engine->clients[slot].actor,actor);
    if(!admitted) return true;
    qa_application_qc_client_presentation value={.recipient=actor,
        .vitals=profile->presentation.vitals,.view=profile->presentation.view};
    bool qc=false;
    if(!qa_application_qc_message_source_read(app,id,&value.source,&qc,error) || !qc ||
        !qa_application_qc_message_client(app,&value.source,actor,&value.source_slot,error)) return false;
    if(value.vitals && (!raw_scalar(p,value.source_slot,actor,profile->presentation.health,&value.health,error) ||
        !raw_scalar(p,value.source_slot,actor,profile->presentation.armor,&value.armor,error))) return false;
    if(!qa_application_qc_message_source_current(app,&value.source))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC client presentation changed its actual Source");
    *out=value; *found=true; return true;
}
bool qa_application_qc_client_presentation_current(qa_application *app,
    const qa_application_qc_client_presentation *view)
{
    qa_application_qc_client_presentation actual; bool found=false;
    return view && qa_application_qc_message_source_current(app,&view->source) &&
        qa_application_qc_client_presentation_read(app,view->source.provider,view->recipient,&actual,&found,NULL) && found &&
        actual.source.descriptor==view->source.descriptor && actual.source.program==view->source.program &&
        actual.source.instance==view->source.instance && actual.source.map_revision==view->source.map_revision &&
        actual.source_slot==view->source_slot && actual.vitals==view->vitals && actual.view==view->view &&
        actual.health==view->health && actual.armor==view->armor;
}
bool qa_application_qc_client_presentation_camera(qa_application *app,
    const qa_application_qc_client_presentation *view,qa_actor_id target,bool intermission,
    const qa_vec3 *angles,qa_application_camera_view *out,bool *found,qa_error *error)
{
    if(!out || !found || !qa_application_qc_client_presentation_current(app,view) ||
        (angles && !qa_vec_finite(*angles)))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC declared camera requires its returned client frame");
    *found=false;
    if(!view->view || (!intermission && (!target.registry || qa_actor_id_equal(target,view->recipient)))) return true;
    if(!target.registry) target=view->recipient;
    if(!qa_actors_get(qa_session_actors(app->session),target)) return true;
    application_provider *p=owner(app,view->source.provider);
    const application_qc_client_presentation *declaration=&p->state.qc.qualified->presentation;
    uint32_t target_slot;
    if(!qa_qc_actor_observation_slot(p->state.qc.instance,target,&target_slot,NULL)) return true;
    qa_application_camera_view value={.actor=view->recipient,.cutscene=true};
    if(!qa_qc_actor_observation_vector(p->state.qc.instance,target_slot,target,declaration->origin->offset,&value.origin,error) ||
        !qa_qc_actor_observation_vector(p->state.qc.instance,target_slot,target,declaration->angles->offset,&value.angles,error) ||
        (!intermission && !qa_qc_actor_observation_vector(p->state.qc.instance,view->source_slot,view->recipient,
            declaration->offset->offset,&value.view_offset,error))) return false;
    if(angles) value.angles=*angles;
    value.view_height=value.view_offset.z;
    if(!qa_vec_finite(value.origin) || !qa_vec_finite(value.angles) || !qa_vec_finite(value.view_offset))
        return application_fail(error,QA_ERROR_FORMAT,"QC declared camera contains a nonfinite source vector");
    if(!qa_application_qc_client_presentation_current(app,view))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC declared camera changed its actual recipient");
    *out=value; *found=true; return true;
}

static bool source_word(double value,uint32_t *out,qa_error *error)
{
    if(!isfinite(value))
        return application_fail(error,QA_ERROR_FORMAT,"QC UI item word is nonfinite");
    *out=(uint32_t)qa_source_float_to_i32((float)value);
    return true;
}
static bool hipnotic_ui(const qa_qc_program *program)
{
    qa_qc_program_info actual=qa_qc_program_describe(program);
    return actual.system_crc==5927u && actual.file_crc==10486u &&
        actual.statement_count==36334u && actual.global_count==5980u &&
        actual.field_count==270u && actual.function_count==2785u;
}
static bool selected_ui_basis(qa_application *app,application_provider *p,qa_actor_id actor,uint32_t slot,qa_launch_role role)
{
    uint32_t actual;
    return app && (app->operation==APPLICATION_IDLE || app->operation==APPLICATION_ADVANCING) &&
        source_returned(p) && p->application==app &&
        application_provider_for(app,actor,role,"")==p &&
        qa_actors_get(qa_session_actors(app->session),actor) &&
        qa_qc_actor_observation_slot(p->state.qc.instance,actor,&actual,NULL) && actual==slot;
}
static bool player_ui_read(qa_application *app,application_provider *p,
    const qa_application_qc_message_source *source,qa_actor_id actor,uint32_t slot,qa_launch_role role,
    qa_application_qc_player_ui *out,qa_error *error)
{
    const struct application_qc_profile *profile=p->state.qc.qualified;
    bool selected=role==QA_ROLE_CHARACTER || role==QA_ROLE_ARSENAL;
    bool arsenal=role!=QA_ROLE_CHARACTER;
    if(profile && arsenal) {
        if(!profile->weapon_field || !profile->weapon_count)
            return application_fail(error,QA_ERROR_UNSUPPORTED,"Declared QC UI has no actual source weapon roster");
        for(size_t i=0;i<profile->weapon_count;++i)
            if(!profile->weapon_values[i].ui_declared)
                return application_fail(error,QA_ERROR_UNSUPPORTED,"Declared QC UI requires explicit weapon label, bit and impulse metadata");
    }
    qa_application_qc_player_ui value={.source=*source,.recipient=actor,.source_slot=slot,
        .now_seconds=(double)p->state.qc.engine->source_time_ns/1e9,
        .binding_count=arsenal?(profile?profile->weapon_count:hipnotic_ui(p->state.qc.program)?11:8):0,.selected_role=role};
    double items;
    if(!scalar(p,slot,actor,"items",NULL,&items,error) ||
        !source_word(items,&value.items,error)) return false;
    if(arsenal && (!scalar(p,slot,actor,"weapon",profile?profile->weapon_field:NULL,&value.weapon,error) ||
        !scalar(p,slot,actor,"currentammo",NULL,&value.current_ammo,error))) return false;
    bool present;
    if(!ui_optional_scalar(p,slot,actor,"items2",&items,&present,error) ||
        (present && !source_word(items,&value.items2,error))) return false;
    if(arsenal) {
        static const char *const names[]={"ammo_shells","ammo_nails","ammo_rockets","ammo_cells"};
        double *const counts[]={&value.shells,&value.nails,&value.rockets,&value.cells};
        for(size_t i=0;i<4;++i) {
            if(!ui_optional_scalar(p,slot,actor,names[i],counts[i],&present,error)) return false;
        }
    }
    value.power_items=value.items & (4194304u | 1048576u | 524288u | 2097152u);
    const char *campaign=p->product->campaign;
    value.power_items2=value.items2 & (campaign && !strcmp(campaign,"hipnotic")?6u:
        campaign && !strcmp(campaign,"rogue")?192u:0u);
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
    if(!(selected?selected_ui_basis(app,p,actor,slot,role):qa_application_qc_message_source_current(app,source)))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC UI source changed during observation");
    *out=value; return true;
}
bool qa_application_qc_message_player_ui_read(qa_application *app,
    const qa_application_qc_message_source *source,qa_actor_id actor,
    qa_application_qc_player_ui *out,qa_error *error)
{
    uint32_t slot;
    return out && qa_application_qc_message_client(app,source,actor,&slot,error) &&
        player_ui_read(app,owner(app,source->provider),source,actor,slot,QA_ROLE_COUNT,out,error);
}
bool qa_application_qc_selected_player_ui_read(qa_application *app,qa_actor_id actor,qa_launch_role role,
    qa_application_qc_player_ui *out,qa_error *error)
{
    if(!app || !out || (role!=QA_ROLE_CHARACTER && role!=QA_ROLE_ARSENAL))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC selected UI requires its actual character or arsenal role");
    application_provider *p=application_provider_for(app,actor,role,""); uint32_t slot;
    if(!source_returned(p) || !p->product->campaign ||
        !qa_qc_actor_observation_slot(p->state.qc.instance,actor,&slot,error) || !selected_ui_basis(app,p,actor,slot,role))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC selected UI lost its returned selected actor binding");
    const struct application_qc_state *engine=p->state.qc.engine;
    if(!qa_q1_profile_valid(engine->protocol,error)) return false;
    qa_application_qc_message_source source={.provider=p->owner,.descriptor=p->launch,.program=p->state.qc.program,
        .instance=p->state.qc.instance,.protocol=engine->protocol,.client_slots=engine->max_clients,
        .map_revision=app->map_revision,.options={.standard_quake=strcmp(p->product->campaign,"hipnotic") &&
            strcmp(p->product->campaign,"rogue"),.private_rerelease=engine->profile==QA_QC_RERELEASE}};
    return player_ui_read(app,p,&source,actor,slot,role,out,error);
}
bool qa_application_qc_message_player_ui_current(qa_application *app,const qa_application_qc_player_ui *view)
{
    qa_application_qc_player_ui actual;
    if(!view || !((view->selected_role==QA_ROLE_CHARACTER || view->selected_role==QA_ROLE_ARSENAL)?
        qa_application_qc_selected_player_ui_read(app,view->recipient,view->selected_role,&actual,NULL):
        qa_application_qc_message_player_ui_read(app,&view->source,view->recipient,&actual,NULL)) ||
        actual.source.provider!=view->source.provider || actual.source.descriptor!=view->source.descriptor ||
        actual.source.program!=view->source.program || actual.source.instance!=view->source.instance ||
        actual.source.map_revision!=view->source.map_revision || actual.selected_role!=view->selected_role ||
        actual.source.protocol.kind!=view->source.protocol.kind || actual.source.protocol.flags!=view->source.protocol.flags ||
        actual.source.protocol.revision!=view->source.protocol.revision || actual.source.client_slots!=view->source.client_slots ||
        actual.source.options.standard_quake!=view->source.options.standard_quake ||
        actual.source.options.private_rerelease!=view->source.options.private_rerelease ||
        actual.source_slot!=view->source_slot || actual.items!=view->items || actual.items2!=view->items2 ||
        actual.power_items!=view->power_items || actual.power_items2!=view->power_items2 || actual.weapon!=view->weapon ||
        actual.shells!=view->shells || actual.nails!=view->nails || actual.rockets!=view->rockets || actual.cells!=view->cells ||
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
    uint32_t via=0;
    return application_qc_weapon_binding_at(p->state.qc.engine,ordinal,out,&via,error);
}
static const struct { qa_q1_weapon weapon; uint32_t bit; int32_t impulse; } original_weapons[]={
    {QA_Q1_AXE,4096,1},{QA_Q1_SHOTGUN,1,2},{QA_Q1_SUPER_SHOTGUN,2,3},
    {QA_Q1_NAILGUN,4,4},{QA_Q1_SUPER_NAILGUN,8,5},{QA_Q1_GRENADE,16,6},
    {QA_Q1_ROCKET,32,7},{QA_Q1_LIGHTNING,64,8},{QA_Q1_LASER,8388608,225},
    {QA_Q1_MJOLNIR,128,226},{QA_Q1_PROXIMITY,65536,6}};
bool application_qc_weapon_binding_at(struct application_qc_state *engine,size_t ordinal,
    qa_application_qc_weapon_ui_binding *out,uint32_t *via,qa_error *error)
{
    application_provider *p=engine?engine->provider:NULL;
    if(!p || !out || !via) return false;
    if(p->state.qc.qualified) {
        if(ordinal>=p->state.qc.qualified->weapon_count) return false;
        const application_qc_weapon_value *value=p->state.qc.qualified->weapon_values+ordinal;
        if(!value->ui_declared) return application_fail(error,QA_ERROR_UNSUPPORTED,"QC weapon request has no declared source impulse binding");
        *out=(qa_application_qc_weapon_ui_binding){value->item,value->label,value->bit,value->impulse};
        *via=value->via;
        return true;
    }
    if(ordinal>=(hipnotic_ui(p->state.qc.program)?11u:8u)) return false;
    qa_q1_weapon_profile profile;
    if(!qa_q1_weapon_profile_identity(ordinal<8?QA_Q1_ID1:QA_Q1_HIPNOTIC,original_weapons[ordinal].weapon,&profile))
        return application_fail(error,QA_ERROR_FORMAT,"QC UI binding has no original SDK identity");
    qa_item_id item=qa_strings_find(qa_session_strings(p->application->session),
        (qa_bytes){(const uint8_t *)profile.item,strlen(profile.item)});
    if(!item) return application_fail(error,QA_ERROR_NOT_FOUND,"QC UI weapon was not admitted in the actual inventory namespace");
    *out=(qa_application_qc_weapon_ui_binding){item,profile.label,original_weapons[ordinal].bit,original_weapons[ordinal].impulse};
    *via=ordinal==10?16u:0u;
    return true;
}

bool qa_application_qc_message_player_ui_definition(qa_application *app,const qa_application_qc_player_ui *view,
    size_t ordinal,qa_item_definition *out,qa_error *error)
{
    qa_application_qc_weapon_ui_binding binding;
    if(!out || !qa_application_qc_message_player_ui_binding(app,view,ordinal,&binding,error)) return false;
    application_provider *p=owner(app,view->source.provider);
    qa_item_definition definition;
    if(p->state.qc.qualified) {
        const application_qc_weapon_value *value=p->state.qc.qualified->weapon_values+ordinal;
        if(value->ammo_declared) definition=(qa_item_definition){.item=binding.item,.ammo=value->ammo,
            .owner=p->owner,.label=binding.label,.weapon=true};
        else {
            if(!qa_inventory_source_definition_read(app->inventory,view->recipient,p->owner,binding.item,&definition,error))
                return false;
            if(!definition.weapon)
                return application_fail(error,QA_ERROR_FORMAT,"QC declared weapon does not own a weapon catalog definition");
        }
    } else {
        qa_q1_weapon_profile profile;
        if(!qa_q1_weapon_profile_identity(ordinal<8?QA_Q1_ID1:QA_Q1_HIPNOTIC,original_weapons[ordinal].weapon,&profile))
            return application_fail(error,QA_ERROR_FORMAT,"QC catalog has no actual SDK weapon profile");
        qa_item_id ammo=profile.ammo?qa_strings_find(qa_session_strings(app->session),
            (qa_bytes){(const uint8_t *)profile.ammo,strlen(profile.ammo)}):0;
        if(profile.ammo && !ammo)
            return application_fail(error,QA_ERROR_NOT_FOUND,"QC SDK ammunition was not admitted in its actual namespace");
        definition=(qa_item_definition){.item=binding.item,.ammo=ammo,.owner=p->owner,
            .label=binding.label,.weapon=true};
    }
    if(!qa_application_qc_message_player_ui_current(app,view))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC catalog changed during source observation");
    *out=definition; return true;
}
