#include "network_unified_private.h"
#include "network_unified_save.h"
#include "internal.h"
#include "remote_unified_presentation.h"
#include "remote_unified_private.h"
#include "network_unified_client.h"
#include "qc_messages.h"
#include "../application/network_unified_private.h"
#include "qa/network_unified_save.h"

#include <stdlib.h>
#include <string.h>

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
typedef struct unified_output_receipts {
    const application_unified_output_external *base;
    frontend_qc_unified_player_receipt *qc;
} unified_output_receipts;
static bool output_receipts_current(void *context, qa_application *app,
    const application_unified_source *source, qa_net_client_id client,
    const qa_unified_session_player *player)
{
    const unified_output_receipts *receipts = context;
    return receipts && receipts->qc && receipts->qc->external.current &&
        receipts->qc->external.current(receipts->qc->external.context, app, source, client, player) &&
        (!receipts->base || (receipts->base->current &&
            receipts->base->current(receipts->base->context, app, source, client, player)));
}
static bool unified_returned(const frontend_network_unified *owner,bool checkpoint)
{
    if (!owner) return true;
    if (owner->restore_pending) return owner->calls == 0;
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
    return checkpoint ? frontend_remote_unified_checkpoint_returned(owner->options.frontend) :
        frontend_remote_unified_idle(owner->options.frontend);
}
bool frontend_network_unified_idle(const frontend_network_unified *owner)
{ return unified_returned(owner,false); }
bool frontend_network_unified_checkpoint_returned(const frontend_network_unified *owner)
{ return unified_returned(owner,true); }

uint64_t frontend_network_unified_events_after(const frontend_network_unified *owner)
{
    uint64_t next = UINT64_MAX;
    if (owner && owner->options.server) for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        const unified_peer *peer = owner->peers + i;
        application_unified_server *server = peer->server;
        if (server && (server->admitted || server->resync_pending) && !server->closed &&
            !qa_unified_session_retiring(peer->session)) {
            uint64_t retired = application_unified_server_events_retired(server);
            if (retired < next) next = retired;
        }
    }
    return next;
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

bool frontend_network_unified_release_pressure(frontend_network_unified *owner,
    uint64_t minimum, bool *released, qa_error *error)
{
    *released = false;
    if (!owner || !owner->options.server) return true;
    ++owner->calls;
    bool okay = true;
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        unified_peer *peer = owner->peers + i;
        application_unified_server *server = peer->server;
        if (!server || (!server->admitted && !server->resync_pending) || server->closed ||
            qa_unified_session_retiring(peer->session) ||
            application_unified_server_events_retired(server) != minimum) continue;
        okay = qa_network_detach(owner->options.runtime, peer->client,
            "Unified event backlog exceeded bounded tick headroom", error);
        if (okay) {
            *released = true;
            okay = prune(owner, error);
        }
        break;
    }
    --owner->calls;
    return okay;
}

static bool resync_peer(frontend_network_unified *owner, unified_peer *peer,
    uint64_t now, qa_error *error)
{
    application_unified_server *server = peer->server;
    if (!server || !server->resync_pending || server->closed ||
        qa_unified_session_retiring(peer->session)) return true;
    if (server->resync_sequence && server->admitted && qa_unified_session_active(peer->session) &&
        qa_unified_session_reliable_acknowledged(peer->session) >= qa_unified_session_required(peer->session)) {
        (void)application_unified_server_events_retired(server);
        server->resync_pending = server->resync_prepared = server->resync_started = false;
        server->resync_sequence = 0;
        return true;
    }
    if (!server->resync_started) {
        server->resync_started = true;
        server->resync_started_ns = now;
    }
    if (now >= server->resync_started_ns &&
        now - server->resync_started_ns >= UINT64_C(5000000000))
        return qa_network_detach(owner->options.runtime, peer->client,
            "Unified reliable backlog exceeded resync grace", error);
    if (server->pending.frame || server->resync_sequence) return true;
    bool ready = false, retiring = false;
    qa_error deferred = {0};
    bool okay = qa_unified_session_restart_prepare(peer->session, &ready, &retiring, &deferred);
    if (okay && (!ready || retiring)) return true;
    if (okay && !server->resync_prepared) {
        uint32_t epoch = qa_unified_session_epoch(peer->session);
        if (epoch == UINT32_MAX)
            return qa_network_detach(owner->options.runtime, peer->client,
                "Unified resync epoch exhausted", error);
        qa_unified_document *offer = NULL;
        okay = application_unified_server_offer(server, epoch + 1,
            owner->options.sidecars, owner->options.sidecar_count, &offer, &deferred);
        qa_unified_document_destroy(offer);
        if (okay) server->resync_prepared = true;
    }
    if (okay) okay = qa_unified_session_offer_ready(peer->session, server->offer, &ready, &deferred);
    if (okay && !ready) return true;
    if (okay) okay = qa_network_restart(owner->options.runtime, peer->client,
        application_unified_server_composition(server), &deferred);
    if (okay) {
        server->resync_sequence = qa_unified_session_required(peer->session);
        return true;
    }
    if (deferred.code == QA_ERROR_MEMORY) return true;
    return qa_network_detach(owner->options.runtime, peer->client,
        "Unified resync could not retain its source", error);
}

