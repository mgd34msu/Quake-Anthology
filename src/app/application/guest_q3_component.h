#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_H
#include "guest_q3_component_records.h"
#include "qa/console_save.h"
#include "guest_q3_mod_operations.h"

typedef struct application_q3_component application_q3_component;
typedef struct application_q3_component_clients {
    void *context;
    bool (*current)(void *,qa_actor_id);
    bool (*userinfo)(void *,qa_actor_id,const char **,qa_error *);
    bool (*set_userinfo)(void *,qa_actor_id,const char *,qa_error *);
    bool (*command)(void *,qa_actor_id,const qa_q3_player *,qa_q3_usercmd *,qa_error *);
    bool (*active_command)(void *,qa_actor_id,const application_q3_mod_inputs *,
        const qa_q3_player *,qa_q3_usercmd *,qa_error *);
    bool (*drop)(void *,qa_actor_owner,qa_actor_id,const char *,qa_error *);
} application_q3_component_clients;

typedef struct application_q3_mod_operations application_q3_mod_operations;
typedef struct application_q3_component_options {
    /* These resources are the genuinely acquired catalog component artifacts.
     * The owner retains both; the host borrows that same selected content view. */
    qa_resource *program,*declaration;
    const char *program_path;
    qa_sha256_digest program_digest,declaration_digest;
    qa_qvm_image *image;
    qa_qvm_abi abi;
    qa_q3_host_options host;
    qa_combat *combat;
    qa_inventory *inventory;
    qa_q3_visibility_world visibility;
    uint64_t generation;
    void *context;
    bool (*current)(void *);
    bool (*storage_current)(void *);
    bool (*match_read)(void *,qa_actor_id,qa_string_id *,double *,qa_error *);
    bool (*match_write)(void *,qa_actor_id,bool,qa_string_id,double,qa_error *);
    application_q3_component_clients clients;
    application_q3_mod_operation_services operations[Q3_MOD_OPERATION_COUNT];
    application_q3_mod_operations *actor_operations;
    bool (*damage_context)(void *,qa_damage_request *,qa_error *);
    bool (*source_command_event)(void *,qa_actor_id,const char *,int32_t,qa_error *);
} application_q3_component_options;

bool application_q3_component_create(const application_q3_component_options *,bool restoring,
    application_q3_component **,qa_error *);
bool application_q3_component_initialize(application_q3_component *,qa_error *);
bool application_q3_component_idle(const application_q3_component *);
bool application_q3_component_destroy(application_q3_component **,qa_error *);
bool application_q3_component_admit(application_q3_component *,qa_actor_id,qa_error *);
bool application_q3_component_userinfo(application_q3_component *,qa_actor_id,qa_error *);
bool application_q3_component_client_current(const application_q3_component *,qa_actor_id);
bool application_q3_component_disconnect(application_q3_component *,qa_actor_id,qa_error *);
bool application_q3_component_command(application_q3_component *,qa_actor_id,
    const qa_command_invocation *,bool *,qa_error *);
bool application_q3_component_activate(application_q3_component *,qa_error *);
bool application_q3_component_callbacks_register(application_q3_component *,qa_error *);
bool application_q3_component_frame(application_q3_component *,const qa_source_frame *,qa_error *);
bool application_q3_component_actor_released(application_q3_component *,qa_actor_record,qa_error *);
bool application_q3_component_touch(application_q3_component *,const qa_touch_contact *,bool *,qa_error *);
bool application_q3_component_actor_callback(application_q3_component *,application_q3_mod_operation,
    const application_q3_mod_actor_request *,bool *,qa_error *);
bool application_q3_component_combat_binding(application_q3_component *,qa_actor_id,uint64_t,qa_combat_binding *,qa_error *);
application_q3_component_source *application_q3_component_source_read(application_q3_component *);
application_q3_mod *application_q3_component_mod(application_q3_component *);
qa_console *application_q3_component_console(application_q3_component *,qa_cvars **);
const application_q3_mod_profile *application_q3_component_profile(const application_q3_component *);
bool application_q3_component_checkpoint(application_q3_component *,qa_buffer *,qa_error *);
bool application_q3_component_restore(application_q3_component *,qa_bytes,const qa_console_save_resolvers *,qa_error *);
bool application_q3_component_finish_restore(application_q3_component *,qa_error *);
#endif
