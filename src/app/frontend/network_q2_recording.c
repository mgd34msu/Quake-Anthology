#include "network_q2_recording.h"
#include "network_q2_events.h"
#include "internal.h"
#include "../application/control_frame.h"
#include "qa/application_native_q2_delivery.h"
#include "qa/world.h"
#include <string.h>

enum { Q2_RECORD_MVD, Q2_RECORD_SERVER, Q2_RECORD_FEEDS };

typedef struct q2_record_feed {
    frontend_network_q2_recording *owner;
    frontend_demo_format format;
    frontend_demo_sink sink;
    qa_unified_frame_lease *header_storage, *frame_storage;
    qa_q2_mvd_header header;
    qa_q2_mvd_frame previous;
    uint64_t event_next, tick, source_frame, map_revision;
    qa_error failure, sink_failure;
    bool held, seeded, failed, sink_failed;
} q2_record_feed;

struct frontend_network_q2_recording {
    qa_frontend *frontend;
    qa_application_network_q2 *source;
    qa_unified_frame_lease *storage, *header_storage;
    qa_q2_mvd_header header;
    qa_application_event_subscription events;
    qa_q2_messages *decoders[2];
    q2_record_feed feeds[Q2_RECORD_FEEDS];
    frontend_demo_sink broadcast;
    uint64_t config_revision, match_revision, map_revision;
    bool header_ready;
    unsigned calls;
    uint8_t mvd_bytes[QA_Q2_MVD_MESSAGE_BYTES];
    /* SV_RecordDemoMessage uses the same 32768-byte message extent. */
    uint8_t server_bytes[QA_Q2_MVD_MESSAGE_BYTES];
};

typedef struct q2_record_capture {
    qa_unified_frame_lease *storage;
    qa_q2_mvd_frame frame;
    qa_application_network_q2_metadata metadata;
} q2_record_capture;

typedef struct q2_record_write {
    frontend_network_q2_recording *owner;
    q2_record_feed *mvd, *server;
    qa_q2_codec codec;
    qa_net_writer mvd_writer, server_writer;
    const qa_application_q2_audience *audience;
    qa_actor_id addressed_actor;
    qa_native_profile source_profile;
    qa_q2_mvd_recipient recipient;
    uint16_t target;
    qa_actor_owner provider;
    bool reliable, multicast;
} q2_record_write;

static bool metadata(frontend_network_q2_recording *owner,
    qa_application_network_q2_metadata *out, qa_error *error)
{
    if (!qa_application_network_q2_metadata_read(owner->source, out, error)) return false;
    qa_application_network_q2_host actual;
    if (!qa_application_network_q2_host_source(owner->frontend->application,
        out->protocol, &actual, error)) return false;
    out->clock = actual.source.clock;
    out->clock_config = actual.source.clock_config;
    out->server_time_ns = actual.source.server_time_ns;
    out->map_revision = actual.source.map_revision;
    return true;
}

static bool feed_current(const void *context)
{
    const q2_record_feed *feed = context;
    return feed && feed->held && feed->owner->source && feed->owner->frontend->application;
}

static void feed_state_clear(q2_record_feed *feed)
{
    qa_unified_frame_lease_release(feed->header_storage);
    qa_unified_frame_lease_release(feed->frame_storage);
    feed->header_storage = feed->frame_storage = NULL;
    feed->header = (qa_q2_mvd_header){0};
    feed->previous = (qa_q2_mvd_frame){0};
    feed->seeded = false;
}

static bool any_feed(const frontend_network_q2_recording *owner)
{
    if (owner->broadcast.append) return true;
    for (unsigned i = 0; i < Q2_RECORD_FEEDS; ++i)
        if (owner->feeds[i].held) return true;
    return false;
}

static void events_retire(frontend_network_q2_recording *owner)
{
    uint64_t next = qa_application_events_next(owner->frontend->application);
    for (unsigned i = 0; i < Q2_RECORD_FEEDS; ++i)
        if (((owner->feeds[i].held && !owner->feeds[i].sink_failed) ||
                (i == Q2_RECORD_MVD && owner->broadcast.append)) &&
            !owner->feeds[i].failed && owner->feeds[i].event_next < next)
            next = owner->feeds[i].event_next;
    if (owner->events.application) qa_application_events_consumed(&owner->events, next);
    if (!any_feed(owner)) qa_application_events_unsubscribe(&owner->events);
}

