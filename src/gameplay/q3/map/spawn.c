#include "internal.h"
#include "qa/game_type.h"

#include <ctype.h>

static bool bytes_equal(qa_bytes value, const char *text) {
    size_t length = strlen(text);
    if (value.size != length)
        return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char a = value.data[i], b = (unsigned char)text[i];
        if (a >= 'A' && a <= 'Z')
            a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = (unsigned char)(b + ('a' - 'A'));
        if (a != b)
            return false;
    }
    return true;
}

bool q3_map_property(const qa_q3_map_fields *fields, const char *key, qa_bytes *out) {
    if (!fields || !key || !out)
        return false;
    for (size_t i = 0; i < fields->count; ++i)
        if (bytes_equal(fields->properties[i].key, key)) {
            *out = fields->properties[i].value;
            return true;
        }
    return false;
}

static bool intern_text(qa_q3_game *game, qa_bytes input, bool fold, qa_string_id *out,
                        qa_error *error) {
    uint8_t local[256];
    uint8_t *text = input.size <= sizeof(local) ? local : malloc(input.size);
    if (!text && input.size) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 authored string");
        return false;
    }
    size_t written = 0;
    for (size_t i = 0; i < input.size; ++i) {
        uint8_t byte = input.data[i];
        if (byte == '\\' && i + 1 < input.size) {
            byte = input.data[++i] == 'n' ? '\n' : '\\';
        }
        if (fold && byte >= 'A' && byte <= 'Z')
            byte = (uint8_t)(byte + ('a' - 'A'));
        text[written++] = byte;
    }
    bool okay = qa_strings_intern(qa_session_strings(game->options.services.session),
                                  (qa_bytes){text, written}, out, error);
    if (text != local)
        free(text);
    return okay;
}

bool q3_map_intern(qa_q3_game *game, qa_bytes input, qa_string_id *out, qa_error *error) {
    return intern_text(game, input, false, out, error);
}

bool q3_map_intern_fold(qa_q3_game *game, qa_bytes input, qa_string_id *out,
                        qa_error *error) {
    return intern_text(game, input, true, out, error);
}

typedef struct q3_source_number {
    qa_bytes text;
    size_t offset;
    bool overrun;
} q3_source_number;

static int source_byte(q3_source_number *input) {
    if (input->offset > input->text.size) {
        input->overrun = true;
        return 0;
    }
    if (input->offset == input->text.size || input->text.data[input->offset] == 0)
        return 0;
    uint8_t byte = input->text.data[input->offset];
    return byte < 128 ? byte : (int)byte - 256;
}

static int source_take(q3_source_number *input) {
    int byte = source_byte(input);
    ++input->offset;
    return byte;
}

static void source_space(q3_source_number *input) {
    int byte;
    while ((byte = source_byte(input)) <= 32 && byte != 0)
        ++input->offset;
}

static int source_sign(q3_source_number *input) {
    int byte = source_byte(input);
    if (byte != '+' && byte != '-')
        return 1;
    ++input->offset;
    return byte == '-' ? -1 : 1;
}

static float source_float(q3_source_number *input, bool scan) {
    source_space(input);
    if (source_byte(input) == 0)
        return 0;
    int sign = source_sign(input);
    float value = 0;
    int character = scan ? '0' : source_byte(input);
    if (source_byte(input) != '.') {
        for (;;) {
            character = source_take(input);
            if (character < '0' || character > '9')
                break;
            value = ((value * 10.0f) + (float)(character - '0'));
        }
    } else if (!scan)
        ++input->offset;
    if (character == '.') {
        float fraction = 0.1f;
        for (;;) {
            character = source_take(input);
            if (character < '0' || character > '9')
                break;
            value = (value + ((float)(character - '0') * fraction));
            fraction = (fraction * 0.1f);
        }
    }
    return (value * (float)sign);
}

float q3_source_atof(qa_bytes text) {
    q3_source_number input = {.text = text};
    return source_float(&input, false);
}

