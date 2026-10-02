#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_RECORDS_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_RECORDS_PRIVATE_H
#include "guest_q3_component_records.h"
#include "qa/binary.h"
#include "qa/json.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum component_field_kind {
    COMPONENT_HEALTH,COMPONENT_INVENTORY,COMPONENT_TEAM,COMPONENT_SCORE,
    COMPONENT_ORIGIN,COMPONENT_VELOCITY,COMPONENT_ANGLES,COMPONENT_MIN,COMPONENT_MAX,
    COMPONENT_RECORD,COMPONENT_CONSTANT,COMPONENT_VECTOR,COMPONENT_PRIVATE
} component_field_kind;
typedef struct component_team { double value; qa_string_id team; } component_team;
typedef struct component_field {
    component_field_kind kind;
    uint32_t offset,length;
    bool floating,writable,body_output;
    qa_item_id item;
    size_t target;
    uint8_t initial[12];
    component_team *teams;
    size_t team_count;
} component_field;
typedef struct component_record {
    const char *id;
    uint32_t address,stride,capacity;
    bool client;
    component_field *fields;
    size_t field_count;
    qa_buffer defaults;
} component_record;
typedef struct component_actor {
    qa_actor_id actor;
    uint32_t slot;
    bool owned,client,admitted,retired,projected,disconnected;
} component_actor;
typedef struct component_observation {
    qa_actor_id actor;
    component_field *field;
    uint32_t address;
    uint8_t bytes[12];
} component_observation;
typedef struct component_frame {
    struct component_frame *outer;
    application_q3_component_records *owner;
    component_observation *observations,*pending;
    size_t observation_count,pending_count,cursor;
    uint32_t entry;
    int32_t words[62];
    size_t word_count;
    qa_qvm_binding watch;
    bool completed;
} component_frame;
struct application_q3_component_records {
    application_q3_component_records_options options;
    component_record *records;
    size_t record_count;
    component_actor *actors;
    size_t actor_count,actor_capacity;
    component_frame *frame;
    uint32_t client_maximum,inuse;
    size_t entity_record,player_record;
    bool has_source,defaults_ready,refreshing;
};
bool q3records_fail(qa_error *,qa_status,const char *);
bool q3records_raw(application_q3_component_records *,uint32_t,qa_bytes,qa_error *);
bool q3records_live(const application_q3_component_records *,qa_actor_id);
component_actor *q3records_actor(application_q3_component_records *,qa_actor_id);
bool q3records_scalar(double,bool,uint8_t[4],qa_error *);
bool q3records_finish_retired(application_q3_component_records *,qa_error *);
bool q3records_reserve_actor(application_q3_component_records *,qa_error *);
#endif
