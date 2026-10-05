/* Retail snapshot and centity transitions follow cg_snapshot.c and cg_event.c,
 * Copyright (C) 1999-2005 Id Software, Inc., GPL-2.0-or-later. */
#include "remote_snapshots.h"
#include "../../presentation/q3_native/trajectory.h"
#include "qa/network_q3_fields_save.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct remote_snapshot {
    qa_q3_snapshot value;
    qa_q3_entity entities[256];
} remote_snapshot;
struct frontend_remote_snapshots {
    frontend_remote_snapshots_options options;
    q3n_remote_source_view source;
    remote_snapshot snap, next;
    frontend_remote_centity entities[QA_Q3_ENTITIES];
    q3n_entity presentations[QA_Q3_ENTITIES];
    int32_t constructor_message, processed, latest, time, command_sequence;
    uint64_t revision, callback_scope;
    bool has_snap, has_next, this_teleport, next_teleport, busy, in_callback, faulted;
};
static bool fail(qa_error *e, qa_status code, const char *s)
{ qa_error_set(e, code, 0, "%s", s); return false; }
static int32_t word(uint32_t bits)
{ int32_t out; memcpy(&out, &bits, sizeof(out)); return out; }
static qa_vec3 vector(const float v[3]) { return qa_v3(v[0], v[1], v[2]); }
static bool domain(const qa_native_q3_remote_client_basis *a,
    const qa_native_q3_remote_client_basis *b)
{
    const qa_application_q3_client_context *x=&a->client,*y=&b->client;
    return qa_net_client_id_equal(a->connection,b->connection) && a->epoch==b->epoch &&
        a->application==b->application && a->session==b->session && a->map==b->map && a->geometry==b->geometry &&
        a->descriptor && b->descriptor && a->descriptor->storage==b->descriptor->storage &&
        a->content==b->content && a->content_product==b->content_product && a->product==b->product &&
        a->configuration_generation==b->configuration_generation &&
        a->publication_generation==b->publication_generation && a->gamestate==b->gamestate &&
        a->physical_client==b->physical_client && x->session==y->session &&
        x->receiver==y->receiver && x->source_owner==y->source_owner &&
        qa_actor_id_equal(x->source_actor,y->source_actor) && x->seat==y->seat &&
        x->source_client==y->source_client && x->service_owner==y->service_owner &&
        x->frontend_lifetime==y->frontend_lifetime && x->console==y->console && x->cvars==y->cvars &&
        x->source_cvars==y->source_cvars && x->client_time_cvars==y->client_time_cvars &&
        x->client_time_owner==y->client_time_owner &&
        x->native_source==y->native_source;
}
static bool source_current(const frontend_remote_snapshots *s)
{ return q3n_remote_source_current(&s->source); }
static bool history_current(const frontend_remote_snapshots *s,const q3n_remote_source_view *source)
{
    return source->publication.has_snapshot?source->publication.latest_message>=s->latest:
        !s->has_snap && !s->has_next && s->processed==s->constructor_message &&
        s->latest==s->constructor_message;
}
static bool retained_source(const frontend_remote_snapshots *s,q3n_remote_source_view *out)
{
    return q3n_remote_source_read(s->options.source,out,NULL) && domain(&s->source.basis,&out->basis) &&
        out->basis.restart_generation==s->source.basis.restart_generation &&
        out->publication.initial_message==s->source.publication.initial_message &&
        out->publication.initial_command==s->source.publication.initial_command &&
        out->reached_command==s->command_sequence && history_current(s,out);
}
static bool create(const frontend_remote_snapshots_options *o,
    const q3n_remote_source_view *source,bool video,frontend_remote_snapshots **out,qa_error *e)
{
    if(!o || !o->frontend || !o->source || !o->reached || !o->respawn || !o->reset_player || !o->event ||
        !o->transition_player || !o->lagometer || !o->warning || !source || !out || *out ||
        (o->product!=QA_Q3_ARENA && o->product!=QA_Q3_TEAM_ARENA) ||
        source->owner!=o->source || source->basis.application!=qa_frontend_application(o->frontend) ||
        source->basis.product!=o->product || !q3n_remote_source_current(source) ||
        (video && (source->publication.server_message<source->publication.initial_message ||
            (source->publication.has_snapshot && source->publication.server_message<source->publication.latest_message))))
        return fail(e,QA_ERROR_ARGUMENT,"Remote centities require actual Init history and native consumers");
    frontend_remote_snapshots *s=calloc(1,sizeof(*s));
    if(!s) return fail(e,QA_ERROR_MEMORY,"Allocating remote native centities");
    s->options=*o; s->source=*source;
    s->constructor_message=video?source->publication.server_message:source->publication.initial_message;
    s->processed=s->constructor_message;
    for(uint32_t i=0;i<QA_Q3_ENTITIES;++i) s->entities[i].presentation=&s->presentations[i];
    s->latest=video && source->publication.has_snapshot?source->publication.latest_message:s->constructor_message;
    s->revision=1;
    s->command_sequence=source->reached_command;
    *out=s; return true;
}
bool frontend_remote_snapshots_create(const frontend_remote_snapshots_options *o,
    const q3n_remote_source_view *source,frontend_remote_snapshots **out,qa_error *e)
{ return create(o,source,false,out,e); }
bool frontend_remote_snapshots_create_video(const frontend_remote_snapshots_options *o,
    const q3n_remote_source_view *source,frontend_remote_snapshots **out,qa_error *e)
{ return create(o,source,true,out,e); }
bool frontend_remote_snapshots_idle(const frontend_remote_snapshots *s) { return !s || !s->busy; }
q3n_entity *frontend_remote_snapshots_storage(frontend_remote_snapshots *s)
{ return s?s->presentations:NULL; }
bool frontend_remote_snapshots_destroy(frontend_remote_snapshots *s, qa_error *e)
{
    if(!frontend_remote_snapshots_idle(s)) return fail(e,QA_ERROR_ARGUMENT,"Remote centity callbacks must return before retirement");
    free(s); return true;
}
static bool store(remote_snapshot *to,const qa_q3_snapshot *from,qa_q3_product product,qa_error *e)
{
    if(!from || !from->valid || from->player.product!=product || from->player.clientNum<0 ||
        from->player.clientNum>=QA_Q3_ENTITY_NONE || (from->entity_count && !from->entities))
        return fail(e,QA_ERROR_FORMAT,"Remote retail snapshot has invalid native fields");
    size_t count=from->entity_count>256?256:from->entity_count;
    for(size_t i=0;i<count;++i) if(from->entities[i].number<0 || from->entities[i].number>=QA_Q3_ENTITY_NONE)
        return fail(e,QA_ERROR_FORMAT,"Remote retail snapshot has an invalid centity number");
    to->value=*from; to->value.entity_count=count;
    if(count) memcpy(to->entities,from->entities,count*sizeof(*to->entities));
    to->value.entities=to->entities; return true;
}
/* Predictable events consume only this owner's retail PS copy. Transport PS
 * and the selected movement predictor remain independent retained objects. */
