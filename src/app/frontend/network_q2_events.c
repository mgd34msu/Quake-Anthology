#include "network_q2_events.h"

#include <stdlib.h>
#include <string.h>

typedef struct q2_event_packet {
    qa_application_network_q2 *publisher;
    qa_actor_owner host_source;
    const qa_application_protocol_event *source;
    const qa_application_q2_protocol_delivery *delivery;
    const qa_net_client *client;
    uint64_t epoch;
    qa_q2_codec codec;
    qa_net_writer writer;
    size_t ordinal;
} q2_event_packet;

static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }

static bool captured_recipient(const q2_event_packet *packet, uint8_t marker, size_t seat,
    const qa_application_q2_recipient *recipient)
{
    const qa_net_seat_binding *binding = packet->client->seats + seat;
    return recipient->has_connection && qa_net_client_id_equal(recipient->connection, packet->client->id) &&
        recipient->connection_epoch == packet->epoch && recipient->connection_seat.owner == binding->seat.owner &&
        recipient->connection_seat.index == binding->seat.index && recipient->remote_index == binding->remote_index &&
        (!marker || marker == (uint8_t)(recipient->remote_index + 1u));
}
static bool captured_seat(const q2_event_packet *packet, uint8_t marker, size_t seat)
{
    const qa_application_q2_audience *audience = &packet->delivery->audience;
    for (size_t i = 0; i < audience->count; ++i) {
        const qa_application_q2_recipient *recipient = audience->recipients + i;
        if (captured_recipient(packet, marker, seat, recipient)) return true;
    }
    return false;
}

static const qa_application_protocol_resource_reference *resource(const q2_event_packet *packet,
    size_t ordinal, qa_native_host_resource_kind kind, uint32_t index)
{
    for (size_t i = 0; i < packet->source->resource_count; ++i) {
        const qa_application_protocol_resource_reference *receipt = packet->source->resources + i;
        if (receipt->record_ordinal == ordinal && receipt->kind == kind && receipt->source_index == index) return receipt;
    }
    return NULL;
}

static bool entity(q2_event_packet *packet, const qa_q2_server_record *record,
    size_t relative_offset, uint32_t source_number, uint32_t *number, qa_error *error)
{
    uintptr_t raw = (uintptr_t)record->raw.data, first = (uintptr_t)packet->source->payload.data;
    if (raw < first || raw - first > packet->source->payload.size ||
        record->raw.size > packet->source->payload.size - (size_t)(raw - first))
        return fail(error, QA_ERROR_FORMAT, "Q2 event entity leaves its captured packet");
    size_t begin = (size_t)(raw - first);
    if (record->raw.size < 2 || relative_offset > record->raw.size - 2)
        return fail(error, QA_ERROR_FORMAT, "Q2 Source entity field exceeds its actual record");
    size_t offset = begin + relative_offset;
    for (size_t i = 0; i < packet->source->reference_count; ++i) {
        const qa_application_protocol_reference *reference = packet->source->references + i;
        if (reference->offset != offset) continue;
        uint32_t original = qa_load_u16le(packet->source->payload.data + reference->offset);
        if (reference->packed_sound) original >>= 3;
        if (original != source_number)
            return fail(error, QA_ERROR_FORMAT, "Q2 captured entity receipt differs from its actual Source word");
        return qa_application_network_q2_event_entity(packet->publisher, packet->source->provider,
            reference->actor, number, error);
    }
    return fail(error, QA_ERROR_FORMAT, "Q2 event has no captured full Source entity");
}

static uint32_t resource_base(bool rerelease, qa_native_host_resource_kind kind)
{
    static const uint32_t classic[] = {32, 288, 544}, modern[] = {62, 8254, 10302};
    return rerelease ? modern[kind] : classic[kind];
}

