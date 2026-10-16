#include "source_trajectory.h"
#include "internal.h"
#include "qa/json.h"
#include "qa/model.h"
#include "qa/network.h"
#include "qa/stamp.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { QC_TRAJECTORY_PROJECTILE = 1, QC_TRAJECTORY_RETIRED = 2 };

typedef struct trajectory_qc_model {
    qa_string_id name;
    qa_resource *resource;
    qa_qc_game_resource value;
    qa_error failure;
    bool present;
} trajectory_qc_model;

typedef struct trajectory_qc {
    qa_application *application;
    const qa_launch_instance *launch;
    qa_qc_program *program;
    qa_qc_game *game;
    qa_qc_instance *vm;
    const qa_qc_game_fields *fields;
    qa_builtin_services services;
    qa_builtin_random random;
    qa_actor_owner owner;
    uint32_t fire, activate, capacity, stride;
    uint64_t time_ns;
    qa_stamp_set refreshed;
    uint32_t *marks;
    uint8_t *projectiles;
    qa_id_index texts, models_by_name;
    trajectory_qc_model *models;
    size_t model_count;
    bool restoring;
    const qa_builtin_weapon_launch *launching;
    unsigned spawned;
    int32_t spawned_reference;
} trajectory_qc;

static bool qc_vector(trajectory_qc *source, int32_t reference,
    const qa_qc_definition *field, qa_vec3 value, qa_error *error)
{
    return !field || qa_qc_project_entity_vector(source->vm, reference, field->offset, value, error);
}

static bool qc_number(trajectory_qc *source, int32_t reference,
    const qa_qc_definition *field, float value, qa_error *error)
{
    return !field || qa_qc_project_entity_float(source->vm, reference, field->offset, value, error);
}

static bool qc_integer(trajectory_qc *source, int32_t reference,
    const qa_qc_definition *field, int32_t value, qa_error *error)
{
    return !field || qa_qc_project_entity_int(source->vm, reference, field->offset, value, error);
}

static bool qc_text(trajectory_qc *source, qa_string_id name, int32_t *out, qa_error *error)
{
    if (!name) { *out = 0; return true; }
    qa_id_index_entry *entry = qa_id_index_find(&source->texts, name, 0);
    if (entry) { *out = (int32_t)(uint32_t)entry->value; return true; }
    if (source->texts.count == source->texts.capacity)
        return application_fail(error, QA_ERROR_CAPACITY, "Trajectory source text admission is full");
    const char *text = qa_strings_cstr(qa_session_strings(source->services.session), name);
    int32_t value;
    if (!text || !qa_qc_string_allocate(source->vm, text, &value, error)) return false;
    qa_id_index_insert(&source->texts, name, 0, (uint32_t)value);
    *out = value;
    return true;
}

static bool qc_bind_literals(trajectory_qc *source, qa_error *error)
{
    qa_bytes literals = qa_qc_program_strings(source->program);
    for (size_t offset = 0; offset < literals.size;) {
        qa_string_id name;
        if (!qa_qc_string_identity(source->vm, (int32_t)offset, &name, error)) return false;
        if (name && !qa_id_index_find(&source->texts, name, 0))
            qa_id_index_insert(&source->texts, name, 0, offset);
        offset += strlen((const char *)literals.data + offset) + 1;
    }
    return true;
}

static bool qc_body(trajectory_qc *source, int32_t reference,
    const qa_body_state *body, qa_error *error)
{
    const qa_qc_game_fields *fields = source->fields;
    return qc_vector(source, reference, fields->origin, body->origin, error) &&
        qc_vector(source, reference, fields->velocity, body->velocity, error) &&
        qc_vector(source, reference, fields->angles, body->angles, error) &&
        qc_vector(source, reference, fields->mins, body->bounds.mins, error) &&
        qc_vector(source, reference, fields->maxs, body->bounds.maxs, error) &&
        qc_vector(source, reference, fields->size, qa_vec_sub(body->bounds.maxs, body->bounds.mins), error) &&
        qc_vector(source, reference, fields->absmin, qa_vec_add(body->origin, body->bounds.mins), error) &&
        qc_vector(source, reference, fields->absmax, qa_vec_add(body->origin, body->bounds.maxs), error);
}

