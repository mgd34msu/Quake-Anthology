#include "bots_knowledge.h"
#include "control_frame.h"
#include <string.h>

static size_t find(const application_bot_knowledge_owner *owner,uint32_t handle)
{
    qa_id_index_cursor cursor=qa_id_index_begin(&owner->handles,QA_STRING_NONE,handle);
    size_t at;
    if(qa_id_index_next(&owner->handles,&cursor,&at)) return at;
    return owner->count;
}

void application_bots_knowledge_release(application_bot_knowledge_owner *owner)
{
    if(!owner) return;
    qa_unified_frame_lease *storage=owner->storage;
    qa_id_index_destroy(&owner->handles);
    qa_unified_frame_lease_release(storage);
}

bool application_bots_knowledge_prepare(application_bots *bots,size_t capacity,
    application_bot_knowledge_owner **out,qa_error *error)
{
    qa_unified_frame_lease *storage=application_control_storage_acquire(bots->application,error);
    if(!storage) return false;
    application_bot_knowledge_owner *owner=qa_unified_frame_lease_alloc(storage,1,
        sizeof(*owner),_Alignof(application_bot_knowledge_owner),error);
    if(!owner){qa_unified_frame_lease_release(storage);return false;}
    owner->storage=storage;
    owner->handles.storage=qa_unified_frame_lease_arena(storage);
    owner->actors=qa_unified_frame_lease_alloc(storage,capacity,sizeof(*owner->actors),
        _Alignof(application_bot_knowledge_actor),error);
    if((capacity&&!owner->actors)||!qa_id_index_reserve(&owner->handles,capacity,error)) {
        application_bots_knowledge_release(owner);return false;
    }
    owner->capacity=capacity;*out=owner;return true;
}

bool application_bots_knowledge_reindex(application_bot_knowledge_owner *owner,qa_error *error)
{
    qa_id_index_clear(&owner->handles);
    if(!qa_id_index_reserve(&owner->handles,owner->count,error)) return false;
    for(size_t i=0;i<owner->count;++i) {
        uint32_t handle=owner->actors[i].handle;
        if(qa_id_index_find(&owner->handles,QA_STRING_NONE,handle))
            return application_fail(error,QA_ERROR_FORMAT,"Duplicate retained weapon-handle knowledge key");
        qa_id_index_insert(&owner->handles,QA_STRING_NONE,handle,i);
    }
    return true;
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
        if(!application_bots_knowledge_prepare(bots,
            qa_actors_capacity(qa_session_actors(bots->application->session)),&owner,error)) return false;
        bots->knowledge_owner=owner;
    }
    size_t at=find(owner,handle);
    if(!provider) {
        if(at<owner->count) {
            memmove(owner->actors+at,owner->actors+at+1,(owner->count-at-1)*sizeof(*owner->actors));
            --owner->count;
            qa_id_index_remove(&owner->handles,QA_STRING_NONE,handle);
            for(size_t i=0;i<owner->handles.count;++i)
                if(owner->handles.entries[i].value>at) --owner->handles.entries[i].value;
        }
        return true;
    }
    if(at==owner->count) {
        if(!qa_id_index_reserve(&owner->handles,1,error)) return false;
        if(owner->count==owner->capacity) {
            size_t capacity=owner->capacity?owner->capacity*2:1;
            if(capacity<owner->capacity || capacity>SIZE_MAX/sizeof(*owner->actors))
                return application_fail(error,QA_ERROR_MEMORY,"Weapon-handle knowledge allocation exceeds native extent");
            application_bot_knowledge_actor *actors=qa_arena_grow(
                qa_unified_frame_lease_arena(owner->storage),owner->actors,
                owner->count*sizeof(*actors),capacity*sizeof(*actors),
                _Alignof(application_bot_knowledge_actor),error);
            if(!actors) return false;
            owner->actors=actors;owner->capacity=capacity;
        }
        qa_id_index_insert(&owner->handles,QA_STRING_NONE,handle,at);
        ++owner->count;
    }
    owner->actors[at]=(application_bot_knowledge_actor){.handle=handle,.actor=actor,.provider=provider};
    return true;
}

void application_bots_knowledge_dispose(application_bots *bots)
{
    if(!bots || !bots->knowledge_owner) return;
    application_bots_knowledge_release(bots->knowledge_owner);bots->knowledge_owner=NULL;
}