static bool translate(q2_event_packet *packet, const qa_q2_server_record *record,
    size_t ordinal, qa_q2_server_event *event, bool *include, qa_error *error)
{
    *event = record->event; *include = true;
    uint32_t number;
    if (event->kind == QA_Q2_SVC_MUZZLEFLASH) {
        if (!entity(packet, record, 1, event->data.muzzle.entity, &number, error)) return false;
        event->data.muzzle.entity = number;
    } else if (event->kind == QA_Q2_SVC_TEMP_ENTITY) {
        qa_q2_temp_entity *temporary = &event->data.temporary;
        for (size_t i = 0; i < temporary->field_count; ++i) {
            qa_q2_temp_field *field = temporary->fields + i;
            bool entity_field = field->name == QA_Q2_TEMP_ENTITY1 || field->name == QA_Q2_TEMP_ENTITY2;
            if (field->kind == QA_Q2_TEMP_INTEGER && field->value.integer >= 0 && entity_field &&
                temporary->type != QA_Q2_TE_STEAM && temporary->type != QA_Q2_TE_WIDOWBEAMOUT) {
                if (!entity(packet, record, 1u + field->offset, (uint32_t)field->value.integer, &number, error)) return false;
                if (number > INT16_MAX) return fail(error, QA_ERROR_FORMAT, "Q2 event entity exceeds its genuine temporary word");
                field->value.integer = (int32_t)number;
            }
        }
    } else if (event->kind == QA_Q2_SVC_SOUND) {
        const qa_application_protocol_resource_reference *receipt = resource(packet, ordinal,
            QA_NATIVE_HOST_SOUND, event->data.sound.index);
        if (!receipt) return fail(error, QA_ERROR_FORMAT, "Q2 sound has no captured Source resource spelling");
        if (!qa_application_network_q2_event_resource(packet->publisher, packet->source->provider, receipt, &number, error)) return false;
        if (number > UINT16_MAX) return fail(error, QA_ERROR_FORMAT, "Q2 sound exceeds its admitted resource word");
        event->data.sound.index = (uint16_t)number;
        if (event->data.sound.flags & 8u) {
            uint8_t flags = event->data.sound.flags;
            size_t offset = 2u + ((flags & 32u) ? 2u : 1u) + ((flags & 1u) != 0) +
                ((flags & 2u) != 0) + ((flags & 16u) != 0);
            if (!entity(packet, record, offset, event->data.sound.entity, &number, error)) return false;
            event->data.sound.entity = number;
        }
        /* Width flags describe the incoming Source encoding. The admitted
         * codec derives its own widths while retaining the semantic flags. */
        event->data.sound.flags &= (uint8_t)~(32u | 64u);
    } else if (event->kind == QA_Q2_SVC_POI && event->data.poi.time != UINT16_MAX) {
        const qa_application_protocol_resource_reference *receipt = resource(packet, ordinal,
            QA_NATIVE_HOST_IMAGE, event->data.poi.image);
        if (!receipt) return fail(error, QA_ERROR_FORMAT, "Q2 POI has no captured Source image spelling");
        if (!qa_application_network_q2_event_resource(packet->publisher, packet->source->provider, receipt, &number, error)) return false;
        if (number > UINT16_MAX) return fail(error, QA_ERROR_FORMAT, "Q2 POI exceeds its admitted image word");
        event->data.poi.image = (uint16_t)number;
    } else if (event->kind == QA_Q2_SVC_CONFIGSTRING) {
        bool rerelease = packet->delivery->profile == QA_NATIVE_Q2_GAME_API2023;
        for (unsigned kind = 0; kind <= QA_NATIVE_HOST_IMAGE; ++kind) {
            uint32_t base = resource_base(rerelease, (qa_native_host_resource_kind)kind);
            if (event->data.config.index < base) continue;
            const qa_application_protocol_resource_reference *receipt = resource(packet, ordinal,
                (qa_native_host_resource_kind)kind, event->data.config.index - base);
            if (!receipt) continue;
            if (!qa_application_network_q2_event_resource(packet->publisher, packet->source->provider, receipt, &number, error)) return false;
            qa_q2_config_layout layout;
            if (!qa_q2_config_layout_read(&packet->codec, &layout, error)) return false;
            const uint32_t bases[] = {layout.models, layout.sounds, layout.images};
            uint32_t wire_base = bases[kind];
            if (wire_base + number > UINT16_MAX) return fail(error, QA_ERROR_FORMAT, "Foreign Q2 configstring exceeds its admitted table");
            event->data.config.index = (uint16_t)(wire_base + number);
            if (!qa_application_network_q2_event_config(packet->publisher, event->data.config.index,
                &event->data.config.value, error)) return false;
            return true;
        }
        /* Network publishes the physical HOST's current globals from its
         * actual config owner, independently of this historical raw journal. */
        *include = false;
    }
    return true;
}

static bool marker(q2_event_packet *packet, uint8_t seat)
{
    qa_q2_server_event event = {.kind = QA_Q2_SVC_SEAT, .data.seat = seat};
    return qa_q2_server_event_write(&packet->codec, &packet->writer, &event);
}