static bool qc_prepare_entity(void *context, qa_qc_instance *vm,
    const qa_qc_entity_access *access, qa_error *error)
{
    trajectory_qc *source = context;
    (void)vm;
    if (access->binding.kind != QA_QC_SLOT_BORROWED) return true;
    uint32_t slot = access->binding.slot;
    qa_actor_id actor = access->binding.actor;
    if (source->restoring) return true;
    if (access->kind == QA_QC_ENTITY_BIND) source->projectiles[slot] = 0;
    if (source->projectiles[slot] ||
        (source->launching && qa_actor_id_equal(actor, source->launching->projectile)) ||
        !qa_stamp_set_mark(&source->refreshed, actor.slot)) return true;
    qa_body_state body;
    qa_error failure = {0};
    if (!qa_world_body_read(source->services.world, actor, &body, &failure)) {
        if (failure.code != QA_ERROR_NOT_FOUND) { if (error) *error = failure; return false; }
        return qc_number(source, access->reference, source->fields->health, 0, error) &&
            qc_number(source, access->reference, source->fields->solid, 0, error);
    }
    qa_combat_state combat = {0};
    if (qa_combat_has(source->services.combat, actor) &&
        !qa_combat_read_traits(source->services.combat, actor, &combat, error)) return false;
    qa_builtin_actor_traits traits = {0};
    if (source->services.actor_traits)
        (void)source->services.actor_traits(source->services.context, actor, &traits);
    const qa_actor_record *record = qa_actors_get(qa_session_actors(source->services.session), actor);
    if (!record) return true;
    qa_actor_player *player = qa_actors_player(qa_session_actors(source->services.session), actor);
    int32_t classname, name;
    qa_linked_body linked;
    return qc_body(source, access->reference, &body, error) &&
        qc_number(source, access->reference, source->fields->health, (float)combat.health, error) &&
        qc_number(source, access->reference, source->fields->solid,
            qa_world_linked(source->services.world, actor, &linked) ? 2 : 0, error) &&
        qc_text(source, traits.classname ? traits.classname : record->definition, &classname, error) &&
        qc_text(source, player && player->present ? player->name : 0, &name, error) &&
        qc_integer(source, access->reference, source->fields->classname, classname, error) &&
        qc_integer(source, access->reference, source->fields->netname, name, error);
}

static bool qc_may_move(void *context, qa_actor_id actor)
{
    (void)context; (void)actor;
    return false;
}

static bool qc_emit(void *context, const qa_builtin_event *event, qa_error *error)
{
    (void)context; (void)event; (void)error;
    return true;
}

static uint32_t qc_random(void *context)
{
    return qa_builtin_random_integer(&((trajectory_qc *)context)->random);
}

static double qc_time(void *context)
{
    return (double)((trajectory_qc *)context)->time_ns / 1e9;
}

static bool qc_resource(void *context, qa_qc_resource_kind kind, const char *path,
    bool precache, qa_qc_game_resource *out, qa_error *error)
{
    trajectory_qc *source = context;
    (void)precache;
    if (!*path) { *out = (qa_qc_game_resource){0}; return true; }
    if (kind != QA_QC_RESOURCE_MODEL) {
        *out = (qa_qc_game_resource){0};
        return true;
    }
    qa_string_id name = qa_strings_find(qa_session_strings(source->services.session),
        (qa_bytes){(const uint8_t *)path, strlen(path)});
    qa_id_index_entry *entry = qa_id_index_find(&source->models_by_name, name, 0);
    if (!entry)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Trajectory model was not admitted from its source assets");
    if (!source->models[entry->value].present) {
        if (source->models[entry->value].failure.code != QA_OK) {
            if (error) *error = source->models[entry->value].failure;
            return false;
        }
        return application_fail(error, QA_ERROR_NOT_FOUND, "Trajectory model is absent from its source assets");
    }
    *out = source->models[entry->value].value;
    return true;
}

static bool qc_spawn(trajectory_qc *source, qa_error *error)
{
    if (!source->launching)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Trajectory think spawned an undeclared additional actor");
    if (source->spawned++)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Trajectory source produced multiple actors; a multi-projectile composition is required");
    int32_t reference;
    if (!qa_qc_actor_reference(source->vm, source->launching->projectile, true, &reference, error)) return false;
    uint32_t words = qa_qc_program_describe(source->program).entity_field_words;
    for (uint32_t word = 0; word < words; ++word)
        if (!qa_qc_project_entity_int(source->vm, reference, word, 0, error)) return false;
    source->spawned_reference = reference;
    source->projectiles[(uint32_t)reference / source->stride] = QC_TRAJECTORY_PROJECTILE;
    return qa_qc_return_int(source->vm, reference, error);
}

static bool qc_view(trajectory_qc *source, qa_actor_id actor, qa_vec3 *angles, qa_error *error)
{
    qa_builtin_player_control control = {0};
    if (source->services.player_control) {
        if (!source->services.player_control(source->services.context, source->owner,
                actor, &control, error)) return false;
        if (control.view_angles) { *angles = *control.view_angles; return true; }
    }
    qa_body_state body;
    if (!qa_world_body_read(source->services.world, actor, &body, error)) return false;
    *angles = body.angles;
    return true;
}

