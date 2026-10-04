#include "internal.h"

static char *text(qa_bytes bytes, qa_error *error) {
    if ((bytes.size && !bytes.data) || bytes.size == SIZE_MAX ||
        (bytes.size && memchr(bytes.data, 0, bytes.size))) {
        qc_game_fail(error, QA_ERROR_FORMAT, "Invalid QC entity text span"); return NULL;
    }
    char *copy = malloc(bytes.size + 1);
    if (!copy) { qc_game_fail(error, QA_ERROR_MEMORY, "Allocating QC entity text"); return NULL; }
    if (bytes.size) memcpy(copy, bytes.data, bytes.size);
    copy[bytes.size] = 0; return copy;
}
static bool pair(qa_qc_game *game, int32_t entity, const qa_qc_definition *def,
                 char *value, bool angle, qa_error *error) {
    switch (def->type) {
    case QA_QC_VOID: return true;
    case QA_QC_STRING: {
        size_t read = 0, written = 0;
        while (value[read]) {
            char c = value[read++];
            if (c == '\\') {
                c = value[read] == 'n' ? '\n' : '\\';
                if (value[read]) ++read;
            }
            value[written++] = c;
        }
        value[written] = 0;
        int32_t id;
        return qa_qc_string_allocate(game->vm, value, &id, error) &&
               qa_qc_set_entity_int(game->vm, entity, def->offset, id, error);
    }
    case QA_QC_FLOAT:
        return qa_qc_set_entity_float(game->vm, entity, def->offset,
            (float)qa_parse_quake_number(value, QA_QUAKE_NUMBER_DIGIT_FIRST), error);
    case QA_QC_VECTOR: {
        float components[3] = {0};
        if (angle) components[1] = (float)qa_parse_quake_number(value, QA_QUAKE_NUMBER_DIGIT_FIRST);
        else {
            char *start = value;
            for (unsigned i = 0; i < 3; ++i) {
                char *end = strchr(start, ' ');
                if (end) *end = 0;
                components[i] = (float)qa_parse_quake_number(start, QA_QUAKE_NUMBER_DIGIT_FIRST);
                if (!end) break;
                start = end + 1;
            }
        }
        return qa_qc_set_entity_vector(game->vm, entity, def->offset,
            qa_v3(components[0], components[1], components[2]), error);
    }
    case QA_QC_ENTITY: {
        float slot = (float)qa_parse_quake_number(value, QA_QUAKE_NUMBER_DIGIT_FIRST);
        if (!isfinite(slot) || slot < 0 || (double)slot >= game->options.vm.entity_capacity)
            return qc_game_fail(error, QA_ERROR_FORMAT, "QC map entity reference exceeds slot capacity");
        qa_qc_entity_layout layout = game->options.vm.entity_layout;
        if (!layout.stride_bytes) layout = qa_qc_default_entity_layout(game->program, game->options.vm.profile);
        int32_t reference = (int32_t)((uint64_t)(uint32_t)slot * layout.stride_bytes);
        return qa_qc_set_entity_int(game->vm, entity, def->offset, reference, error);
    }
    case QA_QC_FUNCTION: {
        uint32_t index;
        if (!qa_qc_program_find_function(game->program, value, &index))
            return qc_game_fail(error, QA_ERROR_FORMAT, "QC map function value is absent");
        return qa_qc_set_entity_int(game->vm, entity, def->offset, (int32_t)index, error);
    }
    case QA_QC_FIELD: {
        const qa_qc_definition *field = qa_qc_program_find_field(game->program, value);
        int32_t offset;
        if (!field) return qc_game_fail(error, QA_ERROR_FORMAT, "QC map field value is absent");
        return qa_qc_global_int(game->vm, field->offset, &offset, error) &&
               qa_qc_set_entity_int(game->vm, entity, def->offset, offset, error);
    }
    case QA_QC_POINTER: case QA_QC_OPAQUE:
        return qc_game_fail(error, QA_ERROR_UNSUPPORTED, "QC opaque map fields require raw checkpoint state");
    }
    return qc_game_fail(error, QA_ERROR_FORMAT, "Unknown QC map field type");
}
bool qa_qc_game_spawn_entity(qa_qc_game *game, bool world, const qa_entity_property *properties,
                              size_t count, int32_t *out, qa_error *error) {
    if (!game || !out || (count && !properties) || game->calls == SIZE_MAX || !game->loading)
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "Invalid QC map entity admission");
    ++game->calls;
    int32_t entity = 0;
    bool ok = world || qa_qc_spawn_entity(game->vm, &entity, error);
    for (size_t i = 0; ok && i < count; ++i) {
        char *key = text(properties[i].key, error), *value = text(properties[i].value, error);
        if (!key || !value) { free(key); free(value); ok = false; break; }
        bool angle = strcmp(key, "angle") == 0;
        const char *name = angle ? "angles" : strcmp(key, "light") == 0 ? "light_lev" : key;
        if (name == key && game->options.vm.profile != QA_QC_QUAKEWORLD) {
            size_t length = strlen(key); while (length && key[length - 1] == ' ') key[--length] = 0;
        }
        const qa_qc_definition *def = *name == '_' ? NULL : qa_qc_program_find_field(game->program, name);
        if (def) ok = pair(game, entity, def, value, angle, error);
        free(key); free(value);
    }
    const qa_qc_definition *classname = qa_qc_program_find_field(game->program, "classname");
    const qa_qc_definition *spawnflags = qa_qc_program_find_field(game->program, "spawnflags");
    float flags = 0;
    if (ok && !world && spawnflags && spawnflags->type == QA_QC_FLOAT)
        ok = qa_qc_entity_float(game->vm, entity, spawnflags->offset, &flags, error);
    if (ok && !world && isfinite(flags) && (double)flags >= INT32_MIN &&
        (double)flags <= INT32_MAX && ((uint32_t)(int32_t)flags & game->options.map_exclusion_flags)) {
        ok = qa_qc_remove_entity(game->vm, entity, error);
        if (ok) *out = 0;
        --game->calls;
        return ok;
    }
    int32_t name_id; const char *borrowed; char *class_name = NULL;
    if (ok) {
        if (!classname || classname->type != QA_QC_STRING)
            ok = qc_game_fail(error, QA_ERROR_FORMAT, "QC program has no classname field");
        else ok = qa_qc_entity_int(game->vm, entity, classname->offset, &name_id, error) &&
                  qa_qc_string(game->vm, name_id, &borrowed, error);
        if (ok) { class_name = text((qa_bytes){(const uint8_t *)borrowed, strlen(borrowed)}, error); ok = class_name != NULL; }
    }
    if (ok) {
        uint32_t function;
        if (!*class_name || !qa_qc_program_find_function(game->program, class_name, &function)) {
            if (world) {
                free(class_name); --game->calls;
                return qc_game_fail(error, QA_ERROR_FORMAT, "QC world has no spawn function");
            }
            qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .family = QA_GAME_Q1,
                                      .provider = game->options.vm.host.owner, .flags = 1};
            ok = qa_builtin_resource(&game->options.services, class_name, &event.text, error) &&
                 qa_builtin_emit(&game->options.services, &event, error);
            if (ok && !world) ok = qa_qc_remove_entity(game->vm, entity, error);
            if (ok) *out = 0;
            free(class_name); --game->calls; return ok;
        }
        const qa_qc_definition *self = qa_qc_program_find_global(game->program, "self");
        int32_t old_self;
        uint32_t staged = (uint32_t)entity, saved;
        if (!self || self->type != QA_QC_ENTITY || !qa_qc_global_int(game->vm, self->offset, &old_self, error)) ok = false;
        else {
            memcpy(&saved, &old_self, sizeof(saved));
            ok = qa_qc_stage_globals(game->vm, self->offset, &staged, 1, error) &&
                 qa_qc_game_call(game, class_name, NULL, 0, NULL, 0, NULL, error);
            qa_error restore_error = {0};
            if (!qa_qc_stage_globals(game->vm, self->offset, &saved, 1, &restore_error)) { if (error) *error = restore_error; ok = false; }
        }
    }
    free(class_name);
    if (ok) *out = entity;
    --game->calls;
    return ok;
}
