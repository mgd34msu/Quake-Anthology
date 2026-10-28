/* Port of src/app/bootstrap/network/unified-event-codec.ts.
 * The TS codec's generated per-kind readers rebuild typed event
 * objects; the landed C event structs do not carry most nested
 * payloads, so events ride as qa_save_value trees here. Write/read
 * validate and rebind the identity-sensitive fields the TS codec
 * enforces at tree level (negotiated unified resource ids, wire
 * actors/clients/seats through the identity decoder, content ids,
 * bounded lists); per-kind scalar shape validation of the
 * generated readers is NOT replicated (see slice unresolved).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_EVENT_CODEC_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_EVENT_CODEC_H
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/unified_types.h"
#include "qa/persistence/value.h"
/* Owned transformed tree (input is the event in typed/tree form;
 * resource ids validated, actors written as wire objects). */
qa_save_value *qa_app_unified_write_simulation_event(const qa_save_value *event);
qa_save_value *qa_app_unified_read_simulation_event(const qa_save_value *value,
    const qa_app_unified_identity_decoder *identity);
qa_save_value *qa_app_unified_write_presentation_events(
    qa_save_value *const *events, size_t count);
uint8_t *qa_app_unified_encode_presentation_events(
    qa_save_value *const *events, size_t count, size_t *out_length);
/* Owned array of owned event trees; free each + the array. */
qa_save_value **qa_app_unified_decode_presentation_events(
    const uint8_t *bytes, size_t length,
    const qa_app_unified_identity_decoder *identity, size_t *out_count);
qa_save_value *qa_app_unified_read_presentation_event(const qa_save_value *value,
    const qa_app_unified_identity_decoder *identity);
#endif
