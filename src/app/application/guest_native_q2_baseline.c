#include "guest_native_q2_private.h"
#include "guest_native_q2_baseline.h"

struct application_native_q2_baseline {
    application_provider *target, *baseline;
    qa_native_host *baseline_host;
    qa_native_host_reconstruction *services;
};
static bool fresh(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider->state.native.q2_engine;
    if (engine->initialized || engine->map_ready || provider->map_bound)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native baseline source owner already initialized or published a map");
    const qa_actor_registry *actors = qa_session_actors(provider->application->session);
    const qa_actor_record *world = qa_actors_get(actors, engine->world_actor);
    if (!world || world->owner != provider->owner || !world->has_source || world->source_slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native baseline needs its fresh static world actor");
    uint32_t cursor = 0; const qa_actor_record *record;
    while (qa_actors_next(actors, &cursor, &record))
        if (record->owner == provider->owner &&
            (!qa_actor_id_equal(record->id, world->id) ||
                qa_world_body_storage_serial(provider->application->world, record->id)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native baseline source owner has published actors or bodies");
    qa_native_entity_table table;
    return qa_native_entity_table_get(qa_native_host_instance(provider->state.native.host), &table, error) &&
        (!table.capacity || application_fail(error, QA_ERROR_ARGUMENT, "Native baseline host already publishes source entity storage"));
}
static bool drained(struct application_native_q2_baseline *phase, qa_error *error)
{
    struct application_native_q2 *target = phase ? phase->target->state.native.q2_engine : NULL;
    struct application_native_q2 *baseline = phase ? phase->baseline->state.native.q2_engine : NULL;
    return (target && baseline && target->baseline == phase && baseline->baseline == phase &&
        !target->calls && !baseline->calls &&
        qa_native_host_destroy_ready(phase->target->state.native.host) &&
        qa_native_host_destroy_ready(phase->baseline_host)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Native baseline phase still owns active source callbacks");
}
static void release(struct application_native_q2_baseline *phase)
{
    phase->baseline->state.native.host = phase->baseline_host;
    phase->target->state.native.q2_engine->baseline = NULL;
    phase->baseline->state.native.q2_engine->baseline = NULL;
    free(phase);
}
bool application_native_q2_baseline_begin(application_provider *target, application_provider *baseline,
    struct application_native_q2_baseline **out, qa_error *error)
{
    struct application_native_q2 *source = target ? target->state.native.q2_engine : NULL;
    struct application_native_q2 *temporary = baseline ? baseline->state.native.q2_engine : NULL;
    if (!out || !source || !temporary || target == baseline || target->application == baseline->application ||
        !target->constructed || !target->attached || target->close_pending ||
        !baseline->constructed || !baseline->attached || baseline->close_pending ||
        !target->launch || !baseline->launch || target->owner != baseline->owner ||
        source->definition != temporary->definition || source->profile != temporary->profile ||
        source->profile == QA_NATIVE_Q2_CGAME_API2023 || !source->initialized ||
        !target->state.native.host || !baseline->state.native.host ||
        !qa_sha256_equal(&target->launch->identity, &baseline->launch->identity) ||
        !application_native_q2_idle(target) || !application_native_q2_idle(baseline))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native baseline requires two isolated prepared matching application owners");
    *out = NULL;
    if (!fresh(baseline, error)) return false;
    /* Exact module and declaration bytes qualify the service/ABI exchange;
     * the full save producer separately qualifies private continuation. */
    qa_native_module_info target_info = qa_native_module_describe(target->state.native.module);
    qa_native_module_info baseline_info = qa_native_module_describe(baseline->state.native.module);
    if (!qa_sha256_equal(&target_info.image.digest, &baseline_info.image.digest) ||
        !target->launch->declaration || !baseline->launch->declaration ||
        !qa_sha256_equal(qa_resource_digest(target->launch->declaration), qa_resource_digest(baseline->launch->declaration)))
        return application_fail(error, QA_ERROR_FORMAT, "Native baseline artifact or declaration differs from the target owner");
    if (!application_native_q2_prepare_restore(target, error) ||
        !application_native_q2_prepare_restore(baseline, error)) return false;
    struct application_native_q2_baseline *phase = calloc(1, sizeof(*phase));
    if (!phase) return application_fail(error, QA_ERROR_MEMORY, "Retaining isolated native baseline owner");
    phase->target = target; phase->baseline = baseline; phase->baseline_host = baseline->state.native.host;
    if (!qa_native_host_reconstruction_begin(target->state.native.host, phase->baseline_host,
        &phase->services, error)) { free(phase); return false; }
    source->baseline = temporary->baseline = phase;
    baseline->state.native.host = target->state.native.host;
    *out = phase; return true;
}
bool application_native_q2_baseline_spawn(struct application_native_q2_baseline *phase,
    const char *map, const char *entities, const char *spawn, qa_error *error)
{
    if (!drained(phase, error)) return false;
    struct application_native_q2 *source = phase->target->state.native.q2_engine;
    struct application_native_q2 *temporary = phase->baseline->state.native.q2_engine;
    ++source->calls; ++temporary->calls;
    bool ok = qa_native_host_spawn_entities(phase->target->state.native.host, map, entities, spawn, error);
    --temporary->calls; --source->calls; return ok;
}
bool application_native_q2_baseline_end(struct application_native_q2_baseline *phase, qa_error *error)
{
    if (!drained(phase, error) || !qa_native_host_reconstruction_end(phase->services, error)) return false;
    release(phase); return true;
}
bool application_native_q2_baseline_abort(struct application_native_q2_baseline *phase, qa_error *error)
{
    if (!drained(phase, error)) return false;
    struct application_native_q2 *source = phase->target->state.native.q2_engine;
    struct application_native_q2 *temporary = phase->baseline->state.native.q2_engine;
    if (source->initialized) {
        ++source->calls; ++temporary->calls;
        bool ok = qa_native_host_shutdown(phase->target->state.native.host, false, error);
        --temporary->calls; --source->calls;
        if (!ok) return false;
        source->initialized = false;
    }
    /* Both destroy predicates passed without callbacks after Shutdown. Normal
     * native destruction consumes the target even if unloading reports a fault. */
    if (!drained(phase, error)) return false;
    bool consumed = false;
    bool ok = qa_native_host_reconstruction_destroy(phase->services, &consumed, error);
    if (!consumed) return false;
    phase->target->state.native.host = NULL;
    release(phase); return ok;
}