static bool qc_import(void *context, qa_qc_instance *vm, qa_qc_builtin builtin,
    const char *name, qa_error *error)
{
    trajectory_qc *source = context;
    int32_t reference;
    qa_vec3 value, maximum;
    switch (builtin) {
    case QA_QC_BUILTIN_SPAWN: return qc_spawn(source, error);
    case QA_QC_BUILTIN_REMOVE: {
        if (!qa_qc_arg_int(vm, 0, &reference, error)) return false;
        uint32_t slot = reference > 0 ? (uint32_t)reference / source->stride : 0;
        if (!slot || slot >= source->capacity ||
            !(source->projectiles[slot] & QC_TRAJECTORY_PROJECTILE))
            return application_fail(error, QA_ERROR_ARGUMENT, "Trajectory source cannot remove another owner's actor");
        source->projectiles[slot] |= QC_TRAJECTORY_RETIRED;
        return true;
    }
    case QA_QC_BUILTIN_SETORIGIN:
        return qa_qc_arg_int(vm, 0, &reference, error) && qa_qc_arg_vector(vm, 1, &value, error) &&
            qc_vector(source, reference, source->fields->origin, value, error);
    case QA_QC_BUILTIN_SETSIZE:
        return qa_qc_arg_int(vm, 0, &reference, error) && qa_qc_arg_vector(vm, 1, &value, error) &&
            qa_qc_arg_vector(vm, 2, &maximum, error) &&
            qc_vector(source, reference, source->fields->mins, value, error) &&
            qc_vector(source, reference, source->fields->maxs, maximum, error) &&
            qc_vector(source, reference, source->fields->size, qa_vec_sub(maximum, value), error);
    case QA_QC_BUILTIN_SETMODEL: {
        int32_t model;
        const char *path;
        qa_qc_game_resource resource;
        return qa_qc_arg_int(vm, 0, &reference, error) && qa_qc_arg_int(vm, 1, &model, error) &&
            qa_qc_arg_string(vm, 1, &path, error) &&
            qc_resource(source, QA_QC_RESOURCE_MODEL, path, false, &resource, error) &&
            qc_integer(source, reference, source->fields->model, model, error) &&
            qc_number(source, reference, source->fields->modelindex, (float)resource.index, error) &&
            qc_vector(source, reference, source->fields->mins, resource.bounds.mins, error) &&
            qc_vector(source, reference, source->fields->maxs, resource.bounds.maxs, error) &&
            qc_vector(source, reference, source->fields->size,
                qa_vec_sub(resource.bounds.maxs, resource.bounds.mins), error);
    }
    case QA_QC_BUILTIN_AIM: {
        qa_actor_id actor;
        qa_vec3 angles;
        if (!qa_qc_arg_int(vm, 0, &reference, error) || !qa_qc_reference_actor(vm, reference, &actor, error) ||
            !qc_view(source, actor, &angles, error)) return false;
        qa_vec3 forward;
        qa_angle_vectors(angles, &forward, NULL, NULL, QA_ANGLE_NATIVE_MATH);
        return qa_qc_return_vector(vm, forward, error);
    }
    case QA_QC_BUILTIN_PRECACHE_MODEL:
    case QA_QC_BUILTIN_PRECACHE_SOUND:
    case QA_QC_BUILTIN_PRECACHE_FILE:
        return qa_qc_arg_int(vm, 0, &reference, error) && qa_qc_return_int(vm, reference, error);
    case QA_QC_BUILTIN_SOUND: case QA_QC_BUILTIN_AMBIENTSOUND:
    case QA_QC_BUILTIN_BPRINT: case QA_QC_BUILTIN_SPRINT: case QA_QC_BUILTIN_DPRINT:
    case QA_QC_BUILTIN_CENTERPRINT: case QA_QC_BUILTIN_PARTICLE:
    case QA_QC_BUILTIN_STUFFCMD: case QA_QC_BUILTIN_COREDUMP: case QA_QC_BUILTIN_EPRINT:
    case QA_QC_BUILTIN_LIGHTSTYLE: case QA_QC_BUILTIN_WRITEBYTE: case QA_QC_BUILTIN_WRITECHAR:
    case QA_QC_BUILTIN_WRITESHORT: case QA_QC_BUILTIN_WRITELONG: case QA_QC_BUILTIN_WRITECOORD:
    case QA_QC_BUILTIN_WRITEANGLE: case QA_QC_BUILTIN_WRITESTRING: case QA_QC_BUILTIN_WRITEENTITY:
    case QA_QC_BUILTIN_MAKESTATIC: case QA_QC_BUILTIN_CHANGELEVEL: case QA_QC_BUILTIN_LOCALCMD:
    case QA_QC_BUILTIN_CVAR_SET: case QA_QC_BUILTIN_LOGFRAG: case QA_QC_BUILTIN_MULTICAST:
    case QA_QC_BUILTIN_SETCOLOR: case QA_QC_BUILTIN_EX_BPRINT: case QA_QC_BUILTIN_EX_SPRINT:
    case QA_QC_BUILTIN_EX_CENTERPRINT: case QA_QC_BUILTIN_EX_LOCALSOUND:
    case QA_QC_BUILTIN_EX_DRAW_POINT: case QA_QC_BUILTIN_EX_DRAW_LINE:
    case QA_QC_BUILTIN_EX_DRAW_ARROW: case QA_QC_BUILTIN_EX_DRAW_RAY:
    case QA_QC_BUILTIN_EX_DRAW_CIRCLE: case QA_QC_BUILTIN_EX_DRAW_BOUNDS:
    case QA_QC_BUILTIN_EX_DRAW_WORLDTEXT: case QA_QC_BUILTIN_EX_DRAW_SPHERE:
    case QA_QC_BUILTIN_EX_DRAW_CYLINDER:
        return true;
    default:
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
            "QuakeC trajectory does not own builtin %s", name);
        return false;
    }
}

