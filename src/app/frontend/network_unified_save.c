#include "network_unified_save.h"
#include "network_unified_private.h"
#include "internal.h"
#include "../application/network_unified_save.h"
#include "../application/network_unified_private.h"
#include "../application/unified_save_internal.h"
#include "qa/network_unified_save.h"
#include "qa/map_sidecars.h"
#include "network_unified_client.h"
#include "remote_unified_save.h"
#include "remote_unified_presentation.h"

#include <stdlib.h>
#include <string.h>

static bool bad(qa_error *e, const char *message)
{ return frontend_fail(e, QA_ERROR_FORMAT, message); }
bool frontend_network_unified_server(const frontend_network_unified *owner)
{ return owner && owner->options.server; }

static bool request_equal(const qa_net_connect *a, const qa_net_connect *b)
{
    return a && b && a->attachment == b->attachment &&
        a->protocol.kind == QA_NET_UNIFIED_1 && b->protocol.kind == QA_NET_UNIFIED_1 &&
        !a->protocol.flags && !a->protocol.revision && !b->protocol.flags && !b->protocol.revision &&
        a->seat_count == 1 && b->seat_count == 1 && a->seats && b->seats &&
        a->seats[0].seat.owner == b->seats[0].seat.owner && a->seats[0].seat.index == b->seats[0].seat.index &&
        !a->seats[0].remote_index && !b->seats[0].remote_index &&
        qa_net_address_equal(&a->endpoint, &b->endpoint, true) && (a->composition == b->composition);
}
bool frontend_network_unified_import_admit(frontend_network_unified *owner,
    const qa_net_connect *request, qa_error *e)
{
    if (!owner || !owner->restore_pending || !request) return bad(e, "Missing decoded actual Unified admission");
    size_t found = 0;
    for (size_t i = 0; i < UNIFIED_PEERS; ++i)
        if (owner->peers[i].occupied && request_equal(request, &owner->peers[i].request)) ++found;
    return found == 1 || bad(e, "Imported connection differs from its complete retained handshake seat receipt");
}

static bool inventory(const frontend_network_unified *owner, qa_network_runtime *runtime,
    bool installed, qa_error *e)
{
    uint32_t cursor = 0; const qa_net_client *client;
    size_t count = 0, held = 0;
    while (qa_net_connections_next(qa_network_connections(runtime), &cursor, &client)) {
        size_t matches = 0;
        for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
            const unified_peer *peer = owner->peers + i;
            if (!peer->occupied || !qa_net_client_id_equal(peer->client, client->id)) continue;
            qa_net_connect actual = {.attachment = client->attachment, .endpoint = client->endpoint,
                .protocol = client->protocol, .seats = client->seats, .seat_count = client->seat_count,
                .composition = client->composition};
            qa_net_connect retained = peer->request;
            /* A successful Source publication changes the real generic
             * composition. Import still qualifies the exact saved receipt. */
            if (!owner->restore_pending) retained.composition = client->composition;
            if (!request_equal(&retained, &actual)) continue;
            qa_unified_session *session = NULL;
            if (installed && (!qa_unified_session_find(runtime, client->id, &session, e) || session != peer->session)) return false;
            ++matches;
        }
        if (matches != 1) return bad(e, "Unified controller differs from its genuine complete connection inventory");
        ++count;
    }
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) if (owner->peers[i].occupied) ++held;
    return held == count || bad(e, "Unified controller retains an absent real connection");
}
static bool server_children(const frontend_network_unified *owner, qa_network_runtime *runtime,
    const application_unified_source *source, qa_error *e)
{
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        const unified_peer *peer = owner->peers + i;
        if (!peer->occupied) continue;
        application_unified_server *child = peer->server;
        if (!child || child->application != owner->options.frontend->application || child->runtime != runtime ||
            child->session != peer->session || child->restore_pending || !child->bound || child->closed ||
            !qa_net_client_id_equal(child->client, peer->client) ||
            child->seat.owner != peer->binding.seat.owner || child->seat.index != peer->binding.seat.index ||
            !qa_unified_session_source_ready(peer->session, e))
            return bad(e, "Unified Source child differs from its real installed transport peer");
        uint32_t wire_epoch = qa_unified_session_epoch(peer->session);
        bool retiring = qa_unified_session_retiring(peer->session);
        if (peer->travel_prepared ? (!owner->traveling || i != owner->travel_cursor || wire_epoch == UINT32_MAX ||
            child->epoch != wire_epoch + 1) : (child->epoch != wire_epoch &&
                retiring && wire_epoch != UINT32_MAX && child->epoch != wire_epoch + 1))
            return bad(e, "Unified travel differs from its actual prepared offer continuation");
        if (owner->traveling && i < owner->travel_cursor && !retiring &&
            application_unified_save_source_obsolete(source, &child->offered))
            return bad(e, "Unified completed travel peer retains an obsolete offer");
    }
    return true;
}
static bool checkpoint_returned(const frontend_network_unified *owner)
{
    return owner && (owner->options.frontend->capture || owner->options.frontend->source_restoring) ?
        frontend_network_unified_checkpoint_returned(owner) : frontend_network_unified_idle(owner);
}