static bool header_refresh(frontend_network_q2_recording *owner,
    qa_unified_frame_lease *storage, qa_application_network_q2_metadata *metadata, qa_error *error)
{
    const qa_string_id *configs;
    size_t count;
    uint64_t revision, match_revision = 0;
    qa_q2_config_entry match;
    bool ok = qa_application_network_q2_config_values(owner->source, &configs, &count, &revision, error) &&
        qa_application_network_q2_metadata_read(owner->source, metadata, error);
    if (ok) (void)qa_application_network_q2_match_status(owner->source, &match, &match_revision);
    if (ok && (!owner->header_ready || owner->config_revision != revision ||
        owner->match_revision != match_revision || owner->map_revision != metadata->map_revision)) {
        qa_q2_mvd_header header;
        ok = qa_application_network_q2_mvd_header(owner->source, storage, &header, error) &&
            qa_unified_frame_lease_retain(storage, error);
        if (ok) {
            qa_unified_frame_lease_release(owner->header_storage);
            owner->header_storage = storage;
            owner->header = header;
            owner->config_revision = revision;
            owner->match_revision = match_revision;
            owner->map_revision = metadata->map_revision;
            owner->header_ready = true;
        }
    }
    return ok;
}

static bool capture(frontend_network_q2_recording *owner,
    q2_record_capture *out, qa_error *error)
{
    q2_record_capture value = *out;
    if (!value.storage) value.storage = application_control_storage_acquire(owner->frontend->application, error);
    if (!value.storage) return false;
    bool ok = qa_application_network_q2_mvd_frame(owner->source, value.storage, &value.frame, error) &&
        header_refresh(owner, value.storage, &value.metadata, error);
    if (!ok) {
        qa_unified_frame_lease_release(value.storage);
        *out = (q2_record_capture){0};
        return false;
    }
    *out = value;
    return true;
}

static bool packet(const frontend_demo_sink *sink, frontend_demo_format format,
    const qa_net_writer *writer, qa_error *error)
{
    frontend_demo_packet value = {.format = format,
        .value.message = {writer->data, qa_net_writer_size(writer)}};
    return sink->append(sink->owner, &value, error);
}

static bool feed_packet(q2_record_feed *feed, const qa_net_writer *writer, qa_error *error)
{
    if (feed->sink.append && !feed->sink_failed &&
        !packet(&feed->sink, feed->format, writer, &feed->sink_failure)) feed->sink_failed = true;
    if (feed->format == FRONTEND_DEMO_MVD && feed->owner->broadcast.append)
        return packet(&feed->owner->broadcast, feed->format, writer, error);
    return true;
}

static bool mvd_seed_write(frontend_network_q2_recording *owner,
    const qa_q2_mvd_header *header, const qa_q2_mvd_frame *frame,
    const frontend_demo_sink *sink, qa_error *error)
{
    qa_net_writer writer;
    qa_net_writer_init(&writer, owner->mvd_bytes, sizeof(owner->mvd_bytes), error);
    return qa_q2_mvd_write_header(&writer, header) &&
        qa_q2_mvd_write_frame(&header->profile, &writer, NULL, frame) &&
        packet(sink, FRONTEND_DEMO_MVD, &writer, error);
}

static bool server_signon(frontend_network_q2_recording *owner,
    const frontend_demo_sink *sink, uint8_t *bytes, qa_error *error)
{
    qa_q2_serverdata data = {.servercount = owner->header.servercount};
    memcpy(data.gamedir, owner->header.gamedir, sizeof(data.gamedir));
    for (size_t i = 0; i < owner->header.config_count; ++i) {
        const qa_q2_mvd_config *row = owner->header.configstrings + i;
        if (row->index) continue;
        size_t length = strlen(row->value);
        if (length >= sizeof(data.levelname))
            return frontend_fail(error, QA_ERROR_FORMAT, "Q2 server recording level title exceeds its wire field");
        memcpy(data.levelname, row->value, length + 1);
        break;
    }
    qa_net_writer writer;
    qa_net_writer_init(&writer, bytes, sizeof(owner->server_bytes), error);
    return qa_q2_server_demo_signon_write(&writer, &data,
        owner->header.configstrings, owner->header.config_count) &&
        packet(sink, FRONTEND_DEMO_Q2_SERVER, &writer, error);
}

static bool feed_header_set(q2_record_feed *feed, qa_error *error)
{
    frontend_network_q2_recording *owner = feed->owner;
    if (!qa_unified_frame_lease_retain(owner->header_storage, error)) return false;
    qa_unified_frame_lease_release(feed->header_storage);
    feed->header_storage = owner->header_storage;
    feed->header = owner->header;
    return true;
}

static bool feed_frame_set(q2_record_feed *feed, const q2_record_capture *captured, qa_error *error)
{
    if (!qa_unified_frame_lease_retain(captured->storage, error)) return false;
    qa_unified_frame_lease_release(feed->frame_storage);
    feed->frame_storage = captured->storage;
    feed->previous = captured->frame;
    return true;
}

