#include "source_state.h"
#include "qa/binary.h"
#include <string.h>

static bool fail(qa_error *error,const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;
}
static bool span(const bot_goal_record *record,qa_bot_memory_span *out,qa_error *error) {
    if(!record) return fail(error,"GoalState source record is absent");
    if(!qa_bot_memory_bytes(record->memory,record->allocation,out,error)) return false;
    return out->size==BOT_GOAL_STATE_BYTES ? true : fail(error,"GoalState source allocation size mismatch");
}
static qa_vec3 vector_read(const uint8_t *at) {
    return (qa_vec3){qa_load_f32le(at),qa_load_f32le(at+4),qa_load_f32le(at+8)};
}
static void vector_write(uint8_t *at,qa_vec3 value) {
    qa_store_f32le(at,value.x);qa_store_f32le(at+4,value.y);qa_store_f32le(at+8,value.z);
}
static bool valid_word(bot_goal_record_word field) {
    return field==BOT_GOAL_CONFIG_POINTER || field==BOT_GOAL_INDEX_POINTER || field==BOT_GOAL_CLIENT ||
        field==BOT_GOAL_LAST_AREA || field==BOT_GOAL_STACK_TOP;
}
bool bot_goal_record_bind(qa_bot_memory *memory,qa_bot_memory_allocation allocation,
    bot_goal_record *out,qa_error *error) {
    if(!out) return fail(error,"GoalState binding requires its typed output");
    bot_goal_record record={memory,allocation};qa_bot_memory_span bytes;
    if(!span(&record,&bytes,error)) return false;
    *out=record;return true;
}
bool bot_goal_record_allocate(qa_bot_memory *memory,int32_t client,bot_goal_record *out,qa_error *error) {
    if(!out) return fail(error,"GoalState allocation requires its typed output");
    qa_bot_memory_allocation allocation;
    if(!qa_bot_memory_allocate(memory,BOT_GOAL_STATE_BYTES,QA_BOT_MEMORY_HEAP,true,NULL,&allocation,error)) return false;
    bot_goal_record record={memory,allocation};qa_bot_memory_span bytes;
    if(!span(&record,&bytes,error)) return false;
    qa_store_u32le(bytes.data+BOT_GOAL_CLIENT,(uint32_t)client);*out=record;return true;
}
bool bot_goal_record_word_read(const bot_goal_record *record,bot_goal_record_word field,
    uint32_t *out,qa_error *error) {
    if(!out || !valid_word(field)) return fail(error,"GoalState word read requires its genuine field/output");
    qa_bot_memory_span bytes;if(!span(record,&bytes,error)) return false;
    *out=qa_load_u32le(bytes.data+(uint32_t)field);return true;
}
bool bot_goal_record_word_write(const bot_goal_record *record,bot_goal_record_word field,
    uint32_t value,qa_error *error) {
    if(!valid_word(field)) return fail(error,"GoalState word write requires its genuine field");
    qa_bot_memory_span bytes;if(!span(record,&bytes,error)) return false;
    qa_store_u32le(bytes.data+(uint32_t)field,value);return true;
}
bool bot_goal_record_integer_read(const bot_goal_record *record,bot_goal_record_word field,
    int32_t *out,qa_error *error) {
    if(!out) return fail(error,"GoalState integer read requires its typed output");
    uint32_t word;if(!bot_goal_record_word_read(record,field,&word,error)) return false;
    *out=qa_i32_from_u32(word);return true;
}
bool bot_goal_record_goal_span(const bot_goal_record *record,int32_t index,
    qa_bot_memory_span *out,qa_error *error) {
    if(!out || index<0 || index>=QA_BOT_GOAL_STACK) return fail(error,"Goal stack index exceeds its source allocation");
    qa_bot_memory_span bytes;if(!span(record,&bytes,error)) return false;
    *out=(qa_bot_memory_span){bytes.data+16+(uint32_t)index*56,56};return true;
}
bool bot_goal_record_goal_read(const bot_goal_record *record,int32_t index,qa_bot_goal *out,qa_error *error) {
    if(!out) return fail(error,"Goal source read requires its typed output");
    qa_bot_memory_span bytes;if(!bot_goal_record_goal_span(record,index,&bytes,error)) return false;
    const uint8_t *at=bytes.data;
    *out=(qa_bot_goal){.origin=vector_read(at),.area=qa_load_i32le(at+12),
        .mins=vector_read(at+16),.maxs=vector_read(at+28),.entity=qa_load_i32le(at+40),
        .number=qa_load_i32le(at+44),.flags=qa_load_i32le(at+48),.item_info=qa_load_i32le(at+52)};
    return true;
}
bool bot_goal_record_goal_write(const bot_goal_record *record,int32_t index,const qa_bot_goal *goal,qa_error *error) {
    if(!goal) return fail(error,"Goal source write requires its typed input");
    qa_bot_memory_span bytes;if(!bot_goal_record_goal_span(record,index,&bytes,error)) return false;
    uint8_t *at=bytes.data;
    vector_write(at,goal->origin);qa_store_u32le(at+12,(uint32_t)goal->area);
    vector_write(at+16,goal->mins);vector_write(at+28,goal->maxs);
    qa_store_u32le(at+40,(uint32_t)goal->entity);qa_store_u32le(at+44,(uint32_t)goal->number);
    qa_store_u32le(at+48,(uint32_t)goal->flags);qa_store_u32le(at+52,(uint32_t)goal->item_info);return true;
}
bool bot_goal_record_goal_bytes_write(const bot_goal_record *record,int32_t index,qa_bytes goal,qa_error *error) {
    if(goal.size<56 || !goal.data) return fail(error,"Goal source copy requires at least 56 input bytes");
    qa_bot_memory_span bytes;if(!bot_goal_record_goal_span(record,index,&bytes,error)) return false;
    memmove(bytes.data,goal.data,bytes.size);return true;
}
bool bot_goal_record_goal_bytes_read(const bot_goal_record *record,int32_t index,qa_bytes *out,qa_error *error) {
    if(!out) return fail(error,"Goal source borrow requires its byte output");
    qa_bot_memory_span bytes;if(!bot_goal_record_goal_span(record,index,&bytes,error)) return false;
    *out=(qa_bytes){bytes.data,bytes.size};return true;
}
bool bot_goal_record_avoid_read(const bot_goal_record *record,int32_t index,qa_bot_avoid_goal *out,qa_error *error) {
    if(!out || index<0 || index>=QA_BOT_AVOID_GOALS) return fail(error,"Avoid-goal index exceeds its source allocation");
    qa_bot_memory_span bytes;if(!span(record,&bytes,error)) return false;
    *out=(qa_bot_avoid_goal){qa_load_i32le(bytes.data+468+(uint32_t)index*4),
        qa_load_f32le(bytes.data+1492+(uint32_t)index*4)};return true;
}
bool bot_goal_record_avoid_write(const bot_goal_record *record,int32_t index,const qa_bot_avoid_goal *goal,qa_error *error) {
    if(!goal || index<0 || index>=QA_BOT_AVOID_GOALS) return fail(error,"Avoid-goal index exceeds its source allocation");
    qa_bot_memory_span bytes;if(!span(record,&bytes,error)) return false;
    qa_store_u32le(bytes.data+468+(uint32_t)index*4,(uint32_t)goal->number);
    qa_store_f32le(bytes.data+1492+(uint32_t)index*4,goal->expires);return true;
}
bool bot_goal_record_reset(const bot_goal_record *record,bool stack,qa_error *error) {
    qa_bot_memory_span bytes;if(!span(record,&bytes,error)) return false;
    uint32_t start=stack?16:468;memset(bytes.data+start,0,BOT_GOAL_STATE_BYTES-start);return true;
}
bool bot_goal_record_state_read(const bot_goal_record *record,qa_bot_goal_state *out,qa_error *error) {
    if(!out) return fail(error,"GoalState capture requires its typed output");
    qa_bot_goal_state state={0};
    if(!bot_goal_record_integer_read(record,BOT_GOAL_CLIENT,&state.client,error) ||
       !bot_goal_record_integer_read(record,BOT_GOAL_LAST_AREA,&state.last_reachability_area,error) ||
       !bot_goal_record_word_read(record,BOT_GOAL_STACK_TOP,&state.stack_top,error)) return false;
    for(int32_t i=0;i<QA_BOT_GOAL_STACK;++i)
        if(!bot_goal_record_goal_read(record,i,&state.stack[i],error)) return false;
    for(int32_t i=0;i<QA_BOT_AVOID_GOALS;++i)
        if(!bot_goal_record_avoid_read(record,i,&state.avoid[i],error)) return false;
    *out=state;return true;
}
bool bot_goal_record_state_write(const bot_goal_record *record,const qa_bot_goal_state *state,qa_error *error) {
    if(!state) return fail(error,"GoalState restore requires its typed input");
    if(!bot_goal_record_word_write(record,BOT_GOAL_CLIENT,(uint32_t)state->client,error) ||
       !bot_goal_record_word_write(record,BOT_GOAL_LAST_AREA,(uint32_t)state->last_reachability_area,error) ||
       !bot_goal_record_word_write(record,BOT_GOAL_STACK_TOP,state->stack_top,error)) return false;
    for(int32_t i=0;i<QA_BOT_GOAL_STACK;++i)
        if(!bot_goal_record_goal_write(record,i,&state->stack[i],error)) return false;
    for(int32_t i=0;i<QA_BOT_AVOID_GOALS;++i)
        if(!bot_goal_record_avoid_write(record,i,&state->avoid[i],error)) return false;
    return true;
}
