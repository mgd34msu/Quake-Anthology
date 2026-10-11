#ifndef QA_APPLICATION_NATIVE_Q2_ARMOR_H
#define QA_APPLICATION_NATIVE_Q2_ARMOR_H
#include "native_q2_callbacks.h"
#include "qa/gameplay.h"
#include "qa/strings.h"

typedef struct application_native_q2_field {
    size_t record;
    uint32_t offset;
    qa_native_value_type encoding;
} application_native_q2_field;
typedef struct application_native_q2_armor application_native_q2_armor;
typedef struct application_native_q2_armor_watch application_native_q2_armor_watch;
typedef struct application_native_q2_armor_options {
    application_native_q2_callbacks *callbacks;
    qa_strings *strings;
    qa_actor_owner owner;
} application_native_q2_armor_options;
typedef bool (*application_native_q2_armor_changed_fn)(void *,const qa_armor *,const qa_armor *,qa_error *);

size_t application_native_q2_field_size(qa_native_value_type);
bool application_native_q2_field_parse(application_native_q2_callbacks *,qa_json_id,
    application_native_q2_field *,qa_error *);
void application_native_q2_field_dispose(application_native_q2_field *);
bool application_native_q2_field_address(application_native_q2_callbacks *,qa_actor_id,
    const application_native_q2_field *,qa_native_address *,qa_error *);
bool application_native_q2_field_read(application_native_q2_callbacks *,qa_actor_id,
    const application_native_q2_field *,double *,qa_error *);
bool application_native_q2_field_write(application_native_q2_callbacks *,qa_actor_id,
    const application_native_q2_field *,double,qa_error *);
bool application_native_q2_field_value(qa_native_value_type,double,qa_error *);

/* Arrays are borrowed from the acquired immutable callback document. Source
 * memory owns all mutable armor words; this owner only retains their layout. */
bool application_native_q2_armor_create(const application_native_q2_armor_options *,
    qa_json_id regular,qa_json_id power,bool q2,application_native_q2_armor **,qa_error *);
bool application_native_q2_armor_read(application_native_q2_armor *,qa_actor_id,qa_armor *,qa_error *);
bool application_native_q2_armor_normalize_legacy(application_native_q2_armor *,qa_actor_id,
    const qa_armor *,qa_armor *,qa_error *);
bool application_native_q2_armor_validate(application_native_q2_armor *,qa_actor_id,const qa_armor *,qa_error *);
bool application_native_q2_armor_write(application_native_q2_armor *,qa_actor_id,const qa_armor *,qa_error *);
bool application_native_q2_armor_observe(application_native_q2_armor *,qa_actor_id,
    application_native_q2_armor_changed_fn,void *,application_native_q2_armor_watch **,qa_error *);
bool application_native_q2_armor_observe_end(application_native_q2_armor_watch **,qa_error *);
bool application_native_q2_armor_observe_cancel(application_native_q2_armor_watch *,qa_error *);
bool application_native_q2_armor_destroy(application_native_q2_armor **,qa_error *);
#endif
