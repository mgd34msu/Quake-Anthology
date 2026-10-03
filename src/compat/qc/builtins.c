/* Quake pr_cmds.c builtins and compatibility profiles. */
#include "internal.h"

#include <math.h>
#include <stdio.h>

#define R(number_, builtin_, name_) {number_, builtin_, name_}
#define COMMON_ENTRIES \
    R(2,QA_QC_BUILTIN_SETORIGIN,"setorigin"), R(3,QA_QC_BUILTIN_SETMODEL,"setmodel"), \
    R(4,QA_QC_BUILTIN_SETSIZE,"setsize"), R(6,QA_QC_BUILTIN_BREAK,"break"), \
    R(8,QA_QC_BUILTIN_SOUND,"sound"), R(11,QA_QC_BUILTIN_OBJERROR,"objerror"), \
    R(14,QA_QC_BUILTIN_SPAWN,"spawn"), R(15,QA_QC_BUILTIN_REMOVE,"remove"), \
    R(16,QA_QC_BUILTIN_TRACELINE,"traceline"), R(17,QA_QC_BUILTIN_CHECKCLIENT,"checkclient"), \
    R(19,QA_QC_BUILTIN_PRECACHE_SOUND,"precache_sound"), R(20,QA_QC_BUILTIN_PRECACHE_MODEL,"precache_model"), \
    R(21,QA_QC_BUILTIN_STUFFCMD,"stuffcmd"), R(22,QA_QC_BUILTIN_FINDRADIUS,"findradius"), \
    R(23,QA_QC_BUILTIN_BPRINT,"bprint"), R(24,QA_QC_BUILTIN_SPRINT,"sprint"), \
    R(25,QA_QC_BUILTIN_DPRINT,"dprint"), R(28,QA_QC_BUILTIN_COREDUMP,"coredump"), \
    R(31,QA_QC_BUILTIN_EPRINT,"eprint"), R(32,QA_QC_BUILTIN_WALKMOVE,"walkmove"), \
    R(34,QA_QC_BUILTIN_DROPTOFLOOR,"droptofloor"), R(35,QA_QC_BUILTIN_LIGHTSTYLE,"lightstyle"), \
    R(40,QA_QC_BUILTIN_CHECKBOTTOM,"checkbottom"), R(41,QA_QC_BUILTIN_POINTCONTENTS,"pointcontents"), \
    R(44,QA_QC_BUILTIN_AIM,"aim"), R(45,QA_QC_BUILTIN_CVAR,"cvar"), \
    R(46,QA_QC_BUILTIN_LOCALCMD,"localcmd"), R(49,QA_QC_BUILTIN_CHANGEYAW,"changeyaw"), \
    R(52,QA_QC_BUILTIN_WRITEBYTE,"WriteByte"), R(53,QA_QC_BUILTIN_WRITECHAR,"WriteChar"), \
    R(54,QA_QC_BUILTIN_WRITESHORT,"WriteShort"), R(55,QA_QC_BUILTIN_WRITELONG,"WriteLong"), \
    R(56,QA_QC_BUILTIN_WRITECOORD,"WriteCoord"), R(57,QA_QC_BUILTIN_WRITEANGLE,"WriteAngle"), \
    R(58,QA_QC_BUILTIN_WRITESTRING,"WriteString"), R(59,QA_QC_BUILTIN_WRITEENTITY,"WriteEntity"), \
    R(67,QA_QC_BUILTIN_MOVETOGOAL,"movetogoal"), R(68,QA_QC_BUILTIN_PRECACHE_FILE,"precache_file"), \
    R(69,QA_QC_BUILTIN_MAKESTATIC,"makestatic"), R(70,QA_QC_BUILTIN_CHANGELEVEL,"changelevel"), \
    R(72,QA_QC_BUILTIN_CVAR_SET,"cvar_set"), R(73,QA_QC_BUILTIN_CENTERPRINT,"centerprint"), \
    R(74,QA_QC_BUILTIN_AMBIENTSOUND,"ambientsound"), R(75,QA_QC_BUILTIN_PRECACHE_MODEL,"precache_model"), \
    R(76,QA_QC_BUILTIN_PRECACHE_SOUND,"precache_sound"), R(77,QA_QC_BUILTIN_PRECACHE_FILE,"precache_file"), \
    R(78,QA_QC_BUILTIN_SETSPAWNPARMS,"setspawnparms")

