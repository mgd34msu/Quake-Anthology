#include "guest_qc_objectives.h"
#include "guest_qc_profile.h"

typedef struct qc_objective_storage {
    const qa_qc_definition *global, *field;
    const qa_qc_definition **indirections;
    size_t indirection_count;
} qc_objective_storage;

typedef struct qc_objective_value {
    float value;
    qa_string_id stage;
    bool complete;
} qc_objective_value;

typedef struct qc_objective_location {
    int32_t reference;
    uint32_t word;
    qa_actor_id actor;
    bool entity;
} qc_objective_location;

typedef struct qc_objective {
    struct application_qc_objectives *objectives;
    qa_string_id id;
    qc_objective_storage state, carrier, target;
    qc_objective_value *values;
    size_t value_count;
    application_qc_call change;
    qa_objective_lease lease;
    qa_objective_state projected;
    qc_objective_location locations[3];
    bool owned, campaign_gate, bot_goal, writable;
    bool has_carrier, has_target, has_change, has_projection;
} qc_objective;

struct application_qc_objectives {
    qc_objective *values;
    size_t count;
    struct application_qc_state *engine;
    qa_modes *modes;
    bool active, busy;
};

static bool definition(const qa_json_document *doc, qa_json_id node,
    const qa_qc_program *program, bool field, qa_qc_value_type type,
    const qa_qc_definition **out, qa_error *error)
{
    char *name = application_qc_declaration_string(doc, node, error);
    const qa_qc_definition *value = name ? (field ? qa_qc_program_find_field(program, name) :
        qa_qc_program_find_global(program, name)) : NULL;
    free(name);
    if (!value || value->type != type)
        return application_fail(error, QA_ERROR_FORMAT, "QC objective requires its declared compiled storage type");
    *out = value;
    return true;
}

static bool storage(const qa_json_document *doc, qa_json_id node,
    const qa_qc_program *program, qa_qc_value_type type,
    qc_objective_storage *out, qa_error *error)
{
    if (qa_json_type(doc, node) == QA_JSON_STRING)
        return definition(doc, node, program, false, type, &out->global, error);
    qa_json_id indirections = qa_json_get(doc, node, "indirections");
    if (!qa_json_string_equal(doc, qa_json_get(doc, node, "kind"), "entity-field") ||
        qa_json_type(doc, indirections) != QA_JSON_ARRAY ||
        !definition(doc, qa_json_get(doc, node, "global"), program, false,
            QA_QC_ENTITY, &out->global, error) ||
        !definition(doc, qa_json_get(doc, node, "field"), program, true, type, &out->field, error))
        return application_fail(error, QA_ERROR_FORMAT, "QC objective field requires an actual compiled entity selector");
    out->indirection_count = qa_json_size(doc, indirections);
    out->indirections = out->indirection_count ? calloc(out->indirection_count, sizeof(*out->indirections)) : NULL;
    if (out->indirection_count && !out->indirections)
        return application_fail(error, QA_ERROR_MEMORY, "Allocating QC objective selectors");
    for (size_t i = 0; i < out->indirection_count; ++i)
        if (!definition(doc, qa_json_at(doc, indirections, i), program, true,
                QA_QC_ENTITY, out->indirections + i, error)) return false;
    return true;
}

static bool identity(application_provider *provider, const qa_json_document *doc,
    qa_json_id node, qa_string_id *out, qa_error *error)
{
    char *name = application_qc_declaration_string(doc, node, error);
    const char *colon = name ? strchr(name, ':') : NULL;
    bool okay = colon && colon != name && colon[1] &&
        qa_strings_intern_cstr(qa_session_strings(provider->application->session), name, out, error);
    free(name);
    return okay || application_fail(error, QA_ERROR_FORMAT, "QC objective requires a namespaced identity");
}