static bool feed_seed(void *context, const frontend_demo_sink *sink, qa_error *error)
{
    q2_record_feed *feed = context;
    frontend_network_q2_recording *owner = feed->owner;
    if (feed->format == FRONTEND_DEMO_MVD && feed->seeded)
        return mvd_seed_write(owner, &feed->header, &feed->previous, sink, error);
    q2_record_capture captured = {0};
    if (!capture(owner, &captured, error)) return false;
    ++owner->calls;
    bool ok;
    if (feed->format == FRONTEND_DEMO_MVD) {
        ok = mvd_seed_write(owner, &owner->header, &captured.frame, sink, error) &&
            feed_frame_set(feed, &captured, error);
    } else ok = server_signon(owner, sink, owner->server_bytes, error);
    if (ok) ok = feed_header_set(feed, error);
    if (ok) {
        if (!owner->events.application) qa_application_events_subscribe(owner->frontend->application, &owner->events);
        feed->event_next = qa_application_events_next(owner->frontend->application);
        feed->source_frame = captured.metadata.clock.frame_number;
        feed->tick = captured.metadata.server_time_ns / UINT64_C(100000000);
        feed->map_revision = captured.metadata.map_revision;
        feed->seeded = true;
    }
    --owner->calls;
    qa_unified_frame_lease_release(captured.storage);
    return ok;
}

static bool feed_attach(void *context, const frontend_demo_sink *sink, bool *attached, qa_error *error)
{
    q2_record_feed *feed = context;
    if (!feed->seeded || feed->sink.append)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 recording feed is not available for attachment");
    feed->sink = *sink;
    *attached = true;
    return true;
}

static bool feed_detach(void *context, const frontend_demo_sink *sink, qa_error *error)
{
    q2_record_feed *feed = context;
    if (feed->owner->calls || feed->sink.owner != sink->owner || feed->sink.append != sink->append)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 recording sink is still entered or differs");
    feed->sink = (frontend_demo_sink){0};
    return true;
}

static bool feed_release(void **context, qa_error *error)
{
    q2_record_feed *feed = context ? *context : NULL;
    if (!feed) return true;
    if (feed->owner->calls || feed->sink.append)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 recording feed remains entered or attached");
    feed->held = false;
    feed->sink_failed = false;
    feed->sink_failure = (qa_error){0};
    if (feed->format != FRONTEND_DEMO_MVD || !feed->owner->broadcast.append) {
        feed_state_clear(feed);
        feed->failed = false;
        feed->failure = (qa_error){0};
    }
    events_retire(feed->owner);
    *context = NULL;
    return true;
}

static bool mvd_flush(q2_record_write *write, qa_error *error)
{
    if (!qa_net_writer_size(&write->mvd_writer)) return true;
    if (!feed_packet(write->mvd, &write->mvd_writer, error)) return false;
    qa_net_writer_init(&write->mvd_writer, write->mvd_writer.data,
        write->mvd_writer.capacity, error);
    return true;
}

static bool mvd_append(q2_record_write *write, const qa_q2_mvd_record *record, qa_error *error)
{
    uint8_t bytes[2047 + 5];
    qa_net_writer writer;
    qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (!qa_q2_mvd_write_record(&write->owner->header.profile, &writer, record)) return false;
    size_t count = qa_net_writer_size(&writer);
    return qa_net_write_data(&write->mvd_writer, bytes, count);
}

static bool config_changes(q2_record_write *write, q2_record_feed *feed, qa_error *error)
{
    const qa_q2_mvd_header *now = &write->owner->header, *old = &feed->header;
    if (feed->header.configstrings == now->configstrings) return true;
    size_t i = 0, j = 0;
    while (i < old->config_count || j < now->config_count) {
        uint32_t previous = i < old->config_count ? old->configstrings[i].index : UINT32_MAX;
        uint32_t current = j < now->config_count ? now->configstrings[j].index : UINT32_MAX;
        const char *from = previous <= current ? old->configstrings[i++].value : "";
        const char *to = current <= previous ? now->configstrings[j++].value : "";
        uint16_t index = (uint16_t)(previous < current ? previous : current);
        if (!strcmp(from, to)) continue;
        if (feed->format == FRONTEND_DEMO_MVD) {
            qa_q2_mvd_record record = {.command.opcode = QA_MVD_CONFIGSTRING,
                .data.config = {index, {(const uint8_t *)to, strlen(to)}}};
            uint8_t bytes[2047 + 5];
            qa_net_writer encoded;
            qa_net_writer_init(&encoded, bytes, sizeof(bytes), error);
            if (!qa_q2_mvd_write_record(&now->profile, &encoded, &record)) return false;
            size_t count = qa_net_writer_size(&encoded);
            if (count > write->mvd_writer.capacity - qa_net_writer_size(&write->mvd_writer) &&
                !mvd_flush(write, error)) return false;
            if (!qa_net_write_data(&write->mvd_writer, bytes, count)) return false;
        } else {
            qa_q2_server_event event = {.kind = QA_Q2_SVC_CONFIGSTRING, .data.config = {index, to}};
            if (!qa_q2_server_event_write(&write->codec, &write->server_writer, &event)) return false;
        }
    }
    return true;
}

