#include "native_q1_console.h"
#include "q3_product.h"
#include "native_q1_wire.h"
#include "guest_qc_profile.h"
#include "startup_flow.h"
#include "map_players_private.h"
#include "qa/cvars_save.h"
#include "qa/console_cvars_prepare.h"
#include "qa/network_q1_qw.h"
#include "qa/source_save.h"
#include "qa/game_q1_source_obituary.h"
#include "qa/game_q1_bots.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

typedef struct native_q1_chat_client {
    bool occupied, remote;
    uint32_t local_seat;
    qa_net_client_id client;
    qa_net_seat_id seat;
    uint64_t times[10], locked_until_ns;
    uint8_t head;
} native_q1_chat_client;
typedef struct native_q1_chat_policy {
    uint32_t messages, persecond, secondsdead;
    char message[255];
    native_q1_chat_client clients[32];
} native_q1_chat_policy;

struct application_native_q1_console {
    application_provider *provider;
    qa_console *console;
    qa_cvars *cvars;
    size_t calls;
    /* svs.info and localinfo survive SV_Spawn; reliable_datagram does not. */
    char serverinfo[513], localinfo[32769];
    uint8_t reliable_info[1450];
    size_t reliable_info_size;
    bool info_initialized;
    qa_error info_error;
    const qa_command_context *info_context;
    native_q1_chat_policy chat;
};

static bool files_source(application_provider *,const qa_command_context *,qa_application_startup_source *,qa_error *);

static bool chat_flood(struct application_native_q1_console *owner,const qa_command_invocation *command,
    const application_native_q1_chat_sender *sender,char denial[320],qa_error *error)
{
    const application_player_record *player=sender->player;
    if (!player || sender->client_slot>=32)
        return application_fail(error,QA_ERROR_ARGUMENT,"QW flood policy lost its physical Source client");
    denial[0]=0;
    if (!owner->chat.messages) return true;
    const qa_application_startup_hooks *hooks=owner->provider->application->startup_hooks;
    qa_command_context context=command->context; context.owner=owner->provider->owner;
    qa_application_startup_source source; uint64_t now;
    if (!hooks || !hooks->source_command_realtime || !files_source(owner->provider,&context,&source,error) ||
        !hooks->source_command_realtime(hooks->context,owner->provider->application,&source,command,
            player->remote,&now,error))
        return error && error->code!=QA_OK?false:
            application_fail(error,QA_ERROR_ARGUMENT,"QW flood policy has no reached realtime owner");
    native_q1_chat_client *row=owner->chat.clients+sender->client_slot;
    bool same=row->occupied && row->remote==player->remote && (player->remote?
        qa_net_client_id_equal(row->client,player->remote_client) &&
        row->seat.owner==player->remote_seat.owner && row->seat.index==player->remote_seat.index:
        row->local_seat==player->seat);
    if (!same) *row=(native_q1_chat_client){.occupied=true,.remote=player->remote,
        .local_seat=player->remote?0:player->seat,.client=player->remote?player->remote_client:(qa_net_client_id){0},
        .seat=player->remote?player->remote_seat:(qa_net_seat_id){0}};
    bool paused=qa_application_q1_paused(owner->provider->application);
    if (!paused && now<row->locked_until_ns) {
        snprintf(denial,320,"You can't talk for %" PRIu64 " more seconds\n",
            (row->locked_until_ns-now)/UINT64_C(1000000000)); return true;
    }
    uint64_t previous=row->times[(row->head+11u-owner->chat.messages)%10u];
    if (!paused && previous && (now<previous || now-previous<(uint64_t)owner->chat.persecond*UINT64_C(1000000000))) {
        uint64_t duration=(uint64_t)owner->chat.secondsdead*UINT64_C(1000000000);
        row->locked_until_ns=now>UINT64_MAX-duration?UINT64_MAX:now+duration;
        if (owner->chat.message[0]) snprintf(denial,320,"FloodProt: %s\n",owner->chat.message);
        else snprintf(denial,320,"FloodProt: You can't talk for %u seconds.\n",owner->chat.secondsdead);
        return true;
    }
    row->head=(uint8_t)((row->head+1u)%10u); row->times[row->head]=now;
    return true;
}

qa_cvars *application_native_q1_console_registry(const application_provider *provider)
{
    return provider && provider->kind == APPLICATION_PROVIDER_Q1 && provider->native_q1_console
        ? provider->native_q1_console->cvars : NULL;
}

bool application_native_q1_console_idle(const application_provider *provider)
{
    const struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    return !owner || (!owner->calls && qa_console_idle(owner->console));
}

static qa_ruleset_id dialect(const application_provider *provider)
{
    return provider->launch->selection.clock.kind == QA_RULESET_QUAKEWORLD ? QA_RULESET_QUAKEWORLD : QA_RULESET_NETQUAKE;
}

static bool qc_chat_source(application_provider *provider, qa_actor_id actor,
    qa_q1_chat_mode mode, const char *target, application_native_q1_chat_sender *sender,
    qa_actor_id *recipients, size_t *count, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    qa_qc_instance *vm = provider->state.qc.instance;
    const qa_qc_definition *netname = application_qc_field(engine, "netname", QA_QC_STRING, error);
    if (!netname) return false;
    const qa_cvar_view *teamplay = qa_cvars_find(engine->cvars, "teamplay");
    bool filtered = actor.registry && mode == QA_Q1_CHAT_TEAM && teamplay->number != 0;
    float sender_team = 0;
    int32_t reference, name;
    if (actor.registry) {
        if (!qa_qc_actor_reference(vm, actor, false, &reference, error) ||
            !qa_qc_entity_int(vm, reference, netname->offset, &name, error) ||
            !qa_qc_string(vm, name, &sender->name, error) ||
            (filtered && !application_qc_float(engine, reference, "team", &sender_team, error))) return false;
    } else sender->name = qa_cvars_find(engine->cvars, "hostname")->value;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        const application_qc_client *client = engine->clients + slot;
        if (!client->connected || !client->spawned) continue;
        if (target || filtered) {
            if (!qa_qc_actor_reference(vm, client->actor, false, &reference, error)) return false;
            if (target) {
                const char *recipient_name;
                if (!qa_qc_entity_int(vm, reference, netname->offset, &name, error) ||
                    !qa_qc_string(vm, name, &recipient_name, error)) return false;
                if (!application_qc_command_name_equal(target, recipient_name)) continue;
            } else {
                float team;
                if (!application_qc_float(engine, reference, "team", &team, error)) return false;
                if (team != sender_team) continue;
            }
        }
        recipients[(*count)++] = client->actor;
        if (target) break;
    }
    return true;
}