static bool qc_invoke(trajectory_qc *source, uint32_t function, qa_actor_id actor,
    qa_error *error)
{
    qa_qc_game_global globals[] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = actor}}},
        {"other", {QA_QC_GAME_ACTOR, {.actor = {0}}}},
        {"time", {QA_QC_GAME_FLOAT, {.number = (float)((double)source->time_ns / 1e9)}}}
    };
    const qa_qc_definition *time = qa_qc_program_resolved_globals(source->program)->time;
    return qa_qc_game_call_index(source->game, function, NULL, 0, globals,
        time ? 3 : 2, NULL, error);
}

static bool qc_update(trajectory_qc *source, int32_t reference,
    qa_builtin_trajectory_update *out, qa_error *error)
{
    return qa_qc_entity_vector(source->vm, reference, source->fields->origin->offset, &out->origin, error) &&
        qa_qc_entity_vector(source->vm, reference, source->fields->velocity->offset, &out->velocity, error) &&
        qa_qc_entity_vector(source->vm, reference, source->fields->angles->offset, &out->angles, error);
}

static bool qc_mode(trajectory_qc *source, qa_error *error)
{
    qa_application *application = source->application;
    if (!application->primary_mode_ready) return true;
    qa_mode_view mode;
    if (!qa_modes_read(application->modes, application->primary_mode, &mode, error)) return false;
    const qa_qc_engine_globals *globals = qa_qc_program_resolved_globals(source->program);
    bool cooperative = mode.rules.kind == QA_MODE_COOPERATIVE || mode.rules.kind == QA_MODE_HORDE;
    return (!globals->deathmatch || qa_qc_set_global_float(source->vm, globals->deathmatch->offset,
            mode.rules.kind != QA_MODE_SINGLE_PLAYER && !cooperative ? 1 : 0, error)) &&
        (!globals->coop || qa_qc_set_global_float(source->vm, globals->coop->offset, cooperative ? 1 : 0, error));
}

static bool qc_release(void *context, application_trajectory_attachment *attachment, qa_error *error)
{
    trajectory_qc *source = context;
    uint32_t slot = attachment->handle <= INT32_MAX ? (uint32_t)attachment->handle / source->stride : 0;
    qa_qc_slot_binding binding;
    if (slot && slot < source->capacity && qa_qc_slot(source->vm, slot, &binding) &&
        (binding.kind == QA_QC_SLOT_FREE || qa_actor_id_equal(binding.actor, attachment->projectile))) {
        if (binding.kind != QA_QC_SLOT_FREE && !qa_qc_unbind_actor(source->vm, slot, error)) return false;
        source->projectiles[slot] = 0;
    }
    *attachment = (application_trajectory_attachment){0};
    return true;
}

static bool qc_launch(void *context, const qa_builtin_weapon_launch *request,
    application_trajectory_attachment *attachment, qa_builtin_trajectory_update *out,
    bool *attached, qa_error *error)
{
    trajectory_qc *source = context;
    *attached = false;
    source->time_ns = request->time_ns;
    source->launching = request;
    source->spawned = 0; source->spawned_reference = 0;
    qa_stamp_set_begin(&source->refreshed);
    int32_t shooter;
    qa_vec3 view;
    bool okay = qc_mode(source, error) &&
        qa_qc_actor_reference(source->vm, request->shooter, true, &shooter, error) &&
        qc_view(source, request->shooter, &view, error) &&
        qc_vector(source, shooter, source->fields->v_angle, view, error) &&
        (!source->activate || qc_invoke(source, source->activate, request->shooter, error)) &&
        qc_invoke(source, source->fire, request->shooter, error);
    source->launching = NULL;
    if (source->spawned_reference) {
        *attachment = (application_trajectory_attachment){.projectile = request->projectile,
            .handle = (uint32_t)source->spawned_reference,
            .retired = (source->projectiles[(uint32_t)source->spawned_reference / source->stride] & QC_TRAJECTORY_RETIRED) != 0};
        if (!okay) { (void)qc_release(source, attachment, NULL); return false; }
        if (!qc_update(source, source->spawned_reference, out, error)) {
            (void)qc_release(source, attachment, NULL); return false;
        }
        *attached = true;
    }
    return okay;
}

static bool qc_step(void *context, application_trajectory_attachment *attachment,
    const qa_body_state *body, uint64_t time_ns, qa_builtin_trajectory_update *out,
    bool *changed, qa_error *error)
{
    trajectory_qc *source = context;
    *changed = false;
    if (attachment->retired) return true;
    int32_t reference = (int32_t)attachment->handle;
    uint32_t slot = (uint32_t)reference / source->stride;
    qa_qc_slot_binding binding;
    if (!qa_qc_slot(source->vm, slot, &binding) || binding.kind != QA_QC_SLOT_BORROWED ||
        !qa_actor_id_equal(binding.actor, attachment->projectile))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Trajectory attachment lost its source row");
    source->time_ns = time_ns;
    qa_stamp_set_begin(&source->refreshed);
    if (!qc_body(source, reference, body, error)) return false;
    float next;
    int32_t think;
    if (!qa_qc_entity_float(source->vm, reference, source->fields->nextthink->offset, &next, error) ||
        !qa_qc_entity_int(source->vm, reference, source->fields->think->offset, &think, error)) return false;
    if (!(next > 0) || (double)next > (double)time_ns / 1e9 || !think) return true;
    if (!qc_number(source, reference, source->fields->nextthink, 0, error) ||
        !qc_invoke(source, (uint32_t)think, attachment->projectile, error)) return false;
    attachment->retired = (source->projectiles[slot] & QC_TRAJECTORY_RETIRED) != 0;
    if (attachment->retired) return true;
    if (!qc_update(source, reference, out, error)) return false;
    *changed = true;
    return true;
}

