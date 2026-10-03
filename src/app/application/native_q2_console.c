#include "native_q2_console.h"
#include "q3_product.h"
#include "startup_flow.h"
#include "map_players_private.h"
#include "qa/cvars_save.h"
#include "qa/console_cvar_observer.h"
#include "qa/console_cvars_prepare.h"
#include "qa/game_q2_source.h"
#include "qa/server_admin.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct q2_source_cvar {
    const char *name, *value;
    uint32_t flags;
} q2_source_cvar;

/* The Source settings owner declares this ENGINE value in both Q2 dialects.
 * Rerelease GAME may separately register it through its actual imports. */
static const q2_source_cvar engine_cvars[] = {
    {"sv_airaccelerate", "0", 0},
};

bool application_native_q2_engine_cvars(qa_cvars *cvars, uint64_t owner, qa_error *error) {
    if (!cvars || !owner || (qa_cvars_dialect(cvars) != QA_CONSOLE_Q2 &&
        qa_cvars_dialect(cvars) != QA_CONSOLE_Q2_RERELEASE))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 ENGINE declarations require their physical Source registry");
    for (size_t i = 0; i < sizeof(engine_cvars) / sizeof(*engine_cvars); ++i)
        if (!qa_cvars_register(cvars, engine_cvars[i].name, engine_cvars[i].value,
            engine_cvars[i].flags, owner, NULL, error)) return false;
    return true;
}

/* Original InitGame and the TypeScript Q2 source settings owner. Q2 flags
 * retain their source word; GAME belongs only to rerelease GAME registrations. */
static const q2_source_cvar common[] = {
    {"gun_x", "0", 0}, {"gun_y", "0", 0}, {"gun_z", "0", 0},
    {"sv_rollspeed", "200", 0}, {"sv_rollangle", "2", 0},
    {"sv_maxvelocity", "2000", 0}, {"sv_gravity", "800", 0},
    {"dedicated", "0", QA_Q2_CVAR_NOSET},
    {"cheats", "0", QA_CVAR_SERVERINFO | QA_Q2_CVAR_LATCH},
    {"maxclients", "4", QA_CVAR_SERVERINFO | QA_Q2_CVAR_LATCH},
    {"maxspectators", "4", QA_CVAR_SERVERINFO},
    {"deathmatch", "0", QA_Q2_CVAR_LATCH}, {"coop", "0", QA_Q2_CVAR_LATCH},
    {"skill", "1", QA_Q2_CVAR_LATCH}, {"maxentities", "1024", QA_Q2_CVAR_LATCH},
    {"dmflags", "0", QA_CVAR_SERVERINFO}, {"fraglimit", "0", QA_CVAR_SERVERINFO},
    {"timelimit", "0", QA_CVAR_SERVERINFO}, {"password", "", QA_CVAR_USERINFO},
    {"spectator_password", "", QA_CVAR_USERINFO}, {"needpass", "0", QA_CVAR_SERVERINFO},
    {"filterban", "1", 0}, {"g_select_empty", "0", QA_CVAR_ARCHIVE},
    {"run_pitch", "0.002", 0}, {"run_roll", "0.005", 0},
    {"bob_up", "0.005", 0}, {"bob_pitch", "0.002", 0}, {"bob_roll", "0.002", 0},
    {"flood_msgs", "4", 0}, {"flood_persecond", "4", 0}, {"flood_waitdelay", "10", 0},
};
static const q2_source_cvar rerelease[] = {
    {"sv_stopspeed", "100", 0},
    {"teamplay", "0", QA_Q2_CVAR_LATCH}, {"huntercam", "1", QA_CVAR_SERVERINFO | QA_Q2_CVAR_LATCH},
    {"g_coop_player_collision", "0", QA_Q2_CVAR_LATCH},
    {"g_coop_squad_respawn", "1", QA_Q2_CVAR_LATCH},
    {"g_coop_enable_lives", "0", QA_Q2_CVAR_LATCH},
    {"g_coop_num_lives", "2", QA_Q2_CVAR_LATCH},
    {"g_coop_instanced_items", "1", QA_Q2_CVAR_LATCH},
    {"capturelimit", "0", QA_CVAR_SERVERINFO},
    {"g_quick_weapon_switch", "1", QA_Q2_CVAR_LATCH},
    {"g_instant_weapon_switch", "0", QA_Q2_CVAR_LATCH},
    {"g_infinite_ammo", "0", QA_Q2_CVAR_LATCH},
    {"g_weapon_respawn_time", "30", 0}, {"g_dm_weapons_stay", "0", 0},
    {"g_dm_instant_items", "1", 0}, {"g_dm_same_level", "0", 0},
    {"g_no_health", "0", 0}, {"g_no_items", "0", 0}, {"g_no_armor", "0", 0},
    {"g_friendly_fire", "0", 0},
    {"g_instagib", "0", 0}, {"g_damage_scale", "1", 0}, {"ai_damage_scale", "1", 0},
    {"g_teamplay_armor_protect", "0", 0},
    {"g_no_mines", "0", 0}, {"g_no_nukes", "0", 0}, {"g_no_spheres", "0", 0},
    {"g_dm_random_items", "0", 0}, {"g_dm_no_quadfire_drop", "0", 0},
    {"g_dm_no_quad_drop", "0", 0}, {"g_dm_no_stack_double", "0", 0},
    {"g_dm_strong_mines", "0", 0}, {"g_dm_force_respawn", "0", 0},
    {"g_dm_force_respawn_time", "0", 0}, {"g_dm_no_fall_damage", "0", 0},
    {"g_dm_spawn_farthest", "1", 0}, {"g_dm_allow_exit", "0", 0},
    {"g_start_items", "", QA_Q2_CVAR_LATCH}, {"g_map_list", "", 0},
    {"g_map_list_shuffle", "0", 0},
};
static const q2_source_cvar rogue[] = {
    {"sv_stopspeed", "100", 0}, {"huntercam", "1", QA_CVAR_SERVERINFO | QA_Q2_CVAR_LATCH},
    {"strong_mines", "0", 0}, {"randomrespawn", "0", 0}, {"gamerules", "0", QA_Q2_CVAR_LATCH},
};
static const q2_source_cvar lmctf[] = {
    {"ctfflags", "0", QA_CVAR_SERVERINFO}, {"refset", "0", QA_CVAR_SERVERINFO},
    {"runes", "15", QA_CVAR_SERVERINFO}, {"countdown_time", "15", 0}, {"autolock", "0", 0},
    {"fastswitch", "0", 0}, {"disabled_weps", "0", 0},
};

