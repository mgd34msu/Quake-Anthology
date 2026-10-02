#include "native_q3_settings.h"
#include "native_q3_console.h"
#include "startup_flow.h"
#include "qa/source_save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct setting_definition {
    const char *name, *value;
    uint32_t flags;
    bool track, team_shader;
} setting_definition;

#define A QA_CVAR_ARCHIVE
#define S QA_CVAR_SERVERINFO
#define U QA_CVAR_USERINFO
#define L QA_CVAR_LATCH
#define R QA_CVAR_READONLY
#define N QA_CVAR_NO_RESTART
#define Y QA_CVAR_SYSTEMINFO
#define CV(name, value, flags, track, shader) {name, value, flags, track, shader}
/* Q3GameSettings definitions preserve the source registration/update order. */
static const setting_definition common_settings[] = {
    CV("sv_cheats", "", 0, false, false),
    CV("g_restarted", "0", R, false, false),
    CV("g_gametype", "0", S | U | L, false, false),
    CV("sv_maxclients", "8", S | L | A, false, false),
    CV("g_maxGameClients", "0", S | L | A, false, false),
    CV("dmflags", "0", S | A, true, false),
    CV("fraglimit", "20", S | A | N, true, false),
    CV("timelimit", "0", S | A | N, true, false),
    CV("capturelimit", "8", S | A | N, true, false),
    CV("g_synchronousClients", "0", Y, false, false),
    CV("g_friendlyFire", "0", A, true, false),
    CV("g_teamAutoJoin", "0", A, false, false),
    CV("g_teamForceBalance", "0", A, false, false),
    CV("g_warmup", "20", A, true, false),
    CV("g_doWarmup", "0", 0, true, false),
    CV("g_log", "games.log", A, false, false),
    CV("g_logSync", "0", A, false, false),
    CV("g_password", "", U, false, false),
    CV("g_banIPs", "", A, false, false),
    CV("g_filterBan", "1", A, false, false),
    CV("g_needpass", "0", S | R, false, false),
    CV("dedicated", "0", 0, false, false),
    CV("g_speed", "320", 0, true, false),
    CV("g_gravity", "800", 0, true, false),
    CV("g_knockback", "1000", 0, true, false),
    CV("g_quadfactor", "3", 0, true, false),
    CV("g_weaponrespawn", "5", 0, true, false),
    CV("g_weaponTeamRespawn", "30", 0, true, false),
    CV("g_forcerespawn", "20", 0, true, false),
    CV("g_inactivity", "0", 0, true, false),
    CV("g_debugMove", "0", 0, false, false),
    CV("g_debugDamage", "0", 0, false, false),
    CV("g_debugAlloc", "0", 0, false, false),
    CV("g_motd", "", 0, false, false),
    CV("com_blood", "1", 0, false, false),
    CV("g_podiumDist", "80", 0, false, false),
    CV("g_podiumDrop", "70", 0, false, false),
    CV("g_allowVote", "1", A, false, false),
    CV("g_listEntity", "0", 0, false, false)
};
static const setting_definition missionpack_settings[] = {
    CV("g_obeliskHealth", "2500", 0, false, false),
    CV("g_obeliskRegenPeriod", "1", 0, false, false),
    CV("g_obeliskRegenAmount", "15", 0, false, false),
    CV("g_obeliskRespawnDelay", "10", S, false, false),
    CV("g_cubeTimeout", "30", 0, false, false),
    CV("g_redteam", "Stroggs", A | S | U, true, true),
    CV("g_blueteam", "Pagans", A | S | U, true, true),
    CV("ui_singlePlayerActive", "", 0, false, false),
    CV("g_enableDust", "0", S, true, false),
    CV("g_enableBreath", "0", S, true, false),
    CV("g_proxMineTimeout", "20000", 0, false, false)
};
static const setting_definition final_settings[] = {
    CV("g_smoothClients", "1", 0, false, false),
    CV("pmove_fixed", "0", Y, false, false),
    CV("pmove_msec", "8", Y, false, false),
    CV("g_rankings", "0", 0, false, false),
    CV("sv_enableRankings", "0", 0, false, false),
    CV("sv_rankingsActive", "0", R, false, false)
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

struct application_native_q3_settings {
    application_provider *provider;
    application_native_q3_settings_options options;
    application_native_q3_cvar_snapshot snapshots[SETTINGS_CAPACITY];
    size_t snapshot_order[SETTINGS_CAPACITY];
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
                definition->flags, owner, error)
            : qa_cvars_register(registry, definition->name, definition->value, definition->flags,
                owner, NULL, error);
        if (!ok) return false;
    }
    return true;
}

