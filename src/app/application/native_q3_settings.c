#include "native_q3_settings.h"
#include "native_q3_console.h"
#include "startup_flow.h"
#include "qa/source_save.h"
#include "qa/game_q3_source.h"
#include "qa/console_cvars_prepare.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct setting_definition {
    application_native_q3_setting setting;
    const char *name, *value;
    uint32_t flags;
    bool track, team_shader;
    qa_cvar_save_policy save_policy;
} setting_definition;

#define A QA_CVAR_ARCHIVE
#define S QA_CVAR_SERVERINFO
#define U QA_CVAR_USERINFO
#define L QA_CVAR_LATCH
#define R QA_CVAR_READONLY
#define N QA_CVAR_NO_RESTART
#define Y QA_CVAR_SYSTEMINFO
#define CV(setting, name, value, flags, track, shader, policy) {setting, name, value, flags, track, shader, policy}
/* Q3GameSettings definitions preserve the source registration/update order. */
static const setting_definition common_settings[] = {
    CV(APPLICATION_Q3_SETTING_SV_CHEATS, "sv_cheats", "", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_RESTARTED, "g_restarted", "0", R, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_GAMETYPE, "g_gametype", "0", S | U | L, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_SV_MAXCLIENTS, "sv_maxclients", "8", S | L | A, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_MAX_GAME_CLIENTS, "g_maxGameClients", "0", S | L | A, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_DMFLAGS, "dmflags", "0", S | A, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_FRAGLIMIT, "fraglimit", "20", S | A | N, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_TIMELIMIT, "timelimit", "0", S | A | N, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_CAPTURELIMIT, "capturelimit", "8", S | A | N, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_SYNCHRONOUS_CLIENTS, "g_synchronousClients", "0", Y, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_FRIENDLY_FIRE, "g_friendlyFire", "0", A, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_TEAM_AUTO_JOIN, "g_teamAutoJoin", "0", A, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_TEAM_FORCE_BALANCE, "g_teamForceBalance", "0", A, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_WARMUP, "g_warmup", "20", A, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_DO_WARMUP, "g_doWarmup", "0", 0, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_LOG, "g_log", "games.log", A, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_LOG_SYNC, "g_logSync", "0", A, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_PASSWORD, "g_password", "", U, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_BAN_IPS, "g_banIPs", "", A, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_FILTER_BAN, "g_filterBan", "1", A, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_NEEDPASS, "g_needpass", "0", S | R, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_DEDICATED, "dedicated", "0", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_SPEED, "g_speed", "320", 0, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_GRAVITY, "g_gravity", "800", 0, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_KNOCKBACK, "g_knockback", "1000", 0, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_QUADFACTOR, "g_quadfactor", "3", 0, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_WEAPONRESPAWN, "g_weaponrespawn", "5", 0, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_WEAPON_TEAM_RESPAWN, "g_weaponTeamRespawn", "30", 0, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_FORCERESPAWN, "g_forcerespawn", "20", 0, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_INACTIVITY, "g_inactivity", "0", 0, true, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_DEBUG_MOVE, "g_debugMove", "0", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_DEBUG_DAMAGE, "g_debugDamage", "0", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_DEBUG_ALLOC, "g_debugAlloc", "0", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_MOTD, "g_motd", "", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_COM_BLOOD, "com_blood", "1", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_PODIUM_DIST, "g_podiumDist", "80", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_PODIUM_DROP, "g_podiumDrop", "70", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_ALLOW_VOTE, "g_allowVote", "1", A, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_LIST_ENTITY, "g_listEntity", "0", 0, false, false, QA_CVAR_SAVE_SETTING)
};
static const setting_definition missionpack_settings[] = {
    CV(APPLICATION_Q3_SETTING_G_OBELISK_HEALTH, "g_obeliskHealth", "2500", 0, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_OBELISK_REGEN_PERIOD, "g_obeliskRegenPeriod", "1", 0, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_OBELISK_REGEN_AMOUNT, "g_obeliskRegenAmount", "15", 0, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_OBELISK_RESPAWN_DELAY, "g_obeliskRespawnDelay", "10", S, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_CUBE_TIMEOUT, "g_cubeTimeout", "30", 0, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_REDTEAM, "g_redteam", "Stroggs", A | S | U, true, true, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_BLUETEAM, "g_blueteam", "Pagans", A | S | U, true, true, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_UI_SINGLE_PLAYER_ACTIVE, "ui_singlePlayerActive", "", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_ENABLE_DUST, "g_enableDust", "0", S, true, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_ENABLE_BREATH, "g_enableBreath", "0", S, true, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_G_PROX_MINE_TIMEOUT, "g_proxMineTimeout", "20000", 0, false, false, QA_CVAR_SAVE_GAMEPLAY)
};
static const setting_definition final_settings[] = {
    CV(APPLICATION_Q3_SETTING_G_SMOOTH_CLIENTS, "g_smoothClients", "1", 0, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_PMOVE_FIXED, "pmove_fixed", "0", Y, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_PMOVE_MSEC, "pmove_msec", "8", Y, false, false, QA_CVAR_SAVE_GAMEPLAY),
    CV(APPLICATION_Q3_SETTING_G_RANKINGS, "g_rankings", "0", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_SV_ENABLE_RANKINGS, "sv_enableRankings", "0", 0, false, false, QA_CVAR_SAVE_SETTING),
    CV(APPLICATION_Q3_SETTING_SV_RANKINGS_ACTIVE, "sv_rankingsActive", "0", R, false, false, QA_CVAR_SAVE_SETTING)
};
#undef CV
#undef A
#undef S
#undef U
#undef L
#undef R
#undef N
#undef Y

