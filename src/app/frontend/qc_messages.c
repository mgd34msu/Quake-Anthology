#include "internal.h"
#include "qc_messages.h"
#include "q1_sky.h"
#include "save_private.h"
#include "qa/network_q1_decoder_save.h"
#include <stdlib.h>
#include <string.h>

typedef union qc_decoder { qa_nq_decoder *nq; qa_qw_decoder *qw; } qc_decoder;
typedef struct qc_recipient {
    struct qc_recipient *next;
    frontend_qc_camera_receipt camera;
    qc_decoder decoder;
    size_t signon_index,signon_offset,event_index,event_offset;
    uint64_t generation,next_sequence;
    int32_t stats[32];
    uint32_t stat_present;
} qc_recipient;
struct frontend_qc_messages {
    qa_frontend *frontend;
    qa_application *application;
    qc_recipient *recipients;
    bool busy;
};
bool frontend_qc_messages_idle(const frontend_qc_messages *owner)
{ return !owner || !owner->busy; }
static bool current(const frontend_qc_messages *owner)
{ return owner && owner->frontend && owner->frontend->application==owner->application; }
static void decoder_free(qa_net_protocol_id protocol,qc_decoder decoder)
{ if(qa_q1_is_qw(protocol))qa_qw_decoder_destroy(decoder.qw); else qa_nq_decoder_destroy(decoder.nq); }
static void row_free(qc_recipient *row)
{ decoder_free(row->camera.source.protocol,row->decoder); free(row); }
bool frontend_qc_messages_destroy(frontend_qc_messages **out,qa_error *error)
{
    if(!out || !*out) return true;
    if(!frontend_qc_messages_idle(*out)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Local QC protocol delivery remains entered");
    while((*out)->recipients) {
        qc_recipient *row=(*out)->recipients; (*out)->recipients=row->next; row_free(row);
    }
    free(*out); *out=NULL; return true;
}
bool frontend_qc_messages_create(qa_frontend *f,frontend_qc_messages **out,qa_error *error)
{
    if(!f || !f->application || !out || *out) return false;
    frontend_qc_messages *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining local QC protocol recipients");
    owner->frontend=f; owner->application=f->application; *out=owner; return true;
}
static bool recipient_current(const frontend_qc_messages *owner,const frontend_qc_camera_receipt *view)
{
    uint32_t slot;
    return current(owner) && view && qa_application_qc_message_source_current(owner->application,&view->source) &&
        (view->recipient.registry?
            qa_application_qc_message_client(owner->application,&view->source,view->recipient,&slot,NULL) && slot==view->source_slot:
            view->source_slot==0);
}
static bool clone_decoder(const qc_recipient *,qc_decoder *,qa_error *);
static bool row_get(frontend_qc_messages *owner,const qa_application_qc_message_source *source,
    qa_actor_id recipient,uint32_t slot,qc_recipient **out,qa_error *error)
{
    qc_recipient **link=&owner->recipients;
    while(*link) {
        qc_recipient *row=*link;
        if(!recipient_current(owner,&row->camera)) { *link=row->next; row_free(row); continue; }
        if(row->camera.source.provider==source->provider && qa_actor_id_equal(row->camera.recipient,recipient)) {
            *out=row; return true;
        }
        link=&row->next;
    }
    qc_recipient *row=calloc(1,sizeof(*row));
    if(!row) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining local QC full recipient decoder");
    row->camera=(frontend_qc_camera_receipt){.source=*source,.recipient=recipient,.source_slot=slot};
    row->next_sequence=1;
    qc_recipient *baseline=NULL;
    if(recipient.registry) for(qc_recipient *scan=owner->recipients;scan;scan=scan->next)
        if(scan->camera.source.provider==source->provider && !scan->camera.recipient.registry) baseline=scan;
    if(baseline) {
        row->camera=baseline->camera; row->camera.recipient=recipient; row->camera.source_slot=slot;
        row->signon_index=baseline->signon_index; row->signon_offset=baseline->signon_offset; row->generation=baseline->generation;
        row->event_index=baseline->event_index; row->event_offset=baseline->event_offset; row->next_sequence=baseline->next_sequence;
        memcpy(row->stats,baseline->stats,sizeof(row->stats)); row->stat_present=baseline->stat_present;
    }
    bool okay;
    if(baseline)okay=clone_decoder(baseline,&row->decoder,error);
    else if(qa_q1_is_qw(source->protocol))okay=(row->decoder.qw=qa_qw_decoder_create(source->protocol,error))!=NULL;
    else okay=qa_nq_decoder_create(source->protocol,source->options,&row->decoder.nq,error);
    if(!okay) { free(row); return false; }
    row->next=owner->recipients; owner->recipients=row; *out=row; return true;
}
static qa_net_protocol_id decoder_protocol(const qc_recipient *row)
{ return qa_q1_is_qw(row->camera.source.protocol)?qa_qw_decoder_protocol(row->decoder.qw):qa_nq_decoder_protocol(row->decoder.nq); }
static bool decoder_checkpoint(const qc_recipient *row,qa_net_protocol_id protocol,qa_buffer *out,qa_error *error)
{ return qa_q1_is_qw(protocol)?qa_qw_decoder_checkpoint(row->decoder.qw,protocol,out,error):
    qa_nq_decoder_checkpoint(row->decoder.nq,protocol,row->camera.source.options,out,error); }
static bool decoder_restore(qa_net_protocol_id protocol,qa_nq_options options,qa_bytes bytes,qc_decoder *out,qa_error *error)
{ return qa_q1_is_qw(protocol)?qa_qw_decoder_restore_checkpoint(bytes,protocol,&out->qw,error):
    qa_nq_decoder_restore_checkpoint(bytes,protocol,options,&out->nq,error); }
static bool clone_decoder(const qc_recipient *row,qc_decoder *out,qa_error *error)
{
    qa_buffer bytes={0}; qa_net_protocol_id protocol=decoder_protocol(row);
    bool okay=decoder_checkpoint(row,protocol,&bytes,error) &&
        decoder_restore(protocol,row->camera.source.options,(qa_bytes){bytes.data,bytes.size},out,error);
    qa_buffer_free(&bytes); return okay;
}
static bool read_message(qa_net_protocol_id protocol,qc_decoder decoder,qa_net_reader *reader,qa_nq_message *out)
{
    if(!qa_q1_is_qw(protocol))return qa_nq_read(decoder.nq,reader,out);
    qa_qw_service source;
    if(!qa_qw_service_read(reader,decoder.qw,0,&source))return false;
    *out=(qa_nq_message){.op=QA_NQ_NOP};
    switch(source.kind) {
    case QA_QW_STAT:out->op=QA_NQ_STAT;out->data.indexed.index=source.data.stat.index;out->data.indexed.value=source.data.stat.value;break;
    case QA_QW_SET_VIEW:out->op=QA_NQ_SETVIEW;out->data.value=source.data.entity;break;
    case QA_QW_SET_ANGLE:out->op=QA_NQ_SETANGLE;memcpy(out->data.angles,source.data.angles,sizeof(out->data.angles));break;
    case QA_QW_INTERMISSION:out->op=QA_NQ_INTERMISSION;break;
    case QA_QW_FINALE:out->op=QA_NQ_FINALE;break;
    default:break;
    }
    return true;
}
static bool captured_target(const qa_nq_message *message,const qa_application_protocol_event *event,
    size_t start,qa_actor_id *out,qa_error *error)
{
    qa_actor_id target={0}; bool captured=false;
    for(size_t i=0;i<event->reference_count;++i) {
        const qa_application_protocol_reference *reference=event->references+i;
        if(reference->offset!=start+1 || reference->packed_sound) continue;
        if(captured && !qa_actor_id_equal(target,reference->actor))
            return frontend_fail(error,QA_ERROR_FORMAT,"QC SETVIEW repeats incompatible captured actor generations");
        target=reference->actor; captured=true;
    }
    if(!captured && message->data.value!=0)
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"QC SETVIEW has no captured full-generation source reference");
    *out=target; return true;
}
static bool project(frontend_qc_messages *owner,qc_recipient *row,const qa_nq_message *message,
    const qa_application_protocol_event *event,size_t start,qa_error *error)
{
    if(message->op==QA_NQ_STAT) {
        if(message->data.indexed.index>=32)
            return frontend_fail(error,QA_ERROR_FORMAT,"QC client stat exceeds its actual protocol roster");
        uint32_t index=message->data.indexed.index;
        row->stats[index]=message->data.indexed.value; row->stat_present|=UINT32_C(1)<<index; return true;
    }
    if(message->op!=QA_NQ_SKYBOX && message->op!=QA_NQ_SETVIEW && message->op!=QA_NQ_SETANGLE &&
        message->op!=QA_NQ_INTERMISSION && message->op!=QA_NQ_FINALE && message->op!=QA_NQ_CUTSCENE) return true;
    if(!row->next_sequence) return frontend_fail(error,QA_ERROR_MEMORY,"Local QC camera receipt sequence is exhausted");
    if(message->op==QA_NQ_SKYBOX) {
        if(!event->recipient.registry && row->camera.recipient.registry) return true;
        if(owner->frontend->q1_sky) {
            if(!frontend_q1_sky_receive(owner->frontend->q1_sky,row->camera.source.provider,
                row->camera.recipient,message->data.text,error)) return false;
        } else if(!owner->frontend->options.dedicated)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Local QC sky delivery has no presentation owner");
    } else if(message->op==QA_NQ_SETVIEW) {
        qa_actor_id target;
        if(!captured_target(message,event,start,&target,error)) return false;
        row->camera.view_entity=target; row->camera.has_view=true; row->camera.view_sequence=row->next_sequence;
    } else if(message->op==QA_NQ_SETANGLE) {
        if(row->camera.recipient.registry && !qa_application_qc_message_angles(owner->application,&row->camera.source,
            row->camera.recipient,qa_v3(message->data.angles[0],message->data.angles[1],message->data.angles[2]),error)) return false;
        memcpy(row->camera.angles,message->data.angles,sizeof(row->camera.angles));
        row->camera.has_angles=true; row->camera.angle_sequence=row->next_sequence;
    } else row->camera.intermission=(uint32_t)message->op;
    ++row->next_sequence; return true;
}
static bool packet(frontend_qc_messages *owner,qc_recipient *row,
    const qa_application_protocol_event *event,size_t *offset,qa_error *error)
{
    bool qw=qa_q1_is_qw(row->camera.source.protocol);
    if(event->provider!=row->camera.source.provider || event->dialect!=(qw?QA_CLOCK_QUAKEWORLD:QA_CLOCK_NETQUAKE) ||
        (event->multicast && !qw) ||
        (event->recipient.registry && !qa_actor_id_equal(event->recipient,row->camera.recipient)) || *offset>event->payload.size)
        return frontend_fail(error,QA_ERROR_FORMAT,"Local QC packet leaves its actual source and recipient");
    qa_net_reader reader; qc_decoder validation={0};
    if(!clone_decoder(row,&validation,error)) return false;
    qa_bytes tail={event->payload.data?event->payload.data+*offset:NULL,event->payload.size-*offset};
    qa_net_reader_init(&reader,tail,error); qa_nq_message message;
    bool okay=true;
    while(okay && qa_net_reader_remaining(&reader)) {
        size_t message_start=*offset+reader.bit/8;
        okay=read_message(row->camera.source.protocol,validation,&reader,&message);
        if(okay && message.op==QA_NQ_SETVIEW) {
            qa_actor_id target;
            okay=captured_target(&message,event,message_start,&target,error);
        }
    }
    okay=okay && qa_net_reader_finish(&reader); decoder_free(row->camera.source.protocol,validation);
    if(!okay) return false;
    qa_net_reader_init(&reader,tail,error); size_t start=*offset;
    while(qa_net_reader_remaining(&reader)) {
        size_t message_start=start+reader.bit/8;
        if(!read_message(row->camera.source.protocol,row->decoder,&reader,&message) || !project(owner,row,&message,event,message_start,error)) return false;
        *offset=start+reader.bit/8;
        if(!recipient_current(owner,&row->camera)) return frontend_fail(error,QA_ERROR_ARGUMENT,"QC recipient retired during decoded delivery");
    }
    return qa_net_reader_finish(&reader);
}
static bool signon(frontend_qc_messages *owner,qc_recipient *row,size_t index,
    const qa_application_protocol_event *event,qa_error *error)
{
    if(index<row->signon_index) return true;
    if(index!=row->signon_index) return false;
    if(!event->recipient.registry || qa_actor_id_equal(event->recipient,row->camera.recipient))
        if(!packet(owner,row,event,&row->signon_offset,error)) return false;
    ++row->signon_index; row->signon_offset=0;
    return true;
}
static bool event(frontend_qc_messages *owner,qc_recipient *row,size_t index,
    const qa_application_protocol_event *event,uint64_t generation,qa_error *error)
{
    if(row->generation!=generation) {
        if(row->event_offset) return frontend_fail(error,QA_ERROR_ARGUMENT,"QC queue cleared a partially delivered packet");
        row->generation=generation; row->event_index=0;
    }
    if(index<row->event_index) return true;
    if(index!=row->event_index) return false;
    if(event->provider==row->camera.source.provider && !event->signon) {
        bool receives=!event->recipient.registry || qa_actor_id_equal(event->recipient,row->camera.recipient);
        if(row->camera.recipient.registry) {
            if(!qa_application_qc_message_receives(owner->application,&row->camera.source,row->camera.recipient,event,&receives,error))return false;
        } else if(event->multicast)receives=event->destination==0 || event->destination==3;
        if(receives && !packet(owner,row,event,&row->event_offset,error))return false;
    }
    ++row->event_index; row->event_offset=0;
    return true;
}
bool frontend_qc_messages_drain(frontend_qc_messages *owner,qa_error *error)
{
    if(!current(owner) || owner->busy || owner->frontend->capture || owner->frontend->resource_inventory ||
        owner->frontend->source_restoring) return false;
    owner->busy=true; bool okay=true;
    size_t sources=qa_application_qc_message_source_count(owner->application);
    for(size_t i=0;okay && i<sources;++i) {
        qa_application_qc_message_source source; bool found=false;
        okay=qa_application_qc_message_source_at(owner->application,i,&source,&found,error);
        if(!okay || !found) continue;
        qc_recipient *baseline=NULL;
        okay=row_get(owner,&source,(qa_actor_id){0},0,&baseline,error);
        for(uint32_t slot=1;okay && slot<=source.client_slots;++slot) {
            qa_actor_id actor; bool connected=false;
            okay=qa_application_qc_message_client_at(owner->application,&source,slot,&actor,&connected,error);
            if(!okay || !connected) continue;
            qc_recipient *row=NULL;
            okay=row_get(owner,&source,actor,slot,&row,error);
        }
    }
    for(size_t i=0;okay && i<sources;++i) {
        qa_application_qc_message_source source; bool found=false; size_t count=0;
        okay=qa_application_qc_message_source_at(owner->application,i,&source,&found,error);
        if(!okay || !found) continue;
        okay=qa_application_qc_message_signon_count(owner->application,&source,&count,error);
        for(qc_recipient *row=owner->recipients;okay && row;row=row->next)
            if(row->camera.source.provider==source.provider && row->signon_index>count) okay=false;
        for(size_t index=0;okay && index<count;++index) {
            qa_application_protocol_event retained;
            okay=qa_application_qc_message_signon_at(owner->application,&source,index,&retained,error);
            for(unsigned pass=0;okay && pass<2;++pass) for(qc_recipient *row=owner->recipients;okay && row;row=row->next)
                if(row->camera.source.provider==source.provider && (row->camera.recipient.registry!=0)==(pass!=0))
                    okay=signon(owner,row,index,&retained,error);
        }
    }
    uint64_t generation=qa_application_protocol_events_generation(owner->application);
    size_t count=qa_application_protocol_event_count(owner->application);
    for(size_t i=0;okay && i<count;++i) {
        qa_application_protocol_event retained;
        okay=qa_application_protocol_event_at(owner->application,i,&retained);
        /* The shared sky publication must precede recipient overrides for
         * this same actual packet; all sources keep the original queue order. */
        for(unsigned pass=0;okay && pass<2;++pass) for(qc_recipient *row=owner->recipients;okay && row;row=row->next)
            if((row->camera.recipient.registry!=0)==(pass!=0)) okay=event(owner,row,i,&retained,generation,error);
        okay=okay && generation==qa_application_protocol_events_generation(owner->application) &&
            count==qa_application_protocol_event_count(owner->application);
    }
    owner->busy=false; return okay;
}
bool frontend_qc_messages_camera_read(const frontend_qc_messages *owner,qa_actor_owner provider,
    qa_actor_id recipient,frontend_qc_camera_receipt *out,qa_error *error)
{
    if(!out || !current(owner) || owner->busy) return false;
    for(const qc_recipient *row=owner->recipients;row;row=row->next)
        if(row->camera.source.provider==provider && qa_actor_id_equal(row->camera.recipient,recipient)) {
            if(!recipient_current(owner,&row->camera)) return false;
            *out=row->camera;
            if(out->view_entity.registry && !qa_actors_get(qa_world_actors(qa_application_world(owner->application)),out->view_entity))
                out->view_entity=(qa_actor_id){0};
            return true;
        }
    return frontend_fail(error,QA_ERROR_NOT_FOUND,"Local QC recipient has no decoded camera receipt");
}
bool frontend_qc_messages_camera_current(const frontend_qc_messages *owner,const frontend_qc_camera_receipt *view)
{
    frontend_qc_camera_receipt actual;
    return view && frontend_qc_messages_camera_read(owner,view->source.provider,view->recipient,&actual,NULL) &&
        actual.source_slot==view->source_slot && actual.source.instance==view->source.instance &&
        actual.source.descriptor==view->source.descriptor && actual.source.map_revision==view->source.map_revision &&
        actual.has_view==view->has_view && actual.has_angles==view->has_angles && actual.intermission==view->intermission &&
        qa_actor_id_equal(actual.view_entity,view->view_entity) && actual.view_sequence==view->view_sequence &&
        actual.angle_sequence==view->angle_sequence && !memcmp(actual.angles,view->angles,sizeof(actual.angles));
}
static bool declared_client_frame(const frontend_qc_messages *owner,qa_actor_id actor,bool camera,
    qa_application_qc_client_presentation *vitals,qa_application_camera_view *view,bool *found,qa_error *error)
{
    if(!current(owner) || owner->busy || !found || (camera?!view:!vitals))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Declared QC client output requires its returned message owner");
    *found=false;
    size_t count=qa_application_qc_message_source_count(owner->application);
    for(size_t i=0;i<count;++i) {
        qa_application_qc_message_source source; bool qc=false;
        if(!qa_application_qc_message_source_at(owner->application,i,&source,&qc,error)) return false;
        if(!qc) continue;
        qa_application_qc_client_presentation frame; bool admitted=false;
        if(!qa_application_qc_client_presentation_read(owner->application,source.provider,actor,&frame,&admitted,error)) return false;
        if(!admitted || !(camera?frame.view:frame.vitals)) continue;
        if(!camera) { *vitals=frame; *found=true; return true; }
        frontend_qc_camera_receipt receipt;
        if(!frontend_qc_messages_camera_read(owner,source.provider,actor,&receipt,error)) return false;
        qa_vec3 angles=qa_v3(receipt.angles[0],receipt.angles[1],receipt.angles[2]);
        if(!qa_application_qc_client_presentation_camera(owner->application,&frame,receipt.view_entity,
            receipt.intermission!=0,receipt.has_angles?&angles:NULL,view,found,error) ||
            !frontend_qc_messages_camera_current(owner,&receipt)) return false;
        if(*found) return true;
    }
    return true;
}
bool frontend_qc_messages_client_vitals(const frontend_qc_messages *owner,qa_actor_id actor,
    qa_application_qc_client_presentation *out,bool *found,qa_error *error)
{ return declared_client_frame(owner,actor,false,out,NULL,found,error); }
bool frontend_qc_messages_client_camera(const frontend_qc_messages *owner,qa_actor_id actor,
    qa_application_camera_view *out,bool *found,qa_error *error)
{ return declared_client_frame(owner,actor,true,NULL,out,found,error); }
bool frontend_qc_messages_stat_read(const frontend_qc_messages *owner,qa_actor_owner provider,
    qa_actor_id recipient,uint32_t index,int32_t *value,bool *present,qa_error *error)
{
    if(!current(owner) || owner->busy || index>=32 || !value || !present)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC stat observation requires its returned actual owner");
    *present=false;
    for(const qc_recipient *row=owner->recipients;row;row=row->next)
        if(row->camera.source.provider==provider && qa_actor_id_equal(row->camera.recipient,recipient)) {
            if(!recipient_current(owner,&row->camera)) return false;
            *present=(row->stat_present&(UINT32_C(1)<<index))!=0;
            if(*present) *value=row->stats[index];
            return true;
        }
    qa_application_qc_message_source source; bool found=false; uint32_t slot;
    return qa_application_qc_message_source_read(owner->application,provider,&source,&found,error) && found &&
        qa_application_qc_message_client(owner->application,&source,recipient,&slot,error);
}
static bool unified_player_current(void *context,qa_application *app,const application_unified_source *source,
    qa_net_client_id client,const qa_unified_session_player *player)
{
    frontend_qc_unified_player_receipt *receipt=context;
    if(!receipt || !source || !player || receipt->application!=app ||
        !qa_net_client_id_equal(client,receipt->client) || !qa_actor_id_equal(player->actor,receipt->player.actor) ||
        player->seat.owner!=receipt->player.seat.owner || player->seat.index!=receipt->player.seat.index ||
        player->source_owner!=receipt->player.source_owner || player->source_slot!=receipt->player.source_slot ||
        player->movement!=receipt->player.movement || source->owner!=receipt->source.owner ||
        source->launch!=receipt->source.launch || source->session!=receipt->source.session ||
        source->world!=receipt->source.world || source->family!=receipt->source.family ||
        source->publication!=receipt->source.publication || source->map_revision!=receipt->source.map_revision ||
        source->frame_revision!=receipt->source.frame_revision ||
        !application_unified_source_current(app,&receipt->source) || !application_unified_source_current(app,source) ||
        !application_unified_player_current(app,client,&receipt->player) || !application_unified_player_current(app,client,player) ||
        !frontend_qc_messages_camera_current(receipt->owner,&receipt->camera)) return false;
    int32_t ammo=0; bool has_ammo=false;
    return frontend_qc_messages_stat_read(receipt->owner,receipt->source.owner,receipt->player.actor,3,&ammo,&has_ammo,NULL) &&
        has_ammo==receipt->external.has_qc_ammo && (!has_ammo || ammo==receipt->external.qc_ammo);
}
bool frontend_qc_messages_unified_player_read(const frontend_qc_messages *owner,qa_application *app,
    const application_unified_source *source,qa_net_client_id client,const qa_unified_session_player *player,
    frontend_qc_unified_player_receipt *out,bool *present,qa_error *error)
{
    if(!out || !present || !source || !player ||
        !application_unified_source_current(app,source) || !application_unified_player_current(app,client,player))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC output receipt requires its actual returned Source and full recipient");
    *present=false;
    qa_application_qc_message_source qc; bool found=false;
    if(!qa_application_qc_message_source_read(app,source->owner,&qc,&found,error)) return false;
    if(!found) { *out=(frontend_qc_unified_player_receipt){0}; return true; }
    if(!current(owner) || owner->application!=app || owner->busy)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC output receipt requires its actual returned decoder owner");
    frontend_qc_unified_player_receipt value={.owner=owner,.application=app,.source=*source,.client=client,.player=*player};
    if(!frontend_qc_messages_camera_read(owner,source->owner,player->actor,&value.camera,error) ||
        value.camera.source_slot!=player->source_slot || value.camera.source.instance!=qc.instance ||
        value.camera.source.descriptor!=qc.descriptor || value.camera.source.map_revision!=source->map_revision ||
        !frontend_qc_messages_stat_read(owner,source->owner,player->actor,3,
            &value.external.qc_ammo,&value.external.has_qc_ammo,error)) return false;
    value.player_camera=(application_unified_player_camera){.source=value.camera.source,.recipient=value.camera.recipient,
        .view_entity=value.camera.view_entity,.source_slot=value.camera.source_slot,.intermission=value.camera.intermission,
        .angles=qa_v3(value.camera.angles[0],value.camera.angles[1],value.camera.angles[2]),.has_angles=value.camera.has_angles};
    *out=value;
    out->external.context=out; out->external.current=unified_player_current; out->external.camera=&out->player_camera;
    if(!unified_player_current(out,app,source,client,player))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QC output receipt changed during actual decoder observation");
    *present=true; return true;
}
static bool bytes_field(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t size=io->direction==QA_SOURCE_SAVE_WRITE?bytes->size:0;
    if(!qa_source_save_count(io,&size,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE) return !size || qa_source_save_bytes(io,(void *)bytes->data,size);
    if(io->offset>io->input.size || size>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){size?io->input.data+io->offset:NULL,size}; io->offset+=size; return true;
}
static bool fields(qa_source_save_io *io,frontend_qc_messages *owner)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','F','Q','L'}; size_t count=0;
    if(!reading) for(qc_recipient *row=owner->recipients;row;row=row->next) if(recipient_current(owner,&row->camera)) ++count;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFQL",4) || !qa_source_save_count(io,&count,reading?io->input.size/8:SIZE_MAX/sizeof(qc_recipient))) return false;
    qc_recipient **link=&owner->recipients;
    for(size_t i=0;i<count;++i) {
        qc_recipient saved={0},*row=NULL; qa_buffer decoder={0}; qa_bytes bytes={0};
        if(reading) row=&saved;
        else {
            while(*link && !recipient_current(owner,&(*link)->camera)) link=&(*link)->next;
            if(!*link) return false;
            saved=**link; row=&saved; link=&(*link)->next;
            if(row->generation!=qa_application_protocol_events_generation(owner->application)) {
                if(row->event_offset) return false;
                row->generation=qa_application_protocol_events_generation(owner->application); row->event_index=0;
            }
            if(row->camera.view_entity.registry &&
                !qa_actors_get(qa_world_actors(qa_application_world(owner->application)),row->camera.view_entity))
                row->camera.view_entity=(qa_actor_id){0};
        }
        frontend_qc_camera_receipt *camera=&row->camera;
        uint64_t map_revision=camera->source.map_revision;
        if(!frontend_save_provider(io,owner->application,&camera->source.provider) || !camera->source.provider ||
            !qa_source_save_u64(io,&map_revision) || !qa_source_save_actor(io,&camera->recipient) ||
            !qa_source_save_actor(io,&camera->view_entity) || !qa_source_save_u32(io,&camera->source_slot) ||
            !qa_source_save_bool(io,&camera->has_view) || !qa_source_save_bool(io,&camera->has_angles) ||
            !qa_source_save_u32(io,&camera->intermission) ||
            (camera->intermission && camera->intermission!=QA_NQ_INTERMISSION &&
             camera->intermission!=QA_NQ_FINALE && camera->intermission!=QA_NQ_CUTSCENE) ||
            !qa_source_save_u64(io,&camera->view_sequence) || !qa_source_save_u64(io,&camera->angle_sequence) ||
            !qa_source_save_u64(io,&row->next_sequence) ||
            camera->has_view!=(camera->view_sequence!=0) || camera->has_angles!=(camera->angle_sequence!=0) ||
            (!camera->has_view && camera->view_entity.registry) ||
            (row->next_sequence && (camera->view_sequence>=row->next_sequence || camera->angle_sequence>=row->next_sequence))) return false;
        for(unsigned component=0;component<3;++component)
            if(!qa_source_save_f32(io,&camera->angles[component]) || !isfinite(camera->angles[component]) ||
                (!camera->has_angles && camera->angles[component]!=0)) return false;
        if(!qa_source_save_u32(io,&row->stat_present)) return false;
        for(unsigned stat=0;stat<32;++stat)
            if(!qa_source_save_i32(io,&row->stats[stat]) ||
                (!(row->stat_present&(UINT32_C(1)<<stat)) && row->stats[stat])) return false;
        if(reading) {
            bool found=false;
            if(!qa_application_qc_message_source_read(owner->application,camera->source.provider,&camera->source,&found,io->error) ||
                !found || camera->source.map_revision!=map_revision ||
                !recipient_current(owner,camera)) return false;
        }
        size_t signon_count=0;
        if(!qa_application_qc_message_signon_count(owner->application,&camera->source,&signon_count,io->error) ||
            !qa_source_save_count(io,&row->signon_index,signon_count) ||
            !qa_source_save_count(io,&row->signon_offset,SIZE_MAX) || !qa_source_save_u64(io,&row->generation) ||
            row->generation!=qa_application_protocol_events_generation(owner->application) ||
            !qa_source_save_count(io,&row->event_index,qa_application_protocol_event_count(owner->application)) ||
            !qa_source_save_count(io,&row->event_offset,SIZE_MAX)) return false;
        qa_application_protocol_event retained;
        if(row->signon_offset && (row->signon_index==signon_count ||
            !qa_application_qc_message_signon_at(owner->application,&camera->source,row->signon_index,&retained,io->error) ||
            row->signon_offset>=retained.payload.size)) return false;
        if(row->event_offset && (!qa_application_protocol_event_at(owner->application,row->event_index,&retained) ||
            row->event_offset>=retained.payload.size || retained.provider!=camera->source.provider || retained.signon)) return false;
        qa_net_protocol_id protocol=reading?(qa_net_protocol_id){0}:decoder_protocol(row);
        uint32_t kind=protocol.kind;
        if(!qa_source_save_u32(io,&kind) || !qa_source_save_u32(io,&protocol.flags) ||
            !qa_source_save_u32(io,&protocol.revision)) return false;
        if(reading) protocol.kind=(qa_net_protocol)kind;
        bool okay=reading || decoder_checkpoint(row,protocol,&decoder,io->error);
        if(!reading) bytes=(qa_bytes){decoder.data,decoder.size};
        okay=okay && bytes_field(io,&bytes) && bytes.size;
        if(reading && okay) {
            for(qc_recipient *other=owner->recipients;other;other=other->next)
                if(other->camera.source.provider==camera->source.provider &&
                    qa_actor_id_equal(other->camera.recipient,camera->recipient)) okay=false;
            qc_recipient *actual=okay?calloc(1,sizeof(*actual)):NULL;
            if(okay && !actual) okay=frontend_fail(io->error,QA_ERROR_MEMORY,"Restoring local QC decoder recipient");
            if(actual) {
                *actual=saved; actual->decoder=(qc_decoder){0}; actual->next=NULL; *link=actual; link=&actual->next;
                okay=qa_q1_is_qw(protocol)==qa_q1_is_qw(camera->source.protocol) &&
                    decoder_restore(protocol,camera->source.options,bytes,&actual->decoder,io->error);
            }
        }
        qa_buffer_free(&decoder); if(!okay) return false;
    }
    return true;
}
bool frontend_qc_messages_checkpoint(const frontend_qc_messages *borrowed,qa_buffer *out,qa_error *error)
{
    if(!current(borrowed) || !borrowed->frontend->capture || !frontend_qc_messages_idle(borrowed) || !out || out->data || out->size) return false;
    frontend_qc_messages *owner=(frontend_qc_messages *)borrowed; qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,qa_application_session(owner->application),error) && fields(&io,owner) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_qc_messages_restore(qa_frontend *f,qa_bytes bytes,frontend_qc_messages **out,qa_error *error)
{
    if(!f || !f->source_restoring || f->capture || !out || *out) return false;
    if(!frontend_qc_messages_create(f,out,error)) return false;
    qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,qa_application_session(f->application),bytes,error) && fields(&io,*out) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!okay && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Local QC decoder continuation leaves its actual packet cursors");
    return okay;
}
