#include "network_q1_source.h"
#include "native_q1_wire.h"
#include "qa/application_network.h"
#include "qa/application_qc_presentation.h"

struct application_qc_state *application_network_q1_qc_observation(qa_application *app,
    qa_actor_owner expected_owner, qa_error *error)
{
    application_provider *primary = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    qa_application_qc_message_source source; bool found;
    if (!primary || (expected_owner && primary->owner != expected_owner) ||
        qa_application_get_state(app) != QA_APPLICATION_RUNNING) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q1 observation lost its primary Source owner");
        return NULL;
    }
    if (!qa_application_qc_message_source_read(app, primary->owner, &source, &found, error)) return NULL;
    if (!found) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q1 observation has no QC Source");
        return NULL;
    }
    return primary->state.qc.engine;
}

struct application_qc_state *application_network_q1_qc_source(qa_application *app,
    qa_actor_owner expected_owner, qa_error *error)
{
    application_provider *provider = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    if (!app || app->destroy_requested || app->state != QA_APPLICATION_RUNNING ||
        !engine || engine->provider != provider || !provider->constructed || !provider->attached ||
        provider->close_pending || (expected_owner && provider->owner != expected_owner) ||
        provider->state.qc.qualified || !provider->state.qc.instance ||
        !engine->initialized || engine->loading || engine->projecting || !engine->clients ||
        !engine->cvars || !engine->max_clients || engine->max_clients > 255 ||
        (engine->profile != QA_QC_NETQUAKE && engine->profile != QA_QC_QUAKEWORLD) ||
        (engine->profile == QA_QC_QUAKEWORLD && engine->max_clients > 32) ||
        engine->protocol.flags || engine->protocol.revision ||
        engine->protocol.kind != (engine->profile == QA_QC_QUAKEWORLD ? QA_NET_QW28 : QA_NET_NQ15) ||
        qa_qc_entity_count(provider->state.qc.instance) <= engine->max_clients ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) ||
        !qa_world_idle(app->world) || !qa_qc_idle(provider->state.qc.instance) ||
        !application_qc_input_idle(provider)) {
        application_fail(error, QA_ERROR_UNSUPPORTED,
            "Original Q1 wire requires its installed primary classic source owner");
        return NULL;
    }
    return engine;
}

static bool binding_current(struct application_qc_state *engine, uint32_t slot,
    qa_actor_id actor, qa_qc_slot_binding *out, qa_error *error)
{
    const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), actor);
    if (!record || !qa_qc_slot(engine->provider->state.qc.instance, slot, out) ||
        out->slot != slot || !qa_actor_id_equal(out->actor, actor) ||
        out->owner != record->owner ||
        out->source_slot != (record->has_source ? record->source_slot : 0))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 source row lost its full canonical actor binding");
    return true;
}

bool application_network_q1_qc_client(struct application_qc_state *engine,
    qa_actor_id actor, uint32_t *out, qa_error *error)
{
    if (!engine || !engine->clients || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 physical client owner");
    uint32_t slot = 0;
    for (uint32_t i = 1; i <= engine->max_clients; ++i) {
        if (!engine->clients[i].connected || !qa_actor_id_equal(engine->clients[i].actor, actor)) continue;
        if (slot) return application_fail(error, QA_ERROR_FORMAT, "Q1 actor occupies multiple physical client rows");
        slot = i;
    }
    qa_qc_slot_binding binding;
    if (!slot || !binding_current(engine, slot, actor, &binding, error) ||
        (binding.kind != QA_QC_SLOT_BORROWED &&
         (binding.kind != QA_QC_SLOT_OWNED || engine->profile != QA_QC_QUAKEWORLD ||
          binding.owner != engine->provider->owner || binding.source_slot != slot)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 client has no genuine physical source row");
    *out = slot;
    return true;
}

bool application_network_q1_qc_entity(struct application_qc_state *engine,
    qa_actor_id actor, uint32_t *source_slot, int32_t *reference, qa_error *error)
{
    if (!engine || !reference)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 physical entity owner");
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    uint32_t count = qa_qc_entity_count(vm), slot = 0;
    for (uint32_t i = 1; i < count; ++i) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(vm, i, &binding))
            return application_fail(error, QA_ERROR_FORMAT, "Q1 source entity allocation disappeared");
        if (binding.kind == QA_QC_SLOT_FREE || !qa_actor_id_equal(binding.actor, actor)) continue;
        if (slot) return application_fail(error, QA_ERROR_FORMAT, "Q1 actor aliases multiple source edicts");
        if (!binding_current(engine, i, actor, &binding, error)) return false;
        if (binding.kind == QA_QC_SLOT_BORROWED) {
            uint32_t client;
            if (!application_network_q1_qc_client(engine, actor, &client, error) || client != i)
                return application_fail(error, QA_ERROR_ARGUMENT, "Q1 borrowed edict has no admitted source client");
        } else if (binding.kind != QA_QC_SLOT_OWNED || binding.owner != engine->provider->owner ||
            binding.source_slot != i)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 owned edict differs from its source namespace");
        slot = i;
    }
    if (!slot || slot > UINT16_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 entity has no representable physical source edict");
    if (!qa_qc_actor_reference(vm, actor, false, reference, error)) return false;
    if (source_slot) *source_slot = slot;
    return true;
}

bool qa_application_network_q1_host_source(qa_application *app,
    qa_application_network_q1_host *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 primary source observation");
    application_provider *primary = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    if (primary && primary->kind == APPLICATION_PROVIDER_Q1)
        return application_native_q1_wire_host(app, out, error);
    struct application_qc_state *engine = application_network_q1_qc_source(app, 0, error);
    if (!engine) return false;
    *out = (qa_application_network_q1_host){.owner = engine->provider->owner,
        .protocol = engine->protocol, .client_slots = engine->max_clients,
        .entity_slots = qa_qc_entity_count(engine->provider->state.qc.instance)};
    return true;
}