static bool emit(void *context, const qa_q2_server_record *record, qa_error *error)
{
    q2_event_packet *packet = context;
    size_t ordinal = packet->ordinal++;
    if (record->event.kind == QA_Q2_SVC_SEAT) return true;
    size_t eligible = 0;
    for (size_t i = 0; i < packet->client->seat_count; ++i)
        if (captured_seat(packet, record->seat, i)) ++eligible;
    if (!eligible) return true;
    qa_q2_server_event event;
    bool include;
    if (!translate(packet, record, ordinal, &event, &include, error)) return false;
    if (!include) return true;
    if (event.kind == QA_Q2_SVC_LAYOUT) {
        const qa_application_q2_audience *audience = &packet->delivery->audience;
        for (size_t seat = 0; seat < packet->client->seat_count; ++seat)
            for (size_t i = 0; i < audience->count; ++i) {
                const qa_application_q2_recipient *recipient = audience->recipients + i;
                if (captured_recipient(packet, record->seat, seat, recipient) &&
                    !qa_application_network_q2_event_layout(packet->publisher, recipient->actor,
                        packet->delivery->profile, event.data.print.text, error)) return false;
            }
    }
    bool kex = packet->codec.protocol.kind == QA_NET_Q2KEX_2023;
    if (!kex) return qa_q2_server_event_write(&packet->codec, &packet->writer, &event);
    if (eligible == packet->client->seat_count)
        return marker(packet, 0) && qa_q2_server_event_write(&packet->codec, &packet->writer, &event) && marker(packet, 1);
    for (size_t i = 0; i < packet->client->seat_count; ++i) {
        if (!captured_seat(packet, record->seat, i)) continue;
        uint8_t seat = (uint8_t)(packet->client->seats[i].remote_index + 1u);
        if (!marker(packet, seat) || !qa_q2_server_event_write(&packet->codec, &packet->writer, &event)) return false;
    }
    return marker(packet, 1);
}

bool frontend_network_q2_event_packet(qa_application_network_q2 *publisher, qa_actor_owner host_source,
    const qa_application_protocol_event *source, const qa_application_q2_protocol_delivery *delivery,
    const qa_net_client *client, uint64_t epoch, const qa_q2_codec *codec, size_t capacity,
    qa_buffer *out, qa_error *error)
{
    if (!publisher || !host_source || !source || !delivery || !client || !codec || !out || out->data || out->size ||
        !capacity || !epoch || !client->seats || !client->seat_count || client->seat_count > QA_Q2_MAX_SEATS ||
        (source->payload.size && !source->payload.data) || (source->reference_count && !source->references) ||
        (source->resource_count && !source->resources) || !delivery->original ||
        (delivery->profile != QA_NATIVE_Q2_GAME_API3 && delivery->profile != QA_NATIVE_Q2_GAME_API2023) ||
        client->protocol.kind != codec->protocol.kind || client->protocol.revision != codec->protocol.revision)
        return fail(error, QA_ERROR_ARGUMENT, "Q2 event transcoding requires its actual Source and admitted codec");
    if (!delivery->audience.captured || !delivery->audience.count) return true;
    if (!delivery->audience.recipients) return fail(error, QA_ERROR_FORMAT, "Q2 event lost its captured audience");
    bool kex = codec->protocol.kind == QA_NET_Q2KEX_2023;
    if (!kex && client->seat_count != 1) return fail(error, QA_ERROR_FORMAT, "Q2 event needs its genuine single-seat wire");
    for (size_t i = 0; i < client->seat_count; ++i)
        if (client->seats[i].remote_index >= QA_Q2_MAX_SEATS)
            return fail(error, QA_ERROR_FORMAT, "Q2 event connection seat exceeds its admitted marker range");
    qa_buffer bytes = {.data = malloc(capacity)};
    if (!bytes.data) return fail(error, QA_ERROR_MEMORY, "Allocating admitted Q2 Source event packet");
    q2_event_packet packet = {.publisher = publisher, .host_source = host_source, .source = source,
        .delivery = delivery, .client = client, .epoch = epoch, .codec = *codec};
    qa_net_writer_init(&packet.writer, bytes.data, capacity, error);
    qa_net_protocol_id protocol = {.kind = delivery->profile == QA_NATIVE_Q2_GAME_API3 ? QA_NET_Q2_34 : QA_NET_Q2KEX_2023};
    qa_q2_message_options options = {.config_strings = delivery->profile == QA_NATIVE_Q2_GAME_API3 ? 2080u : 12448u,
        .inventory_slots = 256, .native_api2023 = delivery->profile == QA_NATIVE_Q2_GAME_API2023};
    qa_q2_messages *decoder = NULL;
    bool ok = qa_q2_messages_create(protocol, &options, &decoder, error) &&
        qa_q2_messages_read(decoder, source->payload, emit, &packet, error);
    qa_q2_messages_destroy(decoder);
    if (!ok) { qa_buffer_free(&bytes); return false; }
    bytes.size = qa_net_writer_size(&packet.writer);
    if (!bytes.size) qa_buffer_free(&bytes);
    *out = bytes; return true;
}