static bool route_player(q2_record_write *write, qa_actor_id actor, qa_actor_owner provider, qa_error *error)
{
    if (!actor.registry) { write->recipient = QA_MVD_RECIPIENT_ALL; write->target = 0; return true; }
    uint32_t slot;
    if (!qa_application_network_q2_event_entity(write->owner->source, provider, actor, &slot, error)) return false;
    if (!slot || slot > 255)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q2 recording recipient cannot use an MVD player number");
    write->recipient = QA_MVD_RECIPIENT_PLAYER;
    write->target = (uint16_t)(slot - 1);
    return true;
}

static bool route_multicast(q2_record_write *write, qa_application_q2_delivery_kind kind,
    qa_vec3 origin, qa_error *error)
{
    write->target = 0;
    write->multicast = true;
    write->recipient = kind == QA_APPLICATION_Q2_PHS ? QA_MVD_RECIPIENT_PHS :
        kind == QA_APPLICATION_Q2_PVS ? QA_MVD_RECIPIENT_PVS : QA_MVD_RECIPIENT_ALL;
    if (write->recipient == QA_MVD_RECIPIENT_ALL) return true;
    qa_collision_leaf leaf;
    if (!qa_collision_point_leaf(qa_world_geometry(qa_application_world(write->owner->frontend->application)),
        origin, QA_LEAF_COLLISION, &leaf, error)) return false;
    if (leaf.leaf >= UINT16_MAX)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q2 multicast leaf exceeds the MVD leaf field");
    write->target = (uint16_t)leaf.leaf;
    return true;
}

static bool event_record(void *context, const qa_q2_server_record *record, qa_error *error)
{
    q2_record_write *write = context;
    if (record->event.kind == QA_Q2_SVC_SEAT) return true;
    if (record->event.kind == QA_Q2_SVC_LAYOUT && write->addressed_actor.registry &&
        !qa_application_network_q2_event_layout(write->owner->source, write->addressed_actor,
            write->source_profile, record->event.data.print.text, error)) return false;
    if (write->server && !write->server->failed && write->multicast &&
        !qa_q2_server_event_write(&write->codec, &write->server_writer, &record->event)) write->server->failed = true;
    if (!write->mvd || write->mvd->failed) return true;
    uint8_t bytes[2047];
    qa_net_writer writer;
    qa_net_writer_init(&writer, bytes, sizeof(bytes), &write->mvd->failure);
    if (!qa_q2_server_event_write(&write->codec, &writer, &record->event)) {
        write->mvd->failed = true;
        return true;
    }
    qa_q2_mvd_record output = {.command.opcode = QA_MVD_ALL,
        .data.route = {write->recipient, write->target, write->reliable,
            {bytes, qa_net_writer_size(&writer)}}};
    if (!write->multicast && write->audience && write->audience->count) {
        for (size_t i = 0; i < write->audience->count; ++i) {
            const qa_application_q2_recipient *recipient = write->audience->recipients + i;
            if (record->seat && record->seat != (uint8_t)(recipient->remote_index + 1u)) continue;
            if (!route_player(write, recipient->actor, write->provider, error)) return false;
            if (record->event.kind == QA_Q2_SVC_LAYOUT &&
                !qa_application_network_q2_event_layout(write->owner->source, recipient->actor,
                    write->source_profile, record->event.data.print.text, error)) return false;
            output.data.route.recipient = write->recipient;
            output.data.route.target = write->target;
            if (!mvd_append(write, &output, &write->mvd->failure)) {
                write->mvd->failed = true;
                break;
            }
        }
        return true;
    }
    if (!mvd_append(write, &output, &write->mvd->failure)) write->mvd->failed = true;
    return true;
}