static bool qc_actor_released(void *context, qa_actor_record actor, qa_error *error)
{
    trajectory_qc *source = context;
    (void)error;
    qa_qc_game_actor_released(source->game, actor);
    return true;
}

static bool qc_write_name(qa_net_writer *writer, qa_strings *strings, qa_string_id id)
{
    qa_bytes text = qa_strings_text(strings, id);
    return text.size <= UINT32_MAX && qa_net_write_u32(writer, (uint32_t)text.size) &&
        qa_net_write_data(writer, text.data, text.size);
}

static bool qc_capture_host(void *context, qa_buffer *out, qa_error *error)
{
    trajectory_qc *source = context;
    size_t size = 8 + 31 * 4 + 2 + 8 + 4 + 4;
    size_t literal_bytes = qa_qc_program_strings(source->program).size;
    uint32_t count = 0;
    uint32_t text_count = 0;
    for (uint32_t slot = 1; slot < qa_qc_entity_count(source->vm); ++slot)
        if (source->projectiles[slot]) { ++count; size += 5; }
    qa_strings *strings = qa_session_strings(source->services.session);
    for (size_t i = 0; i < source->texts.count; ++i)
        if (source->texts.entries[i].value >= literal_bytes) {
            ++text_count;
            size += 8 + qa_strings_text(strings, source->texts.entries[i].key).size;
        }
    uint8_t *bytes = malloc(size);
    if (!bytes) return application_fail(error, QA_ERROR_MEMORY, "Capturing trajectory source state");
    qa_net_writer writer; qa_net_writer_init(&writer, bytes, size, error);
    bool okay = qa_net_write_u64(&writer, source->time_ns);
    for (size_t i = 0; okay && i < 31; ++i) okay = qa_net_write_u32(&writer, source->random.words[i]);
    okay = okay && qa_net_write_u8(&writer, source->random.front) &&
        qa_net_write_u8(&writer, source->random.rear) && qa_net_write_u64(&writer, source->random.draws) &&
        qa_net_write_u32(&writer, count);
    for (uint32_t slot = 1; okay && slot < qa_qc_entity_count(source->vm); ++slot)
        if (source->projectiles[slot]) okay = qa_net_write_u32(&writer, slot) &&
            qa_net_write_u8(&writer, source->projectiles[slot]);
    okay = okay && qa_net_write_u32(&writer, text_count);
    for (size_t i = 0; okay && i < source->texts.count; ++i)
        if (source->texts.entries[i].value >= literal_bytes)
            okay = qc_write_name(&writer, strings, source->texts.entries[i].key) &&
                qa_net_write_i32(&writer, (int32_t)(uint32_t)source->texts.entries[i].value);
    if (!okay) { free(bytes); return false; }
    *out = (qa_buffer){bytes, qa_net_writer_size(&writer)};
    return true;
}

static bool qc_read_name(qa_net_reader *reader, qa_strings *strings, qa_string_id *out)
{
    uint32_t size = qa_net_read_u32(reader);
    qa_bytes text;
    return !reader->failed && qa_net_read_bytes(reader, size, &text) &&
        qa_strings_intern(strings, text, out, reader->error);
}

static bool qc_restore_host(void *context, qa_bytes bytes, qa_error *error)
{
    trajectory_qc *source = context;
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    source->time_ns = qa_net_read_u64(&reader);
    for (size_t i = 0; i < 31; ++i) source->random.words[i] = qa_net_read_u32(&reader);
    source->random.front = qa_net_read_u8(&reader); source->random.rear = qa_net_read_u8(&reader);
    source->random.draws = qa_net_read_u64(&reader);
    if (source->random.front >= 31 || source->random.rear >= 31)
        return qa_net_reader_fail(&reader, "Invalid trajectory random state");
    memset(source->projectiles, 0, source->capacity);
    uint32_t count = qa_net_read_u32(&reader);
    if (count >= source->capacity)
        return qa_net_reader_fail(&reader, "Trajectory source has too many projectiles");
    for (uint32_t i = 0; !reader.failed && i < count; ++i) {
        uint32_t slot = qa_net_read_u32(&reader); uint8_t flags = qa_net_read_u8(&reader);
        qa_qc_slot_binding binding;
        if (!slot || slot >= source->capacity || source->projectiles[slot] ||
            !(flags & QC_TRAJECTORY_PROJECTILE) || (flags & ~(QC_TRAJECTORY_PROJECTILE | QC_TRAJECTORY_RETIRED)) ||
            !qa_qc_slot(source->vm, slot, &binding) || binding.kind != QA_QC_SLOT_BORROWED)
            return qa_net_reader_fail(&reader, "Trajectory projectile binding differs from its restored source");
        source->projectiles[slot] = flags;
    }
    qa_id_index_clear(&source->texts);
    if (!qc_bind_literals(source, error)) return false;
    count = qa_net_read_u32(&reader);
    if (count > source->texts.capacity - source->texts.count)
        return qa_net_reader_fail(&reader, "Trajectory text bindings exceed admitted storage");
    qa_strings *strings = qa_session_strings(source->services.session);
    size_t literal_bytes = qa_qc_program_strings(source->program).size;
    for (uint32_t i = 0; !reader.failed && i < count; ++i) {
        qa_string_id name;
        if (!qc_read_name(&reader, strings, &name)) return false;
        int32_t reference = qa_net_read_i32(&reader);
        const char *text;
        if (reference < 0 || (uint32_t)reference < literal_bytes ||
            !qa_qc_string(source->vm, reference, &text, error) ||
            strcmp(text, qa_strings_cstr(strings, name)) || qa_id_index_find(&source->texts, name, 0))
            return qa_net_reader_fail(&reader, "Trajectory text differs from its restored source");
        qa_id_index_insert(&source->texts, name, 0, (uint32_t)reference);
    }
    qa_stamp_set_begin(&source->refreshed);
    return qa_net_reader_finish(&reader);
}

