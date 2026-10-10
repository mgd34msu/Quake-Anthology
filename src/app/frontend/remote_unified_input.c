#include "remote_unified_input_private.h"
#include "remote_unified_private.h"
#include "remote_unified_save.h"
#include "remote_unified_prediction_private.h"
#include "neutral_config.h"
#include "config_store.h"
#include "unified_input_command.h"
#include "qa/unified_frame_player.h"
#include "qa/unified_frame_prediction.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>


static bool fail(qa_error *e,const char *message)
{ return frontend_unified_fail(e,QA_ERROR_ARGUMENT,message); }
static bool command_equal(const qa_command_context *a,const qa_command_context *b)
{
    return a->session==b->session && a->owner==b->owner && a->client==b->client &&
        a->seat==b->seat && a->dialect==b->dialect && a->origin==b->origin &&
        a->direct==b->direct && a->console_text==b->console_text && a->registry==b->registry &&
        a->cvar_view==b->cvar_view &&
        a->generation==b->generation && qa_actor_id_equal(a->actor,b->actor) &&
        ((!a->script && !b->script) || (a->script && b->script && !strcmp(a->script,b->script)));
}
static bool domain_source_matches(const frontend_remote_unified_domain *d,const qa_application_client_source *s)
{
    return d && s->runtime==d->runtime && qa_net_client_id_equal(s->client,d->client) &&
        s->network_seat.owner==d->seat.owner && s->network_seat.index==d->seat.index &&
        s->context.physical_seat==d->physical_seat && s->context.console==d->console &&
        s->context.cvars==d->cvars && command_equal(&s->context.command,&d->command_context);
}
static bool domain_matches(const frontend_remote_unified_domain *d,const frontend_client_source_view *v)
{ return v->ready&&domain_source_matches(d,&v->source); }
static bool current(const frontend_unified_input *p,qa_error *e)
{
    const frontend_remote_unified_domain *d=p?frontend_remote_unified_domain_read(p->replica):NULL;
    frontend_neutral_config_view configuration;
    return p && p->frontend && d && p->frontend->application==d->application &&
        p->recipe==frontend_remote_unified_recipe(p->replica) && p->epoch==frontend_remote_unified_epoch(p->replica) &&
        p->movement==frontend_remote_unified_provider_published(p->replica,QA_ROLE_MOVEMENT,"") &&
        p->arsenal==frontend_remote_unified_provider_published(p->replica,QA_ROLE_ARSENAL,"") &&
        frontend_remote_unified_current(p->replica,e) && frontend_client_source_current(&p->client_view) &&
        domain_matches(d,&p->client_view) && frontend_neutral_config_current(&p->configuration) &&
        frontend_config_store_neutral_read(p->frontend->config_store,d->cvars,&configuration,e) &&
        configuration.owner==p->configuration.owner && configuration.ready && configuration.published &&
        configuration.client==d->cvars && configuration.mouse==p->configuration.mouse &&
        configuration.movement==p->configuration.movement && configuration.kind==p->builder.kind;
}
static bool create(qa_frontend *f,frontend_remote_unified *replica,
    frontend_remote_unified_prediction *prediction,bool importing,frontend_unified_input **out,qa_error *e)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(replica);
    const qa_recipe_provider *movement=frontend_remote_unified_provider_published(replica,QA_ROLE_MOVEMENT,"");
    const qa_recipe_provider *arsenal=frontend_remote_unified_provider_published(replica,QA_ROLE_ARSENAL,"");
    frontend_unified_prediction_view snapshot;
    frontend_neutral_config_view configuration;
    if(!f || !replica || !prediction || prediction->replica!=replica || !out || *out || !d || f->application!=d->application ||
        !(importing ? frontend_remote_unified_checkpoint_current(replica,e) : frontend_remote_unified_current(replica,e)) ||
        !movement || !arsenal || !movement->registered || !arsenal->registered ||
        !movement->selection.instance || !arsenal->selection.instance ||
        !(importing ? frontend_prediction_checkpoint_snapshot(prediction,&snapshot,e) :
            frontend_remote_unified_prediction_snapshot(prediction,&snapshot,e)) ||
        (!importing && !frontend_config_store_neutral_movement_adopt(f->config_store,d->cvars,snapshot.state.kind,e)) ||
        !(importing?frontend_config_store_neutral_checkpoint_read(f->config_store,d->cvars,&configuration,e):
            frontend_config_store_neutral_read(f->config_store,d->cvars,&configuration,e)) ||
        !configuration.ready || !configuration.published || configuration.client!=d->cvars ||
        configuration.physical_seat!=d->physical_seat || configuration.kind!=snapshot.state.kind)
        return fail(e,"Unified input lacks its actual completed CLIENT settings and prediction baseline");
    frontend_client_source *client=NULL;
    frontend_client_source_view selected={0};
    for(size_t i=0;i<frontend_client_source_count(f);++i) {
        frontend_client_source *candidate=frontend_client_source_at(f,i);
        frontend_client_source_view view;
        if(!frontend_client_source_metadata_read(candidate,&view,e)) return false;
        if(view.source.context.cvars!=d->cvars) continue;
        if(client || (!importing && !frontend_client_source_read(candidate,&view,e)) || !domain_matches(d,&view))
            return fail(e,"Unified input changed its unique physical CLIENT constructor");
        client=candidate;selected=view;
    }
    if(!client || !(importing?frontend_neutral_config_checkpoint_current(&configuration,e):
        frontend_neutral_config_current(&configuration)))
        return fail(e,"Unified input lacks its true selected provider and CLIENT ownership");
    frontend_unified_input *p=calloc(1,sizeof(*p));
    if(!p) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Allocating retained Unified physical input");
    if(!(importing?frontend_client_source_checkpoint_retain(client,&selected.source,e):
        frontend_client_source_retain(client,e))) {free(p);return false;}
    p->frontend=f;p->replica=replica;p->prediction=prediction;p->client=client;p->client_view=selected;
    p->configuration=configuration;p->recipe=frontend_remote_unified_recipe(replica);
    p->movement=movement;p->arsenal=arsenal;p->epoch=frontend_remote_unified_epoch(replica);
    p->builder=(frontend_unified_command_builder){.kind=snapshot.state.kind,
        .angles={(float)snapshot.view_angles.x,(float)snapshot.view_angles.y,(float)snapshot.view_angles.z}};
    p->command_time=snapshot.command_time_ms;
    *out=p;return true;
}
bool frontend_unified_input_create(qa_frontend *f,frontend_remote_unified *replica,
    frontend_remote_unified_prediction *prediction,frontend_unified_input **out,qa_error *e)
{ return create(f,replica,prediction,false,out,e); }
bool frontend_input_import_create(qa_frontend *f,frontend_remote_unified *replica,
    frontend_remote_unified_prediction *prediction,frontend_unified_input **out,qa_error *e)
{ return frontend_remote_unified_restore_pending(replica) && create(f,replica,prediction,true,out,e); }
static bool frame_read(frontend_unified_input *p,double time,
    const frontend_unified_prediction_view *snapshot,frontend_unified_command_frame *out,qa_error *e)
{
    const qa_unified_document *doc=frontend_remote_unified_frame(p->replica);
    const qa_unified_document *prediction=frontend_remote_unified_prediction_document(p->prediction);
    const qa_unified_frame *received=qa_unified_document_frame(doc);
    const qa_unified_frame *predicted=qa_unified_document_frame(prediction);
    if(!received || !received->world || !received->player || !predicted || !predicted->prediction)
        return fail(e,"Unified input lost its actual received frame");
    const qa_unified_player_view *view=&received->player->view;
    frontend_unified_command_frame frame={.kind=snapshot->state.kind,
        .acknowledged_server_seconds=snapshot->command_time_ms/1000,.server_time_ms=trunc(time),
        .weapon=2,.sensitivity=1,.light_level=128,.attack_allowed=true};
    if(frame.kind==QA_RULESET_Q2_RERELEASE)
        frame.server_frame=(double)received->world->source.number;
    if(view->has_pitch_drift) {
        frame.has_pitch_drift=true;
        frame.grounded=view->grounded;
        frame.drift_disabled=view->pitch_drift_disabled;
        frame.ideal_pitch=(float)view->ideal_pitch;
    }
    if(frame.kind==QA_RULESET_Q3) {
        const qa_unified_weapon_state *weapon=&predicted->prediction->weapon;
        if(weapon->kind==QA_GAME_Q3) frame.weapon=weapon->source_weapon;
        if(p->has_q3_values) { frame.weapon=p->q3_weapon; frame.sensitivity=p->q3_sensitivity; }
    }
    *out=frame;return true;
}
static bool submit_pending(frontend_unified_input *p,qa_error *e)
{
    if(!current(p,e) || !frontend_remote_unified_submit(p->replica,&p->pending,p->pending_time,e)) return false;
    p->builder=p->pending_builder;p->command_time=p->pending_time;
    p->last_sequence=p->pending.sequence;p->submitted=true;
    p->has_pending=false;p->has_sample=false;
    return true;
}
static bool prepare_sample(frontend_unified_input *,qa_error *);
bool frontend_unified_input_retry(frontend_unified_input *p,bool *completed,uint64_t *sequence,qa_error *e)
{
    if(!p || p->busy || !completed || !sequence || !current(p,e)) return false;
    *completed=false;
    if(!p->has_sample) {
        frontend_unified_prediction_view snapshot;
        if(!frontend_remote_unified_prediction_snapshot(p->prediction,&snapshot,e)) return false;
        if(snapshot.sequence>0) {*sequence=(uint64_t)snapshot.sequence;*completed=true;}
        return true;
    }
    p->busy=true;
    uint64_t retained=p->retained_sequence;
    bool ok=(p->has_pending || prepare_sample(p,e)) && submit_pending(p,e);
    if(ok) {*sequence=retained;*completed=true;}
    p->busy=false;return ok;
}
static bool prepare_sample(frontend_unified_input *p,qa_error *e)
{
    const qa_seat_input_sample *sample=&p->retained_sample;
    frontend_unified_prediction_view snapshot;
    qa_input_command_tuning tuning;
    if(!frontend_remote_unified_prediction_snapshot(p->prediction,&snapshot,e) ||
        !qa_input_settings_read(p->configuration.mouse,p->configuration.movement,p->configuration.input_tuning,p->builder.kind,&tuning,e)) return false;
    bool acknowledged=p->submitted && snapshot.sequence>=0 && (uint64_t)snapshot.sequence>=p->last_sequence;
    double baseline=snapshot.command_time_ms;
    double duration=p->builder.kind==QA_RULESET_NETQUAKE || p->builder.kind==QA_RULESET_Q3?
        p->retained_elapsed:trunc(p->retained_elapsed>250?100:p->retained_elapsed);
    double time=fmax(acknowledged?baseline:p->command_time,baseline)+duration;
    frontend_unified_command_frame frame;
    frontend_unified_command_builder next=p->builder;
    qa_unified_input input={.sequence=p->retained_sequence};
    if(!isfinite(time) || !frame_read(p,time,&snapshot,&frame,e) ||
        !frontend_unified_command_build(&next,&tuning,sample,&frame,p->retained_elapsed,&input.command,e) ||
        !current(p,e)) return false;
    input.has_arsenal=true;
    input.arsenal=(qa_usercmd_arsenal){
        .provider={(const unsigned char *)p->arsenal->selection.instance,strlen(p->arsenal->selection.instance)},
        .has_impulse=sample->impulse!=0,.impulse=sample->impulse,
        .use_holdable=sample->game_focus &&
            (sample->buttons[QA_INPUT_USE].active || sample->buttons[QA_INPUT_USE].pressed ||
             sample->buttons[QA_INPUT_BUTTON2].active || sample->buttons[QA_INPUT_BUTTON2].pressed)};
    if(input.command.kind==QA_RULESET_Q3) {
        qa_usercmd actual;
        if(!qa_application_control_project_unified(&input.command,&snapshot.state,input.sequence,&actual,e)) return false;
        if(p->q3_command_count==64) {
            memmove(p->q3_commands,p->q3_commands+1,63*sizeof(*p->q3_commands));
            --p->q3_command_count;
        }
        p->q3_commands[p->q3_command_count++]=actual;
    }
    p->pending=input;p->pending_time=time;p->pending_builder=next;p->has_pending=true;
    return true;
}
bool frontend_unified_input_build(frontend_unified_input *p,const qa_seat_input_sample *sample,
    uint64_t sequence,double elapsed,qa_error *e)
{
    if(!p || p->busy || p->has_sample || !sample || sequence>QA_UNIFIED_SAFE_INTEGER ||
        !isfinite(elapsed) || elapsed<0 || !current(p,e))
        return fail(e,"Unified input must settle its retained command before another sample");
    p->retained_sample=*sample;p->retained_sequence=sequence;p->retained_elapsed=elapsed;p->has_sample=true;
    p->busy=true;
    bool ok=prepare_sample(p,e) && submit_pending(p,e);
    p->busy=false;return ok;
}
bool frontend_unified_input_idle(const frontend_unified_input *p)
{ return !p || (!p->busy && frontend_client_source_idle(p->client)); }
bool frontend_unified_input_pending(const frontend_unified_input *p)
{ return p&&(p->has_sample||p->has_pending); }
bool frontend_unified_input_command_values(frontend_unified_input *p,int32_t weapon,float sensitivity,qa_error *e)
{
    if(!p||p->busy||!isfinite(sensitivity)||!current(p,e))
        return fail(e,"Unified Q3 command values require the returned actual input owner");
    if(p->has_sample&&(!p->has_q3_values||p->q3_weapon!=weapon||p->q3_sensitivity!=sensitivity))
        return fail(e,"Unified command values cannot change its retained sampled prefix");
    p->q3_weapon=weapon; p->q3_sensitivity=sensitivity; p->has_q3_values=true; return true;
}
bool frontend_unified_input_oldest_q3(const frontend_unified_input *p,qa_usercmd *out,bool *available,qa_error *e)
{
    if(!p||!out||!available||p->busy||!current(p,e))
        return fail(e,"Q3 command history requires its actual returned input owner");
    *available=false;
    if(p->builder.kind==QA_RULESET_Q3&&p->q3_command_count) {
        uint64_t latest=p->q3_commands[p->q3_command_count-1].sequence;
        if(latest>=63) for(size_t i=0;i<p->q3_command_count;++i) if(p->q3_commands[i].sequence==latest-63) {
            *available=true;*out=p->q3_commands[i];break;
        }
    }
    return true;
}
bool frontend_unified_input_destroy(frontend_unified_input **owned,qa_error *e)
{
    if(!owned || !*owned) return true;
    frontend_unified_input *p=*owned;
    if(!frontend_unified_input_idle(p) || !frontend_client_source_release(p->client,e)) return false;
    free(p);*owned=NULL;return true;
}
static bool physical(qa_frontend *f,uint32_t ordinal,frontend_remote_unified **out,qa_error *e)
{
    *out=NULL;
    if(!f || ordinal>=f->options.seats) return fail(e,"Unified physical input seat is absent");
    for(frontend_remote_unified *p=f->remote_unified;p;p=p->next) {
        const frontend_remote_unified_domain *d=&p->options.domain;
        if(d->physical_seat!=ordinal) continue;
        if(*out) return fail(e,"Two Unified CLIENT owners claim one physical input seat");
        if(p->retired) {
            qa_application_client_source source;
            if(!d->command_context.owner||d->command_context.owner>UINT32_MAX)
                return fail(e,"Retired Unified input has no representable physical CLIENT receiver");
            if(!qa_application_client_physical_read(f->application,(qa_actor_owner)d->command_context.owner,
                d->command_context.seat,&source,e)||!domain_source_matches(d,&source)||
                !qa_application_client_retirement_current(f->application,&source)||
                !p->options.current(p->options.context,d,e))
                return fail(e,"Retired Unified input lost its actual physical CLIENT custody");
        } else if(!frontend_remote_unified_current(p,e)) return false;
        *out=p;
    }
    return true;
}
bool frontend_remote_unified_input_prepare(qa_frontend *f,uint32_t ordinal,uint64_t *sequence,
    bool *owned,bool *needed,qa_error *e)
{
    if(!sequence || !owned || !needed) return false;
    frontend_remote_unified *p;
    if(!physical(f,ordinal,&p,e)) return false;
    *owned=p!=NULL;*needed=false;
    if(!p) return true;
    if(p->retired) return true;
    if(*sequence>=QA_UNIFIED_SAFE_INTEGER) return fail(e,"Unified physical input sequence exceeds the wire domain");
    if(!p->options.consumers.physical_ready) return fail(e,"Unified CLIENT has no retained physical input owner");
    uint64_t completed=0;
    if(!p->options.consumers.physical_ready(p->options.consumers.context,p,&completed,needed,e)) return false;
    if(completed) {
        if(completed>*sequence) *sequence=completed;
    }
    return true;
}
bool frontend_remote_unified_input(qa_frontend *f,uint32_t ordinal,const qa_seat_input_sample *sample,
    uint64_t sequence,double elapsed,bool *handled,qa_error *e)
{
    if(!sample || !handled) return false;
    frontend_remote_unified *p;
    if(!physical(f,ordinal,&p,e)) return false;
    *handled=p!=NULL;
    if(!p) return true;
    if(p->retired) return true;
    return p->options.consumers.physical_input &&
        p->options.consumers.physical_input(p->options.consumers.context,p,sample,sequence,elapsed,e);
}
