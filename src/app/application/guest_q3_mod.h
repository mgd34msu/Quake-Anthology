#ifndef QA_APPLICATION_GUEST_Q3_MOD_H
#define QA_APPLICATION_GUEST_Q3_MOD_H

#include "qa/qvm_save.h"
#include "qa/session.h"
#include "qa/gameplay.h"
#include "qa/operation.h"
#include "qa/source_save.h"
#include "qa/network_q3.h"

typedef struct application_q3_mod_profile application_q3_mod_profile;
typedef struct application_q3_mod application_q3_mod;
typedef struct application_q3_mod_application application_q3_mod_application;
typedef struct application_q3_mod_capture application_q3_mod_capture;
typedef struct application_q3_mod_entry application_q3_mod_entry;
typedef struct application_q3_mod_call application_q3_mod_call;
typedef enum application_q3_mod_input {
    Q3_MOD_VIEW_ANGLES, Q3_MOD_ATTACK, Q3_MOD_JUMP, Q3_MOD_IMPULSE,
    Q3_MOD_FORWARD, Q3_MOD_SIDE, Q3_MOD_UP, Q3_MOD_INPUT_COUNT,
    Q3_MOD_SELF = Q3_MOD_INPUT_COUNT, Q3_MOD_OTHER, Q3_MOD_ACTIVATOR,
    Q3_MOD_ATTACKER, Q3_MOD_INFLICTOR, Q3_MOD_AMOUNT, Q3_MOD_DAMAGE_FLAGS,
    Q3_MOD_PROTECTION_SCALE, Q3_MOD_KNOCKBACK, Q3_MOD_POINT, Q3_MOD_DIRECTION,
    Q3_MOD_NORMAL, Q3_MOD_ITEM, Q3_MOD_TIME, Q3_MOD_ELAPSED, Q3_MOD_RESULT,
    Q3_MOD_PICKUP_COUNT, Q3_MOD_PICKUP_HAS_COUNT, Q3_MOD_PICKUP_DROPPED,
    Q3_MOD_VALUE_COUNT
} application_q3_mod_input;
typedef enum application_q3_mod_value_kind {
    Q3_MOD_VALUE_ABSENT, Q3_MOD_VALUE_SCALAR, Q3_MOD_VALUE_VECTOR,
    Q3_MOD_VALUE_STRING, Q3_MOD_VALUE_ACTOR
} application_q3_mod_value_kind;
typedef struct application_q3_mod_value {
    application_q3_mod_value_kind kind;
    /* Strings are NUL-terminated UTF-8 values; source lowering applies the
     * original QVM low-byte UTF-16 code-unit copy rather than UTF-8 bytes. */
    union { double scalar; qa_vec3 vector; const char *string; qa_actor_id actor; } as;
} application_q3_mod_value;
typedef struct application_q3_mod_inputs {
    application_q3_mod_value values[Q3_MOD_VALUE_COUNT];
} application_q3_mod_inputs;
typedef struct application_q3_mod_output {
    bool consume;
    application_q3_mod_input input;
    union { double scalar; qa_vec3 angles; uint32_t inputs; } value;
} application_q3_mod_output;
typedef enum application_q3_mod_operation {
    Q3_MOD_DAMAGE, Q3_MOD_GIVE, Q3_MOD_CONSUME, Q3_MOD_THINK,
    Q3_MOD_TOUCH, Q3_MOD_USE, Q3_MOD_PAIN, Q3_MOD_DIE,
    Q3_MOD_OPERATION_COUNT
} application_q3_mod_operation;
typedef enum application_q3_mod_stage {
    Q3_MOD_INITIALIZE, Q3_MOD_CLIENT_ADMIT, Q3_MOD_CLIENT_USERINFO,
    Q3_MOD_CLIENT_DISCONNECT, Q3_MOD_CLIENT_FRAME, Q3_MOD_SOURCE_UPDATE,
    Q3_MOD_SOURCE_FRAME, Q3_MOD_STAGE_COUNT
} application_q3_mod_stage;
/* The operation owner supplies its actual request/result layout. Lowering
 * copies callback-duration values; replace resumes the same native next token.
 * No surrogate operation or source actor is created by this owner. */
typedef struct application_q3_mod_operation_services {
    qa_operation *operation;
    void *context;
    bool (*inputs)(void *, const void *request, const void *result,
        application_q3_mod_inputs *, qa_error *);
    bool (*transform)(void *, void *request, bool knockback, double, qa_error *);
    bool (*replace)(void *, void *result, bool, qa_error *);
} application_q3_mod_operation_services;
typedef struct application_q3_mod_services {
    void *context;
    bool (*current)(void *, qa_error *);
    /* Pure retained RAM/source identity proof, including checked retirement
     * and imported storage before executable activation. */
    bool (*storage_current)(void *, qa_error *);
    bool (*pointer)(void *, qa_actor_id, const char *record, uint32_t *, qa_error *);
    bool (*eligible_actor)(void *, qa_actor_id);
    bool (*live_client)(void *, qa_actor_id);
    bool (*client_slot)(void *, qa_actor_id, int32_t *, qa_error *);
    bool (*player_state)(void *, qa_actor_id, qa_q3_player *, qa_error *);
    bool (*time)(void *, double *seconds, qa_error *);
    /* These are the actual component projection owner, shared by every
     * lifecycle/input/protection/callback call. Scope is an owned heap token.
     * Leave nulls a consumed token; refusal retains it for checked retirement. */
    bool (*source_prepare)(void *, qa_error *);
    bool (*source_enter)(void *, uint32_t instruction, const int32_t *, size_t,
        void **scope, qa_error *);
    bool (*source_leave)(void *, void **scope, bool succeeded, int32_t result, qa_error *);
    application_q3_mod_operation_services operations[Q3_MOD_OPERATION_COUNT];
} application_q3_mod_services;