static const qa_qc_builtin_requirement netquake_requirements[] = {
    COMMON_ENTRIES,
    R(48,QA_QC_BUILTIN_PARTICLE,"particle")
};

static const qa_qc_builtin_requirement quakeworld_requirements[] = {
    COMMON_ENTRIES,
    R(79,QA_QC_BUILTIN_LOGFRAG,"logfrag"),
    R(80,QA_QC_BUILTIN_INFOKEY,"infokey"),
    R(82,QA_QC_BUILTIN_MULTICAST,"multicast")
};

static const qa_qc_builtin_requirement rerelease_requirements[] = {
    COMMON_ENTRIES,
    R(48,QA_QC_BUILTIN_PARTICLE,"particle"),
    R(401,QA_QC_BUILTIN_SETCOLOR,"setcolor"),
    R(0,QA_QC_BUILTIN_EX_BPRINT,"ex_bprint"),
    R(0,QA_QC_BUILTIN_EX_SPRINT,"ex_sprint"),
    R(0,QA_QC_BUILTIN_EX_CENTERPRINT,"ex_centerprint"),
    R(0,QA_QC_BUILTIN_EX_FINALE_FINISHED,"ex_finaleFinished"),
    R(0,QA_QC_BUILTIN_EX_LOCALSOUND,"ex_localsound"),
    R(0,QA_QC_BUILTIN_EX_DRAW_POINT,"ex_draw_point"),
    R(0,QA_QC_BUILTIN_EX_DRAW_LINE,"ex_draw_line"),
    R(0,QA_QC_BUILTIN_EX_DRAW_ARROW,"ex_draw_arrow"),
    R(0,QA_QC_BUILTIN_EX_DRAW_RAY,"ex_draw_ray"),
    R(0,QA_QC_BUILTIN_EX_DRAW_CIRCLE,"ex_draw_circle"),
    R(0,QA_QC_BUILTIN_EX_DRAW_BOUNDS,"ex_draw_bounds"),
    R(0,QA_QC_BUILTIN_EX_DRAW_WORLDTEXT,"ex_draw_worldtext"),
    R(0,QA_QC_BUILTIN_EX_DRAW_SPHERE,"ex_draw_sphere"),
    R(0,QA_QC_BUILTIN_EX_DRAW_CYLINDER,"ex_draw_cylinder"),
    R(0,QA_QC_BUILTIN_EX_BOT_MOVETOPOINT,"ex_bot_movetopoint"),
    R(0,QA_QC_BUILTIN_EX_BOT_FOLLOWENTITY,"ex_bot_followentity"),
    R(0,QA_QC_BUILTIN_EX_CHECK_PLAYER_FLAGS,"ex_CheckPlayerEXFlags"),
    R(0,QA_QC_BUILTIN_EX_WALKPATHTOGOAL,"ex_walkpathtogoal"),
    R(0,QA_QC_BUILTIN_EX_PROMPT,"ex_prompt"),
    R(0,QA_QC_BUILTIN_EX_PROMPTCHOICE,"ex_promptchoice"),
    R(0,QA_QC_BUILTIN_EX_CLEARPROMPT,"ex_clearprompt")
};
#undef COMMON_ENTRIES
#undef R

