#include "network_unified.h"
#include "internal.h"
#include "remote_unified_presentation.h"
#include "qa/network_unified_save.h"

#include <stdlib.h>
#include <string.h>

#define UNIFIED_PEERS 264u
typedef struct unified_peer {
    qa_net_connect request;
    qa_net_seat_binding binding;
    qa_net_client_id client;
    qa_unified_session *session;
    application_unified_server *server;
    frontend_remote_unified *remote;
    bool occupied, staging, travel_prepared, frame_published;
} unified_peer;
struct frontend_network_unified {
    frontend_network_unified_options options;
    qa_unified_bootstrap *bootstrap;
    unified_peer peers[UNIFIED_PEERS];
    application_unified_source source, travel_source;
    size_t travel_cursor;
    uint32_t epoch;
    uint64_t frame_before;
    unsigned calls;
    bool closing, traveling, frame_boundary;
};

static bool fail(qa_error *error, const char *message)
{ return frontend_fail(error, QA_ERROR_ARGUMENT, message); }
static bool parent(const frontend_network_unified *owner)
{
    return owner && !owner->closing && owner->options.frontend->application &&
        owner->options.current(owner->options.context, owner);
}
static bool same_source(const application_unified_source *a, const application_unified_source *b)
{
    return a->launch == b->launch && a->session == b->session && a->world == b->world &&
        a->owner == b->owner && a->publication == b->publication &&
        a->map_revision == b->map_revision && a->max_clients == b->max_clients;
}
bool frontend_network_unified_idle(const frontend_network_unified *owner)
{
    if (!owner) return true;
    if (owner->calls || !qa_network_callbacks_idle(owner->options.runtime) ||
        (owner->bootstrap && !qa_unified_bootstrap_idle(owner->bootstrap))) return false;
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        const unified_peer *peer = owner->peers + i;
        if (peer->staging) return false;
        if (peer->occupied && qa_net_connections_get(qa_network_connections(owner->options.runtime), peer->client)) {
            qa_unified_session *installed = NULL;
            if (!qa_unified_session_find(owner->options.runtime, peer->client, &installed, NULL) ||
                installed != peer->session || !qa_unified_session_idle(installed)) return false;
        }
    }
    return frontend_remote_unified_idle(owner->options.frontend);
}

static bool release_peer(unified_peer *peer, qa_error *error)
{
    if (peer->server && !application_unified_server_destroy(peer->server, error)) return false;
    peer->server = NULL;
    if (peer->remote && !frontend_remote_unified_destroy(&peer->remote, error)) return false;
    *peer = (unified_peer){0}; return true;
}
static bool prune(frontend_network_unified *owner, qa_error *error)
{
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        unified_peer *peer = owner->peers + i;
        if (peer->occupied && !peer->staging &&
            !qa_net_connections_get(qa_network_connections(owner->options.runtime), peer->client) &&
            !release_peer(peer, error)) return false;
    }
    return true;
}

bool frontend_network_unified_admit(frontend_network_unified *owner,
    const qa_net_connect *request, bool *recognized, qa_error *error)
{
    if (!owner || !request || !recognized) return fail(error, "Missing actual Unified admission request");
    *recognized = request->protocol.kind == QA_NET_UNIFIED_1;
    if (!*recognized) return true;
    if (!parent(owner)) return fail(error, "Unified admission lost its installed Network owner");
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        const unified_peer *peer = owner->peers + i;
        if (!peer->occupied || !peer->staging || request != &peer->request) continue;
        if (request->protocol.revision || request->protocol.flags || request->attachment != QA_NET_REMOTE ||
            request->seat_count != 1 || request->seats != &peer->binding || peer->binding.remote_index ||
            peer->binding.seat.owner != owner->options.seat_owner)
            return fail(error, "Unified admission changed its staged canonical transport seat");
        if (owner->options.server) {
            application_unified_source source;
            const qa_sha256_digest *composition = application_unified_server_composition(peer->server);
            return composition && qa_sha256_equal(composition, &request->composition) &&
                application_unified_source_read(owner->options.frontend->application, &source, error);
        }
        return peer->remote && qa_net_address_equal(&request->endpoint, &owner->options.remote, true);
    }
    return fail(error, "Unified request has no genuine pending handshake attachment");
}