bool frontend_network_unified_admit(frontend_network_unified *owner,
    const qa_net_connect *request, bool *recognized, qa_error *error)
{
    if (!owner || !request || !recognized) return fail(error, "Missing actual Unified admission request");
    *recognized = request->protocol.kind == QA_NET_UNIFIED_1;
    if (!*recognized) return true;
    if (owner->restore_pending) return frontend_network_unified_import_admit(owner, request, error);
    if (!parent(owner)) return fail(error, "Unified admission lost its installed Network owner");
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        const unified_peer *peer = owner->peers + i;
        if (!peer->occupied) continue;
        if (!peer->staging) {
            const qa_net_client *client = qa_net_connections_get(qa_network_connections(owner->options.runtime), peer->client);
            if (!client || request->seats != client->seats) continue;
            const qa_unified_document *offer = NULL;
            if (owner->options.server) {
                application_unified_server *source_peer = peer->server;
                application_unified_source source;
                bool prepared = (owner->traveling && i == owner->travel_cursor && peer->travel_prepared) ||
                    (source_peer && source_peer->resync_pending && source_peer->resync_prepared);
                if (!prepared || !source_peer ||
                    source_peer->runtime != owner->options.runtime || source_peer->application != owner->options.frontend->application ||
                    source_peer->session != peer->session || !source_peer->bound || source_peer->closed || source_peer->entered ||
                    !application_unified_source_read(source_peer->application, &source, error) ||
                    !same_source(&source, &source_peer->offered))
                    return fail(error, "Unified restart admission lost its genuine prepared Source travel offer");
                offer = source_peer->offer;
            } else {
                const frontend_remote_unified *remote = peer->remote;
                if (!remote || !remote->bound || !remote->busy || remote->retired || remote->session != peer->session ||
                    remote->options.domain.runtime != owner->options.runtime ||
                    !qa_net_client_id_equal(remote->options.domain.client, peer->client) || !remote->recipe ||
                    remote->epoch != qa_executable_recipe_epoch(remote->recipe) || remote->transport_restarted ||
                    request->composition != *qa_executable_recipe_generation(remote->recipe))
                    return fail(error, "Unified restart admission lost its genuine received CLIENT recipe publication");
                offer = remote->offer;
            }
            return qa_unified_session_restart_admit(peer->session, owner->options.runtime, request, offer, error);
        }
        const qa_net_connect *pending = &peer->request;
        if (request->attachment != pending->attachment ||
            !qa_net_address_equal(&request->endpoint, &pending->endpoint, true) ||
            request->protocol.kind != pending->protocol.kind ||
            request->protocol.revision != pending->protocol.revision ||
            request->protocol.flags != pending->protocol.flags ||
            request->composition != pending->composition ||
            request->seat_count != 1 || request->seat_count != pending->seat_count || !request->seats ||
            request->seats[0].seat.owner != peer->binding.seat.owner ||
            request->seats[0].seat.index != peer->binding.seat.index ||
            request->seats[0].remote_index != peer->binding.remote_index) continue;
        if (request->protocol.revision || request->protocol.flags ||
            pending->seats != &peer->binding || peer->binding.remote_index ||
            peer->binding.seat.owner != owner->options.seat_owner)
            return fail(error, "Unified admission changed its staged canonical transport seat");
        if (owner->options.server) {
            application_unified_source source;
            const uint64_t *composition = application_unified_server_composition(peer->server);
            return composition && (*composition == request->composition) &&
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
    uint32_t application_seat = peer->binding.seat.index;
    bool local = server && owner->options.local_seat && owner->options.local_seat(
        owner->options.context, address, &peer->binding.seat, &application_seat);
    peer->request = (qa_net_connect){.attachment = local ? QA_NET_LOCAL_SEAT : QA_NET_REMOTE, .endpoint = *address,
        .protocol = {.kind = QA_NET_UNIFIED_1}, .seats = &peer->binding, .seat_count = 1};
    qa_unified_document *offer = NULL;
    qa_unified_session_hooks hooks = {0};
    bool okay;
    if (server) {
        okay = application_unified_server_create(owner->options.frontend->application, runtime,
            peer->binding.seat, application_seat, owner->epoch, owner->options.sidecars,
            owner->options.sidecar_count, &peer->server, &offer, error);
        if (okay) { peer->request.composition = *application_unified_server_composition(peer->server);
            hooks = application_unified_server_hooks(peer->server); }
    } else {
        frontend_remote_unified_options options = {0};
        okay = frontend_network_unified_client_options_read(owner->options.client_service, &options, error);
        if (okay && (options.domain.runtime != runtime || options.domain.client.owner ||
            options.domain.seat.owner != peer->binding.seat.owner || options.domain.seat.index != peer->binding.seat.index))
            okay = fail(error, "Unified challenge changes its actual prepared CLIENT service domain");
        if (okay) okay = frontend_remote_unified_presentation_create(owner->options.frontend, &options, &peer->remote, error);
        if (okay) hooks = frontend_remote_unified_hooks(peer->remote);
    }
    if (okay) okay = qa_unified_session_attach(runtime, &peer->request, server, token, NULL,
        &hooks, now, &peer->client, &peer->session, error);
    if (okay && !server) okay = frontend_network_unified_client_bind(owner->options.client_service,
        peer->client, peer->binding.seat, peer->remote, error);
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
qa_unified_bootstrap_hooks frontend_network_unified_bootstrap_hooks(frontend_network_unified *owner)
{ return (qa_unified_bootstrap_hooks){owner, attach}; }

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
    } else if (!options->client_service || options->client.domain.application != options->frontend->application ||
        options->client.domain.runtime != options->runtime ||
        options->client.domain.seat.owner != options->seat_owner)
        return fail(error, "Unified client lacks its actual CLIENT factory domain");
    frontend_network_unified *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating actual Unified Network controller");
    owner->options = *options; owner->epoch = 1; owner->source = source;
    if (options->server) {
        owner->world_frames = qa_unified_frame_pool_create(0, error);
        if (!owner->world_frames) { free(owner); return false; }
    }
    qa_unified_bootstrap_options bootstrap = {.server = options->server, .max_clients = capacity,
        .remote = options->remote, .hooks = {owner, attach}};
    if (!qa_unified_bootstrap_create(options->runtime, &bootstrap, &owner->bootstrap, error)) {
        qa_unified_frame_pool_destroy(&owner->world_frames); free(owner); return false;
    }
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
    bool source_ready = false, okay = true;
    if (!frontend_network_unified_step_ready(owner, &source_ready, error)) return false;
    if (!source_ready) { *waiting = true; return true; }
    if (owner->options.server) {
        ++owner->calls;
        for (size_t i = 0; okay && i < UNIFIED_PEERS; ++i)
            okay = resync_peer(owner, owner->peers + i, now, error);
        if (okay) okay = prune(owner, error);
        --owner->calls;
    }
    if (!okay || (owner->options.server && !frontend_network_unified_travel(owner,
        owner->options.sidecars, owner->options.sidecar_count, error))) return false;
    if (owner->traveling) { *waiting = true; return true; }
    ++owner->calls;
    okay = prune(owner, error);
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
    if (owner->traveling) return true;
    application_unified_source source;
    if (!application_unified_source_read(owner->options.frontend->application, &source, error) ||
        !same_source(&source, &owner->source))
        return fail(error, "Unified input precedes its actual Source travel publication");
    if (owner->frame_boundary && source.frame.number > owner->frame_before)
        return fail(error, "Unified retained output must finish before another Source frame");
    ++owner->calls; bool okay = prune(owner, error);
    for (size_t i = 0; okay && i < UNIFIED_PEERS; ++i)
        if (owner->peers[i].server && !qa_unified_session_retiring(owner->peers[i].session)) {
            okay = application_unified_server_pre_frame(owner->peers[i].server, error);
            if (okay) owner->peers[i].frame_published = false;
        }
    if (okay) { owner->frame_before = source.frame.number; owner->frame_boundary = true; }
    --owner->calls; return okay;
}
bool frontend_network_unified_step_ready(frontend_network_unified *owner, bool *ready, qa_error *error)
{
    if (!ready || !parent(owner) || !frontend_network_unified_idle(owner))
        return fail(error, "Unified Source step admission requires its returned Network and publication owners");
    *ready = true;
    if (owner->options.server) {
        ++owner->calls;
        bool okay = true;
        for (size_t i = 0; okay && i < UNIFIED_PEERS; ++i)
            if (owner->peers[i].server)
                okay = application_unified_server_source_drop_finish(owner->peers[i].server, error);
        --owner->calls;
        if (!okay) return false;
    }
    if (!owner->options.server || owner->traveling || !owner->frame_boundary) return true;
    application_unified_source source;
    if (!application_unified_source_read(owner->options.frontend->application, &source, error)) return false;
    if (source.frame.number <= owner->frame_before) return true;
    if (!frontend_network_unified_publish(owner, NULL, error)) return false;
    *ready = !owner->frame_boundary;
    return true;
}
bool frontend_network_unified_publish(frontend_network_unified *owner,
    const application_unified_output_external *external, qa_error *error)
{
    if (!parent(owner) || !frontend_network_unified_idle(owner)) return fail(error, "Unified publication requires returned actual Source output");
    if (!owner->options.server) return true;
    if (owner->traveling) return true;
    application_unified_source source;
    if (!application_unified_source_read(owner->options.frontend->application, &source, error) ||
        !same_source(&source, &owner->source))
        return fail(error, "Unified output precedes its actual Source travel publication");
    if (!owner->frame_boundary || source.frame.phase != QA_FRAME_EXIT || source.frame.number <= owner->frame_before) return true;
    ++owner->calls; bool okay = prune(owner, error);
    qa_unified_world_frame *world = NULL;
    for (size_t i = 0; okay && i < UNIFIED_PEERS; ++i) {
        unified_peer *peer = owner->peers + i;
        if (peer->server && qa_unified_session_retiring(peer->session) &&
            application_unified_server_publication_complete(peer->server)) {
            continue;
        }
        if (peer->server && !peer->frame_published) {
            frontend_qc_unified_player_receipt receipt = {0};
            unified_output_receipts receipts = {.base = external, .qc = &receipt};
            application_unified_output_external observed = external ? *external : (application_unified_output_external){0};
            const application_unified_output_external *actual_external = external;
            if (peer->server->admitted && peer->server->preparing_frame &&
                !peer->server->resync_pending && !peer->server->pending_capture) {
                if (!world && !application_unified_output_world(owner->options.frontend->application,
                    &source, owner->world_frames, &world, error)) {
                    okay = false;
                    break;
                }
                qa_unified_session_player player; bool present = false;
                okay = application_unified_player_read(owner->options.frontend->application,
                    peer->client, peer->binding.seat, &player, error) &&
                    frontend_qc_messages_unified_player_read(owner->options.frontend->qc_messages,
                        owner->options.frontend->application, &source, peer->client, &player,
                        &receipt, &present, error);
                if (okay && present) {
                    if (external && external->player)
                        okay = fail(error, "Unified output has competing actual QC decoder receipts");
                    if (okay) {
                        observed.context = &receipts; observed.current = output_receipts_current;
                        observed.player = &receipt.external; actual_external = &observed;
                    }
                }
            }
            if (okay) okay = application_unified_server_publish(peer->server, world, actual_external, error);
            if (okay) peer->frame_published = application_unified_server_publication_complete(peer->server);
        }
    }
    qa_unified_world_frame_destroy(world);
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
        if (sidecars != owner->restored_sidecars) { free(owner->restored_sidecars); owner->restored_sidecars = NULL; }
        owner->options.sidecars = sidecars; owner->options.sidecar_count = count;
    } else if (!same_source(&source, &owner->travel_source) ||
        sidecars != owner->options.sidecars || count != owner->options.sidecar_count)
        return fail(error, "Unified retained travel was overtaken by another Source or sidecar owner");
    ++owner->calls;
    bool okay = prune(owner, error);
    while (okay && owner->travel_cursor < UNIFIED_PEERS) {
        unified_peer *peer = owner->peers + owner->travel_cursor;
        if (!peer->server) { ++owner->travel_cursor; continue; }
        bool ready = false, retiring = false;
        okay = qa_unified_session_restart_prepare(peer->session, &ready, &retiring, error);
        if (!okay || !ready) break;
        if (retiring) { peer->travel_prepared = false; ++owner->travel_cursor; continue; }
        if (!peer->travel_prepared) {
            qa_unified_document *offer = NULL;
            uint32_t next = qa_unified_session_epoch(peer->session);
            if (next == UINT32_MAX) { okay = fail(error, "Unified peer wire epoch is exhausted"); break; }
            qa_error prepared = {0};
            okay = application_unified_server_offer(peer->server, next + 1, sidecars, count, &offer, &prepared);
            qa_unified_document_destroy(offer);
            if (!okay && prepared.code == QA_ERROR_MEMORY) { okay = true; break; }
            if (!okay && error) *error = prepared;
            if (okay) peer->travel_prepared = true;
        }
        if (okay) okay = qa_unified_session_offer_ready(peer->session, peer->server->offer, &ready, error);
        if (!okay || !ready) break;
        qa_error queued = {0};
        if (okay) okay = qa_network_restart(owner->options.runtime, peer->client,
            application_unified_server_composition(peer->server), &queued);
        if (!okay && queued.code == QA_ERROR_MEMORY) { okay = true; break; }
        if (!okay && error) *error = queued;
        if (okay) { peer->travel_prepared = false; ++owner->travel_cursor; }
    }
    bool complete = owner->travel_cursor == UNIFIED_PEERS;
    if (okay && complete) okay = qa_unified_bootstrap_current_limits(owner->bootstrap, source.max_clients, error);
    if (okay && complete) { owner->source = owner->travel_source; owner->travel_source = (application_unified_source){0};
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
    if (owner->restore_pending) return frontend_network_unified_restore_dispose(slot, error);
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
    qa_unified_bootstrap_destroy(owner->bootstrap);
    qa_unified_frame_pool_destroy(&owner->world_frames);
    free(owner->restored_sidecars); free(owner); *slot = NULL; return true;
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
bool frontend_network_unified_source_drop(frontend_network_unified *owner, qa_actor_owner source,
    uint32_t slot, const char *reason, bool *matched, qa_error *error)
{
    if (!matched || !reason || !source || !parent(owner) || !owner->options.server ||
        owner->restore_pending || !frontend_network_unified_idle(owner))
        return fail(error, "Unified Source DROP requires its actual returned transport controller");
    *matched = false;
    qa_actor_id actor = {0};
    qa_net_client_id id = {0};
    qa_net_seat_id seat = {0};
    bool present = false;
    if (!application_unified_source_drop_recipient(owner->options.frontend->application, source,
        slot, reason, &actor, &id, &seat, &present, error)) return false;
    if (!present) return true;
    unified_peer *recipient = NULL;
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        unified_peer *peer = owner->peers + i;
        application_unified_server *server = peer->server;
        if (!server || !qa_net_client_id_equal(peer->client, id)) continue;
        if (recipient) return fail(error, "Unified Source DROP aliases two retained physical recipients");
        qa_unified_session *installed = NULL;
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(owner->options.runtime), peer->client);
        if (!peer->occupied || peer->staging || !server->admitted_receipt ||
            !qa_actor_id_equal(server->admitted_player.actor, actor) ||
            peer->binding.seat.owner != seat.owner || peer->binding.seat.index != seat.index ||
            server->seat.owner != seat.owner || server->seat.index != seat.index ||
            !client || client->seat_count != 1 || !client->seats ||
            client->seats[0].seat.owner != peer->binding.seat.owner ||
            client->seats[0].seat.index != peer->binding.seat.index ||
            server->application != owner->options.frontend->application || server->runtime != owner->options.runtime ||
            !qa_net_client_id_equal(server->client, peer->client) || server->session != peer->session ||
            !qa_unified_session_find(owner->options.runtime, peer->client, &installed, error) || installed != peer->session)
            return fail(error, "Unified Source DROP lost its installed full recipient and seat");
        recipient = peer;
    }
    if (!recipient) {
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(owner->options.runtime), id);
        if (client && client->protocol.kind != QA_NET_UNIFIED_1) return true;
        return fail(error, "Unified Source DROP has no installed canonical callback recipient");
    }
    ++owner->calls;
    bool okay = application_unified_server_source_drop(recipient->server, source, slot, reason, matched, error);
    --owner->calls;
    return okay;
}
