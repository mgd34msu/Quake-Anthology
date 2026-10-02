#include "network_unified.h"
#include "map_players_private.h"
#include "unified_output.h"
#include "unified_output_capture.h"
#include "unified_events.h"
#include "qa/network_unified_save.h"
#include "unified_output_json.h"

#include <stdlib.h>
#include <string.h>

struct application_unified_server {
    qa_application *application;
    qa_network_runtime *runtime;
    qa_net_seat_id seat;
    uint32_t application_seat, epoch;
    qa_net_client_id client;
    qa_unified_session *session;
    application_unified_inputs *inputs;
    application_unified_component_publisher *components;
    application_unified_output_capture *pending_capture;
    application_unified_source offered;
    qa_sha256_digest composition;
    qa_unified_document *offer;
    application_unified_output pending;
    size_t control_cursor;
    uint64_t frame_before, published_frame;
    uint64_t events_after, pending_events_through;
    int64_t acknowledged;
    bool bound, admitted, player_attached, preparing_frame, entered, closed;
};

static qa_json_id control_value(const qa_unified_document *document)
{
    return qa_json_get(qa_unified_document_json(document), qa_unified_document_root(document), "value");
}

static bool document_clone(const qa_unified_document *document, qa_unified_document **out, qa_error *error)
{
    return qa_unified_document_create(qa_unified_document_type(document),
        qa_json_source(qa_unified_document_json(document), qa_unified_document_root(document)), out, error);
}

static bool offered_current(application_unified_server *owner)
{
    application_unified_source actual;
    return application_unified_source_read(owner->application, &actual, NULL) &&
        actual.launch == owner->offered.launch && actual.session == owner->offered.session &&
        actual.world == owner->offered.world && actual.owner == owner->offered.owner &&
        actual.publication == owner->offered.publication && actual.map_revision == owner->offered.map_revision &&
        actual.max_clients == owner->offered.max_clients;
}

static const char *source_mode(const application_unified_source *source)
{
    const qa_launch_choices *choices = qa_launch_snapshot_choices(source->launch);
    for (size_t i = 0; i < choices->mode_count; ++i) {
        const qa_launch_mode *mode = choices->modes + i;
        if (mode->primary_score && mode->rules.enabled)
            return mode->rules.kind == QA_MODE_SINGLE_PLAYER ? "singleplayer" :
                mode->rules.kind == QA_MODE_COOPERATIVE ? "coop" : "deathmatch";
    }
    return "singleplayer";
}