bool frontend_network_unified_imported(const frontend_network_unified *owner)
{ return owner && owner->imported && !owner->restore_pending; }
bool frontend_network_unified_qualified(const frontend_network_unified *owner,
    qa_network_runtime *runtime, bool complete, qa_error *e)
{
    if (owner && !owner->options.server) {
        if (!runtime || owner->closing || owner->calls || !qa_network_callbacks_idle(runtime))
            return bad(e, "Unified CLIENT controller changed its genuine runtime owner");
        if (owner->restore_pending) {
            if (complete || (owner->options.runtime && owner->options.runtime != runtime) ||
                !owner->bootstrap_import.size) return bad(e, "Unified CLIENT cold graph is not complete");
            for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
                const unified_peer *peer = owner->peers + i;
                if (peer->occupied && (i || !peer->source_import.size || peer->staging))
                    return bad(e, "Unified CLIENT import lost its actual retained replica prefix");
            }
            return true;
        }
        if (owner->options.runtime != runtime || !owner->options.client_service)
            return bad(e, "Unified CLIENT controller lost its actual programme owner");
        frontend_remote_unified_options client = {0};
        bool server; uint32_t maximum; qa_net_address remote;
        if (!frontend_network_unified_client_idle(owner->options.client_service) ||
            !(frontend_network_unified_client_retired(owner->options.client_service) ?
                frontend_network_unified_client_retirement_options_read(owner->options.client_service, &client, e) :
                frontend_network_unified_client_options_read(owner->options.client_service, &client, e)) ||
            client.domain.runtime != runtime || client.domain.application != owner->options.frontend->application ||
            !checkpoint_returned(owner) ||
            !inventory(owner, runtime, true, e) ||
            !qa_unified_bootstrap_domain(owner->bootstrap, &server, &maximum, &remote, e) ||
            server || maximum != 1 || !qa_net_address_equal(&remote, &owner->options.remote, true))
            return bad(e, "Unified CLIENT controller lost its actual programme or handshake");
        for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
            const unified_peer *peer = owner->peers + i;
            if (!peer->occupied) continue;
            if (i || peer->server || !peer->remote ||
                !qa_net_client_id_equal(client.domain.client, peer->client) ||
                !frontend_remote_unified_qualified(peer->remote, runtime,
                    qa_net_connections_get(qa_network_connections(runtime), peer->client), e)) return false;
        }
        return true;
    }
    application_unified_source source;
    if (!owner || !runtime || owner->closing || owner->calls || !owner->options.server ||
        !qa_network_callbacks_idle(runtime) || !application_unified_save_source_read(
            owner->options.frontend->application, &source, e) ||
        (!application_unified_save_source_obsolete(&source, &owner->source) &&
            (source.owner != owner->source.owner || source.launch != owner->source.launch ||
             source.session != owner->source.session || source.world != owner->source.world ||
             source.publication != owner->source.publication || source.map_revision != owner->source.map_revision ||
             source.max_clients != owner->source.max_clients)) ||
        (owner->traveling && (!application_unified_save_source_obsolete(&source, &owner->source) ||
            application_unified_save_source_obsolete(&source, &owner->travel_source) ||
            source.owner != owner->travel_source.owner || source.launch != owner->travel_source.launch ||
            source.session != owner->travel_source.session || source.world != owner->travel_source.world ||
            source.publication != owner->travel_source.publication || source.map_revision != owner->travel_source.map_revision ||
            source.max_clients != owner->travel_source.max_clients || owner->travel_cursor > UNIFIED_PEERS || owner->frame_boundary)))
        return bad(e, "Unified controller changed its actual Source graph or returned runtime");
    if (owner->restore_pending) {
        if (complete || (owner->options.runtime && owner->options.runtime != runtime) ||
            !owner->bootstrap_import.size) return bad(e, "Unified controller cold graph is not complete");
        for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
            const unified_peer *peer = owner->peers + i;
            if (peer->occupied && (!peer->source_import.size || peer->staging))
                return bad(e, "Unified controller import loses its actual retained Source child");
        }
        return true;
    }
    if (owner->options.runtime != runtime || !checkpoint_returned(owner) ||
        !inventory(owner, runtime, true, e)) return false;
    bool server; uint32_t maximum; qa_net_address remote;
    if (!qa_unified_bootstrap_domain(owner->bootstrap, &server, &maximum, &remote, e) ||
        !server || maximum != owner->source.max_clients) return bad(e, "Unified handshake lost its real Source domain");
    return server_children(owner, runtime, &source, e);
}
bool frontend_network_unified_restore_dispose(frontend_network_unified **slot, qa_error *e)
{
    if (!slot || !*slot) return true;
    frontend_network_unified *owner = *slot;
    if (!owner->restore_pending || owner->calls ||
        (owner->options.runtime && !qa_network_callbacks_idle(owner->options.runtime)))
        return frontend_fail(e, QA_ERROR_ARGUMENT, "Quiet controller cleanup requires its unpublished import");
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        unified_peer *peer = owner->peers + i;
        qa_unified_session *session = NULL;
        if (owner->options.runtime && qa_net_connections_get(qa_network_connections(owner->options.runtime), peer->client) &&
            qa_unified_session_find(owner->options.runtime, peer->client, &session, NULL)) {
            if (!qa_unified_session_source_retired(session))
                return frontend_fail(e, QA_ERROR_ARGUMENT, "Imported controller cleanup cannot revoke live Source custody");
            if (peer->server && !peer->server->restore_pending &&
                !application_unified_server_transport_retired(peer->server, session, e)) return false;
            if (peer->remote && !frontend_remote_unified_restore_pending(peer->remote) &&
                !frontend_remote_unified_transport_retired(peer->remote, session, e)) return false;
            if (!qa_network_detach(owner->options.runtime, peer->client, "Unpublished Unified import disposed", e)) return false;
        }
        if (peer->server) {
            bool okay = peer->server->restore_pending ? application_unified_server_restore_dispose(&peer->server, e) :
                application_unified_server_destroy(peer->server, e);
            if (!okay) return false;
            peer->server = NULL;
        }
        if (peer->remote) {
            if (frontend_remote_unified_restore_pending(peer->remote)) {
                if (!frontend_remote_unified_restore_dispose(&peer->remote, e)) return false;
            } else {
                if (!frontend_remote_unified_destroy(&peer->remote, e)) return false;
            }
        }
        qa_buffer_free(&peer->source_import);
    }
    qa_unified_bootstrap_destroy(owner->bootstrap);
    qa_unified_frame_pool_destroy(&owner->world_frames);
    qa_buffer_free(&owner->bootstrap_import);
    free(owner->restored_sidecars); free(owner); *slot = NULL; return true;
}