bool application_qc_objectives_qualify(application_provider *provider,
    const qa_json_document *doc, qa_json_id node, qa_error *error)
{
    if (node == QA_JSON_NONE) return true;
    if (qa_json_type(doc, node) != QA_JSON_ARRAY)
        return application_fail(error, QA_ERROR_FORMAT, "QC objectives require an array");
    struct application_qc_profile *profile = provider->state.qc.qualified;
    if (!profile || profile->objectives)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC objective qualification lost its profile");
    size_t count = qa_json_size(doc, node);
    if (!count) return true;
    struct application_qc_objectives *objectives = calloc(1, sizeof(*objectives));
    if (!objectives) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC objective adapter");
    profile->objectives = objectives;
    objectives->values = calloc(count, sizeof(*objectives->values));
    if (!objectives->values) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC objective declarations");
    objectives->count = count;
    const qa_qc_program *program = provider->state.qc.program;
    for (size_t i = 0; i < count; ++i) {
        qa_json_id row = qa_json_at(doc, node, i), state = qa_json_get(doc, row, "state");
        qa_json_id role = qa_json_get(doc, row, "role"), values = qa_json_get(doc, state, "values");
        qc_objective *objective = objectives->values + i;
        objective->objectives = objectives;
        objective->owned = qa_json_string_equal(doc, role, "owned");
        if ((!objective->owned && !qa_json_string_equal(doc, role, "borrowed")) ||
            !identity(provider, doc, qa_json_get(doc, row, "id"), &objective->id, error) ||
            !storage(doc, qa_json_get(doc, state, "storage"), program, QA_QC_FLOAT, &objective->state, error) ||
            qa_json_type(doc, values) != QA_JSON_ARRAY || !qa_json_size(doc, values))
            return application_fail(error, QA_ERROR_FORMAT, "QC objective requires declared source states and ownership");
        for (size_t j = 0; j < i; ++j)
            if (objectives->values[j].id == objective->id)
                return application_fail(error, QA_ERROR_FORMAT, "Duplicate QC objective channel");
        objective->value_count = qa_json_size(doc, values);
        objective->values = calloc(objective->value_count, sizeof(*objective->values));
        if (!objective->values) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC objective stages");
        for (size_t j = 0; j < objective->value_count; ++j) {
            qa_json_id value = qa_json_at(doc, values, j);
            qc_objective_value *entry = objective->values + j;
            char *stage = application_qc_declaration_string(doc, qa_json_get(doc, value, "stage"), error);
            bool okay = stage && *stage &&
                application_qc_declaration_number(doc, qa_json_get(doc, value, "value"), &entry->value, error) &&
                qa_json_bool(doc, qa_json_get(doc, value, "complete"), &entry->complete, error) &&
                qa_strings_intern_cstr(qa_session_strings(provider->application->session), stage, &entry->stage, error);
            free(stage);
            if (!okay) return application_fail(error, QA_ERROR_FORMAT, "Invalid QC objective stage mapping");
            for (size_t k = 0; k < j; ++k)
                if (objective->values[k].value == entry->value || objective->values[k].stage == entry->stage)
                    return application_fail(error, QA_ERROR_FORMAT, "QC objective stages must have distinct source values and names");
        }
        qa_json_id carrier = qa_json_get(doc, row, "carrier"), target = qa_json_get(doc, row, "target");
        objective->has_carrier = qa_json_type(doc, carrier) != QA_JSON_NULL;
        objective->has_target = qa_json_type(doc, target) != QA_JSON_NULL;
        if ((objective->has_carrier && !storage(doc, carrier, program, QA_QC_ENTITY, &objective->carrier, error)) ||
            (objective->has_target && !storage(doc, target, program, QA_QC_ENTITY, &objective->target, error))) return false;
        if (objective->owned) {
            if (!qa_json_bool(doc, qa_json_get(doc, row, "campaignGate"), &objective->campaign_gate, error) ||
                !qa_json_bool(doc, qa_json_get(doc, row, "botGoal"), &objective->bot_goal, error)) return false;
            qa_json_id change = qa_json_get(doc, row, "change");
            objective->has_change = qa_json_type(doc, change) != QA_JSON_NULL;
            uint64_t available = (UINT64_C(1) << QC_INPUT_SELF) | (UINT64_C(1) << QC_INPUT_OTHER) |
                (UINT64_C(1) << QC_INPUT_ACTIVATOR) | (UINT64_C(1) << QC_INPUT_AMOUNT) |
                (UINT64_C(1) << QC_INPUT_TIME);
            if (objective->has_change && !application_qc_call_parse(doc, change, program,
                    available, false, &objective->change, error)) return false;
        } else if (!qa_json_bool(doc, qa_json_get(doc, row, "writable"), &objective->writable, error)) return false;
    }
    return true;
}

static struct application_qc_objectives *adapter(const struct application_qc_state *engine)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    return profile ? profile->objectives : NULL;
}