bool application_q1_chat(application_provider *provider,
    const qa_command_invocation *command, qa_q1_chat_mode mode, qa_error *error)
{
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    bool native = provider && provider->kind == APPLICATION_PROVIDER_Q1;
    bool qw=provider && provider->launch && provider->launch->selection.clock.kind==QA_RULESET_QUAKEWORLD;
    if (native && (!owner || !command ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->launch || (provider->launch->selection.clock.kind != QA_RULESET_NETQUAKE && !qw) ||
        command->context.dialect != dialect(provider) ||
        (command->context.owner && command->context.owner != provider->owner) ||
        (mode != QA_Q1_CHAT_ALL && mode != QA_Q1_CHAT_TEAM &&
         mode != QA_Q1_CHAT_TELL) || (qw && mode==QA_Q1_CHAT_TELL) ||
        (!command->context.actor.registry && (mode == QA_Q1_CHAT_TELL ||
         (qw && mode!=QA_Q1_CHAT_ALL) ||
         command->context.owner != provider->owner || command->context.origin != QA_COMMAND_SERVER ||
         command->console != owner->console)) ||
        !qa_application_command_context_active(provider->application, &command->context)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 chat lost its actual Source sender");
    if (command->argc < (mode == QA_Q1_CHAT_TELL ? 3u : 2u)) return true;
    if (!command->args_text || (mode == QA_Q1_CHAT_TELL &&
        (!command->argv || !command->argv[1])))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 chat lost its Source command arguments");
    application_native_q1_wire_source source = {0};
    if (native && !application_native_q1_wire_retain(provider, &source, error)) return false;
    if (native) ++owner->calls;
    size_t client_capacity = native ? 255u : provider->state.qc.engine->max_clients;
    qa_actor_id recipients[client_capacity]; size_t count = 0;
    application_native_q1_chat_sender sender={0};
    const char *target = mode == QA_Q1_CHAT_TELL ? command->argv[1] : NULL;
    bool okay = native ? application_native_q1_wire_chat(&source, command->context.actor,
        mode == QA_Q1_CHAT_TEAM, target, &sender, recipients, &count, error) :
        qc_chat_source(provider, command->context.actor, mode, target, &sender, recipients, &count, error);
    char line[2048], denial[320]={0};
    if (okay && qw && sender.player) okay=chat_flood(owner,command,&sender,denial,error);
    bool denied=denial[0]!=0;
    size_t capacity=qw?(sender.player?sizeof(line):1024u):64u, length=0;
    if (okay && denied) {
        length=strlen(denial); memcpy(line,denial,length+1);
        recipients[0]=command->context.actor; count=1;
    } else if (okay) {
        const char *format=qw?(sender.spectator_only?"[SPEC] %.31s: ":
            mode==QA_Q1_CHAT_TEAM?"(%.31s): ":"%.31s: "):
            mode==QA_Q1_CHAT_TELL?"%s: ":command->context.actor.registry?"\001%s: ":"\001<%s> ";
        int written=snprintf(line,capacity,format,sender.name);
        if (written<0 || (size_t)written>capacity-2)
            okay=application_fail(error,QA_ERROR_FORMAT,"Native Q1 chat sender exceeds the Source line extent");
        else {
            size_t prefix=(size_t)written;
            const char *body=command->args_text; size_t body_size=strlen(body);
            if (*body=='"') { ++body; --body_size; if (body_size) --body_size; }
            if (qw && body_size>capacity-2-prefix)
                okay=application_fail(error,QA_ERROR_FORMAT,"QuakeWorld chat exceeds its Source line extent");
            else {
                if (body_size>capacity-2-prefix) body_size=capacity-2-prefix;
                memcpy(line+prefix,body,body_size); length=prefix+body_size;
                line[length++]='\n'; line[length]=0;
            }
        }
    }
    qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .family = QA_GAME_Q1,
        .provider = provider->owner, .flags = 2u | QA_Q1_SOURCE_MESSAGE_LITERAL, .code = 3};
    if (okay) {
        double seconds;
        if (native) okay = qa_q1_game_clock_read(provider->state.q1, &event.time_ns, &seconds);
        else event.time_ns = provider->state.qc.engine->source_time_ns;
        okay = okay && qa_strings_intern(qa_session_strings(provider->application->session),
                (qa_bytes){(const uint8_t *)line, length}, &event.text, error);
        if (!okay && error && error->code == QA_OK)
            application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 chat lost its actual Source clock");
    }
    for (size_t i = 0; okay && i < count; ++i) {
        event.actor = recipients[i];
        okay = application_emit(provider->application, &event, error);
    }
    if (okay && !denied && mode != QA_Q1_CHAT_TELL && (!qw || sender.player)) {
        fputs(qw?line:line+1, stdout);
    }
    if (okay && native && (!qa_q1_wire_receipt_current(&source.receipt) || provider->close_pending ||
        application_world_provider(provider->application, QA_ROLE_ENTITIES, "") != provider))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 chat Source retired during delivery");
    if (native) {
        --owner->calls;
        application_native_q1_wire_end(&source);
    }
    return okay;
}

static bool capture(void *opaque, const qa_command_context *source,
                    qa_command_context *out, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    qa_command_context command = *source;
    if (command.owner && command.owner != owner->provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console received another source owner");
    command.owner = owner->provider->owner;
    command.dialect = dialect(owner->provider);
    return application_command_capture(owner->provider->application, &command, out, error);
}

static bool active(void *opaque, const qa_command_context *command)
{
    struct application_native_q1_console *owner = opaque;
    return command && command->owner == owner->provider->owner &&
        command->dialect == dialect(owner->provider) &&
        application_command_active(owner->provider->application, command);
}

static void print(void *opaque, const qa_command_context *command, const char *text)
{
    struct application_native_q1_console *owner = opaque;
    ++owner->calls;
    application_console_print(owner->provider->application, command, text);
    --owner->calls;
}

static void cvar_print(void *opaque, const char *text)
{
    struct application_native_q1_console *owner = opaque;
    qa_console_emit(owner->console, NULL, text);
}
void application_native_q1_source_console_print(void *opaque, const char *text)
{
    application_provider *provider = opaque;
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (owner && provider->kind == APPLICATION_PROVIDER_Q1 && text)
        qa_console_emit(owner->console, NULL, text);
}
static const qa_launch_instance *source_descriptor(application_provider *provider)
{
    if (!provider || !provider->application || !provider->launch) return NULL;
    qa_application *app = provider->application;
    const qa_launch_snapshot *snapshots[] = {app->routing_snapshot,
        qa_application_startup_candidate(app), qa_application_launch(app)};
    for (size_t i = 0; i < sizeof(snapshots) / sizeof(*snapshots); ++i) {
        const qa_launch_instance *instance = snapshots[i]
            ? qa_launch_snapshot_find(snapshots[i], provider->launch->selection.instance) : NULL;
        if (instance && instance->state == provider && instance->storage == provider->launch->storage)
            return instance;
    }
    return NULL;
}
bool application_native_q1_console_engine_borrow(qa_application *app,
    const qa_command_invocation *command, struct application_native_q1_console **out,
    qa_error *error)
{
    if (!app || !command || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 engine command needs its actual invocation");
    *out = NULL;
    if (command->console != app->console || !command->context.actor.registry ||
        (command->context.dialect != QA_RULESET_NETQUAKE && command->context.dialect != QA_RULESET_QUAKEWORLD))
        return true;
    if (!qa_console_invocation_current(app->console, command) ||
        !qa_application_command_context_active(app, &command->context))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 engine command has no entered invocation");
    qa_actor_id actor = command->context.actor;
    application_provider *candidates[] = {
        application_world_provider(app, QA_ROLE_ENTITIES, ""),
        application_provider_for(app, actor, QA_ROLE_CHARACTER, "")};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(*candidates); ++i) {
        application_provider *source = candidates[i];
        qa_q1_source_client_view client;
        if (!source || source->kind != APPLICATION_PROVIDER_Q1 || !source->state.q1 ||
            !qa_q1_source_client_read(source->state.q1, actor, &client)) continue;
        struct application_native_q1_console *owner = source->native_q1_console;
        if (app->destroy_requested || app->finalizing || !app->players ||
            source->application != app || !source->constructed || !source->attached ||
            source->close_pending || !source_descriptor(source) || !owner ||
            owner->provider != source || command->context.dialect != dialect(source) ||
            (command->context.owner && command->context.owner != source->owner))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 engine command lost its published Source");
        for (size_t j = 0; j < app->players->count; ++j) {
            const application_player_record *player = app->players->records + j;
            if (!player->retiring && qa_actor_id_equal(player->actor, actor) &&
                player->client_slot == client.slot) {
                ++owner->calls;
                *out = owner;
                return true;
            }
        }
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 engine command differs from its Source roster");
    }
    return true;
}

void application_native_q1_console_engine_release(struct application_native_q1_console *owner)
{
    if (owner) --owner->calls;
}

static bool files_source(application_provider *provider,const qa_command_context *command,
    qa_application_startup_source *out,qa_error *error)
{
    struct application_native_q1_console *owner=provider?provider->native_q1_console:NULL;
    const qa_launch_instance *descriptor=source_descriptor(provider);
    if (!owner || !descriptor || dialect(provider)!=QA_RULESET_QUAKEWORLD)
        return application_fail(error,QA_ERROR_ARGUMENT,"Native Source filesystem lost its actual QW descriptor");
    *out=(qa_application_startup_source){.descriptor=descriptor,
        .scope={.provider=provider->owner,.kind=QA_APPLICATION_CONSOLE_Q1_GAME},
        .console=owner->console,.cvars=owner->cvars,.declaration_owner=provider->owner,
        .command=command?*command:(qa_command_context){.owner=provider->owner,.dialect=QA_RULESET_QUAKEWORLD,.origin=QA_COMMAND_SERVER}};
    out->command.cvar_view=qa_cvars_view_identity(owner->cvars);
    return true;
}
bool application_native_q1_source_files(application_provider *provider,qa_launch_source_files *out,
    const char **directory,qa_error *error)
{
    qa_application_startup_source source;
    const qa_application_startup_hooks *hooks=provider && provider->application?provider->application->startup_hooks:NULL;
    return (hooks && hooks->source_files && files_source(provider,NULL,&source,error))?
        hooks->source_files(hooks->context,provider->application,&source,out,directory,error):
        application_fail(error,QA_ERROR_ARGUMENT,"Native Source filesystem has no actual retained service owner");
}
static bool log_source(application_provider *provider,qa_application_startup_source *out)
{
    struct application_native_q1_console *owner=provider?provider->native_q1_console:NULL;
    if (!owner || provider->kind!=APPLICATION_PROVIDER_Q1 || !provider->constructed ||
        !provider->attached || provider->close_pending || !provider->launch ||
        provider->launch->selection.clock.kind!=QA_RULESET_QUAKEWORLD) return false;
    const qa_launch_instance *descriptor = source_descriptor(provider);
    if (!descriptor) return false;
    *out=(qa_application_startup_source){.descriptor=descriptor,
        .scope={.provider=provider->owner,.kind=QA_APPLICATION_CONSOLE_Q1_GAME},
        .console=owner->console,.cvars=owner->cvars,
        .command={.owner=provider->owner,.dialect=QA_RULESET_QUAKEWORLD,.origin=QA_COMMAND_SERVER},
        .declaration_owner=provider->owner};
    return true;
}
void application_native_q1_source_logfrag_write(void *opaque,const char *record)
{
    application_provider *provider=opaque; qa_application_startup_source source;
    if (!record || !log_source(provider,&source)) return;
    const qa_application_startup_hooks *hooks=provider->application->startup_hooks;
    if (!hooks || !hooks->qw_logfrag_write) return;
    ++provider->native_q1_console->calls;
    hooks->qw_logfrag_write(hooks->context,provider->application,&source,record);
    --provider->native_q1_console->calls;
}
bool application_native_q1_source_logfrag_enabled(application_provider *provider,
    bool *enabled,qa_error *error)
{
    qa_application_startup_source source;
    if (!enabled || !log_source(provider,&source))
        return application_fail(error,QA_ERROR_ARGUMENT,"QuakeWorld frag file lost its physical console owner");
    *enabled=false;
    const qa_application_startup_hooks *hooks=provider->application->startup_hooks;
    if (!hooks || !hooks->qw_logfrag_enabled) return true;
    ++provider->native_q1_console->calls;
    bool okay=hooks->qw_logfrag_enabled(hooks->context,provider->application,&source,enabled,error);
    --provider->native_q1_console->calls;
    return okay;
}

/* Keep the Source byte dictionary: filtering can create empty keys and
 * duplicates, and Info_RemoveKey removes only the first matching pair. */
static bool info_pair(const char **cursor, const char **key, size_t *key_size,
    const char **value, size_t *value_size, const char **start)
{
    const char *s = *cursor;
    *start = s;
    if (*s == '\\') ++s;
    *key = s;
    while (*s && *s != '\\') ++s;
    if (!*s) return false;
    *key_size = (size_t)(s - *key);
    *value = ++s;
    while (*s && *s != '\\') ++s;
    *value_size = (size_t)(s - *value);
    *cursor = s;
    return true;
}
static void info_value(const char *info, const char *wanted, const char **out, size_t *size)
{
    const char *cursor = info, *key, *value, *start;
    size_t key_size, value_size, wanted_size = strlen(wanted);
    while (info_pair(&cursor, &key, &key_size, &value, &value_size, &start)) {
        if (key_size == wanted_size && !memcmp(key, wanted, key_size)) {
            *out = value; *size = value_size; return;
        }
        if (!*cursor) break;
    }
    *out = ""; *size = 0;
}
static void info_remove(char *info, const char *wanted)
{
    const char *cursor = info, *key, *value, *start;
    size_t key_size, value_size, wanted_size = strlen(wanted);
    while (info_pair(&cursor, &key, &key_size, &value, &value_size, &start)) {
        if (key_size == wanted_size && !memcmp(key, wanted, key_size)) {
            memmove((char *)start, cursor, strlen(cursor) + 1); return;
        }
        if (!*cursor) break;
    }
}
static void info_print(struct application_native_q1_console *owner, const char *text)
{
    qa_console_emit(owner->console, owner->info_context, text);
}
static void info_set(struct application_native_q1_console *owner, char *info,
    size_t maximum, const char *key, const char *value, bool star)
{
    if (!star && *key == '*') { info_print(owner, "Can't set * keys\n"); return; }
    if (strchr(key, '\\') || strchr(value, '\\')) {
        info_print(owner, "Can't use keys or values with a \\\n"); return;
    }
    if (strchr(key, '"') || strchr(value, '"')) {
        info_print(owner, "Can't use keys or values with a \"\n"); return;
    }
    size_t key_size = strlen(key), value_size = strlen(value);
    if (key_size > 63 || value_size > 63) {
        info_print(owner, "Keys and values must be < 64 characters.\n"); return;
    }
    const char *old; size_t old_size;
    info_value(info, key, &old, &old_size);
    size_t size = strlen(info);
    if (old_size && size - old_size + value_size > maximum) {
        info_print(owner, "Info string length exceeded\n"); return;
    }
    info_remove(info, key);
    if (!value_size) return;
    size = strlen(info);
    if (key_size + value_size + 2 + size > maximum) {
        info_print(owner, "Info string length exceeded\n"); return;
    }
    char pair[129];
    pair[0] = '\\'; memcpy(pair + 1, key, key_size);
    pair[key_size + 1] = '\\'; memcpy(pair + key_size + 2, value, value_size);
    pair[key_size + value_size + 2] = 0;
    const qa_cvar_view *highchars = qa_cvars_find(owner->cvars, "sv_highchars");
    bool high = highchars && highchars->number != 0;
    for (const unsigned char *cursor = (const unsigned char *)pair; *cursor; ++cursor) {
        unsigned char c = *cursor;
        if (!high) { c &= 127; if (c < 32) continue; }
        if (c > 13) info[size++] = (char)c;
    }
    info[size] = 0;
}
static void info_change(struct application_native_q1_console *owner,
    const char *key, const char *value)
{
    application_provider *p = owner->provider;
    /* SV_SendServerInfoChange is inert before the actual server exists. */
    if (!p->constructed || !p->attached || !p->state.q1 || !qa_q1_game_map_name(p->state.q1)) return;
    if (owner->info_error.code != QA_OK) return;
    qa_net_writer writer;
    qa_net_writer_init(&writer, owner->reliable_info + owner->reliable_info_size,
        sizeof(owner->reliable_info) - owner->reliable_info_size, &owner->info_error);
    qa_net_write_u8(&writer, QA_QW_SERVER_INFO);
    qa_net_write_string(&writer, key);
    qa_net_write_string(&writer, value);
    owner->reliable_info_size += qa_net_writer_size(&writer);
}
static void cvar_effect(void *opaque, qa_cvar_effect_kind kind, const qa_cvar_view *variable)
{
    struct application_native_q1_console *owner = opaque;
    if (kind != QA_CVAR_EFFECT_SERVERINFO || !owner->info_initialized ||
        dialect(owner->provider) != QA_RULESET_QUAKEWORLD) return;
    info_set(owner, owner->serverinfo, 512, variable->name, variable->value, false);
    info_change(owner, variable->name, variable->value);
}
static void info_list(struct application_native_q1_console *owner, const char *info)
{
    const char *cursor = info;
    if (*cursor == '\\') ++cursor;
    while (*cursor) {
        const char *key = cursor;
        while (*cursor && *cursor != '\\') ++cursor;
        size_t size = (size_t)(cursor - key), width = size < 20 ? 20 : size;
        char field[32770];
        memcpy(field, key, size); memset(field + size, ' ', width - size); field[width] = 0;
        info_print(owner, field);
        if (!*cursor) { info_print(owner, "MISSING VALUE\n"); return; }
        const char *value = ++cursor;
        while (*cursor && *cursor != '\\') ++cursor;
        size = (size_t)(cursor - value);
        memcpy(field, value, size); field[size] = '\n'; field[size + 1] = 0;
        info_print(owner, field);
        if (*cursor) ++cursor;
    }
}
static bool flood_command(void *opaque,const qa_command_invocation *invocation,qa_error *error)
{
    struct application_native_q1_console *owner=opaque;
    if (!owner || !invocation || invocation->console!=owner->console ||
        !qa_console_invocation_current(owner->console,invocation) ||
        invocation->receiver!=owner->provider->owner || invocation->registration_owner!=owner->provider->owner ||
        invocation->context.origin==QA_COMMAND_REMOTE ||
        !qa_application_command_context_active(owner->provider->application,&invocation->context))
        return application_fail(error,QA_ERROR_ARGUMENT,"QW flood policy lost its local Source operator");
    ++owner->calls; owner->info_context=&invocation->context;
    bool okay=true;
    if (!strcmp(invocation->argv[0],"floodprotmsg")) {
        if (invocation->argc==1) {
            char text[288]; snprintf(text,sizeof(text),"Current msg: %s\n",owner->chat.message); info_print(owner,text);
        } else if (invocation->argc!=2) info_print(owner,"Usage: floodprotmsg \"<message>\"\n");
        else if (strlen(invocation->argv[1])>=sizeof(owner->chat.message))
            okay=application_fail(error,QA_ERROR_FORMAT,"QW flood message exceeds its Source extent");
        else memcpy(owner->chat.message,invocation->argv[1],strlen(invocation->argv[1])+1);
    } else {
        if (invocation->argc==1) {
            if (owner->chat.messages) {
                char text[160]; snprintf(text,sizeof(text),
                    "Current floodprot settings: \nAfter %u msgs per %u seconds, silence for %u seconds\n",
                    owner->chat.messages,owner->chat.persecond,owner->chat.secondsdead); info_print(owner,text);
            } else info_print(owner,"No floodprots enabled.\n");
        }
        if (invocation->argc!=4) {
            if (invocation->argc!=1 || !owner->chat.messages) {
                info_print(owner,"Usage: floodprot <# of messages> <per # of seconds> <seconds to silence>\n");
                info_print(owner,"Use floodprotmsg to set a custom message to say to the flooder.\n");
            }
        } else {
            uint32_t values[3]={0};
            for (size_t i=0;i<3;++i) {
                char *end; errno=0; long value=strtol(invocation->argv[i+1],&end,10);
                if (end!=invocation->argv[i+1] && errno!=ERANGE && value>0 && value<=INT32_MAX)
                    values[i]=(uint32_t)value;
            }
            if (!values[0] || !values[1] || !values[2]) info_print(owner,"All values must be positive numbers\n");
            else if (values[0]>10) info_print(owner,"Can only track up to 10 messages.\n");
            else { owner->chat.messages=values[0]; owner->chat.persecond=values[1]; owner->chat.secondsdead=values[2]; }
        }
    }
    owner->info_context=NULL; --owner->calls; return okay;
}

static bool info_command(void *opaque, const qa_command_invocation *invocation, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    bool local = !strcmp(invocation->argv[0], "localinfo");
    char *info = local ? owner->localinfo : owner->serverinfo;
    ++owner->calls;
    owner->info_context = &invocation->context;
    bool okay = true;
    if (invocation->argc == 1) {
        info_print(owner, local ? "Local info settings:\n" : "Server info settings:\n");
        info_list(owner, info);
    } else if (invocation->argc != 3) {
        info_print(owner, local ? "usage: localinfo [ <key> <value> ]\n" :
            "usage: serverinfo [ <key> <value> ]\n");
    } else if (*invocation->argv[1] == '*') {
        info_print(owner, "Star variables cannot be changed.\n");
    } else {
        info_set(owner, info, local ? 32768 : 512, invocation->argv[1], invocation->argv[2], false);
        if (!local) {
            const qa_cvar_view *variable;
            okay = qa_console_cvar_read(owner->console, &invocation->context,
                invocation->argv[1], &variable, error);
            if (okay && variable) {
                okay = qa_console_cvar_apply(owner->console, &invocation->context,
                    &(qa_cvars_edit_command){.kind = QA_CVARS_EDIT_ASSIGN,
                        .name = invocation->argv[1], .value = invocation->argv[2], .force = true,
                        .source_dialect = QA_RULESET_QUAKEWORLD}, error);
            }
            if (okay) info_change(owner, invocation->argv[1], invocation->argv[2]);
        }
    }
    owner->info_context = NULL;
    --owner->calls;
    if (okay && owner->info_error.code != QA_OK) {
        if (error) *error = owner->info_error;
        okay = false;
    }
    return okay;
}
static bool visible_gamedir_command(void *opaque, const qa_command_invocation *invocation,
    qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    (void)error;
    ++owner->calls;
    owner->info_context = &invocation->context;
    if (invocation->argc == 1) {
        const char *value; size_t size;
        info_value(owner->serverinfo, "*gamedir", &value, &size);
        char text[sizeof(owner->serverinfo) + 32];
        snprintf(text, sizeof(text), "Current *gamedir: %.*s\n", (int)size, value);
        info_print(owner, text);
    } else if (invocation->argc != 2) {
        info_print(owner, "Usage: sv_gamedir <newgamedir>\n");
    } else {
        const char *directory = invocation->argv[1];
        if (strstr(directory, "..") || strpbrk(directory, "/\\:"))
            info_print(owner, "*Gamedir should be a single filename, not a path\n");
        else info_set(owner, owner->serverinfo, 512, "*gamedir", directory, true);
    }
    owner->info_context = NULL;
    --owner->calls;
    return true;
}
static bool physical_gamedir_command(void *opaque,const qa_command_invocation *invocation,qa_error *error)
{
    struct application_native_q1_console *owner=opaque;
    application_provider *provider=owner->provider;
    ++owner->calls; owner->info_context=&invocation->context;
    bool okay=true;
    if (invocation->argc==1) {
        qa_launch_source_files files; const char *directory=NULL;
        okay=application_native_q1_source_files(provider,&files,&directory,error);
        if (okay) { info_print(owner,"Current gamedir: "); info_print(owner,directory); info_print(owner,"\n"); }
    } else if (invocation->argc!=2) info_print(owner,"Usage: gamedir <newdir>\n");
    else if (strstr(invocation->argv[1],"..") || strpbrk(invocation->argv[1],"/\\:"))
        info_print(owner,"Gamedir should be a single filename, not a path\n");
    else {
        const qa_application_startup_hooks *hooks=provider->application->startup_hooks;
        qa_application_startup_source source;
        bool changed=false;
        okay=hooks && hooks->source_gamedir && files_source(provider,&invocation->context,&source,error);
        if (!okay && (!error || error->code==QA_OK))
            application_fail(error,QA_ERROR_ARGUMENT,"Native gamedir has no reached Source filesystem service");
        if (okay) okay=application_native_q1_wire_idle(provider) ||
            application_fail(error,QA_ERROR_ARGUMENT,"Native gamedir entered an active Source resource receipt");
        if (okay) okay=hooks->source_gamedir(hooks->context,provider->application,&source,
            invocation,invocation->argv[1],&changed,error);
        if (changed) {
            qa_error flush={0}; bool flushed=application_native_q1_wire_cache_flush(provider,&flush);
            if (okay && !flushed) { okay=false; if(error)*error=flush; }
        }
        if (okay) info_set(owner,owner->serverinfo,512,"*gamedir",invocation->argv[1],true);
    }
    owner->info_context=NULL; --owner->calls; return okay;
}
bool application_native_q1_source_info(application_provider *provider, bool local,
    const char **out, qa_error *error)
{
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (!owner || !out || provider->kind != APPLICATION_PROVIDER_Q1 || !provider->launch ||
        dialect(provider) != QA_RULESET_QUAKEWORLD || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld info lost its actual Source console");
    *out = local ? owner->localinfo : owner->serverinfo;
    return true;
}
bool application_native_q1_world_info(void *opaque, const char *key, qa_string_id *out, qa_error *error)
{
    application_provider *provider = opaque;
    const char *server, *local, *value; size_t size;
    if (!key || !out || !application_native_q1_source_info(provider, false, &server, error) ||
        !application_native_q1_source_info(provider, true, &local, error)) return false;
    info_value(server, key, &value, &size);
    if (!size) info_value(local, key, &value, &size);
    return qa_strings_intern(qa_session_strings(provider->application->session),
        (qa_bytes){(const uint8_t *)value, size}, out, error);
}
bool application_native_q1_source_visible_gamedir(application_provider *provider,
    const char **out, qa_error *error)
{
    const char *server, *value; size_t size;
    if (!out || !application_native_q1_source_info(provider, false, &server, error)) return false;
    info_value(server, "*gamedir", &value, &size);
    if (!size) { *out = "qw"; return true; }
    qa_string_id directory;
    if (!qa_strings_intern(qa_session_strings(provider->application->session),
        (qa_bytes){(const uint8_t *)value, size}, &directory, error)) return false;
    *out = qa_strings_cstr(qa_session_strings(provider->application->session), directory);
    return true;
}
void application_native_q1_source_info_map_reset(void *opaque)
{
    application_provider *provider = opaque;
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (!owner) return;
    owner->reliable_info_size = 0; owner->info_error = (qa_error){0};
}
bool application_native_q1_source_info_flush(application_provider *provider, qa_error *error)
{
    const char *server;
    if (!application_native_q1_source_info(provider, false, &server, error)) return false;
    struct application_native_q1_console *owner = provider->native_q1_console;
    if (owner->info_error.code != QA_OK) { if (error) *error = owner->info_error; return false; }
    if (!owner->reliable_info_size) return true;
    uint64_t time_ns; double seconds;
    if (!qa_q1_game_clock_read(provider->state.q1, &time_ns, &seconds))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld serverinfo lost its Source clock");
    if (!application_emit_protocol(provider, &(qa_application_protocol_event){
        .provider = provider->owner, .dialect = QA_RULESET_QUAKEWORLD, .time_ns = time_ns,
        .reliable = true, .payload = {owner->reliable_info, owner->reliable_info_size}}, error)) return false;
    owner->reliable_info_size = 0;
    return true;
}

static qa_command_result command(void *opaque, const qa_command_invocation *invocation, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    ++owner->calls;
    qa_command_result result = application_startup_common_command(owner->provider,
        owner->console, owner->cvars, invocation, error);
    if (result == QA_COMMAND_UNHANDLED)
        result = application_command_fallback(owner->provider->application, invocation, error);
    --owner->calls;
    return result;
}

static bool read_script(void *opaque, const qa_command_context *command,
                        const char *path, qa_bytes *out, void **lease, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    if (!active(owner, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 script publication has retired");
    if (application_startup_source_active(owner->provider))
        return application_startup_script_read(owner->provider, command, path, out, lease, error);
    if (application_startup_source_scripts(owner->provider))
        return application_startup_source_script_read(owner->provider, owner->console,
            command, path, out, lease, error);
    qa_resource *resource = NULL;
    qa_vfs *content=application_native_q1_wire_content(owner->provider,error);
    if (!content || !qa_vfs_acquire(content, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource);
    *lease = resource;
    return true;
}

static void release_script(void *opaque, void *lease)
{
    struct application_native_q1_console *owner = opaque;
    if (application_startup_source_active(owner->provider))
        application_startup_script_release(owner->provider, lease);
    else if (application_startup_source_scripts(owner->provider))
        application_startup_source_script_release(owner->provider, owner->cvars, lease);
    else qa_resource_release(lease);
}

static void script_complete(void *opaque, const qa_command_context *command,
    const char *path, bool success)
{
    struct application_native_q1_console *owner = opaque;
    application_startup_script_complete(owner->provider, command, path, success);
}

static bool allow_command(void *opaque, const qa_command_invocation *command)
{
    struct application_native_q1_console *owner = opaque;
    return application_startup_command_allowed(owner->provider, command);
}

bool application_native_q1_console_create_restored(application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1 || !provider->owner ||
        !provider->application || !provider->application->session || !provider->launch ||
        !provider->product || provider->product->family != QA_GAME_Q1 ||
        provider->close_pending || provider->native_q1_console)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console requires its actual source owner");
    struct application_native_q1_console *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "allocating native Q1 source console");
    owner->provider = provider;
    owner->chat=(native_q1_chat_policy){.messages=4,.persecond=4,.secondsdead=10};
    qa_cvar_options cvars = {.dialect = dialect(provider),
        .side = QA_CVAR_SIDE_SERVER, .role = QA_CVAR_ROLE_GAME, .user = owner, .print = cvar_print, .effect = cvar_effect};
    owner->cvars = qa_cvars_create_view(provider->application->cvars, &cvars, error);
    qa_console_options options = {.context = {.owner = provider->owner, .dialect = dialect(provider),
        .origin = QA_COMMAND_SERVER}, .cvars = owner->cvars, .user = owner, .print = print,
        .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = script_complete,
        .allow_command = allow_command, .source_command = command};
    if (owner->cvars && !application_startup_seed_source(provider, owner->cvars, error)) {
        qa_cvars_detach_callbacks(owner->cvars); qa_cvars_destroy(owner->cvars); free(owner); return false;
    }
    options.context.cvar_view = qa_cvars_view_identity(owner->cvars);
    if (owner->cvars && qa_console_bind_source(provider->application->console, &options, error))
        owner->console = provider->application->console;
    if (!owner->console) {
        qa_console_unbind_source(owner->console, qa_cvars_view_identity(owner->cvars), error);
        qa_cvars_detach_callbacks(owner->cvars); qa_cvars_destroy(owner->cvars);
        free(owner);
        return false;
    }
    provider->native_q1_console = owner;
    qa_application *application = provider->application;
    application_provider *prior = application->startup_preinit_provider;
    if (application->operation == APPLICATION_PERSISTING)
        application->startup_preinit_provider = provider;
    bool registered = dialect(provider) != QA_RULESET_QUAKEWORLD ||
        (qa_console_register_context(owner->console, &options.context, "serverinfo", NULL, provider->owner, provider->owner,
            false, info_command, owner, error) &&
         qa_console_register_context(owner->console, &options.context, "localinfo", NULL, provider->owner, provider->owner,
            false, info_command, owner, error) &&
         qa_console_register_context(owner->console, &options.context, "sv_gamedir", NULL, provider->owner, provider->owner,
            false, visible_gamedir_command, owner, error) &&
         qa_console_register_context(owner->console, &options.context, "gamedir", NULL, provider->owner, provider->owner,
            false, physical_gamedir_command, owner, error) &&
         qa_console_register_context(owner->console, &options.context,"floodprot",NULL,provider->owner,provider->owner,false,flood_command,owner,error) &&
         qa_console_register_context(owner->console, &options.context,"floodprotmsg",NULL,provider->owner,provider->owner,false,flood_command,owner,error));
    application->startup_preinit_provider = prior;
    if (!registered) {
        qa_console_unbind_source(owner->console, qa_cvars_view_identity(owner->cvars), error); qa_cvars_detach_callbacks(owner->cvars); qa_cvars_destroy(owner->cvars);
        free(owner); provider->native_q1_console = NULL; return false;
    }
    return true;
}

static bool retain_source_info(application_provider *provider, qa_error *error)
{
    if (provider->application->operation == APPLICATION_PERSISTING) return true;
    struct application_native_q1_console *owner = provider->native_q1_console;
    for (application_provider *previous = provider->application->live_providers; previous;
         previous = previous->next_live) {
        if (previous == provider || previous->kind != APPLICATION_PROVIDER_Q1 ||
            !previous->constructed || !previous->attached || previous->close_pending ||
            !previous->launch || !previous->native_q1_console || previous->owner != provider->owner ||
            strcmp(previous->launch->selection.instance, provider->launch->selection.instance) ||
            dialect(previous) != dialect(provider)) continue;
        if (!application_native_q1_console_idle(previous))
            return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld info carry requires its idle Source");
        memcpy(owner->serverinfo, previous->native_q1_console->serverinfo, sizeof(owner->serverinfo));
        memcpy(owner->localinfo, previous->native_q1_console->localinfo, sizeof(owner->localinfo));
        owner->info_initialized = previous->native_q1_console->info_initialized;
        owner->chat=previous->native_q1_console->chat;
        break;
    }
    return true;
}

bool application_native_q1_console_create(application_provider *provider,
                                          const qa_q1_options *rules, qa_error *error)
{
    if (!rules || !application_native_q1_console_create_restored(provider, error)) return false;
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    bool okay = retain_source_info(provider, error);
    static const struct { const char *name; qa_cvar_save_policy policy; } names[] = {
        {"skill", QA_CVAR_SAVE_GAMEPLAY},
        {"deathmatch", QA_CVAR_SAVE_GAMEPLAY},
        {"coop", QA_CVAR_SAVE_GAMEPLAY},
        {"teamplay", QA_CVAR_SAVE_GAMEPLAY},
        {"sv_gravity", QA_CVAR_SAVE_GAMEPLAY},
        {"sv_maxspeed", QA_CVAR_SAVE_GAMEPLAY},
        {"samelevel", QA_CVAR_SAVE_GAMEPLAY},
        {"timelimit", QA_CVAR_SAVE_GAMEPLAY},
        {"fraglimit", QA_CVAR_SAVE_GAMEPLAY},
        {"gamecfg", QA_CVAR_SAVE_GAMEPLAY},
        {"sv_cheats", QA_CVAR_SAVE_SETTING},
        {"footsteps", QA_CVAR_SAVE_SETTING},
        {"maxclients", QA_CVAR_SAVE_GAMEPLAY},
        {"registered", QA_CVAR_SAVE_SETTING},
        {"developer", QA_CVAR_SAVE_SETTING},
        {"sv_aim", QA_CVAR_SAVE_GAMEPLAY},
        {"hostname", QA_CVAR_SAVE_SETTING}};
    char skill[16], deathmatch[16], coop[2], teamplay[16], gravity[32], gamecfg[16], maximum[16], aim[32];
    snprintf(skill, sizeof(skill), "%u", rules->skill);
    snprintf(deathmatch, sizeof(deathmatch), "%d", rules->deathmatch);
    snprintf(coop, sizeof(coop), "%u", rules->coop ? 1u : 0u);
    snprintf(teamplay, sizeof(teamplay), "%d", rules->teamplay);
    snprintf(gravity, sizeof(gravity), "%.9g", (double)rules->gravity);
    snprintf(gamecfg, sizeof(gamecfg), "%u", rules->gamecfg);
    snprintf(maximum, sizeof(maximum), "%u", rules->quakeworld ? 8u : rules->max_clients);
    snprintf(aim, sizeof(aim), "%.9g", (double)rules->aim_threshold);
    bool registered;
    if (!qa_catalog_q1_registered(provider->application->catalog, provider->product->id,
            &registered, error)) return false;
    const char *values[] = {skill, deathmatch, coop, teamplay, gravity, "320", "0", "0", "0", gamecfg,
        "0", "1", maximum, registered ? "1" : "0", "0", aim, rules->quakeworld ? "unnamed" : "UNNAMED"};
    for (size_t i = 0; okay && i < sizeof(names) / sizeof(*names); ++i) {
        okay = qa_cvars_register(cvars, names[i].name, values[i], 0,
            provider->owner, NULL, error) &&
            qa_cvars_declare_save_policy(cvars, names[i].name, names[i].policy, error);
    }
    if (rules->quakeworld) {
        static const struct { const char *name; qa_cvar_save_policy policy; } qw_names[] = {
            {"sv_maxvelocity", QA_CVAR_SAVE_GAMEPLAY},
            {"sv_stopspeed", QA_CVAR_SAVE_GAMEPLAY},
            {"sv_spectatormaxspeed", QA_CVAR_SAVE_GAMEPLAY},
            {"sv_accelerate", QA_CVAR_SAVE_GAMEPLAY},
            {"sv_airaccelerate", QA_CVAR_SAVE_GAMEPLAY},
            {"sv_wateraccelerate", QA_CVAR_SAVE_GAMEPLAY},
            {"sv_friction", QA_CVAR_SAVE_GAMEPLAY},
            {"sv_waterfriction", QA_CVAR_SAVE_GAMEPLAY},
            {"maxspectators", QA_CVAR_SAVE_SETTING},
            {"pausable", QA_CVAR_SAVE_GAMEPLAY},
            {"sv_spectalk", QA_CVAR_SAVE_SETTING},
            {"sv_mapcheck", QA_CVAR_SAVE_SETTING},
            {"spawn", QA_CVAR_SAVE_GAMEPLAY},
            {"watervis", QA_CVAR_SAVE_GAMEPLAY},
            {"sv_phs", QA_CVAR_SAVE_SETTING},
            {"password", QA_CVAR_SAVE_SETTING},
            {"spectator_password", QA_CVAR_SAVE_SETTING},
            {"sv_highchars", QA_CVAR_SAVE_SETTING}};
        static const char *const qw_values[] = {"2000", "100", "500", "10", "0.7",
            "10", "4", "4", "8", "1", "1", "1", "0", "0", "1", "", "", "1"};
        for (size_t i = 0; okay && i < sizeof(qw_names) / sizeof(*qw_names); ++i) {
            okay = qa_cvars_register(cvars, qw_names[i].name, qw_values[i], 0,
                provider->owner, NULL, error) &&
                qa_cvars_declare_save_policy(cvars, qw_names[i].name, qw_names[i].policy, error);
        }
        static const char *const info_names[] = {"fraglimit", "timelimit", "teamplay",
            "samelevel", "maxclients", "maxspectators", "hostname", "deathmatch", "spawn",
            "watervis"};
        struct application_native_q1_console *owner = provider->native_q1_console;
        bool fresh_info = !owner->info_initialized;
        for (size_t i = 0; okay && i < sizeof(info_names) / sizeof(*info_names); ++i) {
            okay = qa_cvars_add_flags(cvars, info_names[i], QA_CVAR_SERVERINFO, error);
            const qa_cvar_view *variable = qa_cvars_find(cvars, info_names[i]);
            if (okay && fresh_info) info_set(owner, owner->serverinfo, 512,
                variable->name, variable->value, false);
        }
        if (okay && fresh_info) {
            /* QW SV_InitLocal publishes VERSION (bothdefs.h: 2.40). */
            info_set(owner, owner->serverinfo, 512, "*version", "2.40", true);
            for (const qa_cvar_view *variable = qa_cvars_next(cvars, NULL); variable;
                 variable = qa_cvars_next(cvars, variable)) {
                bool standard = false;
                for (size_t n = 0; n < sizeof(info_names) / sizeof(*info_names); ++n)
                    if (!strcmp(variable->name, info_names[n])) standard = true;
                if (!standard && (variable->flags & QA_CVAR_SERVERINFO))
                    info_set(owner, owner->serverinfo, 512, variable->name, variable->value, false);
            }
            owner->info_initialized = true;
        }
    }
    if (!okay) application_native_q1_console_destroy(provider, NULL);
    return okay;
}

bool application_native_q1_console_destroy(application_provider *provider, qa_error *error)
{
    if (!provider || !application_native_q1_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console is borrowed");
    struct application_native_q1_console *owner = provider->native_q1_console;
    if (owner) {
        if (!application_startup_source_retire(provider, owner->console, owner->cvars, error)) return false;
        if (!qa_console_unbind_source(owner->console, qa_cvars_view_identity(owner->cvars), error)) return false;
        qa_cvars_remove_owner(owner->cvars, provider->owner);
        qa_cvars_detach_callbacks(owner->cvars); qa_cvars_destroy(owner->cvars);
        free(owner);
        provider->native_q1_console = NULL;
    }
    return true;
}

bool application_native_q1_console_at(application_provider *provider, qa_console **console,
                                      qa_cvars **cvars, qa_command_context *context)
{
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (!owner || !console) return false;
    *console = owner->console;
    if (cvars) *cvars = owner->cvars;
    if (context) *context = (qa_command_context){.owner = provider->owner,
        .cvar_view = qa_cvars_view_identity(owner->cvars),
        .dialect = dialect(provider), .origin = QA_COMMAND_SERVER};
    return true;
}

enum { QW_CHAT_SAVE_SIZE=12+255+32*(2+4+20+12+1+80+8) };
static bool chat_fields(qa_source_save_io *io,native_q1_chat_policy *policy)
{
    if (!qa_source_save_u32(io,&policy->messages) || !qa_source_save_u32(io,&policy->persecond) ||
        !qa_source_save_u32(io,&policy->secondsdead) || !qa_source_save_bytes(io,policy->message,sizeof(policy->message))) return false;
    if (policy->messages>10 || !policy->persecond || policy->persecond>INT32_MAX ||
        !policy->secondsdead || policy->secondsdead>INT32_MAX || !memchr(policy->message,0,sizeof(policy->message)))
        return application_fail(io->error,QA_ERROR_FORMAT,"QW checkpoint changes its Source flood policy");
    for (size_t i=0;i<32;++i) {
        native_q1_chat_client *row=policy->clients+i;
        if (!qa_source_save_bool(io,&row->occupied) || !qa_source_save_bool(io,&row->remote) ||
            !qa_source_save_u32(io,&row->local_seat) || !qa_source_save_u64(io,&row->client.owner) ||
            !qa_source_save_u64(io,&row->client.generation) || !qa_source_save_u32(io,&row->client.slot) ||
            !qa_source_save_u64(io,&row->seat.owner) || !qa_source_save_u32(io,&row->seat.index) ||
            !qa_source_save_u8(io,&row->head)) return false;
        bool times=false;
        for (size_t j=0;j<10;++j) {
            if (!qa_source_save_u64(io,row->times+j)) return false;
            times=times || row->times[j]!=0;
        }
        if (!qa_source_save_u64(io,&row->locked_until_ns)) return false;
        if (row->head>9 || (!row->occupied && (row->remote || row->local_seat || row->head || times || row->locked_until_ns)) ||
            (row->remote && (!row->occupied || row->local_seat || !row->client.owner || !row->seat.owner)) ||
            (!row->remote && (row->client.owner || row->client.generation || row->client.slot || row->seat.owner || row->seat.index)))
            return application_fail(io->error,QA_ERROR_FORMAT,"QW checkpoint changes its retained Source chat client");
    }
    return true;
}

bool application_native_q1_console_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (!cvars || !out || !application_native_q1_console_idle(provider) || owner->info_error.code != QA_OK)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console capture requires its idle Source owner");
    qa_buffer registry = {0};
    if (!qa_cvars_save_capture(cvars, &registry, error)) return false;
    qa_buffer chat={0};
    if (dialect(provider)==QA_RULESET_QUAKEWORLD) {
        native_q1_chat_policy policy=owner->chat;
        qa_source_save_io io={0};
        bool okay=qa_source_save_writer(&io,NULL,error) && chat_fields(&io,&policy) && qa_source_save_finish(&io,&chat);
        qa_source_save_dispose(&io);
        if (!okay) { qa_buffer_free(&registry); return false; }
        if (chat.size!=QW_CHAT_SAVE_SIZE) {
            qa_buffer_free(&registry); qa_buffer_free(&chat);
            return application_fail(error,QA_ERROR_FORMAT,"QW checkpoint lost its Source chat extent");
        }
    }
    size_t server_size = strlen(owner->serverinfo), local_size = strlen(owner->localinfo);
    if (registry.size > UINT32_MAX || registry.size > SIZE_MAX - 22 - server_size - local_size - owner->reliable_info_size - chat.size) {
        qa_buffer_free(&registry); qa_buffer_free(&chat);
        return application_fail(error, QA_ERROR_MEMORY, "native Q1 console checkpoint exceeds its extent");
    }
    qa_buffer bytes = {.size = 22 + registry.size + server_size + local_size + owner->reliable_info_size + chat.size};
    bytes.data = malloc(bytes.size);
    if (!bytes.data) { qa_buffer_free(&registry); qa_buffer_free(&chat); return application_fail(error, QA_ERROR_MEMORY, "allocating Source console checkpoint"); }
    qa_net_writer writer; qa_net_writer_init(&writer, bytes.data, bytes.size, error);
    bool okay = qa_net_write_u32(&writer, UINT32_C(0x3149514e)) &&
        qa_net_write_u8(&writer, (uint8_t)dialect(provider)) &&
        qa_net_write_u8(&writer, owner->info_initialized ? 1 : 0) &&
        qa_net_write_u32(&writer, (uint32_t)registry.size) && qa_net_write_u32(&writer, (uint32_t)server_size) &&
        qa_net_write_u32(&writer, (uint32_t)local_size) && qa_net_write_u32(&writer, (uint32_t)owner->reliable_info_size) &&
        qa_net_write_data(&writer, registry.data, registry.size) &&
        qa_net_write_data(&writer, owner->serverinfo, server_size) &&
        qa_net_write_data(&writer, owner->localinfo, local_size) &&
        qa_net_write_data(&writer, owner->reliable_info, owner->reliable_info_size) &&
        qa_net_write_data(&writer,chat.data,chat.size);
    qa_buffer_free(&registry); qa_buffer_free(&chat);
    if (!okay) { qa_buffer_free(&bytes); return false; }
    *out = bytes; return true;
}
static bool saved_info(qa_bytes bytes)
{
    if (bytes.size && bytes.data[0] != '\\') return false;
    /* Filtering high-bit characters can introduce delimiters/quotes and
     * incomplete pairs. They are genuine retained Source bytes. */
    for (size_t i = 0; i < bytes.size; ++i)
        if (bytes.data[i] < 14) return false;
    return true;
}
static bool saved_info_messages(qa_bytes bytes)
{
    size_t cursor = 0;
    while (cursor < bytes.size) {
        if (bytes.data[cursor++] != QA_QW_SERVER_INFO) return false;
        for (size_t n = 0; n < 2; ++n) {
            while (cursor < bytes.size && bytes.data[cursor]) ++cursor;
            if (cursor == bytes.size) return false;
            ++cursor;
        }
    }
    return true;
}
bool application_native_q1_console_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    if (!cvars || !application_native_q1_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console restore requires its idle Source owner");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    uint32_t magic = qa_net_read_u32(&reader);
    uint8_t source_dialect = qa_net_read_u8(&reader), initialized = qa_net_read_u8(&reader);
    uint32_t registry_size = qa_net_read_u32(&reader), server_size = qa_net_read_u32(&reader),
        local_size = qa_net_read_u32(&reader), reliable_size = qa_net_read_u32(&reader);
    if (reader.failed) return false;
    if (magic != UINT32_C(0x3149514e) || source_dialect != dialect(provider) ||
        initialized > 1 || server_size > 512 || local_size > 32768 || reliable_size > 1450 ||
        (source_dialect != QA_RULESET_QUAKEWORLD && (initialized || server_size || local_size || reliable_size)))
        return application_fail(error, QA_ERROR_FORMAT, "native Q1 console checkpoint changes its Source recipe");
    qa_bytes registry, server, local, reliable;
    if (!qa_net_read_bytes(&reader, registry_size, &registry) || !qa_net_read_bytes(&reader, server_size, &server) ||
        !qa_net_read_bytes(&reader, local_size, &local) || !qa_net_read_bytes(&reader, reliable_size, &reliable)) return false;
    native_q1_chat_policy chat={0};
    if (source_dialect==QA_RULESET_QUAKEWORLD) {
        qa_bytes saved; qa_source_save_io io={0};
        if (!qa_net_read_bytes(&reader,QW_CHAT_SAVE_SIZE,&saved)) return false;
        bool okay=qa_source_save_reader(&io,NULL,saved,error) && chat_fields(&io,&chat) && qa_source_save_finish(&io,NULL);
        qa_source_save_dispose(&io);
        if (!okay) return false;
    }
    if (!qa_net_reader_finish(&reader)) return false;
    if (!saved_info(server) || !saved_info(local) || !saved_info_messages(reliable))
        return application_fail(error, QA_ERROR_FORMAT, "native Q1 console checkpoint has invalid Source info bytes");
    qa_cvars_restore *ticket = NULL;
    bool okay = qa_cvars_save_prepare(cvars, registry, &ticket, error) && qa_cvars_save_commit(ticket, error);
    if (!okay) { qa_cvars_save_abort(ticket); return false; }
    struct application_native_q1_console *owner = provider->native_q1_console;
    memcpy(owner->serverinfo, server.data, server.size); owner->serverinfo[server.size] = 0;
    memcpy(owner->localinfo, local.data, local.size); owner->localinfo[local.size] = 0;
    memcpy(owner->reliable_info, reliable.data, reliable.size); owner->reliable_info_size = reliable.size;
    owner->info_initialized = initialized != 0; owner->info_error = (qa_error){0};
    if (source_dialect==QA_RULESET_QUAKEWORLD) owner->chat=chat;
    if (provider->application->operation == APPLICATION_PERSISTING) {
        qa_console *console = NULL;
        qa_command_context context;
        if (!application_native_q1_console_at(provider, &console, NULL, &context))
            return application_fail(error, QA_ERROR_ARGUMENT, "Restored native Q1 console lost its physical Source owner");
        return application_startup_source_restore(provider, console, cvars, &context, error);
    }
    return true;
}

bool application_native_q1_cvar(void *opaque, qa_string_id name, float *out, qa_error *error)
{
    application_provider *provider = opaque;
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    const char *text = provider && provider->application && provider->application->session
        ? qa_strings_cstr(qa_session_strings(provider->application->session), name) : NULL;
    if (!cvars || !text || !out || !provider->constructed || provider->close_pending ||
        provider->application->destroy_requested)
        return application_fail(error, QA_ERROR_NOT_FOUND, "native Q1 cvar source has retired");
    const qa_cvar_view *value = qa_cvars_find(cvars, text);
    if (value && value->owner && value->owner != provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 cvar belongs to another source");
    *out = value ? (float)(float)(value->number) : 0;
    return true;
}

bool application_native_q1_client_attack(void *opaque, qa_actor_id actor, bool *out)
{
    application_provider *provider = opaque;
    qa_application_control_view control;
    uint32_t slot;
    if (!out || !provider || provider->kind != APPLICATION_PROVIDER_Q1 || !provider->constructed ||
        !provider->attached || provider->close_pending || provider->application->destroy_requested ||
        !qa_q1_native_client_slot_prepared(provider->state.q1, actor, &slot, NULL)) return false;
    *out = qa_application_control_read(provider->application, actor, &control) && (control.buttons & 1u) != 0;
    return true;
}
