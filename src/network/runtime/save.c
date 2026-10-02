#include "internal.h"
#include "qa/network_save.h"
#include <stdlib.h>
#include <string.h>

static bool writer_storage(size_t size, qa_buffer *bytes, qa_net_writer *w, qa_error *error)
{
    bytes->data = malloc(size); bytes->size = 0;
    if (!bytes->data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating network continuation"); return false; }
    qa_net_writer_init(w, bytes->data, size, error); return true;
}

bool qa_network_connections_checkpoint(const qa_network_runtime *runtime, const qa_network_checkpoint_refs *refs,
    qa_buffer *out, qa_error *error)
{
    if (!runtime || !refs || !out || !qa_network_callbacks_idle(runtime) ||
        (runtime->options.clients && (SIZE_MAX - 16) / runtime->options.clients < 512))
        return qa_network_fail(error, "Network continuation requires an idle runtime owner");
    qa_buffer table = {0}; qa_net_writer tw;
    if (!writer_storage((size_t)runtime->options.clients * 512 + 16, &table, &tw, error)) return false;
    if (!qa_net_connections_checkpoint(runtime->connections, &tw)) { qa_buffer_free(&table); return false; }
    table.size = qa_net_writer_size(&tw);
    qa_buffer *sources = calloc(runtime->options.clients, sizeof(*sources));
    uint32_t *kinds = calloc(runtime->options.clients, sizeof(*kinds));
    if (!sources || !kinds) {
        free(sources); free(kinds); qa_buffer_free(&table);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining network source inventory"); return false;
    }
    bool ok = true; size_t size = 40 + table.size; uint32_t count = 0;
    for (uint32_t i = 0; ok && i < runtime->options.clients; ++i) {
        const qa_network_peer *peer = &runtime->peers[i];
        if (!peer->occupied) continue;
        const qa_net_client *client = qa_net_connections_get(runtime->connections, peer->id);
        if (!client || !peer->epoch || peer->seat_count != client->seat_count) {
            ok = qa_network_fail(error, "Network peer and connection inventory differ"); break;
        }
        if(qa_network_local_peer(peer)) {
            kinds[i]=QA_NETWORK_SOURCE_LOCAL;
            ok=qa_network_local_checkpoint_peer(peer,refs,&sources[i],error);
        } else if(qa_network_q1_client_peer(peer)) {
            kinds[i]=QA_NETWORK_SOURCE_Q1_CLIENT;
            ok=qa_network_q1_client_checkpoint_peer(peer,&sources[i],error);
        } else if (qa_network_nq_peer(peer)) {
            kinds[i] = QA_NETWORK_SOURCE_NQ_SERVER;
            ok = qa_network_nq_checkpoint_peer(peer, &sources[i], error);
        } else if (qa_network_qw_peer(peer)) {
            kinds[i] = QA_NETWORK_SOURCE_QW_SERVER;
            ok = qa_network_qw_checkpoint_peer(peer, &sources[i], error);
        } else if (qa_network_q2_peer(peer)) {
            bool server;
            ok=qa_network_q2_checkpoint_peer(peer,&refs->q2,&server,&sources[i],error);
            if(ok) kinds[i]=server?QA_NETWORK_SOURCE_Q2_SERVER:QA_NETWORK_SOURCE_Q2_CLIENT;
        } else if (qa_unified_session_peer(peer)) {
            kinds[i]=QA_NETWORK_SOURCE_UNIFIED;
            ok=qa_unified_session_peer_checkpoint(peer,&sources[i],error);
        } else ok = qa_network_q3_checkpoint_peer(peer, &kinds[i], &sources[i], error);
        if (ok && (sources[i].size > SIZE_MAX - 24 || size > SIZE_MAX - 24 - sources[i].size))
            ok = qa_network_fail(error, "Network source continuation extent overflow");
        if (ok) { size += 24 + sources[i].size; ++count; }
    }
    qa_buffer bytes = {0}; qa_net_writer w;
    if (ok) ok = writer_storage(size, &bytes, &w, error);
    if (ok) ok = qa_net_write_u32(&w, UINT32_C(0x434e4151)) && qa_net_write_u32(&w, 1) &&
        qa_net_write_u64(&w, runtime->options.timeout_ns) && qa_net_write_u32(&w, runtime->options.packets_per_pump) &&
        qa_net_write_u64(&w, runtime->now_ns) && qa_net_write_u64(&w, table.size) &&
        qa_net_write_data(&w, table.data, table.size) && qa_net_write_u32(&w, count);
    for (uint32_t i = 0; ok && i < runtime->options.clients; ++i) {
        const qa_network_peer *peer = &runtime->peers[i];
        if (!peer->occupied) continue;
        ok = qa_net_write_u32(&w, i) && qa_net_write_u32(&w, kinds[i]) && qa_net_write_u64(&w, peer->epoch) &&
            qa_net_write_u64(&w, sources[i].size) && qa_net_write_data(&w, sources[i].data, sources[i].size);
    }
    for (uint32_t i = 0; i < runtime->options.clients; ++i) qa_buffer_free(&sources[i]);
    free(sources); free(kinds); qa_buffer_free(&table);
    if (!ok) { qa_buffer_free(&bytes); return false; }
    bytes.size = qa_net_writer_size(&w); *out = bytes; return true;
}

static bool read_blob(qa_net_reader *r, qa_bytes *out)
{
    uint64_t size = qa_net_read_u64(r);
    if (r->failed || size > SIZE_MAX) return qa_net_reader_fail(r, "Network continuation extent exceeds host storage");
    return qa_net_read_bytes(r, (size_t)size, out);
}

bool qa_network_connections_restore(qa_bytes bytes, qa_net_transport *transport,
    const qa_network_options *options, const qa_network_checkpoint_refs *refs,
    qa_network_runtime **out, qa_error *error)
{
    if (!options || !out || *out || !transport || !refs ||
        (!refs->source && !refs->source_nq && !refs->source_qw &&
         !refs->q2.source_server && !refs->q2.source_client && !refs->source_unified &&
         !refs->source_q1_client && !refs->source_local) || !bytes.data)
        return qa_network_fail(error, "Network restore requires qualified candidate consumers");
    qa_net_reader r; qa_net_reader_init(&r, bytes, error);
    uint32_t tag = qa_net_read_u32(&r), version = qa_net_read_u32(&r);
    uint64_t timeout = qa_net_read_u64(&r); uint32_t packets = qa_net_read_u32(&r);
    uint64_t now = qa_net_read_u64(&r); qa_bytes table_bytes;
    if (tag != UINT32_C(0x434e4151) || version != 1 || timeout != options->timeout_ns ||
        packets != options->packets_per_pump) return qa_net_reader_fail(&r, "Network candidate policy differs from saved owner");
    if (!read_blob(&r, &table_bytes)) return false;
    qa_network_runtime *runtime = NULL;
    if (!qa_network_create(transport, options, &runtime, error)) return false;
    *out=runtime;
    qa_net_reader table_reader; qa_net_reader_init(&table_reader, table_bytes, error);
    qa_net_connections *connections = NULL;
    if (!qa_net_connections_restore(&table_reader, options->owner, options->clients,
        qa_network_admission, runtime, &connections) ||
        !qa_net_reader_finish(&table_reader)) { qa_net_connections_destroy(connections); goto failure; }
    qa_net_connections_destroy(runtime->connections); runtime->connections = connections; runtime->now_ns = now;
    uint32_t count = qa_net_read_u32(&r), previous = 0;
    if (count > options->clients) { qa_net_reader_fail(&r, "Invalid saved source peer inventory extent"); goto failure; }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t slot = qa_net_read_u32(&r), kind = qa_net_read_u32(&r);
        uint64_t epoch = qa_net_read_u64(&r); qa_bytes source;
        if (r.failed || slot >= options->clients || (i && slot <= previous) || !epoch || !read_blob(&r, &source)) {
            qa_net_reader_fail(&r, "Invalid saved source peer ordering or epoch"); goto failure;
        }
        previous = slot;
        uint32_t cursor = slot; const qa_net_client *client = NULL;
        if (!qa_net_connections_next(connections, &cursor, &client) || client->id.slot != slot || !client->seat_count) {
            qa_net_reader_fail(&r, "Saved source peer has no admitted connection"); goto failure;
        }
        qa_network_peer *peer = &runtime->peers[slot];
        /* Canonical identity and epoch precede physical Source callbacks;
         * the owned protocol channel transfers only after decoding succeeds. */
        peer->id=client->id; peer->epoch=epoch; peer->seat_count=client->seat_count;
        bool restored;
        if(kind==QA_NETWORK_SOURCE_LOCAL) {
            restored=qa_network_local_restore_peer(runtime,client,source,refs,peer,error);
        } else if(kind==QA_NETWORK_SOURCE_Q1_CLIENT) {
            qa_network_q1_client_policy policy={0}; qa_network_q1_client_hooks hooks={0};
            restored=refs->source_q1_client && refs->source_q1_client(refs->context,runtime,client,&policy,&hooks,error) &&
                qa_network_q1_client_restore_peer(runtime,client,source,&policy,&hooks,peer,error);
            if(!refs->source_q1_client) qa_network_fail(error,"Q1 CLIENT restore lacks its actual candidate Source callbacks");
        } else if(kind==QA_NETWORK_SOURCE_UNIFIED) {
            qa_unified_session_hooks hooks={0}; qa_unified_session *session=NULL; qa_network_peer_ops ops;
            restored=refs->source_unified && refs->source_unified(refs->context,runtime,client,&hooks,error) &&
                qa_unified_session_restore(source,runtime,client,&hooks,&session,&ops,error);
            if(restored) *peer=(qa_network_peer){.id=client->id,.ops=ops,.state=session};
            else if(!refs->source_unified) qa_network_fail(error,"Unified restore lacks its actual candidate Source callbacks");
        } else restored = kind == QA_NETWORK_SOURCE_NQ_SERVER
            ? qa_network_nq_restore_peer(runtime, client, source, refs, peer, error)
            : kind == QA_NETWORK_SOURCE_QW_SERVER
            ? qa_network_qw_restore_peer(runtime, client, source, refs, peer, error)
            : kind == QA_NETWORK_SOURCE_Q2_SERVER || kind == QA_NETWORK_SOURCE_Q2_CLIENT
            ? qa_network_q2_restore_peer(runtime,client,kind==QA_NETWORK_SOURCE_Q2_SERVER,source,&refs->q2,peer,error)
            : qa_network_q3_restore_peer(runtime, client, kind, source, refs, peer, error);
        if (!restored) goto failure;
        peer->seats = calloc(client->seat_count, sizeof(*peer->seats));
        if (!peer->seats) {
            peer->ops.close(peer->state); memset(peer, 0, sizeof(*peer));
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring network seat history owners"); goto failure;
        }
        peer->id = client->id; peer->epoch = epoch; peer->seat_count = client->seat_count; peer->occupied = true;
        for (size_t j = 0; j < peer->seat_count; ++j) peer->seats[j].id = client->seats[j].seat;
        if(client->protocol.kind==QA_NET_UNIFIED_1) {
            if(!qa_unified_session_peer(peer)) {
                qa_net_reader_fail(&r,"Saved Unified connection lacks its actual token channel"); goto failure;
            }
            for(uint32_t earlier=0;earlier<slot;++earlier)
                if(runtime->peers[earlier].occupied &&
                    qa_unified_session_peer_tokens_equal(peer,&runtime->peers[earlier])) {
                    qa_net_reader_fail(&r,"Saved Unified peers share an actual channel token"); goto failure;
                }
        }
    }
    uint32_t cursor = 0; const qa_net_client *client;
    while (qa_net_connections_next(connections, &cursor, &client))
        if (!runtime->peers[client->id.slot].occupied) {
            qa_net_reader_fail(&r, "Saved connection lacks an actual source continuation"); goto failure;
        }
    if (!qa_net_reader_finish(&r)) goto failure;
    *out = runtime; return true;
failure:
    /* Physical Source factories may already borrow this exact runtime.
     * Its caller retains the partial candidate until those owners release. */
    return false;
}

