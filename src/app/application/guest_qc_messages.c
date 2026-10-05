#include "guest_qc_internal.h"

static bool message_target(struct application_qc_state *engine, qa_qc_instance *vm,
                            uint32_t destination, application_qc_message **out, qa_error *error)
{
    qa_actor_id recipient = {0};
    if (destination > 4 || (destination == 4 && engine->profile != QA_QC_QUAKEWORLD) ||
        (destination == 3 && engine->profile == QA_QC_QUAKEWORLD && !engine->loading)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeC message destination");
        return false;
    }
    if (destination == 1) {
        const qa_qc_definition *global = qa_qc_program_find_global(engine->provider->state.qc.program, "msg_entity");
        int32_t reference;
        if (global == NULL || global->type != QA_QC_ENTITY || !qa_qc_global_int(vm, global->offset, &reference, error) ||
            !qa_qc_reference_actor(vm, reference, &recipient, error)) return false;
        bool connected = false;
        for (uint32_t i = 1; i <= engine->max_clients; ++i)
            if (engine->clients[i].connected && qa_actor_id_equal(engine->clients[i].actor, recipient)) { connected = true; break; }
        if (!connected) {
            application_fail(error, QA_ERROR_ARGUMENT, "QuakeC MSG_ONE does not name a client");
            return false;
        }
    }
    for (size_t i = 0; i < engine->message_count; ++i)
        if (engine->messages[i].destination == destination &&
            qa_actor_id_equal(engine->messages[i].recipient, recipient)) { *out = &engine->messages[i]; return true; }
    if (engine->message_count == engine->message_capacity) {
        size_t capacity = engine->message_capacity ? engine->message_capacity * 2 : 8;
        if (capacity < engine->message_capacity || capacity > SIZE_MAX / sizeof(*engine->messages)) {
            application_fail(error, QA_ERROR_MEMORY, "QuakeC message routing allocation overflow");
            return false;
        }
        application_qc_message *messages = realloc(engine->messages, capacity * sizeof(*messages));
        if (messages == NULL) {
            application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC message routes");
            return false;
        }
        engine->messages = messages; engine->message_capacity = capacity;
    }
    size_t maximum = engine->profile == QA_QC_QUAKEWORLD ?
        destination == 1 ? 1450u * 5u : destination == 0 || destination == 3 ? 1024u : 1450u :
        destination == 0 || destination == 2 ? 1024u : 8000u;
    uint8_t *data = malloc(maximum);
    if (data == NULL) {
        application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC message buffer");
        return false;
    }
    application_qc_message *message = &engine->messages[engine->message_count++];
    *message = (application_qc_message){.destination = destination, .recipient = recipient,
                                     .data = data, .capacity = maximum};
    *out = message; return true;
}
static bool integer_argument(qa_qc_instance *vm, uint32_t index, int32_t *out, qa_error *error)
{
    float value;
    if (!qa_qc_arg_float(vm, index, &value, error)) return false;
    if (!isfinite(value)) {
        (void)application_fail(error, QA_ERROR_FORMAT, "Nonfinite QuakeC integer message value");
        return false;
    }
    *out = qa_source_float_to_i32(value); return true;
}
static qa_actor_id window_actor(qa_qc_instance *vm, uint32_t word, bool packed)
{
    uint32_t slot = packed ? (word >> 3) & 1023u : word;
    qa_qc_slot_binding binding;
    return qa_qc_slot(vm, slot, &binding) && binding.kind != QA_QC_SLOT_FREE ?
        binding.actor : (qa_actor_id){0};
}
bool application_qc_write_message(struct application_qc_state *engine, qa_qc_instance *vm,
                                   qa_qc_builtin builtin, qa_error *error)
{
    int32_t destination;
    if (!integer_argument(vm, 0, &destination, error)) return false;
    application_qc_message *message;
    if (destination < 0 || !message_target(engine, vm, (uint32_t)destination, &message, error)) return false;
    const char *text = NULL; float value = 0; int32_t integer = 0; size_t bytes = 0;
    if (builtin == QA_QC_BUILTIN_WRITESTRING) {
        if (!qa_qc_arg_string(vm, 1, &text, error)) return false;
        bytes = strlen(text) + 1;
    } else if (builtin == QA_QC_BUILTIN_WRITEENTITY) {
        int32_t reference;
        if (!qa_qc_arg_int(vm, 1, &reference, error)) return false;
        qa_qc_entity_layout layout = qa_qc_default_entity_layout(engine->provider->state.qc.program, engine->profile);
        if (reference < 0 || (uint32_t)reference % layout.stride_bytes != 0 ||
            (uint32_t)reference / layout.stride_bytes >= qa_qc_entity_count(vm))
            return application_fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeC wire entity reference");
        integer = (int32_t)((uint32_t)reference / layout.stride_bytes); bytes = 2;
    } else if (builtin == QA_QC_BUILTIN_WRITECOORD || builtin == QA_QC_BUILTIN_WRITEANGLE) {
        if (!qa_qc_arg_float(vm, 1, &value, error) || !isfinite(value))
            return application_fail(error, QA_ERROR_ARGUMENT, "Nonfinite QuakeC message coordinate");
        bytes = builtin == QA_QC_BUILTIN_WRITECOORD ? 2 : 1;
    } else {
        if (!integer_argument(vm, 1, &integer, error)) return false;
        bytes = builtin == QA_QC_BUILTIN_WRITELONG ? 4 : builtin == QA_QC_BUILTIN_WRITESHORT ? 2 : 1;
    }
    if (bytes > message->capacity)
        return application_fail(error, QA_ERROR_MEMORY, "QuakeC message record exceeds source buffer");
    if (bytes > message->capacity - message->size) {
        if (message->destination != 0)
            return application_fail(error, QA_ERROR_MEMORY, "QuakeC reliable source message overflow");
        message->size = 0; message->reference_count = 0; message->overflowed = true;
    }
    size_t before = message->size;
    qa_net_writer writer;
    qa_net_writer_init(&writer, message->data + message->size, message->capacity - message->size, error);
    bool ok;
    switch (builtin) {
    case QA_QC_BUILTIN_WRITEBYTE: case QA_QC_BUILTIN_WRITECHAR:
        ok = qa_net_write_u8(&writer, (uint8_t)integer); break;
    case QA_QC_BUILTIN_WRITESHORT: case QA_QC_BUILTIN_WRITEENTITY:
        ok = qa_net_write_u16(&writer, (uint16_t)integer); break;
    case QA_QC_BUILTIN_WRITELONG: ok = qa_net_write_i32(&writer, integer); break;
    case QA_QC_BUILTIN_WRITECOORD: ok = qa_q1_write_coord(&writer, engine->protocol, value); break;
    case QA_QC_BUILTIN_WRITEANGLE: ok = qa_q1_write_angle(&writer, engine->protocol, value); break;
    case QA_QC_BUILTIN_WRITESTRING: ok = qa_net_write_string(&writer, text); break;
    default: return application_fail(error, QA_ERROR_ARGUMENT, "Unknown QuakeC message writer");
    }
    if (ok) {
        size_t size = before + qa_net_writer_size(&writer), reference_need = message->reference_count;
        unsigned kinds = engine->profile == QA_QC_QUAKEWORLD ? 2u : 1u;
        for (size_t end = before > 1 ? before : 1; end < size; ++end) {
            uint32_t word = message->data[end - 1] | (uint32_t)message->data[end] << 8;
            for (unsigned kind = 0; kind < kinds; ++kind)
                if (window_actor(vm, word, kind != 0).registry != 0) ++reference_need;
        }
        if (reference_need > message->reference_capacity) {
            size_t capacity = message->reference_capacity ? message->reference_capacity : 32;
            while (capacity < reference_need) {
                if (capacity > SIZE_MAX / 2) { capacity = reference_need; break; }
                capacity *= 2;
            }
            if (capacity > SIZE_MAX / sizeof(*message->references))
                return application_fail(error, QA_ERROR_MEMORY, "QuakeC protocol reference allocation overflow");
            qa_application_protocol_reference *references = realloc(message->references, capacity * sizeof(*references));
            if (references == NULL) return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC protocol references");
            message->references = references; message->reference_capacity = capacity;
        }
        message->size = size;
        for (size_t end = before > 1 ? before : 1; end < size; ++end) {
            uint32_t word = message->data[end - 1] | (uint32_t)message->data[end] << 8;
            for (unsigned kind = 0; kind < kinds; ++kind) {
                qa_actor_id actor = window_actor(vm, word, kind != 0);
                if (actor.registry == 0) continue;
                message->references[message->reference_count++] = (qa_application_protocol_reference){
                    .offset = end - 1, .actor = actor, .packed_sound = kind != 0};
            }
        }
    }
    return ok;
}
static bool publish(struct application_qc_state *engine, application_qc_message *message,
                     bool multicast, qa_vec3 origin, int32_t mode, qa_error *error)
{
    if (message->size == 0) return true;
    qa_application_protocol_event event = {.recipient = message->recipient, .origin = origin,
        .payload = {message->data, message->size}, .destination = multicast ? mode : (int32_t)message->destination,
        .references = message->references, .reference_count = message->reference_count,
        .reliable = multicast ? mode >= 3 : message->destination == 1 || message->destination == 2,
        .multicast = multicast, .signon = message->destination == 3};
    if (!application_emit_protocol(engine->provider, &event, error)) return false;
    message->size = 0; message->reference_count = 0; message->overflowed = false; return true;
}
bool application_qc_flush(struct application_qc_state *engine, qa_error *error)
{
    for (size_t i = 0; i < engine->message_count; ++i) {
        application_qc_message *message = &engine->messages[i];
        if (message->destination != 4 && !publish(engine, message, false, qa_v3(0, 0, 0), 0, error)) return false;
    }
    return true;
}
bool application_qc_multicast(struct application_qc_state *engine, qa_qc_instance *vm, qa_error *error)
{
    qa_vec3 origin; int32_t mode;
    if (!qa_qc_arg_vector(vm, 0, &origin, error) || !qa_vec_finite(origin) ||
        !integer_argument(vm, 1, &mode, error) || mode < 0 || mode > 5)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeC multicast mode or origin");
    for (size_t i = 0; i < engine->message_count; ++i)
        if (engine->messages[i].destination == 4)
            return publish(engine, &engine->messages[i], true, origin, mode, error);
    return true;
}
