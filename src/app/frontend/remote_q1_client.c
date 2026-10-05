#include "remote_q1_private.h"
#include "internal.h"
#include "remote_q1_prediction.h"
#include "remote_q1_effects.h"
#include "remote_q1_skins.h"
#include "remote_unified_q1.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool remote_q1_fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
bool remote_q1_string(char **out, const char *text, qa_error *error)
{
    if (!text) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 service string is absent");
    size_t size = strlen(text) + 1;
    char *value = malloc(size);
    if (!value) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining Q1 service string");
    memcpy(value, text, size); free(*out); *out = value; return true;
}
void remote_q1_demo_clear(frontend_remote_q1 *row)
{
    while (row->demo_seed) {
        remote_q1_demo_seed *next = row->demo_seed->next;
        qa_buffer_free(&row->demo_seed->bytes); free(row->demo_seed); row->demo_seed = next;
    }
    row->demo_seed_last = NULL; row->demo_seed_bytes = 0; row->demo_seed_complete = false;
}
static bool demo_packet(frontend_remote_q1 *row, const frontend_demo_sink *sink,
    qa_bytes bytes, qa_bytes prefix, uint32_t sequence, uint32_t acknowledged, float seconds, qa_error *error)
{
    frontend_demo_packet packet = {.format = qa_q1_is_qw(row->protocol) ? FRONTEND_DEMO_QW : FRONTEND_DEMO_NQ};
    if (packet.format == FRONTEND_DEMO_NQ) {
        packet.value.nq.message = bytes;
        packet.value.nq.angles[0] = row->view_angles.x; packet.value.nq.angles[1] = row->view_angles.y;
        packet.value.nq.angles[2] = row->view_angles.z;
        return sink->append(sink->owner, &packet, error);
    }
    if (bytes.size > 1450 - 8) return remote_q1_fail(error, QA_ERROR_FORMAT, "QWD source packet exceeds its native framing");
    uint8_t data[1450]; qa_net_writer writer; qa_net_writer_init(&writer, data, sizeof(data), error);
    if (!(prefix.size ? (prefix.size == 8 && qa_net_write_data(&writer, prefix.data, prefix.size)) :
            qa_net_write_u32(&writer, sequence) && qa_net_write_u32(&writer, acknowledged)) ||
        !qa_net_write_data(&writer, bytes.data, bytes.size)) return false;
    packet.value.qw = (qa_qw_demo_record){.seconds = seconds, .kind = QA_QW_DEMO_PACKET,
        .data.packet = {data, qa_net_writer_size(&writer)}};
    return sink->append(sink->owner, &packet, error);
}
bool remote_q1_demo_batch(frontend_remote_q1 *row, qa_bytes bytes, qa_bytes prefix, uint32_t sequence,
    uint32_t acknowledged, uint64_t received, qa_error *error)
{
    (void)error;
    if (!row->demo_seed_complete) {
        remote_q1_demo_seed *held = NULL;
        if ((!qa_q1_is_qw(row->protocol) || prefix.size == 8) &&
            bytes.size <= 64u * 1024u * 1024u - row->demo_seed_bytes) held = calloc(1, sizeof(*held));
        if (held && bytes.size) held->bytes.data = malloc(bytes.size);
        if (!held || (bytes.size && !held->bytes.data)) {
            free(held); remote_q1_demo_clear(row); row->demo_seed_complete = true;
        } else {
            if (bytes.size) memcpy(held->bytes.data, bytes.data, bytes.size);
            if (prefix.size == 8) memcpy(held->wire_prefix, prefix.data, prefix.size);
            held->bytes.size = bytes.size; held->sequence = sequence; held->acknowledged = acknowledged;
            if (row->demo_seed_last) row->demo_seed_last->next = held; else row->demo_seed = held;
            row->demo_seed_last = held; row->demo_seed_bytes += bytes.size;
            qa_network_q1_client_state state;
            row->demo_seed_complete = qa_network_q1_client_state_read(row->options.domain.runtime,
                row->options.domain.client, &state, NULL) && state.active;
        }
    }
    if (row->demo_sink.append) {
        qa_error recording = {0};
        float seconds = (float)((double)(received >= row->demo_record_start ? received - row->demo_record_start : 0) / 1e9);
        (void)demo_packet(row, &row->demo_sink, bytes, prefix, sequence, acknowledged, seconds, &recording);
    }
    return true;
}
bool frontend_remote_q1_demo_attach(frontend_remote_q1 *row, const frontend_demo_sink *sink,
    bool *attached, qa_error *error)
{
    if (!attached || !sink || !sink->owner || !sink->append || !row || row->busy ||
        row->demo_sink.append || !remote_q1_live(row, error)) return false;
    row->demo_sink = *sink; row->demo_record_start = row->received_ns; *attached = true; return true;
}
bool frontend_remote_q1_demo_detach(frontend_remote_q1 *row, const frontend_demo_sink *sink, qa_error *error)
{
    if (!row || !sink || row->busy) return false;
    if (!row->demo_sink.append) return true;
    if (row->demo_sink.owner != sink->owner || row->demo_sink.append != sink->append)
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 recording detach differs from its retained sink");
    row->demo_sink = (frontend_demo_sink){0}; return true;
}
bool frontend_remote_q1_demo_angles(frontend_remote_q1 *row, const float angles[3], qa_error *error)
{
    if (!row || !angles || row->busy || !remote_q1_live(row, error) ||
        !isfinite(angles[0]) || !isfinite(angles[1]) || !isfinite(angles[2]) || row->revision == UINT64_MAX) return false;
    row->view_angles = qa_v3(angles[0], angles[1], angles[2]); ++row->revision; return true;
}
static bool demo_nq_message(frontend_remote_q1 *row, const frontend_demo_sink *sink,
    const qa_nq_message *message, qa_nq_options options, const qa_nq_decoder *decoder, qa_error *error)
{
    uint8_t data[64000]; qa_net_writer writer; qa_net_writer_init(&writer, data, sizeof(data), error);
    const qa_q1_entity *baseline = message->op == QA_NQ_ENTITY ? qa_nq_decoder_baseline(decoder, message->data.entity.number) : NULL;
    return qa_nq_write(&writer, row->protocol, options, message, baseline, (float)row->seconds) &&
        demo_packet(row, sink, (qa_bytes){data, qa_net_writer_size(&writer)}, (qa_bytes){0}, 0, 0, 0, error);
}
static bool demo_qw_message(frontend_remote_q1 *row, const frontend_demo_sink *sink,
    const qa_qw_service *message, const qa_qw_decoder *decoder, uint32_t incoming, uint32_t outgoing, qa_error *error)
{
    uint8_t data[1450 - 8]; qa_net_writer writer; qa_net_writer_init(&writer, data, sizeof(data), error);
    return qa_qw_service_write(&writer, row->protocol, message, decoder) &&
        demo_packet(row, sink, (qa_bytes){data, qa_net_writer_size(&writer)}, (qa_bytes){0}, incoming,
            outgoing ? outgoing - 1 : 0, 0, error);
}
bool frontend_remote_q1_demo_seed(frontend_remote_q1 *row, const frontend_demo_sink *sink,
    qa_nq_options options, qa_error *error)
{
    if (!row || !sink || !sink->owner || !sink->append || row->busy || !remote_q1_live(row, error) ||
        !row->demo_seed || !row->demo_seed_complete)
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Recording requires the actual received Q1 signon seed");
    uint32_t outgoing = 0, incoming = 0;
    const bool qw = qa_q1_is_qw(row->protocol);
    if (qw) {
        if (!qa_network_q1_client_sequences(row->options.domain.runtime, row->options.domain.client,
            &outgoing, &incoming, error)) return false;
        frontend_demo_packet packet = {.format = FRONTEND_DEMO_QW, .value.qw = {
            .kind = QA_QW_DEMO_SEQUENCES, .data.sequences = {outgoing, incoming}}};
        if (!sink->append(sink->owner, &packet, error)) return false;
    }
    for (const remote_q1_demo_seed *seed = row->demo_seed; seed; seed = seed->next)
        if (!demo_packet(row, sink, (qa_bytes){seed->bytes.data, seed->bytes.size},
            qw ? (qa_bytes){seed->wire_prefix, sizeof(seed->wire_prefix)} : (qa_bytes){0},
            seed->sequence, seed->acknowledged, 0, error)) return false;
    if (qw) {
        const qa_qw_decoder *decoder = qa_network_q1_client_qw_decoder(row->options.domain.runtime, row->options.domain.client);
        qa_network_q1_client_state state;
        if (!decoder || !qa_network_q1_client_state_read(row->options.domain.runtime, row->options.domain.client, &state, error)) return false;
        const qa_qw_frame *frame = qa_qw_decoder_frame(decoder, state.last_frame);
        if (!frame) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "QWD seed lacks its actual complete entity frame");
        qa_qw_service service = {.kind = QA_QW_PACKET_ENTITIES, .data.packet = {.frame = *frame}};
        if (!demo_qw_message(row, sink, &service, decoder, incoming, outgoing, error)) return false;
        for (size_t i = 0; i < 32; ++i) if (row->qw_player_valid[i]) {
            service = (qa_qw_service){.kind = QA_QW_PLAYER, .data.player = row->qw_players[i]};
            if (!demo_qw_message(row, sink, &service, decoder, incoming, outgoing, error)) return false;
        }
        for (size_t i = 0; i < 256; ++i) {
            service = (qa_qw_service){.kind = QA_QW_STAT, .data.stat = {(uint8_t)i, row->stats[i]}};
            if (!demo_qw_message(row, sink, &service, decoder, incoming, outgoing, error)) return false;
        }
        for (size_t i = 0; i < 32; ++i) if (row->clients[i].userinfo) {
            qa_qw_info info = {0};
            if (!qa_qw_info_parse(row->clients[i].userinfo, &info, error)) return false;
            bool ok = true;
            for (size_t j = 0; ok && j < info.count; ++j) {
                service = (qa_qw_service){.kind = QA_QW_SET_INFO,
                    .data.info = {(uint8_t)i, info.rules[j].name, info.rules[j].value}};
                ok = demo_qw_message(row, sink, &service, decoder, incoming, outgoing, error);
            }
            qa_qw_info_free(&info); if (!ok) return false;
            service = (qa_qw_service){.kind = QA_QW_FRAGS, .data.score = {(uint8_t)i, (int16_t)row->clients[i].frags}};
            if (!demo_qw_message(row, sink, &service, decoder, incoming, outgoing, error)) return false;
        }
        for (size_t i = 0; i < 256; ++i) if (row->styles[i]) {
            service = (qa_qw_service){.kind = QA_QW_LIGHT_STYLE, .data.light_style = {(uint8_t)i, row->styles[i]}};
            if (!demo_qw_message(row, sink, &service, decoder, incoming, outgoing, error)) return false;
        }
    } else {
        const qa_nq_decoder *decoder = qa_network_q1_client_nq_decoder(row->options.domain.runtime, row->options.domain.client);
        if (!decoder) return false;
        qa_nq_message message = {.op = QA_NQ_TIME, .data.seconds = (float)row->seconds};
        if (!demo_nq_message(row, sink, &message, options, decoder, error)) return false;
        for (size_t i = 0; i < row->current.count; ++i) {
            message = (qa_nq_message){.op = QA_NQ_ENTITY, .data.entity = row->current.rows[i]};
            if (!demo_nq_message(row, sink, &message, options, decoder, error)) return false;
        }
        message = (qa_nq_message){.op = QA_NQ_CLIENTDATA, .data.clientdata = row->data};
        if (!demo_nq_message(row, sink, &message, options, decoder, error)) return false;
        for (size_t i = 0; i < 256; ++i) if (row->styles[i]) {
            message = (qa_nq_message){.op = QA_NQ_LIGHTSTYLE, .data.indexed_text = {(uint8_t)i, row->styles[i]}};
            if (!demo_nq_message(row, sink, &message, options, decoder, error)) return false;
        }
        for (size_t i = 0; i < row->max_clients; ++i) if (row->clients[i].present) {
            message = (qa_nq_message){.op = QA_NQ_NAME, .data.indexed_text = {(uint8_t)i, row->clients[i].name ? row->clients[i].name : ""}};
            if (!demo_nq_message(row, sink, &message, options, decoder, error)) return false;
            message = (qa_nq_message){.op = QA_NQ_FRAGS, .data.indexed = {(uint8_t)i, row->clients[i].frags}};
            if (!demo_nq_message(row, sink, &message, options, decoder, error)) return false;
            message = (qa_nq_message){.op = QA_NQ_COLORS, .data.indexed = {(uint8_t)i, row->clients[i].colors}};
            if (!demo_nq_message(row, sink, &message, options, decoder, error)) return false;
        }
    }
    return remote_q1_live(row, error);
}
bool remote_q1_domain_equal(const frontend_remote_q1_domain *a, const frontend_remote_q1_domain *b)
{
    return a && b && a->application == b->application && a->runtime == b->runtime &&
        qa_net_client_id_equal(a->client, b->client) && a->seat.owner == b->seat.owner && a->seat.index == b->seat.index &&
        a->epoch == b->epoch && a->configuration_generation == b->configuration_generation &&
        a->physical_seat == b->physical_seat && a->protocol.kind == b->protocol.kind &&
        a->protocol.revision == b->protocol.revision && a->protocol.flags == b->protocol.flags &&
        a->catalog == b->catalog && a->product == b->product && a->console == b->console && a->cvars == b->cvars &&
        a->actors == b->actors && a->actor_owner == b->actor_owner && a->actor_definition == b->actor_definition &&
        a->command_context.session == b->command_context.session && a->command_context.owner == b->command_context.owner &&
        a->command_context.client == b->command_context.client && a->command_context.seat == b->command_context.seat &&
        a->command_context.registry == b->command_context.registry && a->command_context.generation == b->command_context.generation &&
        a->command_context.dialect == b->command_context.dialect &&
        a->command_context.origin == b->command_context.origin && a->command_context.direct == b->command_context.direct &&
        a->command_context.console_text == b->command_context.console_text && a->command_context.script == b->command_context.script &&
        qa_actor_id_equal(a->command_context.actor, b->command_context.actor);
}
bool remote_q1_live(const frontend_remote_q1 *row, qa_error *error)
{
    if (!row || !row->bound || row->retired || row->frontend->application != row->options.domain.application ||
        qa_network_epoch(row->options.domain.runtime, row->options.domain.client) != row->options.domain.epoch)
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 presentation lost its actual CLIENT session");
    return row->options.current(row->options.context, &row->options.domain, error);
}
bool remote_q1_mutable(const frontend_remote_q1 *row)
{ return row && !row->sky_policy && !row->frontend->capture && !row->frontend->resource_inventory && !row->frontend->source_restoring; }
static bool grow(void **data, size_t *capacity, size_t count, size_t stride, qa_error *error)
{
    if (count <= *capacity) return true;
    size_t next = *capacity ? *capacity : 16;
    while (next < count) { if (next > SIZE_MAX / 2) { next = count; break; } next *= 2; }
    if (next > SIZE_MAX / stride) return remote_q1_fail(error, QA_ERROR_MEMORY, "Q1 presentation array exceeds storage");
    void *value = realloc(*data, next * stride);
    if (!value) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining Q1 presentation array");
    *data = value; *capacity = next; return true;
}
bool remote_q1_entity_set(remote_q1_entities *table, const qa_q1_entity *entity, qa_error *error)
{
    for (size_t i = 0; i < table->count; ++i) if (table->rows[i].number == entity->number) {
        table->rows[i] = *entity; return true;
    }
    if (!grow((void **)&table->rows, &table->capacity, table->count + 1, sizeof(*table->rows), error)) return false;
    table->rows[table->count++] = *entity; return true;
}
bool remote_q1_actor_read(frontend_remote_q1 *row, uint32_t number, qa_actor_id *out, qa_error *error)
{
    for (size_t i = 0; i < row->actor_count; ++i) if (row->actors[i].number == number) {
        const qa_actor_record *record = qa_actors_get(row->options.domain.actors, row->actors[i].id);
        if (!record || record->owner != row->options.domain.actor_owner ||
            record->definition != row->options.domain.actor_definition || !record->has_source || record->source_slot != number)
            return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 presentation actor generation retired");
        *out = record->id; return true;
    }
    if (row->actor_count >= 65536) return remote_q1_fail(error, QA_ERROR_FORMAT, "Remote Q1 actor cache is full");
    if (!grow((void **)&row->actors, &row->actor_capacity, row->actor_count + 1, sizeof(*row->actors), error)) return false;
    qa_application_client_source source; qa_actor_id id;
    if (!frontend_remote_q1_application_read(row, &source, error) ||
        !qa_application_client_entity_read(row->options.domain.application, &source, number, &id, error)) return false;
    row->actors[row->actor_count++] = (remote_q1_actor){number, id}; *out = id; return true;
}
static void names_free(char ***names, size_t *count)
{ for (size_t i = 0; i < *count; ++i) free((*names)[i]); free(*names); *names = NULL; *count = 0; }
static bool names_copy(char ***out, size_t *count, const char *const *names, size_t size, qa_error *error)
{
    if (size && !names) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 precache names are absent");
    if (size > SIZE_MAX / sizeof(char *)) return false;
    char **copy = size ? calloc(size, sizeof(*copy)) : NULL;
    if (size && !copy) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining Q1 precache list");
    for (size_t i = 0; i < size; ++i) if (!remote_q1_string(&copy[i], names[i], error)) {
        size_t partial = size; names_free(&copy, &partial); return false;
    }
    names_free(out, count); *out = copy; *count = size; return true;
}
void remote_q1_clear(frontend_remote_q1 *row)
{
    if (!qa_q1_is_qw(row->options.domain.protocol)) remote_q1_demo_clear(row);
    row->actor_count = 0;
    remote_q1_camera_reset(row);
    row->view_motion = (frontend_q1_view_motion){0};
    row->view_entity_pose_number = 0;
    row->view_pose_ready = false;
    remote_q1_prediction_clear(row);
    remote_q1_media_clear(row);
    remote_q1_qw_queue_clear(row);
    names_free(&row->models, &row->model_count); names_free(&row->sounds, &row->sound_count);
    free(row->sound_available); row->sound_available = NULL;
    for (size_t i = 0; i < 256; ++i) { free(row->styles[i]); row->styles[i] = NULL; }
    for (size_t i = 0; i < 256; ++i) {
        free(row->clients[i].name); free(row->clients[i].social); free(row->clients[i].player_info); free(row->clients[i].userinfo);
        row->clients[i] = (remote_q1_client){0};
    }
    free(row->skybox); row->skybox = NULL;
    row->current.count = row->previous.count = row->statics.count = 0;
    row->qw_entities.count = row->qw_nails.count = row->qw_batch_players.count = 0;
    memset(row->qw_player_valid, 0, sizeof(row->qw_player_valid)); memset(row->stats, 0, sizeof(row->stats));
    row->max_clients = row->view_entity = 0; row->view_angles = qa_v3(0, 0, 0);
    row->pending_impulse = 0;
    row->has_data = row->loaded = row->qw_ready = row->qw_frame = row->qw_intermission = row->intermission = row->published = false;
    row->seconds = row->previous_seconds = 0; row->fraction = 1; row->qw_kick = 0;
    qa_resource_release(row->map); row->map = NULL; qa_vfs_acquisition_dispose(&row->map_opening);
    qa_vfs_destroy(row->content.mounts); qa_catalog_release(row->content.catalog); row->content = (frontend_remote_q1_content){0};
}
bool frontend_remote_q1_create(qa_frontend *f, const frontend_remote_q1_options *options,
    frontend_remote_q1 **out, qa_error *error)
{
    const frontend_remote_q1_domain *d = options ? &options->domain : NULL;
    const qa_product *product = d ? qa_catalog_product(d->catalog, d->product) : NULL;
    if (!f || f->capture || f->resource_inventory || f->source_restoring || !options || !out || *out ||
        !options->current || !options->load_content || !options->service || !options->disconnected ||
        !d->application || f->application != d->application || !d->runtime || !d->catalog || !product || product->family != QA_GAME_Q1 ||
        !d->console || !d->cvars || !d->actors || !d->actor_owner ||
        d->physical_seat >= f->options.seats || !f->seats || !f->seats[d->physical_seat].input ||
        d->client.owner || d->client.generation || d->client.slot || d->epoch || !qa_q1_profile_valid(d->protocol, error) ||
        (qa_q1_is_qw(d->protocol) && !options->skin_bindings))
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 construction requires its pending canonical CLIENT source");
    if (!options->current(options->context, d, error)) return false;
    frontend_remote_q1 *row = calloc(1, sizeof(*row));
    if (!row) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining remote Q1 presentation");
    row->frontend = f; row->options = *options; row->protocol = d->protocol;
    row->fraction = 1; row->revision = row->next_event = 1;
    qa_catalog_retain(d->catalog);
    frontend_remote_q1 **tail = &f->remote_q1; while (*tail) tail = &(*tail)->next;
    *tail = row; *out = row;
    return !qa_q1_is_qw(d->protocol) || frontend_remote_q1_skins_create(row,options->skin_bindings,&row->skins,error);
}
bool frontend_remote_q1_bind(frontend_remote_q1 *row, const frontend_remote_q1_domain *actual, qa_error *error)
{
    if (!remote_q1_mutable(row) || !actual || row->bound || row->busy || row->retired || !actual->client.owner || !actual->client.generation || !actual->epoch)
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 bind requires the actual successful CLIENT attach result");
    frontend_remote_q1_domain expected = row->options.domain;
    expected.client = actual->client; expected.seat = actual->seat; expected.epoch = actual->epoch;
    if (!remote_q1_domain_equal(&expected, actual) || qa_network_epoch(actual->runtime, actual->client) != actual->epoch ||
        !row->options.current(row->options.context, actual, error)) return false;
    row->options.domain = *actual; row->bound = true; return true;
}
static bool serverinfo(frontend_remote_q1 *row, const qa_nq_serverinfo *info, qa_error *error)
{
    const char *map = info->model_count && info->models ? info->models[0] : NULL;
    size_t length = map ? strlen(map) : 0;
    if (!map || length < 9 || strncmp(map, "maps/", 5) || strcmp(map + length - 4, ".bsp") ||
        row->map_generation == UINT64_MAX)
        return remote_q1_fail(error, QA_ERROR_FORMAT, "Remote Q1 server supplied no valid native world and scoreboard");
    frontend_remote_q1_content content = {0};
    if (!row->options.load_content(row->options.context, &row->options.domain, info,
        qa_q1_is_qw(row->options.domain.protocol) ? &row->qw : NULL, &content, error) || !remote_q1_live(row, error)) return false;
    const qa_product *product = qa_catalog_product(content.catalog, content.product);
    if (!product || product->family != QA_GAME_Q1 || !content.mounts ||
        !qa_catalog_product_view_current(content.catalog, content.product, content.mounts))
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Remote Q1 loading lost its genuine received-content catalog");
    qa_vfs *held = qa_vfs_clone(content.mounts, error);
    if (!held) return false;
    qa_catalog_retain(content.catalog);
    if (!remote_q1_effects_clear(row, error)) {
        qa_vfs_destroy(held); qa_catalog_release(content.catalog); return false;
    }
    if (row->skins && !frontend_remote_q1_skins_reset(row->skins,error)) { qa_vfs_destroy(held); qa_catalog_release(content.catalog); return false; }
    remote_q1_clear(row); content.mounts = held; row->content = content;
    if (row->skins) {
        const qa_product *base=qa_catalog_find(content.catalog,"q1-quakeworld");
        qa_fs_root *root=base?qa_catalog_product_write_root(content.catalog,base->id):NULL;
        if (!root || !frontend_remote_q1_skins_content(row->skins,held,root,error)) return false;
    }
    if (!names_copy(&row->models, &row->model_count, info->models, info->model_count, error) ||
        !names_copy(&row->sounds, &row->sound_count, info->sounds, info->sound_count, error)) return false;
    if (!qa_vfs_acquire_receipt(row->content.mounts, map, &row->map, &row->map_opening, error)) return false;
    if (!remote_q1_media_prepare(row, error) || !remote_q1_string(&row->level_name, info->level, error)) return false;
    if (qa_q1_is_qw(row->options.domain.protocol)) row->qw.level = row->level_name;
    row->max_clients = info->max_clients; row->loaded = true; ++row->map_generation; return true;
}
void remote_q1_time_advance(frontend_remote_q1 *row, double seconds, uint64_t received)
{
    row->previous_seconds = row->seconds; row->seconds = seconds;
    remote_q1_entities spare = row->previous; row->previous = row->current; row->current = spare; row->current.count = 0;
    row->received_ns = received; row->fraction = 1; ++row->frame_number;
}
static bool trim_space(unsigned char byte)
{ return byte==' ' || (byte>=9 && byte<=13) || byte==160; }
static bool reconnect(frontend_remote_q1 *row,const char *text,qa_error *error)
{
    if(!text) return remote_q1_fail(error,QA_ERROR_FORMAT,"Received Q1 server command has no text");
    size_t length=strlen(text);
    for(size_t at=0;at<length;) {
        size_t count=qa_command_separator(text+at,length-at,QA_CONSOLE_Q1),first=0,last=count;
        while(first<last && trim_space((unsigned char)text[at+first])) ++first;
        while(last>first && trim_space((unsigned char)text[at+last-1])) --last;
        if(last-first==9 && !memcmp(text+at+first,"reconnect",9)) row->published=false;
        at+=count+1;
    }
    return true;
}
void remote_q1_publication_update(frontend_remote_q1 *row)
{
    if(!row->loaded || !row->view_entity || !row->has_data) return;
    for(size_t i=0;i<row->current.count;++i)
        if(row->current.rows[i].number==row->view_entity) { row->published=true; return; }
}
bool frontend_remote_q1_receive_nq(frontend_remote_q1 *row, const qa_nq_message *message, uint64_t received, qa_error *error)
{
    if (!message || !remote_q1_mutable(row) || row->busy || !remote_q1_live(row, error) || !row->revision || !row->next_event ||
        row->revision == UINT64_MAX || row->next_event == UINT64_MAX ||
        (message->op == QA_NQ_TIME && row->frame_number == UINT64_MAX))
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 service requires its actual returned CLIENT receiver");
    if (message->op == QA_NQ_DAMAGE && !remote_q1_view_damage(row, message, error)) return false;
    ++row->busy; bool ok = true;
    switch (message->op) {
    case QA_NQ_SERVERINFO: ok = serverinfo(row, &message->data.serverinfo, error); break;
    case QA_NQ_TIME: remote_q1_time_advance(row, message->data.seconds, received); break;
    case QA_NQ_ENTITY: ok = remote_q1_entity_set(&row->current, &message->data.entity, error); break;
    case QA_NQ_STATIC: {
        qa_q1_entity entity = message->data.entity;
        if (row->statics.count > UINT32_MAX - 65536u) { ok = false; break; }
        entity.number = 65536u + (uint32_t)row->statics.count;
        ok = remote_q1_entity_set(&row->statics, &entity, error); break;
    }
    case QA_NQ_SETVIEW: row->view_entity = message->data.value; break;
    case QA_NQ_SETANGLE: row->view_angles = qa_v3(message->data.angles[0], message->data.angles[1], message->data.angles[2]); break;
    case QA_NQ_CLIENTDATA: row->data = message->data.clientdata; row->has_data = true; break;
    case QA_NQ_STAT: row->stats[message->data.indexed.index] = message->data.indexed.value; break;
    case QA_NQ_KILLEDMONSTER: ++row->stats[14]; break;
    case QA_NQ_FOUNDSECRET: ++row->stats[13]; break;
    case QA_NQ_LIGHTSTYLE: ok = remote_q1_string(&row->styles[message->data.indexed_text.index], message->data.indexed_text.text, error); break;
    case QA_NQ_SKYBOX: ok = remote_q1_string(&row->skybox, message->data.text, error) && remote_q1_sky_load(row, error); break;
    case QA_NQ_NAME: case QA_NQ_SOCIAL: case QA_NQ_PLAYERINFO: {
        uint32_t index = message->data.indexed_text.index;
        remote_q1_client *client = row->clients + index;
        if (message->op == QA_NQ_NAME) ok = remote_q1_string(&client->name, message->data.indexed_text.text, error);
        else if (message->op == QA_NQ_SOCIAL) { ok = remote_q1_string(&client->social, message->data.indexed_text.text, error); if (ok) client->has_social = true; }
        else { ok = remote_q1_string(&client->player_info, message->data.indexed_text.text, error); if (ok) client->has_player_info = true; }
        if (ok) client->present = true;
        break;
    }
    case QA_NQ_COLORS: case QA_NQ_FRAGS: case QA_NQ_PING: {
        uint32_t index = message->data.indexed.index;
        if (message->op == QA_NQ_COLORS) row->clients[index].colors = (uint8_t)message->data.indexed.value;
        else if (message->op == QA_NQ_FRAGS) row->clients[index].frags = message->data.indexed.value;
        else { row->clients[index].ping = message->data.indexed.value; row->clients[index].has_ping = true; }
        row->clients[index].present = true;
        break;
    }
    case QA_NQ_INTERMISSION: case QA_NQ_FINALE: case QA_NQ_CUTSCENE: row->intermission = true; break;
    case QA_NQ_STUFFTEXT:
        ok=reconnect(row,message->data.text,error) &&
            frontend_view_q1_bonus_commands(&row->view_motion,message->data.text,error); break;
    case QA_NQ_BONUSFLASH: frontend_view_q1_bonus(&row->view_motion); break;
    default: break;
    }
    qa_nq_message presented;
    if (message->op == QA_NQ_CDTRACK && row->options.sample_seconds &&
        row->options.demo_forced_track >= 0 && row->options.demo_forced_track <= UINT8_MAX) {
        presented = *message;
        presented.data.cd.track = presented.data.cd.loop = (uint8_t)row->options.demo_forced_track;
        message = &presented;
    }
    if (ok) ok = remote_q1_effects_service(row, message, error);
    if (ok) { ++row->revision; ok = row->options.service(row->options.context, &row->options.domain,
        row->protocol, message, row->seconds, row->next_event++, error); }
    --row->busy; return ok && remote_q1_live(row, error);
}
bool frontend_remote_q1_bonus(frontend_remote_q1 *row, qa_error *error)
{
    if (!remote_q1_mutable(row) || row->busy || !remote_q1_live(row, error))
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 bonus requires its returned actual CLIENT receiver");
    frontend_view_q1_bonus(&row->view_motion);
    return remote_q1_live(row, error);
}
bool frontend_remote_q1_sample(frontend_remote_q1 *row, uint64_t now, qa_error *error)
{
    if (!remote_q1_mutable(row) || row->busy || !remote_q1_live(row, error)) return false;
    if (row->revision == UINT64_MAX) return remote_q1_fail(error, QA_ERROR_FORMAT, "Q1 presentation revision is exhausted");
    if (row->skins && !frontend_remote_q1_skins_prepare(row->skins,error)) return false;
    if (row->seconds - row->previous_seconds > .1) row->previous_seconds = row->seconds - .1;
    double duration = fmax(0, row->seconds - row->previous_seconds);
    if (row->options.sample_seconds) {
        double seconds;
        if (!row->options.sample_seconds(row->options.context, &row->options.domain, &seconds, error) || !isfinite(seconds)) return false;
        row->fraction = duration == 0 ? 1 : fmin(1, fmax(0, (seconds - row->previous_seconds) / duration));
    } else row->fraction = duration == 0 ? 1 : fmin(1, fmax(0, now >= row->received_ns ?
        (double)(now - row->received_ns) / (duration * 1000000000.0) : 0));
    remote_q1_publication_update(row); ++row->revision;
    return remote_q1_view_sample(row, error);
}
bool frontend_remote_q1_metadata_read(const frontend_remote_q1 *row, frontend_remote_q1_view *out, qa_error *error)
{
    if (!row || !out || row->busy) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 metadata requires returned receiver callbacks");
    qa_actor_id viewer = {0};
    for (size_t i = 0; i < row->actor_count; ++i) if (row->actors[i].number == row->view_entity) { viewer = row->actors[i].id; break; }
    *out = (frontend_remote_q1_view){row, row->options.domain, row->content, row->revision, row->map_generation,
        row->received_ns, row->frame_number, row->seconds, row->previous_seconds,
        row->previous_seconds + (row->seconds - row->previous_seconds) * row->fraction, row->fraction,
        row->view_entity, row->max_clients, viewer, row->view_angles, row->has_data ? &row->data : NULL,
        row->skybox ? row->skybox : "", row->bound, row->loaded, row->retired, row->map, &row->map_opening,
        row->images, row->materials, row->world, row->protocol, row->published, row->level_name, row->stats}; return true;
}
bool frontend_remote_q1_read(const frontend_remote_q1 *row, frontend_remote_q1_view *out, qa_error *error)
{ return remote_q1_live(row, error) && frontend_remote_q1_metadata_read(row, out, error); }
bool frontend_q1_save_client_read(const qa_frontend *f,uint32_t physical_seat,
    qa_q1_save_client *out,qa_error *error)
{
    if (!f || !out || physical_seat>=f->options.seats)
        return remote_q1_fail(error,QA_ERROR_ARGUMENT,"Save comment requires its actual local CLIENT");
    for (size_t i=0;i<frontend_remote_unified_count(f);++i) {
        frontend_remote_unified *replica=frontend_remote_unified_at(f,i);
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(replica);
        if (frontend_remote_unified_retired(replica) || !domain || domain->physical_seat!=physical_seat) continue;
        const qa_unified_q1_world_state *world=frontend_unified_q1_world_read(replica);
        if (!world) return remote_q1_fail(error,QA_ERROR_ARGUMENT,"Save comment requires received Q1 CLIENT world metadata");
        *out=(qa_q1_save_client){world->level,world->killed_monsters,world->total_monsters};
        return true;
    }
    for (frontend_remote_q1 *row=f->remote_q1;row;row=row->next) {
        if (row->retired || row->options.domain.physical_seat!=physical_seat) continue;
        frontend_remote_q1_view view;
        if (!frontend_remote_q1_read(row,&view,error)) return false;
        if (!view.loaded || !view.level_name)
            return remote_q1_fail(error,QA_ERROR_ARGUMENT,"Save comment requires received CLIENT serverinfo");
        *out=(qa_q1_save_client){view.level_name,view.stats[14],view.stats[12]};
        return true;
    }
    return remote_q1_fail(error,QA_ERROR_ARGUMENT,"Save comment has no received local CLIENT");
}
bool frontend_remote_q1_application_read(const frontend_remote_q1 *row,qa_application_client_source *out,qa_error *error)
{
    return row && out && row->options.application_read && remote_q1_live(row,error) &&
        row->options.application_read(row->options.context,&row->options.domain,out,error) && remote_q1_live(row,error);
}
bool frontend_remote_q1_application_metadata_read(const frontend_remote_q1 *row,qa_application_client_source *out,qa_error *error)
{
    return row && out && row->frontend->application==row->options.domain.application &&
        row->options.application_metadata_read &&
        row->options.application_metadata_read(row->options.context,&row->options.domain,out,error);
}
bool frontend_remote_q1_current(const frontend_remote_q1_view *view)
{
    qa_error error = {0}; const frontend_remote_q1 *row = view ? view->owner : NULL;
    return row && !row->busy && remote_q1_live(row, &error) && remote_q1_domain_equal(&row->options.domain, &view->domain) &&
        view->revision == row->revision && view->map_generation == row->map_generation && view->received_ns == row->received_ns &&
        view->protocol.kind == row->protocol.kind && view->protocol.revision == row->protocol.revision && view->protocol.flags == row->protocol.flags &&
        view->content.catalog == row->content.catalog && view->content.mounts == row->content.mounts &&
        view->loaded == row->loaded && view->retired == row->retired && view->published == row->published;
}
bool frontend_remote_q1_idle(const frontend_remote_q1 *row)
{ return row && !row->busy && !row->sky_policy && remote_q1_effects_idle(row) &&
    (!row->skins || frontend_remote_q1_skins_idle(row->skins)); }
