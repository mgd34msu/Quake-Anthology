#include "guest_q3_scene_private.h"
#include "qa/q3_abi.h"
#include "qa/q3_host_save.h"
#include <limits.h>

static bool blob(qa_source_save_io *io, qa_buffer *b, size_t maximum)
{
    size_t n=b->size;
    if(!qa_source_save_count(io,&n,maximum)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(b->data) return q3scene_fail(io->error,QA_ERROR_ARGUMENT,"Component import buffer is not empty");
        b->data=n?malloc(n):NULL; b->size=n;
        if(n&&!b->data) return q3scene_fail(io->error,QA_ERROR_MEMORY,"Retaining component continuation bytes");
    }
    return qa_source_save_bytes(io,b->data,n);
}
typedef struct record_writer { qa_buffer bytes; } record_writer;
static bool actor_reference(qa_source_save_io *io,application_q3_scene *s,qa_actor_id *value)
{
    if(!s->options.actor_encode&&!s->options.actor_decode) return qa_source_save_actor(io,value);
    if(!s->options.actor_encode||!s->options.actor_decode)
        return q3scene_fail(io->error,QA_ERROR_ARGUMENT,"Component actor codec requires both real identity directions");
    bool present=io->direction==QA_SOURCE_SAVE_WRITE&&value->registry;
    qa_saved_actor_id saved={0};
    if(present&&!s->options.actor_encode(s->options.actor_codec_context,*value,&saved,io->error)) return false;
    if(!qa_source_save_bool(io,&present)||!qa_source_save_u32(io,&saved.slot)||!qa_source_save_u64(io,&saved.generation)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(!present) {
            if(saved.slot||saved.generation) return q3scene_fail(io->error,QA_ERROR_FORMAT,"Absent component actor contains wire provenance");
            *value=(qa_actor_id){0};
        } else if(!s->options.actor_decode(s->options.actor_codec_context,saved,value,io->error)) return false;
    }
    return true;
}
static bool record_write(void *context,size_t at,qa_bytes value,qa_error *e)
{
    record_writer *w=context;
    if(at>w->bytes.size||value.size>w->bytes.size-at) return q3scene_fail(e,QA_ERROR_FORMAT,"Component record writer leaves its source ABI");
    if(value.size) memcpy(w->bytes.data+at,value.data,value.size);
    return true;
}
static bool game_state(qa_source_save_io *io,application_q3_scene *s)
{
    uint8_t bytes[20100]={0}; record_writer w={.bytes={bytes,sizeof(bytes)}};
    qa_q3_abi_record record={.abi=s->options.profile->abi,.bytes={bytes,sizeof(bytes)},.context=&w,.write=record_write};
    if(io->direction==QA_SOURCE_SAVE_WRITE&&!qa_q3_abi_write_gamestate(&record,0,true,s->game_state,io->error)) return false;
    if(!qa_source_save_bytes(io,bytes,sizeof(bytes))) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        uint32_t count=qa_load_u32le(bytes+20096);
        if(count>16000) return q3scene_fail(io->error,QA_ERROR_FORMAT,"Component gameState exceeds its source strings");
        for(size_t i=0;i<1024;++i) {
            uint32_t offset=qa_load_u32le(bytes+i*4);
            if(offset>=(count?count:1)||offset>UINT16_MAX ||
                (offset&& !memchr(bytes+4096+offset,0,count-offset)))
                return q3scene_fail(io->error,QA_ERROR_FORMAT,"Component gameState string pointer is invalid");
            s->game_state->config_offsets[i]=(uint16_t)offset;
        }
        memcpy(s->game_state->strings,bytes+4096,16000); s->game_state->string_bytes=count;
    }
    return true;
}
static bool snapshot(qa_source_save_io *io,application_q3_scene *s,q3scene_snapshot *row)
{
    size_t size=qa_qvm_snapshot_bytes(s->options.profile->abi);
    record_writer w={.bytes={calloc(1,size),size}};
    if(!w.bytes.data) return q3scene_fail(io->error,QA_ERROR_MEMORY,"Retaining component snapshot record");
    qa_q3_abi_record r={.abi=s->options.profile->abi,.bytes={w.bytes.data,size},.context=&w,.write=record_write};
    bool ok=io->direction==QA_SOURCE_SAVE_READ||qa_q3_abi_write_snapshot(&r,0,true,&row->value,0,io->error);
    if(ok) ok=qa_source_save_bytes(io,w.bytes.data,size);
    if(ok&&io->direction==QA_SOURCE_SAVE_READ) {
        size_t ps=qa_qvm_player_bytes(r.abi), es=qa_qvm_entity_bytes(r.abi);
        int32_t count=qa_load_i32le(w.bytes.data+44+ps);
        if(count<0||count>256) ok=q3scene_fail(io->error,QA_ERROR_FORMAT,"Component snapshot entity count is invalid");
        if(ok) {
            row->value=(qa_q3_snapshot){.valid=true,.server_time=qa_load_i32le(w.bytes.data+8),
                .flags=w.bytes.data[0],.area_bytes=32,.server_command_number=qa_load_i32le(w.bytes.data+size-4),.entity_count=(size_t)count};
            memcpy(row->value.area_mask,w.bytes.data+12,32);
            row->entities=count?calloc((size_t)count,sizeof(*row->entities)):NULL;
            if(count&&!row->entities) ok=q3scene_fail(io->error,QA_ERROR_MEMORY,"Retaining imported component entities");
            row->value.entities=row->entities;
            if(ok) ok=qa_q3_abi_read_player(&r,44,true,&row->value.player,io->error);
            for(size_t i=0;ok&&i<(size_t)count;++i) ok=qa_q3_abi_read_entity(&r,48+ps+i*es,true,row->entities+i,io->error);
        }
    }
    qa_buffer_free(&w.bytes); return ok;
}
static bool tokens(qa_source_save_io *io,qa_command_tokens *t)
{
    size_t count=t->count;
    if(!qa_source_save_count(io,&count,INT32_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        t->values=count?calloc(count,sizeof(*t->values)):NULL; t->count=count;
        if(count&&!t->values) return q3scene_fail(io->error,QA_ERROR_MEMORY,"Retaining imported component argv");
    }
    for(size_t i=0;i<count;++i) {
        const char *value=io->direction==QA_SOURCE_SAVE_WRITE?t->values[i]:NULL;
        if(!qa_source_save_text(io,&value)||!value) return false;
        if(io->direction==QA_SOURCE_SAVE_READ) t->values[i]=(char *)value;
    }
    const char *tail=io->direction==QA_SOURCE_SAVE_WRITE?(t->args_text?t->args_text:""):NULL;
    if(!qa_source_save_text(io,&tail)||!tail) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) t->args_text=(char *)tail;
    /* Decoded strings borrow the genuine restored session string table;
     * tokens_free frees only values/storage, never these borrowed strings. */
    return true;
}
static bool fields(qa_source_save_io *io,application_q3_scene *s,qa_buffer *vm,qa_buffer *body,qa_qvm_binding *event)
{
    uint8_t magic[8]={'Q','A','G','3','S','C',0,0}; uint32_t version=4;
    qa_sha256_digest declaration=s->options.profile->declaration_digest;
    if(!qa_source_save_bytes(io,magic,8)||memcmp(magic,"QAG3SC\0\0",8)||!qa_source_save_u32(io,&version)||version!=4||
        !qa_source_save_bytes(io,declaration.bytes,32)||!qa_sha256_equal(&declaration,&s->options.profile->declaration_digest)||
        !qa_source_save_u64(io,event)||(!s->options.profile->player_events&&!*event)||(s->options.profile->player_events&&*event)||!blob(io,body,SIZE_MAX)||!blob(io,vm,SIZE_MAX)||
        !qa_source_save_u64(io,&s->context.generation)||!qa_source_save_i64(io,&s->revision)||s->revision<0||
        !qa_source_save_i64(io,&s->scene_revision)||s->scene_revision<0||
        !qa_source_save_i32(io,&s->context.time_ms)||s->context.time_ms<0||
        !qa_source_save_i32(io,&s->context.frame_ms)||s->context.frame_ms<0||
        !qa_source_save_bool(io,&s->context.baseline)||!qa_source_save_bool(io,&s->context.has_weapon_presented)||
        !qa_source_save_bool(io,&s->context.weapon_presented)||
        !qa_source_save_i32(io,&s->context.client_number)||s->context.client_number<0||
        (uint32_t)s->context.client_number>=s->options.profile->capacity||
        !qa_source_save_vec3(io,&s->context.origin)||!qa_vec_finite(s->context.origin)) return false;
    for(size_t i=0;i<3;++i) if(!qa_source_save_vec3(io,s->context.axis+i)||!qa_vec_finite(s->context.axis[i])) return false;
    if(!qa_source_save_bool(io,&s->event_present)||!qa_source_save_u64(io,&s->event_sequence)||
        (!s->event_present&&s->event_sequence)||(s->event_present&&!s->options.profile->player_events)||
        !qa_source_save_bool(io,&s->frame_present)||!qa_source_save_u64(io,&s->frame)||
        !qa_source_save_bool(io,&s->hud_present)||!qa_source_save_u64(io,&s->hud_frame)||
        (s->hud_present&&(!s->frame_present||s->hud_frame>s->frame))||!game_state(io,s)||
        !blob(io,&s->defaults,(size_t)s->options.profile->stride*s->options.profile->capacity)||
        s->defaults.size!=(size_t)s->options.profile->stride*s->options.profile->capacity) return false;
    for(size_t i=0;i<s->options.profile->capacity;++i) if(!actor_reference(io,s,s->players+i)) return false;
    if(!qa_source_save_count(io,&s->actor_count,s->options.profile->capacity)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        s->actors=s->actor_count?calloc(s->actor_count,sizeof(*s->actors)):NULL;
        if(s->actor_count&&!s->actors) return q3scene_fail(io->error,QA_ERROR_MEMORY,"Retaining component actor map");
    }
    for(size_t i=0;i<s->actor_count;++i) {
        application_q3_scene_actor *r=s->actors+i;
        if(!qa_source_save_u32(io,&r->slot)||r->slot>=s->options.profile->capacity||
            !actor_reference(io,s,&r->actor)||!r->actor.registry||!qa_source_save_bool(io,&r->owned)||
            !qa_actor_id_equal(s->players[r->slot],r->actor)) return false;
        for(size_t j=0;j<i;++j) if(s->actors[j].slot==r->slot) return q3scene_fail(io->error,QA_ERROR_FORMAT,"Component actor map repeats a source slot");
    }
    if(!qa_source_save_i32(io,&s->snapshot_number)||s->snapshot_number<0) return false;
    for(size_t i=0;i<32;++i) {
        q3scene_snapshot *r=s->snapshots+i;
        if(!qa_source_save_i32(io,&r->number)||r->number<0||r->number>s->snapshot_number||
            (r->number&&((int64_t)r->number<=(int64_t)s->snapshot_number-32||(uint32_t)r->number%32!=i))) return false;
        if(r->number&&!snapshot(io,s,r)) return false;
    }
    if(s->snapshot_number&&s->snapshots[(uint32_t)s->snapshot_number%32].number!=s->snapshot_number) return false;
    for(size_t i=0;i<64;++i) {
        q3scene_command *r=s->commands+i;
        int32_t latest=s->snapshots[(uint32_t)s->snapshot_number%32].value.server_command_number;
        if(!qa_source_save_i32(io,&r->sequence)||r->sequence< -1||
            (r->sequence>=0&&((uint32_t)r->sequence%64!=i||r->sequence>latest||
                (int64_t)r->sequence<=(int64_t)latest-64))) return false;
        if(r->sequence>=0&&!tokens(io,&r->tokens)) return false;
    }
    return tokens(io,&s->reached);
}
bool application_q3_scene_checkpoint(application_q3_scene *s,qa_buffer *out,qa_error *e)
{
    qa_qvm_saved_function descriptors[3]; qa_buffer vm={0},body={0};
    if(!out||out->data||out->size||!s||!s->initialized||s->failed||s->restoring||
        !q3scene_descriptors(s,descriptors,e)||!qa_qvm_checkpoint_functions(s->vm,descriptors,s->options.profile->player_events?0:3,e))
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component checkpoint requires its complete physical callback owner");
    qa_source_save_io io={0}; qa_qvm_binding event=s->event_binding;
    bool ok=qa_q3_host_checkpoint_portable_ready(s->host,e)&&
        (s->options.profile->player_events||application_q3_component_body_checkpoint(s->body,&body,e))&&qa_qvm_checkpoint(s->vm,&vm,e)&&
        qa_source_save_writer(&io,s->options.host.session,e)&&fields(&io,s,&vm,&body,&event)&&qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&vm); qa_buffer_free(&body); return ok;
}
bool application_q3_scene_restore(application_q3_scene *s,qa_bytes bytes,qa_error *e)
{
    if(!s||!s->restoring||s->initialized||!application_q3_scene_idle(s)||s->defaults.data)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component restore requires its unentered isolated constructor");
    qa_buffer vm={0},body={0}; qa_qvm_binding saved[3]={0}; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,s->options.host.session,bytes,e)&&fields(&io,s,&vm,&body,saved+2)&&qa_source_save_finish(&io,NULL);
    qa_qvm_saved_function descriptors[3];
    if(ok) ok=(s->options.profile->player_events?body.size==0:application_q3_component_body_saved_read(&s->options.profile->body,(qa_bytes){body.data,body.size},saved,e))&&
        q3scene_descriptors(s,descriptors,e)&&qa_qvm_restore_candidate_bindings(s->vm,(qa_bytes){vm.data,vm.size},descriptors,saved,s->options.profile->player_events?0:3,e);
    if(ok) {
        /* Binding admission proved the QAVM2 memory and host extents.
         * Validate the host stream before the actual VM import can reopen it. */
        size_t memory=qa_qvm_memory_size(s->vm);
        ok=qa_q3_host_checkpoint_portable_state((qa_bytes){vm.data+160+memory,vm.size-160-memory},e);
    }
    if(ok) {
        if(s->body) application_q3_component_body_adopt(s->body,saved);
        s->event_binding=saved[2];
        ok=qa_qvm_restore_candidate(s->vm,(qa_bytes){vm.data,vm.size},e);
    }
    if(ok) {
        s->context.revision=s->scene_revision; s->context.game_state_revision=s->revision;
        s->context.game_state=s->game_state; s->context.snapshot=s->snapshot_number?&s->snapshots[(uint32_t)s->snapshot_number%32].value:NULL;
        s->context.actors=s->actors; s->context.actor_count=s->actor_count;
        s->initialized=true;
    } else s->failed=true;
    qa_source_save_dispose(&io); qa_buffer_free(&vm); qa_buffer_free(&body); return ok;
}
bool application_q3_scene_finish_restore(application_q3_scene *s,qa_error *e)
{
    if(!s||!s->restoring||!s->initialized||s->failed||!application_q3_scene_idle(s)||!q3scene_current(s))
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component continuation requires its real candidate source graph");
    if(!qa_q3_host_finish_restore(s->host,e)) return false;
    s->restoring=false; return true;
}
