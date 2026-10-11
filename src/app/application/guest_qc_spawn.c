#include "guest_qc_spawn.h"
#include "guest_qc_items.h"
#include "supplies.h"

static bool client_current(struct application_qc_state *engine, qa_qc_instance *vm,
    uint32_t slot, qa_actor_id actor, qa_error *error)
{
    const application_qc_client *client = engine->clients + slot;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), actor);
    qa_qc_slot_binding binding;
    if (record && client->connected && !client->spectator &&
        qa_actor_id_equal(client->actor, actor) && qa_qc_slot(vm, slot, &binding) &&
        (binding.kind == QA_QC_SLOT_OWNED || binding.kind == QA_QC_SLOT_BORROWED) &&
        qa_actor_id_equal(binding.actor, actor) && binding.owner == record->owner &&
        binding.source_slot == (record->has_source ? record->source_slot : 0)) return true;
    return application_fail(error, QA_ERROR_ARGUMENT, "QC completed spawn lost its actual physical client binding");
}

bool application_qc_spawn_call(void *opaque, qa_qc_instance *vm,
    const qa_qc_call_event *event, qa_qc_call_next next, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    application_provider *provider = engine ? engine->provider : NULL;
    const qa_qc_function *function = provider && event ?
        qa_qc_program_function(provider->state.qc.program, event->function) : NULL;
    if (!function || strcmp(function->name, "PutClientInServer") ||
        provider->state.qc.qualified || engine->loading || !engine->initialized ||
        provider->application->q1_original_save)
        return qa_qc_call_continue(next, error);

    uint32_t declared;
    if (qa_qc_program_find_function(provider->state.qc.program, "PutClientInServer", &declared) != function ||
        declared != event->function || function->named_builtin || function->first_statement < 0 ||
        vm != provider->state.qc.instance || next.instance != vm ||
        qa_qc_instance_program(vm) != provider->state.qc.program ||
        provider->state.qc.engine != engine || !provider->constructed || !provider->attached ||
        provider->close_pending || !engine->clients)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC spawn continuation has no genuine source function owner");
    const qa_qc_definition *self = provider->state.qc.engine->global_bindings->self;
    int32_t reference;
    if (!self || self->type != QA_QC_ENTITY)
        return application_fail(error, QA_ERROR_FORMAT, "QC spawn continuation has no typed source self");
    if (!qa_qc_global_int(vm, self->offset, &reference, error)) return false;
    qa_actor_id actor;
    if (!qa_qc_reference_actor(vm, reference, &actor, error)) return false;
    uint32_t slot = 0;
    for (uint32_t i = 1; i <= engine->max_clients; ++i)
        if (engine->clients[i].connected && !engine->clients[i].spectator &&
            qa_actor_id_equal(engine->clients[i].actor, actor)) { slot = i; break; }
    if (!slot) return qa_qc_call_continue(next, error);
    if (!client_current(engine, vm, slot, actor, error) || !qa_qc_call_continue(next, error)) return false;
    if (!qa_qc_call_completed(next)) return true;
    if (!qa_actors_get(qa_session_actors(engine->services.session), actor)) return true;
    /* The real PutClientInServer body has completed before supplies observe
     * its items. Publish that client stage before binding the Source fields. */
    engine->clients[slot].spawned = true;
    return client_current(engine, vm, slot, actor, error) &&
        application_qc_items_admit(engine,actor,error) &&
        application_supplies_source_spawned(provider, actor, error);
}
