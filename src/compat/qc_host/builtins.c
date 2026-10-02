#include "internal.h"

static bool field(qa_qc_game *game, int32_t entity, const char *name, qa_qc_value_type type,
                  const uint32_t words[3], qa_error *error) {
    const qa_qc_definition *def = qa_qc_program_find_field(game->program, name);
    if (!def || def->type != type) return qc_game_fail(error, QA_ERROR_FORMAT, "Required QC engine field is missing");
    if (type == QA_QC_VECTOR) {
        float v[3]; memcpy(v, words, sizeof(v));
        return qa_qc_set_entity_vector(game->vm, entity, def->offset, qa_v3(v[0], v[1], v[2]), error);
    }
    int32_t value; memcpy(&value, words, sizeof(value));
    return qa_qc_set_entity_int(game->vm, entity, def->offset, value, error);
}
static bool vector_field(qa_qc_game *game, int32_t entity, const char *name, qa_vec3 vector, qa_error *error) {
    float v[3] = {vector.x, vector.y, vector.z}; uint32_t words[3]; memcpy(words, v, sizeof(words));
    return field(game, entity, name, QA_QC_VECTOR, words, error);
}
static bool float_field(qa_qc_game *game, int32_t entity, const char *name, float value, qa_error *error) {
    uint32_t words[3] = {0}; memcpy(words, &value, sizeof(value));
    return field(game, entity, name, QA_QC_FLOAT, words, error);
}
static bool self_reference(qa_qc_game *game, int32_t *out, qa_error *error) {
    const qa_qc_definition *self = qa_qc_program_find_global(game->program, "self");
    if (!self || self->type != QA_QC_ENTITY) return qc_game_fail(error, QA_ERROR_FORMAT, "Missing QC self global");
    return qa_qc_global_int(game->vm, self->offset, out, error);
}
static bool concatenate(qa_qc_instance *vm, uint32_t first, char **out, qa_error *error) {
    size_t size = 1; uint32_t count = qa_qc_argument_count(vm);
    for (uint32_t i = first; i < count; ++i) {
        const char *value;
        if (!qa_qc_arg_string(vm, i, &value, error)) return false;
        size_t length = strlen(value);
        if (length > SIZE_MAX - size) {
            qc_game_fail(error, QA_ERROR_MEMORY, "QC print string overflow");
            return false;
        }
        size += length;
    }
    char *text = malloc(size);
    if (!text) {
        qc_game_fail(error, QA_ERROR_MEMORY, "Allocating QC print text");
        return false;
    }
    size_t used = 0;
    for (uint32_t i = first; i < count; ++i) {
        const char *value;
        if (!qa_qc_arg_string(vm, i, &value, error)) { free(text); return false; }
        size_t length = strlen(value); memcpy(text + used, value, length); used += length;
    }
    text[used] = 0; *out = text; return true;
}
static bool resource(qa_qc_game *game, qa_qc_builtin builtin, qa_error *error) {
    const char *name; int32_t id;
    if (!qa_qc_arg_string(game->vm, 0, &name, error) || !qa_qc_arg_int(game->vm, 0, &id, error)) return false;
    if (builtin == QA_QC_BUILTIN_PRECACHE_FILE) return qa_qc_return_int(game->vm, id, error);
    if (!game->loading) return qc_game_fail(error, QA_ERROR_ARGUMENT, "QC precache outside loading");
    qa_qc_game_resource cached;
    return game->options.resource(game->options.context,
        builtin == QA_QC_BUILTIN_PRECACHE_MODEL ? QA_QC_RESOURCE_MODEL : QA_QC_RESOURCE_SOUND,
        name, true, &cached, error) && qa_qc_return_int(game->vm, id, error);
}
static bool same_model_actor(qa_qc_game *game, int32_t entity, qa_actor_id actor, qa_error *error) {
    qa_actor_id current;
    return !entity || (qa_qc_reference_actor(game->vm, entity, &current, error) &&
        (qa_actor_id_equal(current, actor) || qc_game_fail(error, QA_ERROR_NOT_FOUND, "QC model actor changed")));
}
static bool setmodel(qa_qc_game *game, qa_error *error) {
    int32_t entity, model; const char *name;
    if (!qa_qc_arg_int(game->vm, 0, &entity, error) || !qa_qc_arg_int(game->vm, 1, &model, error) ||
        !qa_qc_arg_string(game->vm, 1, &name, error)) return false;
    qa_actor_id actor = {0};
    if (entity && !qa_qc_reference_actor(game->vm, entity, &actor, error)) return false;
    qa_qc_game_resource cached;
    if (!game->options.resource(game->options.context, QA_QC_RESOURCE_MODEL, name, false, &cached, error)) return false;
    if (!qa_vec_finite(cached.bounds.mins) || !qa_vec_finite(cached.bounds.maxs) ||
        cached.bounds.mins.x > cached.bounds.maxs.x || cached.bounds.mins.y > cached.bounds.maxs.y ||
        cached.bounds.mins.z > cached.bounds.maxs.z)
        return qc_game_fail(error, QA_ERROR_FORMAT, "Invalid QC model bounds");
    qa_actor_id current;
    if (entity && (!qa_qc_reference_actor(game->vm, entity, &current, error) ||
        !qa_actor_id_equal(current, actor)))
        return qc_game_fail(error, QA_ERROR_NOT_FOUND, "QC model actor changed during resource lookup");
    const qa_actor_record *record = qa_actors_get(qa_session_actors(game->options.services.session), actor);
    qa_actor_owner execution;
    bool allowed = !entity || (record && (game->options.vm.host.may_move ?
        game->options.vm.host.may_move(game->options.vm.host.context, actor) :
        record->owner == game->options.vm.host.owner ||
            (qa_session_execution(game->options.services.session, actor, &execution) && execution == game->options.vm.host.owner)));
    if (!allowed || !same_model_actor(game, entity, actor, error))
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "QC model cannot alter another movement owner");
    uint32_t words[3] = {(uint32_t)model, 0, 0};
    if (!field(game, entity, "model", QA_QC_STRING, words, error) ||
        !same_model_actor(game, entity, actor, error) ||
        !float_field(game, entity, "modelindex", (float)cached.index, error) ||
        !same_model_actor(game, entity, actor, error) ||
        !vector_field(game, entity, "mins", cached.bounds.mins, error) ||
        !same_model_actor(game, entity, actor, error) ||
        !vector_field(game, entity, "maxs", cached.bounds.maxs, error) ||
        !same_model_actor(game, entity, actor, error) ||
        !vector_field(game, entity, "size", qa_vec_sub(cached.bounds.maxs, cached.bounds.mins), error)) return false;
    if (!entity) return true;
    if (!qa_qc_reference_actor(game->vm, entity, &current, error) || !qa_actor_id_equal(current, actor))
        return qc_game_fail(error, QA_ERROR_NOT_FOUND, "QC model actor changed during field publication");
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error)) return false;
    body.bounds = cached.bounds;
    return qa_world_body_write(game->options.services.world, actor, &body, error) &&
           qa_world_link(game->options.services.world, actor, NULL, error);
}
static bool emit_sound(qa_qc_game *game, bool ambient, qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND, .family = QA_GAME_Q1,
                              .provider = game->options.vm.host.owner};
    const char *name; int32_t entity; float channel = 0;
    if (ambient) {
        if (!qa_qc_arg_vector(game->vm, 0, &event.origin, error)) return false;
    } else if (!qa_qc_arg_int(game->vm, 0, &entity, error) ||
        !qa_qc_reference_actor(game->vm, entity, &event.actor, error) ||
        !qa_qc_arg_float(game->vm, 1, &channel, error)) return false;
    uint32_t sound_arg = ambient ? 1u : 2u;
    if (!qa_qc_arg_string(game->vm, sound_arg, &name, error) ||
        !qa_qc_arg_float(game->vm, sound_arg + 1, &event.volume, error) ||
        !qa_qc_arg_float(game->vm, sound_arg + 2, &event.attenuation, error)) return false;
    if (!isfinite(channel) || channel < 0 || channel > 7 || !isfinite(event.volume) ||
        event.volume < 0 || event.volume > 1 || !isfinite(event.attenuation) ||
        event.attenuation < 0 || event.attenuation > 4 || !qa_vec_finite(event.origin))
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "QC sound parameters outside source bounds");
    event.channel = (int32_t)channel; event.flags = ambient ? 1u : 0u;
    qa_qc_game_resource cached;
    if (!qa_builtin_resource(&game->options.services, name, &event.resource, error) ||
        !game->options.resource(game->options.context, QA_QC_RESOURCE_SOUND, name, false, &cached, error)) return false;
    event.time_ns = qa_session_elapsed(game->options.services.session);
    return qa_builtin_emit(&game->options.services, &event, error);
}
static bool print(qa_qc_game *game, qa_qc_builtin builtin, qa_error *error) {
    if (builtin == QA_QC_BUILTIN_DPRINT) {
        const qa_cvar_view *developer = qa_cvars_find(game->options.cvars, "developer");
        if (!developer || developer->number == 0) return true;
    }
    uint32_t first = 0; float level = 2;
    qa_builtin_event event = {.kind = builtin == QA_QC_BUILTIN_CENTERPRINT ? QA_BUILTIN_CENTERPRINT : QA_BUILTIN_MESSAGE,
                             .family = QA_GAME_Q1, .provider = game->options.vm.host.owner};
    if (builtin == QA_QC_BUILTIN_SPRINT || builtin == QA_QC_BUILTIN_CENTERPRINT) {
        int32_t entity;
        if (!qa_qc_arg_int(game->vm, 0, &entity, error) || !qa_qc_reference_actor(game->vm, entity, &event.actor, error)) return false;
        first = 1;
    }
    if (game->options.vm.profile == QA_QC_QUAKEWORLD && builtin != QA_QC_BUILTIN_CENTERPRINT && builtin != QA_QC_BUILTIN_DPRINT) {
        if (!qa_qc_arg_float(game->vm, first++, &level, error)) return false;
    }
    if (!isfinite(level) || level < INT32_MIN || (double)level > INT32_MAX)
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "Invalid QC print level");
    event.code = (int32_t)level; event.flags = builtin == QA_QC_BUILTIN_DPRINT ? 1u : 0u;
    char *text;
    if (!concatenate(game->vm, first, &text, error)) return false;
    bool ok = qa_builtin_resource(&game->options.services, text, &event.text, error);
    free(text);
    event.time_ns = qa_session_elapsed(game->options.services.session);
    return ok && qa_builtin_emit(&game->options.services, &event, error);
}
static bool movement(qa_qc_game *game, qa_qc_builtin builtin, qa_error *error) {
    qa_physics *physics = game->options.services.physics;
    int32_t reference;
    if (!physics || !physics->services.read)
        return qc_game_fail(error, QA_ERROR_UNSUPPORTED, "QC movement has no shared physics provider");
    if (builtin == QA_QC_BUILTIN_CHECKBOTTOM) {
        if (!qa_qc_arg_int(game->vm, 0, &reference, error)) return false;
    } else if (!self_reference(game, &reference, error)) return false;
    qa_actor_id actor;
    if (!qa_qc_reference_actor(game->vm, reference, &actor, error)) return false;
    qa_physics_properties properties;
    if (!physics->services.read(physics->services.context, actor, &properties) ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
        properties.family != QA_COLLISION_Q1)
        return qc_game_fail(error, QA_ERROR_UNSUPPORTED, "QC movement source properties are unavailable");
    if (builtin == QA_QC_BUILTIN_CHANGEYAW)
        return qa_physics_change_yaw(physics, actor, 0.1f, error);
    if (builtin == QA_QC_BUILTIN_CHECKBOTTOM) {
        qa_body_state body; bool supported;
        return qa_world_body_read(game->options.services.world, actor, &body, error) &&
               qa_physics_check_bottom(physics, actor, body.origin, &supported, error) &&
               qa_qc_return_float(game->vm, supported ? 1 : 0, error);
    }
    if (builtin == QA_QC_BUILTIN_WALKMOVE) {
        float yaw, distance; bool moved;
        return qa_qc_arg_float(game->vm, 0, &yaw, error) && qa_qc_arg_float(game->vm, 1, &distance, error) &&
               qa_physics_walk_move(physics, actor, yaw, distance, 0.1f, true, true, &moved, error) &&
               qa_qc_return_float(game->vm, moved ? 1 : 0, error);
    }
    float distance;
    if (!(properties.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING | QA_PHYSICS_ONGROUND)))
        return true;
    const qa_qc_definition *goal = qa_qc_program_find_field(game->program, "goalentity");
    int32_t goal_reference; qa_actor_id target;
    if (!goal || goal->type != QA_QC_ENTITY)
        return qc_game_fail(error, QA_ERROR_FORMAT, "Missing QC goalentity field");
    return qa_qc_arg_float(game->vm, 0, &distance, error) &&
           qa_qc_entity_int(game->vm, reference, goal->offset, &goal_reference, error) &&
           qa_qc_reference_actor(game->vm, goal_reference, &target, error) &&
           qa_physics_q1_move_to_goal(physics, actor, target, distance, false, error);
}
bool qc_game_builtin(void *context, qa_qc_instance *vm, qa_qc_builtin builtin,
                      const char *name, qa_error *error) {
    (void)name; qa_qc_game *game = context;
    if (vm != game->vm) return qc_game_fail(error, QA_ERROR_ARGUMENT, "QC import belongs to another instance");
    switch (builtin) {
    case QA_QC_BUILTIN_SETMODEL: return setmodel(game, error);
    case QA_QC_BUILTIN_PRECACHE_MODEL: case QA_QC_BUILTIN_PRECACHE_SOUND:
    case QA_QC_BUILTIN_PRECACHE_FILE: return resource(game, builtin, error);
    case QA_QC_BUILTIN_SOUND: return emit_sound(game, false, error);
    case QA_QC_BUILTIN_AMBIENTSOUND: return emit_sound(game, true, error);
    case QA_QC_BUILTIN_BPRINT: case QA_QC_BUILTIN_SPRINT: case QA_QC_BUILTIN_DPRINT:
    case QA_QC_BUILTIN_CENTERPRINT: return print(game, builtin, error);
    case QA_QC_BUILTIN_CHANGEYAW: case QA_QC_BUILTIN_WALKMOVE:
    case QA_QC_BUILTIN_CHECKBOTTOM: case QA_QC_BUILTIN_MOVETOGOAL: return movement(game, builtin, error);
    case QA_QC_BUILTIN_CVAR: {
        const char *text;
        if (!qa_qc_arg_string(vm, 0, &text, error)) return false;
        const qa_cvar_view *value = qa_cvars_find(game->options.cvars, text);
        return qa_qc_return_float(vm, value ? value->number : 0, error);
    }
    case QA_QC_BUILTIN_CVAR_SET: {
        const char *key, *value;
        if (!qa_qc_arg_string(vm, 0, &key, error) || !qa_qc_arg_string(vm, 1, &value, error)) return false;
        if (qa_cvars_find(game->options.cvars, key))
            return qa_cvars_set(game->options.cvars, key, value, false, error);
        static const char prefix[] = "Cvar_Set: variable ", suffix[] = " not found\n";
        size_t length = strlen(key);
        if (length > SIZE_MAX - sizeof(prefix) - sizeof(suffix))
            return qc_game_fail(error, QA_ERROR_MEMORY, "QC cvar diagnostic overflow");
        char *text = malloc(length + sizeof(prefix) + sizeof(suffix) - 1);
        if (!text) return qc_game_fail(error, QA_ERROR_MEMORY, "Allocating QC cvar diagnostic");
        memcpy(text, prefix, sizeof(prefix) - 1); memcpy(text + sizeof(prefix) - 1, key, length);
        memcpy(text + sizeof(prefix) - 1 + length, suffix, sizeof(suffix));
        qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .family = QA_GAME_Q1,
            .provider = game->options.vm.host.owner, .time_ns = qa_session_elapsed(game->options.services.session)};
        bool ok = qa_builtin_resource(&game->options.services, text, &event.text, error);
        free(text);
        return ok && qa_builtin_emit(&game->options.services, &event, error);
    }
    case QA_QC_BUILTIN_LOCALCMD: {
        const char *text;
        return qa_qc_arg_string(vm, 0, &text, error) &&
               qa_console_append(game->options.console, &game->options.command_context, text, error);
    }
    case QA_QC_BUILTIN_PARTICLE: {
        qa_builtin_event event = {.kind = QA_BUILTIN_PARTICLES, .family = QA_GAME_Q1,
                                 .provider = game->options.vm.host.owner};
        float color, count;
        if (!qa_qc_arg_vector(vm, 0, &event.origin, error) || !qa_qc_arg_vector(vm, 1, &event.direction, error) ||
            !qa_qc_arg_float(vm, 2, &color, error) || !qa_qc_arg_float(vm, 3, &count, error)) return false;
        if (!qa_vec_finite(event.origin) || !qa_vec_finite(event.direction) || !isfinite(color) ||
            !isfinite(count) || (double)color < INT32_MIN || (double)color > INT32_MAX ||
            (double)count < INT32_MIN || (double)count > INT32_MAX)
            return qc_game_fail(error, QA_ERROR_ARGUMENT, "Invalid QC particle arguments");
        event.code = (int32_t)color; event.count = (int32_t)count;
        event.time_ns = qa_session_elapsed(game->options.services.session);
        return qa_builtin_emit(&game->options.services, &event, error);
    }
    case QA_QC_BUILTIN_OBJERROR: {
        int32_t self;
        if (!self_reference(game, &self, error) || !qa_qc_remove_entity(vm, self, error)) return false;
        return qc_game_fail(error, QA_ERROR_FORMAT, "QuakeC object error");
    }
    case QA_QC_BUILTIN_BREAK: return qc_game_fail(error, QA_ERROR_FORMAT, "QuakeC debugger break");
    default: return qc_game_fail(error, QA_ERROR_UNSUPPORTED, "QC engine capability is not bound");
    }
}
