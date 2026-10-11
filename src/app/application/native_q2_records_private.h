#ifndef QA_APPLICATION_NATIVE_Q2_RECORDS_PRIVATE_H
#define QA_APPLICATION_NATIVE_Q2_RECORDS_PRIVATE_H
#include "native_q2_records.h"
#include "qa/binary.h"
#include "qa/json.h"
#include "qa/source_save.h"
#include "qa/native_observe.h"
#include "qa/pool.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum nqr_kind { NQR_HEALTH,NQR_COUNT,NQR_CAPACITY,NQR_TEAM,NQR_SCORE,
    NQR_BODY,NQR_LINK,NQR_ADDRESS,
    NQR_CONSTANT,NQR_VECTOR,NQR_PRIVATE } nqr_kind;
typedef struct nqr_field {
    nqr_kind kind;
    qa_native_value_type encoding;
    uint32_t offset,length;
    union { qa_item_id item; qa_body_vector_kind body; };
    size_t target;
    qa_json_id address;
    uint8_t initial[12];
    qa_team_source_scalar *teams;
    size_t team_count;
    bool writable,body_output,separate;
} nqr_field;
typedef enum nqr_base_kind { NQR_BASE_CLIENTS,NQR_BASE_ENTITIES,NQR_BASE_ADDRESS } nqr_base_kind;
typedef struct nqr_record {
    char *id;
    uint32_t stride,first,capacity;
    bool client,entities,public_record;
    nqr_base_kind base_kind;
    qa_json_id base;
    nqr_field *fields;
    size_t field_count;
} nqr_record;
typedef struct nqr_pose_field { size_t record; uint32_t offset; qa_native_value_type encoding; } nqr_pose_field;
typedef struct nqr_actor {
    struct nqr_actor *next;
    qa_actor_id actor;
    uint32_t index;
    size_t release_cursor;
    bool client,retired,failed,bound;
} nqr_actor;
typedef struct nqr_observation {
    qa_actor_id actor;
    nqr_field *field;
    qa_native_address address;
    uint8_t bytes[12];
    bool count_present,capacity_present;
    double count,capacity;
} nqr_observation;
typedef struct nqr_snapshot {
    size_t storage_slot, rows_slot, count, references;
    nqr_observation *rows;
} nqr_snapshot;
struct application_native_q2_record_scope {
    application_native_q2_records *owner;
    struct application_native_q2_record_scope *outer;
    size_t storage_slot;
    nqr_snapshot *snapshot;
    nqr_observation *pending;
    size_t pending_slot,pending_capacity,pending_count,cursor;
    unsigned committing;
};
struct application_native_q2_pickup_scope {
    application_native_q2_records *owner;
    struct application_native_q2_pickup_scope *outer;
    application_native_q2_record_scope *frame;
    qa_actor_id actor;
    qa_pickup_execution *execution;
    application_native_q2_protection_item_fn protection_item;
    void *context;
    qa_native_write_observer **watches;
    size_t storage_slot,watches_slot,count,capacity;
    bool closing;
};
struct application_native_q2_records {
    qa_arena call_storage;
    qa_pool scopes, snapshots, observations, pickup_scopes, pickup_watches;
    application_native_q2_records_options options;
    const qa_json_document *document;
    nqr_record *records;
    size_t record_count,entity_record;
    uint32_t client_maximum;
    nqr_actor *actors;
    application_native_q2_record_scope *frame;
    application_native_q2_pickup_scope *pickup;
    unsigned projection_depth,lifecycle_depth;
    bool restoring,closing;
    bool has_pose;
    nqr_pose_field view_height,crouch;
    uint32_t crouch_mask;
};
static inline bool nqr_fail(qa_error *e,qa_status code,const char *message)
{ qa_error_set(e,code,0,"%s",message); return false; }
bool nqr_current(application_native_q2_records *,qa_error *);
bool nqr_live(application_native_q2_records *,qa_actor_id);
nqr_actor *nqr_find(application_native_q2_records *,qa_actor_id);
bool nqr_profile(application_native_q2_records *,qa_error *);
bool nqr_private_range(const nqr_record *,uint32_t,size_t);
bool nqr_scalar_encode(double,qa_native_value_type,uint8_t *,qa_error *);
bool nqr_scalar_decode(const uint8_t *,qa_native_value_type,double *,qa_error *);
size_t nqr_scalar_size(qa_native_value_type);
bool nqr_address(application_native_q2_records *,const nqr_actor *,const nqr_record *,qa_native_address *,qa_error *);
bool nqr_seed(application_native_q2_records *,nqr_actor *,bool constants,qa_error *);
bool nqr_releases(application_native_q2_records *,qa_error *);
bool nqr_call_storage_prepare(application_native_q2_records *,qa_error *);
bool nqr_rebase(application_native_q2_records *,qa_error *);
bool nqr_capacity(application_native_q2_records *,const nqr_actor *,qa_error *);
bool nqr_actor_current(application_native_q2_records *,const nqr_actor *,qa_error *);
bool nqr_applies(application_native_q2_records *,const nqr_actor *,const nqr_record *,const nqr_field *);
#endif
