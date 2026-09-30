#include "internal.h"

typedef struct reconstruction_services {
    qa_native_host_engine_services engine;
    qa_native_host_world_services world;
    qa_native_host_movement_services movement;
    qa_native_host_q2_application_fn application;
    void *application_context;
    qa_cvars *cvars;
    qa_console *console;
    qa_command_context command;
} reconstruction_services;
struct qa_native_host_reconstruction {
    qa_native_host *target, *baseline;
    reconstruction_services saved;
    bool *retained;
    size_t retained_capacity;
};
static reconstruction_services services(const qa_native_host *host)
{
    return (reconstruction_services){host->engine, host->world, host->movement,
        host->q2_application, host->q2_application_context, host->cvars,
        host->console, host->command_context};
}
static void apply(qa_native_host *host, const reconstruction_services *value)
{
    host->engine = value->engine; host->world = value->world; host->movement = value->movement;
    host->q2_application = value->application; host->q2_application_context = value->application_context;
    host->cvars = value->cvars; host->console = value->console; host->command_context = value->command;
}
static bool clear_slots(qa_native_host *host, qa_error *error)
{
    qa_native_entity_table table;
    if (!qa_native_entity_table_get(host->instance, &table, error)) return false;
    for (uint32_t slot = 0; slot < table.capacity; ++slot) {
        qa_native_slot_binding value = {.kind = QA_NATIVE_SLOT_FREE, .slot = slot};
        if (!qa_native_bind_slot(host->instance, &value, error)) return false;
    }
    return true;
}
bool qa_native_host_reconstruction_begin(qa_native_host *target, qa_native_host *baseline,
    qa_native_host_reconstruction **out, qa_error *error)
{
    if (!out || !target || !baseline || target == baseline ||
        target->kind != NATIVE_HOST_Q2_GAME || baseline->kind != NATIVE_HOST_Q2_GAME ||
        target->profile != baseline->profile || target->pointer_bytes != baseline->pointer_bytes ||
        target->reconstruction || baseline->reconstruction || target->restoring || baseline->restoring ||
        !qa_native_host_destroy_ready(target) || !qa_native_host_destroy_ready(baseline) ||
        !qa_session_safe(target->world.session) || !qa_session_safe(baseline->world.session) ||
        !qa_world_idle(target->world.world) || !qa_world_idle(baseline->world.world) ||
        target->world.session == baseline->world.session || target->world.world == baseline->world.world ||
        qa_world_actors(baseline->world.world) != qa_session_actors(baseline->world.session) ||
        qa_world_geometry(target->world.world) == qa_world_geometry(baseline->world.world))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
            "Native reconstruction requires two idle complete distinct application service graphs");
    *out = NULL;
    const qa_actor_registry *actors = qa_session_actors(baseline->world.session);
    const qa_actor_record *world = qa_actors_get(actors, baseline->world.world_actor);
    if (!world || world->owner != baseline->world.owner || !world->has_source || world->source_slot)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native baseline requires a fresh source world actor");
    uint32_t cursor = 0; const qa_actor_record *record;
    while (qa_actors_next(actors, &cursor, &record))
        if (record->owner == baseline->world.owner &&
            (!qa_actor_id_equal(record->id, world->id) || qa_world_body_storage_serial(baseline->world.world, record->id)))
            return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native baseline source owner already has actors or bodies");
    qa_native_entity_table table;
    if (!qa_native_entity_table_get(baseline->instance, &table, error)) return false;
    if (table.capacity)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native baseline host already has source entity storage");
    qa_native_host_reconstruction *phase = calloc(1, sizeof(*phase));
    if (!phase) return native_host_fail(error, QA_ERROR_MEMORY, 0, "Retaining native reconstruction owner");
    bool *retained = NULL;
    if (baseline->retained_capacity) {
        retained = calloc(baseline->retained_capacity, sizeof(*retained));
        if (!retained) { free(phase); return native_host_fail(error, QA_ERROR_MEMORY, 0, "Retaining baseline client slots"); }
        memcpy(retained, baseline->retained_clients, baseline->retained_capacity * sizeof(*retained));
    }
    if (!clear_slots(target, error)) { free(retained); free(phase); return false; }
    phase->target = target; phase->baseline = baseline; phase->saved = services(target);
    phase->retained = target->retained_clients; phase->retained_capacity = target->retained_capacity;
    reconstruction_services temporary = services(baseline); apply(target, &temporary);
    target->retained_clients = retained; target->retained_capacity = baseline->retained_capacity;
    target->reconstruction = baseline->reconstruction = phase;
    *out = phase; return true;
}
bool qa_native_host_reconstruction_end(qa_native_host_reconstruction *phase, qa_error *error)
{
    if (!phase || phase->target->reconstruction != phase || phase->baseline->reconstruction != phase ||
        !qa_native_host_destroy_ready(phase->target) || !qa_native_host_destroy_ready(phase->baseline) ||
        !qa_world_idle(phase->target->world.world) || !qa_session_safe(phase->target->world.session))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native reconstruction still owns active source services");
    if (!clear_slots(phase->target, error)) return false;
    free(phase->target->retained_clients);
    phase->target->retained_clients = phase->retained;
    phase->target->retained_capacity = phase->retained_capacity;
    apply(phase->target, &phase->saved);
    phase->target->reconstruction = phase->baseline->reconstruction = NULL;
    free(phase); return true;
}
bool qa_native_host_reconstruction_destroy(qa_native_host_reconstruction *phase, bool *consumed, qa_error *error)
{
    if (consumed) *consumed = false;
    if (!consumed || !phase || phase->target->reconstruction != phase || phase->baseline->reconstruction != phase ||
        !qa_native_host_destroy_ready(phase->target) || !qa_native_host_destroy_ready(phase->baseline))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native reconstruction destruction requires drained source owners");
    phase->target->reconstruction = phase->baseline->reconstruction = NULL;
    bool ok = qa_native_host_destroy(phase->target, error);
    free(phase->retained); free(phase); *consumed = true; return ok;
}
