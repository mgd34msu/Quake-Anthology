#include "native_q3_console.h"
#include "q3_product.h"
#include "startup_flow.h"
#include "engine_shutdown.h"
#include "native_q3_wire_state.h"
#include "native_q3_votes.h"
#include "bots_catalog.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_source.h"
#include "qa/cvars_save.h"

#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct q3_engine_cvar {
    const char *name, *value;
    uint32_t flags;
} q3_engine_cvar;

/* q3ServerCvarDefinitions and collisionMapCvarDefinitions registration order.
 * A NULL default is the actual incoming map identity, not a retained old map. */
static const q3_engine_cvar engine_cvars[] = {
    {"vm_game", "2", QA_CVAR_ARCHIVE},
    {"protocol", "68", QA_CVAR_SERVERINFO | QA_CVAR_READONLY},
    {"sv_pure", "1", QA_CVAR_SYSTEMINFO},
    {"sv_allowDownload", "0", QA_CVAR_SERVERINFO},
    {"sv_maxRate", "0", QA_CVAR_SERVERINFO},
    {"sv_fps", "20", 0},
    {"sv_serverid", "0", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
    {"sv_paks", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
    {"sv_pakNames", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
    {"sv_referencedPaks", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
    {"sv_referencedPakNames", "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY},
    {"sv_maxclients", "8", QA_CVAR_SERVERINFO | QA_CVAR_LATCH},
    {"mapname", NULL, QA_CVAR_SERVERINFO | QA_CVAR_READONLY},
    {"sv_mapname", "", QA_CVAR_SERVERINFO | QA_CVAR_READONLY},
    {"sv_privateClients", "0", QA_CVAR_SERVERINFO},
    {"sv_privatePassword", "", QA_CVAR_TEMPORARY},
    {"sv_reconnectlimit", "3", 0},
    {"sv_minPing", "0", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO},
    {"sv_maxPing", "0", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO},
    {"sv_floodProtect", "1", QA_CVAR_ARCHIVE | QA_CVAR_SERVERINFO},
    {"sv_strictAuth", "1", QA_CVAR_ARCHIVE},
    {"bot_enable", "1", 0},
    {"cm_noAreas", "0", QA_CVAR_CHEAT},
    {"cm_noCurves", "0", QA_CVAR_CHEAT},
    {"cm_playerCurveClip", "1", QA_CVAR_ARCHIVE | QA_CVAR_CHEAT}
};

struct application_native_q3_console {
    application_provider *provider;
    qa_console *console;
    qa_cvars *cvars;
    size_t calls;
    bool settings_bound;
    application_native_q3_source_command_scope *source_command;
    application_native_q3_source_drop_scope *source_drop;
};

bool application_native_q3_console_settings_bound(const application_provider *provider)
{
    return provider && provider->native_q3_console && provider->native_q3_console->settings_bound;
}

void application_native_q3_console_settings_commit(application_provider *provider)
{
    provider->native_q3_console->settings_bound = true;
}

qa_cvars *application_native_q3_console_registry(const application_provider *provider)
{
    return provider && provider->kind == APPLICATION_PROVIDER_Q3 && provider->native_q3_console
        ? provider->native_q3_console->cvars : NULL;
}

bool application_native_q3_console_capture(application_provider *provider,
    qa_buffer *out, qa_error *error)
{
    qa_cvars *registry = application_native_q3_console_registry(provider);
    if (!registry || !application_native_q3_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 registry capture requires its idle source owner");
    return qa_cvars_save_capture(registry, out, error);
}

bool application_native_q3_console_restore(application_provider *provider,
    qa_bytes bytes, qa_error *error)
{
    qa_cvars *registry = application_native_q3_console_registry(provider);
    if (!registry || !application_native_q3_console_idle(provider) ||
        provider->application->operation != APPLICATION_PERSISTING)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 registry import requires its idle candidate owner");
    qa_cvars_restore *ticket = NULL;
    bool okay = qa_cvars_save_prepare(registry, bytes, &ticket, error) &&
        qa_cvars_save_commit(ticket, error);
    if (!okay) qa_cvars_save_abort(ticket);
    return okay;
}

qa_cvars *application_native_q3_cvar_owner(const application_provider *provider, const char *name)
{
    const char *engine = "sv_cheats";
    const char *value = name;
    while (value && *value && *engine && tolower((unsigned char)*value) == *engine) {
        ++value;
        ++engine;
    }
    if (value && !*value && !*engine)
        return application_engine_shutdown_cvars(provider);
    return application_native_q3_console_registry(provider);
}

static qa_cvars *cvar_owner(void *context, const qa_command_context *command, const char *name)
{
    struct application_native_q3_console *owner = context;
    qa_cvars *selected = application_startup_cvar_owner(owner->provider, owner->console, command, name);
    if (selected) return selected;
    return application_native_q3_cvar_owner(owner->provider, name);
}

static qa_cvars *visible_cvars(void *context, const qa_command_context *command, size_t index)
{
    struct application_native_q3_console *owner = context;
    qa_cvars *selected = NULL;
    if (application_startup_visible_cvars(owner->provider, owner->console, command, index, &selected))
        return selected;
    return index == 0 ? owner->cvars : index == 1 ? application_engine_shutdown_cvars(owner->provider) : NULL;
}

static bool cvar_edit(void *context, const qa_command_context *command,
    qa_cvars *registry, qa_cvars_edit **out, qa_error *error)
{
    struct application_native_q3_console *owner = context;
    return application_startup_cvar_edit(owner->provider, owner->console, command, registry, out, error);
}

static bool cheats_allowed(void *context)
{
    struct application_native_q3_console *owner = context;
    return application_native_cheats_enabled(owner->provider);
}

bool application_native_q3_console_idle(const application_provider *provider)
{
    const struct application_native_q3_console *owner = provider ? provider->native_q3_console : NULL;
    return !owner || (!owner->calls && qa_console_idle(owner->console));
}

bool application_native_q3_console_borrow(application_provider *provider, qa_error *error)
{
    struct application_native_q3_console *owner = provider ? provider->native_q3_console : NULL;
    if (!owner || !provider->constructed || !provider->attached || provider->close_pending ||
        provider->application->destroy_requested || owner->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 console source is not admitted");
    ++owner->calls;
    return true;
}

void application_native_q3_console_release(application_provider *provider)
{
    if (provider && provider->native_q3_console && provider->native_q3_console->calls)
        --provider->native_q3_console->calls;
}
static bool source_command_current(const application_native_q3_source_command_scope *scope)
{
    application_provider *p=scope?scope->provider:NULL;
    qa_application *app=p?p->application:NULL;
    uint32_t slot;
    qa_q3_source_binding binding;
    qa_q3_native_client client;
    return app&&!app->destroy_requested&&p->constructed&&p->attached&&!p->close_pending&&
        p->kind==APPLICATION_PROVIDER_Q3&&p->state.q3==scope->game&&p->launch==scope->launch&&
        app->publication_generation==scope->publication_generation&&
        app->command_generation==scope->command_generation&&app->map_revision==scope->map_revision&&
        qa_launch_snapshot_find(qa_application_launch(app),scope->launch->selection.instance)==scope->launch&&
        qa_actors_get(qa_session_actors(app->session),scope->actor)&&
        qa_q3_native_client_slot(scope->game,scope->actor,&slot,NULL)&&slot==scope->slot&&
        qa_q3_source_binding_read(scope->game,slot,&binding,NULL)&&binding.in_use&&
        binding.client_slot==(int32_t)slot&&qa_actor_id_equal(binding.actor,scope->actor)&&
        qa_q3_client_read(scope->game,scope->actor,&client,NULL)&&client.connected==QA_Q3_CLIENT_CONNECTED;
}
bool application_native_q3_source_command_entered(const application_provider *provider)
{
    const struct application_native_q3_console *owner=provider?provider->native_q3_console:NULL;
    return owner&&owner->calls&&owner->source_command&&source_command_current(owner->source_command);
}
bool application_native_q3_source_command_actor_current(const application_provider *provider,qa_actor_id actor)
{
    const struct application_native_q3_console *owner=provider?provider->native_q3_console:NULL;
    return owner&&owner->calls&&owner->source_command&&
        qa_actor_id_equal(owner->source_command->actor,actor)&&source_command_current(owner->source_command);
}
static bool source_drop_current(const application_native_q3_source_drop_scope *scope)
{
    application_provider *p=scope?scope->provider:NULL;
    qa_application *app=p?p->application:NULL;
    if(!app||app->destroy_requested||!p->constructed||!p->attached||p->close_pending||
        p->kind!=APPLICATION_PROVIDER_Q3||p->state.q3!=scope->game||p->launch!=scope->launch||
        app->publication_generation!=scope->publication_generation||
        app->command_generation!=scope->command_generation||app->map_revision!=scope->map_revision||
        qa_launch_snapshot_find(qa_application_launch(app),scope->launch->selection.instance)!=scope->launch)
        return false;
    if(scope->disconnected) {
        qa_q3_native_client client;
        application_native_q3_wire_client_view wire;
        bool admitted;
        return qa_q3_client_slot_read(scope->game,scope->slot,&client,NULL)&&
            client.connected==QA_Q3_CLIENT_DISCONNECTED&&
            application_native_q3_wire_client_admission_read(p,scope->slot,&wire,&admitted,NULL)&&!admitted;
    }
    qa_actor_id actor;
    const char *reason;
    bool pending;
    return qa_actors_get(qa_session_actors(app->session),scope->actor)&&
        application_native_q3_wire_drop_client_read(p,scope->slot,&actor,&reason,&pending,NULL)&&
        pending&&reason&&scope->reason&&!strcmp(reason,scope->reason)&&qa_actor_id_equal(actor,scope->actor);
}
bool application_native_q3_source_entered(const application_provider *provider)
{
    const struct application_native_q3_console *owner=provider?provider->native_q3_console:NULL;
    return application_native_q3_source_command_entered(provider)||
        (owner&&owner->calls&&owner->source_drop&&source_drop_current(owner->source_drop));
}
bool application_native_q3_source_drop_begin(application_provider *provider,uint32_t slot,qa_actor_id actor,
    application_native_q3_source_drop_scope *scope,qa_error *error)
{
    qa_application *app=provider?provider->application:NULL;
    qa_actor_id actual;
    const char *reason;
    bool pending;
    if(!app||!scope||provider->kind!=APPLICATION_PROVIDER_Q3||!provider->state.q3||!provider->launch||
        !provider->launch->selection.instance||!qa_actors_get(qa_session_actors(app->session),actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 DROP lacks its actual pending full client request");
    if(!application_native_q3_wire_drop_client_read(provider,slot,&actual,&reason,&pending,error)) return false;
    if(!pending||!reason||!qa_actor_id_equal(actual,actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 DROP lacks its actual pending full client request");
    size_t length=strlen(reason);
    char *retained=malloc(length+1);
    if(!retained) return application_fail(error,QA_ERROR_MEMORY,"Retaining entered Source Q3 DROP reason");
    memcpy(retained,reason,length+1);
    *scope=(application_native_q3_source_drop_scope){.provider=provider,.game=provider->state.q3,
        .launch=provider->launch,.actor=actor,.slot=slot,.reason=retained,
        .publication_generation=app->publication_generation,.command_generation=app->command_generation,
        .map_revision=app->map_revision};
    if(!source_drop_current(scope)) {
        free(retained);*scope=(application_native_q3_source_drop_scope){0};
        return application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 DROP lost its actual installed request");
    }
    if(!application_native_q3_console_borrow(provider,error)) {
        free(retained);*scope=(application_native_q3_source_drop_scope){0};
        return false;
    }
    scope->previous=provider->native_q3_console->source_drop;
    provider->native_q3_console->source_drop=scope;
    return true;
}
bool application_native_q3_source_drop_disconnected(application_native_q3_source_drop_scope *scope,qa_error *error)
{
    struct application_native_q3_console *owner=scope&&scope->provider?scope->provider->native_q3_console:NULL;
    if(!owner||owner->source_drop!=scope)
        return application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 DROP completion is outside its actual entered request");
    scope->disconnected=true;
    if(source_drop_current(scope)) return true;
    scope->disconnected=false;
    return application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 DROP precedes actual client and wire disconnect");
}
bool application_native_q3_source_drop_end(application_native_q3_source_drop_scope *scope,qa_error *error)
{
    struct application_native_q3_console *owner=scope&&scope->provider?scope->provider->native_q3_console:NULL;
    if(!owner||owner->source_drop!=scope)
        return application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 DROP scope is not the entered request");
    bool current=source_drop_current(scope);
    owner->source_drop=scope->previous;
    application_native_q3_console_release(scope->provider);
    free(scope->reason);*scope=(application_native_q3_source_drop_scope){0};
    return current||application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 DROP changed its retained physical request");
}
bool application_native_q3_source_command_begin(application_provider *provider,qa_actor_id actor,
    const qa_command_invocation *command,application_native_q3_source_command_scope *scope,qa_error *error)
{
    qa_application *app=provider?provider->application:NULL;
    if(!app||!scope||provider->kind!=APPLICATION_PROVIDER_Q3||!provider->state.q3||
        !provider->launch||!provider->launch->selection.instance||!command||!command->argc||!command->argv||
        !command->args_text||command->context.owner!=provider->owner||
        command->context.origin!=QA_COMMAND_REMOTE||command->context.dialect!=QA_CONSOLE_Q3||
        !qa_actor_id_equal(command->context.actor,actor)||
        !qa_application_command_context_active(app,&command->context))
        return application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 command lacks its actual captured client invocation");
    for(size_t i=0;i<command->argc;++i) if(!command->argv[i])
        return application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 command argument is absent");
    *scope=(application_native_q3_source_command_scope){.provider=provider,.game=provider->state.q3,
        .launch=provider->launch,.actor=actor,.publication_generation=app->publication_generation,
        .command_generation=app->command_generation,.map_revision=app->map_revision};
    if(!qa_q3_native_client_slot(scope->game,actor,&scope->slot,error)||!source_command_current(scope))
        return application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 command lost its installed full client binding");
    if(!application_native_q3_console_borrow(provider,error)) return false;
    scope->previous=provider->native_q3_console->source_command;
    provider->native_q3_console->source_command=scope; return true;
}
bool application_native_q3_source_command_end(application_native_q3_source_command_scope *scope,qa_error *error)
{
    struct application_native_q3_console *owner=scope&&scope->provider?scope->provider->native_q3_console:NULL;
    if(!owner||owner->source_command!=scope)
        return application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 command scope is not the entered invocation");
    bool current=source_command_current(scope);
    owner->source_command=scope->previous;
    application_native_q3_console_release(scope->provider);
    *scope=(application_native_q3_source_command_scope){0};
    return current||application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 command retired its captured client");
}

static bool capture(void *context, const qa_command_context *source,
                    qa_command_context *out, qa_error *error)
{
    struct application_native_q3_console *owner = context;
    qa_command_context command = *source;
    if (command.owner && command.owner != owner->provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 console received another source owner");
    command.owner = owner->provider->owner;
    command.dialect = QA_CONSOLE_Q3;
    return application_command_capture(owner->provider->application, &command, out, error);
}

static bool active(void *context, const qa_command_context *command)
{
    struct application_native_q3_console *owner = context;
    return command && command->owner == owner->provider->owner &&
        command->dialect == QA_CONSOLE_Q3 &&
        application_command_active(owner->provider->application, command);
}

static void print(void *context, const qa_command_context *command, const char *text)
{
    struct application_native_q3_console *owner = context;
    ++owner->calls;
    application_console_print(owner->provider->application, command, text);
    --owner->calls;
}

static void cvar_print(void *context, const char *text)
{
    struct application_native_q3_console *owner = context;
    qa_console_emit(owner->console, NULL, text);
}

static bool read_script(void *context, const qa_command_context *command,
                        const char *path, qa_bytes *out, void **lease, qa_error *error)
{
    struct application_native_q3_console *owner = context;
    if (!active(owner, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 script publication has retired");
    if (application_startup_source_active(owner->provider))
        return application_startup_script_read(owner->provider, command, path, out, lease, error);
    if (application_startup_source_scripts(owner->provider))
        return application_startup_source_script_read(owner->provider, owner->console,
            command, path, out, lease, error);
    qa_vfs *files = qa_application_context_files(owner->provider->application, command, NULL);
    qa_resource *resource = NULL;
    if (!files || !qa_vfs_acquire(files, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource);
    *lease = resource;
    return true;
}

static void release_script(void *context, void *lease)
{
    struct application_native_q3_console *owner = context;
    if (application_startup_source_active(owner->provider))
        application_startup_script_release(owner->provider, lease);
    else if (application_startup_source_scripts(owner->provider))
        application_startup_source_script_release(owner->provider, owner->cvars, lease);
    else qa_resource_release(lease);
}

static void script_complete(void *context, const qa_command_context *command,
    const char *path, bool success)
{
    struct application_native_q3_console *owner = context;
    application_startup_script_complete(owner->provider, command, path, success);
}

static bool allow_command(void *context, const qa_command_invocation *command)
{
    struct application_native_q3_console *owner = context;
    return application_startup_command_allowed(owner->provider, command);
}

static qa_command_result command(void *context, const qa_command_invocation *invocation,
                                  qa_error *error)
{
    struct application_native_q3_console *owner = context;
    ++owner->calls;
    qa_command_result result = application_startup_common_command(owner->provider,
        owner->console, owner->cvars, invocation, error);
    if (result == QA_COMMAND_UNHANDLED)
        result = application_command_fallback(owner->provider->application, invocation, error);
    --owner->calls;
    return result;
}
static bool operator_kick(void *context,const qa_command_invocation *call,qa_error *error)
{
    struct application_native_q3_console *owner=context;
    application_provider *provider=owner->provider; qa_application *app=provider->application;
    if (!call || call->console!=owner->console || call->receiver!=provider->owner || call->registration_owner!=provider->owner ||
        call->context.origin==QA_COMMAND_REMOTE ||
        call->context.actor.registry || !qa_console_invocation_current(owner->console,call) ||
        !qa_application_command_context_active(app,&call->context))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q3 kick requires its actual entered Source operator");
    if (qa_application_get_state(app)!=QA_APPLICATION_RUNNING ||
        application_world_provider(app,QA_ROLE_ENTITIES,"")!=provider) {
        qa_console_emit(owner->console,&call->context,"Server is not running.\n"); return true;
    }
    if (call->argc!=2) {
        qa_console_emit(owner->console,&call->context,"Usage: kick <player name|slot|all|allbots>\n"); return true;
    }
    if (!application_native_q3_console_borrow(provider,error)) return false;
    uint32_t maximum; bool ok=qa_q3_source_max_clients(provider->state.q3,&maximum,error),matched=false;
    const char *target=call->argv[1]; char wanted[1024];
    application_native_q3_name_key(target,wanted,sizeof(wanted));
    bool all=strlen(target)==3 && !strcmp(wanted,"all"),
        allbots=strlen(target)==7 && !strcmp(wanted,"allbots");
    for (uint32_t slot=0;ok && slot<maximum;++slot) {
        qa_actor_id dropping; const char *reason; bool pending;
        ok=application_native_q3_wire_drop_client_read(provider,slot,&dropping,&reason,&pending,error);
        if (!ok || pending) continue;
        application_native_q3_wire_client_view wire; bool admitted;
        qa_q3_native_client client;
        ok=application_native_q3_wire_client_admission_read(provider,slot,&wire,&admitted,error);
        if (!ok || !admitted) continue;
        ok=qa_q3_client_read(provider->state.q3,wire.actor,&client,error);
        if (!ok) break;
        char number[16],name[QA_Q3_NATIVE_NETNAME];
        snprintf(number,sizeof(number),"%u",slot);
        application_native_q3_name_key(client.netname,name,sizeof(name));
        if (!(all || (allbots && wire.bot) || (!allbots &&
            (!strcmp(target,number) || !strcmp(wanted,name))))) continue;
        if (wire.seat!=UINT32_MAX) {
            if (!all && !allbots) {
                qa_console_emit(owner->console,&call->context,"Cannot kick host player\n"); matched=true;
            }
            continue;
        }
        matched=true;
        ok=(!wire.bot || application_bots_catalog_remove_begin(app,slot,error)) &&
            application_native_q3_wire_drop(provider,slot,"was kicked",error) &&
            qa_application_command_context_active(app,&call->context);
        if (!ok && (!error || !error->code))
            application_fail(error,QA_ERROR_ARGUMENT,"Q3 kick changed its retained Source operator");
    }
    if (ok && !matched && !all && !allbots) {
        char text[1152]; snprintf(text,sizeof(text),"Player %.1023s is not on the server\n",target);
        qa_console_emit(owner->console,&call->context,text);
    }
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_console_at(application_provider *provider, qa_console **console,
                                       qa_cvars **cvars, qa_command_context *context)
{
    struct application_native_q3_console *owner = provider ? provider->native_q3_console : NULL;
    if (!owner || !console) return false;
    *console = owner->console;
    if (cvars) *cvars = owner->cvars;
    if (context) *context = (qa_command_context){.owner = provider->owner,
        .cvar_view = qa_cvars_view_identity(owner->cvars),
        .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SERVER};
    return true;
}

static bool register_engine_cvars(struct application_native_q3_console *owner,
    const char *map_path, qa_error *error)
{
    const char *name = !strncmp(map_path, "maps/", 5) ? map_path + 5 : map_path;
    size_t length = strlen(name);
    if (length > 4 && !strcmp(name + length - 4, ".bsp")) length -= 4;
    if (!length)
        return application_fail(error, QA_ERROR_FORMAT,
                                "native Q3 startup map has no source identity");
    char *map = malloc(length + 1);
    if (!map)
        return application_fail(error, QA_ERROR_MEMORY,
                                "retaining native Q3 startup map identity");
    memcpy(map, name, length);
    map[length] = 0;

    bool okay = true;
    for (size_t i = 0; okay && i < sizeof(engine_cvars) / sizeof(engine_cvars[0]); ++i) {
        const q3_engine_cvar *definition = &engine_cvars[i];
        okay = qa_cvars_register(owner->cvars, definition->name,
            definition->value ? definition->value : map, definition->flags,
            owner->provider->owner, NULL, error);
    }
    if (okay) okay = qa_cvars_register(owner->cvars, "dedicated", "0", 0,
        owner->provider->owner, NULL, error);
    free(map);
    return okay;
}

bool application_native_q3_console_create(application_provider *provider,
    const char *map_path, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->owner ||
        !provider->application || !provider->application->session || !provider->launch ||
        !provider->product || provider->product->family != QA_GAME_Q3 ||
        !map_path || provider->close_pending || provider->native_q3_console)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 console requires its actual source owner");
    struct application_native_q3_console *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "allocating native Q3 source console");
    owner->provider = provider;
    qa_cvar_options cvars = {.dialect = QA_CONSOLE_Q3,
        .side = QA_CVAR_SIDE_SERVER, .role = QA_CVAR_ROLE_GAME, .user = owner, .print = cvar_print,
        .cheats_allowed = cheats_allowed};
    owner->cvars = qa_cvars_create_view(provider->application->cvars, &cvars, error);
    qa_console_options options = {.context = {.owner = provider->owner, .dialect = QA_CONSOLE_Q3,
        .origin = QA_COMMAND_SERVER}, .cvars = owner->cvars, .user = owner, .print = print,
        .cvar_owner = cvar_owner, .visible_cvars = visible_cvars, .cvar_edit = cvar_edit,
        .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = script_complete,
        .allow_command = allow_command, .source_command = command};
    options.context.cvar_view = qa_cvars_view_identity(owner->cvars);
    if (owner->cvars && qa_console_bind_source(provider->application->console, &options, error))
        owner->console = provider->application->console;
    if (!owner->console || !application_startup_seed_source(provider, owner->cvars, error) ||
        !application_q3_product_register_source(application_q3_product_source_policy(provider->application),
            owner->cvars, provider->owner, error) || !register_engine_cvars(owner, map_path, error)) {
        qa_console_unbind_source(owner->console, qa_cvars_view_identity(owner->cvars), error);
        qa_cvars_detach_callbacks(owner->cvars); qa_cvars_destroy(owner->cvars);
        free(owner);
        return false;
    }
    provider->native_q3_console = owner;
    qa_application *application=provider->application;
    application_provider *previous=application->startup_preinit_provider;
    if (application->operation==APPLICATION_PERSISTING) application->startup_preinit_provider=provider;
    bool registered=qa_console_register_context(owner->console, &options.context,"kick","Kick a Q3 player by name, slot, all or allbots",
        provider->owner,provider->owner,true,operator_kick,owner,error);
    application->startup_preinit_provider=previous;
    if (!registered) {
        application_native_q3_console_destroy(provider,NULL);
        return false;
    }
    return true;
}

bool application_native_q3_console_destroy(application_provider *provider, qa_error *error)
{
    if (!provider || !application_native_q3_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 console is borrowed");
    struct application_native_q3_console *owner = provider->native_q3_console;
    if (owner) {
        if (!application_startup_source_retire(provider, owner->console, owner->cvars, error)) return false;
        if (!qa_console_unbind_source(owner->console, qa_cvars_view_identity(owner->cvars), error)) return false;
        qa_cvars_remove_owner(owner->cvars, provider->owner);
        qa_cvars_detach_callbacks(owner->cvars); qa_cvars_destroy(owner->cvars);
        free(owner);
        provider->native_q3_console = NULL;
    }
    return true;
}