static bool current(const struct application_qc_objectives *objectives, qa_error *error)
{
    const struct application_qc_state *engine = objectives->engine;
    const application_provider *provider = engine ? engine->provider : NULL;
    return (provider && provider->state.qc.engine == engine && provider->state.qc.instance &&
        provider->state.qc.game && provider->constructed && provider->attached && !provider->close_pending &&
        !provider->application->destroy_requested &&
        adapter(engine) == objectives && provider->application->modes == objectives->modes) ||
        application_fail(error, QA_ERROR_ARGUMENT, "QC objective lost its actual current source and canonical modes");
}

static bool callback_current(const struct application_qc_objectives *objectives, qa_error *error)
{
    return current(objectives, error) &&
        ((objectives->engine->initialized && !objectives->engine->loading) ||
         application_fail(error, QA_ERROR_ARGUMENT, "QC objective callback requires its initialized source map"));
}

static bool resolve_location(struct application_qc_objectives *objectives,
    const qc_objective_storage *storage_value, qc_objective_location *out, qa_error *error)
{
    qa_qc_instance *vm = objectives->engine->provider->state.qc.instance;
    qc_objective_location location = {.word = storage_value->global->offset};
    if (storage_value->field) {
        location.entity = true;
        if (!qa_qc_global_int(vm, storage_value->global->offset, &location.reference, error)) return false;
        for (size_t i = 0; i <= storage_value->indirection_count; ++i) {
            if (location.reference) {
                if (!qa_qc_reference_actor(vm, location.reference, &location.actor, error)) return false;
            } else {
                qa_qc_slot_binding world;
                location.actor = (qa_actor_id){0};
                if (!qa_qc_slot(vm, 0, &world) || world.kind != QA_QC_SLOT_WORLD)
                    return application_fail(error, QA_ERROR_NOT_FOUND, "QC objective selector lost its real world row");
            }
            if (i == storage_value->indirection_count) break;
            if (!qa_qc_entity_int(vm, location.reference, storage_value->indirections[i]->offset,
                    &location.reference, error)) return false;
        }
        location.word = storage_value->field->offset;
    }
    *out = location;
    return true;
}

static bool locations(qc_objective *objective, qc_objective_location out[3], qa_error *error)
{
    memset(out, 0, 3 * sizeof(*out));
    return resolve_location(objective->objectives, &objective->state, out, error) &&
        (!objective->has_carrier || resolve_location(objective->objectives, &objective->carrier, out + 1, error)) &&
        (!objective->has_target || resolve_location(objective->objectives, &objective->target, out + 2, error));
}

static bool same_locations(const qc_objective_location left[3], const qc_objective_location right[3])
{
    for (size_t i = 0; i < 3; ++i)
        if (left[i].entity != right[i].entity || left[i].reference != right[i].reference ||
            left[i].word != right[i].word || !qa_actor_id_equal(left[i].actor, right[i].actor)) return false;
    return true;
}

static bool actor_read(qa_qc_instance *vm, const qc_objective_location *location,
    qa_actor_id *out, qa_error *error)
{
    int32_t reference;
    if (!(location->entity ? qa_qc_entity_int(vm, location->reference, location->word, &reference, error) :
        qa_qc_global_int(vm, location->word, &reference, error))) return false;
    if (!reference) { *out = (qa_actor_id){0}; return true; }
    return qa_qc_reference_actor(vm, reference, out, error);
}

static const qc_objective_value *stage(const qc_objective *objective, qa_string_id id)
{
    for (size_t i = 0; i < objective->value_count; ++i)
        if (objective->values[i].stage == id) return objective->values + i;
    return NULL;
}

static bool source_read(qc_objective *objective, const qc_objective_location location[3],
    qa_objective_state *out, qa_error *error)
{
    qa_qc_instance *vm = objective->objectives->engine->provider->state.qc.instance;
    float scalar;
    if (!(location->entity ? qa_qc_entity_float(vm, location->reference, location->word, &scalar, error) :
        qa_qc_global_float(vm, location->word, &scalar, error))) return false;
    const qc_objective_value *value = NULL;
    for (size_t i = 0; i < objective->value_count; ++i)
        if (objective->values[i].value == scalar) { value = objective->values + i; break; }
    if (!value) return application_fail(error, QA_ERROR_FORMAT, "QC objective has an undeclared original state");
    qa_objective_state state_value = {.stage = value->stage, .complete = value->complete,
        .phase = value->complete ? QA_OBJECTIVE_COMPLETE : QA_OBJECTIVE_HOME};
    if ((objective->has_carrier && !actor_read(vm, location + 1, &state_value.carrier, error)) ||
        (objective->has_target && !actor_read(vm, location + 2, &state_value.target, error))) return false;
    *out = state_value;
    return true;
}

