#ifndef QA_UNIFIED_FRAME_INTERNAL_H
#define QA_UNIFIED_FRAME_INTERNAL_H

#include "qa/network_unified_frame.h"
#include "value_internal.h"
#include "qa/network_unified_control.h"

typedef enum qa_unified_field_kind {
    QA_UNIFIED_FIELD_BOOL,
    QA_UNIFIED_FIELD_U8, QA_UNIFIED_FIELD_I8,
    QA_UNIFIED_FIELD_U16, QA_UNIFIED_FIELD_I16,
    QA_UNIFIED_FIELD_U32, QA_UNIFIED_FIELD_I32,
    QA_UNIFIED_FIELD_U64, QA_UNIFIED_FIELD_I64, QA_UNIFIED_FIELD_SIZE,
    QA_UNIFIED_FIELD_F32, QA_UNIFIED_FIELD_F64,
    QA_UNIFIED_FIELD_STRING, QA_UNIFIED_FIELD_NAME, QA_UNIFIED_FIELD_BYTES,
    QA_UNIFIED_FIELD_RECORD, QA_UNIFIED_FIELD_ARRAY,
    QA_UNIFIED_FIELD_POINTER, QA_UNIFIED_FIELD_FIXED, QA_UNIFIED_FIELD_RAW,
    QA_UNIFIED_FIELD_VARIANT, QA_UNIFIED_FIELD_VARIANT_BOOL, QA_UNIFIED_FIELD_INLINE_ARRAY
} qa_unified_field_kind;
typedef struct qa_unified_record_layout qa_unified_record_layout;
typedef enum qa_unified_key_kind {
    QA_UNIFIED_KEY_NONE, QA_UNIFIED_KEY_ACTOR, QA_UNIFIED_KEY_U32, QA_UNIFIED_KEY_I32
} qa_unified_key_kind;
typedef struct qa_unified_field {
    qa_unified_field_kind kind;
    size_t offset;
    const qa_unified_record_layout *record;
    size_t count_offset;
    size_t maximum;
    const qa_unified_record_layout *const *variants;
} qa_unified_field;
struct qa_unified_record_layout {
    size_t size;
    const qa_unified_field *fields;
    size_t field_count;
    size_t key_offset;
    qa_unified_key_kind key_kind;
};

#define QA_UNIFIED_FIELD(type, member, kind) \
    {kind, offsetof(type, member), NULL, 0, 0, NULL}
#define QA_UNIFIED_RECORD(type, member, layout) \
    {QA_UNIFIED_FIELD_RECORD, offsetof(type, member), &(layout), 0, 0, NULL}
#define QA_UNIFIED_ARRAY(type, member, count, layout, limit) \
    {QA_UNIFIED_FIELD_ARRAY, offsetof(type, member), &(layout), offsetof(type, count), limit, NULL}
#define QA_UNIFIED_POINTER(type, member, layout) \
    {QA_UNIFIED_FIELD_POINTER, offsetof(type, member), &(layout), 0, 0, NULL}
#define QA_UNIFIED_FIXED(type, member, layout, count) \
    {QA_UNIFIED_FIELD_FIXED, offsetof(type, member), &(layout), 0, count, NULL}
#define QA_UNIFIED_RAW(type, member) \
    {QA_UNIFIED_FIELD_RAW, offsetof(type, member), NULL, 0, sizeof(((type *)0)->member), NULL}
#define QA_UNIFIED_VARIANT(type, member, discriminator, layouts) \
    {QA_UNIFIED_FIELD_VARIANT, offsetof(type, member), NULL, offsetof(type, discriminator), sizeof(layouts) / sizeof((layouts)[0]), layouts}
#define QA_UNIFIED_VARIANT_BOOL(type, member, discriminator, layouts) \
    {QA_UNIFIED_FIELD_VARIANT_BOOL, offsetof(type, member), NULL, offsetof(type, discriminator), sizeof(layouts) / sizeof((layouts)[0]), layouts}
#define QA_UNIFIED_INLINE_ARRAY(type, member, count, layout, limit) \
    {QA_UNIFIED_FIELD_INLINE_ARRAY, offsetof(type, member), &(layout), offsetof(type, count), limit, NULL}
#define QA_UNIFIED_LAYOUT(type, fields) \
    {sizeof(type), fields, sizeof(fields) / sizeof((fields)[0]), SIZE_MAX, QA_UNIFIED_KEY_NONE}
#define QA_UNIFIED_ACTOR_LAYOUT(type, fields, actor) \
    {sizeof(type), fields, sizeof(fields) / sizeof((fields)[0]), offsetof(type, actor), QA_UNIFIED_KEY_ACTOR}
