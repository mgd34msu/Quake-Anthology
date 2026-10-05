#include "native_q2_wire_engine.h"
#include "qa/native_host_q2_wire.h"
#include "qa/source_save.h"

static bool clock_read(struct application_native_q2 *engine, qa_clock_state *out, qa_error *error)
{
    return (engine && engine->provider && engine->provider->application &&
        qa_session_clock(engine->provider->application->session, engine->provider->owner, out) &&
        out->frame.provider == engine->provider->owner &&
        out->frame.kind == (engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_CLOCK_Q2_CLASSIC : QA_CLOCK_Q2_RERELEASE)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine namespace lost its actual Source clock");
}

void application_native_q2_wire_destroy(application_native_q2_wire_engine **owner)
{
    if (!owner || !*owner) return;
    free((*owner)->rows); free((*owner)->references); free(*owner); *owner = NULL;
}

bool application_native_q2_wire_begin(struct application_native_q2 *engine, qa_error *error)
{
    qa_native_entity_table table;
    const qa_cvar_view *clients = engine ? qa_cvars_find(engine->cvars, "maxclients") : NULL;
    if (!engine || engine->profile == QA_NATIVE_Q2_CGAME_API2023 || !clients ||
        clients->integer < 1 || clients->integer > 256 || !engine->provider->state.native.host ||
        !qa_native_entity_table_get(qa_native_host_instance(engine->provider->state.native.host), &table, error) ||
        table.capacity <= (uint32_t)clients->integer || table.capacity > 65536)
        return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine namespace requires its genuine initialized SDK capacity");
    application_native_q2_wire_engine *wire = calloc(1, sizeof(*wire));
    if (!wire) return application_fail(error, QA_ERROR_MEMORY, "Allocating Original Q2 Engine namespace");
    wire->capacity = table.capacity; wire->clients = (uint32_t)clients->integer;
    wire->rows = calloc(wire->capacity, sizeof(*wire->rows));
    if (!wire->rows) { free(wire); return application_fail(error, QA_ERROR_MEMORY, "Allocating Original Q2 Engine entity bindings"); }
    application_native_q2_wire_destroy(&engine->wire_engine);
    engine->wire_engine = wire;
    return true;
}

static bool physical_slot(struct application_native_q2 *engine, qa_actor_id actor,
    uint32_t *out, bool *present, qa_error *error)
{
    const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->provider->application->session), actor);
    if (!record) return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine admission requires a live full actor");
    *present = false;
    uint32_t slot = 0;
    if (record->owner == engine->provider->owner && record->has_source) {
        slot = record->source_slot; *present = true;
    } else for (uint32_t i = 1; i <= engine->wire_engine->clients; ++i) {
        if (!qa_actor_id_equal(engine->clients[i].actor, actor)) continue;
        if (*present) return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine actor aliases physical clients");
        slot = i; *present = true;
    }
    if (!*present) return true;
    qa_native_slot_binding binding;
    if (slot >= engine->wire_engine->capacity ||
        !qa_native_slot(qa_native_host_instance(engine->provider->state.native.host), slot, &binding, error) ||
        binding.kind == QA_NATIVE_SLOT_FREE || binding.owner != engine->provider->owner ||
        binding.source_slot != slot || !qa_actor_id_equal(binding.actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine actor lost its actual SDK binding");
    *out = slot; return true;
}

static bool remember(application_native_q2_wire_engine *wire, qa_actor_id actor,
    uint32_t number, qa_error *error)
{
    if (wire->reference_count == wire->reference_capacity) {
        size_t capacity = wire->reference_capacity ? wire->reference_capacity * 2 : 32;
        if (capacity < wire->reference_capacity || capacity > SIZE_MAX / sizeof(*wire->references))
            return application_fail(error, QA_ERROR_MEMORY, "Original Q2 Engine provenance extent overflows");
        application_native_q2_wire_reference *references = realloc(wire->references, capacity * sizeof(*references));
        if (!references) return application_fail(error, QA_ERROR_MEMORY, "Retaining Original Q2 Engine admission provenance");
        wire->references = references; wire->reference_capacity = capacity;
    }
    wire->references[wire->reference_count++] = (application_native_q2_wire_reference){actor, number};
    return true;
}

static bool admit(struct application_native_q2 *engine, qa_actor_id actor, bool original,
    uint32_t source_slot, uint32_t *out, qa_error *error)
{
    application_native_q2_wire_engine *wire = engine->wire_engine;
    qa_clock_state clock;
    if (!wire || !clock_read(engine, &clock, error)) return false;
    for (uint32_t i = 0; i < wire->capacity; ++i) {
        application_native_q2_wire_row *row = &wire->rows[i];
        if (!row->occupied || !qa_actor_id_equal(row->actor, actor)) continue;
        if (original) { row->original = true; row->source_slot = source_slot; }
        *out = i; return true;
    }
    uint32_t number = UINT32_MAX;
    if (original && source_slot <= wire->clients) {
        number = source_slot;
        if (wire->rows[number].occupied)
            return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine reserved client slot has another full actor");
    } else if (original && source_slot < wire->capacity && !wire->rows[source_slot].occupied &&
        (!wire->rows[source_slot].retired_ns || clock.frame.time_ns < UINT64_C(2000000000) ||
            (clock.frame.time_ns >= wire->rows[source_slot].retired_ns &&
             clock.frame.time_ns - wire->rows[source_slot].retired_ns > UINT64_C(500000000)))) number = source_slot;
    if (number == UINT32_MAX) for (uint32_t i = wire->capacity; i-- > wire->clients + 1;) {
        const application_native_q2_wire_row *row = &wire->rows[i];
        if (!row->occupied && (!row->retired_ns || clock.frame.time_ns < UINT64_C(2000000000) ||
            (clock.frame.time_ns >= row->retired_ns && clock.frame.time_ns - row->retired_ns > UINT64_C(500000000)))) {
            number = i; break;
        }
    }
    if (number == UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine wire namespace has no free admitted slot");
    if (!remember(wire, actor, number, error)) return false;
    wire->rows[number] = (application_native_q2_wire_row){.actor = actor, .occupied = true,
        .original = original, .source_slot = source_slot};
    *out = number; return true;
}

bool application_native_q2_wire_number(struct application_native_q2 *engine, qa_actor_id actor,
    uint32_t *out, qa_error *error)
{
    if (!engine || !engine->wire_engine || !out || !actor.registry)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine number requires admitted Source provenance");
    for (size_t i = 0; i < engine->wire_engine->reference_count; ++i)
        if (qa_actor_id_equal(engine->wire_engine->references[i].actor, actor)) {
            *out = engine->wire_engine->references[i].number; return true;
        }
    uint32_t slot = 0; bool original;
    if (!physical_slot(engine, actor, &slot, &original, error)) return false;
    if (!original)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine number has no real Source admission");
    return admit(engine, actor, true, slot, out, error);
}

bool application_native_q2_wire_admit(struct application_native_q2 *engine, qa_actor_id actor,
    uint32_t *out, qa_error *error)
{
    if (!engine || !engine->wire_engine || !out || !actor.registry)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 Engine event admission requires its actual namespace");
    for (size_t i = 0; i < engine->wire_engine->reference_count; ++i)
        if (qa_actor_id_equal(engine->wire_engine->references[i].actor, actor)) {
            *out = engine->wire_engine->references[i].number; return true;
        }
    uint32_t slot = 0; bool original;
    if (!physical_slot(engine, actor, &slot, &original, error)) return false;
    return admit(engine, actor, original, slot, out, error);
}

void application_native_q2_wire_actor_released(qa_application *app, qa_actor_id actor)
{
    for (size_t i = 0; app && i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider && provider->constructed && provider->kind == APPLICATION_PROVIDER_NATIVE &&
            provider->state.native.q2_engine)
            application_native_q2_wire_released(provider->state.native.q2_engine, actor);
    }
}

bool application_native_q2_wire_resource(struct application_native_q2 *engine, unsigned kind,
    const char *path, uint32_t *out, qa_error *error)
{
    if (!engine || !out || !path || kind > 2 || engine->profile == QA_NATIVE_Q2_CGAME_API2023)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine resource requires its actual GAME table");
    *out = 0;
    if (!*path) return true;
    uint32_t base = engine->resource_base[kind], maximum = engine->resource_limit[kind];
    uint32_t free_index = 0;
    for (uint32_t i = 1; i < maximum; ++i) {
        const char *current = engine->configstrings[base + i];
        if (current && !strcmp(current, path)) { *out = i; return true; }
        if (!free_index && (!current || !*current)) free_index = i;
    }
    if (!free_index) return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine resource namespace is full");
    size_t length = strlen(path);
    if (length >= 2048 || engine->config_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine resource leaves its real configstring profile");
    char *copy = malloc(length + 1);
    if (!copy) return application_fail(error, QA_ERROR_MEMORY, "Retaining Original Q2 Engine resource name");
    memcpy(copy, path, length + 1);
    free(engine->configstrings[base + free_index]); engine->configstrings[base + free_index] = copy;
    ++engine->config_revision;
    *out = free_index; return true;
}

bool application_native_q2_wire_prepare(struct application_native_q2 *engine,
    const qa_application_native_q2_entity_prefix *entities, uint32_t count, qa_error *error)
{
    if (!engine || !engine->wire_engine || !application_native_q2_idle(engine->provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine publication requires its idle installed namespace");
    qa_native_host *host = engine->provider->state.native.host;
    if (count > engine->wire_engine->capacity) return false;
    for (uint32_t i = 0; i < engine->wire_engine->capacity; ++i) {
        application_native_q2_wire_row *row = &engine->wire_engine->rows[i];
        if (!row->occupied || !row->original) continue;
        qa_native_slot_binding binding;
        if (!qa_native_slot(qa_native_host_instance(host), row->source_slot, &binding, error)) return false;
        if (binding.kind == QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor, row->actor)) {
            if (qa_actors_get(qa_session_actors(engine->provider->application->session), row->actor)) {
                row->original = false; row->source_slot = 0;
            } else application_native_q2_wire_released(engine, row->actor);
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        const qa_native_host_q2_entity *source = &entities[i].source.original;
        if (!source->in_use || source->binding.kind == QA_NATIVE_SLOT_FREE) continue;
        uint32_t number;
        if (!admit(engine, source->binding.actor, true, i, &number, error)) return false;
    }
    return true;
}

bool application_native_q2_wire_linked(struct application_native_q2 *engine,
    const qa_linked_body *linked, qa_error *error)
{
    if (!engine || !engine->wire_engine || !linked)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine link requires its actual namespace");
    uint32_t source_slot = 0; bool original;
    if (!physical_slot(engine, linked->actor, &source_slot, &original, error)) return false;
    uint32_t number;
    if (!admit(engine, linked->actor, original, source_slot, &number, error)) return false;
    qa_clock_state clock;
    if (!clock_read(engine, &clock, error)) return false;
    qa_q2_source_entity_motion *motion = &engine->wire_engine->rows[number].motion;
    if (!motion->creation_present) {
        motion->creation_frame = clock.frame.number; motion->creation_origin = linked->state.origin;
        motion->creation_present = true;
    }
    if (motion->link_count == UINT64_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine link counter exhausted");
    ++motion->link_count;
    motion->actor = linked->actor; motion->source_slot = number;
    motion->source_frame = clock.frame.number; motion->origin = linked->state.origin;
    motion->origins[clock.frame.number & 7u] = (qa_q2_source_origin){clock.frame.number, linked->state.origin, true};
    return true;
}

void application_native_q2_wire_released(struct application_native_q2 *engine, qa_actor_id actor)
{
    if (!engine || !engine->wire_engine) return;
    qa_clock_state clock = {0};
    (void)qa_session_clock(engine->provider->application->session, engine->provider->owner, &clock);
    for (uint32_t i = 0; i < engine->wire_engine->capacity; ++i) {
        application_native_q2_wire_row *row = &engine->wire_engine->rows[i];
        if (row->occupied && qa_actor_id_equal(row->actor, actor))
            *row = (application_native_q2_wire_row){.retired_ns = clock.frame.time_ns};
    }
}

static bool fields(qa_source_save_io *io, application_native_q2_wire_engine *wire)
{
    if (!qa_source_save_u32(io, &wire->capacity) || !qa_source_save_u32(io, &wire->clients) ||
        !wire->capacity || wire->capacity > 65536 || !wire->clients || wire->clients > 256 || wire->clients >= wire->capacity) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        wire->rows = calloc(wire->capacity, sizeof(*wire->rows));
        if (!wire->rows) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring Original Q2 Engine rows");
    }
    for (uint32_t i = 0; i < wire->capacity; ++i) {
        application_native_q2_wire_row *row = &wire->rows[i];
        qa_q2_source_entity_motion *motion = &row->motion;
        if (!qa_source_save_actor(io, &row->actor) || !qa_source_save_bool(io, &row->occupied) ||
            !qa_source_save_bool(io, &row->original) || !qa_source_save_u32(io, &row->source_slot) ||
            !qa_source_save_u64(io, &row->retired_ns) || !qa_source_save_u64(io, &motion->link_count) ||
            !qa_source_save_u64(io, &motion->source_frame) || !qa_source_save_u64(io, &motion->creation_frame) ||
            !qa_source_save_vec3(io, &motion->origin) || !qa_source_save_vec3(io, &motion->creation_origin) ||
            !qa_source_save_bool(io, &motion->creation_present)) return false;
        for (size_t j = 0; j < 8; ++j)
            if (!qa_source_save_u64(io, &motion->origins[j].source_frame) ||
                !qa_source_save_vec3(io, &motion->origins[j].origin) || !qa_source_save_bool(io, &motion->origins[j].present)) return false;
        motion->actor = row->actor; motion->source_slot = i;
        if (row->occupied != (row->actor.registry != 0) || (row->occupied && row->retired_ns) ||
            row->source_slot >= wire->capacity || (row->original && i <= wire->clients && row->source_slot != i) ||
            (!row->occupied && (row->original || row->source_slot || motion->creation_present || motion->link_count)) ||
            !qa_vec_finite(motion->origin) || !qa_vec_finite(motion->creation_origin))
            return application_fail(io->error, QA_ERROR_FORMAT, "Original Q2 Engine row lost its real namespace or lifetime");
        for (uint32_t j = 0; row->occupied && j < i; ++j)
            if (wire->rows[j].occupied && qa_actor_id_equal(row->actor, wire->rows[j].actor))
                return application_fail(io->error, QA_ERROR_FORMAT, "Original Q2 Engine rows alias a full actor");
    }
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size / 12 : UINT32_MAX;
    if (!qa_source_save_count(io, &wire->reference_count, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (wire->reference_count > SIZE_MAX / sizeof(*wire->references)) return false;
        wire->references = wire->reference_count ? calloc(wire->reference_count, sizeof(*wire->references)) : NULL;
        if (wire->reference_count && !wire->references) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring Original Q2 Engine provenance");
        wire->reference_capacity = wire->reference_count;
    }
    for (size_t i = 0; i < wire->reference_count; ++i) {
        application_native_q2_wire_reference *reference = &wire->references[i];
        if (!qa_source_save_actor(io, &reference->actor) || !qa_source_save_u32(io, &reference->number) ||
            !reference->actor.registry || reference->number >= wire->capacity) return false;
        for (size_t j = 0; j < i; ++j)
            if (qa_actor_id_equal(reference->actor, wire->references[j].actor))
                return application_fail(io->error, QA_ERROR_FORMAT, "Original Q2 Engine admission provenance aliases a generation");
    }
    return true;
}

static bool restored_current(struct application_native_q2 *engine,
    const application_native_q2_wire_engine *wire, qa_error *error)
{
    qa_native_entity_table table;
    const qa_cvar_view *clients = qa_cvars_find(engine->cvars, "maxclients");
    qa_clock_state clock;
    if (!clients || clients->integer < 1 || (uint32_t)clients->integer != wire->clients ||
        !qa_native_entity_table_get(qa_native_host_instance(engine->provider->state.native.host), &table, error) ||
        table.capacity != wire->capacity || !clock_read(engine, &clock, error))
        return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine cold namespace differs from its actual SDK policy");
    const qa_actor_registry *actors = qa_session_actors(engine->provider->application->session);
    for (uint32_t i = 0; i < wire->capacity; ++i) {
        const application_native_q2_wire_row *row = &wire->rows[i];
        const qa_q2_source_entity_motion *motion = &row->motion;
        if (row->retired_ns > clock.frame.time_ns || (row->occupied && !qa_actors_get(actors, row->actor)) ||
            (motion->creation_present ? (!motion->link_count || motion->creation_frame > clock.frame.number ||
                motion->source_frame < motion->creation_frame || motion->source_frame > clock.frame.number) :
                (motion->creation_frame || motion->link_count || motion->source_frame ||
                    motion->origin.x != 0 || motion->origin.y != 0 || motion->origin.z != 0 ||
                    motion->creation_origin.x != 0 || motion->creation_origin.y != 0 || motion->creation_origin.z != 0)))
            return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine cold lifetime leaves its real Source clock");
        for (size_t j = 0; j < 8; ++j) {
            const qa_q2_source_origin *origin = &motion->origins[j];
            if (!qa_vec_finite(origin->origin) || (origin->present ? (!motion->creation_present ||
                origin->source_frame < motion->creation_frame || origin->source_frame > clock.frame.number ||
                (origin->source_frame & 7u) != j) :
                (origin->source_frame || origin->origin.x != 0 || origin->origin.y != 0 || origin->origin.z != 0)))
                return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine cold ring is not a genuine link receipt");
        }
        if (!row->occupied) continue;
        bool admitted = false;
        for (size_t j = 0; j < wire->reference_count; ++j)
            if (wire->references[j].number == i && qa_actor_id_equal(wire->references[j].actor, row->actor)) admitted = true;
        if (!admitted) return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine row has no retained admission provenance");
        if (row->original) {
            qa_native_slot_binding binding;
            if (!qa_native_slot(qa_native_host_instance(engine->provider->state.native.host), row->source_slot, &binding, error) ||
                binding.kind == QA_NATIVE_SLOT_FREE || binding.owner != engine->provider->owner ||
                !qa_actor_id_equal(binding.actor, row->actor))
                return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine cold row lost its real SDK actor binding");
        }
    }
    return true;
}

bool application_native_q2_wire_capture(struct application_native_q2 *engine, qa_buffer *out, qa_error *error)
{
    qa_source_save_io io;
    if (!engine || !out || out->data || out->size || !qa_source_save_writer(&io, engine->provider->application->session, error)) return false;
    bool present = engine->wire_engine != NULL;
    bool ok = qa_source_save_bool(&io, &present) &&
        (!present || fields(&io, engine->wire_engine));
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}

bool application_native_q2_wire_restore(struct application_native_q2 *engine, qa_bytes bytes,
    application_native_q2_wire_engine **out, qa_error *error)
{
    if (!engine || !out || *out) return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 Engine restore needs an empty candidate");
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, engine->provider->application->session, bytes, error)) return false;
    bool present = false;
    bool ok = qa_source_save_bool(&io, &present);
    application_native_q2_wire_engine *wire = NULL;
    if (ok && present) {
        wire = calloc(1, sizeof(*wire));
        ok = wire && fields(&io, wire);
        if (!wire) application_fail(error, QA_ERROR_MEMORY, "Restoring Original Q2 Engine namespace");
    }
    if (ok) ok = qa_source_save_finish(&io, NULL) && (!wire || restored_current(engine, wire, error));
    qa_source_save_dispose(&io);
    if (ok) *out = wire;
    else application_native_q2_wire_destroy(&wire);
    return ok;
}
