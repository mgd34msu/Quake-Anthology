#include "q1_client_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool same_protocol(qa_net_protocol_id a, qa_net_protocol_id b)
{ return a.kind == b.kind && a.revision == b.revision && a.flags == b.flags; }
static bool current(q1_runtime_client *c, qa_error *e)
{
    qa_network_peer *peer = qa_network_peer_get(c->runtime, c->id, e);
    const qa_net_client *client = qa_net_connections_get(c->runtime->connections, c->id);
    return (peer && peer->state == c && client && !c->retiring &&
        same_protocol(client->protocol, c->admitted_protocol) &&
        qa_net_address_equal(&client->endpoint, &c->native.remote, true)) ||
        qa_network_fail(e, "Q1 CLIENT lost its actual admitted connection");
}
bool q1_client_queue(q1_runtime_client *c, qa_bytes bytes, qa_error *e)
{
    if ((bytes.size && !bytes.data) || bytes.size > c->policy.message_bytes ||
        bytes.size > c->policy.queued_bytes - c->queued_bytes)
        return qa_network_fail(e, "Q1 CLIENT reliable FIFO exceeds its admitted capacity");
    if (!bytes.size) return true;
    q1_client_pending *p = calloc(1, sizeof(*p));
    if (p) p->bytes.data = malloc(bytes.size);
    if (!p || !p->bytes.data) {
        free(p); qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining Q1 CLIENT reliable service"); return false;
    }
    memcpy(p->bytes.data, bytes.data, bytes.size); p->bytes.size = bytes.size;
    if (c->last) c->last->next = p; else c->first = p;
    c->last = p; c->queued_bytes += bytes.size; return true;
}
static void queue_clear(q1_runtime_client *c)
{
    while (c->first) {
        q1_client_pending *next = c->first->next;
        qa_buffer_free(&c->first->bytes); free(c->first); c->first = next;
    }
    c->last = NULL; c->queued_bytes = 0;
}
void q1_client_batch_clear(q1_runtime_client *c)
{
    for (size_t i = 0; i < c->record_count; ++i) free(c->records[i].names);
    free(c->records); c->records = NULL; c->record_count = c->cursor = 0;
    qa_buffer_free(&c->payload); qa_buffer_free(&c->before_decoder);
    c->before_protocol = (qa_net_protocol_id){0}; c->held = false;
}
static bool names(q1_client_record *record, const char *const *a, size_t an,
    const char *const *b, size_t bn, qa_error *e)
{
    if (an > SIZE_MAX - bn || an + bn > SIZE_MAX / sizeof(*record->names))
        return qa_network_fail(e, "Q1 CLIENT precache pointer extent overflows");
    size_t count = an + bn;
    if (!count) return true;
    record->names = malloc(count * sizeof(*record->names));
    if (!record->names) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining Q1 CLIENT precache service"); return false; }
    if (an) memcpy(record->names, a, an * sizeof(*a));
    if (bn) memcpy(record->names + an, b, bn * sizeof(*b));
    return true;
}
bool q1_client_decode_batch(q1_runtime_client *c, qa_bytes bytes, uint32_t sequence,
    uint32_t acknowledged, uint64_t received, qa_error *e)
{
    if (c->held || bytes.size > c->policy.message_bytes || (bytes.size && !bytes.data))
        return qa_network_fail(e, "Q1 CLIENT batch exceeds its actual receiver boundary");
    bool qw = c->qw != NULL;
    c->before_protocol = c->protocol;
    if (!(qw ? qa_qw_decoder_checkpoint(c->qw, c->protocol, &c->before_decoder, e) :
        qa_nq_decoder_checkpoint(c->nq, c->protocol, c->policy.nq_options, &c->before_decoder, e))) return false;
    c->payload.data = bytes.size ? malloc(bytes.size) : NULL; c->payload.size = bytes.size;
    if (bytes.size && !c->payload.data) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining Q1 CLIENT complete delivery"); q1_client_batch_clear(c); return false;
    }
    if (bytes.size) memcpy(c->payload.data, bytes.data, bytes.size);
    qa_net_reader reader; qa_net_reader_init(&reader, (qa_bytes){c->payload.data, c->payload.size}, e);
    size_t capacity = 0;
    while (qa_net_reader_remaining(&reader)) {
        if (c->record_count == c->policy.service_limit) {
            qa_network_fail(e, "Q1 CLIENT batch exceeds its admitted service count"); goto fail;
        }
        if (c->record_count == capacity) {
            size_t next = capacity ? capacity * 2 : 8;
            if (next > c->policy.service_limit) next = c->policy.service_limit;
            q1_client_record *p = realloc(c->records, next * sizeof(*p));
            if (!p) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining Q1 CLIENT services"); goto fail; }
            c->records = p; capacity = next;
        }
        q1_client_record *record = &c->records[c->record_count++]; memset(record, 0, sizeof(*record));
        if (qw) {
            qa_qw_service *m = &record->service.qw;
            if (!qa_qw_service_read(&reader, c->qw, sequence, m)) goto fail;
            record->protocol = qa_qw_decoder_protocol(c->qw);
            if (!qa_q1_is_qw(record->protocol)) goto protocol_fail;
            if (m->kind == QA_QW_MODEL_LIST || m->kind == QA_QW_SOUND_LIST) {
                if (!names(record, m->data.list.names, m->data.list.count, NULL, 0, e)) goto fail;
                m->data.list.names = record->names;
            }
        } else {
            qa_nq_message *m = &record->service.nq;
            if (!qa_nq_read(c->nq, &reader, m)) goto fail;
            record->protocol = qa_nq_decoder_protocol(c->nq);
            if (qa_q1_is_qw(record->protocol)) goto protocol_fail;
            if (m->op == QA_NQ_SERVERINFO) {
                qa_nq_serverinfo *s = &m->data.serverinfo;
                if (!names(record, s->models, s->model_count, s->sounds, s->sound_count, e)) goto fail;
                s->models = record->names;
                s->sounds = s->sound_count ? record->names + s->model_count : NULL;
            }
        }
    }
    if (!qa_net_reader_finish(&reader)) goto fail;
    c->protocol = qw ? qa_qw_decoder_protocol(c->qw) : qa_nq_decoder_protocol(c->nq);
    c->sequence = sequence; c->acknowledged = acknowledged; c->received_ns = received;
    c->held = true; return true;
