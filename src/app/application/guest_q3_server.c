#include "guest_q3_private.h"
#include "guest_projection_private.h"
#include "native_q3_wire_state.h"
#include "guest_q3_combat.h"
#include "guest_q3_console.h"
#include "guest_q3_component_clients.h"
#include "map_players_private.h"
#include "unified_q3_events.h"
#include "source_projection.h"
#include "control_frame.h"
#include "qa/text.h"

char *q3g_copy_text(const char *text, qa_error *error)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) { application_fail(error, QA_ERROR_MEMORY, "retaining Q3 service text"); return NULL; }
    memcpy(copy, text, length + 1); return copy;
}

bool q3g_arguments(void *context, qa_native_host_command_view *out, qa_error *error)
{
    q3g_role *role = context;
    if (role->native_client && !role->arguments_scoped) {
        if (!application_native_q3_wire_client_arguments(role->native_client, out, error)) return false;
    } else if (role->kind != QA_QVM_GAME && !role->local_client && !role->arguments_scoped && role->common.arguments) {
        if (!role->common.arguments(role->common.context, out, error)) return false;
    } else {
        qa_command_tokens *args = role->kind == QA_QVM_GAME ? &role->engine->arguments : &role->arguments;
        *out = (qa_native_host_command_view){.count = args->count,
            .arguments = (const char *const *)args->values,
            .tail = args->args_text ? args->args_text : ""};
    }
    out->canonical_configstrings = role->kind == QA_QVM_CGAME && !role->arguments_scoped;
    return true;
}

