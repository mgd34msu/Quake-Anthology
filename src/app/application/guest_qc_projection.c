#include "guest_qc_profile.h"
#include "guest_qc_combat.h"

static bool controls_body(struct application_qc_state *engine, qa_actor_id actor)
{
    application_provider *provider = application_provider_for(engine->provider->application, actor, QA_ROLE_MOVEMENT, "");
    if (provider != NULL) return provider == engine->provider;
    qa_actor_owner execution;
    return qa_session_execution(engine->services.session, actor, &execution) && execution == engine->provider->owner;
}
bool application_qc_may_move(void *opaque, qa_actor_id actor)
{
    return controls_body(opaque, actor);
}
static bool collision_read(void *opaque, qa_actor_collision *out, qa_error *error)
{
    application_qc_actor *row = opaque;
    struct application_qc_state *engine = row->engine;
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    qa_actor_id expected = row->actor, current;
    if (!qa_qc_reference_actor(vm, row->reference, &current, error) || !qa_actor_id_equal(current, expected))
        return application_fail(error, QA_ERROR_NOT_FOUND, "QuakeC collision binding lost its actor generation");
    float solid, flags;
    const qa_qc_definition *model_field = application_qc_field(engine, "model", QA_QC_STRING, error);
    const qa_qc_definition *owner_field = application_qc_field(engine, "owner", QA_QC_ENTITY, error);
    int32_t model, owner; const char *name;
    if (!application_qc_float(engine, row->reference, "solid", &solid, error) ||
        !application_qc_float(engine, row->reference, "flags", &flags, error) || model_field == NULL || owner_field == NULL ||
        !qa_qc_entity_int(vm, row->reference, model_field->offset, &model, error) || !qa_qc_string(vm, model, &name, error) ||
        !qa_qc_entity_int(vm, row->reference, owner_field->offset, &owner, error)) return false;
    if (!isfinite(solid) || !isfinite(flags) || (double)flags < INT32_MIN || (double)flags > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Invalid QuakeC collision flags");
    uint32_t bits = (uint32_t)(int32_t)flags;
    qa_actor_collision value = {.family = QA_COLLISION_Q1, .shape = QA_SHAPE_BOX,
        .contents = solid == 0 || solid == 1 ? 0 : 1,
        .role = solid == 1 ? QA_COLLISION_TRIGGER : QA_COLLISION_SOLID,
        .monster = (bits & 32u) != 0, .q1_corpse = solid == 5 && engine->profile == QA_QC_RERELEASE};
    if (solid == 4) {
        double number;
        if (*name != '*' || !qa_parse_number((qa_bytes){(const uint8_t *)name + 1, strlen(name + 1)}, &number, error) ||
            !isfinite(number) || number < 0 || number > UINT32_MAX || trunc(number) != number)
            return application_fail(error, QA_ERROR_FORMAT, "QuakeC brush solid has no inline model");
        value.inline_model = true; value.model = (uint32_t)number;
    }
    if (owner && !qa_qc_reference_actor(vm, owner, &value.owner, error)) return false;
    if (!qa_qc_reference_actor(vm, row->reference, &current, error) || !qa_actor_id_equal(current, expected))
        return application_fail(error, QA_ERROR_NOT_FOUND, "QuakeC collision actor changed during projection");
    *out = value; return true;
}
bool application_qc_prepare_entity(void *opaque, qa_qc_instance *vm,
                                     const qa_qc_entity_access *access, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (engine->projecting || access->binding.kind == QA_QC_SLOT_WORLD || access->binding.kind == QA_QC_SLOT_FREE) return true;
    qa_actor_id actor = access->binding.actor;
    if (actor.slot >= engine->actor_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC projected actor exceeds application capacity");
    if (controls_body(engine, actor) && (!engine->provider->state.qc.qualified || access->binding.kind == QA_QC_SLOT_OWNED)) {
        application_qc_actor *row = &engine->actors[actor.slot];
        if (!qa_actor_id_equal(row->actor, actor)) *row = (application_qc_actor){.engine = engine, .actor = actor, .reference = access->reference};
        if (!row->collision_bound) {
            qa_collision_binding binding = {.context = row, .read = collision_read};
            if (!qa_world_collision_bind(engine->world, actor, &binding, error)) return false;
            row->collision_bound = true;
        }
    }
    return application_qc_combat_prepare(engine, vm, access, error) &&
        application_qc_project_declared(engine, vm, access, error);
}
bool application_qc_project_body_store(struct application_qc_state *engine, qa_qc_instance *vm,
                                         const qa_qc_store_event *event, qa_error *error)
{
    if (engine->projecting || event->kind != QA_QC_STORE_ENTITY || event->entity_reference == 0) return true;
    qa_actor_id actor;
    if (!qa_qc_reference_actor(vm, event->entity_reference, &actor, error)) return false;
    qa_qc_entity_layout layout = qa_qc_default_entity_layout(engine->provider->state.qc.program, engine->profile);
    qa_qc_slot_binding binding;
    if (!qa_qc_slot(vm, (uint32_t)event->entity_reference / layout.stride_bytes, &binding)) return false;
    if (binding.kind != QA_QC_SLOT_BORROWED || !controls_body(engine, actor)) return true;
    static const char *names[] = {"origin", "angles", "velocity"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const qa_qc_definition *def = qa_qc_program_find_field(engine->provider->state.qc.program, names[i]);
        if (def == NULL || def->type != QA_QC_VECTOR || def->offset >= event->word + event->count || (uint32_t)def->offset + 3u <= event->word) continue;
        qa_body_state body;
        if (!qa_world_body_read(engine->world, actor, &body, error)) return false;
        qa_vec3 *vector = i == 0 ? &body.origin : i == 1 ? &body.angles : &body.velocity;
        float values[3] = {vector->x, vector->y, vector->z};
        for (uint32_t component = 0; component < 3; ++component) {
            uint32_t word = def->offset + component;
            if (word >= event->word && word < event->word + event->count)
                memcpy(&values[component], &event->after[word - event->word], sizeof(float));
        }
        *vector = qa_v3(values[0], values[1], values[2]);
        if (!qa_vec_finite(*vector)) return application_fail(error, QA_ERROR_FORMAT, "Nonfinite QuakeC shared body store");
        if (!qa_world_body_write(engine->world, actor, &body, error)) return false;
    }
    return true;
}