static bool attach(void *context, qa_network_runtime *runtime, const qa_net_address *address,
    qa_unified_token token, qa_unified_token nonce, bool server, uint64_t now,
    qa_net_client_id *id, qa_unified_session **session, qa_error *error)
{
    frontend_network_unified *owner = context;
    (void)nonce;
    if (!parent(owner) || owner->traveling || runtime != owner->options.runtime || server != owner->options.server ||
        !address || !id || !session || !prune(owner, error)) return false;
    size_t index = 0;
    while (index < UNIFIED_PEERS && owner->peers[index].occupied) ++index;
    if (index == UNIFIED_PEERS || (!server && index)) return fail(error, "Unified transport peer capacity is exhausted");
    unified_peer *peer = owner->peers + index;
    *peer = (unified_peer){.occupied = true, .staging = true};
    peer->binding = (qa_net_seat_binding){.seat = {owner->options.seat_owner,
        server ? owner->options.remote_seat_base + (uint32_t)index : owner->options.client.domain.seat.index}};
    peer->request = (qa_net_connect){.attachment = QA_NET_REMOTE, .endpoint = *address,
        .protocol = {.kind = QA_NET_UNIFIED_1}, .seats = &peer->binding, .seat_count = 1};
    qa_unified_document *offer = NULL;
    qa_unified_session_hooks hooks = {0};
    bool okay;
    if (server) {
        okay = application_unified_server_create(owner->options.frontend->application, runtime,
            peer->binding.seat, peer->binding.seat.index, owner->epoch, owner->options.sidecars,
            owner->options.sidecar_count, &peer->server, &offer, error);
        if (okay) { peer->request.composition = *application_unified_server_composition(peer->server);
            hooks = application_unified_server_hooks(peer->server); }
    } else {
        frontend_remote_unified_options options = owner->options.client;
        options.domain.runtime = runtime; options.domain.client = (qa_net_client_id){0};
        options.domain.seat = peer->binding.seat;
        okay = frontend_remote_unified_presentation_create(owner->options.frontend, &options, &peer->remote, error);
        if (okay) hooks = frontend_remote_unified_hooks(peer->remote);
    }
    if (okay) okay = qa_unified_session_attach(runtime, &peer->request, server, token, NULL,
        &hooks, now, &peer->client, &peer->session, error);
    if (okay) okay = server ? application_unified_server_bind(peer->server, peer->client, peer->session, error) :
        frontend_remote_unified_bind(peer->remote, peer->client, peer->session, error);
    if (okay && server) okay = qa_unified_session_control(peer->session, offer, error);
    qa_unified_document_destroy(offer);
    peer->staging = false;
    if (!okay) {
        if (qa_net_connections_get(qa_network_connections(runtime), peer->client) &&
            !qa_network_detach(runtime, peer->client, "Unified Source attachment failed", error)) return false;
        if (!release_peer(peer, error)) return false;
        return false;
    }
    *id = peer->client; *session = peer->session; return true;
}

bool frontend_network_unified_create(const frontend_network_unified_options *options,
    frontend_network_unified **out, qa_error *error)
{
    if (!options || !out || *out || !options->frontend || !options->frontend->application ||
        !options->runtime || !options->current || !options->seat_owner ||
        !qa_network_callbacks_idle(options->runtime) || (options->sidecar_count && !options->sidecars) ||
        options->remote_seat_base > UINT32_MAX - (UNIFIED_PEERS - 1))
        return fail(error, "Unified controller requires its actual Network and seat namespace");
    application_unified_source source = {0};
    uint32_t capacity = 1;
    if (options->server) {
        if (!application_unified_source_read(options->frontend->application, &source, error)) return false;
        if (!source.max_clients || source.max_clients > 256)
            return fail(error, "Unified Source capacity exceeds its genuine handshake domain");
        capacity = source.max_clients;
    } else if (options->client.domain.application != options->frontend->application ||
        options->client.domain.runtime != options->runtime ||
        options->client.domain.seat.owner != options->seat_owner)
        return fail(error, "Unified client lacks its actual CLIENT factory domain");
    frontend_network_unified *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating actual Unified Network controller");
    owner->options = *options; owner->epoch = 1; owner->source = source;
    qa_unified_bootstrap_options bootstrap = {.server = options->server, .max_clients = capacity,
        .remote = options->remote, .hooks = {owner, attach}};
    if (!qa_unified_bootstrap_create(options->runtime, &bootstrap, &owner->bootstrap, error)) { free(owner); return false; }
    *out = owner; return true;
}