struct application_native_q2_console {
    application_provider *provider;
    qa_console *console;
    qa_cvars *cvars;
    application_q2_source_scripts scripts;
    size_t calls;
    qa_cvar_observer_token observers[sizeof(engine_cvars) / sizeof(*engine_cvars) + sizeof(common) / sizeof(*common) + sizeof(rerelease) / sizeof(*rerelease) + sizeof(rogue) / sizeof(*rogue) + sizeof(lmctf) / sizeof(*lmctf) + 2];
    size_t observer_count;
};

static qa_console_dialect dialect(const application_provider *provider) {
    return provider->launch->selection.clock.kind == QA_CLOCK_Q2_RERELEASE ?
        QA_CONSOLE_Q2_RERELEASE : QA_CONSOLE_Q2;
}
static bool classic_rogue(const application_provider *provider) {
    return dialect(provider) == QA_CONSOLE_Q2 && !strcmp(provider->product->campaign, "rogue");
}
qa_cvars *application_native_q2_console_registry(const application_provider *provider) {
    return provider && provider->kind == APPLICATION_PROVIDER_Q2 && provider->native_q2_console ?
        provider->native_q2_console->cvars : NULL;
}
bool application_native_q2_console_idle(const application_provider *provider) {
    const struct application_native_q2_console *owner = provider ? provider->native_q2_console : NULL;
    return !owner || (!owner->calls && qa_console_idle(owner->console) && qa_cvars_observer_idle(owner->cvars));
}
static bool capture(void *opaque, const qa_command_context *source,
                     qa_command_context *out, qa_error *error) {
    struct application_native_q2_console *owner = opaque;
    qa_command_context command = *source;
    if (command.owner && command.owner != owner->provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 console received another source owner");
    command.owner = owner->provider->owner;
    command.dialect = dialect(owner->provider);
    return application_command_capture(owner->provider->application, &command, out, error);
}
static bool active(void *opaque, const qa_command_context *command) {
    struct application_native_q2_console *owner = opaque;
    return command && command->owner == owner->provider->owner &&
        command->dialect == dialect(owner->provider) &&
        application_command_active(owner->provider->application, command);
}
static void print(void *opaque, const qa_command_context *context, const char *text) {
    struct application_native_q2_console *owner = opaque;
    ++owner->calls;
    application_console_print(owner->provider->application, context, text);
    --owner->calls;
}
static void cvar_print(void *opaque, const char *text) {
    struct application_native_q2_console *owner = opaque;
    qa_console_emit(owner->console, NULL, text);
}
static const qa_launch_instance *source_descriptor(application_provider *provider)
{
    qa_application *app=provider->application;
    const qa_launch_snapshot *snapshots[]={app->routing_snapshot,
        qa_application_startup_candidate(app),qa_application_launch(app)};
    for (size_t i=0;i<sizeof(snapshots)/sizeof(*snapshots);++i) {
        const qa_launch_instance *selected=snapshots[i]?
            qa_launch_snapshot_find(snapshots[i],provider->launch->selection.instance):NULL;
        if (selected && selected->state==provider && selected->storage==provider->launch->storage) return selected;
    }
    return NULL;
}
static qa_command_result command(void *opaque, const qa_command_invocation *invocation,
                                  qa_error *error) {
    struct application_native_q2_console *owner = opaque;
    ++owner->calls;
    qa_command_result result = application_command_fallback(owner->provider->application, invocation, error);
    application_provider *provider=owner->provider;
    const qa_application_startup_hooks *hooks=provider->application->startup_hooks;
    if (result==QA_COMMAND_UNHANDLED && hooks && hooks->source_common_command) {
        const qa_launch_instance *descriptor=source_descriptor(provider);
        if (!descriptor) {
            --owner->calls;
            application_fail(error,QA_ERROR_ARGUMENT,"Q2 common command lost its actual Source descriptor");
            return QA_COMMAND_FAILED;
        }
        qa_application_startup_source source={.descriptor=descriptor,
            .scope={.provider=provider->owner,.kind=QA_APPLICATION_CONSOLE_Q2_GAME},
            .console=owner->console,.cvars=owner->cvars,.command=invocation->context,
            .declaration_owner=provider->owner};
        bool handled=false;
        bool okay=hooks->source_common_command(hooks->context,provider->application,
            &source,invocation,&handled,error);
        result=!okay?QA_COMMAND_FAILED:handled?QA_COMMAND_HANDLED:QA_COMMAND_UNHANDLED;
    }
    --owner->calls;
    return result;
}
static bool read_script(void *opaque, const qa_command_context *context, const char *path,
                         qa_bytes *out, void **lease, qa_error *error) {
    struct application_native_q2_console *owner = opaque;
    if (!active(owner, context))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 script belongs to a retired publication");
    if (owner->scripts.read)
        return owner->scripts.read(owner->scripts.context, context, path, out, lease, error);
    if (application_startup_source_scripts(owner->provider))
        return application_startup_source_script_read(owner->provider, owner->console,
            context, path, out, lease, error);
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(owner->provider->launch->content, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource);
    *lease = resource;
    return true;
}
static void release_script(void *opaque, void *lease) {
    struct application_native_q2_console *owner = opaque;
    if (owner->scripts.release) owner->scripts.release(owner->scripts.context, lease);
    else if (application_startup_source_scripts(owner->provider))
        application_startup_source_script_release(owner->provider, owner->console, lease);
    else qa_resource_release(lease);
}
static void script_complete(void *opaque, const qa_command_context *context,
                            const char *path, bool success) {
    struct application_native_q2_console *owner = opaque;
    if (owner->scripts.complete)
        owner->scripts.complete(owner->scripts.context, context, path, success);
}
static bool cheats(void *opaque) {
    struct application_native_q2_console *owner = opaque;
    const qa_cvar_view *value = qa_cvars_find(owner->cvars, "cheats");
    return value && value->integer != 0;
}
static bool allow_command(void *opaque, const qa_command_invocation *invocation) {
    struct application_native_q2_console *owner = opaque;
    return application_startup_command_allowed(owner->provider, invocation);
}
static qa_cvars *cvar_owner(void *opaque, const qa_command_context *context, const char *name) {
    struct application_native_q2_console *owner = opaque;
    qa_cvars *registry = application_startup_cvar_owner(owner->provider, owner->console, context, name);
    return registry ? registry : owner->cvars;
}
static bool cvar_edit(void *opaque, const qa_command_context *context, qa_cvars *registry,
                      qa_cvars_edit **out, qa_error *error) {
    struct application_native_q2_console *owner = opaque;
    return application_startup_cvar_edit(owner->provider, owner->console, context, registry, out, error);
}
static qa_cvars *visible_cvars(void *opaque, const qa_command_context *context, size_t index) {
    struct application_native_q2_console *owner = opaque;
    qa_cvars *registry = NULL;
    if (application_startup_visible_cvars(owner->provider, owner->console, context, index, &registry))
        return registry;
    return index == 0 ? owner->cvars : NULL;
}
typedef struct q2_operator_player {
    qa_q2_player_info info;
    char *userinfo;
} q2_operator_player;
static bool operator_command(void *opaque, const qa_command_invocation *invocation, qa_error *error) {
    struct application_native_q2_console *owner = opaque;
    application_provider *provider = owner->provider;
    qa_application *app = provider->application;
    if (!active(owner, &invocation->context) || !provider->state.q2)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 operator command has no admitted source");
    bool dump = !strcmp(invocation->argv[0], "dumpuser");
    if (dump && invocation->argc != 2) {
        qa_console_emit(owner->console, &invocation->context, "Usage: dumpuser <player name|slot>\n");
        return true;
    }
    size_t capacity = app->players ? app->players->count : 0, count = 0;
    if (capacity > SIZE_MAX / sizeof(q2_operator_player))
        return application_fail(error, QA_ERROR_MEMORY, "Q2 operator roster exceeds native storage");
    q2_operator_player *players = capacity ? calloc(capacity, sizeof(*players)) : NULL;
    if (capacity && !players)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 operator player rows");
    for (size_t i = 0; i < capacity; ++i) {
        const application_player_record *row = &app->players->records[i];
        qa_q2_player_info info;
        if (row->retiring || !qa_q2_player_read(provider->state.q2, row->actor, &info) || !info.connected) continue;
        q2_operator_player *player = &players[count++];
        player->info = info;
        const char *userinfo = row->userinfo ? row->userinfo : "";
        size_t size = strlen(userinfo) + 1;
        player->userinfo = malloc(size);
        if (!player->userinfo) {
            for (size_t j = 0; j < count; ++j) free(players[j].userinfo);
            free(players);
            return application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 operator userinfo");
        }
        memcpy(player->userinfo, userinfo, size);
    }
    ++owner->calls;
    bool okay = true;
    if (!dump) {
        const char *map = qa_strings_cstr(qa_session_strings(app->session), app->current_map);
        qa_console_emit(owner->console, &invocation->context, "map              : ");
        if (active(owner, &invocation->context)) qa_console_emit(owner->console, &invocation->context, map ? map : "");
        if (active(owner, &invocation->context)) qa_console_emit(owner->console, &invocation->context, "\nnum score ping name\n");
        for (size_t i = 0; i < count && active(owner, &invocation->context); ++i) {
            char line[128];
            snprintf(line, sizeof(line), "%u %d %d %s\n", players[i].info.slot,
                players[i].info.score, players[i].info.ping, players[i].info.name);
            qa_console_emit(owner->console, &invocation->context, line);
        }
    } else {
        const char *target = invocation->argv[1];
        bool numeric = *target != 0;
        for (const char *p = target; *p; ++p) numeric &= *p >= '0' && *p <= '9';
        q2_operator_player *selected = NULL;
        for (size_t i = 0; i < count; ++i) {
            if (numeric ? strtoul(target, NULL, 10) == players[i].info.slot : !strcmp(target, players[i].info.name)) {
                selected = &players[i];
                break;
            }
        }
        if (!selected) {
            qa_console_emit(owner->console, &invocation->context, "Player ");
            if (active(owner, &invocation->context)) qa_console_emit(owner->console, &invocation->context, target);
            if (active(owner, &invocation->context)) qa_console_emit(owner->console, &invocation->context, " is not on the server\n");
        } else {
            qa_console_emit(owner->console, &invocation->context, "userinfo\n--------\n");
            for (char *p = selected->userinfo; *p && active(owner, &invocation->context);) {
                if (*p == '\\') ++p;
                char *key = p;
                while (*p && *p != '\\') ++p;
                if (!*p) break;
                *p++ = 0;
                char *value = p;
                while (*p && *p != '\\') ++p;
                if (*p) *p++ = 0;
                qa_console_emit(owner->console, &invocation->context, key);
                size_t length = strlen(key);
                char padding[21];
                size_t spaces = length < 20 ? 20 - length : 1;
                memset(padding, ' ', spaces); padding[spaces] = 0;
                if (active(owner, &invocation->context)) qa_console_emit(owner->console, &invocation->context, padding);
                if (active(owner, &invocation->context)) qa_console_emit(owner->console, &invocation->context, value);
                if (active(owner, &invocation->context)) qa_console_emit(owner->console, &invocation->context, "\n");
            }
        }
    }
    if (!active(owner, &invocation->context))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Q2 operator output retired its source publication");
    --owner->calls;
    for (size_t i = 0; i < count; ++i) free(players[i].userinfo);
    free(players);
    return okay;
}
static bool password_required(const char *value) {
    static const char none[] = "none";
    if (!*value) return false;
    size_t i = 0;
    for (; value[i] && none[i]; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)none[i]) return true;
    }
    return value[i] != none[i];
}
static bool changed(void *opaque, qa_cvars *registry, const char *name, qa_error *error) {
    struct application_native_q2_console *owner = opaque;
    if (!strcmp(name, "password") || !strcmp(name, "spectator_password")) {
        const qa_cvar_view *password = qa_cvars_find(registry, "password");
        const qa_cvar_view *spectator = qa_cvars_find(registry, "spectator_password");
        unsigned required = (password && password_required(password->value) ? 1u : 0u) |
                            (spectator && password_required(spectator->value) ? 2u : 0u);
        char value[2] = {(char)('0' + required), 0};
        if (!qa_cvars_set(registry, "needpass", value, true, error)) return false;
    }
    bool reset_rotation = !strcmp(name, "sv_maplist") || !strcmp(name, "g_map_list");
    return !owner->provider->state.q2 ||
        qa_q2_source_apply(owner->provider->state.q2, registry, reset_rotation, error);
}

