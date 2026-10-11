#ifndef QA_APPLICATION_GUEST_Q3_MOD_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_MOD_PRIVATE_H
#include "guest_q3_mod.h"
#include "qa/json.h"
#include "qa/binary.h"
#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum mod_scalar { MOD_INT32, MOD_FLOAT32, MOD_VOID } mod_scalar;
typedef enum mod_argument_kind { MOD_SCALAR, MOD_VECTOR, MOD_STRING, MOD_ACTOR, MOD_CLIENT, MOD_TIME, MOD_ADDRESS } mod_argument_kind;
typedef struct mod_argument {
    mod_argument_kind kind;
    mod_scalar encoding;
    bool input, milliseconds;
    application_q3_mod_input name;
    application_q3_mod_value literal;
    size_t record;
    uint32_t address;
} mod_argument;
typedef struct mod_global { uint32_t address; mod_argument value; } mod_global;
typedef struct application_q3_mod_call {
    application_q3_mod_profile *profile;
    uint32_t entry;
    mod_scalar returns;
    mod_argument *arguments;
    size_t argument_count;
    mod_global *globals;
    size_t global_count;
} mod_call;
typedef struct mod_call_group { mod_call *calls; size_t count; } mod_call_group;
typedef struct mod_record_field { uint32_t offset, length; bool private_field; } mod_record_field;
typedef struct mod_record {
    char *id;
    uint32_t address, stride, capacity;
    bool client;
    mod_record_field *fields;
    size_t field_count;
} mod_record;
typedef struct mod_pointer { bool argument; uint32_t root, offset; uint32_t *indirections; size_t count; } mod_pointer;
typedef struct mod_output {
    enum { MOD_FIELD, MOD_HANDLER, MOD_COMMAND } kind;
    size_t record;
    uint32_t offset, entry, inputs;
    application_q3_mod_input *ordered_inputs;
    size_t input_count;
    application_q3_mod_input input;
    mod_scalar encoding;
    double scale, returned;
    bool has_return;
    mod_pointer actor, command;
} mod_output;
typedef struct mod_input_binding {
    bool slice, before;
    mod_call *calls;
    size_t call_count;
    mod_output *outputs;
    size_t output_count;
} mod_input_binding;
typedef struct mod_field { size_t record; uint32_t offset; mod_scalar encoding; } mod_field;
typedef struct mod_selection_value { double value; uint32_t selected; } mod_selection_value;
typedef struct mod_selection {
    mod_field field;
    bool masked;
    uint32_t mask;
    mod_selection_value *values;
    size_t count;
} mod_selection;
typedef struct mod_protection {
    qa_protection_channel channel;
    qa_protection_claim claim;
    mod_call absorb;
    uint32_t no_armor, no_power, no_regular, energy, radius;
    mod_field count;
    bool has_selection;
    qa_item_id item;
    mod_selection selection;
} mod_protection;
typedef struct mod_callback {
    qa_string_id id;
    application_q3_mod_operation operation;
    qa_operation_hook_kind stage;
    bool knockback;
    mod_call call;
} mod_callback;
typedef struct mod_pickup_context { size_t record; uint32_t offset; mod_argument value; } mod_pickup_context;
typedef struct mod_pickup {
    qa_string_id id;
    qa_item_id *offered;
    size_t offered_count;
    qa_pickup_write *writes;
    size_t write_count;
    mod_call gate, grant;
    bool gated, always;
    mod_pickup_context *context;
    size_t context_count;
} mod_pickup;
struct application_q3_mod_profile {
    qa_qvm_image *image;
    qa_buffer declaration;
    qa_qvm_abi abi;
    bool clients;
    uint32_t maximum;
    size_t entity_record, player_record;
    mod_record *records;
    size_t record_count;
    mod_input_binding *inputs;
    size_t input_count;
    mod_protection *protection;
    size_t protection_count;
    mod_callback *callbacks;
    size_t callback_count;
    mod_pickup *pickups;
    size_t pickup_count;
    uint32_t *entries;
    size_t entry_count;
    mod_call_group stages[Q3_MOD_STAGE_COUNT];
};
typedef struct mod_actor_channel {
    struct mod_actor_channel *next;
    application_q3_mod *owner;
    qa_actor_id actor;
    int32_t client;
    size_t definition;
    qa_protection_lease lease;
    bool bound;
} mod_actor_channel;
typedef struct mod_registered_callback {
    application_q3_mod *owner;
    const mod_callback *definition;
    qa_operation_registration registration;
} mod_registered_callback;
typedef struct mod_protection_stage mod_protection_stage;
typedef struct mod_source_lease {
    struct mod_source_lease *next;
    void *scope;
    qa_qvm_word_projection *globals;
    bool succeeded;
    int32_t result;
} mod_source_lease;
typedef struct mod_pickup_bound { struct mod_pickup_actor *actor; const mod_pickup *definition; } mod_pickup_bound;
typedef struct mod_pickup_actor {
    struct mod_pickup_actor *next;
    application_q3_mod *owner;
    qa_actor_id actor;
    qa_pickup_lease lease;
    qa_pickup_rule *rules;
    mod_pickup_bound *bindings;
} mod_pickup_actor;
struct application_q3_mod {
    application_q3_mod_profile *profile;
    qa_qvm *vm;
    qa_session *session;
    qa_actor_owner owner;
    qa_combat *combat;
    application_q3_mod_services services;
    application_q3_mod_application *application;
    application_q3_mod_capture *capture;
    mod_actor_channel *channels;
    mod_registered_callback *callbacks;
    mod_protection_stage *stages;
    mod_source_lease *source_calls;
    mod_pickup_actor *pickup_actors;
    unsigned pickup_calls;
    unsigned calls;
    bool restoring, restored_owner, active, callbacks_active, restored_callbacks, closing, failed_scope;
};
bool q3mod_fail(qa_error *, qa_status, const char *);
bool q3mod_saved_declaration(const application_q3_mod_profile *, qa_source_save_io *,
    const char identity[4]);