enum {
    COMMON_COUNT = sizeof(common_settings) / sizeof(common_settings[0]),
    MISSIONPACK_COUNT = sizeof(missionpack_settings) / sizeof(missionpack_settings[0]),
    FINAL_COUNT = sizeof(final_settings) / sizeof(final_settings[0]),
    SETTINGS_CAPACITY = COMMON_COUNT + MISSIONPACK_COUNT + FINAL_COUNT
};

typedef enum settings_operation {
    SETTINGS_IDLE, SETTINGS_REGISTERING, SETTINGS_UPDATING, SETTINGS_EFFECT,
    SETTINGS_CAPTURING, SETTINGS_IMPORTING
} settings_operation;

typedef struct setting_reference {
    qa_cvars *registry;
    qa_cvar_handle handle;
} setting_reference;

struct application_native_q3_settings {
    application_provider *provider;
    application_native_q3_settings_options options;
    application_native_q3_cvar_snapshot snapshots[SETTINGS_CAPACITY];
    setting_reference references[SETTINGS_CAPACITY];
    size_t indices[APPLICATION_Q3_SETTING_COUNT];
    qa_q3_product product;
    settings_operation operation;
    uint64_t password_modification_count;
    bool initialized, password_seen;
};

static size_t definition_count(const struct application_native_q3_settings *owner)
{
    return COMMON_COUNT + FINAL_COUNT + (owner->product == QA_Q3_TEAM_ARENA ? MISSIONPACK_COUNT : 0);
}

static const setting_definition *definition_at(const struct application_native_q3_settings *owner,
    size_t index)
{
    if (index < COMMON_COUNT) return &common_settings[index];
    index -= COMMON_COUNT;
    if (owner->product == QA_Q3_TEAM_ARENA) {
        if (index < MISSIONPACK_COUNT) return &missionpack_settings[index];
        index -= MISSIONPACK_COUNT;
    }
    return index < FINAL_COUNT ? &final_settings[index] : NULL;
}

static bool prepare_table(application_provider *provider, const setting_definition *table,
    size_t count, qa_error *error)
{
    for (size_t i = 0; i < count; ++i) {
        const setting_definition *definition = table + i;
        qa_cvars *registry = application_native_q3_cvar_owner(provider, definition->name);
        uint64_t owner = !strcmp(definition->name, "sv_cheats") ? 0 : provider->owner;
        bool ok = registry == qa_application_cvars(provider->application)
            ? application_startup_root_register(provider, definition->name, definition->value,
                definition->flags, owner, definition->save_policy, error)
            : qa_cvars_register(registry, definition->name, definition->value, definition->flags,
                owner, NULL, error) &&
                qa_cvars_declare_save_policy(registry, definition->name, definition->save_policy, error);
        if (!ok) return false;
    }
    return true;
}

