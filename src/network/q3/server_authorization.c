/* Donor: network/q3/authorization.ts and SDK SV_GetChallenge. */
#include "qa/network_q3_server_authorization.h"
#include "qa/source_save.h"
#include <stdio.h>
#include <stdlib.h>

struct qa_q3_server_authorization {
    qa_q3_server_authorization_bindings bindings;
    qa_net_address address;
    bool attempted, resolved, busy;
};
static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
static bool bindings_valid(const qa_q3_server_authorization_bindings *bindings, qa_error *error)
{
    return (bindings && bindings->current && bindings->policy && bindings->send && bindings->print) ||
        fail(error, QA_ERROR_ARGUMENT, "Q3 server authorization requires its actual host, source policy and socket");
}
static bool valid(const qa_q3_server_authorization *owner, qa_error *error)
{
    if (!owner || owner->busy || (owner->resolved &&
        (!owner->attempted || owner->address.kind != QA_NET_IPV4 || !owner->address.port)))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 authorization has no idle server authority");
    return bindings_valid(&owner->bindings, error) && owner->bindings.current(owner->bindings.context, error);
}
bool qa_q3_server_authorization_create(const qa_q3_server_authorization_bindings *bindings,
    qa_q3_server_authorization **out, qa_error *error)
{
    if (!out || *out) return fail(error, QA_ERROR_ARGUMENT, "Q3 server authorization requires an empty output");
    if (!bindings_valid(bindings, error)) return false;
    qa_q3_server_authorization *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Allocating Q3 server authorization owner");
    owner->bindings = *bindings; *out = owner; return true;
}
void qa_q3_server_authorization_destroy(qa_q3_server_authorization *owner) { free(owner); }
bool qa_q3_server_authorization_idle(const qa_q3_server_authorization *owner) { return !owner || !owner->busy; }
const qa_net_address *qa_q3_server_authorization_address(const qa_q3_server_authorization *owner)
{ return owner && owner->resolved ? &owner->address : NULL; }
bool qa_q3_server_authorization_request(qa_q3_server_authorization *owner,
    const qa_q3_challenge *challenge, qa_error *error)
{
    if (!challenge || !valid(owner, error)) return false;
    if (!challenge->present || challenge->address.kind != QA_NET_IPV4) return true;
    bool enabled; const char *game = NULL, *strict = NULL;
    if (!owner->bindings.policy(owner->bindings.context, &enabled, &game, &strict, error)) return false;
    if (!enabled) return true;
    const qa_net_address client = challenge->address; int32_t number = challenge->challenge;
    owner->busy = true; bool ok = false; qa_buffer packet = {0};
    if (!owner->attempted) {
        owner->attempted = true; qa_net_address address = {0}; qa_error resolution = {0};
        bool resolved = owner->bindings.resolve
            ? owner->bindings.resolve(owner->bindings.context, &address, &resolution)
            : qa_net_address_resolve("authorize.quake3arena.com", 27952, 4, &address, &resolution);
        if (!owner->bindings.current(owner->bindings.context, error)) goto done;
        if (!resolved || address.kind != QA_NET_IPV4 || !address.port) {
            char text[sizeof(resolution.message) + 64];
            snprintf(text, sizeof(text), "Couldn't resolve Q3 authorization server: %s\n",
                resolution.message[0] ? resolution.message : "Q3 authorization requires IPv4");
            owner->bindings.print(owner->bindings.context, text);
        } else { owner->address = address; owner->resolved = true; }
    }
    if (!owner->bindings.current(owner->bindings.context, error) ||
        !owner->bindings.policy(owner->bindings.context, &enabled, &game, &strict, error)) goto done;
    if (!owner->resolved || !enabled || !challenge->present || challenge->challenge != number ||
        !qa_net_address_equal(&challenge->address, &client, true)) { ok = true; goto done; }
    if (!game || !strict) {
        fail(error, QA_ERROR_ARGUMENT, "Q3 authorization source lacks its actual game and strict-auth rows"); goto done;
    }
    if (!qa_q3_authorize_server_packet(challenge, game, strict, &packet, error) ||
        !owner->bindings.current(owner->bindings.context, error)) goto done;
    ok = owner->bindings.send(owner->bindings.context, &owner->address,
        (qa_bytes){packet.data, packet.size}, error) && owner->bindings.current(owner->bindings.context, error);
done:
    qa_buffer_free(&packet); owner->busy = false; return ok;
}
static bool fields(qa_source_save_io *io, bool *attempted, bool *resolved, qa_net_address *address)
{
    uint32_t magic = UINT32_C(0x41533351);
    if (!qa_source_save_u32(io, &magic) || magic != UINT32_C(0x41533351) ||
        !qa_source_save_bool(io, attempted) || !qa_source_save_bool(io, resolved)) return false;
    if (*resolved && !*attempted) return fail(io->error, QA_ERROR_FORMAT, "Q3 authority resolved without its actual lookup attempt");
    if (!*resolved) return true;
    address->kind = QA_NET_IPV4;
    if (!qa_source_save_u16(io, &address->port)) return false;
    if (!address->port) return fail(io->error, QA_ERROR_FORMAT, "Saved Q3 server authority has no UDP port");
    return qa_source_save_bytes(io, address->host.ipv4, 4);
}
bool qa_q3_server_authorization_checkpoint(const qa_q3_server_authorization *owner, qa_buffer *out, qa_error *error)
{
    if (!out || !valid(owner, error)) return false;
    bool attempted = owner->attempted, resolved = owner->resolved;
    qa_net_address address = owner->address; qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, &attempted, &resolved, &address) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_q3_server_authorization_restore(qa_bytes bytes, const qa_q3_server_authorization_bindings *bindings,
    qa_q3_server_authorization **out, qa_error *error)
{
    if (!out || *out) return fail(error, QA_ERROR_ARGUMENT, "Q3 authority restore requires an empty output");
    if (!bindings_valid(bindings, error)) return false;
    bool attempted = false, resolved = false; qa_net_address address = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, &attempted, &resolved, &address) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok || !bindings->current(bindings->context, error) ||
        !qa_q3_server_authorization_create(bindings, out, error)) return false;
    (*out)->attempted = attempted; (*out)->resolved = resolved; (*out)->address = address; return true;
}
void qa_q3_server_authorization_rebind(qa_q3_server_authorization *owner, void *context)
{ if (owner) owner->bindings.context = context; }
