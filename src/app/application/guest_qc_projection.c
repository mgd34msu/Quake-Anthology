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
        int32_t stride;
        if(!qa_qc_slot_reference(vm,1,&stride,error) ||
           !qa_qc_entity_collision_fields(vm,(uint32_t)access->reference/(uint32_t)stride,
                                          &row->collision_fields,error)) return false;
        row->collision_fields.models=&engine->model_fields;
        if (!row->collision_bound) {
            qa_collision_binding binding = {.context = row, .fields = &row->collision_fields};
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
