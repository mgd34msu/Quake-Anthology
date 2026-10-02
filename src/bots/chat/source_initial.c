#include "source_initial.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error,const char *text) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",text);return false;
}
static uint32_t word(const uint8_t *bytes) {
    return (uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
}
static void store(uint8_t *bytes,uint32_t value) {
    for(uint32_t index=0;index<4;++index) bytes[index]=(uint8_t)(value>>(8*index));
}
static bool cell(const bot_chat_initial *chat,uint32_t offset,uint32_t size,
    qa_bot_memory_span *out,qa_error *error) {
    if(!chat || !out) return fail(error,"Initial chat view requires its actual allocation/output");
    if(!qa_bot_memory_bytes(chat->memory,chat->allocation,out,error)) return false;
    if(offset>out->size || size>out->size-offset)
        return fail(error,"Initial chat record exceeds its actual allocation");
    out->data+=offset;out->size=size;return true;
}
bool bot_chat_initial_bind(qa_bot_memory *memory,qa_bot_memory_allocation allocation,
    uint32_t pointer,bot_chat_initial *out,qa_error *error) {
    if(!out || !pointer) return fail(error,"Initial chat binding requires its pointer/output");
    bot_chat_initial chat={.memory=memory,.allocation=allocation,.pointer=pointer};
    qa_bot_memory_span bytes;
    if(!cell(&chat,0,BOT_CHAT_INITIAL_BYTES,&bytes,error)) return false;
    *out=chat;return true;
}
bool bot_chat_initial_allocate(qa_bot_memory *memory,uint32_t size,uint32_t pointer,
    bot_chat_initial *out,qa_error *error) {
    if(!out || !pointer || size<BOT_CHAT_INITIAL_BYTES)
        return fail(error,"Initial chat allocation requires its true size/pointer/output");
    qa_bot_memory_allocation allocation;
    if(!qa_bot_memory_allocate(memory,size,QA_BOT_MEMORY_HEAP,true,NULL,&allocation,error)) return false;
    return bot_chat_initial_bind(memory,allocation,pointer,out,error);
}
void bot_chat_initial_dispose(bot_chat_initial *chat) {
    if(!chat) return;
    free(chat->types);free(chat->messages);*chat=(bot_chat_initial){0};
}
bool bot_chat_initial_free(bot_chat_initial *chat,qa_error *error) {
    return chat?qa_bot_memory_free(chat->memory,chat->allocation,error):
        fail(error,"Initial chat free requires its actual owner");
}
static bool member(const uint32_t *pointers,size_t count,uint32_t pointer) {
    for(size_t index=0;index<count;++index) if(pointers[index]==pointer) return true;
    return false;
}
static bool register_pointer(uint32_t **pointers,size_t *count,size_t *capacity,
    uint32_t pointer,qa_error *error) {
    if(member(*pointers,*count,pointer)) return true;
    if(*count==*capacity) {
        size_t next=*capacity?*capacity*2:16;
        if(next<*capacity || next>SIZE_MAX/sizeof(**pointers))
            return fail(error,"Initial chat membership exceeds native address range");
        uint32_t *grown=realloc(*pointers,next*sizeof(*grown));
        if(!grown) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining actual initial chat view membership");return false;}
        *pointers=grown;*capacity=next;
    }
    (*pointers)[(*count)++]=pointer;return true;
}
bool bot_chat_initial_type(const bot_chat_initial *chat,uint32_t pointer,
    qa_bot_memory_span *out,qa_error *error) {
    if(!chat || !pointer || !member(chat->types,chat->type_count,pointer))
        return fail(error,"Initial chat pointer does not identify an allocated type");
    return cell(chat,pointer-1,BOT_CHAT_TYPE_BYTES,out,error);
}
bool bot_chat_initial_message(const bot_chat_initial *chat,uint32_t pointer,
    qa_bot_memory_span *out,qa_error *error) {
    if(!chat || !pointer || !member(chat->messages,chat->message_count,pointer))
        return fail(error,"Initial chat pointer does not identify an allocated message");
    return cell(chat,pointer-1,BOT_CHAT_MESSAGE_BYTES,out,error);
}
static bool type_link(const bot_chat_initial *chat,uint32_t pointer,qa_error *error) {
    qa_bot_memory_span bytes;
    return !pointer || bot_chat_initial_type(chat,pointer,&bytes,error);
}
static bool message_link(const bot_chat_initial *chat,uint32_t pointer,qa_error *error) {
    qa_bot_memory_span bytes;
    return !pointer || bot_chat_initial_message(chat,pointer,&bytes,error);
}
bool bot_chat_initial_first(const bot_chat_initial *chat,uint32_t *out,qa_error *error) {
    if(!out) return fail(error,"Initial chat first type requires its output");
    qa_bot_memory_span bytes;if(!cell(chat,0,4,&bytes,error)) return false;
    uint32_t pointer=word(bytes.data);
    if(!type_link(chat,pointer,error)) return false;
    *out=pointer;return true;
}
bool bot_chat_initial_text(const bot_chat_initial *chat,uint32_t offset,qa_bytes *out,
    qa_error *error) {
    if(!chat || !out) return fail(error,"Initial chat string requires its owner/output");
    qa_bot_memory_span bytes;
    if(!qa_bot_memory_bytes(chat->memory,chat->allocation,&bytes,error)) return false;
    if(offset>=bytes.size) return fail(error,"Initial chat string starts outside its actual allocation");
    const uint8_t *end=memchr(bytes.data+offset,0,bytes.size-offset);
    if(!end) return fail(error,"Initial chat string has no terminator before allocation end");
    *out=(qa_bytes){bytes.data+offset,(size_t)(end-(bytes.data+offset))};return true;
}
bool bot_chat_initial_type_add(bot_chat_initial *chat,uint32_t offset,qa_bytes name,
    uint32_t *out,qa_error *error) {
    if(!chat || !out || (name.size && !name.data))
        return fail(error,"Initial chat type publication requires its source name/output");
    qa_bot_memory_span bytes;
    if(!cell(chat,offset,BOT_CHAT_TYPE_BYTES,&bytes,error) ||
       !register_pointer(&chat->types,&chat->type_count,&chat->type_capacity,offset+1,error)) return false;
    size_t copied=name.size<32?name.size:32;
    if(copied) memcpy(bytes.data,name.data,copied);
    memset(bytes.data+copied,0,32-copied);store(bytes.data+36,0);
    uint32_t previous;
    if(!bot_chat_initial_first(chat,&previous,error)) return false;
    store(bytes.data+40,previous);
    qa_bot_memory_span header;if(!cell(chat,0,4,&header,error)) return false;
    store(header.data,offset+1);*out=offset+1;return true;
}
bool bot_chat_initial_message_add(bot_chat_initial *chat,uint32_t type,uint32_t offset,
    uint32_t *out,qa_error *error) {
    if(!chat || !out) return fail(error,"Initial chat message publication requires its owner/output");
    qa_bot_memory_span bytes;
    if(!cell(chat,offset,BOT_CHAT_MESSAGE_BYTES,&bytes,error) ||
       !register_pointer(&chat->messages,&chat->message_count,&chat->message_capacity,offset+1,error)) return false;
    float time=-40;uint32_t bits;memcpy(&bits,&time,4);store(bytes.data+4,bits);
    uint32_t previous;
    if(!bot_chat_initial_type_first(chat,type,&previous,error)) return false;
    store(bytes.data+8,previous);
    qa_bot_memory_span parent;if(!bot_chat_initial_type(chat,type,&parent,error)) return false;
    store(parent.data+36,offset+1);store(bytes.data,offset+13);*out=offset+1;return true;
}
bool bot_chat_initial_message_write(bot_chat_initial *chat,uint32_t type,uint32_t offset,
    qa_bytes text,qa_error *error) {
    if(text.size>=UINT32_MAX || (text.size && !text.data))
        return fail(error,"Initial chat message source extent is invalid");
    qa_bot_memory_span bytes;
    if(!cell(chat,offset,(uint32_t)text.size+1,&bytes,error)) return false;
    if(text.size) memcpy(bytes.data,text.data,text.size);
    bytes.data[text.size]=0;
    qa_bot_memory_span parent;if(!bot_chat_initial_type(chat,type,&parent,error)) return false;
    store(parent.data+32,word(parent.data+32)+1);return true;
}
bool bot_chat_initial_type_next(const bot_chat_initial *chat,uint32_t type,uint32_t *out,
    qa_error *error) {
    if(!out) return fail(error,"Initial chat type successor requires its output");
    qa_bot_memory_span bytes;if(!bot_chat_initial_type(chat,type,&bytes,error)) return false;
    uint32_t pointer=word(bytes.data+40);if(!type_link(chat,pointer,error)) return false;
    *out=pointer;return true;
}
bool bot_chat_initial_type_first(const bot_chat_initial *chat,uint32_t type,uint32_t *out,
    qa_error *error) {
    if(!out) return fail(error,"Initial chat first message requires its output");
    qa_bot_memory_span bytes;if(!bot_chat_initial_type(chat,type,&bytes,error)) return false;
    uint32_t pointer=word(bytes.data+36);if(!message_link(chat,pointer,error)) return false;
    *out=pointer;return true;
}
bool bot_chat_initial_type_count(const bot_chat_initial *chat,uint32_t type,int32_t *out,
    qa_error *error) {
    if(!out) return fail(error,"Initial chat message count requires its output");
    qa_bot_memory_span bytes;if(!bot_chat_initial_type(chat,type,&bytes,error)) return false;
    uint32_t value=word(bytes.data+32);memcpy(out,&value,4);return true;
}
bool bot_chat_initial_message_next(const bot_chat_initial *chat,uint32_t message,uint32_t *out,
    qa_error *error) {
    if(!out) return fail(error,"Initial chat message successor requires its output");
    qa_bot_memory_span bytes;if(!bot_chat_initial_message(chat,message,&bytes,error)) return false;
    uint32_t pointer=word(bytes.data+8);if(!message_link(chat,pointer,error)) return false;
    *out=pointer;return true;
}
bool bot_chat_initial_message_text(const bot_chat_initial *chat,uint32_t message,qa_bytes *out,
    qa_error *error) {
    qa_bot_memory_span bytes;if(!bot_chat_initial_message(chat,message,&bytes,error)) return false;
    return bot_chat_initial_text(chat,word(bytes.data)-1,out,error);
}
bool bot_chat_initial_message_time(const bot_chat_initial *chat,uint32_t message,float *value,
    bool write,qa_error *error) {
    if(!value) return fail(error,"Initial chat message time requires its value");
    qa_bot_memory_span bytes;if(!bot_chat_initial_message(chat,message,&bytes,error)) return false;
    uint32_t bits;
    if(write) {memcpy(&bits,value,4);store(bytes.data+4,bits);}
    else {bits=word(bytes.data+4);memcpy(value,&bits,4);}
    return true;
}