static bool output_event(q2_record_write *write, const qa_application_event_view *view, qa_error *error)
{
    frontend_network_q2_recording *owner = write->owner;
    write->audience = NULL;
    write->addressed_actor = (qa_actor_id){0};
    write->multicast = false;
    write->reliable = false;
    if (view->protocol) {
        const qa_application_protocol_event *source = view->protocol;
        const qa_ruleset_descriptor *rules = qa_ruleset_read(source->dialect);
        if (!rules || rules->family != QA_GAME_Q2 || source->signon || !view->q2_delivery) return true;
        write->provider = source->provider;
        write->source_profile = view->q2_delivery->profile;
        write->addressed_actor = source->recipient;
        write->reliable = source->reliable;
        write->audience = &view->q2_delivery->audience;
        if (source->multicast) {
            qa_application_q2_delivery_kind kind = source->destination == 1 ? QA_APPLICATION_Q2_PHS :
                source->destination == 2 ? QA_APPLICATION_Q2_PVS : QA_APPLICATION_Q2_ALL;
            if (!route_multicast(write, kind, source->origin, error)) return false;
        } else {
            if (!write->mvd) return true;
            if (!route_player(write, source->recipient, source->provider, error)) return false;
        }
        qa_application_network_q2_metadata host;
        if (!qa_application_network_q2_metadata_read(owner->source, &host, error)) return false;
        return frontend_network_q2_event_records(owner->source, host.source_owner,
            source, view->q2_delivery, &write->codec, owner->decoders, event_record, write, error);
    }
    if (view->kind == QA_APPLICATION_EVENT_BUILTIN) {
        const qa_builtin_event *event = view->value.builtin;
        if (event->family != QA_GAME_Q2) return true;
        write->provider = event->provider;
        write->source_profile = owner->header.profile.rerelease ? QA_NATIVE_Q2_GAME_API2023 : QA_NATIVE_Q2_GAME_API3;
        write->reliable = event->kind == QA_BUILTIN_SOUND &&
            qa_q2_sound_reliable(&write->codec,event->flags,event->channel);
        bool message = event->kind == QA_BUILTIN_MESSAGE || event->kind == QA_BUILTIN_CENTERPRINT;
        if (message) {
            if (!write->mvd) return true;
            write->reliable = true;
            write->addressed_actor = event->actor;
            if (!route_player(write, event->actor, event->provider, error)) return false;
        } else {
            qa_builtin_q2_multicast_kind multicast = event->q2_multicast.kind;
            qa_application_q2_delivery_kind kind = multicast == QA_BUILTIN_Q2_MULTICAST_PVS ? QA_APPLICATION_Q2_PVS :
                multicast == QA_BUILTIN_Q2_MULTICAST_PHS || multicast == QA_BUILTIN_Q2_MULTICAST_PHS_LINE ?
                QA_APPLICATION_Q2_PHS : QA_APPLICATION_Q2_ALL;
            if (multicast == QA_BUILTIN_Q2_MULTICAST_NONE && event->kind == QA_BUILTIN_SOUND &&
                event->attenuation != 0 && !((uint32_t)event->channel & 8u)) kind = QA_APPLICATION_Q2_PHS;
            qa_vec3 origin = multicast == QA_BUILTIN_Q2_MULTICAST_NONE ? event->origin : event->q2_multicast.origin;
            if (!route_multicast(write, kind, origin, error)) return false;
        }
        return qa_application_network_q2_builtin_records(owner->source, event,
            &write->codec, event_record, write, error);
    }
    if (view->kind == QA_APPLICATION_EVENT_Q2_PLAYER) {
        if (!write->mvd) return true;
        const qa_application_q2_player_event *event = view->value.q2_player;
        switch (event->event.kind) {
        case QA_Q2_PLAYER_PRINT: case QA_Q2_PLAYER_STUFFTEXT:
        case QA_Q2_PLAYER_LAYOUT: case QA_Q2_PLAYER_INVENTORY: break;
        default: return true;
        }
        write->provider = event->provider;
        write->source_profile = owner->header.profile.rerelease ? QA_NATIVE_Q2_GAME_API2023 : QA_NATIVE_Q2_GAME_API3;
        write->reliable = true;
        qa_actor_id actor = event->event.kind == QA_Q2_PLAYER_PRINT ? event->event.target : event->event.actor;
        write->addressed_actor = actor;
        if (!route_player(write, actor, event->provider, error)) return false;
        return qa_application_network_q2_player_records(owner->source, &event->event,
            &write->codec, event_record, write, error);
    }
    if (view->kind == QA_APPLICATION_EVENT_Q2_MAP) {
        const qa_application_q2_map_event *event = view->value.q2_map;
        if (event->event.kind != QA_Q2_MAP_STEAM && event->event.kind != QA_Q2_MAP_FORCE_WALL) return true;
        const qa_application_q2_audience *audience = view->q2_audience;
        if (!audience || !audience->captured)
            return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Q2 map effect lacks its retained multicast origin");
        write->provider = event->provider;
        if (!route_multicast(write, audience->kind, audience->multicast_origin, error)) return false;
        return qa_application_network_q2_map_records(owner->source, &event->event,
            &write->codec, event_record, write, error);
    }
    return true;
}