static void player_entity(qa_q3_player *p,qa_q3_entity *s)
{
    s->number=p->clientNum; s->eType=p->pmType==2 || p->pmType==5 || p->stats[0]<=-40?10:1;
    s->pos.type=1; memcpy(s->pos.base,p->origin,sizeof(s->pos.base)); memcpy(s->pos.delta,p->velocity,sizeof(s->pos.delta));
    s->apos.type=1; memcpy(s->apos.base,p->viewangles,sizeof(s->apos.base)); s->angles2[1]=(float)p->movementDir;
    s->legsAnim=p->legsAnim; s->torsoAnim=p->torsoAnim; s->clientNum=p->clientNum;
    s->eFlags=p->stats[0]<=0?p->eFlags|1:p->eFlags&~1;
    if(p->externalEvent) { s->event=p->externalEvent; s->eventParm=p->externalEventParm; }
    else if(p->entityEventSequence<p->eventSequence) {
        int32_t oldest=word((uint32_t)p->eventSequence-2u);
        if(p->entityEventSequence<oldest) p->entityEventSequence=oldest;
        uint32_t sequence=(uint32_t)p->entityEventSequence;
        s->event=p->events[sequence&1u]|(int32_t)((sequence&3u)<<8);
        s->eventParm=p->eventParms[sequence&1u]; p->entityEventSequence=word(sequence+1u);
    }
    s->weapon=p->weapon; s->groundEntityNum=p->groundEntityNum; s->powerups=0;
    for(unsigned i=0;i<16;++i) if(p->powerups[i]) s->powerups|=(int32_t)(1u<<i);
    s->loopSound=p->loopSound; s->generic1=p->generic1;
}
static void published(frontend_remote_centity *row,const qa_q3_snapshot *snap)
{
    row->published=true; row->publication_message=snap->message_number;
    row->presentation->physical=(uint32_t)row->current.number;
    row->presentation->loop_stopped=false;
}
static bool callback_begin(frontend_remote_snapshots *s,qa_error *e)
{
    if(s->callback_scope==UINT64_MAX)
        return fail(e,QA_ERROR_ARGUMENT,"Remote snapshot callback scope is exhausted");
    ++s->callback_scope; s->in_callback=true; return true;
}
static bool reset(frontend_remote_snapshots *s,frontend_remote_centity *row,const qa_q3_snapshot *snap,qa_error *e)
{
    if(row->presentation->snapshot_time<word((uint32_t)s->time-300u)) row->presentation->previous_event=0;
    row->presentation->trail_time=snap->server_time;
    row->presentation->lerp_origin=vector(row->current.origin); row->presentation->lerp_angles=vector(row->current.angles);
    if(row->current.eType!=1) return true;
    if(!callback_begin(s,e)) return false;
    bool ok=s->options.reset_player(s->options.context,&s->source,row,e);
    s->in_callback=false; return ok && source_current(s);
}
static bool events(frontend_remote_snapshots *s,frontend_remote_centity *row,qa_error *e)
{
    qa_q3_entity event=row->current;
    if(event.eType>13) {
        if(row->presentation->previous_event) return true;
        row->presentation->previous_event=1;
        if(event.eFlags&16) event.number=event.otherEntityNum;
        event.event=event.eType-13;
    } else {
        if(event.event==row->presentation->previous_event) return true;
        row->presentation->previous_event=event.event;
        if(!(event.event&~0x300)) return true;
    }
    if(!q3n_trajectory(&event.pos,s->snap.value.server_time,&row->presentation->lerp_origin,e)) return false;
    if(!callback_begin(s,e)) return false;
    bool ok=s->options.event(s->options.context,&s->source,row,&event,row->presentation->lerp_origin,s->time,e);
    s->in_callback=false; return ok && source_current(s);
}
static bool commands(frontend_remote_snapshots *s,int32_t sequence,qa_error *e)
{
    while(s->command_sequence<sequence) {
        if(s->command_sequence==INT32_MAX) return fail(e,QA_ERROR_FORMAT,"Remote reliable sequence is exhausted");
        ++s->command_sequence;
        q3n_remote_command reached; q3n_remote_source_view source;
        if(!q3n_remote_source_command(s->options.source,s->command_sequence,&reached,e) ||
            !q3n_remote_source_read(s->options.source,&source,e)) return false;
        if(!domain(&s->source.basis,&source.basis) || !source.publication.gamestate)
            return fail(e,QA_ERROR_ARGUMENT,"Remote reliable execution retired its native receiver");
        if(source.basis.restart_generation!=s->source.basis.restart_generation) s->this_teleport=true;
        s->source=source;
        if(!callback_begin(s,e)) return false;
        bool adopted=s->options.reached(s->options.context,&reached,e);
        s->in_callback=false;
        if(!adopted || !source_current(s)) return false;
    }
    return true;
}
static bool initial(frontend_remote_snapshots *s,qa_error *e)
{
    s->has_snap=true;
    frontend_remote_centity *local=&s->entities[s->snap.value.player.clientNum];
    player_entity(&s->snap.value.player,&local->current); published(local,&s->snap.value);
    if(!commands(s,s->snap.value.server_command_number,e)) return false;
    if(!callback_begin(s,e)) return false;
    bool ok=s->options.respawn(s->options.context,&s->source,e);
    s->in_callback=false;
    if(!ok || !source_current(s)) return false;
    for(size_t i=0;i<s->snap.value.entity_count;++i) {
        const qa_q3_entity *entry=&s->snap.value.entities[i]; frontend_remote_centity *row=&s->entities[entry->number];
        row->current=*entry; published(row,&s->snap.value); row->presentation->valid=true; row->interpolate=false;
        if(!reset(s,row,&s->snap.value,e) || !events(s,row,e)) return false;
    }
    return true;
}
static void set_next(frontend_remote_snapshots *s)
{
    s->has_next=true;
    player_entity(&s->next.value.player,&s->entities[s->next.value.player.clientNum].next);
    s->entities[s->snap.value.player.clientNum].interpolate=true;
    for(size_t i=0;i<s->next.value.entity_count;++i) {
        const qa_q3_entity *entry=&s->next.value.entities[i]; frontend_remote_centity *row=&s->entities[entry->number];
        row->next=*entry; row->interpolate=row->presentation->valid && !((row->current.eFlags^entry->eFlags)&4);
    }
    s->next_teleport=((s->next.value.player.eFlags^s->snap.value.player.eFlags)&4)!=0 ||
        s->next.value.player.clientNum!=s->snap.value.player.clientNum || ((s->next.value.flags^s->snap.value.flags)&4)!=0;
}
static bool transition(frontend_remote_snapshots *s,const frontend_remote_snapshot_settings *settings,qa_error *e)
{
    if(!commands(s,s->next.value.server_command_number,e)) return false;
    qa_q3_player previous=s->snap.value.player;
    for(size_t i=0;i<s->snap.value.entity_count;++i) s->entities[s->snap.value.entities[i].number].presentation->valid=false;
    s->snap=s->next; s->snap.value.entities=s->snap.entities;
    frontend_remote_centity *local=&s->entities[s->snap.value.player.clientNum];
    player_entity(&s->snap.value.player,&local->current); published(local,&s->snap.value); local->interpolate=false;
    for(size_t i=0;i<s->snap.value.entity_count;++i) {
        frontend_remote_centity *row=&s->entities[s->snap.value.entities[i].number];
        row->current=row->next; published(row,&s->snap.value); row->presentation->valid=true;
        if(!row->interpolate && !reset(s,row,&s->snap.value,e)) return false;
        row->interpolate=false;
        if(!events(s,row,e)) return false;
        row->presentation->snapshot_time=s->snap.value.server_time;
    }
    s->has_next=false;
    if((s->snap.value.player.eFlags^previous.eFlags)&4) s->this_teleport=true;
    if(!(settings->demo_playback || (s->snap.value.player.pmFlags&4096) || settings->no_predict || settings->synchronous_clients)) return true;
    if(!callback_begin(s,e)) return false;
    bool ok=s->options.transition_player(s->options.context,&s->source,&s->snap.value.player,&previous,e);
    s->in_callback=false; return ok && source_current(s);
}
static bool warning(frontend_remote_snapshots *s,const char *message,qa_error *e)
{
    if(!callback_begin(s,e)) return false;
    bool ok=s->options.warning(s->options.context,message,e);
    s->in_callback=false; return ok && source_current(s);
}
static bool read_next(frontend_remote_snapshots *s,remote_snapshot *out,bool *present,qa_error *e)
{
    *present=false;
    if((int64_t)s->latest>(int64_t)s->processed+1000) {
        char message[128];
        snprintf(message,sizeof(message),"WARNING: CG_ReadNextSnapshot: way out of range, %d > %d",s->latest,s->processed);
        if(!warning(s,message,e)) return false;
    }
    while(s->processed<s->latest) {
        if(s->processed==INT32_MAX) return fail(e,QA_ERROR_FORMAT,"Remote snapshot sequence is exhausted");
        ++s->processed; const qa_q3_snapshot *snapshot=NULL; int32_t ping=0;
        qa_q3_host_client_services services;
        if(!source_current(s) ||
            !frontend_network_presentation_services(s->options.frontend,&s->source.basis.client,&services,e) ||
            !services.snapshot(services.context,s->processed,&snapshot,&ping,e) || !source_current(s)) return false;
        if(!callback_begin(s,e)) return false;
        bool ok=s->options.lagometer(s->options.context,snapshot,ping,e);
        s->in_callback=false;
        if(!ok || !source_current(s)) return false;
        if(snapshot) {
            if(snapshot->entity_count>256) {
                char message[128];
                snprintf(message,sizeof(message),"CL_GetSnapshot: truncated %zu entities to 256\n",snapshot->entity_count);
                if(!warning(s,message,e)) return false;
            }
            if(!store(out,snapshot,s->options.product,e)) return false;
            *present=true; return true;
        }
    }
    return true;
}
bool frontend_remote_snapshots_process(frontend_remote_snapshots *s,const frontend_remote_snapshot_settings *settings,qa_error *e)
{
    if(!s || !settings || s->busy || s->faulted || s->revision==UINT64_MAX)
        return fail(e,QA_ERROR_ARGUMENT,"Remote snapshots require their returned native callbacks");
    q3n_remote_source_view source;
    if(!q3n_remote_source_read(s->options.source,&source,e)) return false;
    if(!domain(&s->source.basis,&source.basis) ||
        source.publication.initial_message!=s->source.publication.initial_message ||
        source.publication.initial_command!=s->source.publication.initial_command ||
        source.reached_command!=s->command_sequence || !history_current(s,&source))
        return fail(e,QA_ERROR_ARGUMENT,"Remote snapshots lost their actual Init/history domain");
    s->busy=true; s->source=source;
    if(source.publication.has_snapshot) s->latest=source.publication.latest_message;
    s->time=source.publication.presentation_time; ++s->revision;
    bool ok=true;
    while(ok && !s->has_snap) {
        bool found=false; ok=read_next(s,&s->snap,&found,e);
        if(!ok || !found) break;
        if(!(s->snap.value.flags&2)) ok=initial(s,e);
    }
    while(ok && s->has_snap) {
        if(!s->has_next) {
            bool found=false; ok=read_next(s,&s->next,&found,e);
            if(!ok || !found) break;
            set_next(s);
            if(s->next.value.server_time<s->snap.value.server_time) ok=fail(e,QA_ERROR_FORMAT,"CG_ProcessSnapshots: server time went backwards");
        }
        if(!ok || (s->time>=s->snap.value.server_time && s->time<s->next.value.server_time)) break;
        ok=transition(s,settings,e);
    }
    if(ok && s->has_snap && s->time<s->snap.value.server_time) s->time=s->snap.value.server_time;
    if(ok && s->has_next && s->next.value.server_time<=s->time) ok=fail(e,QA_ERROR_FORMAT,"CG_ProcessSnapshots: next snapshot is not in the future");
    if(ok && !source_current(s)) ok=fail(e,QA_ERROR_ARGUMENT,"Remote snapshot callbacks changed their reached source");
    s->faulted=!ok; s->busy=false;
    if(!ok && (!e || e->code==QA_OK)) fail(e,QA_ERROR_ARGUMENT,"Remote snapshot consumer lost its actual reached source");
    return ok;
}
bool frontend_remote_snapshots_read(const frontend_remote_snapshots *s,frontend_remote_snapshots_view *out)
{
    q3n_remote_source_view source;
    if(!s || !out || s->busy || s->faulted || !retained_source(s,&source)) return false;
    *out=(frontend_remote_snapshots_view){s,source,s->has_snap?&s->snap.value:NULL,s->has_next?&s->next.value:NULL,
        s->time,s->processed,s->command_sequence,s->revision,s->this_teleport,s->next_teleport,0}; return true;
}
static bool view_current(const frontend_remote_snapshots *s,const frontend_remote_snapshots_view *v)
{
    return s && v && v->owner==s && !s->faulted && v->revision==s->revision &&
        v->source.owner==s->options.source && q3n_remote_source_current(&v->source) &&
        v->snapshot==(s->has_snap?&s->snap.value:NULL) && v->next_snapshot==(s->has_next?&s->next.value:NULL) &&
        v->time==s->time && v->processed_message==s->processed && v->command_sequence==s->command_sequence &&
        v->this_frame_teleport==s->this_teleport &&
        v->next_frame_teleport==s->next_teleport && domain(&v->source.basis,&s->source.basis) &&
        v->source.basis.restart_generation==s->source.basis.restart_generation &&
        v->source.publication.initial_message==s->source.publication.initial_message &&
        v->source.publication.initial_command==s->source.publication.initial_command &&
        v->source.reached_command==s->command_sequence && history_current(s,&v->source);
}
bool frontend_remote_snapshots_current(const frontend_remote_snapshots *s,const frontend_remote_snapshots_view *v)
{ return s && !s->busy && v && !v->callback_scope && view_current(s,v); }
bool frontend_remote_snapshots_callback_read(const frontend_remote_snapshots *s,
    const q3n_remote_source_view *source,frontend_remote_snapshots_view *out)
{
    if(!s || !source || !out || !s->busy || !s->in_callback || s->faulted) return false;
    frontend_remote_snapshots_view view={s,*source,s->has_snap?&s->snap.value:NULL,s->has_next?&s->next.value:NULL,
        s->time,s->processed,s->command_sequence,s->revision,s->this_teleport,s->next_teleport,s->callback_scope};
    if(!view_current(s,&view)) return false;
    *out=view; return true;
}
bool frontend_remote_snapshots_callback_current(const frontend_remote_snapshots *s,const frontend_remote_snapshots_view *v)
{
    return s && s->busy && s->in_callback && v && v->callback_scope &&
        v->callback_scope==s->callback_scope && view_current(s,v);
}
bool frontend_remote_snapshots_entity(const frontend_remote_snapshots *s,const frontend_remote_snapshots_view *v,
    uint32_t number,const frontend_remote_centity **out,qa_error *e)
{
    if(!out || number>=QA_Q3_ENTITY_NONE || !frontend_remote_snapshots_current(s,v))
        return fail(e,QA_ERROR_ARGUMENT,"Remote centity read lost its exact native frame");
    *out=&s->entities[number]; return true;
}
bool frontend_remote_snapshots_callback_entity(const frontend_remote_snapshots *s,const frontend_remote_snapshots_view *v,
    uint32_t number,const frontend_remote_centity **out,qa_error *e)
{
    if(!out || number>=QA_Q3_ENTITY_NONE || !frontend_remote_snapshots_callback_current(s,v))
        return fail(e,QA_ERROR_ARGUMENT,"Remote centity read lost its entered snapshot callback");
    *out=&s->entities[number]; return true;
}
bool frontend_remote_snapshots_entity_write(frontend_remote_snapshots *s,const frontend_remote_snapshots_view *v,
    uint32_t number,frontend_remote_centity **out,qa_error *e)
{
    if(!out || number>=QA_Q3_ENTITY_NONE ||
        !(frontend_remote_snapshots_current(s,v) || frontend_remote_snapshots_callback_current(s,v)))
        return fail(e,QA_ERROR_ARGUMENT,"Remote centity authoring lost its genuine CGAME frame scope");
    *out=&s->entities[number]; return true;
}
bool frontend_remote_snapshots_entity_trajectory(frontend_remote_snapshots *s,
    const frontend_remote_snapshots_view *v,uint32_t number,int32_t current_before,int32_t next_before,
    int32_t current_after,int32_t next_after,qa_error *e)
{
    frontend_remote_centity *row;
    if(!frontend_remote_snapshots_entity_write(s,v,number,&row,e)) return false;
    if(!row->published || row->current.number!=(int32_t)number ||
        row->current.pos.type!=current_before || row->next.pos.type!=next_before ||
        current_after!=QA_TRAJECTORY_INTERPOLATE || next_after!=QA_TRAJECTORY_INTERPOLATE)
        return fail(e,QA_ERROR_ARGUMENT,"Remote smoothing lost its genuine private trajectory rows");
    row->current.pos.type=current_after; row->next.pos.type=next_after;
    return frontend_remote_snapshots_current(s,v) || frontend_remote_snapshots_callback_current(s,v);
}
bool frontend_remote_snapshots_entity_weapon(frontend_remote_snapshots *s,
    const frontend_remote_snapshots_view *v,uint32_t number,int32_t before,int32_t after,qa_error *e)
{
    frontend_remote_centity *row;
    if(!frontend_remote_snapshots_entity_write(s,v,number,&row,e)) return false;
    if(!row->published || row->current.number!=(int32_t)number || row->current.weapon!=before ||
        after<0 || after>=16)
        return fail(e,QA_ERROR_ARGUMENT,"Remote weapon presentation lost its genuine private entity row");
    row->current.weapon=after;
    return frontend_remote_snapshots_current(s,v) || frontend_remote_snapshots_callback_current(s,v);
}
bool frontend_remote_snapshots_misc_time_read(const frontend_remote_snapshots *s,
    const frontend_network_prediction_source *source,const qa_q3_prediction_scene_entity_view *entity,int32_t *out,qa_error *e)
{
    if(!s || s->busy || s->faulted || !source || !entity || !entity->entity || !out ||
        entity->source_number>=QA_Q3_ENTITY_NONE || !entity->published ||
        !qa_net_client_id_equal(s->source.basis.connection,source->connection) ||
        source->epoch!=s->source.basis.epoch || source->map!=s->source.basis.map ||
        source->geometry!=s->source.basis.geometry ||
        source->receiver.receiver!=s->source.basis.client.receiver ||
        source->receiver.seat!=s->source.basis.client.seat ||
        source->receiver.service_owner!=s->source.basis.client.service_owner ||
        source->receiver.frontend_lifetime!=s->source.basis.client.frontend_lifetime ||
        source->receiver.console!=s->source.basis.client.console || source->receiver.cvars!=s->source.basis.client.cvars ||
        source->restart_generation!=s->source.basis.restart_generation ||
        !source_current(s) ||
        !frontend_network_prediction_entity_current(s->options.frontend,source,entity))
        return fail(e,QA_ERROR_ARGUMENT,"Remote item miscTime lacks its genuine centity publication");
    const frontend_remote_centity *row=&s->entities[entity->source_number];
    if(!row->published || row->publication_message!=entity->publication_message ||
        row->current.number!=(int32_t)entity->source_number || entity->entity->number!=(int32_t)entity->source_number)
        return fail(e,QA_ERROR_ARGUMENT,"Remote item miscTime differs from the retained native centity row");
    *out=row->presentation->misc_time; return true;
}
bool frontend_remote_snapshots_consume_teleport(frontend_remote_snapshots *s,qa_error *e)
{
    q3n_remote_source_view source;
    if(!s || s->busy || s->faulted || !retained_source(s,&source) || s->revision==UINT64_MAX)
        return fail(e,QA_ERROR_ARGUMENT,"Remote teleport consumption requires its completed native frame");
    s->this_teleport=false; ++s->revision; return true;
}
bool frontend_remote_snapshots_mark_teleport(frontend_remote_snapshots *s,qa_error *e)
{
    q3n_remote_source_view source;
    if(!s || s->busy || s->faulted || !retained_source(s,&source) || s->revision==UINT64_MAX)
        return fail(e,QA_ERROR_ARGUMENT,"Remote teleport feedback requires its returned PS-transition frame");
    if(!s->this_teleport) { s->this_teleport=true; ++s->revision; }
    return true;
}