struct frontend_remote_q1_skins *frontend_remote_q1_skins_owner(const frontend_remote_q1 *row)
{ return row ? row->skins : NULL; }
bool frontend_remote_q1_destroy(frontend_remote_q1 **owned, qa_error *error)
{
    frontend_remote_q1 *row = owned ? *owned : NULL; if (!row) return true;
    if (row->busy || row->sky_policy || row->demo_sink.append || row->frontend->capture || row->frontend->resource_inventory)
        return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 presentation destruction overlaps its actual parent lease");
    if (!frontend_remote_q1_skins_destroy(&row->skins,error)) return false;
    frontend_remote_q1 **link = &row->frontend->remote_q1;
    while (*link && *link != row) link = &(*link)->next;
    if (*link != row) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Q1 owner is outside its real frontend parent list");
    ++row->busy; bool ok = remote_q1_effects_clear(row, error); --row->busy;
    if (!ok) return false;
    remote_q1_clear(row); remote_q1_demo_clear(row);
    free(row->current.rows); free(row->previous.rows); free(row->statics.rows);
    free(row->qw_entities.rows); free(row->qw_nails.rows); free(row->qw_batch_players.rows); free(row->qw_pending); free(row->actors);
    *link = row->next;
    free(row->qw_directory); free(row->level_name); qa_catalog_release(row->options.domain.catalog); free(row); *owned = NULL; return true;
}
bool frontend_remote_q1_disconnected(frontend_remote_q1 *row, const char *reason, qa_error *error)
{
    if (!remote_q1_mutable(row) || !reason || row->busy || !row->bound) return false;
    if (row->retired) {
        ++row->busy;
        bool cleared = (!row->skins || frontend_remote_q1_skins_reset(row->skins,error)) &&
            remote_q1_effects_clear(row, error);
        --row->busy; return cleared;
    }
    ++row->busy;
    bool ok = row->options.disconnected(row->options.context, &row->options.domain, reason, error);
    if (ok) {
        row->retired = true; ++row->revision;
        ok = (!row->skins || frontend_remote_q1_skins_reset(row->skins,error)) && remote_q1_effects_clear(row,error);
    }
    --row->busy; return ok;
}
size_t frontend_remote_q1_count(const qa_frontend *f)
{ size_t count = 0; if (f) for (frontend_remote_q1 *r = f->remote_q1; r; r = r->next) ++count; return count; }
frontend_remote_q1 *frontend_remote_q1_at(const qa_frontend *f, size_t index)
{ frontend_remote_q1 *r = f ? f->remote_q1 : NULL; while (r && index) { r = r->next; --index; } return r; }
bool frontend_remote_q1_sample_all(qa_frontend *f, uint64_t now, qa_error *error)
{
    if (!f) return false;
    for (frontend_remote_q1 *r = f->remote_q1; r; r = r->next)
        if (r->bound && !r->retired && !frontend_remote_q1_sample(r, now, error)) return false;
    return true;
}
bool frontend_remote_q1_destroy_all(qa_frontend *f, qa_error *error)
{
    if (!f) return true;
    while (f->remote_q1) { frontend_remote_q1 *r = f->remote_q1; if (!frontend_remote_q1_destroy(&r, error)) return false; }
    return true;
}
