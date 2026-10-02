#include "network_unified_private.h"
#include "map_players_private.h"
#include "unified_output.h"
#include "unified_output_capture.h"
#include "unified_events.h"
#include "qa/network_unified_save.h"
#include "unified_output_json.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_clients.h"

#include <stdlib.h>
#include <string.h>

static qa_json_id control_value(const qa_unified_document *document)
{
    return qa_json_get(qa_unified_document_json(document), qa_unified_document_root(document), "value");
}

static bool document_clone(const qa_unified_document *document, qa_unified_document **out, qa_error *error)
{
    return qa_unified_document_create(qa_unified_document_type(document),
        qa_json_source(qa_unified_document_json(document), qa_unified_document_root(document)), out, error);
}
static void player_receipt_clear(application_unified_server *owner)
{
    qa_buffer_free(&owner->admitted_arsenal);
    owner->admitted_player=(qa_unified_session_player){0}; owner->admitted_receipt=false;
}
static bool player_receipt_retain(application_unified_server *owner,
    const qa_unified_session_player *actual,qa_error *error)
{
    if (owner->admitted_receipt) {
        const qa_unified_session_player *saved=&owner->admitted_player;
        return (qa_actor_id_equal(saved->actor,actual->actor) && saved->seat.owner==actual->seat.owner &&
            saved->seat.index==actual->seat.index && saved->movement==actual->movement &&
            saved->source_owner==actual->source_owner && saved->source_slot==actual->source_slot &&
            saved->arsenal.size==actual->arsenal.size && (!saved->arsenal.size ||
                !memcmp(saved->arsenal.data,actual->arsenal.data,saved->arsenal.size))) ||
            application_fail(error,QA_ERROR_ARGUMENT,"Unified ready changes its retained physical player admission");
    }
    qa_buffer arsenal={0};
    if (actual->arsenal.size) {
        arsenal.data=malloc(actual->arsenal.size);
        if (!arsenal.data) return application_fail(error,QA_ERROR_MEMORY,"Retaining actual admitted player arsenal identity");
        arsenal.size=actual->arsenal.size; memcpy(arsenal.data,actual->arsenal.data,arsenal.size);
    }
    owner->admitted_arsenal=arsenal; owner->admitted_player=*actual;
    owner->admitted_player.arsenal=(qa_bytes){arsenal.data,arsenal.size}; owner->admitted_receipt=true;
    return true;
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
    player_receipt_clear(owner);
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

static bool source_custody_ready(void *context,qa_network_runtime *runtime,qa_net_client_id client,
    uint32_t epoch,qa_error *error)
{
    application_unified_server *owner=context;
    qa_unified_session *installed=NULL;
    const qa_net_client *peer=runtime?qa_net_connections_get(qa_network_connections(runtime),client):NULL;
    if (!peer_is(owner,runtime,client) || owner->entered || !owner->session || !peer ||
        peer->protocol.kind!=QA_NET_UNIFIED_1 || peer->seat_count!=1 || !peer->seats ||
        peer->seats[0].seat.owner!=owner->seat.owner || peer->seats[0].seat.index!=owner->seat.index ||
        !qa_unified_session_find(runtime,client,&installed,error) || installed!=owner->session)
        return application_fail(error,QA_ERROR_ARGUMENT,"Unified Source custody changed its actual installed peer");
    bool checkpoint=owner->application->operation==APPLICATION_PERSISTING;
    application_unified_source actual;
    if (!(checkpoint?application_unified_source_checkpoint_read(owner->application,&actual,error):
        application_unified_source_read(owner->application,&actual,error))) return false;
    bool current=actual.owner==owner->offered.owner && actual.family==owner->offered.family &&
        actual.publication==owner->offered.publication && actual.map_revision==owner->offered.map_revision &&
        actual.max_clients==owner->offered.max_clients && actual.launch==owner->offered.launch &&
        actual.session==owner->offered.session && actual.world==owner->offered.world;
    if (owner->epoch!=epoch) {
        return (epoch!=UINT32_MAX && owner->epoch==epoch+1 && current && !owner->admitted &&
            !owner->inputs && !owner->components && !owner->pending_capture &&
            !owner->pending.frame && !owner->admitted_receipt) ||
            application_fail(error,QA_ERROR_ARGUMENT,"Unified Source custody has no genuine prepared travel offer");
    }
    if (!qa_sha256_equal(&peer->composition,&owner->composition))
        return application_fail(error,QA_ERROR_ARGUMENT,"Unified Source custody changed its admitted composition");
    if (owner->source_dropped)
        return current && qa_unified_session_source_close_pending(owner->session) &&
            application_unified_server_source_drop_current(owner,error);
    if (current) {
        if (!owner->admitted) return true;
        qa_unified_session_player actual_player;
        return checkpoint?application_unified_player_checkpoint_read(owner->application,client,owner->seat,&actual_player,error):
            application_unified_player_read(owner->application,client,owner->seat,&actual_player,error);
    }
    const qa_unified_session_player *saved=&owner->admitted_player;
    qa_saved_actor_id historical;
    bool obsolete=actual.publication>=owner->offered.publication && actual.map_revision>=owner->offered.map_revision &&
        (actual.publication>owner->offered.publication || actual.map_revision>owner->offered.map_revision);
    if (obsolete && !owner->admitted && !owner->admitted_receipt && !owner->inputs && !owner->components &&
        !owner->pending_capture && !owner->pending.frame) return true;
    bool slot=owner->offered.family==QA_GAME_Q3?saved->source_slot<owner->offered.max_clients:
        saved->source_slot && saved->source_slot<=owner->offered.max_clients;
    return (obsolete && owner->admitted && owner->player_attached && owner->admitted_receipt && saved->seat.owner==owner->seat.owner &&
        saved->seat.index==owner->seat.index && saved->source_owner==owner->offered.owner &&
        slot && saved->movement>=QA_MOVEMENT_NETQUAKE && saved->movement<=QA_MOVEMENT_Q3 &&
        saved->arsenal.data==owner->admitted_arsenal.data && saved->arsenal.size==owner->admitted_arsenal.size &&
        (!saved->arsenal.size || saved->arsenal.data) &&
        qa_actors_save_reference(qa_session_actors(actual.session),saved->actor,&historical,error)) ||
        application_fail(error,QA_ERROR_ARGUMENT,"Unified obsolete offer lacks its genuine historical player admission");
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
    if (!peer_is(owner, runtime, client) || !commit)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified control lost its actual Source peer");
    const qa_json_document *json = qa_unified_document_json(document);
    qa_json_id value = control_value(document), kind = qa_json_get(json, value, "kind");
    qa_unified_session *installed = NULL;
    if (epoch != owner->epoch && !(epoch != UINT32_MAX && owner->epoch == epoch + 1 &&
        qa_json_string_equal(json, kind, "disconnect") && !owner->admitted && !owner->inputs &&
        !owner->components && !owner->pending_capture && !owner->pending.frame &&
        !owner->admitted_receipt && offered_current(owner) && owner->session &&
        qa_unified_session_find(runtime,client,&installed,error) && installed==owner->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified control changes its authentic Source epoch");
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
        if (okay) okay=player_receipt_retain(owner,&actual,error);
        if (okay && !owner->components) okay = application_unified_components_create(owner->application,
            client, actual.actor, &owner->components, error);
        if (okay && !owner->inputs) okay = application_unified_inputs_create(owner->application, runtime, client,
            owner->seat, epoch, &owner->inputs, error);
        if (okay) okay = admitted_document(owner, &actual, &commit->reply, error);
        application_unified_events initial = {0};
        application_unified_source current_source;
        if (okay) okay = application_unified_source_read(owner->application, &current_source, error) &&
            application_unified_events_initial_read(owner->application, &current_source, client, &actual,
                epoch, &initial, error);
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
    if (qa_json_string_equal(json,kind,"source-command")) {
        qa_json_id arguments=qa_json_get(json,value,"args"), activation=qa_json_get(json,value,"activation");
        size_t count=qa_json_size(json,arguments);
        if (!owner->admitted || !count || count>128)
            return application_fail(error,QA_ERROR_ARGUMENT,"Source command has no admitted physical recipient");
        qa_buffer instance={0}, retained[128]={0}; const char *words[128];
        qa_unified_source_command command={.argument_count=count,.arguments=words};
        bool okay=qa_json_string(json,qa_json_get(json,value,"instance"),&instance,error) &&
            qa_json_u64(json,qa_json_get(json,activation,"publication"),&command.publication,error) &&
            qa_json_u64(json,qa_json_get(json,activation,"mapRevision"),&command.map_revision,error);
        command.instance=(const char *)instance.data;
        for (size_t i=0;okay && i<count;++i) {
            okay=qa_json_string(json,qa_json_at(json,arguments,i),retained+i,error);
            if (okay) words[i]=(const char *)retained[i].data;
        }
        if (okay) okay=application_unified_source_command(owner->application,client,owner->seat,&command,error);
        for (size_t i=0;i<count;++i) qa_buffer_free(retained+i);
        qa_buffer_free(&instance); commit->applied=okay; return okay;
    }
    if (qa_json_string_equal(json, kind, "component-command")) {
        qa_json_id arguments = qa_json_get(json, value, "args");
        size_t count = qa_json_size(json, arguments); uint64_t generation;
        if (!owner->admitted || !count || count > 128)
            return application_fail(error, QA_ERROR_ARGUMENT, "Component command has no admitted physical Source recipient");
        qa_buffer retained[128] = {0}; const char *words[128];
        qa_unified_document *component = NULL;
        bool okay = qa_json_u64(json, qa_json_get(json, value, "generation"), &generation, error) &&
            qa_unified_document_create(QA_UNIFIED_CHECKPOINT,
                qa_json_source(json, qa_json_get(json, value, "owner")), &component, error);
        for (size_t i = 0; okay && i < count; ++i) {
            okay = qa_json_string(json, qa_json_at(json, arguments, i), retained + i, error);
            if (okay) words[i] = (const char *)retained[i].data;
        }
        if (okay) okay = application_unified_component_command(owner->application, client, owner->seat,
            component, generation, words, count, error);
        for (size_t i = 0; i < count; ++i) qa_buffer_free(retained + i);
        qa_unified_document_destroy(component); commit->applied = okay; return okay;
    }
    if (qa_json_string_equal(json, kind, "disconnect")) {
        if ((owner->inputs && owner->inputs->advancing) ||
            (!owner->pending_capture && !application_unified_components_idle(owner->components)))
            return application_fail(error,QA_ERROR_ARGUMENT,"Unified disconnect retains an entered Source publication child");
        bool okay = !owner->player_attached || application_unified_player_disconnect(owner->application, client, owner->seat, error);
        if (okay) {
            owner->admitted=owner->player_attached=false;
            application_unified_output_capture_dispose(owner->pending_capture); owner->pending_capture=NULL;
            okay=application_unified_components_destroy(&owner->components,error) &&
                application_unified_inputs_destroy(owner->inputs,error);
            if (okay) {
                owner->inputs=NULL;
                application_unified_output_dispose(&owner->pending);
                owner->control_cursor=0; owner->pending_first=owner->pending_last=0;
                owner->preparing_frame=false; owner->pending_events_through=0;
                player_receipt_clear(owner);
            }
        }
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
    return (qa_unified_session_hooks){.context = owner, .player = player, .source_ready=source_custody_ready, .control = control,
        .input = input, .restart = restart, .closed = closed};
}

bool application_unified_server_pre_frame(application_unified_server *owner, qa_error *error)
{
    application_unified_source source;
    if (!owner || !owner->bound || owner->closed || owner->entered || owner->pending.frame ||
        !owner->session || !qa_unified_session_idle(owner->session) ||
        !application_unified_source_read(owner->application, &source, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified pre-frame requires its returned physical Source peer");
    if (!owner->admitted || !qa_unified_session_active(owner->session)) return true;
    if (!application_unified_inputs_flush(owner->inputs, error)) return false;
    owner->frame_before = source.frame.number; owner->preparing_frame = true; return true;
}

bool application_unified_server_source_drop(application_unified_server *owner,qa_actor_owner source_owner,
    uint32_t slot,const char *reason,bool *matched,qa_error *error)
{
    if (!owner || !matched || !reason)
        return application_fail(error,QA_ERROR_ARGUMENT,"Unified Source DROP has no actual transport recipient");
    *matched=false;
    if (!owner->bound || owner->closed || !owner->session || !owner->admitted_receipt ||
        owner->admitted_player.source_owner!=source_owner || owner->admitted_player.source_slot!=slot) return true;
    qa_application *app=owner->application;
    application_provider *source=application_world_provider(app,QA_ROLE_ENTITIES,"");
    qa_actor_id actor; const char *actual_reason=NULL; bool pending=false;
    if (!app || !app->players || !source || !source->launch || !owner->offered.launch ||
        source->kind!=APPLICATION_PROVIDER_Q3 || source->owner!=source_owner ||
        app->players->map_provider!=source || app->publication_generation!=owner->offered.publication ||
        app->map_revision!=owner->offered.map_revision || source->launch!=qa_launch_snapshot_find(owner->offered.launch,source->launch->selection.instance) ||
        !application_native_q3_wire_drop_client_read(source,slot,&actor,&actual_reason,&pending,error) ||
        !pending || !actual_reason || strcmp(actual_reason,reason) ||
        !qa_actor_id_equal(actor,owner->admitted_player.actor) || !owner->admitted || !owner->player_attached)
        return application_fail(error,QA_ERROR_ARGUMENT,"Unified DROP differs from its actual native Source request");
    if (owner->pending.frame || owner->pending_capture || owner->control_cursor)
        return application_fail(error,QA_ERROR_ARGUMENT,"Source DROP reached an already sealed output publication");
    if (!qa_unified_session_close(owner->session,actual_reason,error)) return false;
    owner->source_dropped=true; *matched=true; return true;
}
bool application_unified_server_source_drop_current(const application_unified_server *owner,qa_error *error)
{
    application_unified_source current;
    bool checkpoint=owner && owner->application && owner->application->operation==APPLICATION_PERSISTING;
    if (!owner || !owner->source_dropped || owner->entered || owner->pending.frame ||
        owner->pending_capture || owner->control_cursor ||
        !(checkpoint?application_unified_source_checkpoint_read(owner->application,&current,error):
            application_unified_source_read(owner->application,&current,error)) ||
        current.owner!=owner->offered.owner || current.publication!=owner->offered.publication ||
        current.map_revision!=owner->offered.map_revision || current.launch!=owner->offered.launch ||
        current.session!=owner->offered.session || current.world!=owner->offered.world ||
        !owner->admitted_receipt || owner->admitted_player.seat.owner!=owner->seat.owner ||
        owner->admitted_player.seat.index!=owner->seat.index ||
        owner->admitted_player.arsenal.data!=owner->admitted_arsenal.data ||
        owner->admitted_player.arsenal.size!=owner->admitted_arsenal.size)
        return application_fail(error,QA_ERROR_ARGUMENT,"Unified DROP cleanup lacks its returned actual Source request");
    application_provider *source=application_world_provider(owner->application,QA_ROLE_ENTITIES,"");
    qa_q3_native_client player; application_native_q3_wire_client_view wire; bool admitted=false;
    qa_saved_actor_id saved;
    const qa_unified_session_player *receipt=&owner->admitted_player;
    if (!source || source->kind!=APPLICATION_PROVIDER_Q3 || source->owner!=receipt->source_owner ||
        receipt->source_slot>=current.max_clients ||
        !qa_q3_client_slot_read(source->state.q3,receipt->source_slot,&player,error) ||
        player.connected!=QA_Q3_CLIENT_DISCONNECTED ||
        !application_native_q3_wire_client_admission_read(source,receipt->source_slot,&wire,&admitted,error) || admitted ||
        qa_actors_get(qa_session_actors(current.session),receipt->actor) ||
        !qa_actors_save_reference(qa_session_actors(current.session),receipt->actor,&saved,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"Unified DROP precedes actual native client and actor retirement");
    return checkpoint?application_unified_source_checkpoint_current(owner->application,&current):
        application_unified_source_current(owner->application,&current);
}
bool application_unified_server_source_drop_finish(application_unified_server *owner,qa_error *error)
{
    if (!owner || !owner->source_dropped) return owner!=NULL;
    if ((!owner->session && !owner->closed) ||
        (owner->session && !qa_unified_session_source_close_pending(owner->session)) ||
        !application_unified_server_source_drop_current(owner,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"Unified DROP has not reached actual transport retirement");
    owner->admitted=false; owner->player_attached=false; owner->preparing_frame=false;
    if (!application_unified_components_destroy(&owner->components,error) ||
        !application_unified_inputs_destroy(owner->inputs,error)) return false;
    owner->inputs=NULL; player_receipt_clear(owner); owner->source_dropped=false; return true;
}

bool application_unified_server_publish(application_unified_server *owner,
    const application_unified_output_external *external, qa_error *error)
{
    if (owner && owner->source_dropped && !application_unified_server_source_drop_finish(owner,error)) return false;
    if (owner && owner->bound && !owner->closed && !owner->admitted) return true;
    if (owner && owner->bound && !owner->closed && owner->session && !owner->preparing_frame &&
        !owner->pending.frame && !qa_unified_session_active(owner->session)) return true;
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
        bool ready = false;
        okay = qa_unified_session_control_ready(owner->session,
            owner->pending.controls[owner->control_cursor], &ready, error);
        if (!okay || !ready) { owner->entered = false; return okay; }
        okay = qa_unified_session_control(owner->session, owner->pending.controls[owner->control_cursor], error);
        if (okay) {
            uint32_t receipt=qa_unified_session_required(owner->session);
            if (!owner->control_cursor) owner->pending_first=receipt;
            owner->pending_last=receipt;
            ++owner->control_cursor;
        }
    }
    if (okay) okay = qa_unified_session_frame(owner->session, owner->pending.frame, error);
    owner->entered = false;
    if (okay) {
        application_unified_output_capture_commit(owner->pending_capture);
        application_unified_output_capture_dispose(owner->pending_capture); owner->pending_capture = NULL;
        owner->events_after = owner->pending_events_through;
        application_unified_output_dispose(&owner->pending); owner->control_cursor = 0;
        owner->pending_first=owner->pending_last=0;
        owner->preparing_frame = false;
    }
    return okay;
}

bool application_unified_server_publication_complete(const application_unified_server *owner)
{
    return owner && !owner->entered && !owner->pending.frame && !owner->pending_capture &&
        !owner->preparing_frame && !owner->control_cursor;
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
    player_receipt_clear(owner);
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
    owner->source_dropped=false;
    return true;
}