const qa_qc_builtin_requirement *qa_qc_builtin_requirements(qa_qc_profile profile,
                                                             size_t *count)
{
    const qa_qc_builtin_requirement *result;
    size_t length;
    if (profile == QA_QC_QUAKEWORLD) {
        result = quakeworld_requirements;
        length = sizeof(quakeworld_requirements) / sizeof(*quakeworld_requirements);
    } else if (profile == QA_QC_RERELEASE) {
        result = rerelease_requirements;
        length = sizeof(rerelease_requirements) / sizeof(*rerelease_requirements);
    } else if (profile == QA_QC_NETQUAKE) {
        result = netquake_requirements;
        length = sizeof(netquake_requirements) / sizeof(*netquake_requirements);
    } else {
        result = NULL;
        length = 0;
    }
    if (count != NULL) *count = length;
    return result;
}

const qa_qc_builtin_requirement *qc_builtin_number(qa_qc_profile profile,
                                                    int32_t number)
{
    size_t count;
    const qa_qc_builtin_requirement *requirements = qa_qc_builtin_requirements(profile, &count);
    for (size_t i = 0; i < count; ++i)
        if (requirements[i].number == number) return &requirements[i];
    return NULL;
}

const qa_qc_builtin_requirement *qc_builtin_name(qa_qc_profile profile,
                                                  const char *name)
{
    size_t count;
    const qa_qc_builtin_requirement *requirements = qa_qc_builtin_requirements(profile, &count);
    for (size_t i = 0; i < count; ++i)
        if (requirements[i].number == 0 && strcmp(requirements[i].name, name) == 0)
            return &requirements[i];
    return NULL;
}

static const qa_qc_builtin_binding *binding(const qa_qc_instance *instance,
                                             qa_qc_builtin builtin,
                                             const char *name)
{
    bool named_request = builtin == QA_QC_BUILTIN_NAMED
        || (builtin >= QA_QC_BUILTIN_EX_BPRINT
            && builtin <= QA_QC_BUILTIN_EX_CLEARPROMPT);
    for (size_t i = 0; i < instance->options.host.builtin_count; ++i) {
        const qa_qc_builtin_binding *candidate = &instance->bindings[i];
        if (candidate->call == NULL) continue;
        if (candidate->builtin == builtin && builtin != QA_QC_BUILTIN_NAMED) return candidate;
        if (named_request && candidate->builtin == QA_QC_BUILTIN_NAMED && name != NULL
            && candidate->name != NULL && strcmp(candidate->name, name) == 0) return candidate;
    }
    return NULL;
}

static bool shared_builtin(const qa_qc_instance *instance, qa_qc_builtin builtin)
{
    switch (builtin) {
    case QA_QC_BUILTIN_SETORIGIN: case QA_QC_BUILTIN_SETSIZE:
    case QA_QC_BUILTIN_TRACELINE: case QA_QC_BUILTIN_DROPTOFLOOR:
    case QA_QC_BUILTIN_POINTCONTENTS:
        return instance->options.host.world != NULL;
    case QA_QC_BUILTIN_SPAWN: case QA_QC_BUILTIN_REMOVE:
        return instance->options.host.session != NULL
            && instance->options.host.owner != 0;
    case QA_QC_BUILTIN_FINDRADIUS:
        return instance->options.host.world != NULL
            && instance->options.host.session != NULL;
    default: return false;
    }
}

bool qc_builtin_available(const qa_qc_instance *instance,
                          const qa_qc_builtin_requirement *requirement)
{
    return requirement != NULL
        && (binding(instance, requirement->builtin, requirement->name) != NULL
            || shared_builtin(instance, requirement->builtin));
}

bool qc_builtin_is_pure(qa_qc_profile profile, int32_t number)
{
    switch (number) {
    case 1: case 7: case 9: case 10: case 12: case 13: case 18:
    case 26: case 27: case 29: case 30: case 36: case 37: case 38:
    case 43: case 47: case 51: case 99: return true;
    case 81: return profile == QA_QC_QUAKEWORLD;
    default: return false;
    }
}

static bool set_named_vector(qa_qc_instance *instance, const char *name,
                             qa_vec3 value, qa_error *error)
{
    const qa_qc_definition *definition = qa_qc_program_find_global(instance->program, name);
    if (definition == NULL || definition->type != QA_QC_VECTOR)
        return qc_fail(error, QA_ERROR_FORMAT, 0, "QuakeC vector global is missing");
    return qa_qc_set_global_vector(instance, definition->offset, value, error);
}

