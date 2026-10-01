#ifndef QA_APPLICATION_BOTS_KNOWLEDGE_H
#define QA_APPLICATION_BOTS_KNOWLEDGE_H
#include "bots_private.h"

typedef struct application_bot_knowledge_actor {
    uint32_t handle;
    qa_actor_id actor;
    application_provider *provider;
} application_bot_knowledge_actor;
typedef struct application_bot_knowledge_owner {
    application_bot_knowledge_actor *actors;
    size_t count,capacity;
} application_bot_knowledge_owner;

bool application_bots_knowledge_update(application_bots *,qa_actor_id,qa_error *);
qa_actor_id application_bots_knowledge_actor(const application_bots *,application_provider *,uint32_t);
void application_bots_knowledge_dispose(application_bots *);
#endif