bool application_native_q3_settings_prepare_definitions(application_provider *provider,
    qa_q3_product product, qa_error *error)
{
    if (!provider || !provider->product || provider->constructed || provider->attached ||
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
            definition->flags, !strcmp(definition->name, "sv_cheats") ? 0 : provider->owner, NULL, error);
        if (okay && !owner_live(owner))
            okay = application_fail(error, QA_ERROR_ARGUMENT, "native Q3 settings source retired during registration");
        const qa_cvar_view *current = okay ? qa_cvars_find(registry, definition->name) : NULL;
        if (okay && !current)
            okay = application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 registered cvar disappeared");
        if (okay) okay = snapshot_copy(&copied[i], current, error);
    }
    if (okay) {
        for (size_t i = 0; i < count; ++i) {
            snapshot_dispose(&owner->snapshots[i]);
            owner->snapshots[i] = copied[i];
            owner->snapshot_order[i] = i;
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

bool application_native_q3_settings_snapshot(const application_provider *provider, const char *name,
    const application_native_q3_cvar_snapshot **out, qa_error *error)
{
    const struct application_native_q3_settings *owner = owner_at(provider);
    if (!owner || !owner->initialized || !name || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 cached settings are not initialized");
    for (size_t i = 0; i < definition_count(owner); ++i)
        if (!strcmp(definition_at(owner, i)->name, name)) {
            *out = &owner->snapshots[i];
            return true;
        }
    return application_fail(error, QA_ERROR_NOT_FOUND, "unregistered native Q3 cached setting");
}

bool application_native_q3_settings_integer(const application_provider *provider, const char *name,
    int32_t *out, qa_error *error)
{
    const application_native_q3_cvar_snapshot *snapshot;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 integer read requires its output");
    if (!application_native_q3_settings_snapshot(provider, name, &snapshot, error)) return false;
    *out = snapshot->integer_value;
    return true;
}

bool application_native_q3_settings_number(const application_provider *provider, const char *name,
    float *out, qa_error *error)
{
    const application_native_q3_cvar_snapshot *snapshot;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 number read requires its output");
    if (!application_native_q3_settings_snapshot(provider, name, &snapshot, error)) return false;
    *out = (float)snapshot->numeric_value;
    return true;
}

bool application_native_q3_settings_string(const application_provider *provider, const char *name,
    const char **out, qa_error *error)
{
    const application_native_q3_cvar_snapshot *snapshot;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 text read requires its output");
    if (!application_native_q3_settings_snapshot(provider, name, &snapshot, error)) return false;
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

bool application_native_q3_settings_force_set(application_provider *provider, const char *name,
    const char *value, qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    const application_native_q3_cvar_snapshot *snapshot;
    if (!owner || !value)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 force-set requires its actual cached setting");
    if (!application_native_q3_settings_snapshot(provider, name, &snapshot, error)) return false;
    (void)snapshot;
    return source_set(owner, name, value, error);
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

bool application_native_q3_settings_clamp_game_type(application_provider *provider, qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    int32_t type;
    if (!owner || !application_native_q3_settings_integer(provider, "g_gametype", &type, error)) return false;
    if (type >= 0 && type < 8) return true;
    if (owner->operation != SETTINGS_IDLE || !owner->options.print ||
        !application_native_q3_console_borrow(provider, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 game-type clamp lacks its actual source print owner");
    owner->operation = SETTINGS_EFFECT;
    char text[96];
    snprintf(text, sizeof(text), "g_gametype %d is out of range, defaulting to 0\n", type);
    bool okay = owner->options.print(owner->options.context, text, error);
    if (okay && !owner_live(owner))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "native Q3 settings source retired during game-type clamp");
    if (okay) okay = application_native_q3_settings_force_set(provider, "g_gametype", "0", error);
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
    if (!application_native_q3_settings_snapshot(provider, "g_password", &password, error)) return false;
    if (owner->password_seen && owner->password_modification_count == password->modification_count)
        return true;
    if (!application_native_q3_console_borrow(provider, error)) return false;
    owner->operation = SETTINGS_EFFECT;
    bool needpass = password->value[0] && !ascii_equal(password->value, "none");
    owner->password_seen = true;
    owner->password_modification_count = password->modification_count;
    bool okay = application_native_q3_settings_force_set(provider, "g_needpass", needpass ? "1" : "0", error);
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
        qa_cvars *registry = application_native_q3_cvar_owner(provider, definition->name);
        const qa_cvar_view *current = qa_cvars_find(registry, definition->name);
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

static bool snapshot_fields(qa_source_save_io *io, application_native_q3_cvar_snapshot *snapshot)
{
    return text_field(io, &snapshot->name, false) && text_field(io, &snapshot->value, false) &&
        text_field(io, &snapshot->reset_value, false) && text_field(io, &snapshot->latched_value, true) &&
        qa_source_save_u32(io, &snapshot->flags) && qa_source_save_bool(io, &snapshot->modified) &&
        qa_source_save_u64(io, &snapshot->modification_count) &&
        qa_source_save_f64(io, &snapshot->numeric_value) && qa_source_save_i32(io, &snapshot->integer_value);
}

static bool cache_header(qa_source_save_io *io, const struct application_native_q3_settings *owner)
{
    uint8_t magic[4] = {'Q', 'A', 'G', 'C'};
    uint32_t version = 2, product = owner->product;
    size_t count = definition_count(owner);
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QAGC", 4) &&
        qa_source_save_u32(io, &version) && version == 2 && qa_source_save_u32(io, &product) &&
        product == (uint32_t)owner->product && qa_source_save_count(io, &count, definition_count(owner)) &&
        count == definition_count(owner);
}

bool application_native_q3_settings_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    struct application_native_q3_settings *owner = owner_at(provider);
    if (!owner || !owner->initialized || !out || owner->operation != SETTINGS_IDLE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 cached settings are not at a continuation boundary");
    owner->operation = SETTINGS_CAPTURING;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) && cache_header(&io, owner) &&
        qa_source_save_bool(&io, &owner->password_seen) &&
        (!owner->password_seen || qa_source_save_u64(&io, &owner->password_modification_count));
    for (size_t i = 0; okay && i < definition_count(owner); ++i)
        okay = snapshot_fields(&io, &owner->snapshots[owner->snapshot_order[i]]);
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
    size_t order[SETTINGS_CAPACITY] = {0};
    bool used[SETTINGS_CAPACITY] = {0};
    bool password_seen = false;
    uint64_t password_modification_count = 0;
    size_t count = definition_count(owner);
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && cache_header(&io, owner) &&
        qa_source_save_bool(&io, &password_seen) &&
        (!password_seen || qa_source_save_u64(&io, &password_modification_count));
    for (size_t row = 0; okay && row < count; ++row) {
        application_native_q3_cvar_snapshot snapshot = {0};
        okay = snapshot_fields(&io, &snapshot);
        size_t index = 0;
        if (okay) {
            while (index < count && !ascii_equal(snapshot.name, definition_at(owner, index)->name)) ++index;
            if (index == count || used[index])
                okay = application_fail(error, QA_ERROR_FORMAT, "Q3 cached settings have invalid or duplicate source names");
        }
        if (okay) {
            restored[index] = snapshot;
            snapshot = (application_native_q3_cvar_snapshot){0};
            used[index] = true;
            order[row] = index;
        }
        snapshot_dispose(&snapshot);
    }
    if (okay) okay = qa_source_save_finish(&io, NULL);
    if (okay) {
        for (size_t i = 0; i < count; ++i) {
            owner->snapshots[i] = restored[i];
            owner->snapshot_order[i] = order[i];
            restored[i] = (application_native_q3_cvar_snapshot){0};
        }
        owner->password_seen = password_seen;
        owner->password_modification_count = password_modification_count;
        owner->initialized = true;
    }
    for (size_t i = 0; i < count; ++i) snapshot_dispose(&restored[i]);
    qa_source_save_dispose(&io);
    owner->operation = SETTINGS_IDLE;
    if (!okay && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_FORMAT, "invalid native Q3 cached settings continuation");
    return okay;
}