static bool makevectors(qa_qc_instance *instance, qa_error *error)
{
    qa_vec3 angles;
    if (!qa_qc_arg_vector(instance, 0, &angles, error)) return false;
    const float degrees = 0.01745329251994329577f;
    float sy = sinf(angles.y * degrees), cy = cosf(angles.y * degrees);
    float sp = sinf(angles.x * degrees), cp = cosf(angles.x * degrees);
    float sr = sinf(angles.z * degrees), cr = cosf(angles.z * degrees);
    qa_vec3 forward = qa_v3(cp * cy, cp * sy, -sp);
    qa_vec3 right = qa_v3((-sr * sp * cy) + cr * sy,
                          (-sr * sp * sy) - cr * cy, -sr * cp);
    qa_vec3 up = qa_v3((cr * sp * cy) + sr * sy,
                       (cr * sp * sy) - sr * cy, cr * cp);
    return set_named_vector(instance, "v_forward", forward, error)
        && set_named_vector(instance, "v_right", right, error)
        && set_named_vector(instance, "v_up", up, error);
}

static uint32_t random_word(qa_qc_instance *instance)
{
    if (instance->options.host.random_u32 != NULL)
        return instance->options.host.random_u32(instance->options.host.context);
    uint32_t value = instance->random_state;
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    instance->random_state = value == 0 ? UINT32_C(0x6d2b79f5) : value;
    return instance->random_state;
}

