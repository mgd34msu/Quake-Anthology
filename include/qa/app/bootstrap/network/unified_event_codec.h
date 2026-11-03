/* Port of src/app/bootstrap/network/unified-event-codec.ts. Events ride as
 * qa_save_value trees in their TS typed-object shape; each generated TS reader and
 * writer is a function of the same name in the .c file, with the same checks in the
 * same order. Invalid events raise the TS error. Trees hold identities as {slot,
 * generation} and {index}; a decoded node also keeps the decoder's handle, session
 * token included, which qa_app_unified_event_actor_id and its siblings return.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_EVENT_CODEC_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_EVENT_CODEC_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "qa/app/bootstrap/network/unified_types.h"
#include "qa/persistence/value.h"
/* The identity a reader decoded at this {slot, generation} or {index} node, as the TS
 * object it would hold, for owner.owns() and same_actor(). False for a node no reader built. */
bool qa_app_unified_event_actor_id(const qa_save_value *node, qa_actor_id *out);
bool qa_app_unified_event_client_id(const qa_save_value *node, qa_client_id *out);
bool qa_app_unified_event_seat_id(const qa_save_value *node, qa_seat_id *out);
/* Owned new trees. */
qa_save_value *qa_app_unified_write_simulation_event(const qa_save_value *event);
qa_save_value *qa_app_unified_read_simulation_event(const qa_save_value *value,
    const qa_app_unified_identity_decoder *identity);
qa_save_value *qa_app_unified_write_presentation_events(
    qa_save_value *const *events, size_t count);
uint8_t *qa_app_unified_encode_presentation_events(
    qa_save_value *const *events, size_t count, size_t *out_length);
/* TS encodeUnifiedPresentationEvents(events, dropped): events whose written form holds a
 * non-finite number are left out and their count passed to `dropped` (NULL = undefined). */
uint8_t *qa_app_unified_encode_presentation_events_reporting(
    qa_save_value *const *events, size_t count,
    void (*dropped)(void *ctx, size_t count), void *dropped_ctx, size_t *out_length);
/* Owned array of owned event trees; free each + the array. */
qa_save_value **qa_app_unified_decode_presentation_events(
    const uint8_t *bytes, size_t length,
    const qa_app_unified_identity_decoder *identity, size_t *out_count);
qa_save_value *qa_app_unified_read_presentation_event(const qa_save_value *value,
    const qa_app_unified_identity_decoder *identity);
#endif