bool application_native_q3_settings_prepare_definitions(application_provider *provider,
    qa_q3_product product, qa_error *error)
{
    if (!provider || !provider->product || provider->state.q3 || provider->attached ||
        !application_native_q3_console_registry(provider) ||
        (product != QA_Q3_ARENA && product != QA_Q3_TEAM_ARENA) ||
        (product == QA_Q3_TEAM_ARENA) != !strcmp(provider->product->campaign, "missionpack"))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 configuration definitions need their actual uninitialized source");
    return prepare_table(provider, common_settings, COMMON_COUNT, error) &&
        (product != QA_Q3_TEAM_ARENA || prepare_table(provider, missionpack_settings, MISSIONPACK_COUNT, error)) &&
        prepare_table(provider, final_settings, FINAL_COUNT, error);
}

static bool ascii_equal(const char *a, const char *b)
{
    if (!a || !b) return false;
    for (; *a && *b; ++a, ++b) {
        unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return *a == *b;
}

qa_cvar_save_policy application_native_q3_cvar_save_policy(const char *name)
{
    const setting_definition *tables[] = {common_settings, missionpack_settings, final_settings};
    const size_t counts[] = {COMMON_COUNT, MISSIONPACK_COUNT, FINAL_COUNT};
    for (size_t table = 0; table < sizeof(tables) / sizeof(*tables); ++table)
        for (size_t row = 0; row < counts[table]; ++row)
            if (ascii_equal(name, tables[table][row].name)) return tables[table][row].save_policy;
    return QA_CVAR_SAVE_UNCLASSIFIED;
}

static void snapshot_dispose(application_native_q3_cvar_snapshot *snapshot)
{
    free((void *)snapshot->name);
    free((void *)snapshot->value);
    free((void *)snapshot->reset_value);
    free((void *)snapshot->latched_value);
    *snapshot = (application_native_q3_cvar_snapshot){0};
}

static char *copy_text(const char *text, qa_error *error)
{
    size_t size = strlen(text);
    if (size == SIZE_MAX) {
        application_fail(error, QA_ERROR_MEMORY, "native Q3 cached cvar text is too large");
        return NULL;
    }
    char *copy = malloc(size + 1);
    if (!copy) {
        application_fail(error, QA_ERROR_MEMORY, "copying native Q3 cached cvar text");
        return NULL;
    }
    memcpy(copy, text, size + 1);
    return copy;
}

static bool snapshot_copy(application_native_q3_cvar_snapshot *out, const qa_cvar_view *source,
    qa_error *error)
{
    application_native_q3_cvar_snapshot copy = {
        .flags = source->flags, .modification_count = source->modification_count,
        .numeric_value = source->number, .integer_value = source->integer,
        .modified = source->modified
    };
    copy.name = copy_text(source->name, error);
    if (copy.name) copy.value = copy_text(source->value, error);
    if (copy.value) copy.reset_value = copy_text(source->reset_value, error);
    if (copy.reset_value && source->latched_value)
        copy.latched_value = copy_text(source->latched_value, error);
    if (!copy.name || !copy.value || !copy.reset_value ||
        (source->latched_value && !copy.latched_value)) {
        snapshot_dispose(&copy);
        return false;
    }
    *out = copy;
    return true;
}

static bool snapshot_matches(const application_native_q3_cvar_snapshot *snapshot,
    const qa_cvar_view *source)
{
    double number = source->number;
    return snapshot->flags == source->flags && snapshot->modified == source->modified &&
        snapshot->modification_count == source->modification_count &&
        snapshot->integer_value == source->integer &&
        !memcmp(&snapshot->numeric_value, &number, sizeof(number)) &&
        !strcmp(snapshot->name, source->name) && !strcmp(snapshot->value, source->value) &&
        !strcmp(snapshot->reset_value, source->reset_value) &&
        ((snapshot->latched_value == NULL && source->latched_value == NULL) ||
         (snapshot->latched_value && source->latched_value &&
          !strcmp(snapshot->latched_value, source->latched_value)));
}

static bool owner_live(const struct application_native_q3_settings *owner)
{
    const application_provider *provider = owner->provider;
    return provider->native_q3_settings == owner && provider->constructed && provider->attached &&
        !provider->close_pending && !provider->application->destroy_requested;
}

static struct application_native_q3_settings *owner_at(const application_provider *provider)
{
    return provider && provider->kind == APPLICATION_PROVIDER_Q3 ? provider->native_q3_settings : NULL;
}

bool application_native_q3_settings_create(application_provider *provider, qa_q3_product product,
    const application_native_q3_settings_options *options, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->owner ||
        !provider->application || !provider->product || provider->product->family != QA_GAME_Q3 ||
        !application_native_q3_console_registry(provider) || provider->native_q3_settings ||
        provider->close_pending || provider->application->destroy_requested ||
        (product != QA_Q3_ARENA && product != QA_Q3_TEAM_ARENA) ||
        (product == QA_Q3_TEAM_ARENA) != !strcmp(provider->product->campaign, "missionpack"))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 settings require their actual source product");
    struct application_native_q3_settings *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "allocating native Q3 cached settings");
    owner->provider = provider;
    owner->product = product;
    for (size_t i = 0; i < APPLICATION_Q3_SETTING_COUNT; ++i) owner->indices[i] = SIZE_MAX;
    for (size_t i = 0; i < definition_count(owner); ++i)
        owner->indices[definition_at(owner, i)->setting] = i;
    if (options) owner->options = *options;
    provider->native_q3_settings = owner;
    return true;
}