static int32_t builtin_float_int(float value)
{
    if (!isfinite(value)) return INT32_MIN;
    double wrapped = fmod(trunc((double)value), 4294967296.0);
    if (wrapped < 0) wrapped += 4294967296.0;
    uint32_t bits = (uint32_t)wrapped;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static bool var_string(qa_qc_instance *instance, char **out, qa_error *error)
{
    size_t total = 1;
    for (uint32_t i = 0; i < instance->argument_count; ++i) {
        const char *part;
        if (!qa_qc_arg_string(instance, i, &part, error)) return false;
        size_t length = strlen(part);
        if (length > SIZE_MAX - total) return qc_fail(error, QA_ERROR_MEMORY, 0, "QuakeC string overflow");
        total += length;
    }
    char *text = malloc(total);
    if (text == NULL) return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate QuakeC message");
    size_t used = 0;
    for (uint32_t i = 0; i < instance->argument_count; ++i) {
        const char *part;
        if (!qa_qc_arg_string(instance, i, &part, error)) { free(text); return false; }
        size_t length = strlen(part);
        memcpy(text + used, part, length); used += length;
    }
    text[used] = '\0';
    *out = text;
    return true;
}

static bool find_entity(qa_qc_instance *instance, bool next_only, qa_error *error)
{
    int32_t reference;
    if (!qa_qc_arg_int(instance, 0, &reference, error)) return false;
    uint32_t start;
    if (!qc_entity_slot(instance, reference, &start, error)) return false;
    int32_t field = 0;
    const char *match = NULL;
    if (!next_only && (!qa_qc_arg_int(instance, 1, &field, error)
        || !qa_qc_arg_string(instance, 2, &match, error))) return false;
    if (!next_only && (field < 0 || (uint32_t)field >= instance->layout.field_words))
        return qc_fail(error, QA_ERROR_FORMAT, 0, "find field is outside entity memory");
    if (!next_only) {
        bool string_field = false;
        for (uint32_t i = 0; i < instance->program->info.field_count; ++i) {
            const qa_qc_definition *definition = &instance->program->fields[i];
            if (definition->offset == (uint32_t)field && definition->type == QA_QC_STRING) {
                string_field = true; break;
            }
        }
        if (!string_field) return qc_fail(error, QA_ERROR_FORMAT, 0, "find requires a string field");
    }
    for (uint32_t slot = start + 1u; slot < instance->entity_count; ++slot) {
        if (instance->slots[slot].kind == QA_QC_SLOT_FREE) continue;
        if (!next_only) {
            if (!qc_prepare_entity_access(instance, slot, (uint32_t)field, 1u,
                                          QA_QC_ENTITY_READ, error)) return false;
            int32_t id = qc_load_int(qc_entity_words(instance, slot), (uint32_t)field);
            const char *text;
            if (id == 0 || !qc_strings_get(&instance->strings, id, &text, error)) {
                if (id == 0) continue;
                return false;
            }
            if (strcmp(text, match) != 0) continue;
        }
        return qa_qc_return_int(instance, (int32_t)((uint64_t)slot * instance->layout.stride_bytes), error);
    }
    return qa_qc_return_int(instance, 0, error);
}

static bool pure_builtin(qa_qc_instance *instance, int32_t number,
                         qa_error *error)
{
    /* A NULL instance queries the same dispatch cases for return-word-only effects. */
    float value;
    qa_vec3 vector;
    char text[128];
    int32_t id;
    switch (number) {
    case 1: return instance && makevectors(instance, error);
    case 7:
        if (!instance) return true;
        return qa_qc_return_float(instance,
                    (float)(random_word(instance) & 0x7fffu) / 32767.0f, error);
    case 9:
        if (!instance) return true;
        if (!qa_qc_arg_vector(instance, 0, &vector, error)) return false;
        return qa_qc_return_vector(instance, qa_vec_normalize(vector), error);
    case 10: {
        if (!instance) return false;
        char *message;
        if (!var_string(instance, &message, error)) return false;
        qa_error_set(error, QA_ERROR_FORMAT, 0, "QuakeC error: %s", message);
        free(message);
        return false;
    }
    case 12:
        if (!instance) return true;
        return qa_qc_arg_vector(instance, 0, &vector, error)
            && qa_qc_return_float(instance, qa_vec_length(vector), error);
    case 13:
        if (!instance) return true;
        if (!qa_qc_arg_vector(instance, 0, &vector, error)) return false;
        if (vector.x == 0 && vector.y == 0) value = 0;
        else { value = truncf(atan2f(vector.y, vector.x) * 57.29577951308232f); if (value < 0) value += 360; }
        return qa_qc_return_float(instance, value, error);
    case 18: return instance && find_entity(instance, false, error);
    case 26:
        if (!instance) return false;
        if (!qa_qc_arg_float(instance, 0, &value, error)) return false;
        if (value == truncf(value)) snprintf(text, sizeof(text), "%d", builtin_float_int(value));
        else snprintf(text, sizeof(text), "%5.1f", (double)value);
        return qa_qc_engine_string(instance, "pr_string_temp", text, 128, &id, error)
            && qa_qc_return_int(instance, id, error);
    case 27:
        if (!instance) return false;
        if (!qa_qc_arg_vector(instance, 0, &vector, error)) return false;
        snprintf(text, sizeof(text), "'%5.1f %5.1f %5.1f'",
                 (double)vector.x, (double)vector.y, (double)vector.z);
        return qa_qc_engine_string(instance, "pr_string_temp", text, 128, &id, error)
            && qa_qc_return_int(instance, id, error);
    case 29: if (!instance) return false; instance->trace_enabled = true; return true;
    case 30: if (!instance) return false; instance->trace_enabled = false; return true;
    case 36:
        if (!instance) return true;
        return qa_qc_arg_float(instance, 0, &value, error)
            && qa_qc_return_float(instance, truncf(value > 0 ? value + 0.5f : value - 0.5f), error);
    case 37:
        if (!instance) return true;
        return qa_qc_arg_float(instance, 0, &value, error)
            && qa_qc_return_float(instance, floorf(value), error);
    case 38:
        if (!instance) return true;
        return qa_qc_arg_float(instance, 0, &value, error)
            && qa_qc_return_float(instance, ceilf(value), error);
    case 43:
        if (!instance) return true;
        return qa_qc_arg_float(instance, 0, &value, error)
            && qa_qc_return_float(instance, fabsf(value), error);
    case 47: return instance && find_entity(instance, true, error);
    case 51: {
        if (!instance) return true;
        if (!qa_qc_arg_vector(instance, 0, &vector, error)) return false;
        float yaw = 0, pitch;
        if (vector.x == 0 && vector.y == 0) pitch = vector.z > 0 ? 90 : 270;
        else {
            yaw = truncf(atan2f(vector.y, vector.x) * 57.29577951308232f);
            if (yaw < 0) yaw += 360;
            pitch = truncf(atan2f(vector.z, sqrtf(vector.x * vector.x + vector.y * vector.y)) * 57.29577951308232f);
            if (pitch < 0) pitch += 360;
        }
        return qa_qc_return_vector(instance, qa_v3(pitch, yaw, 0), error);
    }
    case 81: {
        if (!instance) return true;
        const char *source;
        if (!qa_qc_arg_string(instance, 0, &source, error)) return false;
        char *end;
        value = (float)strtod(source, &end);
        if (end == source) value = 0;
        return qa_qc_return_float(instance, value, error);
    }
    case 99: {
        if (!instance) return true;
        const char *extension;
        if (!qa_qc_arg_string(instance, 0, &extension, error)) return false;
        bool found = false;
        for (size_t i = 0; i < instance->options.host.extension_count; ++i)
            if (strcmp(instance->extensions[i], extension) == 0) { found = true; break; }
        return qa_qc_return_float(instance, found ? 1.0f : 0.0f, error);
    }
    default: return instance && qc_fail(error, QA_ERROR_NOT_FOUND, 0, "unknown pure QuakeC builtin");
    }
}

bool qa_qc_program_function_returns_only(const qa_qc_program *program, uint32_t index)
{
    const qa_qc_function *function = qa_qc_program_function(program, index);
    return function && !function->named_builtin && function->first_statement < 0 &&
        function->first_statement != INT32_MIN && pure_builtin(NULL, -function->first_statement, NULL);
}

bool qc_builtin_call(qa_qc_instance *instance, int32_t number,
                     const char *name, qa_error *error)
{
    if (name == NULL && qc_builtin_is_pure(instance->options.profile, number))
        return pure_builtin(instance, number, error);
    const qa_qc_builtin_requirement *requirement = name == NULL
        ? qc_builtin_number(instance->options.profile, number)
        : qc_builtin_name(instance->options.profile, name);
    if (requirement != NULL) return qc_host_builtin(instance, requirement, error);
    if (name != NULL) {
        const qa_qc_builtin_binding *custom = binding(instance, QA_QC_BUILTIN_NAMED, name);
        if (custom != NULL) {
            qa_error failure = {0};
            ++instance->callback_depth;
            bool ok = custom->call(custom->context, instance,
                                   QA_QC_BUILTIN_NAMED, name, &failure);
            --instance->callback_depth;
            if (ok) return true;
            if (failure.code == QA_OK)
                qa_error_set(&failure, QA_ERROR_ARGUMENT, 0,
                             "QuakeC named builtin callback failed");
            if (error != NULL) *error = failure;
            return false;
        }
    }
    if (instance->options.host.unknown_builtin != NULL) {
        qa_error failure = {0};
        ++instance->callback_depth;
        bool ok = instance->options.host.unknown_builtin(
            instance->options.host.context, instance, number, name, &failure);
        --instance->callback_depth;
        if (ok) return true;
        if (failure.code == QA_OK)
            qa_error_set(&failure, QA_ERROR_UNSUPPORTED, 0,
                         "QuakeC extension builtin callback failed");
        if (error != NULL) *error = failure;
        return false;
    }
    char message[192];
    if (name != NULL) snprintf(message, sizeof(message), "unbound QuakeC builtin %s", name);
    else snprintf(message, sizeof(message), "unbound QuakeC builtin %d", number);
    return qc_fail(error, QA_ERROR_UNSUPPORTED, 0, message);
}
