#ifndef QA_APPLICATION_BOT_WORLD_BIND_H
#define QA_APPLICATION_BOT_WORLD_BIND_H
#include "bot_world.h"

struct application_bots;
typedef struct application_bot_world_binding application_bot_world_binding;
bool application_bot_world_binding_create(struct application_bots *,
    application_bot_world_binding **,qa_error *);
void application_bot_world_binding_destroy(application_bot_world_binding *);
bool application_bot_world_binding_services(application_bot_world_binding *,
    application_bot_world_services *,qa_error *);
void application_bot_world_binding_holder(application_bot_world_binding *,application_bot_world *);
bool application_bot_world_binding_transport_client(void *,qa_actor_id,uint32_t *,qa_error *);
bool application_bot_world_binding_transport_drop(void *,uint32_t,const char *,qa_error *);

#endif