static bool frame_write(q2_record_write *write, const q2_record_capture *captured,
    qa_bytes mvd_events, qa_bytes server_events, qa_error *error)
{
    frontend_network_q2_recording *owner = write->owner;
    if (write->server) {
        uint8_t *bytes = qa_unified_frame_lease_alloc(captured->storage,
            sizeof(owner->server_bytes), 1, 1, error);
        if (!bytes) return false;
        qa_net_writer_init(&write->server_writer, bytes, sizeof(owner->server_bytes), error);
        bool changed = !write->server->seeded || write->server->map_revision != captured->metadata.map_revision;
        if (changed && !server_signon(owner, &write->server->sink, bytes, error)) return false;
        uint32_t bits = (uint32_t)captured->metadata.clock.frame_number;
        int32_t frame;
        memcpy(&frame, &bits, sizeof(frame));
        if (!qa_q2_server_demo_frame_write(&write->server_writer, frame,
            (qa_q2_entity_span){captured->frame.entities, captured->frame.entity_count}, (qa_bytes){0})) return false;
        if (!changed && !config_changes(write, write->server, error)) return false;
        if (!qa_net_write_data(&write->server_writer, server_events.data, server_events.size) ||
            !feed_packet(write->server, &write->server_writer, error)) return false;
    }
    if (write->mvd) {
        uint8_t *bytes = qa_unified_frame_lease_alloc(captured->storage,
            sizeof(owner->mvd_bytes), 1, 1, error);
        if (!bytes) return false;
        qa_net_writer_init(&write->mvd_writer, bytes, sizeof(owner->mvd_bytes), error);
        bool changed = !write->mvd->seeded || write->mvd->map_revision != captured->metadata.map_revision;
        if (changed) {
            if (!qa_q2_mvd_write_header(&write->mvd_writer, &owner->header) ||
                !qa_q2_mvd_write_frame(&owner->header.profile, &write->mvd_writer, NULL, &captured->frame)) return false;
        } else if (!config_changes(write, write->mvd, error) || !mvd_flush(write, error) ||
            !qa_q2_mvd_write_command(&write->mvd_writer, (qa_q2_mvd_command){QA_MVD_FRAME, 0}) ||
            !qa_q2_mvd_write_frame(&owner->header.profile, &write->mvd_writer,
                &write->mvd->previous, &captured->frame)) return false;
        if (!mvd_flush(write, error)) return false;
        if (mvd_events.size) {
            qa_net_writer events = {.data = owner->mvd_bytes,
                .capacity = mvd_events.size, .bit = mvd_events.size * 8};
            if (!feed_packet(write->mvd, &events, error)) return false;
        }
    }
    return true;
}