bool application_q3_mod_profile_create(qa_qvm_image *, qa_qvm_abi,
    const char *artifact_path, qa_bytes declaration, qa_strings *,
    application_q3_mod_profile **, qa_error *);
void application_q3_mod_profile_destroy(application_q3_mod_profile *);
qa_bytes application_q3_mod_declaration(const application_q3_mod_profile *);
/* The profile outlives its runtime; Factory owns both checked lifetimes. */
bool application_q3_mod_create(application_q3_mod_profile *, qa_qvm *,
    qa_session *, qa_actor_owner, qa_combat *, const application_q3_mod_services *,
    bool restoring, application_q3_mod **, qa_error *);
bool application_q3_mod_destroy(application_q3_mod **, qa_error *);
bool application_q3_mod_idle(const application_q3_mod *);
/* Exact declared record inventory for the source projection constructor. */
size_t application_q3_mod_record_count(const application_q3_mod_profile *);
bool application_q3_mod_record(const application_q3_mod_profile *, size_t,
    const char **id, uint32_t *address, uint32_t *stride, uint32_t *capacity);
bool application_q3_mod_clients(const application_q3_mod_profile *, uint32_t *maximum,
    const char **entity_record, const char **player_record);
bool application_q3_mod_record_is_client(const application_q3_mod_profile *, size_t);
/* Component lifecycle uses these same artifact-qualified calls. A bit in
 * available_inputs is 1u << application_q3_mod_input. */
bool application_q3_mod_call_create(application_q3_mod_profile *, qa_bytes,
    uint32_t available_inputs, application_q3_mod_call **, qa_error *);
void application_q3_mod_call_destroy(application_q3_mod_call *);
uint32_t application_q3_mod_call_instruction(const application_q3_mod_call *);
bool application_q3_mod_call_run(application_q3_mod *, const application_q3_mod_call *,
    const application_q3_mod_inputs *, double *, qa_error *);
bool application_q3_mod_stage_run(application_q3_mod *, application_q3_mod_stage,
    const application_q3_mod_inputs *, qa_error *);

/* First-occurrence entry order is the source declaration's physical order.
 * Factory composes shared entries and owns the one physical hook. */
size_t application_q3_mod_entry_count(const application_q3_mod *);
bool application_q3_mod_entry_instruction(const application_q3_mod *, size_t, uint32_t *);
bool application_q3_mod_entry_begin(application_q3_mod *, const qa_qvm_call *,
    application_q3_mod_entry **, qa_error *);
bool application_q3_mod_entry_end(application_q3_mod_entry **,
    bool succeeded, int32_t result, qa_error *);
/* Application identity is the actual open scope, even for recursive same-actor
 * commands. Closing resumes outer capture with a new baseline, not its deltas. */
bool application_q3_mod_open(application_q3_mod *, qa_actor_id,
    const application_q3_mod_inputs *, application_q3_mod_application **, qa_error *);
bool application_q3_mod_close(application_q3_mod_application **, qa_error *);
bool application_q3_mod_client_live(const application_q3_mod *, qa_actor_id);
bool application_q3_mod_application_current(application_q3_mod *,
    const application_q3_mod_application *,qa_actor_id);
bool application_q3_mod_input_update(application_q3_mod_application *,
    const application_q3_mod_inputs *, qa_error *);
typedef bool (*application_q3_mod_input_values_fn)(void *,application_q3_mod_inputs *,qa_error *);
/* Borrow the real working command only through this returned application's
 * lifetime. The innermost matching actor supplies GetUsercmd, not its packet. */
bool application_q3_mod_input_source(application_q3_mod_application *,
    application_q3_mod_input_values_fn,void *,qa_error *);
bool application_q3_mod_input_current(application_q3_mod *,qa_actor_id,
    application_q3_mod_inputs *,bool *found,qa_error *);
size_t application_q3_mod_input_binding_count(const application_q3_mod *);
bool application_q3_mod_input_binding(const application_q3_mod *, size_t,
    bool *movement_slice, bool *before);
/* Runs once immediately before each declared call in the actual application. */
typedef bool (*application_q3_mod_input_prepare_fn)(void *,uint32_t entry,qa_error *);
bool application_q3_mod_input_run(application_q3_mod *, size_t binding,
    application_q3_mod_application *,application_q3_mod_input_prepare_fn,void *,
    application_q3_mod_output **owned_outputs,
    size_t *output_count, qa_error *);
/* Reservation precedes real source client admission; bind follows it. */
bool application_q3_mod_reserve(application_q3_mod *, qa_actor_id, qa_error *);
bool application_q3_mod_admit(application_q3_mod *, qa_actor_id, qa_error *);
bool application_q3_mod_release_actor(application_q3_mod *, qa_actor_id, qa_error *);
bool application_q3_mod_activate(application_q3_mod *, qa_error *);
bool application_q3_mod_callbacks_register(application_q3_mod *, qa_error *);
bool application_q3_mod_validate(application_q3_mod *, qa_error *);
/* Child state contains actor/client protection leases only; physical function
 * IDs belong to Factory's complete union. No transient capture enters bytes. */
bool application_q3_mod_checkpoint(application_q3_mod *, qa_buffer *, qa_error *);
bool application_q3_mod_restore(application_q3_mod *, qa_bytes, qa_error *);
bool application_q3_mod_protection_binding(application_q3_mod *,
    qa_protection_lease, qa_protection_binding *, qa_error *);
bool application_q3_mod_protection_saved_binding(application_q3_mod *,qa_actor_id,
    qa_protection_channel,const qa_protection_claim *,qa_protection_binding *,qa_error *);

#endif