bool application_native_q2_console_create_restored(application_provider *provider, qa_error *error) {
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q2 || !provider->owner ||
        !provider->application || !provider->application->session || !provider->launch ||
        !provider->product || provider->product->family != QA_GAME_Q2 ||
        provider->close_pending || provider->native_q2_console)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 console requires its actual source owner");
    struct application_native_q2_console *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Allocating Q2 source console");
    owner->provider = provider;
    qa_cvar_options variables = {.dialect = dialect(provider), .user = owner,
        .print = cvar_print, .cheats_allowed = cheats};
    owner->cvars = qa_cvars_create(&variables, error);
    qa_console_options options = {.context = {.owner = provider->owner, .dialect = dialect(provider),
        .origin = QA_COMMAND_SERVER}, .cvars = owner->cvars, .user = owner, .print = print,
        .cvar_owner = cvar_owner, .visible_cvars = visible_cvars, .cvar_edit = cvar_edit,
        .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = script_complete,
        .allow_command = allow_command, .source_command = command};
    if (owner->cvars) owner->console = qa_console_create(&options, error);
    if (!owner->console) {
        qa_console_destroy(owner->console);
        qa_cvars_destroy(owner->cvars);
        free(owner);
        return false;
    }
    provider->native_q2_console = owner;
    if (!qa_console_register(owner->console, "status", "Print Q2 source map and connected players",
            provider->owner, true, operator_command, owner, error) ||
        !qa_console_register(owner->console, "dumpuser", "Print a Q2 source player's userinfo",
            provider->owner, true, operator_command, owner, error)) {
        application_native_q2_console_destroy(provider, NULL);
        return false;
    }
    return true;
}
static bool observe_name(struct application_native_q2_console *owner, const char *name, qa_error *error) {
    if (!qa_cvars_find(owner->cvars, name))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 source continuation omits a required cvar");
    if (!qa_cvars_observe(owner->cvars, name, owner->provider->owner, changed, owner,
            &owner->observers[owner->observer_count], error)) return false;
    ++owner->observer_count;
    return true;
}
static bool observe(struct application_native_q2_console *owner, qa_error *error) {
    if (owner->observer_count) return true;
    for (size_t i = 0; i < sizeof(engine_cvars) / sizeof(*engine_cvars); ++i)
        if (!observe_name(owner, engine_cvars[i].name, error)) return false;
    for (size_t i = 0; i < sizeof(common) / sizeof(*common); ++i)
        if (!observe_name(owner, common[i].name, error)) return false;
    if (dialect(owner->provider) == QA_CONSOLE_Q2_RERELEASE) {
        for (size_t i = 0; i < sizeof(rerelease) / sizeof(*rerelease); ++i)
            if (!observe_name(owner, rerelease[i].name, error)) return false;
    } else {
        if (!observe_name(owner, "sv_maplist", error)) return false;
        if (classic_rogue(owner->provider))
            for (size_t i = 0; i < sizeof(rogue) / sizeof(*rogue); ++i)
                if (!observe_name(owner, rogue[i].name, error)) return false;
    }
    if (qa_cvars_find(owner->cvars, "ctfflags"))
        for (size_t i = 0; i < sizeof(lmctf) / sizeof(*lmctf); ++i)
            if (!observe_name(owner, lmctf[i].name, error)) return false;
    if (qa_cvars_find(owner->cvars, "goallimit") && !observe_name(owner, "goallimit", error)) return false;
    return true;
}
static bool clone(application_provider *provider, qa_cvars *destination, bool *cloned, qa_error *error) {
    *cloned = false;
    if (provider->application->startup_hooks) {
        struct application_native_q2_console *owner = provider->native_q2_console;
        qa_application_startup_source source = {
            .descriptor = provider->launch,
            .scope = {.provider = provider->owner, .kind = QA_APPLICATION_CONSOLE_Q2_GAME},
            .console = owner->console, .cvars = destination, .declaration_owner = provider->owner,
            .command = {.owner = provider->owner, .dialect = dialect(provider), .origin = QA_COMMAND_SERVER},
        };
        return application_startup_source_carry(provider, &source, cloned, error);
    }
    for (application_provider *previous = provider->application->live_providers; previous; previous = previous->next_live) {
        if (previous == provider || previous->kind != APPLICATION_PROVIDER_Q2 ||
            !previous->constructed || !previous->attached || previous->close_pending ||
            previous->owner != provider->owner || !previous->launch ||
            strcmp(previous->launch->selection.instance, provider->launch->selection.instance)) continue;
        qa_cvars *source = application_native_q2_console_registry(previous);
        if (!source || qa_cvars_dialect(source) != qa_cvars_dialect(destination)) continue;
        qa_buffer bytes = {0};
        qa_cvars_restore *ticket = NULL;
        bool okay = application_native_q2_console_idle(previous) &&
            qa_cvars_save_capture(source, &bytes, error) &&
            qa_cvars_save_prepare(destination, (qa_bytes){bytes.data, bytes.size}, &ticket, error) &&
            qa_cvars_save_commit(ticket, error);
        if (!okay) qa_cvars_save_abort(ticket);
        qa_buffer_free(&bytes);
        if (okay) *cloned = true;
        return okay;
    }
    return true;
}
static bool definitions(application_provider *provider, const q2_source_cvar *table,
                         size_t count, qa_error *error) {
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    uint32_t game = dialect(provider) == QA_CONSOLE_Q2_RERELEASE ? QA_Q2_CVAR_GAME : 0;
    for (size_t i = 0; i < count; ++i) {
        const char *value = table[i].value;
        uint32_t flags = table[i].flags | game;
        if (game && !strcmp(table[i].name, "maxclients")) value = "8";
        if (game && !strcmp(table[i].name, "maxentities")) value = "8192";
        if (!game && !strcmp(provider->product->campaign, "xatrix") &&
            !strcmp(table[i].name, "spectator_password")) flags = 0;
        if (!qa_cvars_register(cvars, table[i].name, value, flags,
                provider->owner, NULL, error)) return false;
    }
    return true;
}
static bool missing_definitions(application_provider *provider, const q2_source_cvar *table,
                                size_t count, qa_error *error) {
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    for (size_t i = 0; i < count; ++i)
        if (!qa_cvars_find(cvars, table[i].name) && !definitions(provider, table + i, 1, error)) return false;
    return true;
}
bool application_native_q2_console_prepare(application_provider *provider, const qa_q2_options *rules,
                                          const qa_launch_choices *choices, qa_error *error) {
    if (!rules || !choices || !application_native_q2_console_create_restored(provider, error)) return false;
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    bool cloned = false;
    bool okay = application_startup_seed_source(provider, cvars, error) && clone(provider, cvars, &cloned, error);
    const qa_cvar_view *dedicated = qa_cvars_find(cvars, "dedicated");
    char *dedicated_value = NULL;
    if (okay && !cloned && dedicated) {
        size_t size = strlen(dedicated->value) + 1;
        dedicated_value = malloc(size);
        if (!dedicated_value) okay = application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 engine startup mode");
        else memcpy(dedicated_value, dedicated->value, size);
    }
    const char *names[] = {"skill", "deathmatch", "coop", "maxclients"};
    bool present[4];
    for (size_t i = 0; i < 4; ++i) present[i] = qa_cvars_find(cvars, names[i]) != NULL;
    if (okay && !cloned) okay = definitions(provider, common, sizeof(common) / sizeof(*common), error);
    if (okay && !cloned) okay = application_native_q2_engine_cvars(cvars, provider->owner, error);
    if (okay) okay = qa_server_admin_declarations(cvars,provider->owner,error);
    if (okay && dedicated_value) okay = qa_cvars_set(cvars, "dedicated", dedicated_value, true, error);
    free(dedicated_value);
    bool rr = dialect(provider) == QA_CONSOLE_Q2_RERELEASE;
    if (okay && !cloned) okay = rr ? definitions(provider, rerelease, sizeof(rerelease) / sizeof(*rerelease), error) :
        qa_cvars_register(cvars, "sv_maplist", "", 0, provider->owner, NULL, error);
    if (okay && classic_rogue(provider))
        okay = missing_definitions(provider, rogue, sizeof(rogue) / sizeof(*rogue), error);
    for (size_t i = 0; okay && i < choices->mode_count; ++i) {
        const qa_launch_mode *mode = &choices->modes[i];
        if (strcmp(provider->launch->selection.instance, mode->instance)) continue;
        if (mode->rules.source == QA_MODE_LMCTF)
            okay = missing_definitions(provider, lmctf, sizeof(lmctf) / sizeof(*lmctf), error);
        if (mode->rules.source == QA_MODE_Q2_DEATHBALL && !qa_cvars_find(cvars, "goallimit"))
            okay = qa_cvars_register(cvars, "goallimit", "0", 0, provider->owner, NULL, error);
    }
    char skill[16], maximum[sizeof(size_t) * CHAR_BIT + 1];
    snprintf(skill, sizeof(skill), "%d", rules->skill);
    snprintf(maximum, sizeof(maximum), "%zu", choices->seat_count ? choices->seat_count : 1);
    const char *values[] = {skill, rules->deathmatch ? "1" : "0", rules->cooperative ? "1" : "0", maximum};
    for (size_t i = 0; okay && !cloned && i < 4; ++i)
        if (!present[i]) okay = qa_cvars_set(cvars, names[i], values[i], true, error);
    if (okay) {
        qa_cvars_set_server_active(cvars, false);
        okay = changed(provider->native_q2_console, cvars, "password", error) &&
            observe(provider->native_q2_console, error);
    }
    if (!okay) application_native_q2_console_destroy(provider, NULL);
    return okay;
}
bool application_native_q2_console_finalize(application_provider *provider, qa_q2_options *rules,
                                            const qa_launch_choices *choices, qa_error *error) {
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    if (!cvars || !rules || !choices || provider->state.q2 || !application_native_q2_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 source options require their completed idle preparation");
    bool okay = true;
    for (size_t i = 0; okay && i < qa_cvars_count(cvars); ++i) {
        const qa_cvar_view *value = qa_cvars_at(cvars, i);
        if (value->latched_value) okay = qa_cvars_apply_latched(cvars, value->name, error);
    }
    float capacity = 0;
    if (okay) okay = application_native_q2_source_number(provider, "maxclients", &capacity, error);
    if (okay) {
        rules->deathmatch = qa_cvars_find(cvars, "deathmatch")->number != 0;
        rules->cooperative = qa_cvars_find(cvars, "coop")->number != 0;
        if (capacity <= 1) capacity = rules->deathmatch ? 8 : rules->cooperative ? 4 : 1;
        if (capacity < choices->seat_count) capacity = (float)choices->seat_count;
        if (!isfinite(capacity) || capacity < 1 || capacity > 64)
            okay = application_fail(error, QA_ERROR_FORMAT, "Q2 source capacity must be between 1 and 64");
        if (okay) okay = qa_cvars_set_number(cvars, "maxclients", truncf(capacity), error);
        const qa_cvar_view *difficulty = qa_cvars_find(cvars, "skill");
        rules->skill = difficulty->integer < 0 ? 0 : difficulty->integer > 3 ? 3 : difficulty->integer;
        rules->deathmatch_flags = qa_q2_source_deathmatch_flags(cvars);
        if (okay) okay = qa_cvars_set_number(cvars, "skill", (float)rules->skill, error);
    }
    return okay;
}
bool application_native_q2_console_scripts(application_provider *provider,
                                           const application_q2_source_scripts *scripts, qa_error *error) {
    struct application_native_q2_console *owner = provider ? provider->native_q2_console : NULL;
    if (!owner || !application_native_q2_console_idle(provider) || qa_console_pending(owner->console))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 scripts require their retained idle preparation owner");
    if (!scripts) {
        memset(&owner->scripts, 0, sizeof(owner->scripts));
        return true;
    }
    if (!scripts->read || !scripts->release || !scripts->complete || owner->scripts.read)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 scripts require complete unbound preparation callbacks");
    owner->scripts = *scripts;
    return true;
}
bool application_native_q2_console_destroy(application_provider *provider, qa_error *error) {
    if (!provider || !application_native_q2_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 source console is borrowed");
    struct application_native_q2_console *owner = provider->native_q2_console;
    if (owner) {
        if (!application_startup_source_retire(provider, owner->console, owner->cvars, error))
            return false;
        for (size_t i = 0; i < owner->observer_count; ++i)
            qa_cvars_unobserve(owner->cvars, owner->observers[i]);
        qa_console_destroy(owner->console);
        qa_cvars_destroy(owner->cvars);
        free(owner);
        provider->native_q2_console = NULL;
    }
    return true;
}
bool application_native_q2_console_at(application_provider *provider, qa_console **console,
                                      qa_cvars **cvars, qa_command_context *context) {
    struct application_native_q2_console *owner = provider ? provider->native_q2_console : NULL;
    if (!owner || !console) return false;
    *console = owner->console;
    if (cvars) *cvars = owner->cvars;
    if (context) *context = (qa_command_context){.owner = provider->owner,
        .dialect = dialect(provider), .origin = QA_COMMAND_SERVER};
    return true;
}
bool application_native_q2_console_capture(application_provider *provider, qa_buffer *out, qa_error *error) {
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    if (!cvars || !application_native_q2_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 registry capture requires its idle source owner");
    return qa_cvars_save_capture(cvars, out, error);
}
bool application_native_q2_console_restore(application_provider *provider, qa_bytes bytes, qa_error *error) {
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    if (!cvars || !application_native_q2_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 registry import requires its idle candidate owner");
    qa_cvars_restore *ticket = NULL;
    bool okay = qa_cvars_save_prepare(cvars, bytes, &ticket, error) && qa_cvars_save_commit(ticket, error);
    if (!okay) qa_cvars_save_abort(ticket);
    return okay && observe(provider->native_q2_console, error);
}
bool application_native_q2_rotation_changed(void *context, const qa_string_id *maps, size_t count, qa_error *error) {
    application_provider *provider = context;
    qa_console *console = NULL; qa_cvars *cvars = NULL; qa_command_context raw, command;
    if (!provider || !maps || !count || !provider->state.q2 ||
        !application_native_q2_console_at(provider, &console, &cvars, &raw) ||
        qa_cvars_dialect(cvars) != QA_CONSOLE_Q2_RERELEASE || !qa_cvars_find(cvars, "g_map_list") ||
        !qa_application_capture_command_context(provider->application, &raw, &command, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 rotation publication lost its actual rerelease Source");
    qa_cvars *actual = NULL; qa_cvars_edit *ticket = NULL;
    if (!qa_console_cvar_access(console, &command, "g_map_list", &actual, &ticket, error) || actual != cvars)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 rotation publication lost its actual private Source registry");
    qa_strings *strings = qa_session_strings(provider->application->session);
    size_t size = count;
    for (size_t i = 0; i < count; ++i) {
        const char *map = qa_strings_cstr(strings, maps[i]);
        if (!map || !*map || strlen(map) > SIZE_MAX - size)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 shuffled rotation has no genuine map token");
        size += strlen(map);
    }
    char *list = malloc(size);
    if (!list) return application_fail(error, QA_ERROR_MEMORY, "Publishing actual shuffled Q2 Source rotation");
    char *cursor = list;
    for (size_t i = 0; i < count; ++i) {
        const char *map = qa_strings_cstr(strings, maps[i]); size_t length = strlen(map);
        memcpy(cursor, map, length); cursor += length;
        if (i + 1 < count) *cursor++ = ' ';
    }
    *cursor = 0;
    qa_cvars_edit_command edit = {.kind = QA_CVARS_EDIT_SET, .name = "g_map_list", .value = list,
        .force = true, .owner = provider->owner};
    bool okay = qa_console_cvar_apply(console, &command, &edit, error);
    free(list); return okay;
}
bool application_native_q2_console_refresh(application_provider *provider, qa_error *error) {
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    return cvars && provider->state.q2 ? qa_q2_source_apply(provider->state.q2, cvars, false, error) :
        application_fail(error, QA_ERROR_ARGUMENT, "Q2 settings have no constructed source owner");
}
bool application_native_q2_source_player_rules(application_provider *provider, qa_q2_player_rules *rules,
                                               qa_error *error) {
    return qa_q2_source_player_rules(application_native_q2_console_registry(provider), rules, error);
}
bool application_native_q2_source_mode_rules(application_provider *provider, qa_mode_rules *rules,
                                             qa_error *error) {
    if (!rules || rules->source < QA_MODE_Q2 || rules->source >= QA_MODE_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 source settings require a Q2 mode owner");
    float time;
    int32_t frags;
    if (!application_native_q2_source_number(provider, "timelimit", &time, error) ||
        !application_native_q2_source_integer(provider, "fraglimit", &frags, error)) return false;
    rules->time_limit_minutes = time;
    rules->frag_limit = frags;
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    const qa_cvar_view *captures = qa_cvars_find(cvars, "capturelimit");
    if (captures && rules->source == QA_MODE_Q2_CTF) rules->capture_limit = captures->integer;
    if (rules->source == QA_MODE_Q2_DEATHBALL &&
        !application_native_q2_source_integer(provider, "goallimit", &rules->capture_limit, error)) return false;
    if (rules->source == QA_MODE_LMCTF) {
        int32_t flags, referee, runes, automatic;
        if (!application_native_q2_source_integer(provider, "ctfflags", &flags, error) ||
            !application_native_q2_source_integer(provider, "refset", &referee, error) ||
            !application_native_q2_source_integer(provider, "runes", &runes, error) ||
            !application_native_q2_source_integer(provider, "countdown_time", &rules->countdown_seconds, error) ||
            !application_native_q2_source_integer(provider, "autolock", &automatic, error)) return false;
        rules->flags = (uint32_t)flags;
        rules->referee_flags = (uint32_t)referee;
        rules->rune_mask = runes;
        rules->auto_lock = automatic != 0;
    }
    rules->friendly_fire = (qa_q2_source_deathmatch_flags(cvars) & 256u) == 0;
    rules->q2_rerelease = dialect(provider) == QA_CONSOLE_Q2_RERELEASE;
    return true;
}
bool application_native_q2_source_modes_refresh(application_provider *provider, qa_error *error) {
    if (!application_native_q2_console_registry(provider) || !provider->constructed ||
        !provider->attached || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 mode settings require their admitted source");
    qa_application *app = provider->application;
    for (size_t i = 0; i < app->mode_count; ++i) {
        qa_mode_id id = app->mode_ids[i];
        qa_mode_view view;
        if (application_mode_provider(app, id) != provider) continue;
        if (!qa_modes_read(app->modes, id, &view, error)) return false;
        if (view.rules.source < QA_MODE_Q2 || view.rules.source >= QA_MODE_Q3) continue;
        qa_mode_rules rules = view.rules;
        if (!application_native_q2_source_mode_rules(provider, &rules, error)) return false;
        if (rules.time_limit_minutes != view.rules.time_limit_minutes ||
            rules.frag_limit != view.rules.frag_limit || rules.capture_limit != view.rules.capture_limit ||
            rules.flags != view.rules.flags || rules.referee_flags != view.rules.referee_flags ||
            rules.rune_mask != view.rules.rune_mask || rules.countdown_seconds != view.rules.countdown_seconds ||
            rules.auto_lock != view.rules.auto_lock ||
            rules.friendly_fire != view.rules.friendly_fire || rules.q2_rerelease != view.rules.q2_rerelease)
            if (!qa_modes_configure(app->modes, id, &rules, error)) return false;
    }
    return true;
}
bool application_native_q2_source_number(const application_provider *provider, const char *name,
                                         float *out, qa_error *error) {
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    const qa_cvar_view *value = cvars && name ? qa_cvars_find(cvars, name) : NULL;
    if (!value || !out || !isfinite(value->number))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q2 setting has no finite source value");
    *out = value->number;
    return true;
}
bool application_native_q2_source_integer(const application_provider *provider, const char *name,
                                          int32_t *out, qa_error *error) {
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    const qa_cvar_view *value = cvars && name ? qa_cvars_find(cvars, name) : NULL;
    if (!value || !out)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q2 setting has no source integer");
    *out = value->integer;
    return true;
}
bool application_native_q2_cvar(void *opaque, qa_string_id name, float *out, qa_error *error) {
    application_provider *provider = opaque;
    const char *text = provider && provider->application && provider->application->session ?
        qa_strings_cstr(qa_session_strings(provider->application->session), name) : NULL;
    if (!text || !provider->constructed || provider->close_pending || provider->application->destroy_requested)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q2 cvar source has retired");
    const qa_cvar_view *value = qa_cvars_find(application_native_q2_console_registry(provider), text);
    if (!out || (value && value->owner && value->owner != provider->owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 cvar belongs to another source");
    *out = value ? value->number : 0;
    return true;
}
bool application_native_q2_source_weapon_input(application_provider *provider, qa_q2_weapon_input *input,
                                               qa_error *error) {
    if (!input || !application_native_q2_console_registry(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 weapon input requires its source registry");
    if (dialect(provider) == QA_CONSOLE_Q2_RERELEASE) {
        int32_t infinite, instant, quick, no_stack;
        if (!application_native_q2_source_integer(provider, "g_infinite_ammo", &infinite, error) ||
            !application_native_q2_source_integer(provider, "g_instant_weapon_switch", &instant, error) ||
            !application_native_q2_source_integer(provider, "g_quick_weapon_switch", &quick, error) ||
            !application_native_q2_source_integer(provider, "g_dm_no_stack_double", &no_stack, error)) return false;
        input->infinite_ammo = infinite != 0;
        input->instant_switch = instant != 0;
        input->quick_switch = quick != 0;
        input->no_stack_double = no_stack != 0;
    }
    return true;
}
