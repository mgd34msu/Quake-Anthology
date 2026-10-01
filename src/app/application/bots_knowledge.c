#include "bots_knowledge.h"
#include <stdlib.h>
#include <string.h>

static size_t find(const application_bot_knowledge_owner *owner,uint32_t handle)
{
    for(size_t i=0;i<owner->count;++i) if(owner->actors[i].handle==handle) return i;
    return owner->count;
}

bool application_bots_knowledge_update(application_bots *bots,qa_actor_id actor,qa_error *error)
{
    if(!bots || !bots->population || bots->restoring)
        return application_fail(error,QA_ERROR_ARGUMENT,"Knowledge inventory update requires its actual BotState owner");
    uint32_t handle;
    if(!qa_bots_source_weapon_handle(bots->population,actor,&handle,error)) return false;
    application_provider *provider=application_provider_for(bots->application,actor,QA_ROLE_ARSENAL,NULL);
    if(provider && provider->kind!=APPLICATION_PROVIDER_Q1 && provider->kind!=APPLICATION_PROVIDER_Q2 &&
       provider->kind!=APPLICATION_PROVIDER_Q3)
        return application_fail(error,QA_ERROR_UNSUPPORTED,"Knowledge inventory has no admitted native selected arsenal");
    if(provider && provider->kind==APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state state;
        if(!qa_q3_player_read(provider->state.q3,actor,&state)) provider=NULL;
    }
    application_bot_knowledge_owner *owner=bots->knowledge_owner;
    if(!owner) {
        if(!provider) return true;
        owner=calloc(1,sizeof(*owner));
        if(!owner) return application_fail(error,QA_ERROR_MEMORY,"Allocating actual weapon-handle knowledge owner");
        bots->knowledge_owner=owner;
    }
    size_t at=find(owner,handle);
    if(!provider) {
        if(at<owner->count) {
            memmove(owner->actors+at,owner->actors+at+1,(owner->count-at-1)*sizeof(*owner->actors));
            --owner->count;
        }
        return true;
    }
    if(at==owner->count) {
        if(owner->count==owner->capacity) {
            size_t capacity=owner->capacity?owner->capacity*2:8;
            if(capacity<owner->capacity || capacity>SIZE_MAX/sizeof(*owner->actors))
                return application_fail(error,QA_ERROR_MEMORY,"Weapon-handle knowledge allocation exceeds native extent");
            application_bot_knowledge_actor *actors=realloc(owner->actors,capacity*sizeof(*actors));
            if(!actors) return application_fail(error,QA_ERROR_MEMORY,"Growing retained weapon-handle knowledge map");
            owner->actors=actors;owner->capacity=capacity;
        }
        ++owner->count;
    }
    owner->actors[at]=(application_bot_knowledge_actor){.handle=handle,.actor=actor,.provider=provider};
    return true;
}

qa_actor_id application_bots_knowledge_actor(const application_bots *bots,application_provider *provider,uint32_t handle)
{
    const application_bot_knowledge_owner *owner=bots?bots->knowledge_owner:NULL;
    size_t at=owner?find(owner,handle):0;
    return owner && at<owner->count && owner->actors[at].provider==provider?
        owner->actors[at].actor:(qa_actor_id){0};
}

void application_bots_knowledge_dispose(application_bots *bots)
{
    if(!bots || !bots->knowledge_owner) return;
    free(bots->knowledge_owner->actors);free(bots->knowledge_owner);bots->knowledge_owner=NULL;
}
