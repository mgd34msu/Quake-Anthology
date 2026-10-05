#include "internal.h"
#include "qa/settings_server_profile.h"
#include "qa/text.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define VALUE(name_) {QA_SERVER_SETTING_VALUE, name_, 0, false}
#define BIT(name_, mask_, inverted_) {QA_SERVER_SETTING_BIT, name_, mask_, inverted_}
#define TOGGLE(id_, name_, label_, default_, when_) \
    {id_, label_, default_, VALUE(name_), when_, QA_SERVER_SETTING_TOGGLE, {.maximum_length = 0}}
#define BIT_TOGGLE(id_, name_, label_, default_, mask_, inverted_, when_) \
    {id_, label_, default_, BIT(name_, mask_, inverted_), when_, QA_SERVER_SETTING_TOGGLE, {.maximum_length = 0}}
#define NUMBER(id_, name_, label_, default_, integer_, when_) \
    {id_, label_, default_, VALUE(name_), when_, QA_SERVER_SETTING_NUMBER, \
        {.number = {0, 2147483647, integer_ ? 1 : 0.25, integer_}}}
#define CHOICE(id_, name_, label_, choices_, count_) \
    {id_, label_, "0", VALUE(name_), QA_SERVER_SETTING_NEXT_MAP, QA_SERVER_SETTING_CHOICE, \
        {.choices = {choices_, count_}}}
#define TEXT(id_, name_, label_) \
    {id_, label_, "", VALUE(name_), QA_SERVER_SETTING_LIVE, QA_SERVER_SETTING_TEXT, {.maximum_length = 2048}}

static const qa_server_setting_choice q1_teamplay[] = {
    {"0", "Off"}, {"1", "No friendly fire"}, {"2", "Friendly fire"},
    {"3", "Tag"}, {"4", "Capture the flag"}, {"5", "One flag CTF"}, {"6", "Three team CTF"}
};
static const qa_server_setting_definition q1_standard =
    CHOICE("server:q1.teamplay", "teamplay", "Quake teamplay", q1_teamplay, 3);
static const qa_server_setting_definition q1_rogue =
    CHOICE("server:q1.teamplay", "teamplay", "Quake teamplay", q1_teamplay, COUNT(q1_teamplay));
static const qa_server_setting_definition q2_spawn[] = {
    BIT_TOGGLE("server:q2.no-health", "dmflags", "Exclude health items", "0", 1, false, QA_SERVER_SETTING_NEXT_MAP),
    BIT_TOGGLE("server:q2.no-powerups", "dmflags", "Exclude powerups", "0", 2, false, QA_SERVER_SETTING_NEXT_MAP),
    BIT_TOGGLE("server:q2.no-armor", "dmflags", "Exclude armor items", "0", 2048, false, QA_SERVER_SETTING_NEXT_MAP)
};
static const qa_server_setting_definition q2_combat =
    BIT_TOGGLE("server:friendly-fire", "dmflags", "Friendly fire", "1", 256, true, QA_SERVER_SETTING_LIVE);
static const qa_server_setting_definition q2_limits[] = {
    NUMBER("server:time-limit", "timelimit", "Time limit (minutes)", "0", false, QA_SERVER_SETTING_LIVE),
    NUMBER("server:frag-limit", "fraglimit", "Frag limit", "0", true, QA_SERVER_SETTING_LIVE)
};
static const qa_server_setting_definition q2_capture =
    NUMBER("server:capture-limit", "capturelimit", "Capture limit", "0", true, QA_SERVER_SETTING_LIVE);
static const qa_server_setting_definition q2_deathball =
    NUMBER("server:goal-limit", "goallimit", "Goal limit", "0", true, QA_SERVER_SETTING_LIVE);
static const qa_server_setting_definition lmctf_limits[] = {
    NUMBER("server:time-limit", "timelimit", "Match time (minutes)", "0", true, QA_SERVER_SETTING_NEXT_MATCH),
    NUMBER("server:frag-limit", "fraglimit", "Frag limit", "0", true, QA_SERVER_SETTING_LIVE)
};
static const qa_server_setting_definition lmctf_runes[] = {
    BIT_TOGGLE("server:lmctf.rune.damage", "runes", "Damage Artifact", "1", 1, false, QA_SERVER_SETTING_NEXT_MAP),
    BIT_TOGGLE("server:lmctf.rune.haste", "runes", "Haste Artifact", "1", 4, false, QA_SERVER_SETTING_NEXT_MAP),
    BIT_TOGGLE("server:lmctf.rune.resist", "runes", "Resist Artifact", "1", 2, false, QA_SERVER_SETTING_NEXT_MAP),
    BIT_TOGGLE("server:lmctf.rune.regen", "runes", "Regen Artifact", "1", 8, false, QA_SERVER_SETTING_NEXT_MAP),
    BIT_TOGGLE("server:lmctf.rune.vampire", "runes", "Vampire Artifact", "0", 16, false, QA_SERVER_SETTING_NEXT_MAP)
};
static const qa_server_setting_definition lmctf_weapons =
    TOGGLE("server:lmctf.fast-switch", "fastswitch", "Fast weapon switching", "0", QA_SERVER_SETTING_LIVE);
