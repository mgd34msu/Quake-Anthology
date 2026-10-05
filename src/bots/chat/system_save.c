#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_chat_system_save.h"
static const uint8_t magic[8]={'Q','A','B','C','S','Y','S',2};
static bool text_field(qa_source_save_io *io,uint8_t *bytes,size_t capacity) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    const uint8_t *zero=reading?NULL:memchr(bytes,0,capacity);
    size_t size=reading?0:zero?(size_t)(zero-bytes):capacity;
    if(!qa_source_save_count(io,&size,capacity) || !qa_source_save_bytes(io,bytes,size)) return false;
    if(reading && size<capacity) bytes[size]=0;
    return true;
}
static bool word_field(qa_source_save_io *io,uint8_t *bytes,uint32_t offset) {
    uint32_t value=io->direction==QA_SOURCE_SAVE_READ?0:chat_raw_word(bytes+offset);
    if(!qa_source_save_u32(io,&value)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) chat_raw_store(bytes+offset,value);
    return true;
}
static bool time_field(qa_source_save_io *io,uint8_t *bytes,uint32_t offset) {
    uint32_t bits=io->direction==QA_SOURCE_SAVE_WRITE?chat_raw_word(bytes+offset):0;
    float value;memcpy(&value,&bits,sizeof(value));
    if(!qa_source_save_f32(io,&value)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {memcpy(&bits,&value,sizeof(bits));chat_raw_store(bytes+offset,bits);}
    return true;
}
static bool console_fields(qa_source_save_io *io,qa_bot_chat_system *system,uint32_t pointer) {
    qa_bot_memory_span bytes;if(!chat_console_span(system,pointer,&bytes,io->error)) return false;
    return word_field(io,bytes.data,0) && time_field(io,bytes.data,4) && word_field(io,bytes.data,8) &&
        text_field(io,bytes.data+12,256) && word_field(io,bytes.data,268) && word_field(io,bytes.data,272);
}
bool qa_bot_chat_system_state_index(const qa_bot_chat_system *system,const qa_bot_chat *state,size_t *out) {
    if(!system || !state || !out) return false;
    size_t index=0;for(const qa_bot_chat *current=system->states;current;current=current->next,++index)
        if(current==state) {*out=index;return true;}
    return false;
}
void qa_bot_chat_restored_states_free(qa_bot_chat_restored_states *states) {
    if(states) {free(states->states);*states=(qa_bot_chat_restored_states){0};}
}
static bool asset_field(qa_source_save_io *io,const qa_bot_chat_asset_save_refs *refs,
    qa_bot_chat_asset **asset,qa_bot_chat_asset_kind kind) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=!reading && *asset;uint64_t key=0;
    if(!qa_source_save_bool(io,&present)) return false;
    if(!present) {if(reading) *asset=NULL;return true;}
    bool ok=reading || ((*asset)->view.kind==kind && refs->encode(refs->context,*asset,&key,io->error));
    if(ok) ok=qa_source_save_u64(io,&key);
    if(ok && reading) {
        qa_bot_chat_asset *borrowed=NULL;ok=refs->decode(refs->context,key,&borrowed,io->error) && borrowed && borrowed->view.kind==kind;
        if(ok) {qa_bot_chat_asset_retain(borrowed);*asset=borrowed;}
    }
    if(!ok) {if(io->error && io->error->code!=QA_OK) io->failed=true;
        else return bot_save_fail(io,QA_ERROR_FORMAT,"Unqualified shared bot chat asset");}
    return ok;
}
static bool state_fields(qa_source_save_io *io,qa_bot_chat *state,const qa_bot_chat_asset_save_refs *refs) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!reading && !chat_state_initial(state,&state->initial,io->error)) return false;
    if(!asset_field(io,refs,&state->initial,QA_BOT_CHAT_INITIAL) ||
       !qa_source_save_u64(io,&state->initial_revision) || !state->initial_revision) return false;
    if(reading && !qa_bot_memory_allocate(state->system->memory,CHAT_STATE_BYTES,QA_BOT_MEMORY_HEAP,true,NULL,&state->allocation,io->error)) return false;
    qa_bot_memory_span bytes;if(!chat_state_span(state,&bytes,io->error)) return false;
    bool ok=word_field(io,bytes.data,CHAT_GENDER) && word_field(io,bytes.data,CHAT_CLIENT) &&
        text_field(io,bytes.data+CHAT_NAME,32) && text_field(io,bytes.data+CHAT_MESSAGE,256) &&
        word_field(io,bytes.data,CHAT_HANDLE) && word_field(io,bytes.data,CHAT_FIRST) &&
        word_field(io,bytes.data,CHAT_LAST) && word_field(io,bytes.data,CHAT_COUNT);
    if(ok && reading) chat_raw_store(bytes.data+CHAT_INITIAL,state->initial?state->initial->initial_source->initial.pointer:0);
    return ok;
}
static bool cache_fields(qa_source_save_io *io,qa_bot_chat_system *system,const qa_bot_chat_asset_save_refs *refs,size_t index) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;qa_bot_chat_asset *asset=NULL;
    if(!reading && system->initial_cache[index].owner) {
        qa_bot_memory_span bytes;
        if(!qa_bot_memory_bytes(system->memory,system->initial_cache[index],&bytes,io->error) || bytes.size!=132) return false;
        uint32_t pointer=chat_raw_word(bytes.data);
        for(qa_bot_chat_asset *row=system->library?system->library->chat_assets:NULL;pointer && row;row=row->next)
            if(row->initial_source && row->initial_source->initial.pointer==pointer) {asset=row;break;}
        if(pointer && !asset) return bot_save_fail(io,QA_ERROR_FORMAT,"Chat cache has no installed asset recipe");
    }
    if(!asset_field(io,refs,&asset,QA_BOT_CHAT_INITIAL)) return false;
    if(reading && asset) {
        qa_bot_memory_span bytes;
        bool ok=qa_bot_memory_allocate(system->memory,132,QA_BOT_MEMORY_HEAP,true,NULL,&system->initial_cache[index],io->error) &&
            qa_bot_memory_bytes(system->memory,system->initial_cache[index],&bytes,io->error);
        if(ok) {
            chat_raw_store(bytes.data,asset->initial_source->initial.pointer);
            size_t path=strlen(asset->view.path),name=strlen(asset->view.name);
            if(path>63) path=63;
            if(name>63) name=63;
            memcpy(bytes.data+4,asset->view.path,path);memcpy(bytes.data+68,asset->view.name,name);
        }
        qa_bot_chat_asset_release(asset);return ok;
    }
    return true;
}
static bool walk(const qa_bot_chat_system *system,uint32_t pointer,size_t *out,
    uint8_t *seen,qa_error *error) {
    size_t count=0;
    if(system->console_capacity) memset(seen,0,system->console_capacity);
    while(pointer) {
        uint32_t next,back;
        if(pointer>system->console_capacity || seen[pointer-1] ||
           !chat_console_get(system,pointer,272,&next,error) || !chat_console_get(system,pointer,268,&back,error) ||
           back>system->console_capacity) return false;
        seen[pointer-1]=1;pointer=next;++count;
    }
    *out=count;return true;
}
static bool topology_valid(const qa_bot_chat_system *system,size_t *console_count,qa_error *error) {
    if(!system->memory || system->console_capacity>=UINT32_MAX ||
       (system->console_capacity && !system->console)) goto invalid;
    uint8_t *seen=system->console_capacity?calloc(system->console_capacity,1):NULL;
    if(system->console_capacity && !seen) {qa_error_set(error,QA_ERROR_MEMORY,0,"Validating source console typed links");return false;}
    bool ok=true;size_t free_count=0,total=0;
    for(size_t index=0;ok && index<system->console_capacity;++index) {
        qa_bot_memory_span bytes;ok=chat_console_span(system,(uint32_t)index+1,&bytes,error);
        if(ok) ok=chat_raw_word(bytes.data+268)<=system->console_capacity && chat_raw_word(bytes.data+272)<=system->console_capacity;
        for(size_t prior=0;ok && prior<index;++prior) {
            const chat_console_cell *a=&system->console[index],*b=&system->console[prior];
            if(a->allocation.owner==b->allocation.owner && a->allocation.slot==b->allocation.slot &&
               a->allocation.generation==b->allocation.generation && a->offset==b->offset) ok=false;
        }
    }
    if(ok) ok=walk(system,system->free_console,&free_count,seen,error);
    const qa_bot_chat *previous=NULL;
    for(qa_bot_chat *state=system->states;ok && state;state=state->next) {
        qa_bot_memory_span bytes;qa_bot_chat_asset *initial;size_t count=0;
        ok=state->system==system && !state->retired && state->references==1 && state->previous==previous &&
            chat_state_span(state,&bytes,error) && chat_state_initial(state,&initial,error);
        if(ok) ok=walk(system,chat_raw_word(bytes.data+CHAT_FIRST),&count,seen,error);
        if(ok) ok=count==chat_raw_word(bytes.data+CHAT_COUNT) &&
            chat_raw_word(bytes.data+CHAT_LAST)<=system->console_capacity;
        total+=count;previous=state;
        for(qa_bot_chat *prior=system->states;ok && prior!=state;prior=prior->next)
            if(prior->allocation.owner==state->allocation.owner && prior->allocation.slot==state->allocation.slot &&
               prior->allocation.generation==state->allocation.generation) ok=false;
    }
    for(size_t index=0;ok && index<64;++index) if(system->initial_cache[index].owner) {
        qa_bot_memory_span bytes;ok=qa_bot_memory_bytes(system->memory,system->initial_cache[index],&bytes,error);
        uint32_t pointer=ok?chat_raw_word(bytes.data):0;bool found=!pointer;
        if(system->library) for(qa_bot_chat_asset *asset=system->library->chat_assets;!found && asset;asset=asset->next)
            found=asset->initial_source && asset->initial_source->published && asset->initial_source->initial.pointer==pointer;
        ok=ok && found;
        for(size_t prior=0;ok && prior<index;++prior) {
            qa_bot_memory_allocation a=system->initial_cache[index],b=system->initial_cache[prior];
            if(a.owner==b.owner && a.slot==b.slot && a.generation==b.generation) ok=false;
        }
    }
    free(seen);if(ok) {if(console_count) *console_count=total;return true;}
invalid:
    if(!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid source chat allocation/link topology");
    return false;
}
static bool system_fields(qa_source_save_io *io,qa_bot_chat_system *system,
    const qa_bot_chat_asset_save_refs *refs,size_t *states) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t capacity=system->console_capacity;uint32_t free_console=system->free_console;
    bool ok=asset_field(io,refs,&system->options.synonyms,QA_BOT_CHAT_SYNONYMS) &&
        asset_field(io,refs,&system->options.randoms,QA_BOT_CHAT_RANDOMS) && asset_field(io,refs,&system->options.matches,QA_BOT_CHAT_MATCHES) &&
        asset_field(io,refs,&system->options.replies,QA_BOT_CHAT_REPLIES) && qa_source_save_count(io,&system->options.console_capacity,UINT32_MAX-1u) &&
        qa_source_save_bool(io,&system->options.debug) && qa_source_save_bool(io,&system->options.console_unavailable) &&
        qa_source_save_u64(io,&system->revision) && qa_source_save_count(io,&capacity,UINT32_MAX/CHAT_CONSOLE_BYTES) &&
        qa_source_save_u32(io,&free_console) && qa_source_save_count(io,states,SIZE_MAX);
    if(ok && reading && capacity>(io->input.size-io->offset)/28)
        ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated saved bot console cells");
    if(ok && reading && capacity) ok=chat_console_heap(system,(uint32_t)capacity,true,io->error);
    if(ok && reading) system->free_console=free_console;
    for(size_t index=0;ok && index<64;++index) ok=cache_fields(io,system,refs,index);
    return ok;
}
static void scratch_clear(qa_bot_chat_system *scratch) {
    while(scratch->states) {qa_bot_chat *state=scratch->states;scratch->states=state->next;qa_bot_chat_asset_release(state->initial);free(state);}
    qa_bot_chat_asset_release(scratch->options.synonyms);qa_bot_chat_asset_release(scratch->options.randoms);
    qa_bot_chat_asset_release(scratch->options.matches);qa_bot_chat_asset_release(scratch->options.replies);
    free(scratch->console);if(scratch->memory) (void)qa_bot_memory_release(scratch->memory,NULL);
}
bool qa_bot_chat_system_capture(const qa_bot_chat_system *system,const qa_bot_chat_asset_save_refs *refs,qa_buffer *out,qa_error *error) {
    if(!system || !out || !refs || !refs->encode || system->retired || qa_bot_chat_system_active(system)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Bot chat capture requires its idle actual owner");return false;
    }
    size_t console_count=0;if(!topology_valid(system,&console_count,error)) return false;
    qa_bot_chat_system view=*system;view.console_count=console_count;
    size_t count=0;for(const qa_bot_chat *state=system->states;state;state=state->next) ++count;
    qa_source_save_io io={0};bool ok=qa_source_save_writer(&io,NULL,error) && bot_save_signature(&io,magic) && system_fields(&io,&view,refs,&count);
    for(size_t index=0;ok && index<system->console_capacity;++index) {
        ok=console_fields(&io,&view,(uint32_t)index+1);
    }
    for(const qa_bot_chat *state=system->states;ok && state;state=state->next) {qa_bot_chat copy=*state;ok=state_fields(&io,&copy,refs);}
    if(ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return ok;
}
bool qa_bot_chat_system_restore_bytes(qa_bot_chat_system *system,qa_bytes bytes,const qa_bot_chat_asset_save_refs *refs,
    qa_bot_chat_restored_states *out,qa_error *error) {
    if(!system || system->states || system->console_count || system->retired || qa_bot_chat_system_active(system) ||
       !refs || !refs->decode || !out || out->states || out->count) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat restore requires an empty idle owner");return false;
    }
    qa_bot_chat_system scratch={.references=1,.library=system->library};
    bool ok=system->library?qa_bot_memory_retain(system->memory,error):qa_bot_memory_create(NULL,&scratch.memory,error);
    if(system->library && ok) scratch.memory=system->memory;
    qa_bot_chat_restored_states states={0};qa_source_save_io io={0};system->restoring=true;
    if(ok) ok=qa_source_save_reader(&io,NULL,bytes,error) && bot_save_signature(&io,magic) && system_fields(&io,&scratch,refs,&states.count);
    if(ok && (scratch.console_capacity>SIZE_MAX/sizeof(*scratch.console) || states.count>SIZE_MAX/sizeof(*states.states) ||
       scratch.console_capacity>io.input.size-io.offset || states.count>io.input.size-io.offset)) ok=bot_save_fail(&io,QA_ERROR_FORMAT,"Oversized source chat alias maps");
    if(ok && states.count && !(states.states=calloc(states.count,sizeof(*states.states)))) ok=bot_save_fail(&io,QA_ERROR_MEMORY,"Restoring source chat states");
    for(size_t index=0;ok && index<scratch.console_capacity;++index) {
        ok=console_fields(&io,&scratch,(uint32_t)index+1);
    }
    qa_bot_chat **tail=&scratch.states,*previous=NULL;
    for(size_t index=0;ok && index<states.count;++index) {
        qa_bot_chat *state=calloc(1,sizeof(*state));
        if(!state) {ok=bot_save_fail(&io,QA_ERROR_MEMORY,"Restoring actual chat state aliases");break;}
        state->system=&scratch;state->references=1;state->previous=previous;*tail=state;tail=&state->next;previous=state;
        states.states[index]=state;++scratch.references;ok=state_fields(&io,state,refs);
    }
    if(ok) ok=qa_source_save_finish(&io,NULL) && topology_valid(&scratch,&scratch.console_count,error);
    if(ok) {
        qa_bot_chat_asset_release(system->options.synonyms);qa_bot_chat_asset_release(system->options.randoms);
        qa_bot_chat_asset_release(system->options.matches);qa_bot_chat_asset_release(system->options.replies);
        free(system->console);(void)qa_bot_memory_release(system->memory,NULL);
        system->options=scratch.options;system->memory=scratch.memory;system->console_heap=scratch.console_heap;
        system->console=scratch.console;system->free_console=scratch.free_console;system->console_count=scratch.console_count;
        system->console_capacity=scratch.console_capacity;system->states=scratch.states;system->references=scratch.references;system->revision=scratch.revision;
        memcpy(system->initial_cache,scratch.initial_cache,sizeof(system->initial_cache));
        for(qa_bot_chat *state=system->states;state;state=state->next) state->system=system;
        scratch.options=(qa_bot_chat_options){0};scratch.memory=NULL;scratch.console=NULL;scratch.states=NULL;
        *out=states;states=(qa_bot_chat_restored_states){0};
    }
    system->restoring=false;scratch_clear(&scratch);qa_bot_chat_restored_states_free(&states);qa_source_save_dispose(&io);return ok;
}
