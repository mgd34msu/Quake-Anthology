#include "network_q1_signon.h"
#include "event_stream.h"
#include "qa/source_save.h"
#include "qa/game_q1_wire.h"
#include <stdlib.h>
#include <string.h>

typedef struct application_q1_signon_record {
    qa_application_protocol_event event;
    qa_event_lease *lease;
    uint64_t source_serial;
    uint64_t source_revision;
} application_q1_signon_record;
struct application_q1_signon {
    application_q1_signon_record *records;
    size_t count, capacity;
};
static application_provider *source_owner(const qa_application *app, qa_actor_owner owner)
{
    for (size_t i = 0; app && i < app->provider_count; ++i) {
        application_provider *p = app->providers[i];
        if (p && p->owner == owner && p->constructed && p->attached && !p->close_pending && p->launch) return p;
    }
    return NULL;
}
static bool native_source(application_provider *provider) {
    return provider && provider->kind == APPLICATION_PROVIDER_Q1 &&
        provider == application_world_provider(provider->application, QA_ROLE_ENTITIES, "") &&
        provider->launch && (provider->launch->selection.clock.kind == QA_RULESET_NETQUAKE ||
        provider->launch->selection.clock.kind == QA_RULESET_QUAKEWORLD) &&
        qa_q1_wire_enabled(provider->state.q1);
}
static bool event_valid(const qa_application_protocol_event *e, qa_error *error)
{
    if (!e || !e->provider || !e->signon || (e->dialect != QA_RULESET_NETQUAKE && e->dialect != QA_RULESET_QUAKEWORLD) ||
        e->destination != 3 || e->multicast || !qa_vec_finite(e->origin) ||
        (e->payload.size && !e->payload.data) || (e->reference_count && !e->references))
        return application_fail(error, QA_ERROR_FORMAT, "Invalid retained Q1 source signon event");
    for (size_t i = 0; i < e->reference_count; ++i)
        if (e->payload.size < 2 || e->references[i].offset > e->payload.size - 2)
            return application_fail(error, QA_ERROR_FORMAT, "Retained Q1 signon reference leaves original bytes");
    return true;
}
static void owner_free(struct application_q1_signon *owner)
{
    if (!owner) return;
    for (size_t i = 0; i < owner->count; ++i)
        qa_event_lease_release(owner->records[i].lease);
    free(owner->records); free(owner);
}
void application_q1_signon_destroy(qa_application *app)
{
    if (app) { owner_free(app->q1_signon); app->q1_signon = NULL; }
}
void application_q1_signon_reset(qa_application *app)
{ application_q1_signon_destroy(app); }
void application_q1_signon_drop(qa_application *app, qa_actor_owner provider)
{
    struct application_q1_signon *owner = app ? app->q1_signon : NULL;
    for (size_t i = 0; owner && i < owner->count;) {
        if (owner->records[i].event.provider != provider) { ++i; continue; }
        qa_event_lease_release(owner->records[i].lease);
        --owner->count;
        memmove(owner->records + i, owner->records + i + 1, (owner->count - i) * sizeof(*owner->records));
    }
}
bool application_q1_signon_retain(application_provider *provider,
    const qa_application_protocol_event *event, qa_error *error)
{
    if (!provider || !event) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 signon source emission");
    if (!event->signon || (event->dialect != QA_RULESET_NETQUAKE && event->dialect != QA_RULESET_QUAKEWORLD)) return true;
    qa_application *app = provider->application;
    if (!app || !provider->launch ||
        (provider->kind != APPLICATION_PROVIDER_QC && !native_source(provider)) || provider->owner != event->provider ||
        provider->launch->selection.clock.kind != event->dialect)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q1 signon emission lacks its actual primary source owner");
    qa_event_lease *lease = app->event_ring && event->event_id ?
        qa_event_ring_retain(app->event_ring, event->event_id) : NULL;
    if (!lease)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 signon lacks its committed Source event");
    const application_event_envelope *retained = qa_event_lease_record(lease);
    if (retained->kind != QA_APPLICATION_EVENT_PROTOCOL ||
        retained->raw.protocol.event.provider != provider->owner ||
        retained->raw.protocol.event.dialect != provider->launch->selection.clock.kind ||
        !event_valid(&retained->raw.protocol.event, error)) {
        qa_event_lease_release(lease);
        if (error && error->code == QA_OK)
            application_fail(error, QA_ERROR_ARGUMENT, "Q1 signon differs from its committed Source event");
        return false;
    }
    struct application_q1_signon *owner = app->q1_signon;
    if (!owner) {
        owner = calloc(1, sizeof(*owner));
        if (!owner) { qa_event_lease_release(lease); return application_fail(error, QA_ERROR_MEMORY, "Retaining Q1 signon owner"); }
        app->q1_signon = owner;
    }
    if (owner->count == owner->capacity) {
        size_t capacity = owner->capacity ? owner->capacity * 2 : 16;
        if (capacity < owner->capacity || capacity > SIZE_MAX / sizeof(*owner->records)) {
            qa_event_lease_release(lease); return application_fail(error, QA_ERROR_MEMORY, "Q1 signon owner extent exhausted");
        }
        application_q1_signon_record *records = realloc(owner->records, capacity * sizeof(*records));
        if (!records) { qa_event_lease_release(lease); return application_fail(error, QA_ERROR_MEMORY, "Retaining Q1 signon record"); }
        owner->records = records; owner->capacity = capacity;
    }
    application_q1_signon_record record = {.event = retained->raw.protocol.event, .lease = lease,
        .source_serial = provider->launch->identity, .source_revision = app->map_revision};
    record.event.event_id = 0;
    owner->records[owner->count++] = record; return true;
}
size_t application_q1_signon_count(const qa_application *app, qa_actor_owner provider)
{
    size_t count = 0;
    for (size_t i = 0; app && app->q1_signon && i < app->q1_signon->count; ++i)
        count += app->q1_signon->records[i].event.provider == provider;
    return count;
}
static bool record_valid(const qa_application *app, const application_q1_signon_record *record, qa_error *error)
{
    application_provider *p = source_owner(app, record->event.provider);
    if (!p || (p->kind != APPLICATION_PROVIDER_QC && !native_source(p)) || p->launch->selection.clock.kind != record->event.dialect ||
        !record->source_serial || p->launch->identity != record->source_serial || record->source_revision > app->map_revision)
        return application_fail(error, QA_ERROR_FORMAT, "Retained Q1 signon source generation is not admitted");
    return event_valid(&record->event, error);
}
bool application_q1_signon_at(const qa_application *app, qa_actor_owner provider, size_t index,
    qa_application_protocol_event *out, qa_error *error)
{
    if (!app || !out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing retained Q1 signon observation");
    for (size_t i = 0; app->q1_signon && i < app->q1_signon->count; ++i) {
        const application_q1_signon_record *record = &app->q1_signon->records[i];
        if (record->event.provider != provider) continue;
        if (index--) continue;
        if (!record_valid(app, record, error)) return false;
        *out = record->event; return true;
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "Retained Q1 signon index is absent");
}
static bool record_fields(qa_source_save_io *io, application_q1_signon_record *r,
    qa_event_transaction *transaction)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t dialect = reading ? 0 : (uint32_t)r->event.dialect;
    if (!qa_source_save_string(io, &r->event.provider) || !qa_source_save_u32(io, &dialect) ||
        !qa_source_save_u64(io, &r->source_revision) || !qa_source_save_u64(io, &r->event.time_ns) ||
        !qa_source_save_actor(io, &r->event.recipient) || !qa_source_save_vec3(io, &r->event.origin) ||
        !qa_source_save_i32(io, &r->event.destination) || !qa_source_save_bool(io, &r->event.reliable) ||
        !qa_source_save_bool(io, &r->event.multicast) || !qa_source_save_bool(io, &r->event.signon)) return false;
    if (reading) r->event.dialect = (qa_ruleset_id)dialect;
    size_t size = reading ? 0 : r->event.payload.size;
    size_t maximum = reading ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &size, maximum)) return false;
    if (reading) {
        uint8_t *bytes = size ? qa_event_ring_alloc(transaction, size, 1, io->error) : NULL;
        if (size && !bytes) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring retained Q1 signon bytes");
        r->event.payload = (qa_bytes){bytes, size};
    }
    if (!qa_source_save_bytes(io, (void *)r->event.payload.data, size)) return false;
    size_t count = reading ? 0 : r->event.reference_count;
    maximum = reading ? (io->input.size - io->offset) / 22 : SIZE_MAX / sizeof(*r->event.references);
    if (maximum > SIZE_MAX / sizeof(*r->event.references)) maximum = SIZE_MAX / sizeof(*r->event.references);
    if (!qa_source_save_count(io, &count, maximum)) return false;
    if (reading) {
        qa_application_protocol_reference *refs = count ? qa_event_ring_alloc(transaction,
            count * sizeof(*refs), _Alignof(qa_application_protocol_reference), io->error) : NULL;
        if (count && !refs) return application_fail(io->error, QA_ERROR_MEMORY, "Restoring retained Q1 signon references");
        r->event.references = refs; r->event.reference_count = count;
    }
    for (size_t i = 0; i < count; ++i) {
        qa_application_protocol_reference ref = reading ? (qa_application_protocol_reference){0} : r->event.references[i];
        size_t offset = reading ? 0 : ref.offset;
        if (!qa_source_save_count(io, &offset, size < 2 ? 0 : size - 2) ||
            !qa_source_save_actor(io, &ref.actor) || !qa_source_save_bool(io, &ref.packed_sound)) return false;
        if (reading) {
            ref.offset = offset;
            ((qa_application_protocol_reference *)r->event.references)[i] = ref;
        }
    }
    return true;
}
bool application_q1_signon_capture(qa_application *app, qa_buffer *out, qa_error *error)
{
    if (!app || !app->session || !out || !qa_session_safe(app->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 signon capture requires its idle application");
    qa_source_save_io io; if (!qa_source_save_writer(&io, app->session, error)) return false;
    size_t count = app->q1_signon ? app->q1_signon->count : 0;
    bool ok = qa_source_save_bytes(&io, "QAQS", 4) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (size_t i = 0; ok && i < count; ++i) {
        application_q1_signon_record record = app->q1_signon->records[i];
        ok = record_valid(app, &record, error) && record_fields(&io, &record, NULL);
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool application_q1_signon_restore(qa_application *app, qa_bytes bytes, qa_error *error)
{
    if (!app || !app->session || !qa_session_safe(app->session) || app->q1_signon)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 signon restore requires an empty isolated application owner");
    qa_source_save_io io; if (!qa_source_save_reader(&io, app->session, bytes, error)) return false;
    uint8_t magic[4]; size_t count = 0;
    size_t maximum = bytes.size >= 12 ? (bytes.size - 12) / 78 : 0;
    if (maximum > SIZE_MAX / sizeof(application_q1_signon_record)) maximum = SIZE_MAX / sizeof(application_q1_signon_record);
    bool ok = qa_source_save_bytes(&io, magic, 4) && !memcmp(magic, "QAQS", 4) &&
        qa_source_save_count(&io, &count, maximum);
    struct application_q1_signon *owner = NULL;
    qa_event_ring *pages = NULL;
    if (ok && count) {
        owner = calloc(1, sizeof(*owner));
        if (owner) owner->records = calloc(count, sizeof(*owner->records));
        if (!owner || !owner->records) ok = application_fail(error, QA_ERROR_MEMORY, "Restoring original Q1 signon owner");
        else owner->capacity = count;
        size_t fixed = sizeof(application_event_envelope) + 16384;
        if (ok && (count > SIZE_MAX / fixed || bytes.size > (SIZE_MAX - count * fixed) / 2))
            ok = application_fail(error, QA_ERROR_MEMORY, "Restored Q1 signon page extent exhausted");
        if (ok) {
            pages = qa_event_ring_create(bytes.size * 2 + count * fixed, 16384, count, error);
            ok = pages != NULL;
        }
    }
    for (size_t i = 0; ok && i < count; ++i) {
        application_q1_signon_record *record = &owner->records[i];
        qa_event_transaction transaction;
        ok = qa_event_ring_begin(pages, &transaction);
        application_event_envelope *envelope = ok ? qa_event_ring_alloc(&transaction,
            sizeof(*envelope), _Alignof(application_event_envelope), error) : NULL;
        if (!envelope) ok = application_fail(error, QA_ERROR_MEMORY, "Restoring Q1 signon envelope pages");
        if (ok) {
            *envelope = (application_event_envelope){.kind = QA_APPLICATION_EVENT_PROTOCOL};
            ok = record_fields(&io, record, &transaction);
        }
        if (ok) {
            application_provider *provider = source_owner(app, record->event.provider);
            if (!provider)
                ok = application_fail(error, QA_ERROR_FORMAT, "Saved Q1 signon source owner is absent");
            else {
                record->source_serial = provider->launch->identity;
                ok = record_valid(app, record, error);
            }
        }
        if (ok) {
            envelope->raw.protocol.event = record->event;
            uint64_t id = qa_event_ring_commit(&transaction, envelope);
            if (!id) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining restored Q1 signon envelope");
            else {
                record->lease = qa_event_ring_retain(pages, id);
                ++owner->count;
            }
        }
        if (!ok) qa_event_ring_abort(&transaction);
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (ok) { app->q1_signon = owner; owner = NULL; }
    qa_event_ring_destroy(&pages);
    owner_free(owner); qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Invalid original Q1 signon continuation");
    return ok;
}