static bool qc_capture(void *context, qa_buffer *out, qa_error *error)
{
    trajectory_qc *source = context;
    qa_qc_checkpoint *checkpoint = NULL;
    bool okay = qa_qc_game_capture(source->game, &checkpoint, error) &&
        qa_qc_checkpoint_encode(checkpoint, out, error);
    qa_qc_checkpoint_destroy(checkpoint);
    return okay;
}

static bool qc_restore(void *context, qa_bytes bytes, qa_error *error)
{
    trajectory_qc *source = context;
    qa_qc_checkpoint *checkpoint = NULL;
    source->restoring = true;
    bool okay = qa_qc_checkpoint_decode(bytes, &checkpoint, error) &&
        qa_qc_game_restore(source->game, checkpoint, error);
    source->restoring = false;
    qa_qc_checkpoint_destroy(checkpoint);
    return okay;
}

static bool qc_destroy(void *context, qa_error *error)
{
    trajectory_qc *source = context;
    if (source->game && !qa_qc_game_destroy(source->game, error)) return false;
    for (size_t i = 0; i < source->model_count; ++i) qa_resource_release(source->models[i].resource);
    qa_id_index_destroy(&source->texts); qa_id_index_destroy(&source->models_by_name);
    free(source->models); free(source->marks); free(source->projectiles);
    qa_qc_program_destroy(source->program);
    free(source);
    return true;
}

static bool qc_function(trajectory_qc *source, const qa_json_document *document,
    qa_json_id entry, const char *name, bool optional, uint32_t *out, qa_error *error)
{
    qa_json_id id = qa_json_get(document, entry, name);
    if (optional && (id == QA_JSON_NONE || qa_json_type(document, id) == QA_JSON_NULL)) return true;
    qa_buffer text = {0};
    if (!qa_json_string(document, id, &text, error)) return false;
    const qa_qc_function *function = text.size == strlen((const char *)text.data) ?
        qa_qc_program_find_function(source->program, (const char *)text.data, out) : NULL;
    qa_buffer_free(&text);
    if (!function || !*out || function->first_statement < 0 || function->named_builtin || function->parameter_count)
        return application_fail(error, QA_ERROR_FORMAT, "Trajectory callback must be a compiled no-argument QuakeC function");
    return true;
}

static bool qc_qualify(trajectory_qc *source, const qa_catalog_weapon_behavior *behavior, qa_error *error)
{
    qa_json_document *document = NULL;
    if (!qa_json_parse(QA_JSON_STRICT, behavior->entry, &document, error)) return false;
    qa_json_id entry = qa_json_root(document);
    bool okay = qc_function(source, document, entry, "fireFunction", false, &source->fire, error) &&
        qc_function(source, document, entry, "activationFunction", true, &source->activate, error);
    qa_json_destroy(document);
    static const struct { const char *name; qa_qc_value_type type; bool optional; } fields[] = {
        {"origin", QA_QC_VECTOR, false}, {"velocity", QA_QC_VECTOR, false}, {"angles", QA_QC_VECTOR, false},
        {"mins", QA_QC_VECTOR, false}, {"maxs", QA_QC_VECTOR, false}, {"v_angle", QA_QC_VECTOR, false},
        {"health", QA_QC_FLOAT, false}, {"solid", QA_QC_FLOAT, false}, {"nextthink", QA_QC_FLOAT, false},
        {"classname", QA_QC_STRING, false}, {"netname", QA_QC_STRING, false}, {"think", QA_QC_FUNCTION, false},
        {"model", QA_QC_STRING, true}, {"modelindex", QA_QC_FLOAT, true}, {"chain", QA_QC_ENTITY, true},
        {"size", QA_QC_VECTOR, true}, {"absmin", QA_QC_VECTOR, true}, {"absmax", QA_QC_VECTOR, true}
    };
    for (size_t i = 0; okay && i < sizeof(fields) / sizeof(*fields); ++i) {
        const qa_qc_definition *field = qa_qc_program_find_field(source->program, fields[i].name);
        if ((!field && !fields[i].optional) || (field && field->type != fields[i].type)) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "QuakeC trajectory field %s has no matching type", fields[i].name);
            okay = false;
        }
    }
    const qa_qc_definition *self = qa_qc_program_resolved_globals(source->program)->self;
    const qa_qc_definition *other = qa_qc_program_find_global(source->program, "other");
    if (okay && (!self || self->type != QA_QC_ENTITY || !other || other->type != QA_QC_ENTITY))
        okay = application_fail(error, QA_ERROR_FORMAT, "QuakeC trajectory requires typed self and other globals");
    static const struct { const char *name; qa_qc_value_type type; } globals[] = {
        {"time", QA_QC_FLOAT}, {"deathmatch", QA_QC_FLOAT}, {"coop", QA_QC_FLOAT},
        {"trace_endpos", QA_QC_VECTOR}, {"trace_plane_normal", QA_QC_VECTOR},
        {"trace_ent", QA_QC_ENTITY}, {"trace_fraction", QA_QC_FLOAT},
        {"trace_allsolid", QA_QC_FLOAT}, {"trace_startsolid", QA_QC_FLOAT},
        {"trace_inwater", QA_QC_FLOAT}, {"trace_inopen", QA_QC_FLOAT}, {"trace_plane_dist", QA_QC_FLOAT}
    };
    for (size_t i = 0; okay && i < sizeof(globals) / sizeof(*globals); ++i) {
        const qa_qc_definition *global = qa_qc_program_find_global(source->program, globals[i].name);
        if (global && global->type != globals[i].type) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "QuakeC trajectory global %s has no matching type", globals[i].name);
            okay = false;
        }
    }
    return okay;
}