bool q3mod_current(application_q3_mod *, qa_error *);
bool q3mod_storage_current(application_q3_mod *, qa_error *);
bool q3mod_address(application_q3_mod *, qa_actor_id, size_t, uint32_t,
    size_t, uint32_t *, qa_error *);
bool q3mod_scalar_word(double, mod_scalar, int32_t *, qa_error *);
bool q3mod_invoke(application_q3_mod *, const mod_call *, const application_q3_mod_inputs *, double *, qa_error *);
bool q3mod_invoke_started(application_q3_mod *, const mod_call *, const application_q3_mod_inputs *, double *, bool *, qa_error *);
bool q3mod_callbacks_activate(application_q3_mod *, qa_error *);
bool q3mod_callbacks_close(application_q3_mod *, qa_error *);
bool q3mod_protection_read(mod_actor_channel *, qa_armor *, qa_error *);
bool q3mod_protection_close(application_q3_mod *, qa_error *);
bool q3mod_protection_stages_close(application_q3_mod *, qa_error *);
bool q3mod_protection_observe(application_q3_mod *, qa_actor_id, qa_protection_observer *,
    const mod_call *, const application_q3_mod_inputs *, double *, qa_error *);
bool q3mod_pickup_observe(application_q3_mod *, qa_actor_id, qa_pickup_execution *,
    const mod_call *, const application_q3_mod_inputs *, double *, qa_error *);
bool q3mod_pickup_run(application_q3_mod *,const mod_pickup *,const qa_pickup_offer *,
    qa_pickup_execution *,qa_pickup_outcome *,qa_error *);
bool q3mod_pickups_admit(application_q3_mod *,qa_actor_id,qa_error *);
bool q3mod_pickups_release(application_q3_mod *,qa_actor_id,qa_error *);
bool q3mod_pickups_close(application_q3_mod *,qa_error *);
bool q3mod_pickups_fields(application_q3_mod *,qa_source_save_io *);
bool q3mod_pickups_validate(application_q3_mod *,qa_error *);

#endif