bool application_unified_server_offer(application_unified_server *owner, uint32_t epoch,
    const qa_recipe_sidecar *sidecars, size_t count, qa_unified_document **out, qa_error *error)
{
    application_unified_source source;
    if (!owner || !out || owner->entered || owner->closed || !epoch ||
        (owner->offer && epoch <= owner->epoch) || owner->pending.frame ||
        !application_unified_source_read(owner->application, &source, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified offer requires its genuine returned Source publication");
    qa_unified_document *offer = NULL, *copy = NULL;
    if (!qa_application_unified_offer(owner->application, epoch, source_mode(&source),
        source.max_clients, sidecars, count, &offer, error)) return false;
    const qa_json_document *json = qa_unified_document_json(offer);
    qa_json_id composition = qa_json_get(json, control_value(offer), "composition");
    qa_unified_composition canonical = {0};
    bool okay = qa_unified_composition_create(qa_json_source(json,
        qa_json_get(json, composition, "composition")), &canonical, error) &&
        application_unified_source_current(owner->application, &source) && document_clone(offer, &copy, error);
    if (okay) okay = application_unified_components_destroy(&owner->components, error);
    if (okay && owner->inputs) {
        okay = application_unified_inputs_destroy(owner->inputs, error);
        if (okay) owner->inputs = NULL;
    }
    if (!okay) {
        qa_unified_composition_free(&canonical); qa_unified_document_destroy(offer);
        qa_unified_document_destroy(copy); return false;
    }
    owner->inputs = NULL;
    qa_unified_document_destroy(owner->offer);
    owner->offer = offer; owner->composition = canonical.digest; owner->offered = source;
    owner->epoch = epoch; owner->acknowledged = -1; owner->admitted = false;
    owner->preparing_frame = false; owner->published_frame = source.frame.number;
    qa_unified_composition_free(&canonical); *out = copy; return true;
}

bool application_unified_server_create(qa_application *app, qa_network_runtime *runtime,
    qa_net_seat_id seat, uint32_t application_seat, uint32_t epoch,
    const qa_recipe_sidecar *sidecars, size_t count, application_unified_server **out,
    qa_unified_document **offer, qa_error *error)
{
    if (!app || !runtime || !out || !offer || !seat.owner || !qa_network_callbacks_idle(runtime))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified server requires its actual runtime seat owner");
    application_unified_server *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Allocating unified Source peer");
    *owner = (application_unified_server){.application = app, .runtime = runtime,
        .seat = seat, .application_seat = application_seat, .acknowledged = -1};
    if (!application_unified_server_offer(owner, epoch, sidecars, count, offer, error)) { free(owner); return false; }
    *out = owner; return true;
}

const qa_sha256_digest *application_unified_server_composition(const application_unified_server *owner)
{
    return owner && owner->offer ? &owner->composition : NULL;
}

bool application_unified_server_bind(application_unified_server *owner, qa_net_client_id client,
    qa_unified_session *session, qa_error *error)
{
    qa_unified_session *installed = NULL;
    const qa_net_client *peer = owner ? qa_net_connections_get(qa_network_connections(owner->runtime), client) : NULL;
    if (!owner || owner->bound || !session || !qa_unified_session_idle(session) || !peer ||
        peer->protocol.kind != QA_NET_UNIFIED_1 || peer->seat_count != 1 ||
        peer->seats[0].seat.owner != owner->seat.owner || peer->seats[0].seat.index != owner->seat.index ||
        !qa_sha256_equal(&peer->composition, &owner->composition) ||
        !offered_current(owner) || !qa_unified_session_find(owner->runtime, client, &installed, error) || installed != session)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified binding changes its authentic Source offer or peer");
    owner->client = client; owner->session = session; owner->bound = true; return true;
}

static bool peer_is(application_unified_server *owner, qa_network_runtime *runtime, qa_net_client_id client)
{
    return owner && owner->bound && !owner->closed && runtime == owner->runtime &&
        qa_net_client_id_equal(client, owner->client);
}

static bool player(void *context, qa_net_client_id client, qa_unified_session_player *out, qa_error *error)
{
    application_unified_server *owner = context;
    return peer_is(owner, owner->runtime, client) && owner->admitted &&
        application_unified_player_read(owner->application, client, owner->seat, out, error);
}

static bool admitted_document(application_unified_server *owner, const qa_unified_session_player *player,
    qa_unified_document **out, qa_error *error)
{
    application_unified_json json = {0};
    bool okay = application_unified_json_text(&json, "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"admitted\",\"epoch\":", error) &&
        application_unified_json_natural(&json, owner->epoch, error) &&
        application_unified_json_text(&json, ",\"client\":{\"slot\":", error) &&
        application_unified_json_natural(&json, owner->client.slot, error) &&
        application_unified_json_text(&json, ",\"generation\":", error) &&
        application_unified_json_natural(&json, owner->client.generation, error) &&
        application_unified_json_text(&json, "},\"actor\":", error) &&
        application_unified_json_actor(&json, player->actor, error) &&
        application_unified_json_text(&json, ",\"sourceEntity\":", error) &&
        application_unified_json_natural(&json, player->source_slot, error) &&
        application_unified_json_text(&json, "}}", error) &&
        qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT,
            (qa_bytes){json.bytes.data, json.bytes.size}, out, error);
    application_unified_json_dispose(&json); return okay;
}

static bool control_entered(void *context, qa_network_runtime *runtime, qa_net_client_id client,
    uint32_t epoch, const qa_unified_document *document, qa_unified_session_commit *commit, qa_error *error)
{
    application_unified_server *owner = context;
    if (!peer_is(owner, runtime, client) || epoch != owner->epoch || !commit)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified control lost its actual Source peer");
    const qa_json_document *json = qa_unified_document_json(document);
    qa_json_id value = control_value(document), kind = qa_json_get(json, value, "kind");
    if (qa_json_string_equal(json, kind, "ready")) {
        char digest[72] = "sha256:"; qa_sha256_hex(&owner->composition, digest + 7);
        if (!qa_json_string_equal(json, qa_json_get(json, value, "composition"), digest) ||
            !offered_current(owner))
            return application_fail(error, QA_ERROR_ARGUMENT, "Unified readiness changes its retained Source recipe");
        qa_buffer userinfo = {0};
        if (!qa_json_string(json, qa_json_get(json, value, "userinfo"), &userinfo, error)) return false;
        qa_application_remote_player_request request = {.client = client, .seat = owner->seat,
            .application_seat = owner->application_seat, .source_slot = UINT32_MAX,
            .userinfo = (const char *)userinfo.data};
        qa_unified_session_player actual;
        qa_actor_id carried;
        bool exists = qa_application_remote_player_actor(owner->application, client, owner->seat, &carried);
        bool okay = exists ? application_unified_player_read(owner->application, client, owner->seat, &actual, error) :
            application_unified_player_admit(owner->application, runtime, &request, &actual, error);
        qa_buffer_free(&userinfo);
        if (okay) owner->player_attached = true;
        if (okay && !owner->components) okay = application_unified_components_create(owner->application,
            client, actual.actor, &owner->components, error);
        if (okay && !owner->inputs) okay = application_unified_inputs_create(owner->application, runtime, client,
            owner->seat, epoch, &owner->inputs, error);
        if (okay) okay = admitted_document(owner, &actual, &commit->reply, error);
        application_unified_events initial = {0};
        application_unified_source current_source;
        if (okay) okay = application_unified_source_read(owner->application, &current_source, error) &&
            application_unified_events_read(owner->application, &current_source, client, &actual,
                epoch, owner->events_after, &initial, error);
        if (okay && initial.control_count > sizeof(commit->followups) / sizeof(commit->followups[0]))
            okay = application_fail(error, QA_ERROR_FORMAT, "Initial Source controls exceed the actual ordered reply capacity");
        if (okay && !application_unified_events_current(&initial))
            okay = application_fail(error, QA_ERROR_ARGUMENT, "Initial Source events changed before retained reply transfer");
        if (okay) {
            for (size_t i = 0; i < initial.control_count; ++i) {
                commit->followups[i] = initial.controls[i]; initial.controls[i] = NULL;
            }
            commit->followup_count = initial.control_count;
            owner->events_after = initial.through;
        }
        application_unified_events_dispose(&initial);
        if (okay) { owner->admitted = true; commit->applied = true; }
        return okay;
    }
    if (qa_json_string_equal(json, kind, "userinfo")) {
        qa_buffer text = {0};
        bool okay = owner->admitted && qa_json_string(json, qa_json_get(json, value, "value"), &text, error) &&
            application_unified_player_userinfo(owner->application, client, owner->seat, (const char *)text.data, error);
        qa_buffer_free(&text); commit->applied = okay; return okay;
    }
    if (qa_json_string_equal(json, kind, "command")) {
        qa_json_id arguments = qa_json_get(json, value, "args");
        size_t count = qa_json_size(json, arguments);
        if (!owner->admitted || count > 128)
            return application_fail(error, QA_ERROR_ARGUMENT, "Unified command has no admitted physical Source recipient");
        qa_buffer name = {0}, retained[128] = {0};
        const char *words[128];
        bool okay = qa_json_string(json, qa_json_get(json, value, "name"), &name, error);
        for (size_t i = 0; okay && i < count; ++i) {
            okay = qa_json_string(json, qa_json_at(json, arguments, i), retained + i, error);
            if (okay) words[i] = (const char *)retained[i].data;
        }
        if (okay) okay = application_unified_player_command(owner->application, client, owner->seat,
            (const char *)name.data, words, count, error);
        for (size_t i = 0; i < count; ++i) qa_buffer_free(retained + i);
        qa_buffer_free(&name); commit->applied = okay; return okay;
    }
    if (qa_json_string_equal(json, kind, "disconnect")) {
        bool okay = !owner->player_attached || application_unified_player_disconnect(owner->application, client, owner->seat, error);
        if (okay) owner->admitted = owner->player_attached = false;
        commit->applied = okay; return okay;
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED, "Unified command requires its actual typed Source or component command owner");
}

static bool control(void *context, qa_network_runtime *runtime, qa_net_client_id client,
    uint32_t epoch, const qa_unified_document *document, qa_unified_session_commit *commit, qa_error *error)
{
    application_unified_server *owner = context;
    if (!owner || owner->entered)
        return application_fail(error, QA_ERROR_ARGUMENT, "Recursive unified Source control entry");
    owner->entered = true;
    bool okay = control_entered(context, runtime, client, epoch, document, commit, error);
    owner->entered = false; return okay;
}

static bool input(void *context, qa_network_runtime *runtime, qa_net_client_id client,
    const qa_unified_input_batch *batch, qa_error *error)
{
    application_unified_server *owner = context;
    return peer_is(owner, runtime, client) && owner->admitted && owner->inputs &&
        application_unified_inputs_queue(owner->inputs, batch, error);
}

static bool restart(void *context, qa_network_runtime *runtime, qa_net_client_id client,
    uint32_t epoch, const qa_sha256_digest *composition, qa_unified_document **offer, qa_error *error)
{
    application_unified_server *owner = context;
    return peer_is(owner, runtime, client) && epoch == owner->epoch && composition &&
        qa_sha256_equal(composition, &owner->composition) &&
        offered_current(owner) &&
        document_clone(owner->offer, offer, error);
}

static void closed(void *context, qa_net_client_id client)
{
    application_unified_server *owner = context;
    if (owner && owner->bound && qa_net_client_id_equal(client, owner->client)) {
        owner->closed = true; owner->session = NULL;
    }
}

qa_unified_session_hooks application_unified_server_hooks(application_unified_server *owner)
{
    return (qa_unified_session_hooks){.context = owner, .player = player, .control = control,
        .input = input, .restart = restart, .closed = closed};
}

bool application_unified_server_pre_frame(application_unified_server *owner, qa_error *error)
{
    application_unified_source source;
    if (!owner || !owner->bound || owner->closed || owner->entered || owner->pending.frame ||
        !owner->session || !qa_unified_session_idle(owner->session) ||
        !application_unified_source_read(owner->application, &source, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified pre-frame requires its returned physical Source peer");
    if (!owner->admitted) return true;
    if (!application_unified_inputs_flush(owner->inputs, error)) return false;
    owner->frame_before = source.frame.number; owner->preparing_frame = true; return true;
}

bool application_unified_server_publish(application_unified_server *owner,
    const application_unified_output_external *external, qa_error *error)
{
    if (owner && owner->bound && !owner->closed && !owner->admitted) return true;
    application_unified_source source;
    qa_unified_session_player actual;
    if (!owner || owner->closed || owner->entered || !owner->admitted || !owner->session ||
        !qa_unified_session_idle(owner->session) || !owner->preparing_frame ||
        !application_unified_source_read(owner->application, &source, error) ||
        source.frame.phase != QA_FRAME_EXIT || source.frame.number <= owner->frame_before ||
        !application_unified_player_read(owner->application, owner->client, owner->seat, &actual, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified publication lacks its genuinely completed Source frame");
    if (!owner->pending.frame) {
        int64_t acknowledged = application_unified_inputs_submitted(owner->inputs);
        application_unified_output_capture *capture = NULL;
        if (!application_unified_output_acquire(owner->application, &source, owner->client,
            &actual, owner->epoch, acknowledged, owner->events_after, owner->components, external, &capture, error)) return false;
        const application_unified_output *observed = application_unified_output_capture_value(capture);
        application_unified_output candidate = {0};
        bool copied = document_clone(observed->frame, &candidate.frame, error);
        if (copied && observed->control_count) {
            candidate.controls = calloc(observed->control_count, sizeof(*candidate.controls));
            if (!candidate.controls) copied = application_fail(error, QA_ERROR_MEMORY, "Retaining actual unified prerequisite controls");
        }
        for (size_t i = 0; copied && i < observed->control_count; ++i) {
            copied = document_clone(observed->controls[i], candidate.controls + i, error);
            if (copied) ++candidate.control_count;
        }
        if (copied && !application_unified_output_capture_current(capture))
            copied = application_fail(error, QA_ERROR_ARGUMENT, "Unified output children retired during retained publication");
        uint64_t through = application_unified_output_capture_events_through(capture);
        if (copied) copied = application_unified_output_capture_seal(capture, error);
        if (!copied) { application_unified_output_capture_dispose(capture);
            application_unified_output_dispose(&candidate); return false; }
        owner->pending_capture = capture;
        owner->pending = candidate; owner->pending_events_through = through;
        owner->acknowledged = acknowledged; owner->published_frame = source.frame.number;
    } else if (source.frame.number != owner->published_frame)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified pending publication was overtaken by another Source frame");
    owner->entered = true;
    bool okay = true;
    while (okay && owner->control_cursor < owner->pending.control_count) {
        okay = qa_unified_session_control(owner->session, owner->pending.controls[owner->control_cursor], error);
        if (okay) ++owner->control_cursor;
    }
    if (okay) okay = qa_unified_session_frame(owner->session, owner->pending.frame, error);
    owner->entered = false;
    if (okay) {
        application_unified_output_capture_commit(owner->pending_capture);
        application_unified_output_capture_dispose(owner->pending_capture); owner->pending_capture = NULL;
        owner->events_after = owner->pending_events_through;
        application_unified_output_dispose(&owner->pending); owner->control_cursor = 0;
        owner->preparing_frame = false;
    }
    return okay;
}

bool application_unified_server_destroy(application_unified_server *owner, qa_error *error)
{
    if (!owner) return true;
    if (owner->entered || (owner->bound && !owner->closed))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified peer must retire its actual session before its Source owner");
    application_unified_output_capture_dispose(owner->pending_capture); owner->pending_capture = NULL;
    if (!application_unified_components_destroy(&owner->components, error)) return false;
    qa_actor_id actor;
    if (owner->player_attached && qa_application_remote_player_actor(owner->application,
        owner->client, owner->seat, &actor) && !application_unified_player_disconnect(owner->application,
        owner->client, owner->seat, error)) return false;
    if (!application_unified_inputs_destroy(owner->inputs, error)) return false;
    qa_unified_document_destroy(owner->offer); application_unified_output_dispose(&owner->pending);
    free(owner); return true;
}
bool application_unified_server_transport_retired(application_unified_server *owner,
    const qa_unified_session *session, qa_error *error)
{
    if (!owner || owner->entered || owner->session != session || !session ||
        !qa_unified_session_source_retired(session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified Source retirement retains real transport callbacks");
    owner->session = NULL; owner->closed = true;
    /* The physical Source roster belongs to the successful custody transfer.
     * This old bridge never tears down that transferred player. */
    owner->player_attached = owner->admitted = false;
    return true;
}