static int32_t source_integer(qa_bytes text) {
    q3_source_number input = {.text = text};
    source_space(&input);
    if (source_byte(&input) == 0)
        return 0;
    int sign = source_sign(&input);
    uint32_t value = 0;
    for (;;) {
        int character = source_take(&input);
        if (character < '0' || character > '9')
            break;
        value = value * UINT32_C(10) + (uint32_t)(character - '0');
    }
    if (sign < 0)
        value = UINT32_C(0) - value;
    int32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

bool q3_map_number(const qa_q3_map_fields *fields, const char *key, double fallback,
                   double *out, qa_error *error) {
    qa_bytes text;
    if (!q3_map_property(fields, key, &text)) {
        *out = fallback;
        return true;
    }
    *out = q3_source_atof(text);
    (void)error;
    return true;
}

bool q3_map_integer(const qa_q3_map_fields *fields, const char *key, int32_t fallback,
                    int32_t *out) {
    qa_bytes text;
    *out = q3_map_property(fields, key, &text) ? source_integer(text) : fallback;
    return true;
}

bool q3_map_vector(const qa_q3_map_fields *fields, const char *key, qa_vec3 fallback,
                   qa_vec3 *out, qa_error *error) {
    qa_bytes text;
    if (!q3_map_property(fields, key, &text)) {
        *out = fallback;
        return true;
    }
    q3_source_number input = {.text = text};
    float value[3];
    value[0] = source_float(&input, true);
    value[1] = source_float(&input, true);
    value[2] = source_float(&input, true);
    if (input.overrun) {
        qa_error_set(error, QA_ERROR_FORMAT, fields->ordinal, "invalid Q3 vector map field %s",
                     key);
        return false;
    }
    if (!isfinite(value[0]) || !isfinite(value[1]) || !isfinite(value[2]))
        return q3_map_fail(error, "invalid Q3 authored vector");
    *out = qa_v3(value[0], value[1], value[2]);
    return qa_vec_finite(*out);
}

static bool set_string(qa_q3_game *game, qa_bytes value, qa_string_id *field, qa_error *error) {
    if (value.size >= INT32_MAX) return q3_map_fail(error, "Q3 spawn string exceeds G_Alloc size");
    uint32_t offset;
    if (!qa_q3_source_memory_allocate(game, (uint32_t)value.size + 1, &offset, error)) return false;
    uint32_t written = 0;
    for (size_t i = 0; i <= value.size; ++i) {
        uint8_t byte = i == value.size ? 0 : value.data[i];
        if (byte == '\\' && i < value.size) {
            ++i;
            byte = i < value.size && value.data[i] == 'n' ? '\n' : '\\';
        }
        game->memory.pool[offset + written++] = byte;
    }
    const uint8_t *text = game->memory.pool + offset;
    const uint8_t *end = memchr(text, 0, QA_Q3_SOURCE_MEMORY_BYTES - offset);
    if (!end) return q3_map_fail(error, "Game string reads beyond the source memory pool");
    return qa_strings_intern(qa_session_strings(game->options.services.session),
                             (qa_bytes){text, (size_t)(end - text)}, field, error);
}
static bool set_folded_string(qa_q3_game *game, qa_bytes value, qa_string_id *field,
                              qa_error *error) {
    qa_string_id original;
    if (!set_string(game, value, &original, error)) return false;
    const char *text = q3_map_cstr(game, original);
    size_t length = strlen(text);
    uint8_t *folded = malloc(length ? length : 1);
    if (!folded) { qa_error_set(error, QA_ERROR_MEMORY, 0, "folding Q3 target string"); return false; }
    for (size_t i = 0; i < length; ++i)
        folded[i] = text[i] >= 'A' && text[i] <= 'Z' ? (uint8_t)(text[i] + ('a' - 'A')) : (uint8_t)text[i];
    bool ok = qa_strings_intern(qa_session_strings(game->options.services.session),
                                (qa_bytes){folded, length}, field, error);
    free(folded);
    return ok;
}

static bool parse_generic(qa_q3_game *game, qa_bytes key, qa_bytes value,
                          qa_q3_map_actor_state *state, qa_error *error) {
    if (bytes_equal(key, "classname"))
        return set_string(game, value, &state->classname, error);
    if (bytes_equal(key, "model"))
        return set_string(game, value, &state->model, error);
    if (bytes_equal(key, "model2"))
        return set_string(game, value, &state->model2, error);
    if (bytes_equal(key, "target"))
        return set_folded_string(game, value, &state->target, error);
    if (bytes_equal(key, "targetname"))
        return set_folded_string(game, value, &state->targetname, error);
    if (bytes_equal(key, "message"))
        return set_string(game, value, &state->message, error);
    if (bytes_equal(key, "team"))
        return set_string(game, value, &state->team, error);
    if (bytes_equal(key, "targetshadername"))
        return set_string(game, value, &state->shader_old, error);
    if (bytes_equal(key, "targetshadernewname"))
        return set_string(game, value, &state->shader_new, error);
    double number;
    if (bytes_equal(key, "spawnflags") || bytes_equal(key, "count") ||
        bytes_equal(key, "health") || bytes_equal(key, "dmg") ||
        bytes_equal(key, "speed") || bytes_equal(key, "wait") ||
        bytes_equal(key, "random") || bytes_equal(key, "angle")) {
        if (bytes_equal(key, "spawnflags") || bytes_equal(key, "count") ||
            bytes_equal(key, "health") || bytes_equal(key, "dmg")) {
            int32_t integer = source_integer(value);
            if (bytes_equal(key, "spawnflags"))
                state->spawnflags = (uint32_t)integer;
            else if (bytes_equal(key, "count"))
                state->count = integer;
            else if (bytes_equal(key, "health"))
                state->health = integer;
            else
                state->damage = integer;
            return true;
        }
        number = q3_source_atof(value);
        if (!isfinite(number))
            return q3_map_fail(error, "invalid Q3 authored numeric field");
        if (bytes_equal(key, "speed"))
            state->speed = (float)number;
        else if (bytes_equal(key, "wait"))
            state->wait = (float)number;
        else if (bytes_equal(key, "random"))
            state->random = (float)number;
        else
            state->angles = qa_v3(0, (float)number, 0);
        return true;
    }
    if (bytes_equal(key, "origin") || bytes_equal(key, "angles")) {
        qa_q3_map_fields one = {.properties = &(qa_entity_property){key, value}, .count = 1};
        qa_vec3 vector;
        if (!q3_map_vector(&one, bytes_equal(key, "origin") ? "origin" : "angles",
                           qa_v3(0, 0, 0), &vector, error))
            return false;
        if (bytes_equal(key, "origin"))
            state->origin = vector;
        else
            state->angles = vector;
    }
    return true;
}

bool q3_map_parse_state(qa_q3_game *game, const qa_q3_map_fields *fields,
                        qa_q3_map_actor_state *state, qa_error *error) {
    if (!game || !game->map || !fields || (fields->count && !fields->properties) || !state)
        return q3_map_fail(error, "invalid Q3 authored entity fields");
    *state = (qa_q3_map_actor_state){.ordinal = fields->ordinal,
                                     .bounds = {qa_v3(0, 0, 0), qa_v3(0, 0, 0)}};
    for (size_t i = 0; i < fields->count; ++i) {
        if (!parse_generic(game, fields->properties[i].key, fields->properties[i].value, state,
                           error))
            return false;
    }
    if (!q3_map_text(game, state->classname))
        return true;
    qa_bytes value;
    if (q3_map_property(fields, "noise", &value) &&
        !qa_strings_intern(qa_session_strings(game->options.services.session),
                            value, &state->noise, error))
        return false;
    double number;
    state->has_delay = q3_map_property(fields, "delay", &value);
    if (!q3_map_number(fields, "delay", 0, &number, error))
        return false;
    state->delay = (float)number;
    if (!q3_map_number(fields, "roll", 0, &number, error))
        return false;
    state->roll = (float)number;
    const char *model = q3_map_cstr(game, state->model);
    if (model && model[0] == '*' && model[1]) {
        char *end = NULL;
        unsigned long index = strtoul(model + 1, &end, 10);
        if (!*end && index <= UINT32_MAX) {
            state->has_inline_model = true;
            state->inline_model = (uint32_t)index;
        }
    }
    return qa_vec_finite(state->origin) && qa_vec_finite(state->angles) && isfinite(state->speed) &&
           isfinite(state->wait) && isfinite(state->random) && isfinite(state->delay) &&
           isfinite(state->roll);
}

static bool property_nonzero(const qa_q3_map_fields *fields, const char *key, bool *out,
                             qa_error *error) {
    int32_t value;
    (void)error;
    q3_map_integer(fields, key, 0, &value);
    *out = value != 0;
    return true;
}

static bool contains(qa_bytes value, const char *needle) {
    size_t length = strlen(needle);
    if (!length || length > value.size)
        return false;
    for (size_t i = 0; i + length <= value.size; ++i)
        if (!memcmp(value.data + i, needle, length))
            return true;
    return false;
}

static bool filtered(qa_q3_game *game, const qa_q3_map_fields *fields,
                     qa_q3_map_filter *reason, bool *out, qa_error *error) {
    *out = false;
    int type = game->options.rules.game_type;
    bool rejected;
    if (!property_nonzero(fields, "notsingle", &rejected, error))
        return false;
    if (type == 2 && rejected) {
        *reason = QA_Q3_MAP_FILTER_NOT_SINGLE;
        *out = true;
        return true;
    }
    const char *team_key = type >= 3 ? "notteam" : "notfree";
    if (!property_nonzero(fields, team_key, &rejected, error))
        return false;
    if (rejected) {
        *reason = type >= 3 ? QA_Q3_MAP_FILTER_NOT_TEAM : QA_Q3_MAP_FILTER_NOT_FREE;
        *out = true;
        return true;
    }
    const char *product_key =
        game->options.product == QA_Q3_TEAM_ARENA ? "notta" : "notq3a";
    if (!property_nonzero(fields, product_key, &rejected, error))
        return false;
    if (rejected) {
        *reason = game->options.product == QA_Q3_TEAM_ARENA ? QA_Q3_MAP_FILTER_NOT_TA
                                                            : QA_Q3_MAP_FILTER_NOT_Q3A;
        *out = true;
        return true;
    }
    static const char *const names[] = {"ffa", "tournament", "single", "team",
                                        "ctf", "oneflag", "obelisk", "harvester"};
    qa_bytes modes;
    if (type >= 0 && type < (int)(sizeof(names) / sizeof(*names)) &&
        q3_map_property(fields, "gametype", &modes) && !contains(modes, names[type])) {
        *reason = QA_Q3_MAP_FILTER_GAMETYPE;
        *out = true;
    }
    return true;
}

static bool emit_text(qa_q3_game *game, qa_q3_map_event_kind kind, int32_t index,
                      const char *name, qa_string_id text, qa_error *error) {
    qa_string_id key = 0;
    if (name && !q3_map_intern_cstr(game, name, &key, error))
        return false;
    qa_q3_map_event event = {.kind = kind, .name = key, .text = text, .index = index};
    return kind == QA_Q3_MAP_CONFIGSTRING ? q3_configstring_event(game, &event, error)
                                          : q3_map_emit(game, &event, error);
}

static bool worldspawn(qa_q3_game *game, const qa_q3_map_fields *fields, qa_error *error) {
    qa_bytes value;
    qa_string_id music = 0, message = 0, empty, zero, gravity_text, dust, breath;
    if (!q3_map_intern_cstr(game, "", &empty, error) ||
        !q3_map_intern_cstr(game, "0", &zero, error))
        return false;
    if (q3_map_property(fields, "music", &value) &&
        !qa_strings_intern(qa_session_strings(game->options.services.session), value, &music, error))
        return false;
    if (q3_map_property(fields, "message", &value) &&
        !qa_strings_intern(qa_session_strings(game->options.services.session), value, &message, error))
        return false;
    if (!q3_map_property(fields, "gravity", &value))
        value = (qa_bytes){(const uint8_t *)"800", 3};
    if (!qa_strings_intern(qa_session_strings(game->options.services.session), value, &gravity_text, error))
        return false;
    double gravity;
    gravity = q3_source_atof(value);
    if (!isfinite(gravity) || gravity < -FLT_MAX || gravity > FLT_MAX)
        return q3_map_fail(error, "invalid Q3 world gravity");
    if (!q3_map_property(fields, "enableDust", &value))
        value = (qa_bytes){(const uint8_t *)"0", 1};
    if (!qa_strings_intern(qa_session_strings(game->options.services.session), value, &dust, error))
        return false;
    if (!q3_map_property(fields, "enableBreath", &value))
        value = (qa_bytes){(const uint8_t *)"0", 1};
    qa_string_id minus_one = 0;
    if (!qa_strings_intern(qa_session_strings(game->options.services.session), value, &breath, error) ||
        (game->map->options.warmup && !game->map->options.restarted &&
         !q3_map_intern_cstr(game, "-1", &minus_one, error)))
        return false;
    game->map->world_spawned = true;
    game->physics.gravity = (float)gravity;
    game->team_state.warmup_time_ms = 0;
    if (!emit_text(game, QA_Q3_MAP_CONFIGSTRING, 2, NULL, music, error) ||
        !emit_text(game, QA_Q3_MAP_CONFIGSTRING, 3, NULL, message, error) ||
        !emit_text(game, QA_Q3_MAP_CONFIGSTRING, 4, NULL, game->map->options.motd, error) ||
        (game->map->options.world_gravity &&
         !game->map->options.world_gravity(game->map->options.context, (float)gravity, error)) ||
        !emit_text(game, QA_Q3_MAP_CVAR, 0, "g_gravity", gravity_text, error) ||
        !emit_text(game, QA_Q3_MAP_CVAR, 0, "g_enableDust", dust, error) ||
        !emit_text(game, QA_Q3_MAP_CVAR, 0, "g_enableBreath", breath, error) ||
        !emit_text(game, QA_Q3_MAP_CONFIGSTRING, 5, NULL, empty, error))
        return false;
    if (game->map->options.restarted) {
        if (!emit_text(game, QA_Q3_MAP_CVAR, 0, "g_restarted", zero, error))
            return false;
    } else if (game->map->options.warmup) {
        game->team_state.warmup_time_ms = -1;
        if (!emit_text(game, QA_Q3_MAP_CONFIGSTRING, 5, NULL, minus_one, error))
            return false;
        if (game->options.hooks.source_log &&
            !game->options.hooks.source_log(game->options.hooks.context, "Warmup:\n", error))
            return false;
    }
    return true;
}

static bool point_class(const char *name) {
    static const char *const names[] = {
        "info_player_start",       "info_player_deathmatch", "info_player_intermission",
        "team_CTF_redplayer",     "team_CTF_blueplayer",    "team_CTF_redspawn",
        "team_CTF_bluespawn",     "item_botroam",           "info_camp",
        "info_notnull",           "misc_teleporter_dest"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (!strcmp(name, names[i]))
            return true;
    return false;
}

static bool target_class(const char *name) {
    static const char *const names[] = {
        "target_give",       "target_remove_powerups", "target_delay",
        "target_score",      "target_print",           "target_speaker",
        "target_push",       "target_laser",           "target_teleporter",
        "target_kill",       "target_location",        "target_relay",
        "target_position"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (!strcmp(name, names[i]))
            return true;
    return false;
}

static bool trigger_class(const char *name) {
    static const char *const names[] = {"trigger_multiple", "trigger_always",
                                        "trigger_push", "trigger_teleport",
                                        "trigger_hurt", "func_timer"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (!strcmp(name, names[i]))
            return true;
    return false;
}

static bool misc_class(const char *name) {
    static const char *const names[] = {"misc_portal_surface", "misc_portal_camera",
                                        "shooter_rocket", "shooter_plasma",
                                        "shooter_grenade"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (!strcmp(name, names[i]))
            return true;
    return false;
}

static bool mover_class(const char *name) {
    static const char *const names[] = {
        "func_door",   "func_plat",     "func_button",  "func_train",
        "path_corner", "func_static",   "func_rotating", "func_bobbing",
        "func_pendulum"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (!strcmp(name, names[i]))
            return true;
    return false;
}

static bool map_spawn(qa_q3_game *game, const qa_q3_map_fields *fields,
                     qa_q3_map_spawn_result *out, qa_error *error) {
    if (out)
        *out = (qa_q3_map_spawn_result){0};
    if (!game || !game->map || !fields || (fields->count && !fields->properties) || !out)
        return q3_map_fail(error, "invalid Q3 authored spawn");
    if (!game->map->world_spawned) {
        qa_bytes classname;
        if (!q3_map_property(fields, "classname", &classname) ||
            !bytes_equal(classname, "worldspawn"))
            return q3_map_fail(error, "first Q3 authored entity is not worldspawn");
        if (!worldspawn(game, fields, error))
            return false;
        *out = (qa_q3_map_spawn_result){.status = QA_Q3_MAP_WORLD,
            .actor = game->source_entities[QA_Q3_SOURCE_WORLD].actor,
            .classname = game->source_entities[QA_Q3_SOURCE_WORLD].classname};
        return true;
    }
    qa_actor_id allocated = {0};
    if (!q3_spawn_actor(game, &(qa_builtin_spawn){.owner = game->options.owner,
                         .definition = game->source_noclass},
                         &allocated, error))
        return false;
    qa_q3_map_actor_state state;
    if (!q3_map_parse_state(game, fields, &state, error))
        return allocated.registry ? q3_rollback_spawn(game, allocated, error) : false;
    state.actor = allocated;
    const char *classname = q3_map_cstr(game, state.classname);
    uint32_t source_slot;
    if (!qa_q3_source_actor_slot(game, allocated, &source_slot, error))
        return q3_rollback_spawn(game, allocated, error);
    game->source_entities[source_slot].classname = state.classname;
    if (!classname) {
        *out = (qa_q3_map_spawn_result){.status = QA_Q3_MAP_UNKNOWN};
        q3_map_warn(game, (qa_actor_id){0}, "Q3 authored entity has no classname");
        return qa_session_release(game->options.services.session, state.actor, error);
    }
    if (!qa_actors_set_metadata(qa_session_actor_registry(game->options.services.session),
            allocated, game->options.owner, state.classname, error))
        return q3_rollback_spawn(game, allocated, error);
    qa_q3_map_filter reason = QA_Q3_MAP_FILTER_NONE;
    bool rejected;
    if (!filtered(game, fields, &reason, &rejected, error))
        return q3_rollback_spawn(game, state.actor, error);
    if (rejected) {
        *out = (qa_q3_map_spawn_result){.status = QA_Q3_MAP_FILTERED,
                                        .filter = reason,
                                        .classname = state.classname};
        return qa_session_release(game->options.services.session, state.actor, error);
    }
    q3_wire_entity_source *wire = q3_wire_entity(game, state.actor);
    if (!wire)
        return q3_rollback_spawn(game, state.actor, error);
    wire->authored_origin = state.origin;
    wire->authored_angles = state.angles;
    wire->position.base = state.origin;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, state.actor, &body, error))
        return q3_rollback_spawn(game, state.actor, error);
    body.origin = state.origin;
    if (!qa_world_body_write(game->options.services.world, state.actor, &body, error))
        return q3_rollback_spawn(game, state.actor, error);
    uint32_t item_index = 0;
    if (qa_q3_find_item(game->options.product, classname, &item_index)) {
        if (!q3_map_register_item(game, item_index, error))
            return false;
        if (game->map->options.item_disabled &&
            game->map->options.item_disabled(game->map->options.context, item_index)) {
            double wait, random;
            if (!q3_map_number(fields, "wait", 0, &wait, error) ||
                !q3_map_number(fields, "random", 0, &random, error))
                return q3_rollback_spawn(game, state.actor, error);
            state.wait = (float)wait;
            state.random = (float)random;
            state.kind = QA_Q3_MAP_POINT;
            if (!q3_map_allocate(game, &state, NULL, false, error) ||
                !q3_wire_entity_ready(game, state.actor, error))
                return q3_rollback_spawn(game, state.actor, error);
            *out = (qa_q3_map_spawn_result){.status = QA_Q3_MAP_FILTERED,
                                            .filter = QA_Q3_MAP_FILTER_DISABLED_ITEM,
                                            .actor = state.actor,
                                            .classname = state.classname};
            return true;
        }
        if (!q3_map_spawn_item(game, fields, &state, item_index, error))
            return false;
    } else if (game->options.product == QA_Q3_TEAM_ARENA &&
               (!strcmp(classname, "team_redobelisk") ||
                !strcmp(classname, "team_blueobelisk") ||
                !strcmp(classname, "team_neutralobelisk"))) {
        state.kind = QA_Q3_MAP_POINT;
        if (!q3_map_allocate(game, &state, NULL, false, error))
            return false;
        int32_t team = !strcmp(classname, "team_redobelisk") ? 1 :
            !strcmp(classname, "team_blueobelisk") ? 2 : 0;
        qa_actor_id trigger;
        if (!qa_q3_source_obelisk_spawn(game, state.actor, team,
                                         state.spawnflags, &trigger, error))
            return false;
        if (!qa_actors_get(qa_session_actors(game->options.services.session), state.actor))
            state.actor = (qa_actor_id){0};
        else {
            qa_q3_map_actor_state *stored = q3_map_get(game, state.actor);
            qa_linked_body linked;
            if (stored)
                stored->linked = qa_world_linked(game->options.services.world,
                                                 state.actor, &linked);
        }
    } else if (point_class(classname)) {
        state.kind = QA_Q3_MAP_POINT;
        if (!strcmp(classname, "info_player_start")) {
            if (!q3_map_intern_cstr(game, "info_player_deathmatch", &state.classname, error))
                return q3_rollback_spawn(game, state.actor, error);
            if (!qa_actors_set_metadata(qa_session_actor_registry(game->options.services.session),
                    state.actor, game->options.owner, state.classname, error))
                return q3_rollback_spawn(game, state.actor, error);
            game->source_entities[source_slot].classname = state.classname;
        }
        if (!strcmp(classname, "info_player_start") ||
            !strcmp(classname, "info_player_deathmatch")) {
            int32_t value;
            q3_map_integer(fields, "nobots", 0, &value);
            state.no_bots = value != 0;
            q3_map_integer(fields, "nohumans", 0, &value);
            state.no_humans = value != 0;
        }
        if (!q3_map_allocate(game, &state, NULL, false, error))
            return false;
        if (!q3_wire_entity_ready(game, state.actor, error))
            return q3_rollback_spawn(game, state.actor, error);
    } else if (!strcmp(classname, "info_null") || !strcmp(classname, "func_group") ||
               !strcmp(classname, "light") || !strcmp(classname, "misc_model")) {
        *out = (qa_q3_map_spawn_result){.status = QA_Q3_MAP_SPAWNED,
                                        .classname = state.classname};
        return qa_session_release(game->options.services.session, state.actor, error);
    } else if (target_class(classname)) {
        if (!q3_map_spawn_target(game, fields, &state, error))
            return false;
    } else if (trigger_class(classname)) {
        if (!q3_map_spawn_trigger(game, fields, &state, error))
            return false;
    } else if (mover_class(classname)) {
        if (!q3_map_spawn_mover(game, fields, &state, error))
            return false;
    } else if (misc_class(classname)) {
        if (!q3_map_spawn_misc(game, &state, error))
            return false;
    } else {
        q3_map_warn(game, (qa_actor_id){0}, "Q3 classname has no native spawn function");
        *out = (qa_q3_map_spawn_result){.status = QA_Q3_MAP_UNKNOWN,
                                        .classname = state.classname};
        return qa_session_release(game->options.services.session, state.actor, error);
    }
    *out = (qa_q3_map_spawn_result){.status = QA_Q3_MAP_SPAWNED,
                                    .actor = state.actor,
                                    .classname = state.classname};
    return true;
}

bool qa_q3_map_spawn(qa_q3_game *game, const qa_q3_map_fields *fields,
                     qa_q3_map_spawn_result *out, qa_error *error) {
    if (!game || !fields || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_map_fail(error, "invalid Q3 authored spawn boundary");
    qa_q3_map_fields captured = *fields;
    ++game->observation_depth;
    bool result = map_spawn(game, &captured, out, error);
    --game->observation_depth;
    return result;
}

static bool maps_post_spawn(qa_q3_game *game, qa_error *error) {
    if (!game || !game->map || !game->map->world_spawned || game->map->post_spawned)
        return q3_map_fail(error, "invalid Q3 map post-spawn phase");
    size_t count = 0;
    for (uint32_t i = 0; i < game->source_count; ++i)
        if (q3_map_get(game, game->source_entities[i].actor))
            ++count;
    qa_q3_map_actor_state **ordered = count ? malloc(count * sizeof(*ordered)) : NULL;
    if (count && !ordered) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 map source-order view");
        return false;
    }
    size_t n = 0;
    for (uint32_t i = 0; i < game->source_count; ++i) {
        qa_q3_map_actor_state *state = q3_map_get(game, game->source_entities[i].actor);
        if (state)
            ordered[n++] = state;
    }
    for (size_t i = 0; i < count; ++i) {
        ordered[i]->team_master = (qa_actor_id){0};
        ordered[i]->team_next = (qa_actor_id){0};
        ordered[i]->team_slave = false;
    }
    for (size_t i = 0; i < count; ++i) {
        qa_q3_map_actor_state *master = ordered[i];
        if (!q3_map_text(game, master->team) || master->team_slave)
            continue;
        master->team_master = master->actor;
        for (size_t j = i + 1; j < count; ++j) {
            qa_q3_map_actor_state *member = ordered[j];
            if (member->team_slave || member->team != master->team)
                continue;
            member->team_next = master->team_next;
            master->team_next = member->actor;
            member->team_master = master->actor;
            member->team_slave = true;
            if (q3_map_text(game, member->targetname)) {
                master->targetname = member->targetname;
                member->targetname = QA_STRING_NONE;
            }
        }
    }
    free(ordered);
    qa_targets_changed(game->map->options.targets);
    if (!q3_map_mover_post_spawn(game, error))
        return false;
    game->map->post_spawned = true;
    if (qa_game_type_is_team(game->options.rules.game_type) && game->options.hooks.source_team_items &&
        !game->options.hooks.source_team_items(game->options.hooks.context, error))
        return false;
    size_t item_count;
    (void)qa_q3_items(game->options.product, &item_count);
    uint8_t registered[64];
    for (size_t i = 0; i < item_count; ++i)
        registered[i] = (game->map->registered_items & (UINT64_C(1) << i)) ? '1' : '0';
    qa_string_id item_config;
    if (!q3_map_intern(game, (qa_bytes){registered, item_count}, &item_config, error) ||
        !emit_text(game, QA_Q3_MAP_CONFIGSTRING, 27, NULL, item_config, error))
        return false;
    uint32_t cursor = 0;
    qa_q3_map_spawnpoint point;
    while (qa_q3_map_spawnpoint_next(game, &cursor, &point)) {
        qa_q3_map_event event = {.kind = QA_Q3_MAP_SPAWNPOINT,
                                 .actor = point.actor,
                                 .origin = point.origin,
                                 .angles = point.angles,
                                 .index = (int32_t)point.kind,
                                 .flags = point.flags,
                                 .no_bots = point.no_bots,
                                 .no_humans = point.no_humans};
        if (!q3_map_emit(game, &event, error))
            return false;
    }
    return true;
}

bool qa_q3_maps_post_spawn(qa_q3_game *game, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_map_fail(error, "invalid Q3 authored post-spawn boundary");
    ++game->observation_depth;
    bool result = maps_post_spawn(game, error);
    if (result)
        game->map->loaded_game_type = game->options.rules.game_type;
    --game->observation_depth;
    return result;
}
