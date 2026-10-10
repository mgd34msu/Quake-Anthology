#include "internal.h"
#include "native_q2_messages.h"
#include "particle_delivery.h"
#include "../application/event_stream.h"

bool frontend_native_q2_messages(qa_frontend *frontend, qa_error *error)
{
    uint64_t first = qa_application_events_local_first(frontend->application);
    uint64_t next = qa_application_events_next(frontend->application);
    for (uint64_t id = first; id < next; ++id) {
        const application_event_envelope *envelope = application_event_stream_at(frontend->application, id);
        if (!envelope || envelope->kind != QA_APPLICATION_EVENT_PROTOCOL) continue;
        const application_protocol_record *record = &envelope->raw.protocol;
        const qa_application_q2_protocol_delivery *delivery = &record->q2;
        if (!delivery->original || !delivery->audience.captured || !delivery->audience.count) continue;
        for (const application_event_view *view = envelope->views; view; view = view->next) {
            const qa_unified_presentation_payload *presentation = view->event.presentation;
            if (!presentation) continue;
            bool temporary = presentation->kind == QA_UNIFIED_PRESENTATION_Q2_TEMPORARY;
            bool muzzle = presentation->kind == QA_UNIFIED_PRESENTATION_Q2_PROTOCOL &&
                presentation->value.q2_protocol.kind == QA_Q2_SVC_MUZZLEFLASH;
            bool print = presentation->kind == QA_UNIFIED_PRESENTATION_Q2_PROTOCOL &&
                (presentation->value.q2_protocol.kind == QA_Q2_SVC_PRINT ||
                 presentation->value.q2_protocol.kind == QA_Q2_SVC_CENTERPRINT);
            if (!temporary && !muzzle && !print) continue;
            const qa_application_q2_recipient *recipient = NULL;
            for (size_t i = 0; i < delivery->audience.count; ++i)
                if (qa_actor_id_equal(delivery->audience.recipients[i].actor, view->event.recipient)) {
                    recipient = delivery->audience.recipients + i; break;
                }
            if (!recipient) continue;
            if (print) {
                frontend_native_q2_print_event(frontend, record->event.provider,
                    &presentation->value.q2_protocol, view->event.recipient);
                continue;
            }
            qa_application_q2_audience audience = delivery->audience;
            audience.recipients = recipient; audience.count = 1;
            if (muzzle) {
                const qa_unified_q2_muzzle *source = &presentation->value.q2_protocol.muzzle;
                qa_vec3 origin = record->event.origin;
                if (!record->event.multicast) {
                    qa_body_state body;
                    if (!qa_world_body_read(qa_application_world(frontend->application), source->actor, &body, error)) return false;
                    origin = body.origin;
                }
                qa_builtin_event event = {.kind = QA_BUILTIN_MUZZLE, .family = QA_GAME_Q2,
                    .provider = record->event.provider, .actor = source->actor,
                    .time_ns = view->event.time_ns, .origin = origin, .code = source->flash,
                    .flags = source->silenced ? 128u : 0u};
                if (!frontend_native_q2_muzzle_sound(frontend, &record->event, &audience, &event,
                    source->monster, delivery->profile == QA_NATIVE_Q2_GAME_API3 ?
                        QA_Q2_CLASSIC : QA_Q2_RERELEASE, error)) return false;
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
                if (!frontend_particle_q2_temporary(frontend, &record->event, &audience, &effect, actors, error)) return false;
            }
        }
    }
    return true;
}
