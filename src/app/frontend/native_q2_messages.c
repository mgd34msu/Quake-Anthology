#include "internal.h"
#include "native_q2_messages.h"
#include "particle_delivery.h"

typedef struct message_delivery {
    qa_frontend *frontend;
    qa_application_protocol_event message;
    qa_application_q2_protocol_delivery delivery;
} message_delivery;

static bool receive(void *opaque, const qa_q2_server_record *record, qa_error *error)
{
    message_delivery *context=opaque;
    /* Configstrings, sound, print, inventory and layout already have actual
     * native import owners. Their wire records do not publish those effects
     * a second time through this temporary-entity consumer. */
    if (record->event.kind!=QA_Q2_SVC_TEMP_ENTITY) return true;
    return frontend_particle_q2_temporary(context->frontend,&context->message,
        &context->delivery.audience,&record->event.data.temporary,error);
}

bool frontend_native_q2_messages(qa_frontend *frontend, qa_error *error)
{
    size_t count=qa_application_protocol_event_count(frontend->application);
    uint64_t generation=qa_application_protocol_events_generation(frontend->application);
    for (size_t i=0;i<count;++i) {
        message_delivery context={.frontend=frontend};
        if (!qa_application_protocol_event_at(frontend->application,i,&context.message) ||
            !qa_application_protocol_q2_delivery_at(frontend->application,i,&context.delivery))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Original Q2 message queue changed during delivery");
        if (!context.delivery.original || !context.delivery.audience.captured ||
            !context.delivery.audience.count) continue;
        qa_net_protocol_id protocol={.kind=context.delivery.profile==QA_NATIVE_Q2_GAME_API3 ?
            QA_NET_Q2_34:QA_NET_Q2KEX_2023};
        /* These are the actual API3/API2023 engine configstring extents,
         * shared with the original source namespace in guest_native_q2.c. */
        qa_q2_message_options options={.config_strings=
            context.delivery.profile==QA_NATIVE_Q2_GAME_API3?2080:12448,.inventory_slots=256};
        qa_q2_messages *reader=NULL;
        bool ok=qa_q2_messages_create(protocol,&options,&reader,error) &&
            qa_q2_messages_read(reader,context.message.payload,NULL,NULL,error);
        if (ok) {
            /* Validate the complete retained wire boundary before publishing
             * any particles or audio. This decoder owns no GAME execution. */
            qa_q2_messages_reset(reader);
            ok=qa_q2_messages_read(reader,context.message.payload,receive,&context,error);
        }
        qa_q2_messages_destroy(reader);
        if (!ok) return false;
        if (generation!=qa_application_protocol_events_generation(frontend->application) ||
            count!=qa_application_protocol_event_count(frontend->application))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Original Q2 delivery retired its retained queue");
    }
    return true;
}
