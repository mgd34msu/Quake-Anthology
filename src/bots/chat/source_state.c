#include "internal.h"

uint32_t chat_raw_word(const uint8_t *bytes) {
    return (uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
}
void chat_raw_store(uint8_t *bytes,uint32_t value) {
    for(uint32_t index=0;index<4;++index) bytes[index]=(uint8_t)(value>>(index*8));
}
bool chat_state_span(const qa_bot_chat *state,qa_bot_memory_span *out,qa_error *error) {
    if(!state || !state->system || !state->system->memory ||
       !qa_bot_memory_bytes(state->system->memory,state->allocation,out,error)) return false;
    if(out->size!=CHAT_STATE_BYTES) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat state does not alias its source 316-byte heap");return false;
    }
    return true;
}
bool chat_state_get(const qa_bot_chat *state,uint32_t offset,uint32_t *out,qa_error *error) {
    qa_bot_memory_span span;
    if(!out || offset>CHAT_STATE_BYTES-4 || !chat_state_span(state,&span,error)) return false;
    *out=chat_raw_word(span.data+offset);return true;
}
bool chat_state_set(qa_bot_chat *state,uint32_t offset,uint32_t value,qa_error *error) {
    qa_bot_memory_span span;
    if(offset>CHAT_STATE_BYTES-4 || !chat_state_span(state,&span,error)) return false;
    chat_raw_store(span.data+offset,value);return true;
}
bool chat_state_text(const qa_bot_chat *state,uint32_t offset,uint32_t size,char **out,qa_error *error) {
    qa_bot_memory_span span;
    if(!out || offset>CHAT_STATE_BYTES || size>CHAT_STATE_BYTES-offset ||
       !chat_state_span(state,&span,error)) return false;
    if(memchr(span.data+offset,0,size)) {*out=(char *)span.data+offset;return true;}
    qa_bot_chat *view=(qa_bot_chat *)state;
    char *projection=offset==CHAT_NAME && size==32?view->name_projection:
        offset==CHAT_MESSAGE && size==256?view->message_projection:NULL;
    if(!projection) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Unknown bounded chat string field");return false;
    }
    memcpy(projection,span.data+offset,size);projection[size]=0;
    *out=projection;return true;
}
bool chat_state_message_strip(qa_bot_chat *state,qa_error *error) {
    qa_bot_memory_span span;
    if(!chat_state_span(state,&span,error)) return false;
    uint8_t *message=span.data+CHAT_MESSAGE;size_t length=0,written=0;
    while(length<256 && message[length]) ++length;
    for(size_t index=0;index<length;++index)
        if(message[index]!='~') message[written++]=message[index];
    if(written>=256) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Expanded chat exceeds the source 256-byte buffer");return false;
    }
    message[written]=0;return true;
}
bool chat_state_message_clear(qa_bot_chat *state,qa_error *error) {
    qa_bot_memory_span span;
    if(!chat_state_span(state,&span,error)) return false;
    span.data[CHAT_MESSAGE]=0;return true;
}
bool chat_console_span(const qa_bot_chat_system *system,uint32_t pointer,qa_bot_memory_span *out,qa_error *error) {
    if(!system || !pointer || pointer>system->console_capacity) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat console pointer is outside its retained typed heap views");return false;
    }
    const chat_console_cell *cell=&system->console[pointer-1];qa_bot_memory_span allocation;
    if(!qa_bot_memory_bytes(system->memory,cell->allocation,&allocation,error)) return false;
    if(cell->offset>allocation.size || CHAT_CONSOLE_BYTES>allocation.size-cell->offset) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat console view exceeds its true hunk allocation");return false;
    }
    *out=(qa_bot_memory_span){allocation.data+cell->offset,CHAT_CONSOLE_BYTES};return true;
}
bool chat_console_get(const qa_bot_chat_system *system,uint32_t pointer,uint32_t offset,uint32_t *out,qa_error *error) {
    qa_bot_memory_span span;
    if(!out || offset>CHAT_CONSOLE_BYTES-4 || !chat_console_span(system,pointer,&span,error)) return false;
    *out=chat_raw_word(span.data+offset);return true;
}
bool chat_console_set(qa_bot_chat_system *system,uint32_t pointer,uint32_t offset,uint32_t value,qa_error *error) {
    qa_bot_memory_span span;
    if(offset>CHAT_CONSOLE_BYTES-4 || !chat_console_span(system,pointer,&span,error)) return false;
    chat_raw_store(span.data+offset,value);return true;
}
bool chat_console_heap(qa_bot_chat_system *system,uint32_t count,bool replace,qa_error *error) {
    if(!system || !count || count>UINT32_MAX/CHAT_CONSOLE_BYTES ||
       count>UINT32_MAX-system->console_capacity ||
       system->console_capacity+count>SIZE_MAX/sizeof(*system->console)) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Source console hunk exceeds its word extent");return false;
    }
    size_t first=system->console_capacity,total=first+count;
    chat_console_cell *cells=realloc(system->console,total*sizeof(*cells));
    if(!cells) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining historical source console views");return false;}
    system->console=cells;
    qa_bot_memory_allocation allocation;
    if(!qa_bot_memory_allocate(system->memory,count*CHAT_CONSOLE_BYTES,QA_BOT_MEMORY_HUNK,true,NULL,&allocation,error)) return false;
    system->console_heap=allocation;
    uint32_t old_free=replace?0:system->free_console;
    for(uint32_t index=0;index<count;++index) {
        uint32_t pointer=(uint32_t)first+index+1;
        cells[first+index]=(chat_console_cell){allocation,index*CHAT_CONSOLE_BYTES};
        system->console_capacity=first+index+1;
        if(!chat_console_set(system,pointer,268,index?pointer-1:0,error) ||
           !chat_console_set(system,pointer,272,index+1<count?pointer+1:old_free,error)) return false;
    }
    if(old_free && !chat_console_set(system,old_free,268,(uint32_t)total,error)) return false;
    system->free_console=(uint32_t)first+1;return true;
}
bool chat_state_initial(const qa_bot_chat *state,qa_bot_chat_asset **out,qa_error *error) {
    uint32_t pointer;*out=NULL;
    if(!chat_state_get(state,CHAT_INITIAL,&pointer,error)) return false;
    if(!pointer) return true;
    if(state->initial && state->initial->initial_source &&
       state->initial->initial_source->initial.pointer==pointer) {*out=state->initial;return true;}
    if(state->system->library)
        for(qa_bot_chat_asset *asset=state->system->library->chat_assets;asset;asset=asset->next)
            if(asset->initial_source && asset->initial_source->published &&
               asset->initial_source->initial.pointer==pointer) {*out=asset;return true;}
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Chat state initial pointer has no retained allocation view");return false;
}