bool frontend_network_q2_recording_publish(frontend_network_q2_recording *owner, qa_error *error)
{
    if (!owner || owner->calls) return owner == NULL;
    (void)error;
    qa_error failure = {0};
    qa_application_network_q2_metadata current;
    if (!metadata(owner, &current, &failure)) {
        for (unsigned i = 0; i < Q2_RECORD_FEEDS; ++i) if (owner->feeds[i].sink.append ||
            (i == Q2_RECORD_MVD && owner->broadcast.append)) {
            owner->feeds[i].failed = true;
            owner->feeds[i].failure = failure;
        }
        events_retire(owner);
        return true;
    }
    q2_record_write write = {.owner = owner};
    q2_record_feed *mvd = &owner->feeds[Q2_RECORD_MVD], *server = &owner->feeds[Q2_RECORD_SERVER];
    uint64_t tick = current.server_time_ns / UINT64_C(100000000);
    if (((mvd->sink.append && !mvd->sink_failed) || owner->broadcast.append) && !mvd->failed &&
        (!mvd->seeded || mvd->tick != tick || mvd->map_revision != current.map_revision)) write.mvd = mvd;
    if (server->sink.append && !server->sink_failed && !server->failed && (!server->seeded || server->source_frame != current.clock.frame_number ||
        server->map_revision != current.map_revision)) write.server = server;
    if (!write.mvd && !write.server) return true;
    q2_record_capture captured = {.storage = application_control_storage_acquire(owner->frontend->application, &failure)};
    ++owner->calls;
    bool ok = captured.storage != NULL;
    if (ok && !owner->header_ready) ok = header_refresh(owner, captured.storage, &captured.metadata, &failure);
    if (ok) ok = qa_q2_codec_init(&write.codec, owner->header.profile.protocol, &failure);
    if (ok && owner->header.profile.extended && !owner->header.profile.rerelease)
        write.codec.wire_flags = 8u | (owner->header.profile.v2 ? 16u : 0u);
    qa_net_writer_init(&write.mvd_writer, owner->mvd_bytes, sizeof(owner->mvd_bytes), &mvd->failure);
    qa_net_writer_init(&write.server_writer, owner->server_bytes, sizeof(owner->server_bytes), &server->failure);
    qa_application *app = owner->frontend->application;
    uint64_t end = qa_application_events_next(app);
    qa_application_event_range range = {.next = owner->events.next, .end = end};
    qa_application_event_view view;
    while (ok && qa_application_event_raw_next(app, &range, UINT64_MAX, &view)) {
        q2_record_feed *mvd_output = write.mvd, *server_output = write.server;
        if (write.mvd && (write.mvd->failed || range.id < write.mvd->event_next)) write.mvd = NULL;
        if (write.server && (write.server->failed || range.id < write.server->event_next)) write.server = NULL;
        if (write.mvd || write.server) {
            qa_application_event_cursor cursor = {.id = range.id};
            bool first = true;
            while (ok && qa_application_event_read(app, &cursor, &view)) {
                if (view.protocol || first) ok = output_event(&write, &view, &failure);
                first = false;
            }
        }
        write.mvd = mvd_output;
        write.server = server_output;
    }
    qa_bytes mvd_events = {owner->mvd_bytes, qa_net_writer_size(&write.mvd_writer)};
    qa_bytes server_events = {owner->server_bytes, qa_net_writer_size(&write.server_writer)};
    if (ok) ok = capture(owner, &captured, &failure);
    q2_record_feed *delivered[] = {write.mvd, write.server};
    for (size_t i = 0; i < sizeof(delivered) / sizeof(*delivered); ++i) {
        q2_record_feed *feed = delivered[i];
        if (!feed || feed->failed) continue;
        if (!ok) {
            feed->failed = true;
            feed->failure = failure;
            continue;
        }
        q2_record_write one = write;
        one.mvd = feed == mvd ? feed : NULL;
        one.server = feed == server ? feed : NULL;
        bool accepted = frame_write(&one, &captured, mvd_events, server_events, &feed->failure);
        if (accepted && feed == mvd) accepted = feed_frame_set(feed, &captured, &feed->failure);
        if (accepted) accepted = feed_header_set(feed, &feed->failure);
        if (accepted) {
            feed->event_next = end;
            feed->tick = captured.metadata.server_time_ns / UINT64_C(100000000);
            feed->source_frame = captured.metadata.clock.frame_number;
            feed->map_revision = captured.metadata.map_revision;
            feed->seeded = true;
        } else feed->failed = true;
    }
    --owner->calls;
    qa_unified_frame_lease_release(captured.storage);
    events_retire(owner);
    return true;
}

static bool feed_publish(void *context, qa_error *error)
{
    q2_record_feed *feed = context;
    if (!feed->failed && !frontend_network_q2_recording_publish(feed->owner, error)) return false;
    if (feed->sink_failed) { if (error) *error = feed->sink_failure; return false; }
    if (!feed->failed) return true;
    if (error) *error = feed->failure;
    return false;
}

bool frontend_network_q2_recording_create(qa_frontend *frontend, qa_application_network_q2 *source,
    frontend_network_q2_recording **out, qa_error *error)
{
    qa_unified_frame_lease *storage = application_control_storage_acquire(frontend->application, error);
    if (!storage) return false;
    frontend_network_q2_recording *owner = qa_unified_frame_lease_alloc(storage, 1,
        sizeof(*owner), _Alignof(frontend_network_q2_recording), error);
    if (!owner) { qa_unified_frame_lease_release(storage); return false; }
    owner->storage = storage;
    owner->frontend = frontend;
    owner->source = source;
    for (unsigned i = 0; i < 2; ++i) {
        qa_net_protocol_id protocol = {.kind = i ? QA_NET_Q2KEX_2023 : QA_NET_Q2_34};
        qa_q2_message_options options = {.config_strings = i ? QA_Q2_RERELEASE_CONFIGSTRINGS : QA_Q2_CLASSIC_CONFIGSTRINGS,
            .inventory_slots = 256, .native_api2023 = i != 0};
        if (!qa_q2_messages_create(protocol, &options, owner->decoders + i, error)) {
            (void)frontend_network_q2_recording_destroy(&owner, NULL);
            return false;
        }
    }
    owner->feeds[Q2_RECORD_MVD] = (q2_record_feed){.owner = owner, .format = FRONTEND_DEMO_MVD};
    owner->feeds[Q2_RECORD_SERVER] = (q2_record_feed){.owner = owner, .format = FRONTEND_DEMO_Q2_SERVER};
    *out = owner;
    return true;
}

