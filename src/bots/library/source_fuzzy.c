#include "source_fuzzy.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static uint32_t read_word(const uint8_t *at) {
    return (uint32_t)at[0]|((uint32_t)at[1]<<8)|((uint32_t)at[2]<<16)|((uint32_t)at[3]<<24);
}
static void write_word(uint8_t *at,uint32_t value) {
    for(uint32_t i=0;i<4;++i) at[i]=(uint8_t)(value>>(i*8));
}
static bool live(const bot_fuzzy_heap *heap,qa_error *error) {
    return heap && heap->memory && !qa_bot_memory_disposed(heap->memory) ? true :
        fail(error,"Fuzzy heap requires its actual live memory owner");
}
static bool pointer_span(const bot_fuzzy_heap *heap,uint32_t pointer,bot_fuzzy_pointer_kind kind,
    qa_bot_memory_span *out,qa_error *error) {
    if(!live(heap,error)) return false;
    const bot_fuzzy_pointer *row=heap->first;
    while(row && row->pointer!=pointer) row=row->next;
    if(!row || row->kind!=kind) return fail(error,"Invalid fuzzy heap pointer");
    return qa_bot_memory_bytes(heap->memory,row->allocation,out,error);
}
static bool publish(bot_fuzzy_heap *heap,bot_fuzzy_pointer_kind kind,qa_bot_memory_allocation allocation,
    uint32_t *out,qa_error *error) {
    if(!heap->next_pointer || heap->next_pointer>UINT32_MAX)
        return fail(error,"Fuzzy heap pointer identities exhausted");
    bot_fuzzy_pointer *row=calloc(1,sizeof(*row));
    if(!row) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining fuzzy source pointer");return false;}
    *row=(bot_fuzzy_pointer){(uint32_t)heap->next_pointer++,kind,allocation,NULL};
    if(heap->last) heap->last->next=row;else heap->first=row;
    heap->last=row;*out=row->pointer;return true;
}
bool bot_fuzzy_heap_bind(bot_fuzzy_heap *heap,qa_bot_memory *memory,qa_error *error) {
    if(!heap || heap->memory || heap->first || heap->last || heap->next_pointer ||
       !memory || qa_bot_memory_disposed(memory)) return fail(error,"Fuzzy heap binding requires its fresh owner");
    if(!qa_bot_memory_retain(memory,error)) return false;
    heap->memory=memory;heap->next_pointer=1;return true;
}
bool bot_fuzzy_heap_clear(bot_fuzzy_heap *heap,qa_error *error) {
    if(!heap) return fail(error,"Fuzzy heap clear requires its actual owner");
    if(heap->memory && !qa_bot_memory_release(heap->memory,error)) return false;
    while(heap->first) {bot_fuzzy_pointer *row=heap->first;heap->first=row->next;free(row);}
    *heap=(bot_fuzzy_heap){0};return true;
}
static size_t terminated(qa_bytes bytes) {
    const uint8_t *zero=bytes.size?memchr(bytes.data,0,bytes.size):NULL;
    return zero?(size_t)(zero-bytes.data):bytes.size;
}
bool bot_fuzzy_name_allocate(bot_fuzzy_heap *heap,qa_bytes name,uint32_t *out,qa_error *error) {
    if(!live(heap,error) || !out || (name.size && !name.data)) return fail(error,"Fuzzy name allocation requires source bytes/output");
    size_t size=terminated(name);
    if(size>INT32_MAX-5) return fail(error,"Fuzzy name exceeds the source allocation domain");
    uint8_t *encoded=size?malloc(size):NULL;
    if(size && !encoded) {qa_error_set(error,QA_ERROR_MEMORY,0,"Encoding fuzzy source name");return false;}
    if(size) memcpy(encoded,name.data,size);
    qa_bot_memory_allocation allocation;
    if(!qa_bot_memory_allocate(heap->memory,(uint32_t)size+1,QA_BOT_MEMORY_HEAP,true,NULL,&allocation,error)) {free(encoded);return false;}
    qa_bot_memory_span bytes;
    if(!qa_bot_memory_bytes(heap->memory,allocation,&bytes,error)) {free(encoded);return false;}
    if(size) memcpy(bytes.data,encoded,size);
    free(encoded);
    return publish(heap,BOT_FUZZY_NAME,allocation,out,error);
}
bool bot_fuzzy_name_read(const bot_fuzzy_heap *heap,uint32_t pointer,qa_bytes *out,qa_error *error) {
    if(!out) return fail(error,"Fuzzy name read requires its byte output");
    qa_bot_memory_span bytes;if(!pointer_span(heap,pointer,BOT_FUZZY_NAME,&bytes,error)) return false;
    *out=(qa_bytes){bytes.data,terminated((qa_bytes){bytes.data,bytes.size})};return true;
}
bool bot_fuzzy_pointer_free(bot_fuzzy_heap *heap,uint32_t pointer,bot_fuzzy_pointer_kind kind,qa_error *error) {
    if(!live(heap,error)) return false;
    bot_fuzzy_pointer **link=&heap->first,*previous=NULL;
    while(*link && (*link)->pointer!=pointer) {previous=*link;link=&(*link)->next;}
    bot_fuzzy_pointer *row=*link;
    if(!row || row->kind!=kind) return fail(error,"Invalid fuzzy heap release pointer");
    if(!qa_bot_memory_free(heap->memory,row->allocation,error)) return false;
    *link=row->next;if(heap->last==row) heap->last=previous;free(row);return true;
}
static bool separator_span(const bot_fuzzy_separator *separator,qa_bot_memory_span *out,qa_error *error) {
    if(!separator) return fail(error,"Fuzzy separator record is absent");
    if(!pointer_span(separator->heap,separator->pointer,BOT_FUZZY_SEPARATOR,out,error)) return false;
    return out->size==BOT_FUZZY_SEPARATOR_BYTES ? true : fail(error,"Fuzzy separator allocation size mismatch");
}
bool bot_fuzzy_separator_allocate(bot_fuzzy_heap *heap,bot_fuzzy_separator *out,qa_error *error) {
    if(!live(heap,error) || !out) return fail(error,"Fuzzy separator allocation requires its output");
    qa_bot_memory_allocation allocation;uint32_t pointer;
    if(!qa_bot_memory_allocate(heap->memory,BOT_FUZZY_SEPARATOR_BYTES,QA_BOT_MEMORY_HEAP,true,NULL,&allocation,error) ||
       !publish(heap,BOT_FUZZY_SEPARATOR,allocation,&pointer,error)) return false;
    *out=(bot_fuzzy_separator){heap,pointer};return true;
}
bool bot_fuzzy_separator_bind(bot_fuzzy_heap *heap,uint32_t pointer,bot_fuzzy_separator *out,qa_error *error) {
    if(!out) return fail(error,"Fuzzy separator binding requires its output");
    bot_fuzzy_separator separator={heap,pointer};qa_bot_memory_span bytes;
    if(!separator_span(&separator,&bytes,error)) return false;
    *out=separator;return true;
}
static bool valid_word(bot_fuzzy_word field) {
    return field==BOT_FUZZY_INVENTORY || field==BOT_FUZZY_THRESHOLD || field==BOT_FUZZY_BALANCED ||
        field==BOT_FUZZY_CHILD || field==BOT_FUZZY_NEXT;
}
static bool valid_float(bot_fuzzy_float field) {
    return field==BOT_FUZZY_WEIGHT || field==BOT_FUZZY_MINIMUM || field==BOT_FUZZY_MAXIMUM;
}
bool bot_fuzzy_separator_word_read(const bot_fuzzy_separator *separator,bot_fuzzy_word field,uint32_t *out,qa_error *error) {
    if(!out || !valid_word(field)) return fail(error,"Fuzzy separator read requires its genuine word field/output");
    qa_bot_memory_span bytes;if(!separator_span(separator,&bytes,error)) return false;
    *out=read_word(bytes.data+(uint32_t)field);return true;
}
bool bot_fuzzy_separator_word_write(const bot_fuzzy_separator *separator,bot_fuzzy_word field,uint32_t value,qa_error *error) {
    if(!valid_word(field)) return fail(error,"Fuzzy separator write requires its genuine word field");
    qa_bot_memory_span bytes;if(!separator_span(separator,&bytes,error)) return false;
    write_word(bytes.data+(uint32_t)field,value);return true;
}
bool bot_fuzzy_separator_integer_read(const bot_fuzzy_separator *separator,bot_fuzzy_word field,int32_t *out,qa_error *error) {
    if(!out) return fail(error,"Fuzzy separator integer read requires its typed output");
    uint32_t word;if(!bot_fuzzy_separator_word_read(separator,field,&word,error)) return false;
    *out=word<=INT32_MAX?(int32_t)word:-1-(int32_t)(UINT32_MAX-word);return true;
}
bool bot_fuzzy_separator_float_read(const bot_fuzzy_separator *separator,bot_fuzzy_float field,float *out,qa_error *error) {
    if(!out || !valid_float(field)) return fail(error,"Fuzzy separator read requires its genuine float field/output");
    qa_bot_memory_span bytes;if(!separator_span(separator,&bytes,error)) return false;
    uint32_t word=read_word(bytes.data+(uint32_t)field);memcpy(out,&word,4);return true;
}
bool bot_fuzzy_separator_float_write(const bot_fuzzy_separator *separator,bot_fuzzy_float field,float value,qa_error *error) {
    if(!valid_float(field)) return fail(error,"Fuzzy separator write requires its genuine float field");
    qa_bot_memory_span bytes;if(!separator_span(separator,&bytes,error)) return false;
    uint32_t word;memcpy(&word,&value,4);write_word(bytes.data+(uint32_t)field,word);return true;
}
static bool config_span(const bot_fuzzy_config *config,qa_bot_memory_span *out,qa_error *error) {
    if(!config) return fail(error,"Fuzzy configuration record is absent");
    if(!live(config->heap,error) ||
       !qa_bot_memory_bytes(config->heap->memory,config->allocation,out,error)) return false;
    return out->size==BOT_FUZZY_CONFIG_BYTES ? true : fail(error,"Fuzzy configuration allocation size mismatch");
}
bool bot_fuzzy_config_allocate(bot_fuzzy_heap *heap,qa_bytes filename,bot_fuzzy_config *out,qa_error *error) {
    if(!live(heap,error) || !out || (filename.size && !filename.data)) return fail(error,"Fuzzy configuration allocation requires its source filename/output");
    size_t size=terminated(filename);if(size>63) size=63;
    uint8_t encoded[63];if(size) memcpy(encoded,filename.data,size);
    qa_bot_memory_allocation allocation;
    if(!qa_bot_memory_allocate(heap->memory,BOT_FUZZY_CONFIG_BYTES,QA_BOT_MEMORY_HEAP,true,NULL,&allocation,error)) return false;
    bot_fuzzy_config config={heap,allocation};qa_bot_memory_span bytes;
    if(!config_span(&config,&bytes,error)) return false;
    if(size) memcpy(bytes.data+1028,encoded,size);
    *out=config;return true;
}
bool bot_fuzzy_config_bind(bot_fuzzy_heap *heap,qa_bot_memory_allocation allocation,bot_fuzzy_config *out,qa_error *error) {
    if(!out) return fail(error,"Fuzzy configuration binding requires its output");
    bot_fuzzy_config config={heap,allocation};qa_bot_memory_span bytes;
    if(!config_span(&config,&bytes,error)) return false;
    *out=config;return true;
}
bool bot_fuzzy_config_count(const bot_fuzzy_config *config,int32_t *out,qa_error *error) {
    if(!out) return fail(error,"Fuzzy count read requires its typed output");
    qa_bot_memory_span bytes;if(!config_span(config,&bytes,error)) return false;
    uint32_t word=read_word(bytes.data);
    if(word>BOT_FUZZY_WEIGHTS) return fail(error,"Invalid fuzzy source weight count");
    *out=(int32_t)word;return true;
}
bool bot_fuzzy_config_count_write(const bot_fuzzy_config *config,int32_t count,qa_error *error) {
    qa_bot_memory_span bytes;if(!config_span(config,&bytes,error)) return false;
    write_word(bytes.data,(uint32_t)count);return true;
}
static bool config_pointer_span(const bot_fuzzy_config *config,int32_t index,bool separator,uint8_t **out,qa_error *error) {
    if(index<0 || index>=BOT_FUZZY_WEIGHTS) return fail(error,"Fuzzy weight slot exceeds its configuration allocation");
    qa_bot_memory_span bytes;if(!config_span(config,&bytes,error)) return false;
    *out=bytes.data+4+(uint32_t)index*8+(separator?4:0);return true;
}
bool bot_fuzzy_config_pointer_read(const bot_fuzzy_config *config,int32_t index,bool separator,uint32_t *out,qa_error *error) {
    if(!out) return fail(error,"Fuzzy pointer read requires its typed output");
    uint8_t *at;if(!config_pointer_span(config,index,separator,&at,error)) return false;
    *out=read_word(at);return true;
}
bool bot_fuzzy_config_pointer_write(const bot_fuzzy_config *config,int32_t index,bool separator,uint32_t pointer,qa_error *error) {
    uint8_t *at;if(!config_pointer_span(config,index,separator,&at,error)) return false;
    write_word(at,pointer);return true;
}
bool bot_fuzzy_config_filename(const bot_fuzzy_config *config,qa_bytes *out,qa_error *error) {
    if(!out) return fail(error,"Fuzzy filename read requires its byte output");
    qa_bot_memory_span bytes;if(!config_span(config,&bytes,error)) return false;
    qa_bytes filename={bytes.data+1028,64};*out=(qa_bytes){filename.data,terminated(filename)};return true;
}
bool bot_fuzzy_config_matches_filename(const bot_fuzzy_config *config,qa_bytes filename,bool *out,qa_error *error) {
    if(!out || (filename.size && !filename.data)) return fail(error,"Fuzzy filename match requires source bytes/output");
    qa_bytes stored;if(!bot_fuzzy_config_filename(config,&stored,error)) return false;
    size_t size=terminated(filename);
    *out=stored.size==size && (!size || !memcmp(stored.data,filename.data,size));return true;
}
