#include "network_unified_save.h"
#include "network_unified_private.h"
#include "network_unified_inputs_save.h"
#include "unified_components_save.h"
#include "unified_output_capture_save.h"
#include "unified_save_internal.h"
#include "internal.h"
#include "qa/network_unified_save.h"

#include <stdlib.h>
#include <string.h>

static bool bad(qa_error *e, const char *message)
{ return application_fail(e, QA_ERROR_FORMAT, message); }
static bool player_receipt(qa_source_save_io *io, application_unified_server *owner,
    const qa_unified_session_player *actual)
{
    if (!qa_source_save_bool(io, &owner->admitted_receipt)) return false;
    if (!owner->admitted_receipt) return !owner->admitted_arsenal.data && !owner->admitted_arsenal.size;
    if (!actual) return false;
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    if (writing) {
        const qa_unified_session_player *saved = &owner->admitted_player;
        if (!qa_actor_id_equal(saved->actor, actual->actor) || saved->seat.owner != actual->seat.owner ||
            saved->seat.index != actual->seat.index || saved->movement != actual->movement ||
            saved->source_owner != actual->source_owner || saved->source_slot != actual->source_slot ||
            saved->arsenal.data != owner->admitted_arsenal.data || saved->arsenal.size != owner->admitted_arsenal.size ||
            saved->arsenal.size != actual->arsenal.size ||
            (saved->arsenal.size && memcmp(saved->arsenal.data, actual->arsenal.data, saved->arsenal.size))) return false;
    } else owner->admitted_player = *actual;
    if (!application_unified_save_player(io, &owner->admitted_player)) return false;
    if (!writing) {
        size_t size = actual->arsenal.size;
        owner->admitted_arsenal.data = size ? malloc(size) : NULL;
        if (size && !owner->admitted_arsenal.data)
            return application_fail(io->error, QA_ERROR_MEMORY, "Restoring actual admitted player arsenal receipt");
        owner->admitted_arsenal.size = size;
        if (size) memcpy(owner->admitted_arsenal.data, actual->arsenal.data, size);
        owner->admitted_player.arsenal = (qa_bytes){owner->admitted_arsenal.data, size};
    }
    return true;
}
static bool offer_valid(const application_unified_server *owner, const qa_net_client *peer, qa_error *e)
{
    const qa_json_document *json = qa_unified_document_json(owner->offer);
    qa_json_id value = qa_json_get(json, qa_unified_document_root(owner->offer), "value");
    uint64_t epoch;
    if (!owner->offer || qa_unified_document_type(owner->offer) != QA_UNIFIED_CONTROL_DOCUMENT ||
        !qa_json_string_equal(json, qa_json_get(json, value, "kind"), "offer") ||
        !qa_json_u64(json, qa_json_get(json, value, "epoch"), &epoch, e) || epoch != owner->epoch) return false;
    qa_json_id composition = qa_json_get(json, value, "composition");
    qa_unified_composition canonical = {0};
    bool okay = qa_unified_composition_create(qa_json_source(json,
        qa_json_get(json, composition, "composition")), &canonical, e);
    if (okay) okay = qa_sha256_equal(&canonical.digest, &owner->composition) &&
        qa_sha256_equal(&peer->composition, &owner->composition);
    qa_unified_composition_free(&canonical);
    return okay;
}
static bool peer_valid(const application_unified_server *owner, const qa_net_client *peer)
{
    return peer && peer->protocol.kind == QA_NET_UNIFIED_1 && !peer->protocol.flags && !peer->protocol.revision &&
        peer->seat_count == 1 && peer->seats && qa_net_client_id_equal(peer->id, owner->client) &&
        peer->seats[0].seat.owner == owner->seat.owner && peer->seats[0].seat.index == owner->seat.index;
}
static bool output_valid(const application_unified_server *owner,
    const application_unified_source *source, const qa_unified_session_player *player, qa_error *e)
{
    if (!owner->pending.frame) return !owner->pending_capture && !owner->control_cursor &&
        !owner->pending_first && !owner->pending_last && !owner->pending.control_count;
    if (!player || !owner->pending_capture || !owner->admitted || !owner->preparing_frame ||
        source->frame.phase != QA_FRAME_EXIT || source->frame.number != owner->published_frame ||
        owner->published_frame <= owner->frame_before || owner->control_cursor > owner->pending.control_count ||
        owner->control_cursor > UINT32_MAX ||
        !application_unified_save_output_equal(&owner->pending,
            application_unified_output_capture_value(owner->pending_capture)) ||
        application_unified_output_capture_events_through(owner->pending_capture) != owner->pending_events_through ||
        owner->pending_events_through < owner->events_after) return false;
    if (!owner->control_cursor) {
        if (owner->pending_first || owner->pending_last) return false;
    } else if (!owner->pending_first || owner->pending_last < owner->pending_first ||
        (uint64_t)owner->pending_last - owner->pending_first + 1 != owner->control_cursor) return false;
    const qa_json_document *json = qa_unified_document_json(owner->pending.frame);
    qa_json_id root = qa_unified_document_root(owner->pending.frame);
    uint64_t epoch, slot, generation, frame;
    int64_t acknowledged;
    qa_json_id actor = qa_json_get(json, qa_json_get(json, root, "player"), "actor");
    qa_json_id snapshot = qa_json_get(json, qa_json_get(json, root, "output"), "snapshot");
    if (!qa_json_u64(json, qa_json_get(json, root, "epoch"), &epoch, e) || epoch != owner->epoch ||
        !qa_json_i64(json, qa_json_get(json, root, "acknowledgedInput"), &acknowledged, e) ||
        acknowledged != owner->acknowledged ||
        !qa_json_u64(json, qa_json_get(json, actor, "slot"), &slot, e) || slot != player->actor.slot ||
        !qa_json_u64(json, qa_json_get(json, actor, "generation"), &generation, e) || generation != player->actor.generation ||
        !qa_json_u64(json, qa_json_get(json, qa_json_get(json, snapshot, "frame"), "frame"), &frame, e) ||
        frame != source->frame.number) return false;
    for (size_t i = 0; i < owner->pending.control_count; ++i) {
        const qa_json_document *control = qa_unified_document_json(owner->pending.controls[i]);
        qa_json_id value = qa_json_get(control, qa_unified_document_root(owner->pending.controls[i]), "value");
        if (!qa_json_u64(control, qa_json_get(control, value, "epoch"), &epoch, e) || epoch != owner->epoch) return false;
    }
    return true;
}
static bool continuation_valid(const application_unified_server *owner,
    const application_unified_source *source, const qa_unified_session_player *player,
    const qa_net_client *peer, qa_error *e)
{
    return owner->bound && !owner->entered && !owner->closed && owner->epoch &&
        peer_valid(owner, peer) && offer_valid(owner, peer, e) &&
        (!owner->admitted || (owner->player_attached && owner->admitted_receipt && owner->inputs && owner->components)) &&
        (!(owner->player_attached || owner->inputs || owner->components) || player) &&
        (!owner->preparing_frame || owner->admitted) && owner->frame_before <= source->frame.number &&
        owner->published_frame <= source->frame.number && owner->acknowledged >= -1 &&
        owner->acknowledged <= (int64_t)QA_UNIFIED_SAFE_INTEGER &&
        (!owner->inputs || owner->acknowledged <= owner->inputs->submitted) &&
        owner->events_after <= owner->application->unified_event_sequence &&
        owner->pending_events_through <= owner->application->unified_event_sequence &&
        output_valid(owner, source, player, e);
}
static bool receipts_valid(const application_unified_server *owner, const qa_unified_session *session, qa_error *e)
{
    if (!session || !qa_unified_session_idle(session) ||
        qa_unified_session_epoch(session) != owner->epoch ||
        (owner->inputs && qa_network_epoch(owner->runtime, owner->client) != owner->inputs->runtime_epoch)) return false;
    for (size_t i = 0; i < owner->control_cursor; ++i)
        if (!qa_unified_session_control_receipt(session, owner->pending_first + (uint32_t)i,
            owner->pending.controls[i], e)) return false;
    return true;
}
static bool fields(qa_source_save_io *io, application_unified_server *owner,
    const application_unified_source *source, const qa_net_client *peer)
{
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    application_unified_source current = *source;
    bool player_present = owner->player_attached || owner->admitted_receipt || owner->inputs || owner->components || owner->pending_capture;
    uint64_t seat_owner = owner->seat.owner;
    uint32_t seat_index = owner->seat.index;
    char magic[4] = {'Q','U','S','B'}; uint32_t version = 2;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QUSB", sizeof(magic)) ||
        !qa_source_save_u32(io, &version) || version != 2 ||
        !application_unified_save_source(io, owner->application, source, &current, false) ||
        !application_unified_save_source(io, owner->application, source, &owner->offered, true) ||
        !application_unified_save_client(io, peer->id) ||
        !qa_source_save_u64(io, &seat_owner) || seat_owner != peer->seats[0].seat.owner ||
        !qa_source_save_u32(io, &seat_index) || seat_index != peer->seats[0].seat.index ||
        !qa_source_save_u32(io, &owner->application_seat) ||
        !qa_source_save_u32(io, &owner->epoch) || !owner->epoch ||
        !qa_source_save_bytes(io, owner->composition.bytes, sizeof(owner->composition.bytes)) ||
        !application_unified_save_document(io, &owner->offer, QA_UNIFIED_CONTROL_DOCUMENT) || !owner->offer ||
        !qa_source_save_bool(io, &owner->bound) || !qa_source_save_bool(io, &owner->admitted) ||
        !qa_source_save_bool(io, &owner->player_attached) || !qa_source_save_bool(io, &owner->preparing_frame) ||
        !qa_source_save_bool(io, &player_present)) return false;
    qa_unified_session_player player = {0};
    if (player_present && (!application_unified_save_player_read(owner->application, peer->id, owner->seat,
        &player, io->error) || !application_unified_save_player(io, &player))) return false;
    if (!player_receipt(io, owner, player_present ? &player : NULL)) return false;
    if (!application_unified_inputs_save(io, &owner->inputs, owner->application, owner->runtime,
        owner->client, owner->seat, owner->epoch, source, player_present ? &player : NULL)) return false;
    bool components = owner->components != NULL;
    if (!qa_source_save_bool(io, &components)) return false;
    if (components) {
        if (!player_present) return false;
        qa_buffer bytes = {0};
        bool okay = !writing || application_unified_components_checkpoint(owner->components, &bytes, io->error);
        if (okay) okay = application_unified_save_blob(io, &bytes) && bytes.size;
        if (okay && !writing) okay = application_unified_components_restore((qa_bytes){bytes.data, bytes.size},
            owner->application, source, peer->id, &player, &owner->components, io->error);
        qa_buffer_free(&bytes);
        if (!okay) return false;
    }
    if (!application_unified_save_output(io, &owner->pending) ||
        !qa_source_save_count(io, &owner->control_cursor, owner->pending.control_count) ||
        !qa_source_save_u32(io, &owner->pending_first) || !qa_source_save_u32(io, &owner->pending_last) ||
        !qa_source_save_u64(io, &owner->frame_before) || !qa_source_save_u64(io, &owner->published_frame) ||
        !qa_source_save_u64(io, &owner->events_after) || !qa_source_save_u64(io, &owner->pending_events_through) ||
        !qa_source_save_i64(io, &owner->acknowledged)) return false;
    bool capture = owner->pending_capture != NULL;
    if (!qa_source_save_bool(io, &capture) || capture != (owner->pending.frame != NULL)) return false;
    if (capture) {
        if (!player_present) return false;
        qa_buffer bytes = {0};
        bool okay = !writing || application_unified_output_capture_checkpoint(owner->pending_capture, &bytes, io->error);
        if (okay) okay = application_unified_save_blob(io, &bytes) && bytes.size;
        if (okay && !writing) okay = application_unified_output_capture_restore((qa_bytes){bytes.data, bytes.size},
            owner->application, source, peer->id, &player, owner->components, &owner->pending_capture, io->error);
        qa_buffer_free(&bytes);
        if (!okay) return false;
    }
    return continuation_valid(owner, source, player_present ? &player : NULL, peer, io->error);
}
bool application_unified_server_checkpoint(const application_unified_server *owner, qa_buffer *out, qa_error *e)
{
    application_unified_source source;
    qa_unified_session *installed = NULL;
    if (!owner || !out || out->data || out->size || owner->restore_pending || owner->entered || owner->closed ||
        !qa_network_callbacks_idle(owner->runtime) ||
        !application_unified_save_source_read(owner->application, &source, e) ||
        !qa_unified_session_find(owner->runtime, owner->client, &installed, e) || installed != owner->session ||
        !receipts_valid(owner, installed, e))
        return application_fail(e, QA_ERROR_ARGUMENT, "Source checkpoint requires its actual bound returned peer");
    const qa_net_client *peer = qa_net_connections_get(qa_network_connections(owner->runtime), owner->client);
    if (!peer_valid(owner, peer)) return bad(e, "Source checkpoint changes its genuine canonical seat");
    application_unified_server copy = *owner;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, source.session, e) && fields(&io, &copy, &source, peer) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return okay || bad(e, "Source checkpoint continuation differs from its returned graph");
}
bool application_unified_server_restore_dispose(application_unified_server **slot, qa_error *e)
{
    if (!slot || !*slot) return true;
    application_unified_server *owner = *slot;
    if (!owner->restore_pending || owner->session || owner->entered)
        return application_fail(e, QA_ERROR_ARGUMENT, "Quiet import cleanup requires its never-published Source owner");
    owner->bound = owner->player_attached = owner->admitted = false;
    owner->closed = true;
    if (owner->inputs && !owner->inputs->commands) owner->inputs->count = owner->inputs->cursor = 0;
    if (!application_unified_server_destroy(owner, e)) return false;
    *slot = NULL; return true;
}
bool application_unified_server_restore(qa_bytes bytes, qa_application *app, qa_network_runtime *runtime,
    const qa_net_client *peer, application_unified_server **out, qa_error *e)
{
    application_unified_source source;
    if (!out || *out || !runtime || !peer || peer->protocol.kind != QA_NET_UNIFIED_1 ||
        peer->seat_count != 1 || !peer->seats || !qa_network_callbacks_idle(runtime) ||
        !application_unified_save_source_read(app, &source, e))
        return application_fail(e, QA_ERROR_ARGUMENT, "Source import requires its real candidate graph and connection");
    application_unified_server *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(e, QA_ERROR_MEMORY, "Restoring actual Unified Source continuation");
    *owner = (application_unified_server){.application = app, .runtime = runtime,
        .client = peer->id, .seat = peer->seats[0].seat, .offered = source, .restore_pending = true};
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, source.session, bytes, e) && fields(&io, owner, &source, peer) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!okay) { application_unified_server_restore_dispose(&owner, NULL);
        return bad(e, "Source import continuation differs from its genuine restored graph"); }
    *out = owner; return true;
}
bool application_unified_server_restore_bind(application_unified_server *owner,
    qa_unified_session *session, qa_error *e)
{
    qa_unified_session *actual = NULL;
    application_unified_source source;
    qa_unified_session_player player;
    if (!owner || !owner->restore_pending || owner->session || !session || owner->entered ||
        !qa_unified_session_find(owner->runtime, owner->client, &actual, e) || actual != session ||
        !qa_unified_session_source_retired(session) || !receipts_valid(owner, session, e) ||
        !application_unified_save_source_read(owner->application, &source, e))
        return application_fail(e, QA_ERROR_ARGUMENT, "Source import binding lacks its genuine unbound installed session");
    const qa_net_client *peer = qa_net_connections_get(qa_network_connections(owner->runtime), owner->client);
    bool needs_player = owner->player_attached || owner->admitted_receipt || owner->inputs || owner->components || owner->pending_capture;
    if ((needs_player && !application_unified_save_player_read(owner->application, owner->client, owner->seat, &player, e)) ||
        !continuation_valid(owner, &source, needs_player ? &player : NULL, peer, e))
        return bad(e, "Source import binding changed its retained physical player or output");
    owner->session = session; owner->restore_pending = false;
    return true;
}
