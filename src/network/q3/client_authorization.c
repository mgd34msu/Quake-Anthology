/* Donor: client-authorization.ts and SDK CL_RequestAuthorization. */
#include "qa/network_q3_authorization.h"
#include "qa/source_save.h"
#include <stdlib.h>

struct qa_q3_client_authorization {
    qa_q3_client_authorization_bindings bindings;
    qa_net_address address;
    bool resolved, busy;
};
static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
static bool bindings_valid(const qa_q3_client_authorization_bindings *bindings, qa_error *error)
{
    return (bindings && bindings->cvars && bindings->current && bindings->read_profile && bindings->print) ||
        fail(error, QA_ERROR_ARGUMENT, "Q3 authorization requires its actual published key profile and client registry");
}
static bool valid(const qa_q3_client_authorization *owner, qa_error *error)
{
    if (!owner || owner->busy ||
        (owner->resolved && (owner->address.kind != QA_NET_IPV4 || !owner->address.port)))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 authorization lost its idle profile or retained IPv4 authority");
    return bindings_valid(&owner->bindings, error) && owner->bindings.current(owner->bindings.context, error);
}
bool qa_q3_client_authorization_create(const qa_q3_client_authorization_bindings *bindings,
    qa_q3_client_authorization **out, qa_error *error)
{
    if (!out || *out) return fail(error, QA_ERROR_ARGUMENT, "Q3 authorization requires an empty owner output");
    if (!bindings_valid(bindings, error)) return false;
    qa_q3_client_authorization *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Allocating Q3 client-static authorization owner");
    owner->bindings = *bindings; *out = owner; return true;
}
void qa_q3_client_authorization_destroy(qa_q3_client_authorization *owner) { free(owner); }
bool qa_q3_client_authorization_idle(const qa_q3_client_authorization *owner)
{ return !owner || !owner->busy; }
static bool current(qa_q3_client_authorization *owner,
    bool (*connection_current)(void *, qa_error *), void *connection, qa_error *error)
{
    return owner->bindings.current(owner->bindings.context, error) && connection_current(connection, error);
}
bool qa_q3_client_authorization_request(qa_q3_client_authorization *owner,
    bool (*connection_current)(void *, qa_error *), qa_q3_send_fn send, void *connection, qa_error *error)
{
    if (!connection_current || !send)
        return fail(error, QA_ERROR_ARGUMENT, "Q3 authorization requires its actual connection and socket callbacks");
    if (!valid(owner, error) || !connection_current(connection, error)) return false;
    owner->busy = true; bool ok = false; qa_buffer packet = {0};
    if (!owner->resolved) {
        qa_net_address address = {0}; qa_error resolution = {0};
        bool resolved = owner->bindings.resolve
            ? owner->bindings.resolve(owner->bindings.context, &address, &resolution)
            : qa_net_address_resolve("authorize.quake3arena.com", 27952, 4, &address, &resolution);
        if (!current(owner, connection_current, connection, error)) goto done;
        if (!resolved || address.kind != QA_NET_IPV4 || !address.port) {
            owner->bindings.print(owner->bindings.context, "Couldn't resolve Q3 authorization server\n");
            ok = current(owner, connection_current, connection, error); goto done;
        }
        owner->address = address; owner->resolved = true;
    }
    uint8_t key[33] = {0}; bool demo = false;
    if (!owner->bindings.read_profile(owner->bindings.context, key, &demo, error) ||
        !current(owner, connection_current, connection, error)) goto done;
    key[32] = 0;
    if (!qa_cvars_register(owner->bindings.cvars, "cl_anonymous", "0", QA_CVAR_INIT | QA_CVAR_SYSTEMINFO,
        0, "Original Q3 authorization policy", error) ||
        !current(owner, connection_current, connection, error)) goto done;
    const qa_cvar_view *anonymous = qa_cvars_find(owner->bindings.cvars, "cl_anonymous");
    if (!anonymous || !qa_q3_authorize_client_packet((const char *)key, demo, anonymous->integer, &packet, error) ||
        !current(owner, connection_current, connection, error)) goto done;
    ok = send(connection, &owner->address, (qa_bytes){packet.data, packet.size}, error) &&
        current(owner, connection_current, connection, error);
done:
    qa_buffer_free(&packet); owner->busy = false; return ok;
}
static bool fields(qa_source_save_io *io, bool *resolved, qa_net_address *address)
{
    uint32_t magic = UINT32_C(0x41433351);
    if (!qa_source_save_u32(io, &magic) || magic != UINT32_C(0x41433351) ||
        !qa_source_save_bool(io, resolved)) return false;
    if (!*resolved) return true;
    address->kind = QA_NET_IPV4;
    if (!qa_source_save_u16(io, &address->port)) return false;
    if (!address->port) return fail(io->error, QA_ERROR_FORMAT, "Saved Q3 authorization authority has no UDP port");
    return qa_source_save_bytes(io, address->host.ipv4, 4);
}
bool qa_q3_client_authorization_checkpoint(const qa_q3_client_authorization *owner, qa_buffer *out, qa_error *error)
{
    if (!out || !valid(owner, error)) return false;
    bool resolved = owner->resolved; qa_net_address address = owner->address; qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, &resolved, &address) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_q3_client_authorization_restore(qa_bytes bytes, const qa_q3_client_authorization_bindings *bindings,
    qa_q3_client_authorization **out, qa_error *error)
{
    if (!out || *out) return fail(error, QA_ERROR_ARGUMENT, "Q3 authorization restore requires an empty owner output");
    if (!bindings_valid(bindings, error)) return false;
    bool resolved = false; qa_net_address address = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, &resolved, &address) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok || !bindings->current(bindings->context, error) ||
        !qa_q3_client_authorization_create(bindings, out, error)) return false;
    (*out)->resolved = resolved; (*out)->address = address; return true;
}
void qa_q3_client_authorization_rebind(qa_q3_client_authorization *owner, void *context)
{ if (owner) owner->bindings.context = context; }