bool application_trajectory_qc_check(qa_qc_program *program,
    const qa_catalog_weapon_behavior *behavior, qa_error *error)
{
    trajectory_qc source = {.program = program};
    return qc_qualify(&source, behavior, error);
}

static bool qc_model_literal(const char *text)
{
    size_t size = strlen(text);
    return size > 4 && (!strcmp(text + size - 4, ".mdl") || !strcmp(text + size - 4, ".md2") ||
        !strcmp(text + size - 4, ".md3") || !strcmp(text + size - 4, ".spr") ||
        !strcmp(text + size - 4, ".sp2") || !strcmp(text + size - 4, ".bsp"));
}

static bool qc_admit_models(trajectory_qc *source, qa_error *error)
{
    qa_bytes literals = qa_qc_program_strings(source->program);
    qa_strings *strings = qa_session_strings(source->services.session);
    for (size_t offset = 0; offset < literals.size;) {
        const char *text = (const char *)literals.data + offset;
        size_t length = strlen(text);
        if (qc_model_literal(text)) {
            qa_string_id name;
            if (!qa_strings_intern_cstr(strings, text, &name, error)) return false;
            if (!qa_id_index_find(&source->models_by_name, name, 0)) {
                size_t index = source->model_count++;
                trajectory_qc_model *row = source->models + index;
                row->name = name;
                bool found = false; uint64_t bytes = 0;
                bool okay = qa_vfs_probe(source->launch->content, text, &found, &bytes, &row->failure);
                if (okay && found) {
                    okay = qa_vfs_acquire(source->launch->content, text, &row->resource, NULL, &row->failure);
                }
                if (okay && found) {
                    qa_bytes data = qa_resource_bytes(row->resource);
                    if (!strcmp(text + length - 4, ".bsp")) {
                        qa_bsp_view bsp; qa_bsp_model model;
                        okay = qa_bsp_open(data, &bsp, &row->failure) && qa_bsp_read_model(&bsp, 0, &model, &row->failure);
                        if (okay) row->value.bounds = (qa_bounds){model.bounds.min, model.bounds.max};
                    } else {
                        qa_model model = {0};
                        okay = qa_model_load(data, &model, &row->failure);
                        if (okay) row->value.bounds = model.format == QA_MODEL_MDL ?
                            (qa_bounds){qa_v3(-16, -16, -16), qa_v3(16, 16, 16)} :
                            (qa_bounds){qa_v3(model.bounds.min[0], model.bounds.min[1], model.bounds.min[2]),
                                qa_v3(model.bounds.max[0], model.bounds.max[1], model.bounds.max[2])};
                        qa_model_free(&model);
                    }
                    row->present = okay;
                }
                if (!okay && row->failure.code == QA_ERROR_MEMORY) {
                    if (error) *error = row->failure;
                    return false;
                }
                row->value.index = (uint32_t)index + 1;
                qa_id_index_insert(&source->models_by_name, name, 0, index);
            }
        }
        offset += length + 1;
    }
    return true;
}

