#include "guest_qc_objectives.h"
#include "guest_qc_profile.h"
#include "source_objectives.h"
#include <math.h>

typedef struct qc_objective_storage {
    const qa_qc_definition *global, *field;
    const qa_qc_definition **indirections;
    size_t indirection_count;
} qc_objective_storage;

typedef struct qc_objective {
    qc_objective_storage state, carrier, target;
    application_qc_call change;
    bool has_carrier, has_target;
} qc_objective;

struct application_qc_objectives {
    qc_objective *values;
    size_t count;
    struct application_qc_state *engine;
    application_source_objectives *shared;
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


static struct application_qc_objectives *adapter(const struct application_qc_state *engine)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    return profile ? profile->objectives : NULL;
}

static bool current(void *context, bool callback, qa_error *error)
{
    const struct application_qc_objectives *objectives = context;
    const struct application_qc_state *engine = objectives->engine;
    const application_provider *provider = engine ? engine->provider : NULL;
    return (provider && provider->state.qc.engine == engine && provider->state.qc.instance &&
        provider->state.qc.game && provider->constructed && provider->attached && !provider->close_pending &&
        !provider->application->destroy_requested && adapter(engine) == objectives &&
        (!callback || (engine->initialized && !engine->loading))) ||
        application_fail(error, QA_ERROR_ARGUMENT, "QC objective lost its actual source map");
}

static bool normalize(void *context, size_t index, double *value, qa_error *error)
{
    (void)context; (void)index;
    *value = (float)*value;
    return isfinite(*value) || application_fail(error, QA_ERROR_FORMAT, "QC objective exceeds float storage");
}

static bool resolve_location(struct application_qc_objectives *objectives,
    const qc_objective_storage *storage_value, application_source_objective_location *out, qa_error *error)
{
    qa_qc_instance *vm = objectives->engine->provider->state.qc.instance;
    application_source_objective_location location = {.address = storage_value->global->offset};
    if (storage_value->field) {
        int32_t reference;
        if (!qa_qc_global_int(vm, storage_value->global->offset, &reference, error)) return false;
        for (size_t i = 0; i <= storage_value->indirection_count; ++i) {
            if (reference) {
                if (!qa_qc_reference_actor(vm, reference, &location.actor, error)) return false;
            } else {
                qa_qc_slot_binding world;
                location.actor = (qa_actor_id){0};
                if (!qa_qc_slot(vm, 0, &world) || world.kind != QA_QC_SLOT_WORLD)
                    return application_fail(error, QA_ERROR_NOT_FOUND, "QC objective selector lost its real world row");
            }
            if (i == storage_value->indirection_count) break;
            if (!qa_qc_entity_int(vm, reference, storage_value->indirections[i]->offset, &reference, error)) return false;
        }
        location.address = storage_value->field->offset;
        location.selector = (UINT64_C(1) << 32) | (uint32_t)reference;
    }
    *out = location; return true;
}

static bool locations(void *context, size_t index, application_source_objective_location out[3], qa_error *error)
{
    struct application_qc_objectives *objectives = context;
    const qc_objective *row = objectives->values + index;
    memset(out, 0, 3 * sizeof(*out));
    return resolve_location(objectives, &row->state, out, error) &&
        (!row->has_carrier || resolve_location(objectives, &row->carrier, out + 1, error)) &&
        (!row->has_target || resolve_location(objectives, &row->target, out + 2, error));
}

static bool actor_read(qa_qc_instance *vm, const application_source_objective_location *location,
    qa_actor_id *out, qa_error *error)
{
    int32_t reference;
    if (!(location->selector >> 32 ? qa_qc_entity_int(vm, (int32_t)(uint32_t)location->selector,
        (uint32_t)location->address, &reference, error) :
        qa_qc_global_int(vm, (uint32_t)location->address, &reference, error))) return false;
    if (!reference) { *out = (qa_actor_id){0}; return true; }
    return qa_qc_reference_actor(vm, reference, out, error);
}

static bool source_read(void *context, size_t index, const application_source_objective_location location[3],
    double *value, qa_actor_id *carrier, qa_actor_id *target, qa_error *error)
{
    struct application_qc_objectives *objectives = context;
    const qc_objective *row = objectives->values + index;
    qa_qc_instance *vm = objectives->engine->provider->state.qc.instance;
    float scalar;
    if (!(location->selector >> 32 ? qa_qc_entity_float(vm, (int32_t)(uint32_t)location->selector,
        (uint32_t)location->address, &scalar, error) :
        qa_qc_global_float(vm, (uint32_t)location->address, &scalar, error))) return false;
    *value = scalar;
    return (!row->has_carrier || actor_read(vm, location + 1, carrier, error)) &&
        (!row->has_target || actor_read(vm, location + 2, target, error));
}

static bool project_word(qa_qc_instance *vm, const application_source_objective_location *location,
    uint32_t word, qa_error *error)
{
    if (!(location->selector >> 32)) return qa_qc_stage_globals(vm, (uint32_t)location->address, &word, 1, error);
    int32_t value; memcpy(&value, &word, sizeof(value));
    return qa_qc_project_entity_int(vm, (int32_t)(uint32_t)location->selector, (uint32_t)location->address, value, error);
}

