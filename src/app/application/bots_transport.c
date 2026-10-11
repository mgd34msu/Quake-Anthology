#include "bots_transport.h"
#include "unified_q3_events.h"
#include "control_frame.h"
#include "../../bots/save_fields.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct bot_connection {
    struct bot_connection *next;
    qa_unified_frame_lease *storage;
    uint32_t client;
    qa_actor_id actor;
    application_q3_command_cursor server_commands;
    int32_t snapshot[APPLICATION_BOT_WORLD_LAST+1];
    size_t snapshot_count;
    bool active,snapshot_present;
} bot_connection;
struct application_bot_transport {
    application_bot_transport_services services;
    bot_connection *first,*last;
    size_t count,calls;
    double elapsed_ms;
};
static bool fail(qa_error *error,const char *text) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",text);return false;}
static bot_connection *connection(const application_bot_transport *transport,uint32_t client) {
    for(bot_connection *entry=transport?transport->first:NULL;entry;entry=entry->next) {
        if(entry->active && entry->client==client) return entry;
    }
    return NULL;
}
static void sweep(application_bot_transport *transport) {
    if(transport->calls) return;
    bot_connection **at=&transport->first,*previous=NULL;
    while(*at) {
        bot_connection *entry=*at;
        if(entry->active) {previous=entry;at=&entry->next;}
        else {
            *at=entry->next;
            if(transport->last==entry) transport->last=previous;
            --transport->count;
            application_q3_command_clear(&entry->server_commands);
            qa_unified_frame_lease_release(entry->storage);
        }
    }
}
static bot_connection *connection_take(application_bot_transport *transport,qa_error *error) {
    qa_unified_frame_lease *storage=application_control_storage_acquire(transport->services.application,error);
    if(!storage) return NULL;
    bot_connection *entry=qa_unified_frame_lease_alloc(storage,1,sizeof(*entry),_Alignof(bot_connection),error);
    if(!entry) {qa_unified_frame_lease_release(storage);return NULL;}
    entry->storage=storage;entry->active=true;
    application_q3_command_init(&entry->server_commands,transport->services.application);
    if(transport->last) transport->last->next=entry;
    else transport->first=entry;
    transport->last=entry;++transport->count;
    return entry;
}
bool application_bot_transport_create(const application_bot_transport_services *services,
    application_bot_transport **out,qa_error *error) {
    if(!services || !services->session || !services->world || !services->drop || !services->client || !out || *out)
        return fail(error,"local bot transport requires its real source world and disconnect owner");
    application_bot_transport *transport=calloc(1,sizeof(*transport));
    if(!transport) {qa_error_set(error,QA_ERROR_MEMORY,0,"allocating local bot transport");return false;}
    transport->services=*services;*out=transport;return true;
}
bool application_bot_transport_destroy(application_bot_transport *transport,qa_error *error) {
    if(!transport) return true;
    if(transport->calls) return fail(error,"local bot transport is executing a source callback");
    while(transport->first) {
        bot_connection *entry=transport->first;transport->first=entry->next;
        application_q3_command_clear(&entry->server_commands);
        qa_unified_frame_lease_release(entry->storage);
    }
    free(transport);return true;
}
bool application_bot_transport_can_destroy(const application_bot_transport *transport) {return !transport || !transport->calls;}
bool application_bot_transport_open(application_bot_transport *transport,uint32_t client,qa_actor_id actor,qa_error *error) {
    uint32_t actual;
    if(!transport || client>INT32_MAX || connection(transport,client) ||
       !qa_actors_get(qa_session_actors(transport->services.session),actor) ||
       !transport->services.client(transport->services.context,actor,&actual,error) || actual!=client)
        return fail(error,"local bot connection lacks its actual unique physical source client");
    for(bot_connection *entry=transport->first;entry;entry=entry->next)
        if(entry->active && qa_actor_id_equal(entry->actor,actor))
            return fail(error,"local bot actor already has a source connection");
    bot_connection *entry=connection_take(transport,error);
    if(!entry) return false;
    entry->client=client;entry->actor=actor;return true;
}
bool application_bot_transport_close(application_bot_transport *transport,uint32_t client,qa_error *error) {
    if(!transport) return fail(error,"local bot transport owner is absent");
    bot_connection *entry=connection(transport,client);if(entry) entry->active=false;
    sweep(transport);return true;
}
qa_actor_id application_bot_transport_actor(const application_bot_transport *transport,uint32_t client) {
    bot_connection *entry=connection(transport,client);return entry?entry->actor:(qa_actor_id){0};
}
static bool bot_output(void *context,int32_t client,const char *text,
    const qa_unified_q3_event *event,qa_event_lease *lease,uint32_t config_index,uint32_t fragment,qa_error *error)
{
    application_bot_transport *transport=context;
    (void)text; (void)lease;
    for(bot_connection *entry=transport->first;entry;entry=entry->next) {
        if(!entry->active || (client>=0 && entry->client!=(uint32_t)client)) continue;
        bool overflow;
        if(!application_q3_command_admit(&entry->server_commands,event->text,
            config_index,fragment,&overflow,error)) return false;
        if(overflow) {
            entry->active=false;
            if(!transport->services.drop(transport->services.context,entry->client,"Server command overflow",error)) return false;
        }
    }
    return true;
}
bool application_bot_transport_output(application_bot_transport *transport,const qa_unified_q3_event *event,
    qa_event_lease *lease,qa_error *error)
{
    ++transport->calls;
    bool okay=application_q3_output_fanout(event,lease,bot_output,transport,error);
    --transport->calls;
    sweep(transport);
    return okay;
}
bool application_bot_transport_console(application_bot_transport *transport,uint32_t client,char *out,size_t capacity,
    bool *found,qa_error *error) {
    bot_connection *entry=connection(transport,client);
    if(!entry || !out || !capacity || !found) return fail(error,"bot console consumption lacks its actual local connection");
    *found=false;out[0]=0;
    if(entry->server_commands.acknowledged==entry->server_commands.sequence) return true;
    if(entry->server_commands.acknowledged==INT32_MAX) return fail(error,"raw bot reliable acknowledgement exceeds its signed source domain");
    int32_t next=entry->server_commands.acknowledged+1;bool clamped;
    const char *text;
    if(!application_q3_command_lookup(&entry->server_commands,next,&text,error) ||
       !application_q3_command_ack(&entry->server_commands,next,&clamped,error)) return false;
    if(!*text) return true;
    size_t size=strlen(text);if(size>=capacity) size=capacity-1;
    memcpy(out,text,size);out[size]=0;*found=true;return true;
}
bool application_bot_transport_snapshot(application_bot_transport *transport,uint32_t client,int32_t sequence,
    int32_t *out,qa_error *error) {
    bot_connection *entry=connection(transport,client);
    if(!transport || !entry || !out || transport->calls==SIZE_MAX) return fail(error,"bot snapshot lacks its actual local source connection");
    if(sequence<0) {*out=-1;return true;}
    ++transport->calls;bool okay=true;
    if(!entry->snapshot_present) {
        uint32_t count=application_bot_world_entity_count(transport->services.world);
        size_t visible=0;
        for(uint32_t number=0;okay && number<count;++number) {
            application_bot_world_entity observed;
            okay=application_bot_world_read(transport->services.world,(int32_t)number,&observed,error);
            if(okay && observed.present && observed.linked && !observed.hidden) entry->snapshot[visible++]=(int32_t)number;
        }
        if(okay) {entry->snapshot_count=visible;entry->snapshot_present=true;}
    }
    if(okay) *out=(size_t)sequence<entry->snapshot_count?entry->snapshot[sequence]:-1;
    --transport->calls;sweep(transport);return okay;
}
bool application_bot_transport_frame(application_bot_transport *transport,double elapsed_ms,qa_error *error) {
    if(!transport || transport->calls) return fail(error,"local bot frame reset requires its idle transport");
    transport->elapsed_ms=elapsed_ms;
    for(bot_connection *entry=transport->first;entry;entry=entry->next) {
        entry->snapshot_count=0;entry->snapshot_present=false;
    }
    return true;
}
static bool connection_fields(qa_source_save_io *io,application_bot_transport *transport,bot_connection *entry) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(reading) application_q3_command_init(&entry->server_commands,transport->services.application);
    if(!qa_source_save_u32(io,&entry->client) || !qa_source_save_actor(io,&entry->actor) ||
       !qa_source_save_i32(io,&entry->server_commands.sequence) || !qa_source_save_i32(io,&entry->server_commands.acknowledged)) return false;
    if(entry->client>=application_bot_world_max_clients(transport->services.world) || entry->server_commands.sequence<0 ||
       !qa_actors_get(qa_session_actors(transport->services.session),entry->actor))
        return bot_save_fail(io,QA_ERROR_FORMAT,"invalid actual local bot connection continuation");
    uint32_t actual;
    if(!transport->services.client(transport->services.context,entry->actor,&actual,io->error)) return false;
    if(actual!=entry->client) return bot_save_fail(io,QA_ERROR_FORMAT,"local bot source client binding differs");
    for(size_t i=0;i<QA_Q3_RELIABLE;++i) {
        const char *text=NULL;
        if(!reading && !application_q3_command_lookup_slot(&entry->server_commands,i,&text,io->error)) return false;
        if(!bot_save_text(io,&text)) return false;
        bool valid=text && strlen(text)<QA_Q3_COMMAND_CHARS;
        if(reading) {
            if(valid) valid=application_q3_command_restore_slot(&entry->server_commands,i,text,io->error);
            free((void *)text);
        }
        if(!valid) return bot_save_fail(io,QA_ERROR_FORMAT,"invalid local bot reliable slot continuation");
    }
    if(!qa_source_save_bool(io,&entry->snapshot_present) ||
       !qa_source_save_count(io,&entry->snapshot_count,UINT32_MAX)) return false;
    if(!entry->snapshot_present && entry->snapshot_count)
        return bot_save_fail(io,QA_ERROR_FORMAT,"absent local bot snapshot has entity entries");
    if(reading) {
        if(io->offset>io->input.size || entry->snapshot_count>(io->input.size-io->offset)/4 ||
           entry->snapshot_count>SIZE_MAX/sizeof(*entry->snapshot))
            return bot_save_fail(io,QA_ERROR_FORMAT,"local bot snapshot exceeds its actual saved extent");
        if(entry->snapshot_count>sizeof(entry->snapshot)/sizeof(entry->snapshot[0]))
            return bot_save_fail(io,QA_ERROR_FORMAT,"local bot snapshot exceeds its source entity namespace");
    }
    uint32_t extent=application_bot_world_entity_count(transport->services.world);
    for(size_t i=0;i<entry->snapshot_count;++i) {
        if(!qa_source_save_i32(io,entry->snapshot+i)) return false;
        if(entry->snapshot[i]<0 || (uint32_t)entry->snapshot[i]>=extent)
            return bot_save_fail(io,QA_ERROR_FORMAT,"saved bot sensory snapshot entity exceeds its real source namespace");
    }
    return true;
}
bool application_bot_transport_fields(qa_source_save_io *io,application_bot_transport *transport) {
    if(!io) return false;
    if(!transport || transport->calls)
        return bot_save_fail(io,QA_ERROR_ARGUMENT,"local bot transport codec requires its actual idle owner");
    double elapsed=transport->elapsed_ms;
    size_t count=transport->count;
    if(!qa_source_save_f64(io,&elapsed) ||
       !isfinite(elapsed) || elapsed<0 ||
       !qa_source_save_count(io,&count,application_bot_world_max_clients(transport->services.world)))
        return bot_save_fail(io,QA_ERROR_FORMAT,"invalid local bot transport continuation header");
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    application_bot_transport *scratch=NULL,*owner=transport;bool okay=true;
    if(reading) {
        if(io->offset>io->input.size || count>(io->input.size-io->offset)/8)
            return bot_save_fail(io,QA_ERROR_FORMAT,"local bot connections exceed their saved extent");
        if(!application_bot_transport_create(&transport->services,&scratch,io->error)) return false;
        scratch->elapsed_ms=elapsed;
        owner=scratch;
    }
    bot_connection *current=owner->first;
    for(size_t i=0;okay && i<count;++i) {
        bot_connection *entry;
        if(reading) {
            entry=connection_take(owner,io->error);
            if(!entry) {okay=false;break;}
        } else {entry=current;current=entry->next;}
        okay=connection_fields(io,owner,entry);
        for(bot_connection *prior=owner->first;okay && prior!=entry;prior=prior->next)
            if(prior->client==entry->client || qa_actor_id_equal(prior->actor,entry->actor))
                okay=bot_save_fail(io,QA_ERROR_FORMAT,"duplicate local bot physical connection continuation");
    }
    if(reading) {
        if(okay) {
            application_bot_transport old=*transport;*transport=*scratch;*scratch=old;
        }
        application_bot_transport_destroy(scratch,NULL);
    }
    return okay;
}