bool application_native_q3_settings_idle(const application_provider *provider)
{
    const struct application_native_q3_settings *owner = owner_at(provider);
    return !owner || owner->operation == SETTINGS_IDLE;
}

bool application_native_q3_settings_initialized(const application_provider *provider)
{
    const struct application_native_q3_settings *owner = owner_at(provider);
    return owner && owner->initialized;
}

bool application_native_q3_settings_destroy(application_provider *provider, qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    if (!provider || !application_native_q3_settings_idle(provider) ||
        !application_native_q3_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 cached settings are borrowed");
    if (owner) {
        for (size_t i = 0; i < definition_count(owner); ++i) snapshot_dispose(&owner->snapshots[i]);
        free(owner);
        provider->native_q3_settings = NULL;
    }
    return true;
}

static bool register_cache(application_provider *provider, const char *build_date, bool reset,
    qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    if (!owner || !build_date || owner->operation != SETTINGS_IDLE ||
        (!reset && owner->initialized))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 cache registration has no empty source owner");
    if (!application_native_q3_console_borrow(provider, error)) return false;
    owner->operation = SETTINGS_REGISTERING;
    application_native_q3_cvar_snapshot copied[SETTINGS_CAPACITY] = {{0}};
    size_t count = definition_count(owner);
    if (reset) {
        owner->initialized = false;
        owner->password_seen = false;
        owner->password_modification_count = 0;
        for (size_t i = 0; i < count; ++i) snapshot_dispose(&owner->snapshots[i]);
    }
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        const setting_definition *definition = definition_at(owner, i);
        qa_cvars *registry = application_native_q3_cvar_owner(provider, definition->name);
        if (!strcmp(definition->name, "g_restarted"))
            okay = qa_cvars_register(registry, "gamename", "baseq3", QA_CVAR_SERVERINFO | QA_CVAR_READONLY,
                provider->owner, NULL, error) &&
                qa_cvars_register(registry, "gamedate", build_date, QA_CVAR_READONLY,
                    provider->owner, NULL, error);
        if (okay) okay = qa_cvars_register(registry, definition->name, definition->value,
            definition->flags, !strcmp(definition->name, "sv_cheats") ? 0 : provider->owner, NULL, error) &&
            qa_cvars_declare_save_policy(registry, definition->name, definition->save_policy, error);
        if (okay && !owner_live(owner))
            okay = application_fail(error, QA_ERROR_ARGUMENT, "native Q3 settings source retired during registration");
        if (okay) owner->references[i] = (setting_reference){registry,
            qa_cvars_resolve(registry, definition->name)};
        const qa_cvar_view *current = okay ? qa_cvars_read(registry,
            owner->references[i].handle) : NULL;
        if (okay && !current)
            okay = application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 registered cvar disappeared");
        if (okay) okay = snapshot_copy(&copied[i], current, error);
    }
    if (okay) {
        for (size_t i = 0; i < count; ++i) {
            snapshot_dispose(&owner->snapshots[i]);
            owner->snapshots[i] = copied[i];
            copied[i] = (application_native_q3_cvar_snapshot){0};
        }
        owner->initialized = true;
    }
    for (size_t i = 0; i < count; ++i) snapshot_dispose(&copied[i]);
    owner->operation = SETTINGS_IDLE;
    application_native_q3_console_release(provider);
    return okay;
}