bool frontend_network_unified_receive(frontend_network_unified *owner,
    const qa_net_datagram *packet, bool *recognized, qa_error *error)
{
    if (!parent(owner)) return fail(error, "Unified receive lost its actual Network controller");
    return qa_unified_bootstrap_receive(owner->bootstrap, packet, recognized, error);
}
bool frontend_network_unified_tick(frontend_network_unified *owner, uint64_t now,
    bool *waiting, qa_error *error)
{
    if (!waiting || !parent(owner) || !frontend_network_unified_idle(owner))
        return fail(error, "Unified processing requires its returned Network and Source owners");
    if (owner->options.server && !frontend_network_unified_travel(owner,
        owner->options.sidecars, owner->options.sidecar_count, error)) return false;
    ++owner->calls;
    bool okay = prune(owner, error);
    if (okay && owner->options.server) {
        application_unified_source source;
        okay = application_unified_source_read(owner->options.frontend->application, &source, error) &&
            qa_unified_bootstrap_current_limits(owner->bootstrap, source.max_clients, error);
    }
    if (okay) okay = qa_unified_bootstrap_process(owner->bootstrap, now, waiting, error);
    if (okay) okay = prune(owner, error);
    --owner->calls; return okay;
}
bool frontend_network_unified_pre_frame(frontend_network_unified *owner, qa_error *error)
{
    if (!parent(owner) || !frontend_network_unified_idle(owner)) return fail(error, "Unified Source input boundary is busy");
    if (!owner->options.server) return true;
    application_unified_source source;
    if (!application_unified_source_read(owner->options.frontend->application, &source, error) ||
        !same_source(&source, &owner->source) || owner->traveling)
        return fail(error, "Unified input precedes its actual Source travel publication");
    if (owner->frame_boundary && source.frame.number > owner->frame_before)
        return fail(error, "Unified retained output must finish before another Source frame");
    ++owner->calls; bool okay = prune(owner, error);
    for (size_t i = 0; okay && i < UNIFIED_PEERS; ++i)
        if (owner->peers[i].server) {
            okay = application_unified_server_pre_frame(owner->peers[i].server, error);
            if (okay) owner->peers[i].frame_published = false;
        }
    if (okay) { owner->frame_before = source.frame.number; owner->frame_boundary = true; }
    --owner->calls; return okay;
}
bool frontend_network_unified_publish(frontend_network_unified *owner,
    const application_unified_output_external *external, qa_error *error)
{
    if (!parent(owner) || !frontend_network_unified_idle(owner)) return fail(error, "Unified publication requires returned actual Source output");
    if (!owner->options.server) return true;
    application_unified_source source;
    if (!application_unified_source_read(owner->options.frontend->application, &source, error) ||
        !same_source(&source, &owner->source) || owner->traveling)
        return fail(error, "Unified output precedes its actual Source travel publication");
    if (!owner->frame_boundary || source.frame.phase != QA_FRAME_EXIT || source.frame.number <= owner->frame_before) return true;
    ++owner->calls; bool okay = prune(owner, error);
    for (size_t i = 0; okay && i < UNIFIED_PEERS; ++i) {
        unified_peer *peer = owner->peers + i;
        if (peer->server && !peer->frame_published) {
            okay = application_unified_server_publish(peer->server, external, error);
            if (okay) peer->frame_published = true;
        }
    }
    if (okay) owner->frame_boundary = false;
    --owner->calls; return okay;
}
bool frontend_network_unified_travel(frontend_network_unified *owner,
    const qa_recipe_sidecar *sidecars, size_t count, qa_error *error)
{
    if (!parent(owner) || !owner->options.server || !frontend_network_unified_idle(owner) ||
        (count && !sidecars)) return fail(error, "Unified travel lacks its returned new Source publication");
    application_unified_source source;
    if (!application_unified_source_read(owner->options.frontend->application, &source, error)) return false;
    if (!owner->traveling && same_source(&source, &owner->source)) return true;
    if (!owner->traveling) {
        if (owner->epoch == UINT32_MAX) return fail(error, "Unified Source epoch is exhausted");
        ++owner->epoch; owner->travel_source = source; owner->traveling = true; owner->frame_boundary = false;
        owner->travel_cursor = 0;
        owner->options.sidecars = sidecars; owner->options.sidecar_count = count;
    } else if (!same_source(&source, &owner->travel_source) ||
        sidecars != owner->options.sidecars || count != owner->options.sidecar_count)
        return fail(error, "Unified retained travel was overtaken by another Source or sidecar owner");
    ++owner->calls;
    bool okay = prune(owner, error);
    while (okay && owner->travel_cursor < UNIFIED_PEERS) {
        unified_peer *peer = owner->peers + owner->travel_cursor;
        if (!peer->server) { ++owner->travel_cursor; continue; }
        if (!peer->travel_prepared) {
            qa_unified_document *offer = NULL;
            uint32_t next = qa_unified_session_epoch(peer->session);
            if (next == UINT32_MAX) { okay = fail(error, "Unified peer wire epoch is exhausted"); break; }
            okay = application_unified_server_offer(peer->server, next + 1, sidecars, count, &offer, error);
            qa_unified_document_destroy(offer);
            if (okay) peer->travel_prepared = true;
        }
        if (okay) okay = qa_network_restart(owner->options.runtime, peer->client,
            application_unified_server_composition(peer->server), error);
        if (okay) { peer->travel_prepared = false; ++owner->travel_cursor; }
    }
    if (okay) okay = qa_unified_bootstrap_current_limits(owner->bootstrap, source.max_clients, error);
    if (okay) { owner->source = owner->travel_source; owner->travel_source = (application_unified_source){0};
        owner->traveling = false; owner->travel_cursor = 0; }
    --owner->calls; return okay;
}
bool frontend_network_unified_client(const frontend_network_unified *owner,
    qa_net_client_id *id, frontend_remote_unified **remote)
{
    qa_unified_session *session = NULL;
    if (!parent(owner) || owner->options.server || !id || !remote ||
        !qa_unified_bootstrap_client(owner->bootstrap, id, &session)) return false;
    const unified_peer *peer = owner->peers;
    if (!peer->occupied || session != peer->session || !qa_net_client_id_equal(*id, peer->client)) return false;
    *remote = peer->remote; return true;
}
bool frontend_network_unified_destroy(frontend_network_unified **slot, qa_error *error)
{
    if (!slot || !*slot) return true;
    frontend_network_unified *owner = *slot;
    if (!frontend_network_unified_idle(owner)) return fail(error, "Unified destruction requires returned actual callback owners");
    owner->closing = true;
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        unified_peer *peer = owner->peers + i;
        if (!peer->occupied) continue;
        if (qa_net_connections_get(qa_network_connections(owner->options.runtime), peer->client)) {
            qa_unified_session *installed = NULL;
            if (!qa_unified_session_find(owner->options.runtime, peer->client, &installed, error) || installed != peer->session) return false;
            if (qa_unified_session_source_retired(installed)) {
                if (peer->server && !application_unified_server_transport_retired(peer->server, installed, error)) return false;
                if (peer->remote && !frontend_remote_unified_transport_retired(peer->remote, installed, error)) return false;
            }
            if (!qa_network_detach(owner->options.runtime, peer->client, "Unified Network controller closed", error)) return false;
        }
        if (!release_peer(peer, error)) return false;
    }
    qa_unified_bootstrap_destroy(owner->bootstrap); free(owner); *slot = NULL; return true;
}
bool frontend_network_unified_close(frontend_network_unified *owner, qa_net_client_id id,
    const char *reason, qa_error *error)
{
    if (!parent(owner) || !reason || !qa_net_connections_get(qa_network_connections(owner->options.runtime), id))
        return fail(error, "Unified Source close lost its actual retained peer");
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        unified_peer *peer = owner->peers + i;
        if (peer->occupied && !peer->staging && qa_net_client_id_equal(peer->client, id))
            return qa_unified_session_close(peer->session, reason, error);
    }
    return fail(error, "Unified Source close has no installed callback owner");
}
