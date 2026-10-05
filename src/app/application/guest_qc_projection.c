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
    const qa_qc_game_fields *fields = qa_qc_game_resolved_fields(engine->provider->state.qc.game);
    if (!fields || !fields->solid || fields->solid->type != QA_QC_FLOAT ||
        !fields->flags || fields->flags->type != QA_QC_FLOAT ||
        !fields->owner || fields->owner->type != QA_QC_ENTITY)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC engine field is missing or has a different type");
    float solid, flags;
    int32_t owner;
    if (!qa_qc_entity_float(vm, row->reference, fields->solid->offset, &solid, error) ||
        !qa_qc_entity_float(vm, row->reference, fields->flags->offset, &flags, error) ||
        !qa_qc_entity_int(vm, row->reference, fields->owner->offset, &owner, error)) return false;
    if (!isfinite(solid) || !isfinite(flags) || (double)flags < INT32_MIN || (double)flags > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Invalid QuakeC collision flags");
    uint32_t bits = (uint32_t)(int32_t)flags;
    qa_actor_collision value = {.family = QA_COLLISION_Q1, .shape = QA_SHAPE_BOX,
        .contents = solid == 0 || solid == 1 ? 0 : 1,
        .role = solid == 1 ? QA_COLLISION_TRIGGER : QA_COLLISION_SOLID,
        .monster = (bits & 32u) != 0, .q1_corpse = solid == 5 && engine->profile == QA_QC_RERELEASE};
    if (solid == 4) {
        float model;
        if (!fields->modelindex || fields->modelindex->type != QA_QC_FLOAT)
            return application_fail(error, QA_ERROR_FORMAT, "QuakeC engine field is missing or has a different type");
        if (!qa_qc_entity_float(vm, row->reference, fields->modelindex->offset, &model, error)) return false;
        int32_t index = qa_source_float_to_i32(model);
        const application_qc_resource *resource = NULL;
        if (isfinite(model) && index > 0)
            for (size_t i = 0; i < engine->resource_count; ++i)
                if (engine->resources[i].kind == QA_QC_RESOURCE_MODEL &&
                    engine->resources[i].value.index == (uint32_t)index) {
                    resource = &engine->resources[i]; break;
                }
        /* Stock doors/plats link their origin before setmodel installs the
         * brush index. SV_LinkEdict uses the current box; hull queries require
         * the actual precached brush and remain strict. */
        bool pending = model == 0 && qa_world_collision_link_observation(engine->world, expected);
        if ((!resource || !resource->has_inline_model) && !pending)
            return application_fail(error, QA_ERROR_FORMAT, "QuakeC brush solid has no retained inline model");
        if (!pending) { value.inline_model = true; value.model = resource->inline_model; }
    }
    if (owner && !qa_qc_reference_actor(vm, owner, &value.owner, error)) return false;
    if (!qa_qc_reference_actor(vm, row->reference, &current, error) || !qa_actor_id_equal(current, expected))
        return application_fail(error, QA_ERROR_NOT_FOUND, "QuakeC collision actor changed during projection");
    *out = value; return true;
}
static bool bind_entity(struct application_qc_state *engine, qa_qc_instance *vm,
                          const qa_qc_entity_access *access, qa_error *error)
{
    if (engine->projecting) return true;
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
    return application_qc_combat_bind(engine, vm, access, error);
}
bool application_qc_prepare_entity(void *opaque, qa_qc_instance *vm,
                                     const qa_qc_entity_access *access, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (engine->projecting || access->binding.kind == QA_QC_SLOT_WORLD || access->binding.kind == QA_QC_SLOT_FREE) return true;
    if (access->kind != QA_QC_ENTITY_BIND)
        return application_qc_project_declared(engine, vm, access, error);
    if (application_qc_restore_pending(engine)) return true;
    return bind_entity(engine, vm, access, error);
}
bool application_qc_bind_entities(application_provider *provider, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC
        ? provider->state.qc.engine : NULL;
    if (!engine || !engine->initialized || engine->loading) return true;
    qa_qc_instance *vm = provider->state.qc.instance;
    for (uint32_t slot = 1; slot < qa_qc_entity_count(vm); ++slot) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(vm, slot, &binding) || (binding.kind != QA_QC_SLOT_OWNED &&
            binding.kind != QA_QC_SLOT_BORROWED)) continue;
        int32_t reference; qa_actor_id actor;
        if (!qa_qc_slot_reference(vm, slot, &reference, error) ||
            !qa_qc_reference_actor(vm, reference, &actor, error)) return false;
        qa_qc_entity_access access = {.kind = QA_QC_ENTITY_BIND,
            .binding = binding, .reference = reference};
        if (!bind_entity(engine, vm, &access, error)) return false;
    }
    return true;
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
    const qa_qc_game_fields *fields = qa_qc_game_resolved_fields(engine->provider->state.qc.game);
    if (!fields)
        return application_fail(error, QA_ERROR_NOT_FOUND, "QuakeC body store lost its retained game fields");
    const qa_qc_definition *vectors[] = {fields->origin, fields->angles, fields->velocity};
    for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        const qa_qc_definition *def = vectors[i];
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
