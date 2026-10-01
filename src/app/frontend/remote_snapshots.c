/* Retail snapshot and centity transitions follow cg_snapshot.c and cg_event.c,
 * Copyright (C) 1999-2005 Id Software, Inc., GPL-2.0-or-later. */
#include "remote_snapshots.h"
#include "../../presentation/q3_native/trajectory.h"
#include "../../presentation/q3_native/entity_save.h"
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
    frontend_network_presentation_source source;
    remote_snapshot snap, next;
    frontend_remote_centity entities[QA_Q3_ENTITIES];
    q3n_entity presentations[QA_Q3_ENTITIES];
    int32_t processed, latest, time, command_sequence;
    uint64_t revision;
    bool has_snap, has_next, this_teleport, next_teleport, busy, faulted;
};
static bool fail(qa_error *e, qa_status code, const char *s)
{ qa_error_set(e, code, 0, "%s", s); return false; }
static int32_t word(uint32_t bits)
{ int32_t out; memcpy(&out, &bits, sizeof(out)); return out; }
static qa_vec3 vector(const float v[3]) { return qa_v3(v[0], v[1], v[2]); }
static bool domain(const frontend_network_prediction_source *a,
    const frontend_network_prediction_source *b)
{
    const qa_application_q3_client_context *x=&a->receiver,*y=&b->receiver;
    return qa_net_client_id_equal(a->connection,b->connection) && a->epoch==b->epoch &&
        a->map==b->map && a->geometry==b->geometry && x->session==y->session &&
        x->receiver==y->receiver && x->seat==y->seat && x->service_owner==y->service_owner &&
        x->frontend_lifetime==y->frontend_lifetime && x->console==y->console && x->cvars==y->cvars &&
        x->native_source==y->native_source;
}
static bool source_current(const frontend_remote_snapshots *s)
{ return frontend_network_presentation_source_current(s->options.frontend,&s->source); }
static bool retained_source(const frontend_remote_snapshots *s,frontend_network_presentation_source *out)
{
    bool present=false;
    return frontend_network_presentation_source_read(s->options.frontend,out,&present,NULL) && present &&
        domain(&s->source.prediction,&out->prediction) &&
        out->prediction.restart_generation==s->source.prediction.restart_generation &&
        out->initial_message==s->source.initial_message && out->initial_command==s->source.initial_command &&
        out->executed_command==s->source.executed_command && out->latest_message>=s->latest;
}
bool frontend_remote_snapshots_create(const frontend_remote_snapshots_options *o,
    const frontend_network_presentation_source *source, frontend_remote_snapshots **out, qa_error *e)
{
    if(!o || !o->frontend || !o->command || !o->respawn || !o->reset_player || !o->event ||
        !o->transition_player || !o->lagometer || !o->warning || !source || !out || *out ||
        (o->product!=QA_Q3_ARENA && o->product!=QA_Q3_TEAM_ARENA) ||
        !frontend_network_presentation_source_current(o->frontend,source))
        return fail(e,QA_ERROR_ARGUMENT,"Remote centities require actual Init history and native consumers");
    frontend_remote_snapshots *s=calloc(1,sizeof(*s));
    if(!s) return fail(e,QA_ERROR_MEMORY,"Allocating remote native centities");
    s->options=*o; s->source=*source; s->processed=source->initial_message;
    for(uint32_t i=0;i<QA_Q3_ENTITIES;++i) s->entities[i].presentation=&s->presentations[i];
    s->latest=source->initial_message; s->revision=1;
    s->command_sequence=source->initial_command;
    *out=s; return true;
}
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
{ row->published=true; row->publication_message=snap->message_number; row->presentation->physical=(uint32_t)row->current.number; }
static bool reset(frontend_remote_snapshots *s,frontend_remote_centity *row,const qa_q3_snapshot *snap,qa_error *e)
{
    if(row->presentation->snapshot_time<word((uint32_t)s->time-300u)) row->presentation->previous_event=0;
    row->presentation->trail_time=snap->server_time;
    row->presentation->lerp_origin=vector(row->current.origin); row->presentation->lerp_angles=vector(row->current.angles);
    return row->current.eType!=1 || (s->options.reset_player(s->options.context,&s->source,row,e) && source_current(s));
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
    return s->options.event(s->options.context,&s->source,row,&event,row->presentation->lerp_origin,s->time,e) && source_current(s);
}
static bool commands(frontend_remote_snapshots *s,int32_t sequence,qa_error *e)
{
    while(s->command_sequence<sequence) {
        if(s->command_sequence==INT32_MAX) return fail(e,QA_ERROR_FORMAT,"Remote reliable sequence is exhausted");
        ++s->command_sequence;
        frontend_network_presentation_command reached;
        if(!frontend_network_presentation_execute(s->options.frontend,&s->source,s->command_sequence,&reached,e)) return false;
        if(!domain(&s->source.prediction,&reached.source.prediction) || !reached.source.gamestate)
            return fail(e,QA_ERROR_ARGUMENT,"Remote reliable execution retired its native receiver");
        if(reached.source.prediction.restart_generation!=s->source.prediction.restart_generation) s->this_teleport=true;
        s->source=reached.source;
        if(reached.present && (!s->options.command(s->options.context,&reached,e) || !source_current(s))) return false;
    }
    return true;
}
static bool initial(frontend_remote_snapshots *s,qa_error *e)
{
    s->has_snap=true;
    frontend_remote_centity *local=&s->entities[s->snap.value.player.clientNum];
    player_entity(&s->snap.value.player,&local->current); published(local,&s->snap.value);
    if(!commands(s,s->snap.value.server_command_number,e) ||
        !s->options.respawn(s->options.context,&s->source,e) || !source_current(s)) return false;
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
    return !(settings->demo_playback || (s->snap.value.player.pmFlags&4096) || settings->no_predict || settings->synchronous_clients) ||
        (s->options.transition_player(s->options.context,&s->source,&s->snap.value.player,&previous,e) && source_current(s));
}
static bool read_next(frontend_remote_snapshots *s,remote_snapshot *out,bool *present,qa_error *e)
{
    *present=false;
    if((int64_t)s->latest>(int64_t)s->processed+1000) {
        char message[128];
        snprintf(message,sizeof(message),"WARNING: CG_ReadNextSnapshot: way out of range, %d > %d",s->latest,s->processed);
        if(!s->options.warning(s->options.context,message,e) || !source_current(s)) return false;
    }
    while(s->processed<s->latest) {
        if(s->processed==INT32_MAX) return fail(e,QA_ERROR_FORMAT,"Remote snapshot sequence is exhausted");
        ++s->processed; const qa_q3_snapshot *snapshot=NULL; int32_t ping=0;
        if(!frontend_network_presentation_snapshot(s->options.frontend,&s->source,s->processed,&snapshot,&ping,e) ||
            !s->options.lagometer(s->options.context,snapshot,ping,e) || !source_current(s)) return false;
        if(snapshot) {
            if(snapshot->entity_count>256) {
                char message[128];
                snprintf(message,sizeof(message),"CL_GetSnapshot: truncated %zu entities to 256\n",snapshot->entity_count);
                if(!s->options.warning(s->options.context,message,e) || !source_current(s)) return false;
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
    frontend_network_presentation_source source; bool present=false;
    if(!frontend_network_presentation_source_read(s->options.frontend,&source,&present,e)) return false;
    if(!present || !domain(&s->source.prediction,&source.prediction) || source.initial_message!=s->source.initial_message ||
        source.initial_command!=s->source.initial_command || source.latest_message<s->latest)
        return fail(e,QA_ERROR_ARGUMENT,"Remote snapshots lost their actual Init/history domain");
    s->busy=true; s->source=source; s->latest=source.latest_message; s->time=source.presentation_time; ++s->revision;
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
    frontend_network_presentation_source source;
    if(!s || !out || s->busy || s->faulted || !retained_source(s,&source)) return false;
    *out=(frontend_remote_snapshots_view){s,source,s->has_snap?&s->snap.value:NULL,s->has_next?&s->next.value:NULL,
        s->time,s->processed,s->command_sequence,s->revision,s->this_teleport,s->next_teleport}; return true;
}
bool frontend_remote_snapshots_current(const frontend_remote_snapshots *s,const frontend_remote_snapshots_view *v)
{
    return s && v && v->owner==s && !s->busy && !s->faulted && v->revision==s->revision &&
        frontend_network_presentation_source_current(s->options.frontend,&v->source) &&
        v->snapshot==(s->has_snap?&s->snap.value:NULL) && v->next_snapshot==(s->has_next?&s->next.value:NULL) &&
        v->time==s->time && v->processed_message==s->processed && v->command_sequence==s->command_sequence &&
        v->this_frame_teleport==s->this_teleport &&
        v->next_frame_teleport==s->next_teleport && domain(&v->source.prediction,&s->source.prediction) &&
        v->source.prediction.restart_generation==s->source.prediction.restart_generation &&
        v->source.initial_message==s->source.initial_message && v->source.initial_command==s->source.initial_command &&
        v->source.executed_command==s->source.executed_command && v->source.latest_message>=s->latest;
}
bool frontend_remote_snapshots_entity(const frontend_remote_snapshots *s,const frontend_remote_snapshots_view *v,
    uint32_t number,const frontend_remote_centity **out,qa_error *e)
{
    if(!out || number>=QA_Q3_ENTITY_NONE || !frontend_remote_snapshots_current(s,v))
        return fail(e,QA_ERROR_ARGUMENT,"Remote centity read lost its exact native frame");
    *out=&s->entities[number]; return true;
}
bool frontend_remote_snapshots_misc_time_read(const frontend_remote_snapshots *s,
    const frontend_network_prediction_source *source,const qa_q3_prediction_scene_entity_view *entity,int32_t *out,qa_error *e)
{
    if(!s || s->busy || s->faulted || !source || !entity || !entity->entity || !out ||
        entity->source_number>=QA_Q3_ENTITY_NONE || !entity->published ||
        !domain(&s->source.prediction,source) ||
        source->restart_generation!=s->source.prediction.restart_generation ||
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
    frontend_network_presentation_source source;
    if(!s || s->busy || s->faulted || !retained_source(s,&source) || s->revision==UINT64_MAX)
        return fail(e,QA_ERROR_ARGUMENT,"Remote teleport consumption requires its completed native frame");
    s->this_teleport=false; ++s->revision; return true;
}
static bool entity_fields(qa_source_save_io *io,qa_q3_entity *entity)
{
    uint8_t bytes[sizeof(qa_q3_player)+sizeof(qa_q3_entity)];
    size_t size=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE) {
        qa_net_writer writer; qa_net_writer_init(&writer,bytes,sizeof(bytes),io->error);
        if(!qa_q3_save_entity_fields(&writer,entity)) return false;
        size=qa_net_writer_size(&writer);
    }
    if(!qa_source_save_count(io,&size,sizeof(bytes)) || !qa_source_save_bytes(io,bytes,size)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        qa_net_reader reader; qa_net_reader_init(&reader,(qa_bytes){bytes,size},io->error);
        return qa_q3_restore_entity_fields(&reader,entity) && qa_net_reader_finish(&reader);
    }
    return true;
}
static bool player_fields(qa_source_save_io *io,qa_q3_player *player,qa_q3_product product)
{
    uint8_t bytes[sizeof(qa_q3_player)+sizeof(qa_q3_entity)];
    size_t size=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE) {
        qa_net_writer writer; qa_net_writer_init(&writer,bytes,sizeof(bytes),io->error);
        if(!qa_q3_save_player_fields(&writer,player)) return false;
        size=qa_net_writer_size(&writer);
    }
    if(!qa_source_save_count(io,&size,sizeof(bytes)) || !qa_source_save_bytes(io,bytes,size)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        qa_net_reader reader; qa_net_reader_init(&reader,(qa_bytes){bytes,size},io->error);
        return qa_q3_restore_player_fields(&reader,player,product) && qa_net_reader_finish(&reader);
    }
    return player->product==product;
}
static bool snapshot_fields(qa_source_save_io *io,remote_snapshot *snap,qa_q3_product product)
{
    qa_q3_snapshot *v=&snap->value;
    if(!qa_source_save_bool(io,&v->valid) || !v->valid || !qa_source_save_i32(io,&v->message_number) ||
        !qa_source_save_i32(io,&v->server_time) || !qa_source_save_i32(io,&v->delta_number) ||
        !qa_source_save_i32(io,&v->server_command_number) || !qa_source_save_u64(io,&v->parse_entities_number) ||
        !qa_source_save_u8(io,&v->flags) || !qa_source_save_u8(io,&v->area_bytes) || v->area_bytes>32 ||
        !qa_source_save_bytes(io,v->area_mask,sizeof(v->area_mask)) || !player_fields(io,&v->player,product) ||
        v->player.clientNum<0 || v->player.clientNum>=QA_Q3_ENTITY_NONE ||
        !qa_source_save_count(io,&v->entity_count,256)) return false;
    v->entities=snap->entities;
    for(size_t i=0;i<v->entity_count;++i)
        if(!entity_fields(io,&snap->entities[i]) || snap->entities[i].number<0 ||
            snap->entities[i].number>=QA_Q3_ENTITY_NONE) return false;
    return true;
}
static bool same_u64(qa_source_save_io *io,uint64_t expected)
{ uint64_t value=expected; return qa_source_save_u64(io,&value) && value==expected; }
static bool same_u32(qa_source_save_io *io,uint32_t expected)
{ uint32_t value=expected; return qa_source_save_u32(io,&value) && value==expected; }
static bool same_i32(qa_source_save_io *io,int32_t expected)
{ int32_t value=expected; return qa_source_save_i32(io,&value) && value==expected; }
static bool map_fields(qa_source_save_io *io,const qa_resource *map)
{
    const qa_sha256_digest *expected=qa_resource_digest(map);
    const char *path=qa_resource_path(map);
    if(!expected || !path) return false;
    qa_sha256_digest digest=*expected;
    size_t size=strlen(path),saved=size;
    if(!qa_source_save_bytes(io,digest.bytes,sizeof(digest.bytes)) || !qa_sha256_equal(&digest,expected) ||
        !qa_source_save_count(io,&saved,SIZE_MAX) || saved!=size) return false;
    for(size_t i=0;i<size;++i) {
        uint8_t value=(uint8_t)path[i];
        if(!qa_source_save_u8(io,&value) || value!=(uint8_t)path[i]) return false;
    }
    return true;
}
static bool codec(qa_source_save_io *io,frontend_remote_snapshots *s)
{
    uint8_t magic[4]={'Q','R','S','P'};
    const frontend_network_prediction_source *source=&s->source.prediction;
    if(!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QRSP",sizeof(magic)) ||
        !same_u32(io,1) || !same_u32(io,(uint32_t)s->options.product) ||
        !same_u64(io,source->connection.owner) || !same_u64(io,source->connection.generation) ||
        !same_u32(io,source->connection.slot) || !same_u64(io,source->epoch) ||
        !same_u64(io,source->restart_generation) || !same_u64(io,source->receiver.receiver) ||
        !same_u64(io,source->receiver.service_owner) || !same_u32(io,source->receiver.seat) ||
        !same_i32(io,s->source.initial_message) || !same_i32(io,s->source.initial_command) ||
        !same_i32(io,s->source.executed_command) || !map_fields(io,source->map) ||
        !qa_source_save_i32(io,&s->processed) || !qa_source_save_i32(io,&s->latest) ||
        !qa_source_save_i32(io,&s->time) || !qa_source_save_i32(io,&s->command_sequence) ||
        s->command_sequence<s->source.initial_command || s->command_sequence>s->source.received_command ||
        !qa_source_save_u64(io,&s->revision) || !s->revision ||
        s->processed<s->source.initial_message || s->processed>s->latest || s->latest>s->source.latest_message ||
        !qa_source_save_bool(io,&s->has_snap) || !qa_source_save_bool(io,&s->has_next) ||
        (s->has_next && !s->has_snap) || !qa_source_save_bool(io,&s->this_teleport) ||
        !qa_source_save_bool(io,&s->next_teleport)) return false;
    if(s->has_snap && (!snapshot_fields(io,&s->snap,s->options.product) || s->snap.value.message_number>s->processed ||
        s->snap.value.server_time>s->time)) return false;
    if(s->has_next && (!snapshot_fields(io,&s->next,s->options.product) ||
        s->next.value.message_number>s->processed || s->next.value.message_number<=s->snap.value.message_number ||
        s->next.value.server_time<=s->time)) return false;
    for(uint32_t i=0;i<QA_Q3_ENTITIES;++i) {
        frontend_remote_centity *row=&s->entities[i];
        if(!qa_source_save_bool(io,&row->published) || !qa_source_save_bool(io,&row->interpolate) ||
            !qa_source_save_i32(io,&row->publication_message) || !entity_fields(io,&row->current) ||
            !entity_fields(io,&row->next) || !q3n_entity_codec(io,row->presentation) ||
            (row->published && (row->current.number!=(int32_t)i || row->presentation->physical!=i ||
                row->publication_message>s->processed)) ||
            (!row->published && row->presentation->valid)) return false;
    }
    return true;
}
bool frontend_remote_snapshots_checkpoint(const frontend_remote_snapshots *s,qa_buffer *out,qa_error *e)
{
    if(!s || !out || out->data || out->size || s->busy || s->faulted)
        return fail(e,QA_ERROR_ARGUMENT,"Remote centity capture requires its completed native continuation");
    frontend_network_presentation_source source; bool present=false;
    if(!frontend_network_presentation_source_read(s->options.frontend,&source,&present,e)) return false;
    if(!present || !domain(&s->source.prediction,&source.prediction) ||
        source.prediction.restart_generation!=s->source.prediction.restart_generation ||
        source.initial_message!=s->source.initial_message || source.initial_command!=s->source.initial_command ||
        source.executed_command!=s->source.executed_command || source.latest_message<s->latest)
        return fail(e,QA_ERROR_ARGUMENT,"Remote centity capture lost its actual network and reached-command domain");
    frontend_remote_snapshots *copy=malloc(sizeof(*copy));
    if(!copy) return fail(e,QA_ERROR_MEMORY,"Copying remote centity capture fields");
    *copy=*s; copy->source=source;
    for(uint32_t i=0;i<QA_Q3_ENTITIES;++i) copy->entities[i].presentation=&copy->presentations[i];
    copy->snap.value.entities=copy->snap.entities; copy->next.value.entities=copy->next.entities;
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,source.prediction.receiver.session,e) && codec(&io,copy) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); free(copy);
    if(!ok && (!e || e->code==QA_OK)) return fail(e,QA_ERROR_FORMAT,"Invalid remote centity capture fields");
    return ok;
}
bool frontend_remote_snapshots_restore(const frontend_remote_snapshots_options *options,
    const frontend_network_presentation_source *source,qa_bytes bytes,frontend_remote_snapshots **out,qa_error *e)
{
    if(!out || *out) return fail(e,QA_ERROR_ARGUMENT,"Remote centity restore requires an empty actual owner");
    frontend_remote_snapshots *s=NULL;
    if(!frontend_remote_snapshots_create(options,source,&s,e)) return false;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,source->prediction.receiver.session,bytes,e) && codec(&io,s) && io.offset==io.input.size;
    qa_source_save_dispose(&io);
    if(!ok) { free(s); if(!e || e->code==QA_OK) fail(e,QA_ERROR_FORMAT,"Invalid complete remote centity continuation"); return false; }
    *out=s; return true;
}
