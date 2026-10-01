#include "source_weapon_state.h"
#include <limits.h>
#include <stdlib.h>

static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static uint32_t read_word(const uint8_t *at) {
    return (uint32_t)at[0]|((uint32_t)at[1]<<8)|((uint32_t)at[2]<<16)|((uint32_t)at[3]<<24);
}
static void write_word(uint8_t *at,uint32_t value) {
    for(uint32_t i=0;i<4;++i) at[i]=(uint8_t)(value>>(i*8));
}
static bool span(const bot_weapon_record *record,qa_bot_memory_span *out,qa_error *error) {
    if(!record) return fail(error,"WeaponState source record is absent");
    return qa_bot_memory_bytes(record->memory,record->allocation,out,error) &&
        (out->size==BOT_WEAPON_STATE_BYTES || fail(error,"WeaponState source allocation size mismatch"));
}
bool bot_weapon_record_bind(qa_bot_memory *memory,qa_bot_memory_allocation allocation,
    bot_weapon_record *out,qa_error *error) {
    if(!out) return fail(error,"WeaponState binding requires its output");
    bot_weapon_record record={memory,allocation};qa_bot_memory_span bytes;
    if(!span(&record,&bytes,error)) return false;
    *out=record;return true;
}
bool bot_weapon_record_allocate(qa_bot_memory *memory,bot_weapon_record *out,qa_error *error) {
    if(!out) return fail(error,"WeaponState allocation requires its output");
    qa_bot_memory_allocation allocation;
    if(!qa_bot_memory_allocate(memory,BOT_WEAPON_STATE_BYTES,QA_BOT_MEMORY_HEAP,true,NULL,&allocation,error)) return false;
    return bot_weapon_record_bind(memory,allocation,out,error);
}
static bool field_valid(bot_weapon_record_word field) {
    return field==BOT_WEAPON_CONFIG_POINTER || field==BOT_WEAPON_INDEX_POINTER;
}
bool bot_weapon_record_read(const bot_weapon_record *record,bot_weapon_record_word field,
    uint32_t *out,qa_error *error) {
    if(!out || !field_valid(field)) return fail(error,"WeaponState read requires its actual field/output");
    qa_bot_memory_span bytes;if(!span(record,&bytes,error)) return false;
    *out=read_word(bytes.data+(uint32_t)field);return true;
}
bool bot_weapon_record_write(const bot_weapon_record *record,bot_weapon_record_word field,
    uint32_t value,qa_error *error) {
    if(!field_valid(field)) return fail(error,"WeaponState write requires its actual field");
    qa_bot_memory_span bytes;if(!span(record,&bytes,error)) return false;
    write_word(bytes.data+(uint32_t)field,value);return true;
}
bool bot_weapon_record_reset(const bot_weapon_record *record,qa_error *error) {
    qa_bot_memory_span bytes;if(!span(record,&bytes,error)) return false;
    uint32_t config=read_word(bytes.data),indexes=read_word(bytes.data+4);
    write_word(bytes.data,config);write_word(bytes.data+4,indexes);return true;
}
bool bot_weapon_indexes_allocate(qa_bot_memory *memory,uint32_t count,
    qa_bot_memory_allocation *out,qa_error *error) {
    if(!out || count>UINT32_MAX/4) return fail(error,"Weapon indexes exceed the allocator byte domain");
    return qa_bot_memory_allocate(memory,count*4,QA_BOT_MEMORY_HEAP,true,NULL,out,error);
}
static bool index_span(qa_bot_memory *memory,qa_bot_memory_allocation allocation,uint32_t index,
    qa_bot_memory_span *out,qa_error *error) {
    if(!qa_bot_memory_bytes(memory,allocation,out,error)) return false;
    if(index>=out->size/4) return fail(error,"Weapon index exceeds its actual allocation");
    out->data+=index*4;out->size=4;return true;
}
bool bot_weapon_index_read(qa_bot_memory *memory,qa_bot_memory_allocation allocation,uint32_t index,
    int32_t *out,qa_error *error) {
    if(!out) return fail(error,"Weapon index read requires its output");
    qa_bot_memory_span bytes;if(!index_span(memory,allocation,index,&bytes,error)) return false;
    uint32_t value=read_word(bytes.data);*out=value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);return true;
}
bool bot_weapon_index_write(qa_bot_memory *memory,qa_bot_memory_allocation allocation,uint32_t index,
    int32_t value,qa_error *error) {
    qa_bot_memory_span bytes;if(!index_span(memory,allocation,index,&bytes,error)) return false;
    write_word(bytes.data,(uint32_t)value);return true;
}
void bot_weapon_pointers_init(bot_weapon_pointers *pointers) {
    *pointers=(bot_weapon_pointers){.next_pointer=1};
}
void bot_weapon_pointers_clear(bot_weapon_pointers *pointers) {
    if(!pointers) return;
    while(pointers->first) {
        bot_weapon_pointer *row=pointers->first;pointers->first=row->next;
        if(row->kind==BOT_WEAPON_POINTER_CONFIG) qa_bot_weights_release(row->config);
        free(row);
    }
    while(pointers->configs) {
        bot_weapon_config_identity *row=pointers->configs;pointers->configs=row->next;
        qa_bot_weights_release(row->config);free(row);
    }
    bot_weapon_pointers_init(pointers);
}
static bot_weapon_pointer *resolve(const bot_weapon_pointers *pointers,uint32_t pointer,qa_error *error) {
    bot_weapon_pointer *row=pointers?pointers->first:NULL;
    while(row && row->pointer!=pointer) row=row->next;
    if(!row) fail(error,"Invalid source weapon pointer");
    return row;
}
static bool pointer_next(bot_weapon_pointers *pointers,uint32_t *out,qa_error *error) {
    if(!pointers || !pointers->next_pointer || pointers->next_pointer>UINT32_MAX)
        return fail(error,"Weapon pointer identities exhausted");
    *out=(uint32_t)pointers->next_pointer++;return true;
}
static void append(bot_weapon_pointers *pointers,bot_weapon_pointer *row) {
    if(pointers->last) pointers->last->next=row;else pointers->first=row;
    pointers->last=row;
}
static void remove_row(bot_weapon_pointers *pointers,bot_weapon_pointer *row) {
    bot_weapon_pointer *previous=NULL,**link=&pointers->first;
    while(*link!=row) {previous=*link;link=&(*link)->next;}
    *link=row->next;if(pointers->last==row) pointers->last=previous;
    if(row->kind==BOT_WEAPON_POINTER_CONFIG) qa_bot_weights_release(row->config);
    free(row);
}
static bool config_pointer(bot_weapon_pointers *pointers,qa_bot_weights *config,uint32_t *out,qa_error *error) {
    bot_weapon_config_identity *identity=pointers->configs;
    while(identity && identity->config!=config) identity=identity->next;
    if(identity) {
        for(bot_weapon_pointer *row=pointers->first;row;row=row->next) if(row->pointer==identity->pointer) {
            if(row->kind!=BOT_WEAPON_POINTER_CONFIG || row->references>=UINT64_C(9007199254740991))
                return fail(error,"Weapon configuration identity/reference mismatch");
            ++row->references;*out=row->pointer;return true;
        }
    }
    bot_weapon_pointer *row=calloc(1,sizeof(*row));
    bot_weapon_config_identity *created=identity?NULL:calloc(1,sizeof(*created));
    if(!row || (!identity && !created)) {
        free(row);free(created);qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source weapon configuration pointer");return false;
    }
    uint32_t pointer;
    if(identity) pointer=identity->pointer;
    else if(!pointer_next(pointers,&pointer,error)) {free(row);free(created);return false;}
    if(created) {
        *created=(bot_weapon_config_identity){.config=config,.pointer=pointer};qa_bot_weights_retain(config);
        if(pointers->last_config) pointers->last_config->next=created;else pointers->configs=created;
        pointers->last_config=created;
    }
    *row=(bot_weapon_pointer){.pointer=pointer,.kind=BOT_WEAPON_POINTER_CONFIG,.config=config,.references=1};
    qa_bot_weights_retain(config);
    append(pointers,row);*out=pointer;return true;
}
bool bot_weapon_config_get(const bot_weapon_pointers *pointers,const bot_weapon_record *record,
    qa_bot_weights **out,qa_error *error) {
    if(!out) return fail(error,"Weapon configuration read requires its output");
    uint32_t pointer;if(!bot_weapon_record_read(record,BOT_WEAPON_CONFIG_POINTER,&pointer,error)) return false;
    if(!pointer) {*out=NULL;return true;}
    bot_weapon_pointer *row=resolve(pointers,pointer,error);
    if(!row) return false;
    if(row->kind!=BOT_WEAPON_POINTER_CONFIG) return fail(error,"Weapon configuration pointer has another source kind");
    *out=row->config;return true;
}
bool bot_weapon_config_set(bot_weapon_pointers *pointers,const bot_weapon_record *record,
    qa_bot_weights *config,qa_error *error) {
    if(!pointers) return fail(error,"Weapon configuration write requires its pointer owner");
    qa_bot_memory_span bytes;if(!span(record,&bytes,error)) return false;
    uint32_t pointer=0;
    if(config && !config_pointer(pointers,config,&pointer,error)) return false;
    uint32_t previous=read_word(bytes.data);
    if(previous) {
        bot_weapon_pointer *row=resolve(pointers,previous,error);
        if(!row) return false;
        if(row->kind!=BOT_WEAPON_POINTER_CONFIG || !row->references)
            return fail(error,"Released weapon configuration pointer has another source kind");
        if(--row->references==0) remove_row(pointers,row);
    }
    write_word(bytes.data,pointer);return true;
}
bool bot_weapon_indexes_get(const bot_weapon_pointers *pointers,const bot_weapon_record *record,
    qa_bot_memory_allocation *out,bool *present,qa_error *error) {
    if(!out || !present) return fail(error,"Weapon indexes read requires its outputs");
    uint32_t pointer;if(!bot_weapon_record_read(record,BOT_WEAPON_INDEX_POINTER,&pointer,error)) return false;
    *present=pointer!=0;if(!pointer) {*out=(qa_bot_memory_allocation){0};return true;}
    bot_weapon_pointer *row=resolve(pointers,pointer,error);
    if(!row) return false;
    if(row->kind!=BOT_WEAPON_POINTER_INDEXES) return fail(error,"Weapon indexes pointer has another source kind");
    *out=row->indexes;return true;
}
bool bot_weapon_indexes_publish(bot_weapon_pointers *pointers,const bot_weapon_record *record,
    qa_bot_memory_allocation indexes,qa_error *error) {
    qa_bot_memory_span bytes;
    if(!pointers || !record) return fail(error,"Weapon index publication requires its actual pointer/state owners");
    if(!qa_bot_memory_bytes(record->memory,indexes,&bytes,error)) return false;
    if(bytes.size%4) return fail(error,"Weapon index allocation has a partial word");
    bot_weapon_pointer *row=calloc(1,sizeof(*row));
    if(!row) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source weapon indexes pointer");return false;}
    if(!pointer_next(pointers,&row->pointer,error)) {free(row);return false;}
    row->kind=BOT_WEAPON_POINTER_INDEXES;row->indexes=indexes;append(pointers,row);
    return bot_weapon_record_write(record,BOT_WEAPON_INDEX_POINTER,row->pointer,error);
}
bool bot_weapon_indexes_forget(bot_weapon_pointers *pointers,const bot_weapon_record *record,qa_error *error) {
    if(!pointers) return fail(error,"Weapon index release requires its pointer owner");
    uint32_t pointer;if(!bot_weapon_record_read(record,BOT_WEAPON_INDEX_POINTER,&pointer,error)) return false;
    if(pointer) {
        bot_weapon_pointer *row=resolve(pointers,pointer,error);
        if(!row) return false;
        if(row->kind!=BOT_WEAPON_POINTER_INDEXES) return fail(error,"Released weapon index pointer has another source kind");
        remove_row(pointers,row);
    }
    return bot_weapon_record_write(record,BOT_WEAPON_INDEX_POINTER,0,error);
}