static void print(void *context, const char *text)
{
    q3g_role *role = context;
    if (role->common.print) role->common.print(role->common.context, text);
}
static uint32_t milliseconds(void *context)
{
    q3g_role *role = context;
    return role->common.milliseconds ? role->common.milliseconds(role->common.context) :
        (uint32_t)role->engine->milliseconds;
}
static int32_t calendar(void *context, qa_q3_host_calendar *out)
{
    q3g_role *role = context;
    if (role->common.calendar) return role->common.calendar(role->common.context, out);
    if (out) *out = (qa_q3_host_calendar){0};
    return -1;
}
static bool installed_mods(void *context, qa_vfs_listing *out, qa_error *error)
{
    q3g_role *role = context;
    return role->common.installed_mods ? role->common.installed_mods(role->common.context, out, error) :
        application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 installed mod enumeration is unbound");
}
static bool clipboard(void *context, qa_buffer *out, qa_error *error)
{
    q3g_role *role = context;
    return role->common.clipboard ? role->common.clipboard(role->common.context, out, error) :
        application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 clipboard is unbound");
}
static bool client_command(void *context, const char *text, qa_error *error)
{
    q3g_role *role = context;
    if (role->native_client)
        return application_native_q3_wire_client_command(role->native_client, text, error);
    if (role->common.client_command) return role->common.client_command(role->common.context, text, error);
    if (role->client_engine && role->client < 64)
        return application_q3_guest_client_command(role->client_engine->provider, role->client, text, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 seat has no connected command recipient");
}
static bool configstring(void *context, uint32_t index, const char **out, qa_error *error)
{
    q3g_role *role = context;
    if (index >= QA_Q3_CONFIGSTRINGS) return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 configstring");
    *out = qa_q3_configstring(&role->engine->gamestate, index); return true;
}
static bool command_recipient(const struct application_q3_guest *engine, size_t slot)
{
    return !engine->clients[slot].pending_retirement && (engine->clients[slot].connected ||
        (engine->round.phase != Q3G_ROUND_NONE && engine->round.phase != Q3G_ROUND_FAILED &&
         (engine->round.carried & (UINT64_C(1) << slot))));
}
static bool retained_command_recipient(const struct application_q3_guest *engine, size_t slot)
{
    if (!command_recipient(engine, slot)) return false;
    const q3g_client *client = &engine->clients[slot];
    if (client->bot) return true;
    const qa_application *app = engine->provider->application;
    for (const application_provider *provider = app->live_providers; provider; provider = provider->next_live) {
        const struct application_q3_guest *receiver = q3g_engine((application_provider *)provider);
        if (!receiver) continue;
        for (const q3g_role *role = receiver->roles; role; role = role->next)
            if (role->kind == QA_QVM_CGAME && role->local_client && role->client_engine == engine &&
                role->client == slot && role->host && role->ready && !role->retired) return true;
    }
    return false;
}
static bool drop_client(void *,uint32_t,const char *,qa_error *);
static bool output_command(void *context,int32_t client,const char *text,
    const qa_unified_q3_event *event,qa_event_lease *lease,uint32_t config_index,uint32_t fragment,qa_error *error)
{
    q3g_role *role=context;
    if(client < -1 || client >= 64 || strlen(text)>=QA_Q3_COMMAND_CHARS)
        return application_fail(error,QA_ERROR_ARGUMENT,"invalid Q3 server command recipient or size");
    for(size_t i=0;i<64;++i) {
        if((client>=0 && i!=(size_t)client) || !retained_command_recipient(role->engine,i)) continue;
        bool overflow;
        if(!application_q3_command_admit(&role->engine->clients[i].server_commands,event->text,
            config_index,fragment,&overflow,error)) return false;
        if(overflow && !drop_client(role,(uint32_t)i,"Server command overflow",error)) return false;
    }
    return !role->server.send_command || role->server.send_command(role->server.context,
        client,text,event,lease,config_index,fragment,error);
}
static bool send_command(void *context,int32_t client,const char *text,
    const qa_unified_q3_event *event,qa_event_lease *lease,uint32_t config_index,uint32_t fragment,qa_error *error)
{
    (void)event;(void)lease;(void)config_index;(void)fragment;
    q3g_role *role = context;
    if (client < -1 || client >= 64 || !text || strlen(text) >= QA_Q3_COMMAND_CHARS)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 server command recipient or size");
    return application_unified_q3_text(role->engine->provider,
        APPLICATION_Q3_SOURCE_COMMAND, client, text, error);
}

bool q3g_output(application_provider *provider,const qa_unified_q3_event *event,
    qa_event_lease *lease,qa_error *error)
{
    struct application_q3_guest *engine=q3g_engine(provider);
    if(!engine || !engine->game) return true;
    return application_q3_output_fanout(event,lease,output_command,engine->game,error);
}

bool q3g_set_configstring(q3g_role *role, uint32_t index, const char *text, qa_error *error)
{
    if (index >= QA_Q3_CONFIGSTRINGS || !text)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 configstring update");
    const char *prior = qa_q3_configstring(&role->engine->gamestate, index);
    if (!strcmp(prior, text)) return true;
    return qa_q3_configstring_set(&role->engine->gamestate, index, text, error) &&
        application_unified_q3_configstring(role->engine->provider, index, text, error);
}

bool q3g_publish_information(q3g_role *role, bool force, qa_error *error)
{
    qa_cvars *cvars = application_guest_q3_console_registry(role->engine->provider);
    if (!cvars)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 server information requires its GAME registry");
    const uint32_t masks[] = {QA_CVAR_SERVERINFO, QA_CVAR_SYSTEMINFO};
    const uint32_t information = masks[0] | masks[1];
    uint32_t modified = qa_cvars_take_modified_flags(cvars);
    qa_cvars_mark_modified_flags(cvars, modified & ~information);
    for (uint32_t index = 0; index < 2; ++index) {
        if (!force && !(modified & masks[index])) continue;
        char text[8192];
        bool ok = qa_q3_host_info_write(role->host, masks[index], index ? sizeof(text) : 1024, text, error) &&
            q3g_set_configstring(role, index, text, error);
        if (!ok) {
            qa_cvars_mark_modified_flags(cvars, modified & information);
            return false;
        }
    }
    return true;
}

static bool set_configstring(void *context, uint32_t index, const char *text, qa_error *error)
{
    return q3g_set_configstring(context, index, text, error);
}
static bool userinfo(void *context, uint32_t client, const char **out, qa_error *error)
{
    q3g_role *role = context;
    if (client >= 64) return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 userinfo client");
    *out = role->engine->clients[client].userinfo ? role->engine->clients[client].userinfo : ""; return true;
}
static bool set_userinfo(void *context, uint32_t client, const char *text, qa_error *error)
{
    q3g_role *role = context;
    if (client >= 64 || !text) return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 userinfo update");
    char *copy = q3g_copy_text(text, error); if (!copy) return false;
    if (role->server.set_userinfo && !role->server.set_userinfo(role->server.context, client, text, error)) {
        free(copy); return false;
    }
    free(role->engine->clients[client].userinfo); role->engine->clients[client].userinfo = copy; return true;
}
static bool user_command(void *context, uint32_t client, qa_q3_usercmd *out, qa_error *error)
{
    q3g_role *role = context;
    if (client >= 64) return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 user command client");
    *out = role->engine->clients[client].command; return true;
}
static bool drop_client(void *context, uint32_t client, const char *reason, qa_error *error)
{
    q3g_role *role = context;
    if (client >= 64) return application_fail(error, QA_ERROR_ARGUMENT, "invalid dropped Q3 client");
    application_provider *provider = role->engine->provider;
    qa_actor_id actor = role->engine->clients[client].actor;
    if (application_player_record_find(provider->application->players, actor))
        return application_guest_client_drop(provider, client, reason, error) &&
            application_clients_reject(provider->application, provider->owner, actor,
                role->engine->clients[client].retirement_reason, NULL, error);
    if (role->server.drop_client &&
        !role->server.drop_client(role->server.context, client, reason, error)) return false;
    return application_guest_client_drop(role->engine->provider, client, reason, error);
}
static bool allocate_bot(void *context, int32_t *out, qa_error *error)
{
    q3g_role *role = context;
    bool allocated = role->server.allocate_bot ?
        role->server.allocate_bot(role->server.context, out, error) :
        application_guest_bot_allocate(role->engine->provider, out, error);
    if (!allocated) return false;
    if (*out == -1) return true;
    if (*out < 0 || *out >= 64 || role->engine->clients[*out].allocated ||
        role->engine->seats[*out] != UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 bot allocator returned an unavailable source slot");
    qa_actor_id actor;
    if (!qa_q3_host_actor(role->host, (uint32_t)*out, false, &actor, error) || !actor.registry ||
        !qa_actors_get(qa_session_actors(role->engine->provider->application->session), actor))
        return application_fail(error, QA_ERROR_FORMAT, "Q3 bot allocator returned no canonical source actor");
    if (!q3g_client_actor_set(role->engine, (uint32_t)*out, actor, error)) return false;
    role->engine->clients[*out].pending_bot = true;
    role->engine->clients[*out].allocated = role->engine->clients[*out].bot = true;
    return true;
}
static bool free_bot(void *context, int32_t client, qa_error *error)
{
    q3g_role *role = context;
    if (client < 0 || client >= 64)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid freed Q3 bot client");
    if (!role->engine->clients[client].bot) return true;
    if (role->server.free_bot && !role->server.free_bot(role->server.context, client, error)) return false;
    return application_guest_bot_free(role->engine->provider, client, error);
}
static bool bot_snapshot_entity(void *context, int32_t client, int32_t sequence, int32_t *out, qa_error *error)
{
    q3g_role *role = context;
    if (role->server.bot_snapshot_entity)
        return role->server.bot_snapshot_entity(role->server.context, client, sequence, out, error);
    if (client < 0 || client >= 64 || sequence < 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 bot snapshot entity");
    q3g_client *source = &role->engine->clients[client];
    const qa_q3_snapshot_slot *snapshot = &source->snapshots[(uint32_t)source->snapshot_sequence & (QA_Q3_PACKET_BACKUP - 1)];
    *out = source->has_snapshot && (size_t)sequence < snapshot->value.entity_count ?
        snapshot->value.entities[sequence].number : -1; return true;
}
static bool bot_console_message(void *context, int32_t client, const char **out, qa_error *error)
{
    q3g_role *role = context;
    if (!qa_application_q3_output_drain(role->engine->provider->application, error)) return false;
    if (role->server.bot_console_message)
        return role->server.bot_console_message(role->server.context, client, out, error);
    if (client < 0 || client >= 64) return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 bot console client");
    q3g_client *source = &role->engine->clients[client];
    *out = NULL;
    if (source->consumed_server_command < source->server_commands.sequence) {
        int32_t sequence = source->consumed_server_command + 1;
        if (!application_q3_command_lookup(&source->server_commands, sequence, out, error)) return false;
        source->consumed_server_command = sequence;
        bool clamped;
        return application_q3_command_ack(&source->server_commands,
                                  source->consumed_server_command, &clamped, error);
    }
    return true;
}
static bool bot_user_command(void *context, int32_t client, const qa_q3_usercmd *command, qa_error *error)
{
    q3g_role *role = context;
    if (role->server.bot_user_command)
        return role->server.bot_user_command(role->server.context, client, command, error);
    if (client < 0 || client >= 64 || !role->engine->clients[client].bot)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 bot user command");
    if (!application_guest_bots_admit(role->engine->provider, error)) return false;
    const qa_actor_record *actor = qa_actors_get(
        qa_session_actors(role->engine->provider->application->session), role->engine->clients[client].actor);
    if (!actor) return true;
    return application_control_frames_receive_guest_bot(role->engine->provider->application,
        actor->id, command, error);
}
static bool admit_actor(void *context, qa_actor_id actor, qa_error *error)
{
    q3g_role *role = context;
    bool admitted = role->server.admit_actor ? role->server.admit_actor(role->server.context, actor, error) :
        application_guest_actor_admit(role->engine->provider, actor, error);
    if (!admitted) return false;
    if (!role->combat) return true;
    uint32_t slot;
    qa_q3_host_game_data table;
    if (!qa_q3_host_actor_slot(role->host, actor, &slot, error)) return false;
    if (!qa_q3_host_game_data_read(role->host, &table))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original combat admission requires its located Source records");
    if (slot < table.client_count && !role->engine->clients[slot].begun) return true;
    return application_q3_combat_admit(role->combat, actor, error);
}
static bool player_velocity(void *context, qa_actor_id actor, qa_vec3 velocity, qa_error *error)
{
    q3g_role *role = context;
    return role->server.player_velocity ? role->server.player_velocity(role->server.context, actor, velocity, error) :
        application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 canonical player velocity owner is unbound");
}
static qa_actor_id world_actor(void *context)
{
    q3g_role *role = context;
    return role->server.world_actor ? role->server.world_actor(role->server.context) : (qa_actor_id){0};
}

static bool projection_span(void *context, uint64_t address, size_t size,
    qa_bytes *out, qa_error *error)
{
    q3g_role *role = context;
    if (role->native)
        return qa_native_borrow(qa_native_host_instance(role->native), address, size, out, error);
    if (address > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 source projection exceeds its QVM pointer word");
    return qa_qvm_span(role->vm, (int32_t)address, 0, size, out, error);
}

static bool projection_write(void *context, uint64_t address, qa_bytes bytes,
    bool private_fields, qa_error *error)
{
    q3g_role *role = context;
    return private_fields ? qa_q3_host_source_bytes_project(role->host, address, bytes, error) :
        qa_q3_host_source_bytes_write(role->host, address, bytes, error);
}

static bool projection_invoke(void *context, const qa_qvm_call *call, uint32_t entry,
    const qa_native_value *arguments, size_t count, qa_native_value *out, qa_error *error)
{
    q3g_role *role = context;
    q3g_role *previous = role->engine->entered_role;
    role->engine->entered_role = role;
    ++role->engine->calls;
    uint64_t address = entry;
    bool okay = (!role->native || qa_native_rva(qa_native_host_instance(role->native),
        entry, 1, &address, error)) &&
        qa_q3_host_source_call(role->host, call, address, arguments, count, out, error);
    --role->engine->calls;
    role->engine->entered_role = previous;
    return okay;
}

static bool projection_read(q3g_role *role, application_source_projection *out)
{
    if (!role->vm && !role->native) return false;
    const application_q3_combat_definition *combat = role->artifact ?
        application_q3_combat_profile_definition(role->artifact->combat_profile) : NULL;
    const application_q3_pickup_profile *pickup = &role->pickup_profile;
    if (!combat && !pickup->present) return false;
    *out = (application_source_projection){.host = role->host, .abi = role->abi,
        .entity_stride = combat ? combat->entity_stride : pickup->entity_stride,
        .inuse = combat ? combat->fields.inuse : pickup->fields.inuse,
        .health = combat ? combat->fields.health : pickup->fields.health,
        .takedamage = combat ? combat->fields.takedamage : 0,
        .allocate = combat ? combat->callbacks.allocate : pickup->allocate,
        .free = combat ? combat->callbacks.free : pickup->free,
        .has_allocate = combat ? combat->callbacks.has_allocate : pickup->allocate != 0,
        .has_free = combat ? combat->callbacks.has_free : pickup->free != 0,
        .context = role, .span = projection_span, .write = projection_write,
        .invoke = projection_invoke};
    return true;
}

bool q3g_project_actor(void *context, const qa_qvm_call *call, qa_actor_id actor,
    uint32_t *out, qa_error *error)
{
    q3g_role *role = context;
    application_source_projection projection;
    if (!projection_read(role, &projection))
        return role->server.project_actor ? role->server.project_actor(role->server.context,
            call, actor, out, error) : qa_q3_host_actor_slot(role->host, actor, out, error);
    qa_application *application = role->engine->provider->application;
    qa_actor_player *player = qa_actors_player(qa_session_actors(application->session), actor);
    bool client = player && player->present;
    qa_body_state body; qa_combat_state combat = {0}; bool created;
    if (qa_combat_has(application->combat, actor) &&
        !qa_combat_read_traits(application->combat, actor, &combat, error)) return false;
    if (client) {
        if (!qa_q3_host_actor_slot(role->host, actor, out, error)) return false;
    } else if (!qa_world_body_read(application->world, actor, &body, error) ||
        !application_source_projection_borrow(&projection, call, actor, out, &created, error)) return false;
    projection.can_take_damage = combat.can_take_damage;
    int32_t health = qa_source_to_i32(combat.health);
    return application_source_projection_publish(&projection, *out, client ? NULL : &body, &health,
        (uint64_t)(uint32_t)role->engine->milliseconds * UINT64_C(1000000), error);
}

bool q3g_projection_after_free(q3g_role *role, uint64_t pointer, qa_error *error)
{
    application_source_projection projection;
    if (!projection_read(role, &projection)) return true;
    qa_q3_host_game_data data; uint32_t slot;
    if (!application_source_projection_slot(&projection, pointer, &slot, error) ||
        !qa_q3_host_game_data_read(role->host, &data)) return false;
    if (slot < data.client_count) return true;
    qa_actor_id actor;
    if (!qa_q3_host_actor(role->host, slot, false, &actor, error)) return false;
    const qa_actor_record *record = qa_actors_get(
        qa_session_actors(role->engine->provider->application->session), actor);
    return !record || record->owner == role->engine->provider->owner ||
        (application_source_projection_keep(&projection, slot, error) &&
            q3g_project_actor(role, NULL, actor, &slot, error));
}

bool q3g_projection_actor_released(q3g_role *role, qa_actor_record actor, qa_error *error)
{
    application_source_projection projection; uint32_t slot = 0; qa_q3_host_game_data data;
    bool borrowed = actor.owner != role->engine->provider->owner &&
        projection_read(role, &projection) && projection.has_free &&
        qa_q3_host_actor_slot(role->host, actor.id, &slot, NULL) &&
        qa_q3_host_game_data_read(role->host, &data) && slot >= data.client_count && slot < QA_Q3_ENTITY_WORLD;
    return qa_q3_host_actor_released(role->host, actor, error) &&
        (!borrowed || application_source_projection_release(&projection, NULL, slot, error));
}

void q3g_server_bind(q3g_role *role, qa_q3_host_options *options)
{
    role->common = options->common; role->server = options->server;
    options->console_command=q3g_declared_command;
    options->console_command_context=role;
    options->common = (qa_q3_host_common_services){role, print, milliseconds, calendar,
        q3g_arguments, client_command, installed_mods, clipboard};
    if (role->kind != QA_QVM_GAME) return;
    options->server = (qa_q3_host_server_services){.context = role, .maximum_clients = 64,
        .project_actor = q3g_project_actor,
        .configstring = configstring, .set_configstring = set_configstring,
        .userinfo = userinfo, .set_userinfo = set_userinfo, .user_command = user_command,
        .drop_client = drop_client, .send_command = send_command, .allocate_bot = allocate_bot,
        .free_bot = free_bot, .bot_snapshot_entity = bot_snapshot_entity,
        .bot_console_message = bot_console_message, .bot_user_command = bot_user_command,
        .admit_actor = admit_actor, .player_velocity = player_velocity, .world_actor = world_actor};
}