static bool read(void *opaque, qa_objective_state *out, qa_error *error)
{
    qc_objective *objective = opaque;
    qc_objective_location location[3];
    return callback_current(objective->objectives, error) && locations(objective, location, error) &&
        source_read(objective, location, out, error) && callback_current(objective->objectives, error);
}

static bool change(void *opaque, const qa_objective_state *request, qa_error *error)
{
    qc_objective *objective = opaque;
    struct application_qc_objectives *objectives = objective->objectives;
    const qc_objective_value *value = stage(objective, request->stage);
    if (!callback_current(objectives, error) || !objective->has_change || !value ||
        (!objective->has_carrier && request->carrier.registry) ||
        (!objective->has_target && request->target.registry))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC objective change requires its original call and storage");
    application_qc_inputs inputs = {.self = request->target, .other = request->carrier,
        .activator = request->target, .amount = value->value, .time_ns = objectives->engine->source_time_ns};
    return application_qc_run_call(objectives->engine, &objective->change, &inputs, NULL, error) &&
        callback_current(objectives, error);
}

static bool project_word(qa_qc_instance *vm, const qc_objective_location *location,
    uint32_t word, qa_error *error)
{
    if (!location->entity) return qa_qc_stage_globals(vm, location->word, &word, 1, error);
    int32_t value;
    memcpy(&value, &word, sizeof(value));
    return qa_qc_project_entity_int(vm, location->reference, location->word, value, error);
}

static bool project(qc_objective *objective, const qa_objective_state *state_value,
    qa_error *error)
{
    struct application_qc_objectives *objectives = objective->objectives;
    qa_qc_instance *vm = objectives->engine->provider->state.qc.instance;
    const qc_objective_value *value = stage(objective, state_value->stage);
    if (!value || value->complete != state_value->complete ||
        (!objective->has_carrier && state_value->carrier.registry) ||
        (!objective->has_target && state_value->target.registry))
        return application_fail(error, QA_ERROR_FORMAT, "QC borrowed objective cannot represent its canonical stage and references");
    int32_t carrier = 0, target = 0;
    if ((state_value->carrier.registry && !application_qc_reference(objectives->engine, state_value->carrier, &carrier, error)) ||
        (state_value->target.registry && !application_qc_reference(objectives->engine, state_value->target, &target, error))) return false;
    qc_objective_location location;
    uint32_t scalar;
    memcpy(&scalar, &value->value, sizeof(scalar));
    if (!resolve_location(objectives, &objective->state, &location, error) ||
        !project_word(vm, &location, scalar, error)) return false;
    if (objective->has_carrier &&
        (!resolve_location(objectives, &objective->carrier, &location, error) ||
         !project_word(vm, &location, (uint32_t)carrier, error))) return false;
    if (objective->has_target &&
        (!resolve_location(objectives, &objective->target, &location, error) ||
         !project_word(vm, &location, (uint32_t)target, error))) return false;
    if (!locations(objective, objective->locations, error)) return false;
    objective->projected = *state_value;
    objective->has_projection = true;
    return current(objectives, error);
}

static bool refresh(struct application_qc_objectives *objectives, qa_error *error)
{
    for (size_t i = 0; i < objectives->count; ++i) {
        qc_objective *objective = objectives->values + i;
        if (objective->owned) continue;
        qa_objective_state state_value;
        if (!qa_modes_objective(objectives->modes, (qa_mode_id){0}, objective->id, &state_value, error) ||
            !current(objectives, error) || !project(objective, &state_value, error)) return false;
    }
    return true;
}

static bool flush(struct application_qc_objectives *objectives, qa_error *error)
{
    for (size_t i = 0; i < objectives->count; ++i) {
        qc_objective *objective = objectives->values + i;
        if (objective->owned || !objective->has_projection) continue;
        qc_objective_location location[3];
        if (!locations(objective, location, error)) return false;
        if (!same_locations(objective->locations, location)) {
            objective->has_projection = false;
            continue;
        }
        qa_objective_state state_value;
        if (!source_read(objective, location, &state_value, error)) return false;
        if (state_value.stage == objective->projected.stage &&
            qa_actor_id_equal(state_value.carrier, objective->projected.carrier) &&
            qa_actor_id_equal(state_value.target, objective->projected.target)) continue;
        if (!objective->writable)
            return application_fail(error, QA_ERROR_ARGUMENT, "QC source wrote a read-only borrowed objective");
        if (!qa_modes_change_objective(objectives->modes, (qa_mode_id){0}, objective->id, &state_value, error) ||
            !current(objectives, error)) return false;
    }
    return true;
}