bool application_native_q3_settings_register_cache(application_provider *provider,
    const char *build_date, qa_error *error)
{
    return register_cache(provider, build_date, false, error);
}

bool application_native_q3_settings_reset_cache(application_provider *provider,
    const char *build_date, qa_error *error)
{
    return register_cache(provider, build_date, true, error);
}

bool application_native_q3_settings_snapshot_at(const application_provider *provider,
    application_native_q3_setting setting, const application_native_q3_cvar_snapshot **out,
    qa_error *error)
{
    const struct application_native_q3_settings *owner = owner_at(provider);
    if (!owner || !owner->initialized || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 cached settings are not initialized");
    size_t index = owner->indices[setting];
    if (index == SIZE_MAX)
        return application_fail(error, QA_ERROR_NOT_FOUND, "unregistered native Q3 cached setting");
    *out = &owner->snapshots[index];
    return true;
}

bool application_native_q3_settings_integer_at(const application_provider *provider,
    application_native_q3_setting setting, int32_t *out, qa_error *error)
{
    const application_native_q3_cvar_snapshot *snapshot = NULL;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 integer read requires its output");
    if (!application_native_q3_settings_snapshot_at(provider, setting, &snapshot, error)) return false;
    *out = snapshot->integer_value;
    return true;
}

bool application_native_q3_settings_number_at(const application_provider *provider,
    application_native_q3_setting setting, float *out, qa_error *error)
{
    const application_native_q3_cvar_snapshot *snapshot = NULL;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 number read requires its output");
    if (!application_native_q3_settings_snapshot_at(provider, setting, &snapshot, error)) return false;
    *out = (float)snapshot->numeric_value;
    return true;
}

bool application_native_q3_settings_string_at(const application_provider *provider,
    application_native_q3_setting setting, const char **out, qa_error *error)
{
    const application_native_q3_cvar_snapshot *snapshot = NULL;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 text read requires its output");
    if (!application_native_q3_settings_snapshot_at(provider, setting, &snapshot, error)) return false;
    *out = snapshot->value;
    return true;
}


static bool source_set(struct application_native_q3_settings *owner, const char *name,
    const char *value, qa_error *error)
{
    application_provider *provider = owner->provider;
    if (!application_native_q3_console_borrow(provider, error)) return false;
    bool own_operation = owner->operation == SETTINGS_IDLE;
    if (own_operation) owner->operation = SETTINGS_EFFECT;
    qa_cvars *registry = application_native_q3_cvar_owner(provider, name);
    bool okay = qa_cvars_set(registry, name, value, true, error);
    if (okay && !owner_live(owner))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "native Q3 settings source retired during force-set");
    if (own_operation) owner->operation = SETTINGS_IDLE;
    application_native_q3_console_release(provider);
    return okay;
}

bool application_native_q3_settings_source_set(application_provider *provider, const char *name,
    const char *value, qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    if (!owner || !owner->initialized || !name || !value)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 source set requires its initialized GAME owner");
    return source_set(owner, name, value, error);
}

bool application_native_q3_settings_force_set(application_provider *provider, application_native_q3_setting setting,
    const char *value, qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    const application_native_q3_cvar_snapshot *snapshot;
    if (!owner || !value)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 force-set requires its actual cached setting");
    if (!application_native_q3_settings_snapshot_at(provider, setting, &snapshot, error)) return false;
    (void)snapshot;
    return source_set(owner, definition_at(owner, owner->indices[setting])->name, value, error);
}

