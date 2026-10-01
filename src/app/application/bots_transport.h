#ifndef QA_APPLICATION_BOTS_TRANSPORT_H
#define QA_APPLICATION_BOTS_TRANSPORT_H
#include "bot_world.h"

typedef struct application_bot_transport application_bot_transport;
typedef struct application_bot_transport_services {
    void *context;
    application_bot_world *world;
    qa_session *session;
    bool (*client)(void *,qa_actor_id,uint32_t *,qa_error *);
    bool (*drop)(void *,uint32_t,const char *,qa_error *);
} application_bot_transport_services;
bool application_bot_transport_create(const application_bot_transport_services *,application_bot_transport **,qa_error *);
bool application_bot_transport_destroy(application_bot_transport *,qa_error *);
bool application_bot_transport_can_destroy(const application_bot_transport *);
bool application_bot_transport_open(application_bot_transport *,uint32_t,qa_actor_id,qa_error *);
bool application_bot_transport_close(application_bot_transport *,uint32_t,qa_error *);
qa_actor_id application_bot_transport_actor(const application_bot_transport *,uint32_t);
bool application_bot_transport_message(application_bot_transport *,int32_t,const char *,qa_error *);
bool application_bot_transport_console(application_bot_transport *,uint32_t,char *,size_t,bool *,qa_error *);
bool application_bot_transport_snapshot(application_bot_transport *,uint32_t,int32_t,int32_t *,qa_error *);
bool application_bot_transport_frame(application_bot_transport *,double elapsed_ms,qa_error *);
bool application_bot_transport_fields(qa_source_save_io *,application_bot_transport *);
#endif
