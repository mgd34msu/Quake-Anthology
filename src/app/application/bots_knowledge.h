#ifndef QA_APPLICATION_BOTS_KNOWLEDGE_H
#define QA_APPLICATION_BOTS_KNOWLEDGE_H
#include "bots_private.h"
#include "qa/id_index.h"
#include "qa/network_unified_frame_pool.h"

typedef struct application_bot_knowledge_actor {
    uint32_t handle;
    qa_actor_id actor;
    application_provider *provider;
} application_bot_knowledge_actor;
typedef struct application_bot_knowledge_owner {
    application_bot_knowledge_actor *actors;
    size_t count,capacity;
    qa_unified_frame_lease *storage;
    qa_id_index handles;
} application_bot_knowledge_owner;

bool application_bots_knowledge_prepare(application_bots *,size_t,
    application_bot_knowledge_owner **,qa_error *);
bool application_bots_knowledge_reindex(application_bot_knowledge_owner *,qa_error *);
void application_bots_knowledge_release(application_bot_knowledge_owner *);
bool application_bots_knowledge_update(application_bots *,qa_actor_id,qa_error *);
void application_bots_knowledge_dispose(application_bots *);
#endif
