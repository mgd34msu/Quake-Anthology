#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_RECORDS_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_RECORDS_H
#include "guest_q3_mod.h"
#include "guest_q3_component_source.h"
#include "qa/inventory.h"

typedef struct application_q3_component_records application_q3_component_records;
typedef struct application_q3_component_records_options {
    application_q3_mod_profile *profile;
    qa_qvm *vm;
    const qa_qvm_image *image;
    qa_session *session;
    qa_world *world;
    qa_combat *combat;
    qa_inventory *inventory;
    qa_strings *strings;
    void *context;
    bool (*storage_current)(void *,qa_error *);
    /* The actual selected MATCH player owner implements these operations. */
    bool (*match_read)(void *,qa_actor_id,qa_string_id *team,double *score,qa_error *);
    bool (*match_write)(void *,qa_actor_id,bool team,qa_string_id,double,qa_error *);
    bool (*bound)(void *,qa_actor_id,uint32_t,bool owned,bool client,qa_error *);
    bool (*released)(void *,qa_actor_id,qa_error *);
    bool (*lifecycle_begin)(void *,uint32_t,const int32_t *,size_t,qa_error *);
    bool (*lifecycle)(void *,uint32_t,const int32_t *,size_t,int32_t,qa_error *);
} application_q3_component_records_options;

bool application_q3_component_records_create(const application_q3_component_records_options *,
    application_q3_component_records **,qa_error *);
bool application_q3_component_records_destroy(application_q3_component_records **,qa_error *);
bool application_q3_component_records_idle(const application_q3_component_records *);
/* Capture actual post-Initialize arrays, before any foreign projection. */
bool application_q3_component_records_defaults(application_q3_component_records *,qa_error *);
bool application_q3_component_records_bind(application_q3_component_records *,qa_actor_id,
    uint32_t slot,bool owned,bool client,qa_error *);
bool application_q3_component_records_reserve_client(application_q3_component_records *,qa_actor_id,uint32_t *,qa_error *);
bool application_q3_component_records_release(application_q3_component_records *,qa_actor_id,qa_error *);
bool application_q3_component_records_admitted(application_q3_component_records *,qa_actor_id,bool,qa_error *);
bool application_q3_component_records_pointer(void *,qa_actor_id,const char *,uint32_t *,qa_error *);
bool application_q3_component_records_eligible(void *,qa_actor_id);
bool application_q3_component_records_live_client(void *,qa_actor_id);
bool application_q3_component_records_client_slot(void *,qa_actor_id,int32_t *,qa_error *);
bool application_q3_component_records_prepare(void *,qa_error *);
bool application_q3_component_records_enter(void *,uint32_t,const int32_t *,size_t,void **,qa_error *);
bool application_q3_component_records_leave(void *,void **,bool,int32_t,qa_error *);
bool application_q3_component_records_refresh(application_q3_component_records *,qa_error *);
bool application_q3_component_records_checkpoint(application_q3_component_records *,qa_buffer *,qa_error *);
bool application_q3_component_records_restore(application_q3_component_records *,qa_bytes,qa_error *);
bool application_q3_component_records_validate(application_q3_component_records *,qa_error *);
#endif
