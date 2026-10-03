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
#include "remote_unified_presentation_save.h"

#include <stdlib.h>
#include <string.h>

static bool bad(qa_error *e, const char *message)
{ return frontend_fail(e, QA_ERROR_FORMAT, message); }
bool frontend_network_unified_server(const frontend_network_unified *owner)
{ return owner && owner->options.server; }
static bool address(qa_source_save_io *io, qa_net_address *a)
{
    uint32_t kind = (uint32_t)a->kind;
    if (!qa_source_save_u32(io, &kind) || !qa_source_save_u16(io, &a->port)) return false;
    a->kind = (qa_net_address_kind)kind;
    if (a->kind == QA_NET_IPV4) return qa_source_save_bytes(io, a->host.ipv4, 4);
    if (a->kind == QA_NET_IPV6) return qa_source_save_bytes(io, a->host.ipv6.bytes, 16) &&
        qa_source_save_u32(io, &a->host.ipv6.scope);
    if (a->kind == QA_NET_LOOPBACK) {
        const char *name = a->host.loopback;
        if (!qa_source_save_text(io, &name) || !name || !*name || strlen(name) >= sizeof(a->host.loopback)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) memcpy(a->host.loopback, name, strlen(name) + 1);
        return true;
    }
    return false;
}
static bool request_equal(const qa_net_connect *a, const qa_net_connect *b)
{
    return a && b && a->attachment == b->attachment && a->attachment == QA_NET_REMOTE &&
        a->protocol.kind == QA_NET_UNIFIED_1 && b->protocol.kind == QA_NET_UNIFIED_1 &&
        !a->protocol.flags && !a->protocol.revision && !b->protocol.flags && !b->protocol.revision &&
        a->seat_count == 1 && b->seat_count == 1 && a->seats && b->seats &&
        a->seats[0].seat.owner == b->seats[0].seat.owner && a->seats[0].seat.index == b->seats[0].seat.index &&
        !a->seats[0].remote_index && !b->seats[0].remote_index &&
        qa_net_address_equal(&a->endpoint, &b->endpoint, true) && qa_sha256_equal(&a->composition, &b->composition);
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
static bool client_request(const unified_peer *peer, const qa_net_client *client)
{
    qa_net_connect request = {.attachment = client->attachment, .endpoint = client->endpoint,
        .protocol = client->protocol, .seats = client->seats, .seat_count = client->seat_count,
        .composition = client->composition};
    return qa_net_client_id_equal(peer->client, client->id) && request_equal(&peer->request, &request);
}
static bool sidecars(qa_source_save_io *io, frontend_network_unified *owner)
{
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    size_t count = owner->options.sidecar_count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(qa_recipe_sidecar))) return false;
    if (!count) return true;
    const qa_map_sidecars *actual = qa_application_map_sidecars(owner->options.frontend->application);
    if (!actual || !qa_map_sidecars_current(actual) || count != qa_map_sidecars_count(actual)) return false;
    qa_product_id product = qa_map_sidecars_product(actual);
    const qa_product *catalog_product = qa_catalog_product(qa_map_sidecars_catalog(actual), product);
    if (!catalog_product || !catalog_product->identity) return false;
    if (!writing) {
        owner->restored_sidecars = calloc(count, sizeof(*owner->restored_sidecars));
        if (!owner->restored_sidecars) return frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring actual sidecar assertion rows");
        owner->options.sidecars = owner->restored_sidecars; owner->options.sidecar_count = count;
    }
    if (!owner->options.sidecars) return false;
    for (size_t i = 0; i < count; ++i) {
        const qa_map_sidecar *row = qa_map_sidecars_at(actual, i);
        if (!row || !row->path) return false;
        const char *content = catalog_product->identity, *path = row->path;
        bool present = row->resource != NULL;
        if (writing && (owner->options.sidecars[i].product != product || !owner->options.sidecars[i].path ||
            strcmp(owner->options.sidecars[i].path, path) || owner->options.sidecars[i].resource != row->resource)) return false;
        if (!qa_source_save_text(io, &content) || !content || strcmp(content, catalog_product->identity) ||
            !qa_source_save_text(io, &path) || !path || strcmp(path, row->path) ||
            !qa_source_save_bool(io, &present) || present != (row->resource != NULL)) return false;
        if (present) {
            const qa_sha256_digest *actual_digest = qa_resource_digest(row->resource);
            if (!actual_digest) return false;
            qa_sha256_digest digest = *actual_digest;
            uint64_t size = qa_resource_bytes(row->resource).size;
            if (!qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes)) ||
                !qa_sha256_equal(&digest, actual_digest) || !qa_source_save_u64(io, &size) ||
                size != qa_resource_bytes(row->resource).size) return false;
        }
        if (!writing) owner->restored_sidecars[i] = (qa_recipe_sidecar){product, row->path, row->resource};
    }
    return true;
}
static bool peer_fields(qa_source_save_io *io, unified_peer *peer, size_t index,
    frontend_network_unified *owner, uint64_t connection_owner)
{
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    const qa_net_client *actual = writing ? qa_net_connections_get(
        qa_network_connections(owner->options.runtime), peer->client) : NULL;
    if (writing && (!actual || peer->staging ||
        (owner->options.server ? (!peer->server || peer->remote) : (peer->server || !peer->remote || index)) ||
        !peer->session || actual->protocol.kind != QA_NET_UNIFIED_1 || actual->seat_count != 1 || !actual->seats)) return false;
    qa_net_client_id id = peer->client;
    qa_net_seat_binding binding = writing ? actual->seats[0] : peer->binding;
    qa_net_address endpoint = writing ? actual->endpoint : peer->request.endpoint;
    qa_sha256_digest composition = writing ? actual->composition : peer->request.composition;
    if (!qa_source_save_u64(io, &id.owner) || id.owner != connection_owner ||
        !qa_source_save_u64(io, &id.generation) || !id.generation || !qa_source_save_u32(io, &id.slot) ||
        !qa_source_save_u64(io, &binding.seat.owner) || binding.seat.owner != owner->options.seat_owner ||
        !qa_source_save_u32(io, &binding.seat.index) ||
        binding.seat.index != (owner->options.server ? owner->options.remote_seat_base + (uint32_t)index :
            owner->options.client.domain.seat.index) ||
        !qa_source_save_u32(io, &binding.remote_index) || binding.remote_index || !address(io, &endpoint) ||
        !qa_source_save_bytes(io, composition.bytes, sizeof(composition.bytes)) ||
        !qa_source_save_bool(io, &peer->frame_published) || !qa_source_save_bool(io, &peer->travel_prepared) ||
        (peer->travel_prepared && (!owner->options.server || !owner->traveling || index != owner->travel_cursor))) return false;
    if (!writing) {
        peer->client = id; peer->binding = binding;
        peer->request = (qa_net_connect){.attachment = QA_NET_REMOTE, .endpoint = endpoint,
            .protocol = {.kind = QA_NET_UNIFIED_1}, .seats = &peer->binding, .seat_count = 1, .composition = composition};
    }
    qa_buffer source = {0};
    bool okay = !writing || (owner->options.server ?
        application_unified_server_checkpoint(peer->server, &source, io->error) :
        frontend_remote_unified_checkpoint(peer->remote,
            qa_application_content_graph_read(owner->options.frontend->application), &source, io->error));
    if (okay) okay = application_unified_save_blob(io, &source) && source.size;
    if (okay && !writing) { peer->source_import = source; source = (qa_buffer){0}; }
    qa_buffer_free(&source);
    return okay;
}
static bool fields(qa_source_save_io *io, frontend_network_unified *owner,
    uint64_t connection_owner, const application_unified_source *actual)
{
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    bool server = owner->options.server;
    uint64_t seat_owner = owner->options.seat_owner;
    uint32_t seat_base = owner->options.remote_seat_base;
    char magic[4] = {'Q','U','F','H'};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QUFH", sizeof(magic)) ||
        !qa_source_save_bool(io, &server)) return false;
    if (server != owner->options.server)
        return bad(io->error, "Unified continuation changes its actual HOST or CLIENT role");
    if (!qa_source_save_u64(io, &seat_owner) || seat_owner != owner->options.seat_owner ||
        !qa_source_save_u32(io, &seat_base) || seat_base > UINT32_MAX - (UNIFIED_PEERS - 1) ||
        (server && seat_base != owner->options.remote_seat_base)) return false;
    if (!writing && !server) owner->options.remote_seat_base = seat_base;
    if (server) {
        application_unified_source current = *actual;
        if (!sidecars(io, owner) ||
            !application_unified_save_source(io, owner->options.frontend->application, actual, &current, false) ||
            !application_unified_save_retained_source(io, owner->options.frontend->application, actual, &owner->source) ||
            !qa_source_save_bool(io, &owner->traveling) ||
            !qa_source_save_count(io, &owner->travel_cursor, UNIFIED_PEERS)) return false;
        if (owner->traveling) {
            if (!application_unified_save_retained_source(io, owner->options.frontend->application, actual,
                &owner->travel_source) || application_unified_save_source_obsolete(actual, &owner->travel_source) ||
                !application_unified_save_source_obsolete(actual, &owner->source)) return false;
        } else if (owner->travel_cursor) return false;
    } else {
        qa_net_address remote = owner->options.remote;
        uint32_t physical = owner->options.client.domain.physical_seat;
        uint32_t seat = owner->options.client.domain.seat.index;
        if (!address(io, &remote) ||
            (writing && !qa_net_address_equal(&remote, &owner->options.remote, true)) ||
            !qa_source_save_u32(io, &physical) || physical >= owner->options.frontend->options.seats ||
            !qa_source_save_u32(io, &seat)) return false;
        if (!writing) {
            owner->options.remote = remote;
            owner->options.client.domain.physical_seat = physical;
            owner->options.client.domain.seat = (qa_net_seat_id){owner->options.seat_owner, seat};
        }
    }
    if (!qa_source_save_u32(io, &owner->epoch) || !owner->epoch ||
        !qa_source_save_u64(io, &owner->frame_before) || (server ?
            (!application_unified_save_source_obsolete(actual, &owner->source) && owner->frame_before > actual->frame.number) :
            owner->frame_before != 0) ||
        !qa_source_save_bool(io, &owner->frame_boundary) || ((!server || owner->traveling) && owner->frame_boundary)) return false;
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        unified_peer *peer = owner->peers + i;
        if (!qa_source_save_bool(io, &peer->occupied)) return false;
        if (peer->occupied && !server && i) return false;
        if (peer->occupied && !peer_fields(io, peer, i, owner, connection_owner)) return false;
        for (size_t j = 0; peer->occupied && j < i; ++j)
            if (owner->peers[j].occupied && qa_net_client_id_equal(peer->client, owner->peers[j].client)) return false;
    }
    qa_buffer bootstrap = {0};
    bool okay = !writing || qa_unified_bootstrap_checkpoint(owner->bootstrap, &bootstrap, io->error);
    if (okay) okay = application_unified_save_blob(io, &bootstrap) && bootstrap.size;
    if (okay && !writing) { owner->bootstrap_import = bootstrap; bootstrap = (qa_buffer){0}; }
    qa_buffer_free(&bootstrap);
    return okay;
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
                !(retiring && wire_epoch != UINT32_MAX && child->epoch == wire_epoch + 1)))
            return bad(e, "Unified travel differs from its actual prepared offer continuation");
        if (owner->traveling && i < owner->travel_cursor && !retiring &&
            application_unified_save_source_obsolete(source, &child->offered))
            return bad(e, "Unified completed travel peer retains an obsolete offer");
    }
    return true;
}
bool frontend_network_unified_checkpoint(const frontend_network_unified *owner, qa_buffer *out, qa_error *e)
{
    application_unified_source source = {0};
    if (!owner || owner->restore_pending || owner->closing ||
        !out || out->data || out->size || !frontend_network_unified_idle(owner) ||
        (owner->options.server && !application_unified_save_source_read(owner->options.frontend->application, &source, e)))
        return frontend_fail(e, QA_ERROR_ARGUMENT, "Unified checkpoint requires returned actual Source owners and children");
    uint64_t connection_owner = owner->options.seat_owner;
    frontend_network_unified copy = *owner;
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) if (copy.peers[i].occupied) {
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(owner->options.runtime), copy.peers[i].client);
        if (!client || client->seat_count != 1 || !client->seats) return bad(e, "Unified host retains an absent canonical seat");
        copy.peers[i].binding = client->seats[0];
        copy.peers[i].request = (qa_net_connect){.attachment = client->attachment, .endpoint = client->endpoint,
            .protocol = client->protocol, .seats = &copy.peers[i].binding, .seat_count = 1, .composition = client->composition};
    }
    qa_source_save_io io = {0};
    bool okay = frontend_network_unified_qualified(owner, owner->options.runtime, true, e) &&
        inventory(&copy, owner->options.runtime, true, e) &&
        qa_source_save_writer(&io, qa_application_session(owner->options.frontend->application), e) &&
        fields(&io, &copy, connection_owner, owner->options.server ? &source : NULL) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return okay;
}
bool frontend_network_unified_restore_prepare(const frontend_network_unified_options *options,
    uint64_t connection_owner, qa_bytes bytes, frontend_network_unified **out, qa_error *e)
{
    application_unified_source source = {0};
    if (!options || !options->frontend || !options->frontend->application ||
        !options->current || !options->seat_owner || connection_owner != options->seat_owner ||
        options->remote_seat_base > UINT32_MAX - (UNIFIED_PEERS - 1) || !out || *out ||
        (options->server && !application_unified_save_source_read(options->frontend->application, &source, e)))
        return frontend_fail(e, QA_ERROR_ARGUMENT, "Unified import requires its actual candidate Source namespace");
    frontend_network_unified *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(e, QA_ERROR_MEMORY, "Decoding actual Unified host continuation");
    owner->options = *options; owner->options.runtime = NULL; owner->options.sidecars = NULL; owner->options.sidecar_count = 0;
    if (options->server) owner->source = source;
    owner->options.client.domain.runtime = NULL; owner->options.client_service = NULL; owner->restore_pending = true;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, qa_application_session(options->frontend->application), bytes, e) &&
        fields(&io, owner, connection_owner, options->server ? &source : NULL) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!okay) { frontend_network_unified_restore_dispose(&owner, NULL); return false; }
    *out = owner; return true;
}
bool frontend_network_unified_restore_hooks(frontend_network_unified *owner, qa_network_runtime *runtime,
    const qa_net_client *client, qa_unified_session_hooks *hooks, qa_error *e)
{
    if (!owner || !owner->restore_pending || !runtime || !client || !hooks ||
        (owner->options.runtime && owner->options.runtime != runtime))
        return frontend_fail(e, QA_ERROR_ARGUMENT, "Unified hooks require their genuine candidate runtime");
    owner->options.runtime = runtime;
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        unified_peer *peer = owner->peers + i;
        if (!peer->occupied || !client_request(peer, client)) continue;
        if (peer->server || peer->remote || !peer->source_import.size) return bad(e, "Unified Source owner was imported twice");
        if (owner->options.server) {
            if (!application_unified_server_restore((qa_bytes){peer->source_import.data, peer->source_import.size},
                owner->options.frontend->application, runtime, client, &peer->server, e)) return false;
            *hooks = application_unified_server_hooks(peer->server);
        } else {
            if (!owner->options.client_service || owner->options.client.domain.runtime != runtime ||
                !qa_net_client_id_equal(owner->options.client.domain.client, client->id) ||
                !frontend_remote_unified_presentation_restore_prefix(owner->options.frontend, &owner->options.client,
                    client, qa_application_content_graph_read(owner->options.frontend->application),
                    (qa_bytes){peer->source_import.data, peer->source_import.size}, &peer->remote, e)) return false;
            *hooks = frontend_remote_unified_hooks(peer->remote);
        }
        return true;
    }
    return bad(e, "Unified Source resolver has no actual retained connection receipt");
}
bool frontend_network_unified_restore_client_service(frontend_network_unified *owner,
    frontend_network_unified_client_service *service, qa_error *e)
{
    frontend_network_unified_client_view physical;
    frontend_remote_unified_options client = {0};
    if (!owner || !owner->restore_pending || owner->options.server || !service ||
        (owner->options.client_service && owner->options.client_service != service) ||
        !frontend_network_unified_client_metadata_read(service, &physical, e) ||
        !frontend_network_unified_client_import_options_read(service, &client, e) ||
        client.domain.application != owner->options.frontend->application || !client.domain.runtime ||
        (owner->options.runtime && owner->options.runtime != client.domain.runtime) ||
        !qa_net_address_equal(&physical.remote, &owner->options.remote, true) ||
        physical.seat.owner != owner->options.seat_owner ||
        physical.seat.index != owner->options.client.domain.seat.index ||
        client.domain.physical_seat != owner->options.client.domain.physical_seat)
        return frontend_fail(e, QA_ERROR_ARGUMENT, "Unified import changed its actual CLIENT service domain");
    size_t count = 0;
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        const unified_peer *peer = owner->peers + i;
        if (!peer->occupied) continue;
        if (i || !qa_net_client_id_equal(client.domain.client, peer->client) ||
            !qa_net_client_owns_seat(qa_net_connections_get(qa_network_connections(client.domain.runtime),
                peer->client), peer->binding.seat)) return false;
        ++count;
    }
    if (!count && (client.domain.client.owner || client.domain.client.generation || client.domain.client.slot)) {
        if (!physical.retired || !client.domain.client.owner || !client.domain.client.generation ||
            qa_net_connections_get(qa_network_connections(client.domain.runtime), client.domain.client) ||
            !frontend_network_unified_client_retirement_current(service, &physical.physical.source))
            return bad(e, "Absent Unified peer has no actual retired physical CLIENT receipt");
    }
    owner->options.runtime = client.domain.runtime; owner->options.client_service = service; owner->options.client = client;
    return true;
}
static bool resolve(void *context, qa_net_client_id client, qa_unified_session **out, qa_error *e)
{
    frontend_network_unified *owner = context;
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        const unified_peer *peer = owner->peers + i;
        if (peer->occupied && qa_net_client_id_equal(peer->client, client))
            return qa_unified_session_find(owner->options.runtime, client, out, e) && *out == peer->session;
    }
    return bad(e, "Restored handshake has no real installed Source session");
}
bool frontend_network_unified_restore_lower(frontend_network_unified *owner, qa_network_runtime *runtime, qa_error *e)
{
    if (!owner || !owner->restore_pending || !runtime ||
        (owner->options.runtime && owner->options.runtime != runtime) || !qa_network_callbacks_idle(runtime))
        return frontend_fail(e, QA_ERROR_ARGUMENT, "Unified import finish requires its actual restored runtime");
    owner->options.runtime = runtime;
    if (owner->lower_restored) return inventory(owner, runtime, true, e);
    if (!owner->options.server && !owner->options.client_service)
        return bad(e, "Unified lower import precedes its genuine physical CLIENT prefix");
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
        unified_peer *peer = owner->peers + i;
        if (!peer->occupied) continue;
        if (!qa_unified_session_find(runtime, peer->client, &peer->session, e)) return false;
        if (owner->options.server) {
            if (!peer->server || (peer->server->restore_pending &&
                !application_unified_server_restore_bind(peer->server, peer->session, e))) return false;
        } else {
            if (!peer->remote || i) return false;
            if (!peer->client_bound && !frontend_network_unified_client_bind_restored(owner->options.client_service,
                peer->client, peer->binding.seat, peer->remote, e)) return false;
            peer->client_bound = true;
        }
    }
    if (!inventory(owner, runtime, true, e)) return false;
    qa_unified_bootstrap_hooks hooks = frontend_network_unified_bootstrap_hooks(owner);
    if (!owner->bootstrap && !qa_unified_bootstrap_restore((qa_bytes){owner->bootstrap_import.data, owner->bootstrap_import.size},
        runtime, owner->options.seat_owner, &hooks, resolve, owner, &owner->bootstrap, e)) return false;
    bool server; uint32_t maximum; qa_net_address remote;
    if (!qa_unified_bootstrap_domain(owner->bootstrap, &server, &maximum, &remote, e) || server != owner->options.server ||
        maximum != (server ? owner->source.max_clients : 1) ||
        (!server && !qa_net_address_equal(&remote, &owner->options.remote, true)))
        return bad(e, "Imported handshake changes its genuine Source domain");
    owner->lower_restored = true;
    return true;
}
bool frontend_network_unified_restore_client(frontend_network_unified *owner,
    qa_network_runtime *runtime, qa_net_client_id *client, frontend_remote_unified **remote,
    bool *present, qa_error *e)
{
    if (!owner || !owner->restore_pending || !owner->lower_restored || owner->options.server ||
        owner->options.runtime != runtime || !runtime || owner->closing || owner->calls ||
        !owner->options.frontend->source_restoring || !qa_network_callbacks_idle(runtime) ||
        !client || !remote || !present)
        return frontend_fail(e, QA_ERROR_ARGUMENT, "Unified replica prefix requires its actual lower-restored CLIENT graph");
    *client = (qa_net_client_id){0}; *remote = NULL; *present = false;
    if (!inventory(owner, runtime, true, e)) return false;
    unified_peer *peer = owner->peers;
    if (!peer->occupied) return true;
    if (!peer->client_bound || !peer->source_import.size || !peer->remote ||
        !qa_unified_session_source_retired(peer->session) ||
        !frontend_remote_unified_restore_pending(peer->remote) ||
        !frontend_remote_unified_checkpoint_current(peer->remote, e))
        return bad(e, "Unified CLIENT import lost its genuine owned replica prefix");
    *client = peer->client; *remote = peer->remote; *present = true;
    return true;
}
bool frontend_network_unified_restore_finish(frontend_network_unified *owner, qa_network_runtime *runtime, qa_error *e)
{
    if (!frontend_network_unified_restore_lower(owner, runtime, e)) return false;
    if (owner->options.server) {
        application_unified_source source;
        if (!application_unified_save_source_read(owner->options.frontend->application, &source, e) ||
            !server_children(owner, runtime, &source, e)) return false;
    } else {
        for (size_t i = 0; i < UNIFIED_PEERS; ++i) {
            unified_peer *peer = owner->peers + i;
            if (!peer->occupied) continue;
            if (frontend_remote_unified_restore_pending(peer->remote) &&
                (!frontend_remote_unified_presentation_restore_ready(peer->remote, e) ||
                 !frontend_remote_unified_restore_bind(peer->remote, peer->session, e))) return false;
            if (!frontend_remote_unified_presentation_restore_activate(peer->remote, e)) return false;
            if (!frontend_remote_unified_qualified(peer->remote, runtime,
                qa_net_connections_get(qa_network_connections(runtime), peer->client), e)) return false;
        }
    }
    for (size_t i = 0; i < UNIFIED_PEERS; ++i) qa_buffer_free(&owner->peers[i].source_import);
    qa_buffer_free(&owner->bootstrap_import); owner->restore_pending = false; owner->imported = true;
    return true;
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
            !(owner->options.frontend->capture || owner->options.frontend->source_restoring ?
                frontend_network_unified_checkpoint_returned(owner) : frontend_network_unified_idle(owner)) ||
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
    if (owner->options.runtime != runtime || !frontend_network_unified_idle(owner) ||
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
    qa_buffer_free(&owner->bootstrap_import);
    free(owner->restored_sidecars); free(owner); *slot = NULL; return true;
}
