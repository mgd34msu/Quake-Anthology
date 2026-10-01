#include "internal.h"
#include "../checkpoint_internal.h"

bool bot_goal_memory_bind(qa_bot_goals *goals,qa_bot_memory *memory,qa_error *error)
{
    if(!bot_goal_mutable(goals,error) || !memory || qa_bot_memory_disposed(memory))
        return bot_goal_fail(error,"Goal allocation binding requires its actual live memory owner");
    if(goals->memory==memory) {goals->shared_memory=true;return true;}
    if(goals->indexes || goals->weights)
        return bot_goal_fail(error,"Goal allocation binding cannot replace populated source aliases");
    for(uint32_t i=0;i<goals->options.maximum_states;++i)
        if(goals->states[i].used) return bot_goal_fail(error,"Goal allocation binding requires its fresh constructor");
    if(!qa_bot_memory_retain(memory,error)) return false;
    if(!qa_bot_memory_release(goals->memory,error)) {
        (void)qa_bot_memory_release(memory,NULL);return false;
    }
    goals->memory=memory;goals->shared_memory=true;return true;
}
bool bot_goal_indexes_create(qa_bot_goals *goals,uint32_t count,qa_bot_memory_allocation *out,qa_error *error)
{
    if(count>(INT32_MAX-4)/4) return bot_goal_fail(error,"Item index allocation exceeds its source size");
    return qa_bot_memory_allocate(goals->memory,count*4,QA_BOT_MEMORY_HEAP,true,NULL,out,error);
}
bool bot_goal_indexes_write(qa_bot_goals *goals,qa_bot_memory_allocation allocation,
    uint32_t index,int32_t value,qa_error *error)
{
    qa_bot_memory_span bytes;
    if(!qa_bot_memory_bytes(goals->memory,allocation,&bytes,error)) return false;
    if(index>=bytes.size/4) return bot_goal_fail(error,"Item index write is outside its source allocation");
    uint32_t word=(uint32_t)value;
    for(uint32_t byte=0;byte<4;++byte) bytes.data[index*4+byte]=(uint8_t)(word>>(byte*8));
    return true;
}
bool bot_goal_indexes_read(qa_bot_goals *goals,const bot_goal_slot *state,int32_t index,
    int32_t *out,qa_error *error)
{
    if(!state->index_pointer || !out) return bot_goal_fail(error,"Item weight indexes have not been initialized");
    bot_goal_indexes *row=goals->indexes;
    while(row && row->pointer!=state->index_pointer) row=row->next;
    if(!row) return bot_goal_fail(error,"Invalid goal item weight index pointer");
    qa_bot_memory_span bytes;
    if(!qa_bot_memory_bytes(goals->memory,row->allocation,&bytes,error)) return false;
    if(index<0 || (uint32_t)index>=bytes.size/4)
        return bot_goal_fail(error,"Item weight index is stale for the current item configuration");
    const uint8_t *at=bytes.data+(uint32_t)index*4;
    uint32_t word=(uint32_t)at[0]|((uint32_t)at[1]<<8)|((uint32_t)at[2]<<16)|((uint32_t)at[3]<<24);
    *out=word<=INT32_MAX?(int32_t)word:-1-(int32_t)(UINT32_MAX-word);return true;
}
bool bot_goal_indexes_publish(qa_bot_goals *goals,bot_goal_slot *state,
    qa_bot_memory_allocation allocation,qa_error *error)
{
    qa_bot_memory_span bytes;
    if(!qa_bot_memory_bytes(goals->memory,allocation,&bytes,error)) return false;
    if(!goals->next_pointer || goals->next_pointer>UINT32_MAX)
        return bot_goal_fail(error,"Goal index pointer identities exhausted");
    bot_goal_indexes *row=calloc(1,sizeof(*row));
    if(!row) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining goal index allocation reference");return false;}
    row->allocation=allocation;row->pointer=(uint32_t)goals->next_pointer++;
    if(goals->last_indexes) goals->last_indexes->next=row;else goals->indexes=row;
    goals->last_indexes=row;state->index_pointer=row->pointer;return true;
}
bool bot_goal_indexes_release(qa_bot_goals *goals,bot_goal_slot *state,qa_error *error)
{
    bot_goal_indexes **link=&goals->indexes,*previous=NULL;
    if(!state->index_pointer) return true;
    while(*link && (*link)->pointer!=state->index_pointer) {previous=*link;link=&(*link)->next;}
    bot_goal_indexes *row=*link;
    if(!row) return bot_goal_fail(error,"Invalid goal item weight index pointer");
    if(!qa_bot_memory_free(goals->memory,row->allocation,error)) return false;
    *link=row->next;
    if(goals->last_indexes==row) goals->last_indexes=previous;
    state->index_pointer=0;free(row);return true;
}
void bot_goal_indexes_clear(qa_bot_goals *goals)
{
    while(goals->indexes) {
        bot_goal_indexes *row=goals->indexes;goals->indexes=row->next;free(row);
    }
    goals->last_indexes=NULL;
}
bool bot_goal_indexes_capture(qa_bot_goals *goals,uint32_t handle,bot_goal_index_image *out,qa_error *error)
{
    bot_goal_slot *state=bot_goal_slot_get(goals,handle,error);
    if(!state || !out || out->bytes.data) return bot_goal_fail(error,"Goal index checkpoint requires its empty output");
    if(!state->index_pointer) return true;
    bot_goal_indexes *row=goals->indexes;
    while(row && row->pointer!=state->index_pointer) row=row->next;
    if(!row) return bot_goal_fail(error,"Invalid goal item weight index pointer");
    qa_bot_memory_span bytes;
    if(!qa_bot_memory_bytes(goals->memory,row->allocation,&bytes,error)) return false;
    if(bytes.size && !(out->bytes.data=malloc(bytes.size))) {
        qa_error_set(error,QA_ERROR_MEMORY,bytes.size,"Capturing goal index allocation bytes");return false;
    }
    if(bytes.size) memcpy(out->bytes.data,bytes.data,bytes.size);
    out->bytes.size=bytes.size;out->pointer=row->pointer;out->allocation=row->allocation;return true;
}
