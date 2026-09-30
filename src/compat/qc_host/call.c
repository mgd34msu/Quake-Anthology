#include "internal.h"

typedef struct staged_global {
    const qa_qc_definition *definition;
    uint32_t old[3], value[3], count;
} staged_global;
static qa_qc_value_type value_type(qa_qc_game_value_kind kind) {
    switch (kind) {
    case QA_QC_GAME_FLOAT: return QA_QC_FLOAT;
    case QA_QC_GAME_VECTOR: return QA_QC_VECTOR;
    case QA_QC_GAME_STRING: return QA_QC_STRING;
    case QA_QC_GAME_ACTOR: return QA_QC_ENTITY;
    case QA_QC_GAME_FUNCTION: return QA_QC_FUNCTION;
    case QA_QC_GAME_FIELD: return QA_QC_FIELD;
    case QA_QC_GAME_INTEGER: return QA_QC_OPAQUE;
    }
    return QA_QC_VOID;
}
bool qc_game_value(qa_qc_game *game, const qa_qc_game_value *value, uint32_t words[3], qa_error *error) {
    memset(words, 0, 3 * sizeof(*words));
    int32_t integer;
    switch (value->kind) {
    case QA_QC_GAME_FLOAT:
        if (!isfinite(value->value.number)) return qc_game_fail(error, QA_ERROR_ARGUMENT, "Nonfinite QC call argument");
        memcpy(words, &value->value.number, sizeof(float)); return true;
    case QA_QC_GAME_VECTOR: {
        if (!qa_vec_finite(value->value.vector)) return qc_game_fail(error, QA_ERROR_ARGUMENT, "Nonfinite QC call vector");
        float components[3] = {value->value.vector.x, value->value.vector.y, value->value.vector.z};
        memcpy(words, components, sizeof(components)); return true;
    }
    case QA_QC_GAME_STRING:
        if (!value->value.string) return qc_game_fail(error, QA_ERROR_ARGUMENT, "Missing QC string call value");
        {
            static const char prefix[] = "qc-call-value:";
            size_t length = strlen(value->value.string);
            if (length > SIZE_MAX - sizeof(prefix)) return qc_game_fail(error, QA_ERROR_MEMORY, "QC call string name overflow");
            char *name = malloc(length + sizeof(prefix));
            if (!name) return qc_game_fail(error, QA_ERROR_MEMORY, "Allocating QC call string name");
            memcpy(name, prefix, sizeof(prefix) - 1); memcpy(name + sizeof(prefix) - 1, value->value.string, length + 1);
            size_t capacity = length + 1; if (capacity < 128) capacity = 128;
            bool ok = qa_qc_engine_string(game->vm, name, value->value.string, capacity, &integer, error);
            free(name); if (!ok) return false;
        }
        memcpy(words, &integer, sizeof(integer)); return true;
    case QA_QC_GAME_ACTOR:
        integer = 0;
        if (value->value.actor.registry && !qa_qc_actor_reference(game->vm, value->value.actor, true, &integer, error)) return false;
        memcpy(words, &integer, sizeof(integer)); return true;
    case QA_QC_GAME_FUNCTION:
        if (value->value.integer < 0 || !qa_qc_program_function(game->program, (uint32_t)value->value.integer))
            return qc_game_fail(error, QA_ERROR_ARGUMENT, "QC function argument is outside program");
        words[0] = (uint32_t)value->value.integer; return true;
    case QA_QC_GAME_FIELD:
        if (value->value.integer < 0 || (uint32_t)value->value.integer >= qa_qc_program_describe(game->program).entity_field_words)
            return qc_game_fail(error, QA_ERROR_ARGUMENT, "QC field argument is outside program");
        words[0] = (uint32_t)value->value.integer; return true;
    case QA_QC_GAME_INTEGER: words[0] = (uint32_t)value->value.integer; return true;
    }
    return qc_game_fail(error, QA_ERROR_ARGUMENT, "Invalid QC call value kind");
}
static bool read_words(qa_qc_instance *vm, uint32_t offset, uint32_t count, uint32_t *out, qa_error *error) {
    for (uint32_t i = 0; i < count; ++i) {
        int32_t word;
        if (!qa_qc_global_int(vm, offset + i, &word, error)) return false;
        memcpy(out + i, &word, sizeof(word));
    }
    return true;
}
bool qa_qc_game_call_index(qa_qc_game *game, uint32_t index, const qa_qc_game_value *arguments,
                      size_t count, const qa_qc_game_global *globals, size_t global_count,
                      uint32_t result[3], qa_error *error) {
    if (!game || count > 8 || (count && !arguments) || (global_count && !globals) ||
        global_count > SIZE_MAX / sizeof(staged_global) || game->calls == SIZE_MAX)
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "Invalid QC source callback declaration");
    const qa_qc_function *fn = qa_qc_program_function(game->program, index);
    if (!fn || !index || fn->first_statement < 0 || fn->named_builtin || fn->parameter_count != count)
        return qc_game_fail(error, QA_ERROR_FORMAT, "QC source callback signature differs");
    ++game->calls;
    uint32_t args[8][3], reserved[27];
    staged_global local[16];
    staged_global *saved = global_count <= 16 ? local : calloc(global_count, sizeof(*saved));
    bool ok = saved != NULL;
    if (!ok) qc_game_fail(error, QA_ERROR_MEMORY, "Allocating QC callback global staging");
    for (size_t i = 0; ok && i < count; ++i) {
        if (fn->parameter_sizes[i] != (arguments[i].kind == QA_QC_GAME_VECTOR ? 3 : 1))
            ok = qc_game_fail(error, QA_ERROR_FORMAT, "QC callback parameter width differs");
        else ok = qc_game_value(game, &arguments[i], args[i], error);
    }
    for (size_t i = 0; ok && i < global_count; ++i) {
        const qa_qc_definition *def = globals[i].name ? qa_qc_program_find_global(game->program, globals[i].name) : NULL;
        uint32_t width = globals[i].value.kind == QA_QC_GAME_VECTOR ? 3u : 1u;
        if (!def || def->offset < 28 || def->type != value_type(globals[i].value.kind)) {
            ok = qc_game_fail(error, QA_ERROR_FORMAT, "QC callback global type differs"); break;
        }
        for (size_t j = 0; j < i; ++j)
            if (def->offset < saved[j].definition->offset + saved[j].count && saved[j].definition->offset < def->offset + width) {
                ok = qc_game_fail(error, QA_ERROR_FORMAT, "QC callback globals overlap"); break;
            }
        if (!ok) break;
        saved[i].definition = def; saved[i].count = width;
        ok = read_words(game->vm, def->offset, width, saved[i].old, error) &&
             qc_game_value(game, &globals[i].value, saved[i].value, error);
    }
    if (ok) ok = read_words(game->vm, 1, 27, reserved, error);
    bool staged = ok;
    if (staged) {
        for (size_t i = 0; ok && i < count; ++i)
            ok = qa_qc_stage_globals(game->vm, 4 + (uint32_t)i * 3, args[i], 3, error);
        for (size_t i = 0; ok && i < global_count; ++i)
            ok = qa_qc_stage_globals(game->vm, saved[i].definition->offset, saved[i].value, saved[i].count, error);
        if (ok) ok = qa_qc_execute(game->vm, index, (uint32_t)count, error);
        uint32_t returned[3];
        if (ok) ok = read_words(game->vm, 1, 3, returned, error);
        qa_error restore_error = {0};
        bool restored = qa_qc_stage_globals(game->vm, 1, reserved, 27, &restore_error);
        for (size_t i = 0; i < global_count; ++i)
            if (!qa_qc_stage_globals(game->vm, saved[i].definition->offset, saved[i].old, saved[i].count, &restore_error)) restored = false;
        if (!restored) { if (error) *error = restore_error; ok = false; }
        if (ok && result) memcpy(result, returned, sizeof(returned));
    }
    if (saved != local) free(saved);
    --game->calls; return ok;
}
bool qa_qc_game_call(qa_qc_game *game, const char *name, const qa_qc_game_value *arguments,
                      size_t count, const qa_qc_game_global *globals, size_t global_count,
                      uint32_t result[3], qa_error *error) {
    uint32_t index;
    if (!game || !name || !qa_qc_program_find_function(game->program, name, &index))
        return qc_game_fail(error, QA_ERROR_FORMAT, "QC source callback is absent");
    return qa_qc_game_call_index(game, index, arguments, count, globals, global_count, result, error);
}
bool qa_qc_game_callback(qa_qc_game *game, qa_actor_id self, qa_actor_id other,
                          const char *field_name, qa_error *error) {
    if (!game || !field_name) return qc_game_fail(error, QA_ERROR_ARGUMENT, "Invalid QC entity callback");
    const qa_qc_definition *field = qa_qc_program_find_field(game->program, field_name);
    int32_t reference, function;
    if (!field || field->type != QA_QC_FUNCTION)
        return qc_game_fail(error, QA_ERROR_FORMAT, "QC callback field is missing");
    if (!qa_qc_actor_reference(game->vm, self, false, &reference, error) ||
        !qa_qc_entity_int(game->vm, reference, field->offset, &function, error)) return false;
    if (!function) return true;
    const qa_qc_function *fn = function > 0 ? qa_qc_program_function(game->program, (uint32_t)function) : NULL;
    if (!fn) return qc_game_fail(error, QA_ERROR_FORMAT, "QC callback function is invalid");
    qa_qc_game_global globals[2] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = self}}},
        {"other", {QA_QC_GAME_ACTOR, {.actor = other}}}
    };
    return qa_qc_game_call_index(game, (uint32_t)function, NULL, 0, globals, 2, NULL, error);
}