bool application_qc_objectives_sync(struct application_qc_state *engine, qa_error *error)
{
    struct application_qc_objectives *objectives = adapter(engine);
    if (!objectives || objectives->busy) return true;
    if (!objectives->active) {
        if (engine->provider->application->operation == APPLICATION_PERSISTING) return true;
        objectives->engine = engine;
        objectives->modes = engine->provider->application->modes;
    }
    if (!current(objectives, error)) return false;
    objectives->busy = true;
    bool okay = flush(objectives, error) && refresh(objectives, error);
    objectives->busy = false;
    return okay;
}

bool application_qc_objectives_suspend(struct application_qc_state *engine, qa_error *error)
{
    struct application_qc_objectives *objectives = adapter(engine);
    if (!objectives) return true;
    if (objectives->busy)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC objective source is borrowed by its canonical callback");
    if (objectives->modes && objectives->modes != engine->provider->application->modes)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC objective bindings outlived their canonical modes");
    for (size_t i = 0; i < objectives->count; ++i) {
        qc_objective *objective = objectives->values + i;
        if (objective->lease.serial && !qa_modes_unbind_objective(objectives->modes, objective->lease, error)) return false;
        objective->lease = (qa_objective_lease){0};
        objective->has_projection = false;
    }
    objectives->active = false;
    objectives->engine = NULL;
    objectives->modes = NULL;
    return true;
}

bool application_qc_objectives_activate(struct application_qc_state *engine, qa_error *error)
{
    struct application_qc_objectives *objectives = adapter(engine);
    if (!objectives) return true;
    if (objectives->active) return application_qc_objectives_sync(engine, error);
    objectives->engine = engine;
    objectives->modes = engine->provider->application->modes;
    if (!objectives->modes || !callback_current(objectives, error)) return false;
    for (size_t i = 0; i < objectives->count; ++i) {
        qc_objective *objective = objectives->values + i;
        if (!objective->owned) continue;
        qa_objective_state state_value;
        if (!read(objective, &state_value, error)) goto failed;
        qa_objective_binding binding = {.owner = engine->provider->owner, .id = objective->id,
            .campaign_gate = objective->campaign_gate, .bot_goal = objective->bot_goal,
            .context = objective, .read = read, .change = objective->has_change ? change : NULL};
        if (!qa_modes_bind_objective(objectives->modes, &binding, &objective->lease, error)) goto failed;
    }
    objectives->active = true;
    objectives->busy = true;
    bool okay = refresh(objectives, error);
    objectives->busy = false;
    if (okay) return true;
failed:
    {
        qa_error cleanup = {0};
        if (!application_qc_objectives_suspend(engine, &cleanup) && error) *error = cleanup;
    }
    return false;
}

bool application_qc_objectives_restored(struct application_qc_state *engine, qa_error *error)
{
    return application_qc_objectives_suspend(engine, error) && application_qc_objectives_activate(engine, error);
}

bool application_qc_objectives_ready(const struct application_qc_state *engine, qa_error *error)
{
    struct application_qc_objectives *objectives = adapter(engine);
    if (!objectives) return true;
    if (objectives->busy)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC objective checkpoint intersects a canonical callback");
    if (!objectives->active) return true;
    if (!current(objectives, error)) return false;
    for (size_t i = 0; i < objectives->count; ++i)
        if ((objectives->values[i].lease.serial != 0) != objectives->values[i].owned)
            return application_fail(error, QA_ERROR_FORMAT, "QC objective differs from its actual owner binding");
    return true;
}

void application_qc_objectives_release(struct application_qc_profile *profile)
{
    struct application_qc_objectives *objectives = profile->objectives;
    if (!objectives) return;
    for (size_t i = 0; i < objectives->count; ++i) {
        qc_objective *objective = objectives->values + i;
        free(objective->state.indirections);
        free(objective->carrier.indirections);
        free(objective->target.indirections);
        free(objective->values);
        application_qc_call_free(&objective->change);
    }
    free(objectives->values);
    free(objectives);
    profile->objectives = NULL;
}