bool application_trajectory_qc_create(qa_application *application, const qa_launch_instance *launch,
    qa_qc_program *program, const qa_catalog_weapon_behavior *behavior,
    application_trajectory_source *out, qa_error *error)
{
    trajectory_qc *source = calloc(1, sizeof(*source));
    if (!source) return application_fail(error, QA_ERROR_MEMORY, "Creating QuakeC trajectory source");
    source->application = application; source->launch = launch; source->program = program;
    qa_qc_program_retain(program);
    source->fields = qa_qc_program_resolved_fields(program);
    source->services = application_builtin_services(application, application->world, application->physics);
    source->services.emit = qc_emit;
    source->capacity = qa_actors_capacity(qa_session_actors(application->session)) + 1;
    qa_qc_profile profile = qa_qc_program_describe(program).api == QA_QC_API_QUAKEWORLD ?
        QA_QC_QUAKEWORLD : QA_QC_NETQUAKE;
    const qa_product *product = qa_catalog_product(qa_launch_instance_catalog(launch), behavior->product);
    if (profile != QA_QC_QUAKEWORLD && product && product->edition == QA_EDITION_RERELEASE) profile = QA_QC_RERELEASE;
    source->stride = qa_qc_default_entity_layout(program, profile).stride_bytes;
    qa_bytes literals = qa_qc_program_strings(program);
    size_t literal_count = 0;
    for (size_t offset = 0; offset < literals.size; offset += strlen((const char *)literals.data + offset) + 1) ++literal_count;
    source->models = calloc(literal_count ? literal_count : 1, sizeof(*source->models));
    source->marks = calloc(source->capacity, sizeof(*source->marks));
    source->projectiles = calloc(source->capacity, 1);
    qa_stamp_set_init(&source->refreshed, source->marks, source->marks ? source->capacity : 0);
    bool okay = source->models && source->marks && source->projectiles &&
        qa_id_index_reserve(&source->models_by_name, literal_count, error) &&
        qa_id_index_reserve(&source->texts, literal_count + source->capacity * 2u, error) &&
        qa_strings_intern_cstr(qa_session_strings(application->session), behavior->id, &source->owner, error) &&
        qc_qualify(source, behavior, error);
    size_t required_count = 0;
    const qa_qc_builtin_requirement *requirements = qa_qc_builtin_requirements(profile, &required_count);
    qa_qc_builtin_binding *bindings = okay ? calloc(required_count, sizeof(*bindings)) : NULL;
    size_t binding_count = 0;
    if (okay && !bindings) okay = application_fail(error, QA_ERROR_MEMORY, "Binding trajectory source imports");
    for (size_t i = 0; okay && i < required_count; ++i) {
        qa_qc_builtin builtin = requirements[i].builtin;
        if (builtin == QA_QC_BUILTIN_TRACELINE || builtin == QA_QC_BUILTIN_FINDRADIUS ||
            builtin == QA_QC_BUILTIN_POINTCONTENTS || builtin == QA_QC_BUILTIN_CVAR) continue;
        size_t j = 0;
        while (j < binding_count && bindings[j].builtin != builtin) ++j;
        if (j == binding_count) bindings[binding_count++] = (qa_qc_builtin_binding){builtin, NULL, source, qc_import};
    }
    qa_builtin_random_seed(&source->random, source->owner);
    qa_qc_game_options options = {
        .vm = {.profile = profile, .entity_capacity = source->capacity,
            .host = {.owner = source->owner, .default_definition = source->owner,
                .vfs = launch->content, .context = source, .random_u32 = qc_random,
                .may_move = qc_may_move, .declared_projection = true,
                .prepare_entity = qc_prepare_entity, .source_time_seconds = qc_time,
                .builtins = bindings, .builtin_count = binding_count}},
        .services = source->services, .cvars = application->cvars, .console = application->console,
        .command_context = {.owner = source->owner, .dialect = profile == QA_QC_QUAKEWORLD ? QA_RULESET_QUAKEWORLD : QA_RULESET_NETQUAKE,
            .origin = QA_COMMAND_SERVER},
        .context = source, .resource = qc_resource, .checkpoint = qc_capture_host, .restore = qc_restore_host
    };
    if (okay) okay = qa_qc_game_create(program, &options, &source->game, error);
    free(bindings);
    if (okay) {
        source->vm = qa_qc_game_instance(source->game);
        okay = qc_admit_models(source, error) && qa_qc_game_loading(source->game, false, error);
    }
    if (okay) {
        okay = qc_bind_literals(source, error);
        uint32_t cursor = 0; const qa_actor_record *actor;
        while (okay && qa_actors_next(qa_session_actors(application->session), &cursor, &actor)) {
            qa_builtin_actor_traits traits = {0}; int32_t reference;
            if (source->services.actor_traits) (void)source->services.actor_traits(source->services.context, actor->id, &traits);
            qa_actor_player *player = qa_actors_player(qa_session_actors(application->session), actor->id);
            okay = qc_text(source, traits.classname ? traits.classname : actor->definition, &reference, error) &&
                qc_text(source, player && player->present ? player->name : 0, &reference, error);
        }
        if (okay) okay = qc_mode(source, error);
    }
    if (!okay) {
        if (error && error->code == QA_OK) application_fail(error, QA_ERROR_MEMORY, "Preparing QuakeC trajectory source");
        (void)qc_destroy(source, NULL);
        return false;
    }
    *out = (application_trajectory_source){.context = source, .launch = qc_launch, .step = qc_step,
        .release = qc_release, .actor_released = qc_actor_released, .capture = qc_capture,
        .restore = qc_restore, .destroy = qc_destroy};
    return true;
}