static bool remap_teams(struct application_native_q3_settings *owner, qa_error *error)
{
    if (owner->product != QA_Q3_TEAM_ARENA) return true;
    if (!owner->options.remap_teams)
        return application_fail(error, QA_ERROR_ARGUMENT, "missionpack settings have no actual source shader-remap owner");
    return owner->options.remap_teams(owner->options.context, error) &&
        (owner_live(owner) || application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 settings source retired during team remap"));
}

bool application_native_q3_settings_remap_teams(application_provider *provider, qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    if (!owner || !owner->initialized || owner->operation != SETTINGS_IDLE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 team remap has no idle initialized settings owner");
    if (!application_native_q3_console_borrow(provider, error)) return false;
    owner->operation = SETTINGS_EFFECT;
    bool okay = remap_teams(owner, error);
    owner->operation = SETTINGS_IDLE;
    application_native_q3_console_release(provider);
    return okay;
}

bool application_native_q3_settings_check_cvars(application_provider *provider, qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    const application_native_q3_cvar_snapshot *password;
    if (!owner || owner->operation != SETTINGS_IDLE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 CheckCvars requires its idle source module state");
    if (!application_native_q3_settings_snapshot_at(provider, APPLICATION_Q3_SETTING_G_PASSWORD, &password, error)) return false;
    if (owner->password_seen && owner->password_modification_count == password->modification_count)
        return true;
    if (!application_native_q3_console_borrow(provider, error)) return false;
    owner->operation = SETTINGS_EFFECT;
    bool needpass = password->value[0] && !ascii_equal(password->value, "none");
    owner->password_seen = true;
    owner->password_modification_count = password->modification_count;
    bool okay = application_native_q3_settings_force_set(provider, APPLICATION_Q3_SETTING_G_NEEDPASS, needpass ? "1" : "0", error);
    owner->operation = SETTINGS_IDLE;
    application_native_q3_console_release(provider);
    return okay;
}

bool application_native_q3_settings_update(application_provider *provider, qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    if (!owner || !owner->initialized || owner->operation != SETTINGS_IDLE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 settings update has no idle initialized source owner");
    if (!application_native_q3_console_borrow(provider, error)) return false;
    owner->operation = SETTINGS_UPDATING;
    bool okay = true, remapped = false;
    for (size_t i = 0; okay && i < definition_count(owner); ++i) {
        const setting_definition *definition = definition_at(owner, i);
        const setting_reference *reference = &owner->references[i];
        const qa_cvar_view *current = qa_cvars_read(reference->registry, reference->handle);
        if (!current) {
            okay = application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 cached cvar disappeared from its registry");
            break;
        }
        if (snapshot_matches(&owner->snapshots[i], current)) continue;
        application_native_q3_cvar_snapshot copied = {0};
        if (!snapshot_copy(&copied, current, error)) { okay = false; break; }
        bool changed = owner->snapshots[i].modification_count != copied.modification_count;
        snapshot_dispose(&owner->snapshots[i]);
        owner->snapshots[i] = copied;
        if (!changed) continue;
        if (definition->track) {
            size_t name_length = strlen(definition->name), value_length = strlen(copied.value);
            size_t fixed_length = sizeof("print \"Server:  changed to \n\"");
            if (name_length > 32000 - fixed_length ||
                value_length > 32000 - fixed_length - name_length || !owner->options.send_server_command) {
                okay = application_fail(error, QA_ERROR_FORMAT, "Q3 changed-cvar command exceeds its source formatter or lacks its actual sink");
                break;
            }
            size_t length = name_length + value_length + fixed_length;
            char *command = malloc(length);
            if (!command) { okay = application_fail(error, QA_ERROR_MEMORY, "formatting Q3 changed-cvar command"); break; }
            snprintf(command, length, "print \"Server: %s changed to %s\n\"", definition->name, copied.value);
            okay = owner->options.send_server_command(owner->options.context, -1, command, error);
            free(command);
            if (okay && !owner_live(owner))
                okay = application_fail(error, QA_ERROR_ARGUMENT, "native Q3 settings source retired during changed notification");
        }
        if (definition->team_shader) remapped = true;
    }
    if (okay && remapped) okay = remap_teams(owner, error);
    owner->operation = SETTINGS_IDLE;
    application_native_q3_console_release(provider);
    return okay;
}

static bool text_field(qa_source_save_io *io, const char **text, bool optional)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool present = !reading && *text != NULL;
    if (!qa_source_save_bool(io, &present) || (!optional && !present)) return false;
    if (!present) { if (reading) *text = NULL; return true; }
    size_t size = reading ? 0 : strlen(*text);
    size_t maximum = reading && io->offset <= io->input.size ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &size, maximum) || size == SIZE_MAX) return false;
    if (!reading) return qa_source_save_bytes(io, (void *)*text, size);
    char *owned = malloc(size + 1);
    if (!owned) return application_fail(io->error, QA_ERROR_MEMORY, "importing native Q3 cached cvar text");
    if (!qa_source_save_bytes(io, owned, size) || memchr(owned, 0, size)) { free(owned); return false; }
    owned[size] = 0;
    *text = owned;
    return true;
}

static bool gameplay_fields(qa_source_save_io *io, const char **name, const char **value, bool *pending)
{
    return text_field(io, name, false) && text_field(io, value, false) && qa_source_save_bool(io, pending);
}

static size_t gameplay_count(const struct application_native_q3_settings *owner)
{
    size_t count = 0;
    for (size_t i = 0; i < definition_count(owner); ++i)
        if (definition_at(owner, i)->save_policy == QA_CVAR_SAVE_GAMEPLAY) ++count;
    return count;
}

static bool cache_header(qa_source_save_io *io, const struct application_native_q3_settings *owner,
    size_t *count)
{
    uint8_t magic[4] = {'Q', 'A', 'G', 'C'};
    uint32_t product = owner->product;
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QAGC", 4) &&
        qa_source_save_u32(io, &product) && product == (uint32_t)owner->product &&
        qa_source_save_count(io, count, gameplay_count(owner));
}

/* Reuse the actual registry's numeric conversion and declaration metadata.
 * Aborting the prepared assignment leaves current values and callbacks alone. */
static bool snapshot_restore_value(application_native_q3_cvar_snapshot *out,
    qa_cvars *registry, const char *name, const char *value, bool pending, qa_error *error)
{
    const qa_cvar_view *current = qa_cvars_find(registry, name);
    if (!current || (!pending && strcmp(current->value, value)))
        return application_fail(error, QA_ERROR_FORMAT, "Q3 copied gameplay value disagrees with its current source registry");
    uint64_t modification = current->modification_count;
    bool modified = current->modified;
    qa_cvars_edit *edit = NULL;
    bool okay = qa_cvars_edit_prepare(registry, &edit, error) &&
        qa_cvars_edit_apply(edit, &(qa_cvars_edit_command){.kind = QA_CVARS_EDIT_ASSIGN,
            .name = name, .value = value, .source_dialect = QA_RULESET_Q3, .force = true}, error);
    const qa_cvar_view *copy = okay ? qa_cvars_edit_find(edit, name) : NULL;
    if (okay) okay = copy && snapshot_copy(out, copy, error);
    qa_cvars_edit_abort(edit);
    if (okay) {
        out->modification_count = pending ? modification - 1 : modification;
        out->modified = modified;
    }
    return okay;
}

bool application_native_q3_settings_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    if (!owner || !owner->initialized || !out || owner->operation != SETTINGS_IDLE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 cached settings are not at a continuation boundary");
    owner->operation = SETTINGS_CAPTURING;
    qa_source_save_io io = {0};
    qa_q3_source_match_state match;
    const application_native_q3_cvar_snapshot *warmup = NULL;
    bool warmup_observed = false;
    bool pending[SETTINGS_CAPACITY] = {0};
    size_t count = 0;
    bool okay = application_native_q3_settings_snapshot_at(provider, APPLICATION_Q3_SETTING_G_WARMUP, &warmup, error) &&
        qa_q3_source_match_state_read(provider->state.q3, &match, error);
    if (okay) warmup_observed = match.warmup_modification_count == warmup->modification_count;
    for (size_t i = 0; okay && i < definition_count(owner); ++i) {
        const setting_definition *definition = definition_at(owner, i);
        if (definition->save_policy != QA_CVAR_SAVE_GAMEPLAY) continue;
        const qa_cvar_view *current = qa_cvars_find(application_native_q3_cvar_owner(provider,
            definition->name), definition->name);
        if (!current) okay = application_fail(error, QA_ERROR_NOT_FOUND,
            "Q3 copied gameplay value lost its source registry");
        else if (owner->snapshots[i].modification_count != current->modification_count) {
            pending[i] = true;
            ++count;
        }
    }
    if (okay) okay = qa_source_save_writer(&io, NULL, error) && cache_header(&io, owner, &count) &&
        qa_source_save_bool(&io, &warmup_observed);
    for (size_t i = 0; okay && i < definition_count(owner); ++i)
        if (pending[i]) {
            application_native_q3_cvar_snapshot *snapshot = &owner->snapshots[i];
            okay = gameplay_fields(&io, &snapshot->name, &snapshot->value, &pending[i]);
        }
    if (okay) okay = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    owner->operation = SETTINGS_IDLE;
    return okay;
}

bool application_native_q3_settings_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    if (!owner || owner->initialized || owner->operation != SETTINGS_IDLE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 cached settings require an empty candidate owner");
    owner->operation = SETTINGS_IMPORTING;
    qa_source_save_io io = {0};
    application_native_q3_cvar_snapshot restored[SETTINGS_CAPACITY] = {{0}};
    bool used[SETTINGS_CAPACITY] = {0};
    bool warmup_observed = false;
    size_t count = 0;
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && cache_header(&io, owner, &count) &&
        qa_source_save_bool(&io, &warmup_observed);
    for (size_t i = 0; okay && i < definition_count(owner); ++i) {
        const setting_definition *definition = definition_at(owner, i);
        qa_cvars *registry = application_native_q3_cvar_owner(provider, definition->name);
        owner->references[i] = (setting_reference){registry,
            qa_cvars_resolve(registry, definition->name)};
        const qa_cvar_view *current = qa_cvars_read(registry, owner->references[i].handle);
        if (!current) okay = application_fail(error, QA_ERROR_FORMAT,
            "Q3 copied setting has no current source declaration");
        else okay = snapshot_copy(&restored[i], current, error);
    }
    for (size_t row = 0; okay && row < count; ++row) {
        const char *name = NULL, *value = NULL;
        bool pending = false;
        okay = gameplay_fields(&io, &name, &value, &pending);
        size_t index = 0;
        if (okay) {
            while (index < definition_count(owner) && !ascii_equal(name, definition_at(owner, index)->name)) ++index;
            if (index == definition_count(owner) || used[index] ||
                definition_at(owner, index)->save_policy != QA_CVAR_SAVE_GAMEPLAY)
                okay = application_fail(error, QA_ERROR_FORMAT, "Q3 copied gameplay names are unknown, repeated or settings");
        }
        application_native_q3_cvar_snapshot snapshot = {0};
        if (okay) okay = snapshot_restore_value(&snapshot,
            application_native_q3_cvar_owner(provider, definition_at(owner, index)->name),
            definition_at(owner, index)->name, value, pending, error);
        if (okay) {
            snapshot_dispose(&restored[index]);
            restored[index] = snapshot;
            snapshot = (application_native_q3_cvar_snapshot){0};
            used[index] = true;
        }
        snapshot_dispose(&snapshot);
        free((void *)name);
        free((void *)value);
    }
    if (okay) okay = qa_source_save_finish(&io, NULL);
    if (okay) {
        size_t warmup = 0;
        while (warmup < definition_count(owner) && strcmp(definition_at(owner, warmup)->name, "g_warmup")) ++warmup;
        okay = qa_q3_source_warmup_rebind(provider->state.q3,
            restored[warmup].modification_count, warmup_observed, error);
    }
    if (okay) {
        for (size_t i = 0; i < definition_count(owner); ++i) {
            snapshot_dispose(&owner->snapshots[i]);
            owner->snapshots[i] = restored[i];
            restored[i] = (application_native_q3_cvar_snapshot){0};
        }
        owner->password_seen = false;
        owner->password_modification_count = 0;
        owner->initialized = true;
    }
    for (size_t i = 0; i < definition_count(owner); ++i) snapshot_dispose(&restored[i]);
    qa_source_save_dispose(&io);
    owner->operation = SETTINGS_IDLE;
    if (!okay && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_FORMAT, "invalid native Q3 copied gameplay continuation");
    return okay;
}