#define QA_UNIFIED_NUMBER_LAYOUT(type, fields, member, kind) \
    {sizeof(type), fields, sizeof(fields) / sizeof((fields)[0]), offsetof(type, member), kind}

/* One fixed C layout table drives typed field comparison, delta I/O, and
 * checked ownership cleanup. It contains no runtime value nodes or names. */
extern const qa_unified_record_layout qa_unified_actor_layout;
extern const qa_unified_record_layout qa_unified_vector_layout;
extern const qa_unified_record_layout qa_unified_bounds_layout;
extern const qa_unified_record_layout qa_unified_body_layout;
extern const qa_unified_record_layout qa_unified_provider_layout;
extern const qa_unified_record_layout qa_unified_inventory_entry_layout;
extern const qa_unified_record_layout qa_unified_prediction_layout;
extern const qa_unified_record_layout qa_unified_player_frame_layout;
extern const qa_unified_record_layout qa_unified_visual_frame_layout;
extern const qa_unified_record_layout qa_unified_q3_frame_layout;
extern const qa_unified_record_layout qa_unified_component_frame_layout;
extern const qa_unified_record_layout qa_unified_frame_layout;
extern const qa_unified_record_layout qa_unified_inputs_layout;
extern const qa_unified_record_layout qa_unified_handshake_layout;
extern const qa_unified_record_layout qa_unified_control_layout;
bool qa_unified_resource_serial(const char *, uint64_t *);
bool qa_unified_control_check(const qa_unified_control *, size_t *, qa_error *);
extern const qa_unified_record_layout qa_unified_events_layout;
extern const qa_unified_record_layout qa_unified_metadata_layout;
extern const qa_unified_record_layout qa_unified_q3_configuration_layout;
extern const qa_unified_record_layout qa_unified_presentation_payload_layout;
extern const qa_unified_record_layout qa_unified_simulation_payload_layout;
extern const qa_unified_record_layout qa_unified_presentation_event_layout;
extern const qa_unified_record_layout qa_unified_simulation_event_layout;
typedef bool (*qa_unified_actor_remap_fn)(void *, qa_actor_id, qa_actor_id *, qa_error *);
bool qa_unified_record_actor_remap(const qa_unified_record_layout *, void *,
    qa_unified_actor_remap_fn, void *, qa_error *);

void qa_unified_record_dispose(const qa_unified_record_layout *, void *);
bool qa_unified_record_equal(const qa_unified_record_layout *, const void *, const void *,
    const qa_strings *, const qa_strings *);
bool qa_unified_record_clone(const qa_unified_record_layout *, const void *, void *, qa_error *);
typedef void *(*qa_unified_clone_alloc_fn)(void *, size_t, size_t, qa_error *);
/* Caller supplies zeroed output. The callback returns aligned storage, which this
 * traversal zeroes. NULL selects heap storage. Failure leaves a partial output;
 * custom owners rewind their allocation and clear it, never call dispose.
 * Strings/bytes within retained_payload are borrowed from that same owner. */
bool qa_unified_record_clone_alloc(const qa_unified_record_layout *, const void *,
    void *zeroed_output, qa_unified_clone_alloc_fn, void *, qa_bytes retained_payload, qa_error *);
bool qa_unified_record_delta_encode(const qa_unified_record_layout *, const void *,
    const void *baseline, size_t maximum, qa_buffer *, const qa_strings *, qa_error *);
bool qa_unified_record_delta_write(const qa_unified_record_layout *, const void *,
    const void *baseline, qa_unified_builder *, qa_error *);
bool qa_unified_record_delta_decode(const qa_unified_record_layout *, qa_bytes,
    const void *baseline, void *zeroed_output, qa_unified_frame_lease *, qa_unified_clone_alloc_fn, void *, qa_strings *, const qa_strings *, qa_error *);
bool qa_unified_record_measure(const qa_unified_record_layout *, const void *, size_t *, qa_error *);
bool qa_unified_world_frame_clock_check(const qa_unified_world_frame *,qa_error *);
bool qa_unified_frame_check(const qa_unified_frame *, size_t *, qa_error *);
struct qa_unified_frame_events;
bool qa_unified_events_check(const struct qa_unified_frame_events *, size_t *, qa_error *);
struct qa_unified_frame_metadata;
bool qa_unified_metadata_check(const struct qa_unified_frame_metadata *, size_t *, qa_error *);
bool qa_unified_document_create_handshake(const qa_unified_handshake *, qa_unified_document **, qa_error *);
const qa_unified_handshake *qa_unified_document_handshake(const qa_unified_document *);

#endif
