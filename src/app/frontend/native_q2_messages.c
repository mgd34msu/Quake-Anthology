#include "internal.h"
#include "native_q2_messages.h"
#include "particle_delivery.h"
#include "../application/unified_events.h"

bool frontend_native_q2_messages(qa_frontend *frontend, qa_error *error)
{
    uint64_t first = qa_application_events_local_first(frontend->application);
    uint64_t next = qa_application_events_next(frontend->application);
    qa_application_event_range records = {.next = first, .end = next};
    qa_application_event_view output;
    while (qa_application_event_raw_next(frontend->application, &records,
        UINT64_MAX, &output)) {
        if (!output.protocol) continue;
        const qa_application_protocol_event *record = output.protocol;
        const qa_application_q2_protocol_delivery *delivery = output.q2_delivery;
        if (!delivery->original) continue;
        frontend_native_q2_print_output(frontend, record);
        if (!delivery->audience.captured || !delivery->audience.count) continue;
        const application_unified_event_record *projected;
        qa_application_event_range projections = {.next = records.id, .end = records.id + 1};
        while ((projected = qa_application_event_range_next(frontend->application, &projections, NULL))) {
            const qa_unified_presentation_payload *presentation = projected->presentation;
            if (!presentation) continue;
            bool temporary = presentation->kind == QA_UNIFIED_PRESENTATION_Q2_TEMPORARY;
            bool muzzle = presentation->kind == QA_UNIFIED_PRESENTATION_Q2_PROTOCOL &&
                presentation->value.q2_protocol.kind == QA_Q2_SVC_MUZZLEFLASH;
            bool print = presentation->kind == QA_UNIFIED_PRESENTATION_Q2_PROTOCOL &&
                (presentation->value.q2_protocol.kind == QA_Q2_SVC_PRINT ||
                 presentation->value.q2_protocol.kind == QA_Q2_SVC_CENTERPRINT ||
                 presentation->value.q2_protocol.kind == QA_Q2_SVC_LOCALIZED_PRINT);
            bool sound = presentation->kind == QA_UNIFIED_PRESENTATION_Q2_PROTOCOL &&
                presentation->value.q2_protocol.kind == QA_Q2_SVC_SOUND;
            if (!temporary && !muzzle && !print && !sound) continue;
            const qa_application_q2_recipient *recipient = NULL;
            for (size_t i = 0; i < delivery->audience.count; ++i)
                if (qa_actor_id_equal(delivery->audience.recipients[i].actor, projected->recipient)) {
                    recipient = delivery->audience.recipients + i; break;
                }
            if (!recipient) continue;
            if (sound) {
                if (!frontend_native_q2_sound_event(frontend, record,
                    &presentation->value.q2_protocol, projected->recipient, error)) return false;
                continue;
            }
            if (print) {
                if (!frontend_native_q2_print_event(frontend, record->provider,
                    &presentation->value.q2_protocol, projected->recipient, error)) return false;
                continue;
            }
            qa_application_q2_audience audience = delivery->audience;
            audience.recipients = recipient; audience.count = 1;
            if (muzzle) {
                const qa_unified_q2_muzzle *source = &presentation->value.q2_protocol.muzzle;
                qa_vec3 origin = record->origin;
                if (!record->multicast) {
                    qa_body_state body;
                    if (!qa_world_body_read(qa_application_world(frontend->application), source->actor, &body, error)) return false;
                    origin = body.origin;
                }
                qa_builtin_event event = {.kind = QA_BUILTIN_MUZZLE, .family = QA_GAME_Q2,
                    .provider = record->provider, .actor = source->actor,
                    .time_ns = projected->time_ns, .origin = origin, .code = source->flash,
                    .flags = source->silenced ? 128u : 0u};
                if (!frontend_native_q2_muzzle_sound(frontend, record, &audience, &event,
                    source->monster, qa_ruleset_q2_from_tag(delivery->profile != QA_NATIVE_Q2_GAME_API3), error)) return false;
            } else {
                const qa_unified_q2_temporary *source = &presentation->value.q2_temporary;
                qa_q2_temp_entity effect = {.type = source->type, .field_count = source->field_count};
                qa_actor_id actors[7] = {0};
                for (size_t i = 0; i < source->field_count; ++i) {
                    const qa_unified_q2_temp_field *from = source->fields + i;
                    qa_q2_temp_field *to = effect.fields + i;
                    to->name = from->name; to->kind = from->kind; actors[i] = from->actor;
                    if (from->kind == QA_Q2_TEMP_INTEGER) to->value.integer = from->integer;
                    else {
                        to->value.vector[0] = from->vector.x;
                        to->value.vector[1] = from->vector.y;
                        to->value.vector[2] = from->vector.z;
                    }
                }
                if (!frontend_particle_q2_temporary(frontend, record, &audience, &effect, actors, error)) return false;
            }
        }
    }
    return true;
}
