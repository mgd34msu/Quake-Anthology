#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_PRIVATE_H
#include "guest_q3_component.h"
#include "guest_q3_component_records_private.h"
#include "guest_q3_mod_actors.h"
#include "qa/vfs.h"

typedef struct component_hook {
    application_q3_component *owner;
    uint32_t entry;
    qa_qvm_binding id;
    bool middleware,allocate,release,frame,actor;
} component_hook;
typedef struct component_call_lease {
    struct component_call_lease *next;
    void *scope;
    application_q3_mod_entry *middleware;
    bool succeeded;
    int32_t result;
} component_call_lease;
typedef struct component_initial_store { uint32_t address; int32_t value; } component_initial_store;
struct application_q3_component {
    application_q3_component_options options;
    application_q3_mod_profile *profile;
    application_q3_mod *mod;
    application_q3_component_records *records;
    application_q3_component_source *source;
    application_q3_mod_actors *actor_semantics;
    qa_qvm_binding actor_resolver;
    qa_error actor_error;
    qa_qvm *vm;
    qa_q3_host *host;
    qa_qvm_options lower;
    qa_cvars *cvars;
    qa_console *console;
    const qa_command_invocation *arguments;
    component_hook *hooks;
    size_t hook_count;
    component_call_lease *calls;
    component_initial_store *initial_stores;
    size_t initial_store_count;
    size_t entity_record,player_record;
    uint32_t maximum,allocate_entry,release_entry,release_argument,inuse;
    uint32_t frame_entry,frame_end;
    bool frame_taken,has_actor_frame,actor_frame_active,actor_frame_completed;
    uint32_t *frame_branches,*frame_locals;
    size_t frame_branch_count;
    qa_string_id definition;
    int32_t milliseconds;
    bool initialized,restoring,closing,busy,has_source,draining,scene;
    bool restored_storage,restored_actors,restored_mod,restored_callbacks;
};
bool q3component_storage(void *,qa_error *);
bool q3component_current(void *,qa_error *);
bool q3component_services(application_q3_component *,qa_error *);
bool q3component_lifecycle_begin(void *,uint32_t,const int32_t *,size_t,qa_error *);
bool q3component_lifecycle(void *,uint32_t,const int32_t *,size_t,int32_t,qa_error *);
bool q3component_bind_hooks(application_q3_component *,qa_error *);
bool q3component_descriptors(application_q3_component *,qa_qvm_saved_function *,qa_error *);
bool q3component_bootstrap_profile(application_q3_component *,qa_error *);
bool q3component_frame_profile(application_q3_component *,qa_error *);
bool q3component_frame_proceed(application_q3_component *,const qa_qvm_call *,int32_t *,qa_error *);
bool q3component_call(application_q3_component *,uint32_t,const int32_t *,size_t,int32_t *,qa_error *);
bool q3component_actors_create(application_q3_component *,qa_error *);
qa_qvm_function_hook q3component_actor_resolve(void *,const qa_qvm_call *,void **);
#endif
