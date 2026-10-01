#include "source_weapon_history.h"
#include <stdlib.h>
#include <string.h>

struct bot_weapon_pointer_history {
    bot_weapon_pointers *owner;
    bot_weapon_pointers state;
    bot_weapon_state *states;
    uint32_t count;
    qa_bot_weapons *config;
};
struct bot_weapon_pointer_restore {
    bot_weapon_pointers *owner;
    bot_weapon_pointers state;
    bot_weapon_state *states,*destination;
    uint32_t count;
    qa_bot_weapons *config,**config_destination;
};
static bool copy(const bot_weapon_pointers *source,bot_weapon_pointers *out,qa_error *error) {
    *out=(bot_weapon_pointers){.next_pointer=source->next_pointer};
    for(const bot_weapon_config_identity *row=source->configs;row;row=row->next) {
        bot_weapon_config_identity *next=malloc(sizeof(*next));
        if(!next) goto failed;
        *next=*row;next->next=NULL;qa_bot_weights_retain(next->config);
        if(out->last_config) out->last_config->next=next;else out->configs=next;
        out->last_config=next;
    }
    for(const bot_weapon_pointer *row=source->first;row;row=row->next) {
        bot_weapon_pointer *next=malloc(sizeof(*next));
        if(!next) goto failed;
        *next=*row;next->next=NULL;
        if(next->kind==BOT_WEAPON_POINTER_CONFIG) qa_bot_weights_retain(next->config);
        if(out->last) out->last->next=next;else out->first=next;
        out->last=next;
    }
    return true;
failed:
    bot_weapon_pointers_clear(out);
    qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source weapon pointer history");return false;
}
void bot_weapon_pointer_history_destroy(bot_weapon_pointer_history *image) {
    if(!image) return;
    bot_weapon_pointers_clear(&image->state);qa_bot_weapons_release(image->config);free(image->states);free(image);
}
bool bot_weapon_pointer_capture(bot_weapon_pointers *pointers,bot_weapon_state *states,uint32_t count,
    qa_bot_weapons *config,bot_fuzzy_history *fuzzy,
    bot_weapon_pointer_history **out,qa_error *error) {
    if(!pointers || !states || !count || count>SIZE_MAX/sizeof(*states) || !fuzzy || !out || *out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Weapon pointer capture requires actual owners and empty output");return false;
    }
    bot_weapon_pointer_history *image=calloc(1,sizeof(*image));
    if(!image) {qa_error_set(error,QA_ERROR_MEMORY,0,"Capturing source weapon pointer owner");return false;}
    image->owner=pointers;
    image->count=count;image->config=config;qa_bot_weapons_retain(config);
    image->states=malloc((size_t)count*sizeof(*states));
    if(!image->states) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Capturing all physical source weapon states");
        bot_weapon_pointer_history_destroy(image);return false;
    }
    memcpy(image->states,states,(size_t)count*sizeof(*states));
    for(uint32_t i=0;i<count;++i) if(states[i].used) {
        bot_weapon_record qualified;
        if(!bot_weapon_record_bind(states[i].record.memory,states[i].record.allocation,&qualified,error)) {
            bot_weapon_pointer_history_destroy(image);return false;
        }
    }
    if(!copy(pointers,&image->state,error)) {bot_weapon_pointer_history_destroy(image);return false;}
    for(bot_weapon_config_identity *row=pointers->configs;row;row=row->next)
        if(!bot_fuzzy_history_include(fuzzy,row->config,error)) {
            bot_weapon_pointer_history_destroy(image);return false;
        }
    for(bot_weapon_pointer *row=pointers->first;row;row=row->next)
        if(row->kind==BOT_WEAPON_POINTER_CONFIG && !bot_fuzzy_history_include(fuzzy,row->config,error)) {
            bot_weapon_pointer_history_destroy(image);return false;
        }
    *out=image;return true;
}
bool bot_weapon_pointer_prepare(bot_weapon_pointers *pointers,bot_weapon_state *states,uint32_t count,
    qa_bot_weapons **config,const bot_weapon_pointer_history *image,
    const qa_bot_memory_prepared *memory,bot_weapon_pointer_restore **out,qa_error *error) {
    if(!pointers || !states || !config || !image || image->owner!=pointers || count!=image->count || !memory || !out || *out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Weapon pointer restore differs from its captured owner");return false;
    }
    bot_weapon_pointer_restore *plan=calloc(1,sizeof(*plan));
    if(!plan) {qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing source weapon pointer aliases");return false;}
    plan->owner=pointers;
    if(!copy(&image->state,&plan->state,error)) {free(plan);return false;}
    plan->destination=states;plan->count=count;plan->config_destination=config;
    plan->config=image->config;qa_bot_weapons_retain(plan->config);
    plan->states=malloc((size_t)count*sizeof(*states));
    if(!plan->states) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing all physical source weapon states");
        bot_weapon_pointer_finish(plan,false);return false;
    }
    memcpy(plan->states,image->states,(size_t)count*sizeof(*states));
    for(uint32_t i=0;i<count;++i) if(plan->states[i].used &&
       !qa_bot_memory_checkpoint_resolve(memory,plan->states[i].record.allocation,&plan->states[i].record.allocation,error)) {
        bot_weapon_pointer_finish(plan,false);return false;
    }
    for(bot_weapon_pointer *row=plan->state.first;row;row=row->next)
        if(row->kind==BOT_WEAPON_POINTER_INDEXES &&
           !qa_bot_memory_checkpoint_resolve(memory,row->indexes,&row->indexes,error)) {
            bot_weapon_pointer_finish(plan,false);return false;
        }
    *out=plan;return true;
}
void bot_weapon_pointer_finish(bot_weapon_pointer_restore *plan,bool commit) {
    if(!plan) return;
    if(commit) {
        bot_weapon_pointers old=*plan->owner;*plan->owner=plan->state;
        bot_weapon_pointers_clear(&old);
        memcpy(plan->destination,plan->states,(size_t)plan->count*sizeof(*plan->states));
        qa_bot_weapons_release(*plan->config_destination);*plan->config_destination=plan->config;
    } else {bot_weapon_pointers_clear(&plan->state);qa_bot_weapons_release(plan->config);}
    free(plan->states);free(plan);
}