static const qa_server_setting_definition q2_rerelease[] = {
    TOGGLE("server:q2.random-items", "g_dm_random_items", "Random item respawns", "0", QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.no-quadfire-drop", "g_dm_no_quadfire_drop", "Prevent DualFire drop", "0", QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.instant-switch", "g_instant_weapon_switch", "Instant weapon switching", "0", QA_SERVER_SETTING_NEXT_MAP),
    NUMBER("server:q2.weapon-respawn", "g_weapon_respawn_time", "Weapon respawn seconds", "30", false, QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.weapons-stay", "g_dm_weapons_stay", "Weapons stay", "0", QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.instant-items", "g_dm_instant_items", "Instant powerups", "1", QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.same-level", "g_dm_same_level", "Repeat current level", "0", QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.no-quad-drop", "g_dm_no_quad_drop", "Prevent Quad drop", "0", QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.no-stack-double", "g_dm_no_stack_double", "Disable stacked Double Damage", "0", QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.strong-mines", "g_dm_strong_mines", "Strong proximity mines", "0", QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.squad-respawn", "g_coop_squad_respawn", "Respawn near teammates", "1", QA_SERVER_SETTING_NEXT_MAP),
    TOGGLE("server:q2.instanced-items", "g_coop_instanced_items", "Individual cooperative pickups", "1", QA_SERVER_SETTING_NEXT_MAP),
    TOGGLE("server:q2.coop-lives", "g_coop_enable_lives", "Limited cooperative lives", "0", QA_SERVER_SETTING_NEXT_MAP),
    NUMBER("server:q2.coop-num-lives", "g_coop_num_lives", "Extra cooperative lives", "2", true, QA_SERVER_SETTING_NEXT_MAP),
    TOGGLE("server:q2.player-collision", "g_coop_player_collision", "Cooperative player collision", "0", QA_SERVER_SETTING_NEXT_MAP),
    TOGGLE("server:q2.force-respawn", "g_dm_force_respawn", "Force deathmatch respawn", "0", QA_SERVER_SETTING_LIVE),
    NUMBER("server:q2.respawn-time", "g_dm_force_respawn_time", "Forced respawn delay", "0", false, QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.no-fall-damage", "g_dm_no_fall_damage", "Disable deathmatch fall damage", "0", QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.farthest-spawn", "g_dm_spawn_farthest", "Farthest deathmatch spawn", "1", QA_SERVER_SETTING_LIVE),
    TOGGLE("server:q2.allow-exit", "g_dm_allow_exit", "Allow deathmatch exits", "0", QA_SERVER_SETTING_LIVE)
};
static const qa_server_setting_definition q2_rotation = TEXT("server:map-rotation", "sv_maplist", "Map rotation");
static const qa_server_setting_definition q2_rerelease_rotation = TEXT("server:map-rotation", "g_map_list", "Map rotation");
static const qa_server_setting_definition q2_shuffle =
    TOGGLE("server:map-rotation-shuffle", "g_map_list_shuffle", "Shuffle after last map", "0", QA_SERVER_SETTING_LIVE);
static const qa_server_setting_definition q3_limits[] = {
    NUMBER("server:time-limit", "timelimit", "Time limit (minutes)", "0", true, QA_SERVER_SETTING_LIVE),
    NUMBER("server:frag-limit", "fraglimit", "Frag limit", "20", true, QA_SERVER_SETTING_LIVE),
    NUMBER("server:capture-limit", "capturelimit", "Capture limit", "8", true, QA_SERVER_SETTING_LIVE)
};
static const qa_server_setting_choice q3_modes[] = {
    {"0", "Free for all"}, {"1", "Tournament"}, {"2", "Single player"}, {"3", "Team deathmatch"},
    {"4", "Capture the flag"}, {"5", "One flag CTF"}, {"6", "Overload"}, {"7", "Harvester"}
};
static const qa_server_setting_definition q3_match =
    CHOICE("server:q3.game-type", "g_gametype", "Q3 match type", q3_modes, 5);
static const qa_server_setting_definition team_arena_match =
    CHOICE("server:q3.game-type", "g_gametype", "Q3 match type", q3_modes, COUNT(q3_modes));
static const qa_server_setting_definition q3_combat =
    TOGGLE("server:friendly-fire", "g_friendlyFire", "Friendly fire", "0", QA_SERVER_SETTING_LIVE);

static bool append(qa_server_setting_catalog *catalog, const qa_server_setting_definition *items,
    size_t count, qa_error *error)
{
    if (count > QA_SERVER_SETTING_MAX - catalog->count)
        return settings_fail(error, "Server setting catalog exceeds its declared bound");
    for (size_t i = 0; i < count; ++i) catalog->items[catalog->count++] = items + i;
    return true;
}

bool qa_server_setting_definitions(qa_server_setting_selection selection,
    qa_server_setting_catalog *out, qa_error *error)
{
    if (!out) return settings_fail(error, "Missing selected server definitions");
    qa_server_setting_catalog catalog = {0};
    bool ok = true;
    switch (selection.source) {
    case QA_SERVER_SOURCE_NONE: case QA_SERVER_SOURCE_Q1_CTF: break;
    case QA_SERVER_SOURCE_Q1: ok = append(&catalog, &q1_standard, 1, error); break;
    case QA_SERVER_SOURCE_Q1_ROGUE: ok = append(&catalog, &q1_rogue, 1, error); break;
    case QA_SERVER_SOURCE_Q2: case QA_SERVER_SOURCE_Q2_RERELEASE:
        ok = append(&catalog, q2_spawn, COUNT(q2_spawn), error) &&
            (!selection.native_combat || append(&catalog, &q2_combat, 1, error));
        if (selection.match == QA_MODE_LMCTF)
            ok = ok && append(&catalog, lmctf_limits, COUNT(lmctf_limits), error) &&
                append(&catalog, lmctf_runes, COUNT(lmctf_runes), error) && append(&catalog, &lmctf_weapons, 1, error);
        else ok = ok && append(&catalog, q2_limits, COUNT(q2_limits), error);
        if (selection.match == QA_MODE_Q2_CTF) ok = ok && append(&catalog, &q2_capture, 1, error);
        if (selection.match == QA_MODE_Q2_DEATHBALL) ok = ok && append(&catalog, &q2_deathball, 1, error);
        if (selection.source == QA_SERVER_SOURCE_Q2_RERELEASE)
            ok = ok && append(&catalog, q2_rerelease, COUNT(q2_rerelease), error) &&
                append(&catalog, &q2_rerelease_rotation, 1, error) && append(&catalog, &q2_shuffle, 1, error);
        else ok = ok && append(&catalog, &q2_rotation, 1, error);
        break;
    case QA_SERVER_SOURCE_Q3: case QA_SERVER_SOURCE_TEAM_ARENA:
        ok = append(&catalog, q3_limits, COUNT(q3_limits), error) &&
            append(&catalog, selection.source == QA_SERVER_SOURCE_TEAM_ARENA ? &team_arena_match : &q3_match, 1, error) &&
            (!selection.native_combat || append(&catalog, &q3_combat, 1, error));
        break;
    default: return settings_fail(error, "Unknown selected server Source");
    }
    if (!ok) return false;
    *out = catalog;
    return true;
}

static bool retain(const char *input, qa_buffer *out, qa_error *error)
{
    size_t size = strlen(input);
    unsigned char *copy = malloc(size + 1);
    if (!copy) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining server setting value"); return false; }
    memcpy(copy, input, size + 1);
    *out = (qa_buffer){copy, size};
    return true;
}

bool qa_server_setting_parse(const qa_server_setting_definition *definition,
    const char *input, qa_buffer *out, qa_error *error)
{
    if (!definition || !input || !out) return settings_fail(error, "Missing server setting value");
    const char *normalized = input;
    char number[32];
    qa_bytes bytes = {(const unsigned char *)input, strlen(input)};
    switch (definition->kind) {
    case QA_SERVER_SETTING_TOGGLE:
        if (!strcmp(input, "1") || !strcmp(input, "true")) normalized = "1";
        else if (!strcmp(input, "0") || !strcmp(input, "false")) normalized = "0";
        else return settings_fail(error, "Server toggle requires a boolean");
        break;
    case QA_SERVER_SETTING_NUMBER: {
        size_t cursor = 0; uint32_t scalar; bool nonempty = false;
        while (qa_utf8_next(bytes, &cursor, &scalar))
            if (!qa_unicode_whitespace(scalar)) { nonempty = true; break; }
        double value;
        if (!nonempty) return settings_fail(error, "Server number requires nonempty numeric text");
        if (!qa_parse_ecmascript_number(bytes, &value, error)) return false;
        if (!isfinite(value) || value < definition->control.number.minimum ||
            value > definition->control.number.maximum ||
            (definition->control.number.integer && (floor(value) != value || fabs(value) > 9007199254740991.0)))
            return settings_fail(error, "Server number is outside its allowed range");
        if (!qa_format_ecmascript_number(value, number, error)) return false;
        normalized = number;
        break;
    }
    case QA_SERVER_SETTING_CHOICE: {
        bool found = false;
        for (size_t i = 0; i < definition->control.choices.count; ++i)
            if (!strcmp(input, definition->control.choices.items[i].id)) { found = true; break; }
        if (!found) return settings_fail(error, "Unknown server setting choice");
        break;
    }
    case QA_SERVER_SETTING_TEXT: {
        if (!qa_utf8_valid(bytes) || strpbrk(input, "\r\n"))
            return settings_fail(error, "Server setting contains invalid text");
        size_t cursor = 0, length = 0; uint32_t scalar;
        while (qa_utf8_next(bytes, &cursor, &scalar)) {
            length += scalar > 0xffff ? 2u : 1u;
            if (length > definition->control.maximum_length)
                return settings_fail(error, "Server setting text exceeds its allowed length");
        }
        break;
    }
    default: return settings_fail(error, "Unknown server setting control");
    }
    return retain(normalized, out, error);
}

struct qa_server_profile {
    qa_server_profile_override *overrides;
    size_t count;
};

void qa_server_profile_destroy(qa_server_profile *profile)
{
    if (!profile) return;
    for (size_t i = 0; i < profile->count; ++i) free((void *)profile->overrides[i].value);
    free(profile->overrides);
    free(profile);
}

const qa_server_profile_override *qa_server_profile_overrides(const qa_server_profile *profile, size_t *count)
{
    if (count) *count = profile ? profile->count : 0;
    return profile ? profile->overrides : NULL;
}

static const qa_server_setting_definition *find(const qa_server_setting_catalog *catalog, const char *id)
{
    for (size_t i = 0; i < catalog->count; ++i)
        if (!strcmp(catalog->items[i]->id, id)) return catalog->items[i];
    return NULL;
}

static bool profile_version(const qa_json_document *document, qa_json_id root, qa_error *error)
{
    qa_json_id id = qa_json_get(document, root, "version");
    double version;
    return settings_object(document, root, error) &&
        (qa_json_type(document, id) == QA_JSON_NUMBER || settings_fail(error, "Unsupported server profile version")) &&
        qa_parse_ecmascript_number(qa_json_source(document, id), &version, error) &&
        (version == 1 || settings_fail(error, "Unsupported server profile version"));
}

bool qa_server_profile_parse(qa_bytes bytes, qa_server_setting_selection selection,
    qa_server_profile **out, qa_error *error)
{
    if (!out || *out) return settings_fail(error, "Server profile requires an empty output");
    qa_server_setting_catalog catalog;
    if (!qa_server_setting_definitions(selection, &catalog, error)) return false;
    qa_json_document *document;
    if (!qa_json_parse(bytes, &document, error)) return false;
    qa_json_id root = qa_json_root(document), overrides = qa_json_get(document, root, "overrides");
    qa_server_profile *profile = NULL;
    bool ok = profile_version(document, root, error) &&
        (qa_json_type(document, overrides) == QA_JSON_ARRAY || settings_fail(error, "Server profile requires overrides"));
    size_t count = ok ? qa_json_size(document, overrides) : 0;
    if (ok && count > catalog.count) ok = settings_fail(error, "Server profile setting has no unique selected owner");
    if (ok) {
        profile = calloc(1, sizeof(*profile));
        if (profile && count) profile->overrides = calloc(count, sizeof(*profile->overrides));
        if (!profile || (count && !profile->overrides)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining server profile"); ok = false;
        }
    }
    for (size_t i = 0; ok && i < count; ++i) {
        qa_json_id entry = qa_json_at(document, overrides, i);
        char *id = NULL, *value = NULL;
        qa_buffer normalized = {0};
        ok = settings_object(document, entry, error) &&
            settings_string(document, qa_json_get(document, entry, "id"), &id, error) &&
            settings_string(document, qa_json_get(document, entry, "value"), &value, error);
        const qa_server_setting_definition *definition = ok ? find(&catalog, id) : NULL;
        if (ok && !definition) ok = settings_fail(error, "Server profile setting has no selected owner");
        for (size_t j = 0; ok && j < profile->count; ++j)
            if (!strcmp(profile->overrides[j].id, id)) ok = settings_fail(error, "Duplicate server profile setting");
        if (ok) ok = qa_server_setting_parse(definition, value, &normalized, error);
        if (ok) profile->overrides[profile->count++] =
            (qa_server_profile_override){definition->id, (char *)normalized.data};
        free(id); free(value);
    }
    qa_json_destroy(document);
    if (!ok) { qa_server_profile_destroy(profile); return false; }
    *out = profile;
    return true;
}

bool qa_server_profile_encode(const qa_server_profile *profile, qa_buffer *out, qa_error *error)
{
    if (!profile || !out) return settings_fail(error, "Missing server profile encoding");
    qa_json_writer storage = {0};
    qa_json_writer *writer = &storage;
    qa_json_writer_object(writer);
    settings_key_number(writer, "version", 1);
    qa_json_writer_key(writer, "overrides"); qa_json_writer_array(writer);
    for (size_t i = 0; i < profile->count; ++i) {
        qa_json_writer_object(writer);
        settings_key_string(writer, "id", profile->overrides[i].id);
        settings_key_string(writer, "value", profile->overrides[i].value);
        qa_json_writer_end(writer);
    }
    qa_json_writer_end(writer); qa_json_writer_end(writer);
    return settings_finish(writer, out, error);
}

bool qa_settings_load_server_profile(qa_settings_store store, const char *path,
    qa_server_setting_selection selection, qa_server_profile **out, bool *found, qa_error *error)
{
    if (!out || *out || !found) return settings_fail(error, "Missing server profile load output");
    qa_resource *resource = NULL; bool present;
    if (!qa_settings_read(store, path, &resource, &present, error)) return false;
    bool ok = !present || qa_server_profile_parse(qa_resource_bytes(resource), selection, out, error);
    qa_resource_release(resource);
    if (ok) *found = present;
    return ok;
}

bool qa_server_profile_apply_startup(const qa_server_profile *profile,
    qa_server_setting_selection selection, const qa_server_profile_owner *owner, qa_error *error)
{
    if (!profile || !owner || !owner->read_desired || !owner->write_initial)
        return settings_fail(error, "Missing startup server setting owner");
    qa_server_setting_catalog catalog;
    if (!qa_server_setting_definitions(selection, &catalog, error)) return false;
    const qa_server_setting_definition *definitions[QA_SERVER_SETTING_MAX];
    qa_buffer values[QA_SERVER_SETTING_MAX] = {{0}};
    bool ok = true;
    for (size_t i = 0; ok && i < profile->count; ++i) {
        definitions[i] = find(&catalog, profile->overrides[i].id);
        if (!definitions[i]) { ok = settings_fail(error, "Server profile setting has no fresh selected owner"); break; }
        const char *desired = NULL;
        ok = qa_server_setting_parse(definitions[i], profile->overrides[i].value, values + i, error) &&
            owner->read_desired(owner->context, definitions[i]->target.name, &desired, error);
        if (ok && !desired) ok = settings_fail(error, "Server setting has no initial cvar owner");
    }
    for (size_t i = 0; ok && i < profile->count; ++i) {
        const qa_server_setting_target *target = &definitions[i]->target;
        const char *value = (const char *)values[i].data;
        char merged[32];
        if (target->kind == QA_SERVER_SETTING_BIT) {
            const char *desired = NULL;
            if (!owner->read_desired(owner->context, target->name, &desired, error) || !desired) { ok = false; break; }
            double number = 0;
            qa_error numeric = {0};
            if (!qa_parse_ecmascript_number((qa_bytes){(const unsigned char *)desired, strlen(desired)}, &number, &numeric) &&
                numeric.code != QA_ERROR_FORMAT) {
                if (error) *error = numeric;
                ok = false; break;
            }
            uint32_t bits = (uint32_t)qa_number_to_i32(number);
            bool enabled = (!strcmp(value, "1")) != target->inverted;
            bits = enabled ? bits | target->mask : bits & ~target->mask;
            if (!qa_format_ecmascript_number((double)bits, merged, error)) { ok = false; break; }
            value = merged;
        }
        ok = owner->write_initial(owner->context, target->name, value, error);
    }
    for (size_t i = 0; i < profile->count; ++i) qa_buffer_free(values + i);
    return ok;
}