static bool source_write(void *context, size_t index, const application_source_objective_location location[3],
    double value, qa_actor_id carrier, qa_actor_id target, qa_error *error)
{
    struct application_qc_objectives *objectives = context;
    const qc_objective *row = objectives->values + index;
    qa_qc_instance *vm = objectives->engine->provider->state.qc.instance;
    int32_t carrier_reference = 0, target_reference = 0;
    if ((carrier.registry && !application_qc_reference(objectives->engine, carrier, &carrier_reference, error)) ||
        (target.registry && !application_qc_reference(objectives->engine, target, &target_reference, error))) return false;
    float scalar = (float)value; uint32_t word;
    memcpy(&word, &scalar, sizeof(word));
    return project_word(vm, location, word, error) &&
        (!row->has_carrier || project_word(vm, location + 1, (uint32_t)carrier_reference, error)) &&
        (!row->has_target || project_word(vm, location + 2, (uint32_t)target_reference, error));
}

static bool source_change(void *context, size_t index, double value, const qa_objective_state *request, qa_error *error)
{
    struct application_qc_objectives *objectives = context;
    application_qc_inputs inputs = {.self = request->target, .other = request->carrier,
        .activator = request->target, .amount = (float)value, .time_ns = objectives->engine->source_time_ns};
    return application_qc_run_call(objectives->engine, &objectives->values[index].change, &inputs, NULL, error);
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
        qc_objective *objective = objectives->values + i;
        if (!storage(doc, qa_json_get(doc, state, "storage"), program, QA_QC_FLOAT, &objective->state, error)) return false;
        qa_json_id carrier = qa_json_get(doc, row, "carrier"), target = qa_json_get(doc, row, "target");
        objective->has_carrier = qa_json_type(doc, carrier) != QA_JSON_NULL;
        objective->has_target = qa_json_type(doc, target) != QA_JSON_NULL;
        if ((objective->has_carrier && !storage(doc, carrier, program, QA_QC_ENTITY, &objective->carrier, error)) ||
            (objective->has_target && !storage(doc, target, program, QA_QC_ENTITY, &objective->target, error))) return false;
        qa_json_id change = qa_json_get(doc, row, "change");
        if (qa_json_string_equal(doc, qa_json_get(doc, row, "role"), "owned") && qa_json_type(doc, change) != QA_JSON_NULL) {
            uint64_t available = (UINT64_C(1) << QC_INPUT_SELF) | (UINT64_C(1) << QC_INPUT_OTHER) |
                (UINT64_C(1) << QC_INPUT_ACTIVATOR) | (UINT64_C(1) << QC_INPUT_AMOUNT) | (UINT64_C(1) << QC_INPUT_TIME);
            if (!application_qc_call_parse(doc, change, program, available, false, &objective->change, error)) return false;
        }
    }
    application_source_objective_services services = {.context = objectives, .current = current,
        .normalize = normalize, .locate = locations, .read = source_read, .write = source_write, .change = source_change};
    return application_source_objectives_create(qa_session_strings(provider->application->session), provider->owner,
        doc, node, &services, &objectives->shared, error);
}

bool application_qc_objectives_sync(struct application_qc_state *engine, qa_error *error)
{
    struct application_qc_objectives *objectives = adapter(engine);
    if (!objectives) return true;
    if (!application_source_objectives_active(objectives->shared) &&
        engine->provider->application->operation == APPLICATION_PERSISTING) return true;
    objectives->engine = engine;
    return application_source_objectives_sync(objectives->shared, engine->provider->application->modes, error);
}

bool application_qc_objectives_suspend(struct application_qc_state *engine, qa_error *error)
{
    struct application_qc_objectives *objectives = adapter(engine);
    if (!objectives) return true;
    if (!application_source_objectives_suspend(objectives->shared, error)) return false;
    objectives->engine = NULL; return true;
}

bool application_qc_objectives_activate(struct application_qc_state *engine, qa_error *error)
{
    struct application_qc_objectives *objectives = adapter(engine);
    if (!objectives) return true;
    objectives->engine = engine;
    return application_source_objectives_activate(objectives->shared, engine->provider->application->modes, error);
}

bool application_qc_objectives_restored(struct application_qc_state *engine, qa_error *error)
{ return application_qc_objectives_suspend(engine, error) && application_qc_objectives_activate(engine, error); }

bool application_qc_objectives_ready(const struct application_qc_state *engine, qa_error *error)
{
    struct application_qc_objectives *objectives = adapter(engine);
    return !objectives || application_source_objectives_ready(objectives->shared, error);
}

void application_qc_objectives_release(struct application_qc_profile *profile)
{
    struct application_qc_objectives *objectives = profile->objectives;
    if (!objectives) return;
    for (size_t i = 0; i < objectives->count; ++i) {
        qc_objective *objective = objectives->values + i;
        free(objective->state.indirections); free(objective->carrier.indirections); free(objective->target.indirections);
        application_qc_call_free(&objective->change);
    }
    application_source_objectives_destroy(objectives->shared);
    free(objectives->values); free(objectives); profile->objectives = NULL;
}
