#include "internal.h"
#include "../library/internal.h"
#include "../checkpoint_internal.h"
#include "qa/bots_allocator_checkpoint.h"

struct bot_goal_history {
    qa_bot_goals *owner;
    qa_bot_goals state;
};
struct bot_goal_history_restore {
    qa_bot_goals *owner;
    qa_bot_goals state;
    const bot_goal_history *image;
};
static void clear(qa_bot_goals *state)
{
    while(state->weights) {
        bot_goal_weights *row=state->weights;state->weights=row->next;
        qa_bot_weights_release(row->weights);free(row);
    }
    bot_goal_indexes_clear(state);bot_goal_map_clear(state);
    qa_bot_items_release(state->items);free(state->states);
    state->items=NULL;state->states=NULL;
}
static void *copy_array(const void *source,size_t count,size_t stride,qa_error *error)
{
    if(!count) return NULL;
    if(!source || count>SIZE_MAX/stride) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Invalid goal checkpoint array extent");return NULL;
    }
    void *copy=malloc(count*stride);
    if(!copy) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining complete goal checkpoint arrays");return NULL;}
    memcpy(copy,source,count*stride);return copy;
}
static bool copy(const qa_bot_goals *source,qa_bot_goals *out,qa_error *error)
{
    if(source->source_count>source->source_capacity || (source->source_count && !source->source))
        return bot_goal_fail(error,"Invalid retained source-goal checkpoint storage");
    *out=*source;
    out->items=NULL;out->states=NULL;out->weights=NULL;out->indexes=NULL;out->last_indexes=NULL;
    out->prepared_indexes=NULL;out->level=NULL;out->info=NULL;out->source=NULL;
    out->items=source->items;qa_bot_items_retain(out->items);
    out->states=copy_array(source->states,source->options.maximum_states,sizeof(*source->states),error);
    if(!out->states) goto failed;
    bot_goal_weights **weights=&out->weights;
    for(const bot_goal_weights *row=source->weights;row;row=row->next) {
        *weights=calloc(1,sizeof(**weights));
        if(!*weights) goto memory_failure;
        **weights=*row;(*weights)->next=NULL;qa_bot_weights_retain(row->weights);
        weights=&(*weights)->next;
    }
    bot_goal_indexes **indexes=&out->indexes;
    for(const bot_goal_indexes *row=source->indexes;row;row=row->next) {
        if(row->prepared_users) {bot_goal_fail(error,"Goal checkpoint cannot capture prepared index aliases");goto failed;}
        *indexes=malloc(sizeof(**indexes));
        if(!*indexes) goto memory_failure;
        **indexes=*row;(*indexes)->next=NULL;out->last_indexes=*indexes;indexes=&(*indexes)->next;
    }
    out->level=copy_array(source->level,source->level_capacity,sizeof(*source->level),error);
    if(source->level_capacity && !out->level) goto failed;
    out->info=copy_array(source->info,source->info_count,sizeof(*source->info),error);
    out->info_capacity=source->info_count;
    if(source->info_count && !out->info) goto failed;
    if(source->source_capacity) {
        if(source->source_capacity>SIZE_MAX/sizeof(*source->source) || source->source_count>source->source_capacity)
            {bot_goal_fail(error,"Invalid retained source-goal checkpoint capacity");goto failed;}
        out->source=calloc(source->source_capacity,sizeof(*source->source));
        if(!out->source) goto memory_failure;
    }
    for(size_t i=0;i<source->source_count;++i) {
        const bot_source_goal *row=&source->source[i];
        out->source[i]=*row;out->source[i].name=NULL;
        if(!row->name || !row->name_capacity || strlen(row->name)>=row->name_capacity)
            {bot_goal_fail(error,"Invalid retained source-goal checkpoint name");goto failed;}
        out->source[i].name=calloc(row->name_capacity,1);
        if(!out->source[i].name) goto memory_failure;
        memcpy(out->source[i].name,row->name,strlen(row->name)+1);
    }
    return true;
memory_failure:
    qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining complete goal reference history");
failed:
    if(!out->source) out->source_count=0;
    clear(out);return false;
}
void bot_goal_history_destroy(bot_goal_history *image)
{
    if(!image) return;
    clear(&image->state);
    free(image);
}
bool bot_goal_history_capture(qa_bot_goals *goals,bot_fuzzy_history *fuzzy,bot_goal_history **out,qa_error *error)
{
    if(!bot_goal_mutable(goals,error) || goals->prepared_indexes || !out || *out)
        return bot_goal_fail(error,"Complete goal checkpoint requires an actual idle owner and empty output");
    if (!bot_goal_info_topology(goals,error)) return false;
    for(bot_goal_indexes *row=goals->indexes;row;row=row->next) {
        qa_bot_memory_span bytes;
        if(!qa_bot_memory_bytes(goals->memory,row->allocation,&bytes,error)) return false;
    }
    for(uint32_t i=0;i<goals->options.maximum_states;++i) if(goals->states[i].used) {
        bot_goal_record qualified;
        if(goals->states[i].record.memory!=goals->memory ||
           !bot_goal_record_bind(goals->memory,goals->states[i].record.allocation,&qualified,error)) return false;
    }
    bot_goal_history *image=calloc(1,sizeof(*image));
    if(!image) {qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating complete goal checkpoint");return false;}
    image->owner=goals;
    if(!copy(goals,&image->state,error)) {free(image);return false;}
    for(bot_goal_weights *config=goals->weights;config;config=config->next) {
        if(!bot_fuzzy_history_include(fuzzy,config->weights,error)) goto failed;
    }
    *out=image;return true;
failed:
    bot_goal_history_destroy(image);return false;
}
bool bot_goal_history_prepare(qa_bot_goals *goals,const bot_goal_history *image,
    const qa_bot_memory_prepared *memory,bot_goal_history_restore **out,qa_error *error)
{
    if(!goals || !image || image->owner!=goals || goals->memory!=image->state.memory ||
       goals->options.maximum_states!=image->state.options.maximum_states ||
       goals->entities!=image->state.entities || goals->workspace!=image->state.workspace ||
       !memory || !out || *out || goals->prepared_indexes)
        return bot_goal_fail(error,"Complete goal restore differs from its actual captured owner");
    bot_goal_history_restore *plan=calloc(1,sizeof(*plan));
    if(!plan) {qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing complete goal reference history");return false;}
    plan->owner=goals;plan->image=image;
    if(!copy(&image->state,&plan->state,error)) {free(plan);return false;}
    for(uint32_t i=0;i<plan->state.options.maximum_states;++i)
        if(plan->state.states[i].used &&
           !qa_bot_memory_checkpoint_resolve(memory,plan->state.states[i].record.allocation,
               &plan->state.states[i].record.allocation,error)) goto failed;
    for(bot_goal_indexes *row=plan->state.indexes;row;row=row->next)
        if(!qa_bot_memory_checkpoint_resolve(memory,row->allocation,&row->allocation,error)) goto failed;
    for(size_t i=0;i<plan->state.info_count;++i)
        if(!qa_bot_memory_checkpoint_resolve(memory,plan->state.info[i].allocation,
            &plan->state.info[i].allocation,error)) goto failed;
    *out=plan;return true;
failed:
    clear(&plan->state);free(plan);return false;
}
void bot_goal_history_finish(bot_goal_history_restore *plan,bool commit)
{
    if(!plan) return;
    if(commit) {
        qa_bot_goals *goals=plan->owner;
        qa_bot_goals old=*goals;
        bool busy=goals->busy;*goals=plan->state;goals->busy=busy;
        clear(&old);
    } else clear(&plan->state);
    free(plan);
}
