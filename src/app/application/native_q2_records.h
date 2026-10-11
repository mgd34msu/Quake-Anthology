#ifndef QA_APPLICATION_NATIVE_Q2_RECORDS_H
#define QA_APPLICATION_NATIVE_Q2_RECORDS_H
#include "native_q2_callbacks.h"
#include "qa/inventory.h"
#include "qa/world.h"

typedef struct application_native_q2_records application_native_q2_records;
typedef struct application_native_q2_record_scope application_native_q2_record_scope;
typedef struct application_native_q2_pickup_scope application_native_q2_pickup_scope;
typedef bool (*application_native_q2_protection_item_fn)(void *,qa_actor_id,
    qa_protection_channel,qa_item_id,bool *found,qa_error *);
typedef struct application_native_q2_inventory_commit {
    qa_actor_id actor;
    const qa_item_id *items;
    size_t count;
    void *context;
    bool (*current)(void *,qa_error *);
    qa_inventory_committed_fn committed;
} application_native_q2_inventory_commit;
typedef struct application_native_q2_records_options {
    application_native_q2_callbacks *callbacks;
    qa_native_instance *instance;
    qa_session *session;
    qa_world *world;
    qa_combat *combat;
    qa_inventory *inventory;
    qa_strings *strings;
    void *context;
    bool (*current)(void *,qa_error *);
    /* found distinguishes an original owned row from a foreign projection.
     * The callback proves the actual full actor and its original source slot. */
    bool (*owned_record)(void *,qa_actor_id,size_t,qa_native_address *,bool *found,qa_error *);
    bool (*client_slot)(void *,qa_actor_id,uint32_t *,bool *found,qa_error *);
    bool (*client_admitted)(void *,qa_actor_id);
    bool (*client_rejected)(void *,qa_actor_id);
    bool (*record_source)(void *,size_t,uint32_t index,qa_native_address *,qa_error *);
    /* Array capacity exists independently of its allocated actor count. Client
     * records return zero extent; their real private rows are read separately. */
    bool (*record_validate)(void *,size_t,qa_native_address *,uint64_t *bytes,qa_error *);
    /* Pure binding preflight followed by the lower no-callback slot publish;
     * failure leaves the slot unchanged. */
    bool (*bound)(void *,qa_actor_id,uint32_t source_slot,qa_error *);
    bool (*binding_current)(void *,qa_actor_id,uint32_t source_slot);
    bool (*released)(void *,qa_actor_id,uint32_t source_slot,qa_error *);
    bool (*match_read)(void *,qa_actor_id,qa_string_id *,double *,bool *found,qa_error *);
    bool (*match_write)(void *,qa_actor_id,bool team,qa_string_id,double,qa_error *);
    bool (*time)(void *,double *,qa_error *);
    /* Runs PROJECT/RELEASE without creating another projection transfer. */
    bool (*lifecycle)(void *,qa_json_id,const application_native_callback_inputs *,double *,bool *entered,qa_error *);
    bool (*pose)(void *,qa_actor_id,double *view_height,bool *crouched,qa_error *);
    bool (*pose_publish)(void *,uint32_t source_slot,double view_height,qa_error *);
} application_native_q2_records_options;

bool application_native_q2_records_create(const application_native_q2_records_options *,application_native_q2_records **,qa_error *);
bool application_native_q2_records_destroy(application_native_q2_records **,qa_error *);
bool application_native_q2_records_idle(const application_native_q2_records *);
/* True only while this owner writes an actual canonical projection. */
bool application_native_q2_records_writing(const application_native_q2_records *);
bool application_native_q2_records_lifecycle(const application_native_q2_records *);
bool application_native_q2_records_restoring(const application_native_q2_records *);
bool application_native_q2_records_pointer(application_native_q2_records *,qa_actor_id,size_t,qa_native_address *,qa_error *);
bool application_native_q2_records_refresh(application_native_q2_records *,qa_error *);
bool application_native_q2_records_validate(application_native_q2_records *,qa_error *);
bool application_native_q2_records_begin(application_native_q2_records *,application_native_q2_record_scope **,qa_error *);
bool application_native_q2_records_commit(application_native_q2_records *,qa_error *);
bool application_native_q2_records_commit_inventory(application_native_q2_records *,
    const application_native_q2_inventory_commit *,qa_error *);
/* Cleanup revokes the execution borrow before retiring write subscriptions.
 * Failed construction/removal retains *scope and its disabled subscriptions. */
bool application_native_q2_records_pickup_begin(application_native_q2_records *,qa_actor_id,
    qa_pickup_execution *,application_native_q2_protection_item_fn,void *,
    application_native_q2_pickup_scope **,qa_error *);
bool application_native_q2_records_pickup_end(application_native_q2_records *,
    application_native_q2_pickup_scope **,qa_error *);
/* Failed checked cleanup retains the exact innermost scope in *scope. */
bool application_native_q2_records_end(application_native_q2_records *,application_native_q2_record_scope **,bool succeeded,qa_error *);
bool application_native_q2_records_release(application_native_q2_records *,qa_actor_id,qa_error *);
bool application_native_q2_records_checkpoint(application_native_q2_records *,qa_buffer *,qa_error *);
/* Import only the full-actor mapping into an empty owner. Source RAM is restored
 * by the enclosing original module. finish_restore rebinds addresses/links and
 * refreshes canonical fields without PROJECT or Initialize replay. */
bool application_native_q2_records_restore(application_native_q2_records *,qa_bytes,qa_error *);
bool application_native_q2_records_finish_restore(application_native_q2_records *,qa_error *);
#endif
