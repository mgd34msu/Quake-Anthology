#include "guest_q3_component_source_private.h"
#include "qa/q3_abi.h"
#include "qa/source_save.h"
#include "qa/binary.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static bool fail(qa_source_save_io *io,const char *text)
{ qa_error_set(io->error,QA_ERROR_FORMAT,0,"%s",text); return false; }
static bool write_record(void *context,size_t at,qa_bytes bytes,qa_error *e)
{ (void)e; if(bytes.size) memcpy((uint8_t *)context+at,bytes.data,bytes.size); return true; }
static bool gamestate(qa_source_save_io *io,qa_qvm_abi abi,qa_q3_gamestate *state)
{
    uint8_t bytes[20100]={0}; qa_q3_abi_record record={.abi=abi,.bytes={bytes,sizeof(bytes)},.context=bytes,.write=write_record};
    if(io->direction==QA_SOURCE_SAVE_WRITE&&!qa_q3_abi_write_gamestate(&record,0,true,state,io->error)) return false;
    if(!qa_source_save_bytes(io,bytes,sizeof(bytes))) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        uint32_t size=qa_load_u32le(bytes+20096);
        if(size>16000||bytes[4096]) return fail(io,"Component saved gameState exceeds its source strings");
        for(size_t i=0;i<1024;++i) {
            uint32_t at=qa_load_u32le(bytes+4*i);
            if(at>=(size?size:1)||(at&&!memchr(bytes+4096+at,0,size-at))) return fail(io,"Component saved configstring has no source terminator");
            state->config_offsets[i]=(uint16_t)at;
        }
        state->string_bytes=size; memcpy(state->strings,bytes+4096,16000);
    }
    return true;
}
static bool actor(qa_source_save_io *io,application_q3_scene_actor *row)
{ return qa_source_save_u32(io,&row->slot)&&row->slot<1022&&qa_source_save_actor(io,&row->actor)&&row->actor.registry&&qa_source_save_bool(io,&row->owned); }
static bool command(qa_source_save_io *io,component_source_command *row)
{
    return qa_source_save_i32(io,&row->sequence)&&row->sequence>0&&
        qa_source_save_actor(io,&row->recipient)&&qa_source_save_owned_text(io,&row->text)&&row->text;
}
static bool entity(qa_source_save_io *io,qa_qvm_abi abi,component_source_entity *row)
{
    uint8_t bytes[512]={0}; size_t size=qa_qvm_shared_entity_bytes(abi);
    qa_q3_abi_record record={.abi=abi,.bytes={bytes,size},.context=bytes,.write=write_record};
    if(size>sizeof(bytes)||!actor(io,&row->actor)) return false;
    bool ok=io->direction==QA_SOURCE_SAVE_READ||
        (qa_q3_abi_write_entity(&record,0,true,&row->state,io->error)&&qa_q3_abi_write_shared_entity(&record,0,&row->shared,io->error));
    if(ok) ok=qa_source_save_bytes(io,bytes,size);
    if(ok&&io->direction==QA_SOURCE_SAVE_READ) ok=qa_q3_abi_read_entity(&record,0,true,&row->state,io->error)&&qa_q3_abi_read_shared_entity(&record,0,&row->shared,io->error);
    if(!ok||row->state.number!=(int32_t)row->actor.slot||!qa_source_save_i32(io,&row->visibility.area)||
        !qa_source_save_i32(io,&row->visibility.area2)||!qa_source_save_i32(io,&row->visibility.last_cluster)||
        !qa_source_save_u32(io,&row->visibility.cluster_count)||row->visibility.cluster_count>16) return false;
    for(size_t i=0;i<16;++i) if(!qa_source_save_i32(io,row->visibility.clusters+i)) return false;
    return true;
}
static bool client(qa_source_save_io *io,qa_qvm_abi abi,component_source_client *row)
{
    uint8_t bytes[468]={0}; size_t size=qa_qvm_player_bytes(abi);
    qa_q3_abi_record record={.abi=abi,.bytes={bytes,size},.context=bytes,.write=write_record};
    uint32_t product=(uint32_t)row->state.product;
    if(size>sizeof(bytes)||!qa_source_save_actor(io,&row->actor)||!row->actor.registry||!qa_source_save_u32(io,&row->slot)||row->slot>=64||
        !qa_source_save_u32(io,&product)||product>QA_Q3_TEAM_ARENA) return false;
    bool ok=io->direction==QA_SOURCE_SAVE_READ||qa_q3_abi_write_player(&record,0,true,false,&row->state,io->error);
    if(ok) ok=qa_source_save_bytes(io,bytes,size);
    if(ok&&io->direction==QA_SOURCE_SAVE_READ) { ok=qa_q3_abi_read_player(&record,0,true,&row->state,io->error); row->state.product=(qa_q3_product)product; }
    return ok;
}
static bool publication(qa_source_save_io *io,application_q3_component_source *s,component_source_publication *p)
{
    bool present=p->game_state!=NULL;
    if(!qa_source_save_bool(io,&present)||!present) return !io->failed;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        p->game_state=calloc(1,sizeof(*p->game_state)); if(!p->game_state) return fail(io,"Retaining actual saved component publication");
    }
    if(!qa_source_save_i64(io,&p->revision)||p->revision<=0||p->revision>s->revision||
        !qa_source_save_i64(io,&p->game_state_revision)||p->game_state_revision<0||p->game_state_revision>s->game_state_revision||
        !qa_source_save_i32(io,&p->time_ms)||p->time_ms<0||p->time_ms>s->time_ms||
        !qa_source_save_i32(io,&p->command_sequence)||p->command_sequence<0||p->command_sequence>s->command_sequence||
        !gamestate(io,s->options.abi,p->game_state)||!qa_source_save_count(io,&p->entity_count,1022)||
        !qa_source_save_count(io,&p->client_count,64)||!qa_source_save_count(io,&p->command_count,64)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        p->entities=p->entity_count?calloc(p->entity_count,sizeof(*p->entities)):NULL;
        p->clients=p->client_count?calloc(p->client_count,sizeof(*p->clients)):NULL;
        p->commands=p->command_count?calloc(p->command_count,sizeof(*p->commands)):NULL;
        if((p->entity_count&&!p->entities)||(p->client_count&&!p->clients)||(p->command_count&&!p->commands)) return fail(io,"Retaining actual source publication rows");
    }
    for(size_t i=0;i<p->entity_count;++i) {
        if(!entity(io,s->options.abi,p->entities+i)) return false;
        for(size_t j=0;j<i;++j) if(p->entities[j].actor.slot==p->entities[i].actor.slot||qa_actor_id_equal(p->entities[j].actor.actor,p->entities[i].actor.actor)) return fail(io,"Saved source publication duplicates an entity");
    }
    for(size_t i=0;i<p->client_count;++i) {
        if(!client(io,s->options.abi,p->clients+i)) return false;
        bool found=false;
        for(size_t j=0;j<p->entity_count;++j) if(p->entities[j].actor.slot==p->clients[i].slot&&qa_actor_id_equal(p->entities[j].actor.actor,p->clients[i].actor)&&!p->entities[j].actor.owned) found=true;
        if(!found) return fail(io,"Saved source client lacks its actual published entity");
        for(size_t j=0;j<i;++j) if(p->clients[j].slot==p->clients[i].slot) return fail(io,"Saved source publication duplicates a client");
    }
    int32_t first=p->command_sequence>64?p->command_sequence-63:1;
    if(p->command_count!=(size_t)(p->command_sequence-first+1)) return fail(io,"Saved source command window has a gap");
    for(size_t i=0;i<p->command_count;++i) if(!command(io,p->commands+i)||p->commands[i].sequence!=(int32_t)((int64_t)first+(int64_t)i)) return fail(io,"Saved source publication has an unordered command");
    return true;
}
static bool fields(qa_source_save_io *io,application_q3_component_source *s)
{
    uint8_t magic[4]={'Q','G','C','S'},expected[4]={'Q','G','C','S'};
    uint64_t owner=s->options.owner,generation=s->options.generation;
    if(!qa_source_save_bytes(io,magic,4)||memcmp(magic,expected,4)||
        !qa_source_save_u64(io,&owner)||owner!=s->options.owner||!qa_source_save_u64(io,&generation)||generation!=s->options.generation||
        !qa_source_save_i64(io,&s->revision)||s->revision<0||!qa_source_save_i64(io,&s->game_state_revision)||s->game_state_revision<0||
        !qa_source_save_i32(io,&s->time_ms)||s->time_ms<0||!qa_source_save_i32(io,&s->command_sequence)||s->command_sequence<0||
        !qa_source_save_bool(io,&s->dirty)||!gamestate(io,s->options.abi,s->game_state)||!qa_source_save_count(io,&s->actor_count,1022)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) { s->actors=s->actor_count?calloc(s->actor_count,sizeof(*s->actors)):NULL; if(s->actor_count&&!s->actors) return fail(io,"Retaining saved component source bindings"); }
    for(size_t i=0;i<s->actor_count;++i) {
        if(!actor(io,&s->actors[i].row)||!qa_source_save_bool(io,&s->actors[i].client)) return false;
        const qa_actor_record *actual=qa_actors_get(qa_session_actors(s->options.session),s->actors[i].row.actor);
        if(!actual||(s->actors[i].row.owned&&(actual->owner!=s->options.owner||!actual->has_source||actual->source_slot!=s->actors[i].row.slot))||
            (s->actors[i].client&&(s->actors[i].row.owned||s->actors[i].row.slot>=64))) return fail(io,"Saved source binding lost its real owner or client");
        for(size_t j=0;j<i;++j) if(s->actors[j].row.slot==s->actors[i].row.slot||qa_actor_id_equal(s->actors[j].row.actor,s->actors[i].row.actor)) return fail(io,"Saved source bindings duplicate physical identity");
    }
    int32_t first=s->command_sequence>64?s->command_sequence-63:1;
    for(int64_t n=first;n<=s->command_sequence;++n) { component_source_command *row=s->commands+(uint32_t)n%64; if(!command(io,row)||row->sequence!=(int32_t)n) return fail(io,"Saved component command ring is incomplete"); }
    return publication(io,s,&s->current)&&publication(io,s,&s->baseline);
}
bool application_q3_component_source_checkpoint(application_q3_component_source *s,qa_buffer *out,qa_error *e)
{
    if(!s||!out||out->data||out->size||!application_q3_component_source_idle(s)) return false;
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,s->options.session,e)&&fields(&io,s)&&qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool application_q3_component_source_restore(application_q3_component_source *s,qa_bytes bytes,qa_error *e)
{
    if(!s||s->actors||s->revision||s->command_sequence||s->current.game_state||s->baseline.game_state||!application_q3_component_source_idle(s)) return false;
    qa_source_save_io io={0}; bool ok=qa_source_save_reader(&io,s->options.session,bytes,e)&&fields(&io,s)&&qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); return ok;
}
bool application_q3_component_source_validate(application_q3_component_source *s,qa_error *e)
{
    if(!s||!s->options.vm||!s->options.host||!s->options.current(s->options.context)||!application_q3_component_source_idle(s)) return false;
    for(size_t i=0;i<s->actor_count;++i) {
        uint32_t slot;
        if(!qa_q3_host_actor_slot(s->options.host,s->actors[i].row.actor,&slot,e)||slot!=s->actors[i].row.slot) return false;
    }
    return true;
}