bool qa_network_source_publication_ready(const qa_network_runtime *runtime,qa_error *error)
{
    if(!runtime || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error,"Source publication requires its actual returned runtime");
    for(uint32_t i=0;i<runtime->options.clients;++i)
        if(runtime->peers[i].occupied && qa_unified_session_peer(&runtime->peers[i]) &&
            !qa_unified_session_peer_source_ready(&runtime->peers[i],error)) return false;
    return true;
}
void qa_network_transport_publish_retained(qa_network_runtime *active,qa_network_runtime *candidate)
{
    for (uint32_t i = 0; i < active->options.clients; ++i)
        if (active->peers[i].occupied) {
            qa_unified_session_peer_source_retire(&active->peers[i]);
            qa_network_nq_transport_rebind(&active->peers[i], active->transport);
            qa_network_qw_transport_rebind(&active->peers[i], active->transport);
            qa_network_q1_client_transport_rebind(&active->peers[i],active->transport);
        }
    for (uint32_t i = 0; i < candidate->options.clients; ++i)
        if (candidate->peers[i].occupied) {
            qa_unified_session_peer_source_publish(&candidate->peers[i]);
            qa_network_nq_transport_rebind(&candidate->peers[i], candidate->transport);
            qa_network_qw_transport_rebind(&candidate->peers[i], candidate->transport);
            qa_network_q1_client_transport_rebind(&candidate->peers[i],candidate->transport);
        }
}
void qa_network_transport_exchange(qa_network_runtime *active, qa_network_runtime *candidate)
{
    qa_net_transport *transport = active->transport;
    active->transport = candidate->transport; candidate->transport = transport;
    qa_network_transport_publish_retained(active,candidate);
}
const qa_net_address *qa_network_local_address(const qa_network_runtime *runtime)
{ return runtime ? qa_net_transport_address(runtime->transport) : NULL; }