protocol_fail:
    qa_network_fail(e, "Received Q1 CLIENT dialect changes its admitted channel family");
fail:
    q1_client_batch_clear(c); return false;
}
static bool receive(void *context, qa_network_runtime *runtime, qa_net_client_id id,
    const qa_net_datagram *packet, qa_error *e)
{
    q1_runtime_client *c = context;
    if (c->held || c->busy) return qa_network_fail(e, "Q1 CLIENT has an unconsumed source batch");
    qa_q1_delivery delivery;
    if (!qa_q1_peer_receive(&c->native, &packet->from, packet->payload, packet->received_ns, &delivery, e)) return false;
    if (!delivery.present || c->retiring) return true;
    return qa_network_received(runtime, id, packet->received_ns, e) &&
        q1_client_decode_batch(c, delivery.payload, delivery.sequence, delivery.acknowledged, packet->received_ns, e);
}
static bool queue_next(q1_runtime_client *c, qa_error *e)
{
    if (!c->first || (c->qw ? qa_qw_channel_pending(c->native.channel.qw) :
        !qa_nq_channel_ready(c->native.channel.nq))) return true;
    q1_client_pending *p = c->first;
    if (!(c->qw ? qa_qw_channel_queue(c->native.channel.qw, (qa_bytes){p->bytes.data,p->bytes.size}, e) :
        qa_nq_channel_queue(c->native.channel.nq, (qa_bytes){p->bytes.data,p->bytes.size}, e))) return false;
    c->first = p->next; if (!c->first) c->last = NULL;
    c->queued_bytes -= p->bytes.size; qa_buffer_free(&p->bytes); free(p); return true;
}
static bool transmit(q1_runtime_client *c, qa_bytes bytes, uint64_t now, qa_error *e)
{
    qa_bytes packet;
    return (c->qw ? qa_qw_channel_transmit(c->native.channel.qw, bytes, now, false, &packet, e) :
        qa_nq_channel_unreliable(c->native.channel.nq, bytes, &packet, e)) &&
        qa_q1_peer_send(&c->native, packet, e);
}
static bool retire_client(q1_runtime_client *c, const char *reason, bool notify, qa_error *e)
{
    q1_client_retirement *r = &c->retirement;
    if (!r->reason) {
        if (!q1_client_retirement_start(r, reason, notify, c->policy.message_bytes + 10, e)) return false;
        c->retiring = true; c->active = false;
    }
    if (r->marked) return true;
    qa_network_peer *peer = qa_network_peer_get(c->runtime, c->id, e);
    const qa_net_client *client = qa_net_connections_get(c->runtime->connections, c->id);
    if (!peer || peer->state != c || !client || !same_protocol(client->protocol, c->admitted_protocol) ||
        !qa_net_address_equal(&client->endpoint, &c->native.remote, true))
        return qa_network_fail(e, "Q1 CLIENT retirement lost its actual connection");
    unsigned required = c->qw ? 3u : 1u;
    bool ok = true;
    while (ok && r->notify && r->transmissions < required) {
        if (!r->packet.size) {
            uint8_t data[6]; qa_net_writer writer; qa_bytes packet;
            qa_net_writer_init(&writer, data, sizeof(data), e);
            qa_q1_client_message message = {.op = c->qw ? QA_Q1_CLC_STRING : QA_Q1_CLC_DISCONNECT,
                .data.text = "drop"};
            ok = qa_q1_client_write(&writer, c->protocol, 0, &message) &&
                (c->qw ? qa_qw_channel_transmit(c->native.channel.qw,
                    (qa_bytes){data, qa_net_writer_size(&writer)}, c->runtime->now_ns, false, &packet, e) :
                    qa_nq_channel_unreliable(c->native.channel.nq,
                    (qa_bytes){data, qa_net_writer_size(&writer)}, &packet, e));
            if (ok && packet.size > r->capacity) ok = qa_network_fail(e, "Q1 CLIENT retirement packet exceeds its channel");
            if (ok) { memcpy(r->packet.data, packet.data, packet.size); r->packet.size = packet.size; }
        }
        if (ok) ok = qa_q1_peer_send(&c->native, (qa_bytes){r->packet.data, r->packet.size}, e);
        if (ok) { ++r->transmissions; r->packet.size = 0; }
    }
    if (ok) {
        ok = c->hooks.drop(c->hooks.context, c->id, r->reason, e);
        if (ok) r->marked = true;
    }
    return ok;
}
static bool flush(void *context, qa_network_runtime *runtime, qa_net_client_id id, uint64_t now, qa_error *e)
{
    q1_runtime_client *c = context; (void)runtime; (void)id;
    if (c->busy) return true;
    if (c->retiring) {
        bool previous = c->runtime->callback;
        c->runtime->callback = true; c->busy = true;
        bool ok = retire_client(c, "server disconnected", false, e);
        c->busy = false; c->runtime->callback = previous; return ok;
    }
    if (c->held) return true;
    if (!queue_next(c, e)) return false;
    if (!c->qw) {
        bool present; qa_bytes packet;
        if (!qa_nq_channel_next(c->native.channel.nq, now, &present, &packet, e) ||
            (present && !qa_q1_peer_send(&c->native, packet, e))) return false;
    }
    size_t consumed = 0;
    uint8_t *data = malloc(c->policy.message_bytes);
    if (!data) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding Q1 CLIENT input"); return false; }
    bool ok = true;
    while (ok && consumed < c->command_count &&
        (!c->qw || qa_qw_channel_can_send(c->native.channel.qw, now))) {
        qa_net_writer writer; qa_net_writer_init(&writer, data, c->policy.message_bytes, e);
        q1_client_move *move = &c->commands[consumed]; uint32_t sequence = c->moves;
        bool present = true;
        if (c->qw) {
            sequence = qa_qw_channel_get_stats(c->native.channel.qw).outgoing_sequence;
            uint32_t age = (sequence - c->last_frame) & INT32_MAX;
            if (c->has_delta && age >= QA_QW_UPDATE_BACKUP - 1) c->has_delta = false;
            const qa_qw_history_frame *oldest = qa_qw_history_get(&c->history, (sequence - 2) & INT32_MAX);
            const qa_qw_history_frame *previous = qa_qw_history_get(&c->history, (sequence - 1) & INT32_MAX);
            uint8_t loss;
            if (!current(c, e) || !c->hooks.qw_loss(c->hooks.context, c->id, sequence, &loss, e) || !current(c, e)) {
                ok = false; break;
            }
            qa_q1_client_message message = {0};
            message.op = QA_Q1_CLC_MOVE;
            message.data.qw_move.oldest = oldest ? oldest->command : (qa_qw_command){0};
            message.data.qw_move.previous = previous ? previous->command : (qa_qw_command){0};
            message.data.qw_move.current = move->command.qw;
            message.data.qw_move.loss = loss;
            ok = qa_q1_client_write(&writer, c->protocol, sequence, &message);
            if (ok && c->has_delta) {
                message = (qa_q1_client_message){.op = QA_Q1_CLC_DELTA, .data.delta = (uint8_t)c->last_frame};
                ok = qa_q1_client_write(&writer, c->protocol, sequence, &message);
            }
        } else ok = qa_nq_move_send(&c->moves, false, &writer, c->protocol, &move->command.nq, &present);
        if (ok && present) ok = transmit(c, (qa_bytes){data, qa_net_writer_size(&writer)}, now, e);
        if (ok && c->qw) {
            ok = qa_qw_history_record(&c->history, sequence, (double)now / 1e9, &move->command.qw, e);
            if (ok) {
                qa_qw_decoder_delta_request(c->qw, sequence, c->has_delta, c->last_frame);
            }
        }
        if (ok && present) ok = current(c, e) && c->hooks.sent(c->hooks.context, c->id, sequence,
            c->qw ? NULL : &move->command.nq, c->qw ? &move->command.qw : NULL, now, e) && current(c, e);
        if (ok) ++consumed;
    }
    free(data);
    if (consumed) {
        c->command_count -= consumed;
        memmove(c->commands, c->commands + consumed, c->command_count * sizeof(*c->commands));
    }
    if (ok && c->qw && !consumed && qa_qw_channel_pending(c->native.channel.qw) &&
        qa_qw_channel_can_send(c->native.channel.qw, now)) ok = transmit(c, (qa_bytes){0}, now, e);
    return ok;
}
static bool enqueue_move(q1_runtime_client *c, const qa_q1_command *nq, const qa_qw_command *qw, qa_error *e)
{
    if (!c->active || c->retiring || c->command_count == c->policy.pending_commands ||
        (c->qw ? !qw : !nq)) return qa_network_fail(e, "Q1 input lacks its active admitted Source or queue capacity");
    const float *angles = c->qw ? qw->angles : nq->angles;
    for (size_t i = 0; i < 3; ++i)
        if (!isfinite(angles[i])) return qa_network_fail(e, "Q1 input has a nonfinite Source angle");
    if (!c->qw && !isfinite(nq->time)) return qa_network_fail(e, "NQ input has a nonfinite Source time");
    if (c->qw) c->commands[c->command_count].command.qw = *qw;
    else c->commands[c->command_count].command.nq = *nq;
    ++c->command_count; return true;
}
static bool command(void *context, const qa_network_command *value, qa_error *e)
{
    q1_runtime_client *c = context; qa_q1_command nq; qa_qw_command qw;
    if (!c->active || c->command_count == c->policy.pending_commands || !current(c, e))
        return qa_network_fail(e, "Q1 command lacks its actual active Source or retained queue capacity");
    if (!(c->qw ? c->hooks.command_qw(c->hooks.context, value, c->runtime->now_ns, &qw, e) :
        c->hooks.command_nq(c->hooks.context, value, &nq, e)) || !current(c, e)) return false;
    if (c->qw && c->hooks.qw_teleport) {
        qa_vec3 origin; bool present=false;
        if (!c->hooks.qw_teleport(c->hooks.context,c->id,&origin,&present,e) || !current(c,e)) return false;
        if (present) {
            if (!qa_vec_finite(origin)) return qa_network_fail(e,"QW camera supplied a nonfinite spectator teleport");
            uint8_t data[13]; qa_net_writer writer; qa_net_writer_init(&writer,data,sizeof(data),e);
            qa_q1_client_message message={.op=QA_Q1_CLC_TELEPORT,
                .data.teleport={origin.x,origin.y,origin.z}};
            if (!qa_q1_client_write(&writer,c->protocol,0,&message) ||
                !q1_client_queue(c,(qa_bytes){data,qa_net_writer_size(&writer)},e)) return false;
        }
    }
    return enqueue_move(c, c->qw ? NULL : &nq, c->qw ? &qw : NULL, e);
}
static bool restart(void *context, uint64_t epoch, const qa_sha256_digest *composition, qa_error *e)
{ (void)context; (void)epoch; (void)composition; return qa_network_fail(e, "Q1 CLIENT travel must arrive from its original server"); }
static bool rebind(void *context, const qa_net_address *address, qa_error *e)
{ (void)context; (void)address; return qa_network_fail(e, "Q1 CLIENT endpoint requires a genuine connection admission"); }
void q1_client_close(void *context)
{
    q1_runtime_client *c = context; if (!c) return;
    queue_clear(c); q1_client_batch_clear(c);
    q1_client_retirement_clear(&c->retirement);
    qa_nq_decoder_destroy(c->nq); qa_qw_decoder_destroy(c->qw); qa_qw_precache_destroy(c->precache);
    if (c->native.kind == QA_Q1_PEER_QUAKEWORLD) qa_qw_channel_destroy(c->native.channel.qw);
    else qa_nq_channel_destroy(c->native.channel.nq);
    free(c->name); free(c->parameters); free(c->commands); free(c);
}
static bool pending(const void *context) { return ((const q1_runtime_client *)context)->held; }
const qa_network_peer_ops qa_network_q1_client_ops = {receive, flush, command, restart, rebind, q1_client_close, pending};
bool qa_network_q1_client_peer(const qa_network_peer *p)
{ return p && p->ops.receive == receive; }
bool qa_network_q1_client_retirement_pending(const qa_network_peer *p)
{
    if (!qa_network_q1_client_peer(p)) return false;
    const q1_runtime_client *c = p->state;
    return c->retiring && !c->retirement.marked;
}
bool qa_network_q1_client_timeout(qa_network_peer *p,const char *reason,qa_error *e)
{
    if (!qa_network_q1_client_peer(p) || !reason)
        return qa_network_fail(e,"Q1 CLIENT timeout requires its actual peer and reason");
    q1_runtime_client *c=p->state;
    if (!c->runtime->pumping || !c->runtime->callback || c->busy)
        return qa_network_fail(e,"Q1 CLIENT timeout requires its genuine idle pump callback");
    c->busy=true;
    bool ok=retire_client(c,reason,true,e);
    c->busy=false;
    return ok;
}
void qa_network_q1_client_transport_rebind(qa_network_peer *p, qa_net_transport *transport)
{ if (qa_network_q1_client_peer(p)) ((q1_runtime_client *)p->state)->native.transport = transport; }
q1_runtime_client *q1_client_get(qa_network_runtime *runtime, qa_net_client_id id, qa_error *e)
{
    qa_network_peer *p = qa_network_peer_get(runtime, id, e);
    if (!p || !qa_network_q1_client_peer(p)) { qa_network_fail(e, "Connection is not the original Q1 CLIENT owner"); return NULL; }
    return p->state;
}
bool q1_client_create(qa_network_runtime *runtime, const qa_net_connect *request,
    const qa_network_q1_client_policy *policy, const qa_network_q1_client_hooks *hooks,
    q1_runtime_client **out, qa_error *e)
{
    bool qw = request && qa_q1_is_qw(request->protocol);
    if (!runtime || !request || !policy || !hooks || !out || !qa_q1_profile_valid(request->protocol, e) ||
        request->seat_count != 1 || policy->message_bytes < 64 || policy->message_bytes > (qw ? 65525u : 65527u) ||
        !policy->service_limit || policy->service_limit > policy->message_bytes ||
        policy->service_limit > SIZE_MAX / sizeof(q1_client_record) ||
        !policy->pending_commands || policy->pending_commands > QA_NETWORK_COMMAND_BACKUP ||
        policy->queued_bytes < policy->message_bytes ||
        policy->message_bytes + (qw ? 10u : 8u) > qa_net_transport_limit(runtime->transport) ||
        !hooks->end || !hooks->sent || !hooks->acknowledged || !hooks->drop ||
        (qw ? (!hooks->qw || !hooks->qw_game_state || !hooks->qw_skins || !hooks->qw_loss || !hooks->command_qw || !policy->bytes_per_second) :
        (!hooks->nq || !hooks->command_nq || !policy->fragment_bytes || policy->fragment_bytes > policy->message_bytes ||
            !policy->nq_identity.name || !policy->nq_identity.spawn_parameters || policy->nq_identity.stage)))
        return qa_network_fail(e, "Original Q1 CLIENT requires its complete actual Source and channel policy");
    q1_runtime_client *c = calloc(1, sizeof(*c));
    if (!c) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating original Q1 CLIENT"); return false; }
    c->runtime = runtime; c->protocol = c->admitted_protocol = request->protocol;
    c->policy = *policy; c->hooks = *hooks;
    c->native = (qa_q1_peer){.transport = runtime->transport, .remote = request->endpoint,
        .kind = qw ? QA_Q1_PEER_QUAKEWORLD : QA_Q1_PEER_NETQUAKE};
    c->commands = calloc(policy->pending_commands, sizeof(*c->commands));
    bool ok = c->commands != NULL;
    if (!ok) qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q1 CLIENT input queue");
    if (ok && qw) {
        c->qw = qa_qw_decoder_create(c->protocol, e);
        ok = c->qw && qa_qw_precache_create(c->protocol, &c->precache, e) &&
            qa_qw_channel_create(QA_Q1_CHANNEL_CLIENT, policy->qport, policy->message_bytes,
                policy->bytes_per_second, &c->native.channel.qw, e);
    } else if (ok) {
        size_t a = strlen(policy->nq_identity.name), b = strlen(policy->nq_identity.spawn_parameters);
        c->name = malloc(a + 1); c->parameters = malloc(b + 1);
        if (!c->name || !c->parameters) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining NQ signon identity"); ok = false; }
        else {
            memcpy(c->name, policy->nq_identity.name, a + 1); memcpy(c->parameters, policy->nq_identity.spawn_parameters, b + 1);
            c->signon = policy->nq_identity; c->signon.name = c->name; c->signon.spawn_parameters = c->parameters;
            c->policy.nq_identity = c->signon;
            qa_buffer checked = {0}; ok = qa_nq_signon_checkpoint(&c->signon, &checked, e); qa_buffer_free(&checked);
            if (ok) ok = qa_nq_decoder_create(c->protocol, policy->nq_options, &c->nq, e) &&
                qa_nq_channel_create(policy->message_bytes, policy->fragment_bytes, &c->native.channel.nq, e);
        }
    }
    if (!ok) { q1_client_close(c); return false; }
    *out = c; return true;
}
bool qa_network_attach_q1_client(qa_network_runtime *runtime, const qa_net_connect *request,
    const qa_network_q1_client_policy *policy, const qa_network_q1_client_hooks *hooks,
    uint64_t now, qa_net_client_id *out, qa_error *e)
{
    if (!out) return qa_network_fail(e, "Q1 CLIENT attach has no identity output");
    q1_runtime_client *c = NULL;
    if (!q1_client_create(runtime, request, policy, hooks, &c, e)) return false;
    if (!qa_network_attach(runtime, request, &qa_network_q1_client_ops, c, now, &c->id, e)) { q1_client_close(c); return false; }
    *out = c->id; return true;
}
static bool source_loading(q1_runtime_client *c, qa_error *e)
{
    const qa_net_client *client = qa_net_connections_get(c->runtime->connections, c->id);
    qa_network_peer *peer = qa_network_peer_get(c->runtime, c->id, e);
    if (!client || !peer) return false;
    qa_sha256_digest composition = client->composition;
    if (!qa_net_connections_restart(c->runtime->connections, c->id, &composition, e)) return false;
    qa_network_history_clear(peer); queue_clear(c); c->command_count = c->moves = 0;
    c->active = c->has_delta = c->waiting_skins = false; c->signon.stage = 0;
    qa_qw_history_init(&c->history); return true;
}
static bool emit_writer(q1_runtime_client *c, qa_net_writer *writer, qa_error *e)
{ return !writer->failed && q1_client_queue(c, (qa_bytes){writer->data, qa_net_writer_size(writer)}, e); }
static bool qw_game_state(q1_runtime_client *c, uint32_t *checksum, qa_error *e)
{
    size_t models = qa_qw_precache_count(c->precache, true), sounds = qa_qw_precache_count(c->precache, false);
    const char **a = models ? malloc(models * sizeof(*a)) : NULL;
    const char **b = sounds ? malloc(sounds * sizeof(*b)) : NULL;
    if ((models && !a) || (sounds && !b)) { free(a); free(b); qa_error_set(e, QA_ERROR_MEMORY, 0, "Borrowing actual QW Source precaches"); return false; }
    for (size_t i = 0; i < models; ++i) a[i] = qa_qw_precache_name(c->precache, true, i);
    for (size_t i = 0; i < sounds; ++i) b[i] = qa_qw_precache_name(c->precache, false, i);
    bool ok = current(c, e) && c->hooks.qw_game_state(c->hooks.context, c->id, a, models, b, sounds, checksum, e) && current(c, e);
    free(a); free(b); return ok;
}
static bool qw_stuff(q1_runtime_client *c, const char *text, bool *skins_requested, qa_error *e)
{
    while (*text) {
        const char *end = text; bool quoted = false;
        while (*end) {
            if (*end == '"') quoted = !quoted;
            if (*end == '\n' || (!quoted && *end == ';')) break;
            ++end;
        }
        size_t size = (size_t)(end - text);
        char *line = malloc(size + 1);
        if (!line) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining QW signon command"); return false; }
        memcpy(line, text, size); line[size] = 0;
        const char *cursor = line; char name[32]; bool present;
        bool ok = qa_q1_token(&cursor, true, name, sizeof(name), &present, e);
        if (ok && present && !strcmp(name, "cmd")) {
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            if (*cursor) ok = qa_network_q1_client_command(c->runtime, c->id, cursor, e);
        } else if (ok && present && !strcmp(name, "skins")) {
            char extra[2]; bool argument;
            ok = qa_q1_token(&cursor, true, extra, sizeof(extra), &argument, e);
            if (ok && !argument) *skins_requested=true;
        }
        free(line); if (!ok) return false;
        text = *end ? end + 1 : end;
    }
    return true;
}
static bool protocol_service(q1_runtime_client *c, q1_client_record *record, qa_net_writer *writer,
    bool *skins_requested, qa_error *e)
{
    if (!c->qw) {
        qa_nq_message *m = &record->service.nq;
        if (m->op == QA_NQ_SERVERINFO && !source_loading(c, e)) return false;
        if (!c->hooks.nq(c->hooks.context, c->id, record->protocol, m, c->received_ns, e) || !current(c, e)) return false;
        if (m->op == QA_NQ_SIGNON) {
            if (!qa_nq_signon_receive(&c->signon, (uint8_t)m->data.value, writer) || !emit_writer(c, writer, e)) return false;
            if (c->signon.stage == 2 && !qa_network_phase(c->runtime, c->id, QA_NET_PRIMED, e)) return false;
        }
        if (m->op == QA_NQ_ENTITY) qa_nq_signon_first_entity(&c->signon);
        if (c->signon.stage == 4 && !c->active) {
            if (!qa_network_phase(c->runtime, c->id, QA_NET_ACTIVE, e)) return false;
            c->active = true;
        }
        if (m->op == QA_NQ_DISCONNECT) c->retiring = true;
        return true;
    }
    qa_qw_service *m = &record->service.qw;
    if (m->kind == QA_QW_SERVER_DATA && (!source_loading(c, e) ||
        !qa_qw_precache_reset(c->precache, record->protocol, m->data.server.server_count, writer) || !emit_writer(c, writer, e))) return false;
    if (!c->hooks.qw(c->hooks.context, c->id, record->protocol, m, c->received_ns, e) || !current(c, e)) return false;
    if (m->kind == QA_QW_MODEL_LIST || m->kind == QA_QW_SOUND_LIST) {
        bool models = m->kind == QA_QW_MODEL_LIST;
        if (!qa_qw_precache_list(c->precache, models, m->data.list.first, m->data.list.names,
            m->data.list.count, m->data.list.next, writer) || !emit_writer(c, writer, e)) return false;
        if (!m->data.list.next) {
            qa_net_writer_init(writer, writer->data, writer->capacity, e);
            uint32_t checksum;
            if (!(models ? qw_game_state(c, &checksum, e) && qa_qw_precache_models_ready(c->precache, checksum, writer) :
                qa_qw_precache_sounds_ready(c->precache, writer)) || !emit_writer(c, writer, e)) return false;
            if (models && !qa_network_phase(c->runtime, c->id, QA_NET_PRIMED, e)) return false;
        }
    }
    if (m->kind == QA_QW_PACKET_ENTITIES) {
        c->last_frame = m->data.packet.frame.sequence; c->has_delta = true;
        if (!c->active) { if (!qa_network_phase(c->runtime, c->id, QA_NET_ACTIVE, e)) return false; c->active = true; }
    } else if (m->kind == QA_QW_INVALID_DELTA) c->has_delta = false;
    else if (m->kind == QA_QW_DISCONNECT) c->retiring = true;
    else if (m->kind == QA_QW_STUFFTEXT && !qw_stuff(c, m->data.text.value, skins_requested, e)) return false;
    return true;
}
bool qa_network_q1_client_continue(qa_network_runtime *runtime, qa_net_client_id id, qa_error *e)
{
    if (!runtime || !qa_network_callbacks_idle(runtime)) return qa_network_fail(e, "Q1 CLIENT source continuation requires idle runtime ownership");
    q1_runtime_client *c = q1_client_get(runtime, id, e);
    if (!c || c->busy || c->retiring) return qa_network_fail(e, "Q1 CLIENT cannot resume its retired source batch");
    if (!c->held) return true;
    uint8_t *data = malloc(c->policy.message_bytes);
    if (!data) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding original Q1 signon responses"); return false; }
    c->busy = true; runtime->callback = true;
    bool ok = current(c, e), skins_requested=false;
    if (ok && c->qw) {
        qa_qw_history_acknowledge(&c->history, c->acknowledged, (double)c->received_ns / 1e9);
        ok = c->hooks.acknowledged(c->hooks.context, id, c->acknowledged, c->received_ns, e) && current(c, e);
    }
    while (ok && !c->retiring && c->cursor < c->record_count) {
        qa_net_writer writer; qa_net_writer_init(&writer, data, c->policy.message_bytes, e);
        ok = current(c, e) && protocol_service(c, &c->records[c->cursor], &writer, &skins_requested, e);
        if (ok) ++c->cursor;
    }
    if (ok) ok = c->hooks.end(c->hooks.context, id, c->received_ns, e);
    if (ok && !c->retiring) ok = current(c, e);
    if (ok && !c->retiring && skins_requested) {
        bool ready=false;
        ok=c->hooks.qw_skins(c->hooks.context,c->id,&ready,e) && current(c,e);
        if (ok && !c->active) {
            c->waiting_skins=!ready;
            if (ready) {
                qa_net_writer writer; qa_net_writer_init(&writer,data,c->policy.message_bytes,e);
                ok=qa_qw_precache_skins_ready(c->precache,&writer) && emit_writer(c,&writer,e);
            }
        }
    }
    if (c->retiring || !ok) {
        qa_error ignored = {0}; bool dropped = retire_client(c,
            ok ? "server disconnected" : "source service failed", false, ok ? e : &ignored);
        ok = ok && dropped;
    }
    runtime->callback = false; c->busy = false; free(data); q1_client_batch_clear(c); return ok;
}
bool qa_network_q1_client_command(qa_network_runtime *runtime, qa_net_client_id id, const char *text, qa_error *e)
{
    q1_runtime_client *c = q1_client_get(runtime, id, e);
    if (!c || !text || c->retiring) return qa_network_fail(e, "Q1 CLIENT command lacks its actual live owner");
    size_t size = strlen(text);
    if (size > c->policy.message_bytes - 2) return qa_network_fail(e, "Q1 CLIENT text exceeds its actual channel policy");
    uint8_t *data = malloc(size + 2);
    if (!data) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding original Q1 command"); return false; }
    qa_net_writer writer; qa_net_writer_init(&writer, data, size + 2, e);
    qa_q1_client_message message = {.op = QA_Q1_CLC_STRING, .data.text = text};
    bool ok = qa_q1_client_write(&writer, c->protocol, 0, &message) && emit_writer(c, &writer, e);
    free(data); return ok;
}
bool qa_network_q1_client_skins_ready(qa_network_runtime *runtime, qa_net_client_id id, qa_error *e)
{
    q1_runtime_client *c = q1_client_get(runtime, id, e);
    if (!c || !c->qw || !c->waiting_skins || c->active || !current(c, e) ||
        (!qa_network_callbacks_idle(runtime) && !(c->busy && runtime->callback && !runtime->pumping)))
        return qa_network_fail(e, "QW skin completion lacks its genuine pending Source preparation");
    uint8_t *data = malloc(c->policy.message_bytes);
    if (!data) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding genuine QW skin completion"); return false; }
    qa_net_writer writer; qa_net_writer_init(&writer, data, c->policy.message_bytes, e);
    bool ok = qa_qw_precache_skins_ready(c->precache, &writer) && emit_writer(c, &writer, e);
    free(data);
    if (ok) c->waiting_skins = false;
    return ok;
}
bool qa_network_q1_client_start(qa_network_runtime *runtime, qa_net_client_id id, qa_error *e)
{
    q1_runtime_client *c = q1_client_get(runtime, id, e);
    if (!c || !qa_network_callbacks_idle(runtime) || c->started || c->retiring)
        return qa_network_fail(e, "Q1 CLIENT start requires its freshly bound actual Source");
    if (c->qw && !qa_network_q1_client_command(runtime, id, "new", e)) return false;
    c->started = true; return true;
}
bool qa_network_q1_client_disconnect(qa_network_runtime *runtime, qa_net_client_id id,
    const char *reason, qa_error *e)
{
    if (!runtime || !qa_network_callbacks_idle(runtime) || !reason)
        return qa_network_fail(e, "Q1 CLIENT disconnect requires its actual idle owner and reason");
    q1_runtime_client *c = q1_client_get(runtime, id, e);
    if (!c || c->busy) return qa_network_fail(e, "Q1 CLIENT disconnect overlaps its Source callback");
    runtime->callback = true; c->busy = true;
    bool ok = retire_client(c, reason, !c->retiring, e);
    runtime->callback = false; c->busy = false;
    return ok && qa_network_detach(runtime, id, reason, e);
}
bool qa_network_q1_client_move_nq(qa_network_runtime *runtime, qa_net_client_id id, const qa_q1_command *move, qa_error *e)
{
    q1_runtime_client *c = q1_client_get(runtime, id, e);
    return c && !c->qw && qa_network_callbacks_idle(runtime) && enqueue_move(c, move, NULL, e);
}
bool qa_network_q1_client_move_qw(qa_network_runtime *runtime, qa_net_client_id id, const qa_qw_command *move, qa_error *e)
{
    q1_runtime_client *c = q1_client_get(runtime, id, e);
    return c && c->qw && qa_network_callbacks_idle(runtime) && enqueue_move(c, NULL, move, e);
}
bool qa_network_q1_client_receive_pending(qa_network_runtime *runtime, qa_net_client_id id)
{ q1_runtime_client *c = q1_client_get(runtime, id, NULL); return c && c->held; }
bool qa_network_q1_client_state_read(qa_network_runtime *runtime, qa_net_client_id id,
    qa_network_q1_client_state *out, qa_error *e)
{
    q1_runtime_client *c = q1_client_get(runtime, id, e);
    if (!c || !out) return qa_network_fail(e, "Q1 CLIENT state has no actual owner/output");
    *out = (qa_network_q1_client_state){c->protocol, c->admitted_protocol, c->before_protocol,
        c->moves, c->last_frame, c->record_count, c->cursor,
        c->queued_bytes, c->command_count, c->signon.stage, c->held, c->active, c->retiring, c->has_delta,
        c->waiting_skins}; return true;
}
const qa_nq_decoder *qa_network_q1_client_nq_decoder(qa_network_runtime *runtime, qa_net_client_id id)
{ q1_runtime_client *c = q1_client_get(runtime, id, NULL); return c ? c->nq : NULL; }
const qa_qw_decoder *qa_network_q1_client_qw_decoder(qa_network_runtime *runtime, qa_net_client_id id)
{ q1_runtime_client *c = q1_client_get(runtime, id, NULL); return c ? c->qw : NULL; }