bool frontend_network_q2_recording_source(frontend_network_q2_recording *owner,
    frontend_demo_action action, qa_fs_root *root, frontend_demo_record_source *out, qa_error *error)
{
    if (action != FRONTEND_DEMO_MVD_RECORD && action != FRONTEND_DEMO_SERVER_RECORD)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 recording action has no Source feed");
    q2_record_feed *feed = &owner->feeds[action == FRONTEND_DEMO_MVD_RECORD ? Q2_RECORD_MVD : Q2_RECORD_SERVER];
    if (feed->held) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 recording feed is already retained");
    qa_application_network_q2_metadata current;
    if (!metadata(owner, &current, error)) return false;
    if (feed->format == FRONTEND_DEMO_Q2_SERVER &&
        (current.edition != QA_RULESET_Q2_CLASSIC || current.protocol.kind != QA_NET_Q2_34))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Original Q2 serverrecord requires the classic Source wire layout");
    feed->held = true;
    if (!feed->seeded) feed->event_next = qa_application_events_next(owner->frontend->application);
    *out = (frontend_demo_record_source){.owner = feed, .format = feed->format,
        .protocol = current.protocol, .root = root, .current = feed_current,
        .seed = feed_seed, .attach = feed_attach, .detach = feed_detach,
        .publish = feed_publish, .release = feed_release};
    return true;
}

bool frontend_network_q2_recording_rebind(frontend_network_q2_recording *owner,
    qa_application_network_q2 *source, qa_error *error)
{
    if (owner->calls) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 recording source remains entered");
    owner->source = source;
    qa_unified_frame_lease_release(owner->header_storage);
    owner->header_storage = NULL;
    owner->header = (qa_q2_mvd_header){0};
    owner->header_ready = false;
    for (unsigned i = 0; i < Q2_RECORD_FEEDS; ++i) feed_state_clear(owner->feeds + i);
    return true;
}

bool frontend_network_q2_recording_broadcast(frontend_network_q2_recording *owner,
    const frontend_demo_sink *sink, qa_error *error)
{
    if (!owner) return !sink;
    if (owner->calls) return frontend_fail(error, QA_ERROR_ARGUMENT, "MVD capture is still entered");
    q2_record_feed *feed = owner->feeds + Q2_RECORD_MVD;
    bool was_active = owner->broadcast.append != NULL;
    owner->broadcast = sink ? *sink : (frontend_demo_sink){0};
    if (sink && !was_active && !feed->held) {
        feed_state_clear(feed);
        feed->failed = false;
        feed->failure = (qa_error){0};
        feed->event_next = qa_application_events_next(owner->frontend->application);
    }
    if (sink && !owner->events.application)
        qa_application_events_subscribe(owner->frontend->application, &owner->events);
    if (!sink && !feed->held) feed_state_clear(feed);
    events_retire(owner);
    return true;
}

bool frontend_network_q2_recording_broadcast_seed(frontend_network_q2_recording *owner,
    const frontend_demo_sink *sink, qa_error *error)
{
    if (!owner) return true;
    q2_record_feed *feed = owner->feeds + Q2_RECORD_MVD;
    if (feed->failed) { if (error) *error = feed->failure; return false; }
    if (!feed->seeded) return true;
    return mvd_seed_write(owner, &feed->header, &feed->previous, sink, error);
}

bool frontend_network_q2_recording_idle(const frontend_network_q2_recording *owner)
{ return !owner || owner->calls == 0; }

bool frontend_network_q2_recording_destroy(frontend_network_q2_recording **out, qa_error *error)
{
    frontend_network_q2_recording *owner = out ? *out : NULL;
    if (!owner) return true;
    if (owner->calls || any_feed(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 recording owner still retains an entered or attached feed");
    qa_application_events_unsubscribe(&owner->events);
    for (unsigned i = 0; i < 2; ++i) {
        feed_state_clear(owner->feeds + i);
        qa_q2_messages_destroy(owner->decoders[i]);
    }
    qa_unified_frame_lease_release(owner->header_storage);
    qa_unified_frame_lease *storage = owner->storage;
    *out = NULL;
    qa_unified_frame_lease_release(storage);
    return true;
}
