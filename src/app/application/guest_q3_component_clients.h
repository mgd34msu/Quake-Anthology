#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_CLIENTS_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_CLIENTS_H
#include "internal.h"
#include "guest_q3_component.h"

typedef struct application_q3_component_client_adapter application_q3_component_client_adapter;
typedef struct application_q3_component_client_drop {
    qa_actor_owner component,source;
    qa_actor_id actor;
    uint32_t source_slot;
    const char *reason;
} application_q3_component_client_drop;
bool application_q3_component_client_adapter_create(qa_application *,application_provider *,qa_world *,
    application_q3_component_client_adapter **,qa_error *);
bool application_q3_component_client_adapter_destroy(application_q3_component_client_adapter **,qa_error *);
bool application_q3_component_client_adapter_idle(const application_q3_component_client_adapter *);
bool application_q3_component_client_adapter_transport_bind(application_q3_component_client_adapter *,
    void *,bool (*)(void *,qa_actor_owner,qa_actor_id,const char *,qa_error *),qa_error *);
application_q3_component_clients application_q3_component_client_adapter_services(application_q3_component_client_adapter *);
bool application_q3_component_client_match_read(void *,qa_actor_id,qa_string_id *,double *,qa_error *);
bool application_q3_component_client_match_write(void *,qa_actor_id,bool,qa_string_id,double,qa_error *);
/* Returned source execution drains these requests through the physical client
 * disconnect and canonical roster release. Pending requests prevent capture. */
bool application_q3_component_client_adapter_drain(application_q3_component_client_adapter *,qa_error *);
bool application_q3_component_client_drop_read(const application_q3_component_client_adapter *,
    application_q3_component_client_drop *,bool *present,qa_error *);
#endif
