#include "remote_unified_save.h"
#include "remote_unified_private.h"
#include "../application/unified_save_internal.h"
#include "qa/executable_recipe_save.h"
#include "qa/network_unified_save.h"

#include <stdlib.h>
#include <string.h>

static bool bad(qa_error *e, const char *message)
{ return frontend_unified_fail(e, QA_ERROR_FORMAT, message); }
static bool actor_pair(qa_source_save_io *io, qa_saved_actor_id *id)
{ return qa_source_save_u64(io, &id->generation) && qa_source_save_u32(io, &id->slot); }
static bool pair_equal(qa_saved_actor_id a, qa_saved_actor_id b)
{ return a.slot == b.slot && a.generation == b.generation; }
static bool actor_reference(qa_source_save_io *io, qa_actor_registry *actors, qa_actor_id *actor)
{
    bool present = actor->registry != 0;
    bool writing = io->direction == QA_SOURCE_SAVE_WRITE;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return !actor->registry && !actor->generation && !actor->slot;
    qa_saved_actor_id saved = {0};
    return (!writing || qa_actors_save_reference(actors, *actor, &saved, io->error)) &&
        actor_pair(io, &saved) && (writing || qa_actors_reference_saved(actors, saved, true, actor, io->error));
}
static bool private_strings(qa_source_save_io *io, frontend_remote_unified *owner)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = reading ? 0 : qa_strings_count(owner->strings);
    if (!qa_source_save_count(io, &count, UINT32_MAX) ||
        (reading && count > (io->input.size - io->offset) / 8)) return false;
    for (size_t i = 0; i < count; ++i) {
        qa_buffer bytes = {0};
        if (!reading) {
            qa_bytes text = qa_strings_text(owner->strings, (qa_string_id)(i + 1));
            bytes = (qa_buffer){(uint8_t *)text.data, text.size};
        }
        bool okay = application_unified_save_blob(io, &bytes);
        if (okay && reading) {
            qa_string_id id = 0;
            okay = qa_strings_intern(owner->strings, (qa_bytes){bytes.data, bytes.size},
                &id, io->error) && id == i + 1;
        }
        if (reading) qa_buffer_free(&bytes);
        if (!okay) return false;
    }
    return true;
}
static bool string_reference(const frontend_remote_unified *owner, qa_string_id id)
{ return !id || id <= qa_strings_count(owner->strings); }
static bool private_actors(qa_source_save_io *io, frontend_remote_unified *owner)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_actor_checkpoint checkpoint = {0};
    bool okay = reading || qa_actors_checkpoint(owner->actors, &checkpoint, io->error);
    if (okay) okay = qa_source_save_u32(io, &checkpoint.capacity) &&
        checkpoint.capacity == owner->options.identity_capacity &&
        qa_source_save_u32(io, &checkpoint.count) && checkpoint.count <= checkpoint.capacity &&
        (!reading || checkpoint.count <= (io->input.size - io->offset) / 22);
    if (okay && reading && checkpoint.count) {
        checkpoint.slots = calloc(checkpoint.count, sizeof(*checkpoint.slots));
        if (!checkpoint.slots) okay = frontend_unified_fail(io->error, QA_ERROR_MEMORY, "Restoring private Unified actor history");
    }
    for (uint32_t i = 0; okay && i < checkpoint.count; ++i) {
        qa_actor_slot_checkpoint *row = checkpoint.slots + i;
        okay = qa_source_save_u64(io, &row->generation) && qa_source_save_u32(io, &row->owner) &&
            qa_source_save_u32(io, &row->definition) && qa_source_save_u32(io, &row->source_slot) &&
            qa_source_save_bool(io, &row->active) && qa_source_save_bool(io, &row->has_source) &&
            !row->has_source && (!row->active ||
                (string_reference(owner, row->owner) && string_reference(owner, row->definition)));
    }
    if (okay && reading) okay = qa_actors_restore(&checkpoint, NULL, NULL, &owner->actors, io->error);
    qa_actor_checkpoint_free(&checkpoint);
    return okay;
}
static bool identities(qa_source_save_io *io, frontend_remote_unified *owner)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = 0;
    if (!reading) for (const frontend_unified_identity *row = owner->identities; row; row = row->next) {
        if (count == SIZE_MAX) return false;
        ++count;
    }
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(frontend_unified_identity)) ||
        (reading && count > (io->input.size - io->offset) / 25)) return false;
    frontend_unified_identity **tail = &owner->identities;
    frontend_unified_identity *row = owner->identities;
    for (size_t i = 0; i < count; ++i) {
        if (reading) {
            row = calloc(1, sizeof(*row));
            if (!row) return frontend_unified_fail(io->error, QA_ERROR_MEMORY, "Restoring exact Unified wire identity order");
            *tail = row; tail = &row->next;
        }
        if (!row || !actor_pair(io, &row->wire) || row->wire.generation > QA_UNIFIED_SAFE_INTEGER ||
            !actor_reference(io, owner->actors, &row->actual) || !row->actual.registry) return false;
        for (frontend_unified_identity *previous = owner->identities; previous != row; previous = previous->next)
            if (pair_equal(previous->wire, row->wire) || qa_actor_id_equal(previous->actual, row->actual)) return false;
        if (!reading) row = row->next;
    }
    uint32_t cursor = 0; const qa_actor_record *actor;
    while (qa_actors_next(owner->actors, &cursor, &actor)) {
        bool found = false;
        for (const frontend_unified_identity *identity = owner->identities; identity; identity = identity->next)
            if (qa_actor_id_equal(identity->actual, actor->id)) { found = true; break; }
        if (!found) return false;
    }
    return true;
}
static bool recipe(qa_source_save_io *io, qa_application_content_graph *graph, qa_executable_recipe **value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool present = *value != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    qa_buffer bytes = {0};
    bool okay = reading || qa_executable_recipe_checkpoint(*value, graph, &bytes, io->error);
    if (okay) okay = application_unified_save_blob(io, &bytes) && bytes.size;
    if (okay && reading) okay = qa_executable_recipe_restore(graph, (qa_bytes){bytes.data, bytes.size}, value, io->error);
    qa_buffer_free(&bytes); return okay;
}
static bool metadata(qa_source_save_io *io, frontend_remote_unified *owner)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool present = owner->metadata != NULL;
    size_t count = owner->metadata_count;
    if (!qa_source_save_bool(io, &present) || !qa_source_save_count(io, &count, owner->options.identity_capacity) ||
        (!present && count) || (reading && count > (io->input.size - io->offset) / 29)) return false;
    if (reading) {
        owner->metadata_count = count;
        owner->metadata = present ? calloc(count ? count : 1, sizeof(*owner->metadata)) : NULL;
        if (present && !owner->metadata)
            return frontend_unified_fail(io->error, QA_ERROR_MEMORY, "Restoring pending received actor metadata");
    }
    for (size_t i = 0; i < count; ++i) {
        frontend_unified_metadata *row = owner->metadata + i;
        if (!actor_reference(io, owner->actors, &row->actor) || !qa_actors_get(owner->actors, row->actor) ||
            !qa_source_save_u32(io, &row->owner) || !string_reference(owner, row->owner) ||
            !qa_source_save_u32(io, &row->definition) || !string_reference(owner, row->definition) ||
            !qa_source_save_u32(io, &row->old_owner) || !string_reference(owner, row->old_owner) ||
            !qa_source_save_u32(io, &row->old_definition) || !string_reference(owner, row->old_definition)) return false;
        for (size_t j = 0; j < i; ++j) if (qa_actor_id_equal(owner->metadata[j].actor, row->actor)) return false;
    }
    return true;
}
static bool wire_actor(const qa_json_document *json, qa_json_id node, qa_saved_actor_id *out, qa_error *e)
{
    uint64_t slot;
    return qa_json_u64(json, qa_json_get(json, node, "slot"), &slot, e) && slot <= UINT32_MAX &&
        qa_json_u64(json, qa_json_get(json, node, "generation"), &out->generation, e) &&
        (out->slot = (uint32_t)slot, true);
}
static bool document_equal(const qa_unified_document *a, const qa_unified_document *b)
{
    if (!a || !b) return a == b;
    qa_bytes x = qa_json_source(qa_unified_document_json(a), qa_unified_document_root(a));
    qa_bytes y = qa_json_source(qa_unified_document_json(b), qa_unified_document_root(b));
    return qa_unified_document_type(a) == qa_unified_document_type(b) && x.size == y.size &&
        (!x.size || !memcmp(x.data, y.data, x.size));
}
static bool frame_valid(const frontend_remote_unified *owner, const qa_unified_document *frame,
    uint64_t *number, qa_error *e)
{
    const qa_json_document *json = qa_unified_document_json(frame);
    qa_json_id root = qa_unified_document_root(frame);
    qa_saved_actor_id actor; uint64_t epoch;
    return qa_json_u64(json, qa_json_get(json, root, "epoch"), &epoch, e) && epoch == owner->epoch &&
        wire_actor(json, qa_json_get(json, qa_json_get(json, root, "player"), "actor"), &actor, e) &&
        pair_equal(actor, owner->wire_player) && qa_json_u64(json, qa_json_get(json, qa_json_get(json,
            qa_json_get(json, qa_json_get(json, root, "output"), "snapshot"), "frame"), "frame"), number, e);
}
static bool retained_valid(const frontend_remote_unified *owner, const qa_net_client *peer, qa_error *e)
{
    if (!owner->bound || !peer || peer->protocol.kind != QA_NET_UNIFIED_1 || peer->protocol.flags || peer->protocol.revision ||
        peer->seat_count != 1 || !peer->seats || !qa_net_client_id_equal(peer->id, owner->options.domain.client) ||
        !qa_net_client_owns_seat(peer, owner->options.domain.seat) || peer->seats[0].remote_index ||
        owner->busy || !owner->actors || !owner->strings || (owner->retirement_pending && !owner->retired) ||
        (owner->prepared && !owner->preparing) || (owner->preparing && (!owner->preparing_recipe || !owner->offer)) ||
        (owner->preparing_recipe && !owner->offer) || (owner->frame_obsolete && !owner->prepared_frame) ||
        (owner->admitted && (!owner->recipe || !owner->epoch || !qa_actors_get(owner->actors, owner->player))) ||
        (!owner->recipe && (owner->epoch || owner->frame || owner->admitted)) ||
        (owner->frame && !owner->admitted) || (owner->prepared_frame && (!owner->admitted || !owner->prediction)) ||
        (!owner->prepared_frame && (owner->prediction || owner->metadata || owner->metadata_count)) ||
        (!owner->frame && owner->frame_number)) return bad(e, "Invalid retained Unified replica lifecycle");
    const qa_executable_recipe *recipes[] = {owner->recipe, owner->preparing_recipe, owner->retiring_recipe};
    for (size_t i = 0; i < 3; ++i) if (recipes[i] && !qa_executable_recipe_current(recipes[i], owner->options.domain.catalog))
        return bad(e, "Unified replica recipe belongs to another catalog graph");
    if (owner->recipe && (qa_executable_recipe_epoch(owner->recipe) != owner->epoch ||
        (owner->transport_restarted && !qa_sha256_equal(qa_executable_recipe_digest(owner->recipe), &peer->composition))))
        return bad(e, "Unified replica installed recipe differs from its actual publication");
    if (owner->offer) {
        const qa_json_document *json = qa_unified_document_json(owner->offer);
        qa_json_id value = qa_json_get(json, qa_unified_document_root(owner->offer), "value");
        uint64_t epoch;
        if (!qa_json_string_equal(json, qa_json_get(json, value, "kind"), "offer") ||
            !qa_json_u64(json, qa_json_get(json, value, "epoch"), &epoch, e) || epoch < owner->epoch)
            return bad(e, "Unified replica lost its exact pending offer");
        const qa_executable_recipe *offered = owner->preparing_recipe ? owner->preparing_recipe :
            epoch == owner->epoch ? owner->recipe : NULL;
        if (offered) {
            qa_unified_composition canonical = {0};
            bool okay = qa_executable_recipe_epoch(offered) == epoch &&
                qa_unified_composition_create(qa_json_source(json, qa_json_get(json,
                    qa_json_get(json, value, "composition"), "composition")), &canonical, e) &&
                qa_sha256_equal(&canonical.digest, qa_executable_recipe_digest(offered));
            qa_unified_composition_free(&canonical);
            if (!okay) return bad(e, "Unified replica pending recipe changes its retained offer");
        }
    }
    if (owner->admitted) {
        qa_saved_actor_id wire;
        if (!frontend_remote_unified_wire_actor(owner, owner->player, &wire) || !pair_equal(wire, owner->wire_player))
            return bad(e, "Unified replica player lost its exact private identity mapping");
    }
    uint64_t number;
    if (owner->frame && (!frame_valid(owner, owner->frame, &number, e) || number != owner->frame_number))
        return bad(e, "Unified replica published frame changes its Source clock or player");
    if (owner->prepared_frame) {
        if (!frame_valid(owner, owner->prepared_frame, &number, e)) return false;
        qa_buffer bytes = {0}; qa_unified_document *prediction = NULL;
        const qa_json_document *json = qa_unified_document_json(owner->prepared_frame);
        bool okay = qa_unified_document_bytes(owner->prepared_frame, qa_json_get(json,
            qa_unified_document_root(owner->prepared_frame), "prediction"), &bytes, e) &&
            qa_unified_document_decode(QA_UNIFIED_PREDICTION_DOCUMENT,
                (qa_bytes){bytes.data, bytes.size}, &prediction, e) && document_equal(prediction, owner->prediction);
        if (okay) {
            const qa_json_document *predicted_json = qa_unified_document_json(prediction);
            qa_json_id root = qa_unified_document_root(prediction);
            qa_saved_actor_id predicted;
            int64_t sequence, acknowledged;
            okay = wire_actor(predicted_json, qa_json_get(predicted_json, root, "actor"), &predicted, e) &&
                pair_equal(predicted, owner->wire_player) &&
                qa_json_i64(predicted_json, qa_json_get(predicted_json, root, "sequence"), &sequence, e) &&
                qa_json_i64(json, qa_json_get(json, qa_unified_document_root(owner->prepared_frame),
                    "acknowledgedInput"), &acknowledged, e) && sequence == acknowledged;
        }
        qa_buffer_free(&bytes); qa_unified_document_destroy(prediction);
        if (!okay) return bad(e, "Unified replica pending prediction differs from its received frame");
        if (owner->metadata) {
            qa_json_id rows = qa_json_get(json, qa_json_get(json, qa_json_get(json,
                qa_unified_document_root(owner->prepared_frame), "output"), "snapshot"), "actors");
            if (qa_json_size(json, rows) != owner->metadata_count) return false;
            for (size_t i = 0; i < owner->metadata_count; ++i) {
                const frontend_unified_metadata *pending = owner->metadata + i;
                qa_json_id row = qa_json_at(json, rows, i); qa_saved_actor_id wire, mapped;
                const char *source = qa_strings_cstr(owner->strings, pending->owner);
                const char *definition = qa_strings_cstr(owner->strings, pending->definition);
                if (!wire_actor(json, qa_json_get(json, row, "id"), &wire, e) ||
                    !frontend_remote_unified_wire_actor(owner, pending->actor, &mapped) || !pair_equal(wire, mapped) ||
                    !source || !definition || !qa_json_string_equal(json, qa_json_get(json, row, "owner"), source) ||
                    !qa_json_string_equal(json, qa_json_get(json, row, "definition"), definition)) return false;
            }
        }
    }
    return true;
}
static bool fields(qa_source_save_io *io, frontend_remote_unified *owner,
    const qa_net_client *peer, qa_application_content_graph *graph)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    frontend_remote_unified_domain *domain = &owner->options.domain;
    uint64_t catalog = qa_application_content_catalog_id(graph, domain->catalog);
    uint64_t pool = qa_application_content_pool_id(graph, domain->resources);
    uint32_t physical = domain->physical_seat, capacity = owner->options.identity_capacity;
    qa_net_seat_id seat = domain->seat;
    char magic[4] = {'Q','U','R','P'}; if (!catalog || !pool || !qa_source_save_bytes(io, magic, sizeof(magic)) ||
        memcmp(magic, "QURP", sizeof(magic)) || !application_unified_save_client(io, domain->client) ||
        !qa_source_save_u64(io, &seat.owner) || seat.owner != domain->seat.owner ||
        !qa_source_save_u32(io, &seat.index) || seat.index != domain->seat.index ||
        !qa_source_save_u32(io, &physical) || physical != domain->physical_seat ||
        !qa_source_save_u32(io, &capacity) || capacity != owner->options.identity_capacity ||
        !qa_source_save_u64(io, &catalog) || qa_application_content_catalog(graph, catalog) != domain->catalog ||
        !qa_source_save_u64(io, &pool) || qa_application_content_pool(graph, pool) != domain->resources ||
        !private_strings(io, owner) || !private_actors(io, owner) || !identities(io, owner) ||
        !recipe(io, graph, &owner->recipe) || !recipe(io, graph, &owner->preparing_recipe) ||
        !recipe(io, graph, &owner->retiring_recipe) ||
        !application_unified_save_document(io, &owner->offer, QA_UNIFIED_CONTROL_DOCUMENT) ||
        !application_unified_save_document(io, &owner->frame, QA_UNIFIED_FRAME_DOCUMENT) ||
        !application_unified_save_document(io, &owner->prepared_frame, QA_UNIFIED_FRAME_DOCUMENT) ||
        !application_unified_save_document(io, &owner->prediction, QA_UNIFIED_PREDICTION_DOCUMENT) ||
        !metadata(io, owner) || !actor_pair(io, &owner->wire_player) || !actor_pair(io, &owner->wire_client) ||
        !actor_reference(io, owner->actors, &owner->player) || !qa_source_save_u32(io, &owner->epoch) ||
        !qa_source_save_u32(io, &owner->source_entity) || !qa_source_save_u64(io, &owner->frame_number) ||
        !qa_source_save_bool(io, &owner->frame_obsolete) || !qa_source_save_bool(io, &owner->bound) ||
        !qa_source_save_bool(io, &owner->preparing) || !qa_source_save_bool(io, &owner->prepared) ||
        !qa_source_save_bool(io, &owner->admitted) || !qa_source_save_bool(io, &owner->retired) ||
        !qa_source_save_bool(io, &owner->consumers_live) || !qa_source_save_bool(io, &owner->transport_restarted) ||
        !qa_source_save_bool(io, &owner->retirement_pending)) return false;
    if (reading) owner->restore_pending = true;
    return retained_valid(owner, peer, io->error);
}
bool frontend_remote_unified_checkpoint(const frontend_remote_unified *owner,
    qa_application_content_graph *graph, qa_buffer *out, qa_error *e)
{
    const qa_net_client *peer = owner ? qa_net_connections_get(
        qa_network_connections(owner->options.domain.runtime), owner->options.domain.client) : NULL;
    qa_unified_session *installed = NULL;
    if (!owner || !graph || !out || out->data || out->size || owner->restore_pending ||
        !owner->session || !qa_unified_session_idle(owner->session) ||
        !owner->options.consumers.checkpoint_returned ||
        !owner->options.consumers.checkpoint_returned(owner->options.consumers.context, owner) ||
        !qa_unified_session_find(owner->options.domain.runtime, owner->options.domain.client, &installed, e) ||
        installed != owner->session || !frontend_remote_unified_qualified(owner,
            owner->options.domain.runtime, peer, e)) return false;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, e) &&
        fields(&io, (frontend_remote_unified *)owner, peer, graph) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!okay && (!e || e->code == QA_OK)) bad(e, "Invalid retained Unified replica graph");
    return okay;
}
bool frontend_remote_unified_restore_prefix(qa_frontend *frontend, const frontend_remote_unified_options *options,
    const qa_net_client *peer, qa_application_content_graph *graph, qa_bytes bytes,
    frontend_remote_unified **out, qa_error *e)
{
    const frontend_remote_unified_domain *domain = options ? &options->domain : NULL;
    const frontend_remote_unified_consumers *consumers = options ? &options->consumers : NULL;
    if (!frontend || !domain || !graph || !peer || !out || *out || !frontend->source_restoring ||
        frontend->application != domain->application || !domain->runtime ||
        !qa_net_client_id_equal(domain->client, peer->id) || !domain->catalog || !domain->resources ||
        !domain->console || !domain->cvars || !options->identity_capacity || !options->current ||
        !options->userinfo || !options->disconnected || !options->retirement || !options->transport_restart ||
        !consumers->prepare || !consumers->offer_publish ||
        !consumers->offer_ready || !consumers->control || !consumers->frame || !consumers->publish ||
        !consumers->input || !consumers->begin_frame || !consumers->clock_read ||
        !consumers->physical_ready || !consumers->physical_input ||
        !consumers->sample || !consumers->draw || !consumers->idle || !consumers->checkpoint_returned || !consumers->close ||
        !consumers->content_visit || domain->physical_seat >= frontend->options.seats ||
        !options->current(options->context, domain, e))
        return frontend_unified_fail(e, QA_ERROR_ARGUMENT, "Unified replica import requires its actual restored CLIENT and presentation owners");
    frontend_remote_unified *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_unified_fail(e, QA_ERROR_MEMORY, "Retaining restored Unified replica prefix");
    owner->frontend = frontend; owner->options = *options; owner->restore_pending = true;
    qa_catalog_retain(domain->catalog);
    owner->next = frontend->remote_unified; frontend->remote_unified = owner; *out = owner;
    qa_source_save_io io = {0};
    bool okay = qa_strings_create(&owner->strings, e) && qa_source_save_reader(&io, NULL, bytes, e) &&
        fields(&io, owner, peer, graph) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!okay && (!e || e->code == QA_OK)) bad(e, "Invalid restored Unified replica prefix");
    return okay;
}
bool frontend_remote_unified_restore_bind(frontend_remote_unified *owner, qa_unified_session *session, qa_error *e)
{
    qa_unified_session *installed = NULL;
    const qa_net_client *peer = owner ? qa_net_connections_get(
        qa_network_connections(owner->options.domain.runtime), owner->options.domain.client) : NULL;
    if (!owner || !owner->restore_pending || owner->session || !session ||
        !qa_unified_session_source_retired(session) || !qa_unified_session_qualified(session, peer, e) ||
        !qa_unified_session_find(owner->options.domain.runtime, owner->options.domain.client, &installed, e) ||
        installed != session || !retained_valid(owner, peer, e) ||
        !owner->options.current(owner->options.context, &owner->options.domain, e) ||
        !owner->options.consumers.checkpoint_returned ||
        !owner->options.consumers.checkpoint_returned(owner->options.consumers.context, owner)) return false;
    if (!qa_unified_session_client_receipt(session, owner->epoch, owner->admitted, owner->retired,
        owner->offer, owner->frame, owner->prepared_frame, e)) return false;
    owner->session = session; owner->restore_pending = false; return true;
}
bool frontend_remote_unified_restore_pending(const frontend_remote_unified *owner)
{ return owner && owner->restore_pending && !owner->busy; }
bool frontend_remote_unified_checkpoint_current(const frontend_remote_unified *owner, qa_error *e)
{
    bool linked = false;
    for (const frontend_remote_unified *row = owner && owner->frontend ? owner->frontend->remote_unified : NULL;
        row; row = row->next) if (row == owner) { linked = true; break; }
    if (!owner || !linked || owner->busy || owner->frontend->application != owner->options.domain.application ||
        (!owner->frontend->capture && !owner->frontend->source_restoring) ||
        !owner->options.current(owner->options.context, &owner->options.domain, e)) return false;
    const qa_net_client *peer = qa_net_connections_get(qa_network_connections(owner->options.domain.runtime),
        owner->options.domain.client);
    if (!retained_valid(owner, peer, e)) return false;
    if (owner->frontend->source_restoring && !owner->restore_pending) {
        qa_unified_session *installed = NULL;
        return owner->session && qa_unified_session_source_retired(owner->session) &&
            qa_unified_session_find(owner->options.domain.runtime, owner->options.domain.client, &installed, e) &&
            installed == owner->session && qa_unified_session_qualified(installed, peer, e) &&
            qa_unified_session_client_receipt(installed, owner->epoch, owner->admitted, owner->retired,
                owner->offer, owner->frame, owner->prepared_frame, e);
    }
    return !owner->restore_pending || owner->frontend->source_restoring;
}
bool frontend_remote_unified_qualified(const frontend_remote_unified *owner, qa_network_runtime *runtime,
    const qa_net_client *peer, qa_error *e)
{
    qa_unified_session *installed = NULL;
    bool linked = false;
    for (const frontend_remote_unified *row = owner && owner->frontend ? owner->frontend->remote_unified : NULL;
        row; row = row->next) if (row == owner) { linked = true; break; }
    return owner && linked && !owner->restore_pending && owner->options.domain.runtime == runtime &&
        owner->frontend->application == owner->options.domain.application &&
        owner->options.current(owner->options.context, &owner->options.domain, e) &&
        ((owner->frontend->capture || owner->frontend->source_restoring)?
            (owner->options.consumers.checkpoint_returned && owner->options.consumers.checkpoint_returned(owner->options.consumers.context, owner)):
            owner->options.consumers.idle(owner->options.consumers.context, owner)) && retained_valid(owner, peer, e) &&
        qa_unified_session_find(runtime, owner->options.domain.client, &installed, e) && installed == owner->session &&
        qa_unified_session_qualified(installed, peer, e) &&
        qa_unified_session_client_receipt(installed, owner->epoch, owner->admitted, owner->retired,
            owner->offer, owner->frame, owner->prepared_frame, e);
}
bool frontend_remote_unified_restore_dispose(frontend_remote_unified **owned, qa_error *e)
{
    frontend_remote_unified *owner = owned ? *owned : NULL;
    if (!owner) return true;
    if (!owner->restore_pending || owner->busy || (owner->session && !qa_unified_session_source_retired(owner->session)))
        return frontend_unified_fail(e, QA_ERROR_ARGUMENT, "Unified prefix cleanup still owns physical callback custody");
    if (!owner->options.consumers.close(owner->options.consumers.context, owner, e)) return false;
    owner->consumers_live = false;
    owner->session = NULL; owner->retired = true; owner->retirement_pending = false;
    return frontend_remote_unified_destroy(owned, e);
}
