#include "internal.h"
#include "native_q2_messages.h"
#include "particle_delivery.h"

typedef struct message_delivery {
    qa_frontend *frontend;
    qa_application_protocol_event message;
    qa_application_q2_protocol_delivery delivery;
    qa_application_q2_recipient *selected;
} message_delivery;

static bool muzzle(message_delivery *context, const qa_q2_server_record *record,
    const qa_application_q2_audience *audience, qa_error *error)
{
    const qa_application_protocol_event *message=&context->message;
    uintptr_t raw=(uintptr_t)record->raw.data, payload=(uintptr_t)message->payload.data;
    if (raw<payload || raw-payload>message->payload.size || record->raw.size<3 ||
        record->raw.size>message->payload.size-(size_t)(raw-payload))
        return frontend_fail(error,QA_ERROR_FORMAT,"Q2 muzzle record leaves its retained Source packet");
    size_t offset=(size_t)(raw-payload)+1;
    uint32_t number=qa_load_u16le(message->payload.data+offset);
    if (record->event.data.muzzle.monster && record->opcode==2 &&
        context->delivery.profile!=QA_NATIVE_Q2_GAME_API3) number&=0x1fffu;
    qa_actor_id actor={0};
    for (size_t i=0;i<message->reference_count;++i) {
        const qa_application_protocol_reference *reference=message->references+i;
        if (reference->offset==offset && !reference->packed_sound &&
            number==record->event.data.muzzle.entity) {
            actor=reference->actor; break;
        }
    }
    if (!actor.registry)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q2 muzzle lost its captured full Source actor reference");
    qa_vec3 origin=message->origin;
    if (!message->multicast) {
        qa_body_state body;
        if (!qa_world_body_read(qa_application_world(context->frontend->application),actor,&body,error)) return false;
        origin=body.origin;
    }
    qa_builtin_event event={.kind=QA_BUILTIN_MUZZLE,.family=QA_GAME_Q2,.provider=message->provider,
        .actor=actor,.time_ns=message->time_ns,.origin=origin,.code=(int32_t)record->event.data.muzzle.flash,
        .flags=record->event.data.muzzle.silenced?128u:0u};
    return frontend_native_q2_muzzle_sound(context->frontend,message,audience,&event,
        record->event.data.muzzle.monster,context->delivery.profile==QA_NATIVE_Q2_GAME_API3?
            QA_Q2_CLASSIC:QA_Q2_RERELEASE,error);
}

static bool receive(void *opaque, const qa_q2_server_record *record, qa_error *error)
{
    message_delivery *context=opaque;
    /* Configstrings, sound, print, inventory and layout already have actual
     * native import owners. Their wire records do not publish those effects
     * a second time through this effect consumer. */
    if (record->event.kind!=QA_Q2_SVC_TEMP_ENTITY && record->event.kind!=QA_Q2_SVC_MUZZLEFLASH) return true;
    qa_application_q2_audience audience=context->delivery.audience;
    if (record->seat) {
        if (!context->selected && audience.count) {
            if (audience.count>SIZE_MAX/sizeof(*context->selected))
                return frontend_fail(error,QA_ERROR_MEMORY,"Q2 selected audience extent overflows");
            context->selected=malloc(audience.count*sizeof(*context->selected));
            if (!context->selected)
                return frontend_fail(error,QA_ERROR_MEMORY,"Retaining Q2 selected effect audience");
        }
        size_t count=0;
        for (size_t i=0;i<audience.count;++i) {
            const qa_application_q2_recipient *recipient=audience.recipients+i;
            if (recipient->has_connection && record->seat==recipient->remote_index+1u)
                context->selected[count++]=*recipient;
        }
        audience.recipients=context->selected; audience.count=count;
        if (!count) return true;
    }
    if (record->event.kind==QA_Q2_SVC_MUZZLEFLASH) return muzzle(context,record,&audience,error);
    return frontend_particle_q2_temporary(context->frontend,&context->message,
        &audience,&record->event.data.temporary,error);
}

bool frontend_native_q2_messages(qa_frontend *frontend, qa_error *error)
{
    uint64_t first=qa_application_events_local_first(frontend->application);
    uint64_t next=qa_application_events_next(frontend->application);
    uint64_t generation=qa_application_protocol_events_generation(frontend->application);
    for (uint64_t id=first;id<next;++id) {
        message_delivery context={.frontend=frontend};
        if (!qa_application_protocol_event_at(frontend->application,id,&context.message)) continue;
        if (!qa_application_protocol_q2_delivery_at(frontend->application,id,&context.delivery)) continue;
        if (!context.delivery.original || !context.delivery.audience.captured ||
            !context.delivery.audience.count) continue;
        qa_net_protocol_id protocol={.kind=context.delivery.profile==QA_NATIVE_Q2_GAME_API3 ?
            QA_NET_Q2_34:QA_NET_Q2KEX_2023};
        /* These are the actual API3/API2023 engine configstring extents,
         * shared with the original source namespace in guest_native_q2.c. */
        qa_q2_message_options options={.config_strings=
            context.delivery.profile==QA_NATIVE_Q2_GAME_API3?2080:12448,.inventory_slots=256,
            .native_api2023=context.delivery.profile==QA_NATIVE_Q2_GAME_API2023};
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
        free(context.selected);
        if (!ok) return false;
        if (generation!=qa_application_protocol_events_generation(frontend->application) ||
            first!=qa_application_events_local_first(frontend->application))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Original Q2 delivery retired its retained queue");
    }
    return true;
}
