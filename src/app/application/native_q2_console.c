#include "native_q2_console.h"
#include "guest_native_q2_original_save.h"
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
    qa_cvar_save_policy save_policy;
} q2_source_cvar;

/* The Source settings owner declares this ENGINE value in both Q2 dialects.
 * Rerelease GAME may separately register it through its actual imports. */
static const q2_source_cvar engine_cvars[] = {
    {"sv_airaccelerate", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"sv_noreload", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
};

bool application_native_q2_engine_cvars(qa_cvars *cvars, uint64_t owner, qa_error *error) {
    if (!cvars || !owner || (qa_cvars_dialect(cvars) != QA_RULESET_Q2_CLASSIC &&
        qa_cvars_dialect(cvars) != QA_RULESET_Q2_RERELEASE))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 ENGINE declarations require their physical Source registry");
    for (size_t i = 0; i < sizeof(engine_cvars) / sizeof(*engine_cvars); ++i)
        if (!qa_cvars_register(cvars, engine_cvars[i].name, engine_cvars[i].value,
            engine_cvars[i].flags, owner, NULL, error) ||
            !qa_cvars_declare_save_policy(cvars, engine_cvars[i].name,
                engine_cvars[i].save_policy, error)) return false;
    return true;
}

/* Original InitGame and the TypeScript Q2 source settings owner. Q2 flags
 * retain their source word; GAME belongs only to rerelease GAME registrations. */
static const q2_source_cvar common[] = {
    {"gun_x", "0", 0, QA_CVAR_SAVE_SETTING}, {"gun_y", "0", 0, QA_CVAR_SAVE_SETTING}, {"gun_z", "0", 0, QA_CVAR_SAVE_SETTING},
    {"sv_rollspeed", "200", 0, QA_CVAR_SAVE_SETTING}, {"sv_rollangle", "2", 0, QA_CVAR_SAVE_SETTING},
    {"sv_maxvelocity", "2000", 0, QA_CVAR_SAVE_GAMEPLAY}, {"sv_gravity", "800", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"dedicated", "0", QA_Q2_CVAR_NOSET, QA_CVAR_SAVE_SETTING},
    {"cheats", "0", (uint32_t)QA_CVAR_SERVERINFO | (uint32_t)QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_SETTING},
    {"maxclients", "4", (uint32_t)QA_CVAR_SERVERINFO | (uint32_t)QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"maxspectators", "4", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_SETTING},
    {"deathmatch", "0", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY}, {"coop", "0", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"skill", "1", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY}, {"maxentities", "1024", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"dmflags", "0", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_GAMEPLAY}, {"fraglimit", "0", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_GAMEPLAY},
    {"timelimit", "0", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_GAMEPLAY}, {"password", "", QA_CVAR_USERINFO, QA_CVAR_SAVE_SETTING},
    {"spectator_password", "", QA_CVAR_USERINFO, QA_CVAR_SAVE_SETTING}, {"needpass", "0", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_SETTING},
    {"filterban", "1", 0, QA_CVAR_SAVE_SETTING}, {"g_select_empty", "0", QA_CVAR_ARCHIVE, QA_CVAR_SAVE_SETTING},
    {"run_pitch", "0.002", 0, QA_CVAR_SAVE_SETTING}, {"run_roll", "0.005", 0, QA_CVAR_SAVE_SETTING},
    {"bob_up", "0.005", 0, QA_CVAR_SAVE_SETTING}, {"bob_pitch", "0.002", 0, QA_CVAR_SAVE_SETTING}, {"bob_roll", "0.002", 0, QA_CVAR_SAVE_SETTING},
    {"flood_msgs", "4", 0, QA_CVAR_SAVE_SETTING}, {"flood_persecond", "4", 0, QA_CVAR_SAVE_SETTING}, {"flood_waitdelay", "10", 0, QA_CVAR_SAVE_SETTING},
};
static const q2_source_cvar rerelease[] = {
    {"sv_stopspeed", "100", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"teamplay", "0", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY}, {"huntercam", "1", (uint32_t)QA_CVAR_SERVERINFO | (uint32_t)QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"g_coop_player_collision", "0", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"g_coop_squad_respawn", "1", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"g_coop_enable_lives", "0", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"g_coop_num_lives", "2", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"g_coop_instanced_items", "1", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"capturelimit", "0", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_GAMEPLAY},
    {"g_quick_weapon_switch", "1", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"g_instant_weapon_switch", "0", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"g_infinite_ammo", "0", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"g_weapon_respawn_time", "30", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_dm_weapons_stay", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_dm_instant_items", "1", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_dm_same_level", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_no_health", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_no_items", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_no_armor", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_friendly_fire", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_instagib", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_damage_scale", "1", 0, QA_CVAR_SAVE_GAMEPLAY}, {"ai_damage_scale", "1", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_teamplay_armor_protect", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_no_mines", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_no_nukes", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_no_spheres", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_dm_random_items", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_dm_no_quadfire_drop", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_dm_no_quad_drop", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_dm_no_stack_double", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_dm_strong_mines", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_dm_force_respawn", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_dm_force_respawn_time", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_dm_no_fall_damage", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_dm_spawn_farthest", "1", 0, QA_CVAR_SAVE_GAMEPLAY}, {"g_dm_allow_exit", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_start_items", "", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY}, {"g_map_list", "", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"g_map_list_shuffle", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
};
static const q2_source_cvar rogue[] = {
    {"sv_stopspeed", "100", 0, QA_CVAR_SAVE_GAMEPLAY}, {"huntercam", "1", (uint32_t)QA_CVAR_SERVERINFO | (uint32_t)QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
    {"strong_mines", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"randomrespawn", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"gamerules", "0", QA_Q2_CVAR_LATCH, QA_CVAR_SAVE_GAMEPLAY},
};
static const q2_source_cvar lmctf[] = {
    {"ctfflags", "0", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_GAMEPLAY}, {"refset", "0", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_GAMEPLAY},
    {"runes", "15", QA_CVAR_SERVERINFO, QA_CVAR_SAVE_GAMEPLAY}, {"countdown_time", "15", 0, QA_CVAR_SAVE_GAMEPLAY}, {"autolock", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
    {"fastswitch", "0", 0, QA_CVAR_SAVE_GAMEPLAY}, {"disabled_weps", "0", 0, QA_CVAR_SAVE_GAMEPLAY},
};

struct application_native_q2_console {
    application_provider *provider;
    qa_console *console;
    qa_cvars *cvars;
    qa_hud_cvar_handles hud_cvars;
    struct {
        qa_cvar_handle infinite_ammo, instant_switch, quick_switch, no_stack_double;
    } weapon_cvars;
    qa_cvar_handle combat_cvars[APPLICATION_Q2_COMBAT_SETTING_COUNT];
    qa_cvar_handle cheats, password, spectator_password;
    application_q2_source_scripts scripts;
    size_t calls;
    qa_cvar_observer_token observers[sizeof(engine_cvars) / sizeof(*engine_cvars) + sizeof(common) / sizeof(*common) + sizeof(rerelease) / sizeof(*rerelease) + sizeof(rogue) / sizeof(*rogue) + sizeof(lmctf) / sizeof(*lmctf) + 2];
    size_t observer_count;
};

static void source_cvars_bind(struct application_native_q2_console *owner) {
    owner->cheats = qa_cvars_resolve(owner->cvars, "cheats");
    owner->password = qa_cvars_resolve(owner->cvars, "password");
    owner->spectator_password = qa_cvars_resolve(owner->cvars, "spectator_password");
    owner->weapon_cvars.infinite_ammo = qa_cvars_resolve(owner->cvars, "g_infinite_ammo");
    owner->weapon_cvars.instant_switch = qa_cvars_resolve(owner->cvars, "g_instant_weapon_switch");
    owner->weapon_cvars.quick_switch = qa_cvars_resolve(owner->cvars, "g_quick_weapon_switch");
    owner->weapon_cvars.no_stack_double = qa_cvars_resolve(owner->cvars, "g_dm_no_stack_double");
    static const char *const combat_names[APPLICATION_Q2_COMBAT_SETTING_COUNT] = {
        "g_instagib", "teamplay", "g_damage_scale", "ai_damage_scale", "g_teamplay_armor_protect"
    };
    for (size_t i = 0; i < APPLICATION_Q2_COMBAT_SETTING_COUNT; ++i)
        owner->combat_cvars[i] = qa_cvars_resolve(owner->cvars, combat_names[i]);
}

static qa_ruleset_id dialect(const application_provider *provider) {
    return provider->launch->selection.clock.kind == QA_RULESET_Q2_RERELEASE ?
        QA_RULESET_Q2_RERELEASE : QA_RULESET_Q2_CLASSIC;
}
static bool classic_rogue(const application_provider *provider) {
    return dialect(provider) == QA_RULESET_Q2_CLASSIC && !strcmp(provider->product->campaign, "rogue");
}
qa_cvars *application_native_q2_console_registry(const application_provider *provider) {
    return provider && provider->kind == APPLICATION_PROVIDER_Q2 && provider->native_q2_console ?
        provider->native_q2_console->cvars : NULL;
}
const qa_hud_cvar_handles *application_native_q2_console_hud_controls(const application_provider *provider) {
    return application_native_q2_console_registry(provider) ? &provider->native_q2_console->hud_cvars : NULL;
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
static qa_command_result command(void *opaque, const qa_command_invocation *invocation,
                                  qa_error *error) {
    struct application_native_q2_console *owner = opaque;
    ++owner->calls;
    qa_command_result result = application_startup_common_command(owner->provider,
        owner->console, owner->cvars, invocation, error);
    if (result == QA_COMMAND_UNHANDLED)
        result = application_command_fallback(owner->provider->application, invocation, error);
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
        application_startup_source_script_release(owner->provider, owner->cvars, lease);
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
    const qa_cvar_view *value = qa_cvars_read(owner->cvars, owner->cheats);
    return value && value->integer != 0;
}
static bool allow_command(void *opaque, const qa_command_invocation *invocation) {
    struct application_native_q2_console *owner = opaque;
    return application_startup_command_allowed(owner->provider, invocation);
}
typedef struct q2_operator_player {
    qa_q2_player_info info;
    char *userinfo;
} q2_operator_player;
static bool operator_command(void *opaque, const qa_command_invocation *invocation, qa_error *error) {
    struct application_native_q2_console *owner = opaque;
    application_provider *provider = owner->provider;
    qa_application *app = provider->application;
    if (invocation->console!=owner->console || invocation->receiver!=provider->owner ||
        invocation->registration_owner!=provider->owner ||
        !qa_console_invocation_current(owner->console,invocation) ||
        !qa_application_command_context_active(app,&invocation->context) || !provider->state.q2)
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
        const qa_cvar_view *password = qa_cvars_read(registry, owner->password);
        const qa_cvar_view *spectator = qa_cvars_read(registry, owner->spectator_password);
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
    qa_cvar_options variables = {.dialect = dialect(provider),
        .side = QA_CVAR_SIDE_SERVER, .role = QA_CVAR_ROLE_GAME, .user = owner,
        .print = cvar_print, .cheats_allowed = cheats};
    owner->cvars = qa_cvars_create_view(provider->application->cvars, &variables, error);
    qa_console_options options = {.context = {.owner = provider->owner, .dialect = dialect(provider),
        .origin = QA_COMMAND_SERVER}, .cvars = owner->cvars, .user = owner, .print = print,
        .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = script_complete,
        .allow_command = allow_command, .source_command = command};
    options.context.cvar_view = qa_cvars_view_identity(owner->cvars);
    if (owner->cvars && qa_console_bind_source(provider->application->console, &options, error))
        owner->console = provider->application->console;
    if (!owner->console) {
        qa_console_unbind_source(owner->console, qa_cvars_view_identity(owner->cvars), error);
        qa_cvars_detach_callbacks(owner->cvars); qa_cvars_destroy(owner->cvars);
        free(owner);
        return false;
    }
    provider->native_q2_console = owner;
    qa_application *application = provider->application;
    application_provider *prior = application->startup_preinit_provider;
    if (application->operation == APPLICATION_PERSISTING)
        application->startup_preinit_provider = provider;
    bool registered = qa_console_register_context(owner->console, &options.context, "status", "Print Q2 source map and connected players",
            provider->owner, provider->owner, true, operator_command, owner, error) &&
        qa_console_register_context(owner->console, &options.context, "dumpuser", "Print a Q2 source player's userinfo",
            provider->owner, provider->owner, true, operator_command, owner, error);
    application->startup_preinit_provider = prior;
    if (!registered) {
        application_native_q2_console_destroy(provider, NULL);
        return false;
    }
    source_cvars_bind(owner);
    return true;
}
static bool observe_name(struct application_native_q2_console *owner, const char *name, qa_error *error) {
    const qa_cvar_view *variable = qa_cvars_find(owner->cvars, name);
    if (!variable || !variable->declared)
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
    if (dialect(owner->provider) == QA_RULESET_Q2_RERELEASE) {
        for (size_t i = 0; i < sizeof(rerelease) / sizeof(*rerelease); ++i)
            if (!observe_name(owner, rerelease[i].name, error)) return false;
    } else {
        if (!observe_name(owner, "sv_maplist", error)) return false;
        if (classic_rogue(owner->provider))
            for (size_t i = 0; i < sizeof(rogue) / sizeof(*rogue); ++i)
                if (!observe_name(owner, rogue[i].name, error)) return false;
    }
    const qa_cvar_view *ctf = qa_cvars_find(owner->cvars, "ctfflags");
    if (ctf && ctf->declared)
        for (size_t i = 0; i < sizeof(lmctf) / sizeof(*lmctf); ++i)
            if (!observe_name(owner, lmctf[i].name, error)) return false;
    const qa_cvar_view *goal = qa_cvars_find(owner->cvars, "goallimit");
    if (goal && goal->declared && !observe_name(owner, "goallimit", error)) return false;
    return true;
}
static bool definitions(application_provider *provider, const q2_source_cvar *table,
                         size_t count, qa_error *error) {
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    uint32_t game = dialect(provider) == QA_RULESET_Q2_RERELEASE ? QA_Q2_CVAR_GAME : 0;
    for (size_t i = 0; i < count; ++i) {
        const char *value = table[i].value;
        uint32_t flags = table[i].flags | game;
        if (game && !strcmp(table[i].name, "maxclients")) value = "8";
        if (game && !strcmp(table[i].name, "maxentities")) value = "8192";
        if (!game && !strcmp(provider->product->campaign, "xatrix") &&
            !strcmp(table[i].name, "spectator_password")) flags = 0;
        if (!qa_cvars_register(cvars, table[i].name, value, flags,
                provider->owner, NULL, error) ||
            !qa_cvars_declare_save_policy(cvars, table[i].name, table[i].save_policy, error)) return false;
    }
    return true;
}
bool application_native_q2_console_prepare(application_provider *provider, const qa_q2_options *rules,
                                          const qa_launch_choices *choices, qa_error *error) {
    if (!rules || !choices || !application_native_q2_console_create_restored(provider, error)) return false;
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    bool okay = application_startup_seed_source(provider, cvars, error);
    if (okay) okay = definitions(provider, common, sizeof(common) / sizeof(*common), error);
    if (okay) okay = application_native_q2_engine_cvars(cvars, provider->owner, error);
    if (okay) okay = qa_server_admin_declarations(cvars,provider->owner,error);
    bool rr = dialect(provider) == QA_RULESET_Q2_RERELEASE;
    if (okay) okay = rr ? definitions(provider, rerelease, sizeof(rerelease) / sizeof(*rerelease), error) :
        (qa_cvars_register(cvars, "sv_maplist", "", 0, provider->owner, NULL, error) &&
         qa_cvars_declare_save_policy(cvars, "sv_maplist", QA_CVAR_SAVE_GAMEPLAY, error));
    if (okay && classic_rogue(provider))
        okay = definitions(provider, rogue, sizeof(rogue) / sizeof(*rogue), error);
    for (size_t i = 0; okay && i < choices->mode_count; ++i) {
        const qa_launch_mode *mode = &choices->modes[i];
        if (strcmp(provider->launch->selection.instance, mode->instance)) continue;
        if (mode->rules.source == QA_MODE_LMCTF)
            okay = definitions(provider, lmctf, sizeof(lmctf) / sizeof(*lmctf), error);
        if (mode->rules.source == QA_MODE_Q2_DEATHBALL)
            okay = qa_cvars_register(cvars, "goallimit", "0", 0, provider->owner, NULL, error);
    }
    if (okay) {
        qa_hud_cvars_bind(cvars, QA_HUD_CVAR_USE_FONT, &provider->native_q2_console->hud_cvars);
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
    bool okay = application_q2_original_prepare(provider,error);
    if (okay) okay = application_startup_apply_latched(provider, cvars, error);
    qa_cvar_view prepared_capacity;
    float capacity = 0;
    if (okay) okay = qa_cvars_effective_view(cvars, "maxclients", &prepared_capacity, error);
    if (okay) capacity = prepared_capacity.number;
    if (okay) {
        rules->deathmatch = qa_cvars_find(cvars, "deathmatch")->number != 0;
        rules->cooperative = qa_cvars_find(cvars, "coop")->number != 0;
        if (capacity <= 1) capacity = rules->deathmatch ? 8 : rules->cooperative ? 4 : 1;
        if (capacity < (float)choices->seat_count) capacity = (float)choices->seat_count;
        if (!isfinite(capacity) || capacity < 1 || capacity > 64)
            okay = application_fail(error, QA_ERROR_FORMAT, "Q2 source capacity must be between 1 and 64");
        if (okay) okay = qa_cvars_set_number(cvars, "maxclients", truncf(capacity), error);
        if (okay) okay = application_publication_source_capacity(provider, (uint32_t)truncf(capacity), error);
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
        if (!qa_console_unbind_source(owner->console, qa_cvars_view_identity(owner->cvars), error)) return false;
        qa_cvars_remove_owner(owner->cvars, provider->owner);
        qa_cvars_detach_callbacks(owner->cvars); qa_cvars_destroy(owner->cvars);
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
        .cvar_view = qa_cvars_view_identity(owner->cvars),
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
    if (okay) {
        qa_hud_cvars_bind(cvars, QA_HUD_CVAR_USE_FONT, &provider->native_q2_console->hud_cvars);
        source_cvars_bind(provider->native_q2_console);
    }
    return okay && observe(provider->native_q2_console, error);
}
bool application_native_q2_rotation_changed(void *context, const qa_string_id *maps, size_t count, qa_error *error) {
    application_provider *provider = context;
    qa_console *console = NULL; qa_cvars *cvars = NULL; qa_command_context raw, command;
    if (!provider || !maps || !count || !provider->state.q2 ||
        !application_native_q2_console_at(provider, &console, &cvars, &raw) ||
        qa_cvars_dialect(cvars) != QA_RULESET_Q2_RERELEASE || !qa_cvars_find(cvars, "g_map_list") ||
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
    if (!cvars || !provider->state.q2)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 settings have no constructed source owner");
    if (!qa_q2_source_apply(provider->state.q2, cvars, false, error)) return false;
    qa_q2_source_bind(provider->state.q2, cvars);
    return true;
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
    rules->q2_rerelease = dialect(provider) == QA_RULESET_Q2_RERELEASE;
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
static bool source_integer(const qa_cvar_view *value, int32_t *out, qa_error *error) {
    if (!value || !out)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q2 setting has no source integer");
    *out = value->integer;
    return true;
}
bool application_native_q2_source_integer(const application_provider *provider, const char *name,
                                          int32_t *out, qa_error *error) {
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    const qa_cvar_view *value = cvars && name ? qa_cvars_find(cvars, name) : NULL;
    return source_integer(value, out, error);
}
bool application_native_q2_combat_integer(const application_provider *provider,
                                         application_q2_combat_setting setting,
                                         int32_t *out, qa_error *error) {
    qa_cvars *cvars = application_native_q2_console_registry(provider);
    const qa_cvar_view *value = cvars ?
        qa_cvars_read(cvars, provider->native_q2_console->combat_cvars[setting]) : NULL;
    return source_integer(value, out, error);
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
    if (dialect(provider) == QA_RULESET_Q2_RERELEASE) {
        struct application_native_q2_console *owner = provider->native_q2_console;
        input->infinite_ammo = qa_cvars_read(owner->cvars, owner->weapon_cvars.infinite_ammo)->integer != 0;
        input->instant_switch = qa_cvars_read(owner->cvars, owner->weapon_cvars.instant_switch)->integer != 0;
        input->quick_switch = qa_cvars_read(owner->cvars, owner->weapon_cvars.quick_switch)->integer != 0;
        input->no_stack_double = qa_cvars_read(owner->cvars, owner->weapon_cvars.no_stack_double)->integer != 0;
    }
    return true;
}
