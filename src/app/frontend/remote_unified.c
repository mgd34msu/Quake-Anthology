#include "remote_unified_private.h"
#include "../application/unified_output_json.h"
#include "qa/network_unified_save.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool frontend_unified_fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }

bool frontend_unified_clone(const qa_unified_document *source, qa_unified_document **out, qa_error *error)
{
    return qa_unified_document_create(qa_unified_document_type(source),
        qa_json_source(qa_unified_document_json(source), qa_unified_document_root(source)), out, error);
}

static bool linked(const frontend_remote_unified *owner)
{
    for (const frontend_remote_unified *row = owner && owner->frontend ? owner->frontend->remote_unified : NULL;
        row; row = row->next) if (row == owner) return true;
    return false;
}

bool frontend_remote_unified_current(const frontend_remote_unified *owner, qa_error *error)
{
    if (!owner || !linked(owner) || owner->retired || owner->frontend->application != owner->options.domain.application ||
        !owner->options.current(owner->options.context, &owner->options.domain, error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified replica lost its actual CLIENT owner");
    if (owner->bound) {
        const qa_net_client *peer = qa_net_connections_get(qa_network_connections(owner->options.domain.runtime),
            owner->options.domain.client);
        if (!peer || peer->protocol.kind != QA_NET_UNIFIED_1 || peer->seat_count != 1 ||
            peer->seats[0].seat.owner != owner->options.domain.seat.owner ||
            peer->seats[0].seat.index != owner->options.domain.seat.index)
            return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified replica changed its real transport seat");
    }
    return !owner->recipe || qa_executable_recipe_current(owner->recipe, owner->options.domain.catalog) ||
        frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified replica changed its retained executable recipe");
}

bool frontend_remote_unified_create(qa_frontend *frontend, const frontend_remote_unified_options *options,
    frontend_remote_unified **out, qa_error *error)
{
    const frontend_remote_unified_domain *d = options ? &options->domain : NULL;
    const frontend_remote_unified_consumers *c = options ? &options->consumers : NULL;
    if (!frontend || !options || !out || *out || !d->application || frontend->application != d->application ||
        frontend->resource_inventory || !d->runtime || !d->catalog || !d->resources || !d->console || !d->cvars ||
        !d->seat.owner || d->client.owner || d->client.generation || d->client.slot ||
        d->physical_seat >= frontend->options.seats || !frontend->seats || !frontend->seats[d->physical_seat].input ||
        !options->identity_capacity || !options->current || !options->userinfo || !options->disconnected ||
        !c->prepare || !c->offer_publish || !c->control || !c->frame || !c->publish || !c->input ||
        !c->sample || !c->draw || !c->idle || !c->close || !c->content_visit ||
        !options->current(options->context, d, error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified construction requires its actual CLIENT and presentation consumers");
    frontend_remote_unified *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Allocating unified readonly replica");
    owner->frontend = frontend; owner->options = *options;
    if (!qa_actors_create(options->identity_capacity, NULL, NULL, &owner->actors, error)) { free(owner); return false; }
    if (!qa_strings_create(&owner->strings, error)) { qa_actors_destroy(owner->actors, NULL); free(owner); return false; }
    qa_catalog_retain(d->catalog);
    owner->next = frontend->remote_unified; frontend->remote_unified = owner; *out = owner; return true;
}

bool frontend_remote_unified_bind(frontend_remote_unified *owner, qa_net_client_id client,
    qa_unified_session *session, qa_error *error)
{
    qa_unified_session *installed = NULL;
    if (!owner || owner->bound || owner->busy || !session || !client.owner || !client.generation ||
        !qa_unified_session_idle(session) || !frontend_remote_unified_current(owner, error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified binding requires its actual successful attach");
    frontend_remote_unified_domain candidate = owner->options.domain; candidate.client = client;
    const qa_net_client *peer = qa_net_connections_get(qa_network_connections(candidate.runtime), client);
    if (!peer || peer->protocol.kind != QA_NET_UNIFIED_1 || peer->seat_count != 1 ||
        peer->seats[0].seat.owner != candidate.seat.owner || peer->seats[0].seat.index != candidate.seat.index ||
        !owner->options.current(owner->options.context, &candidate, error) ||
        !qa_unified_session_find(candidate.runtime, client, &installed, error) || installed != session)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified attach changed its genuine CLIENT tuple");
    owner->options.domain = candidate; owner->session = session; owner->bound = true; return true;
}

qa_executable_recipe *frontend_remote_unified_recipe(const frontend_remote_unified *owner)
{ return owner ? owner->recipe : NULL; }
qa_actor_registry *frontend_remote_unified_registry(const frontend_remote_unified *owner)
{ return owner ? owner->actors : NULL; }
const qa_collision_geometry *frontend_remote_unified_geometry(const frontend_remote_unified *owner)
{ return qa_executable_recipe_geometry(frontend_remote_unified_recipe(owner)); }
const qa_unified_document *frontend_remote_unified_frame(const frontend_remote_unified *owner)
{ return owner ? owner->frame : NULL; }
const frontend_remote_unified_domain *frontend_remote_unified_domain_read(const frontend_remote_unified *owner)
{ return owner ? &owner->options.domain : NULL; }
uint32_t frontend_remote_unified_epoch(const frontend_remote_unified *owner)
{ return owner ? owner->epoch : 0; }

const qa_recipe_provider *frontend_remote_unified_provider(const frontend_remote_unified *owner,
    qa_launch_role role, const char *selector)
{
    if (!owner || !owner->admitted || !owner->recipe || (selector && *selector)) return NULL;
    const qa_unified_document *frame = owner->prepared_frame ? owner->prepared_frame : owner->frame;
    if (!frame) return NULL;
    const qa_json_document *json = qa_unified_document_json(frame);
    qa_json_id output = qa_json_get(json, qa_unified_document_root(frame), "output");
    qa_json_id rows = qa_json_get(json, qa_json_get(json, output, "snapshot"), "configurations");
    const char *field = role == QA_ROLE_MOVEMENT ? "movement" : role == QA_ROLE_ARSENAL ? "weapons" :
        role == QA_ROLE_INVENTORY ? "inventory" : role == QA_ROLE_CHARACTER || role == QA_ROLE_BODY ? "character" : NULL;
    if (!field) return NULL;
    for (size_t i = 0; i < qa_json_size(json, rows); ++i) {
        qa_json_id row = qa_json_at(json, rows, i), actor = qa_json_get(json, row, "actor");
        uint64_t slot, generation;
        if (!qa_json_u64(json, qa_json_get(json, actor, "slot"), &slot, NULL) ||
            !qa_json_u64(json, qa_json_get(json, actor, "generation"), &generation, NULL) ||
            slot != owner->wire_player.slot || generation != owner->wire_player.generation) continue;
        qa_json_id selected = qa_json_get(json, row, field);
        if (role == QA_ROLE_ARSENAL) selected = qa_json_at(json, selected, 0);
        if (role == QA_ROLE_CHARACTER || role == QA_ROLE_BODY)
            selected = qa_json_get(json, selected, role == QA_ROLE_CHARACTER ? "definition" : "appearance");
        qa_json_id instance = qa_json_get(json, selected, "provider"), content = qa_json_get(json, selected, "content");
        for (size_t p = 0; p < qa_executable_recipe_provider_count(owner->recipe); ++p) {
            const qa_recipe_provider *provider = qa_executable_recipe_provider(owner->recipe, p);
            const qa_product *product = provider ? qa_catalog_product(owner->options.domain.catalog,
                provider->selection.product) : NULL;
            if (provider && product && (provider->roles & QA_ROLE_BIT(role)) &&
                qa_json_string_equal(json, instance, provider->selection.instance) &&
                qa_json_string_equal(json, content, product->identity)) return provider;
        }
        return NULL;
    }
    return NULL;
}

bool frontend_remote_unified_actor_present(const frontend_remote_unified *owner, uint32_t slot,
    uint64_t generation)
{
    if (!owner) return false;
    const qa_unified_document *frame = owner->prepared_frame ? owner->prepared_frame : owner->frame;
    if (frame) {
        const qa_json_document *json = qa_unified_document_json(frame);
        qa_json_id rows = qa_json_get(json, qa_json_get(json, qa_json_get(json,
            qa_unified_document_root(frame), "output"), "snapshot"), "actors");
        for (size_t i = 0; i < qa_json_size(json, rows); ++i) {
            qa_json_id id = qa_json_get(json, qa_json_at(json, rows, i), "id");
            uint64_t actual_slot, actual_generation;
            if (qa_json_u64(json, qa_json_get(json, id, "slot"), &actual_slot, NULL) &&
                qa_json_u64(json, qa_json_get(json, id, "generation"), &actual_generation, NULL) &&
                actual_slot == slot && actual_generation == generation) return true;
        }
    }
    return false;
}
bool frontend_remote_unified_actor(frontend_remote_unified *owner, uint32_t slot,
    uint64_t generation, qa_actor_id *out, qa_error *error)
{
    if (!owner || !out || !linked(owner) || owner->retired)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified identity needs its actual replica namespace");
    for (frontend_unified_identity *row = owner->identities; row; row = row->next)
        if (row->wire.slot == slot && row->wire.generation == generation) { *out = row->actual; return true; }
    if ((owner->prepared_frame || owner->frame) && !frontend_remote_unified_actor_present(owner, slot, generation))
        return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified reference lacks its received actor metadata");
    frontend_unified_identity *row = calloc(1, sizeof(*row));
    if (!row) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Retaining unified wire identity");
    if (!qa_actors_allocate(owner->actors, 0, 0, &row->actual, error)) { free(row); return false; }
    row->wire = (qa_saved_actor_id){.slot = slot, .generation = generation};
    row->next = owner->identities; owner->identities = row; *out = row->actual; return true;
}
bool frontend_remote_unified_wire_actor(const frontend_remote_unified *owner, qa_actor_id actor,
    qa_saved_actor_id *out)
{
    if (!owner || !out) return false;
    for (const frontend_unified_identity *row = owner->identities; row; row = row->next)
        if (qa_actor_id_equal(row->actual, actor)) { *out = row->wire; return true; }
    return false;
}
bool frontend_remote_unified_player(const frontend_remote_unified *owner, qa_actor_id *actor, uint32_t *entity)
{
    if (!owner || !owner->admitted || !actor || !entity) return false;
    *actor = owner->player; *entity = owner->source_entity; return true;
}

static qa_json_id value(const qa_unified_document *document)
{ return qa_json_get(qa_unified_document_json(document), qa_unified_document_root(document), "value"); }
static bool same_document(const qa_unified_document *a, const qa_unified_document *b)
{
    if (!a || !b || qa_unified_document_type(a) != qa_unified_document_type(b)) return false;
    qa_bytes x = qa_json_source(qa_unified_document_json(a), qa_unified_document_root(a));
    qa_bytes y = qa_json_source(qa_unified_document_json(b), qa_unified_document_root(b));
    return x.size == y.size && (!x.size || !memcmp(x.data, y.data, x.size));
}
static bool wire_actor(const qa_json_document *json, qa_json_id id, qa_saved_actor_id *out, qa_error *error)
{
    uint64_t slot, generation;
    if (!qa_json_u64(json, qa_json_get(json, id, "slot"), &slot, error) || slot > UINT32_MAX ||
        !qa_json_u64(json, qa_json_get(json, id, "generation"), &generation, error))
        return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified actor leaves its actual wire identity domain");
    *out = (qa_saved_actor_id){.slot = (uint32_t)slot, .generation = generation}; return true;
}

static bool stage_metadata(frontend_remote_unified *owner, qa_error *error)
{
    const qa_json_document *json = qa_unified_document_json(owner->prepared_frame);
    qa_json_id rows = qa_json_get(json, qa_json_get(json, qa_json_get(json,
        qa_unified_document_root(owner->prepared_frame), "output"), "snapshot"), "actors");
    size_t count = qa_json_size(json, rows);
    frontend_unified_metadata *metadata = count ? calloc(count, sizeof(*metadata)) : NULL;
    if (count && !metadata) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Staging received actor metadata");
    for (size_t i = 0; i < count; ++i) {
        qa_json_id row = qa_json_at(json, rows, i);
        qa_saved_actor_id wire; qa_buffer source = {0}, definition = {0};
        bool okay = wire_actor(json, qa_json_get(json, row, "id"), &wire, error) &&
            frontend_remote_unified_actor(owner, wire.slot, wire.generation, &metadata[i].actor, error) &&
            qa_json_string(json, qa_json_get(json, row, "owner"), &source, error) &&
            qa_json_string(json, qa_json_get(json, row, "definition"), &definition, error) &&
            qa_strings_intern_cstr(owner->strings, (const char *)source.data, &metadata[i].owner, error) &&
            qa_strings_intern_cstr(owner->strings, (const char *)definition.data, &metadata[i].definition, error);
        qa_buffer_free(&source); qa_buffer_free(&definition);
        if (!okay) { free(metadata); return false; }
        for (size_t j = 0; j < i; ++j) if (qa_actor_id_equal(metadata[j].actor, metadata[i].actor)) {
            free(metadata); return frontend_unified_fail(error, QA_ERROR_FORMAT, "Duplicate received actor metadata");
        }
    }
    owner->metadata = metadata; owner->metadata_count = count; return true;
}

static bool metadata_apply(frontend_remote_unified *owner, qa_error *error)
{
    uint64_t revision = qa_actors_revision(owner->actors);
    if (owner->metadata_count > (UINT64_MAX - revision) / 2)
        return frontend_unified_fail(error, QA_ERROR_MEMORY, "Actor metadata cannot retain a rollback revision");
    for (size_t i = 0; i < owner->metadata_count; ++i) {
        frontend_unified_metadata *row = owner->metadata + i;
        const qa_actor_record *actual = qa_actors_get(owner->actors, row->actor);
        if (!actual || actual->has_source)
            return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Received actor metadata lost its private identity");
        row->old_owner = actual->owner; row->old_definition = actual->definition;
    }
    for (size_t i = 0; i < owner->metadata_count; ++i) {
        frontend_unified_metadata *row = owner->metadata + i;
        if (!qa_actors_set_metadata(owner->actors, row->actor, row->owner, row->definition, error)) {
            for (size_t j = 0; j < i; ++j) {
                frontend_unified_metadata *old = owner->metadata + j;
                (void)qa_actors_set_metadata(owner->actors, old->actor, old->old_owner, old->old_definition, NULL);
            }
            return false;
        }
    }
    return true;
}

static void metadata_rollback(frontend_remote_unified *owner)
{
    for (size_t i = 0; i < owner->metadata_count; ++i) {
        frontend_unified_metadata *row = owner->metadata + i;
        (void)qa_actors_set_metadata(owner->actors, row->actor, row->old_owner, row->old_definition, NULL);
    }
}

static bool prepare_offer(frontend_remote_unified *owner, const qa_unified_document *document,
    bool *ready, qa_error *error)
{
    if (owner->offer && !same_document(owner->offer, document))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified preparation changed its retained offer");
    if (!owner->offer && !frontend_unified_clone(document, &owner->offer, error)) return false;
    if (!owner->preparing_recipe && !qa_executable_recipe_prepare(document, owner->options.domain.catalog,
        owner->options.domain.resources, &owner->preparing_recipe, error)) return false;
    owner->preparing = true; owner->consumers_live = true;
    bool okay = owner->options.consumers.prepare(owner->options.consumers.context,
        owner, owner->preparing_recipe, ready, error);
    if (okay) okay = frontend_remote_unified_current(owner, error) &&
        qa_executable_recipe_current(owner->preparing_recipe, owner->options.domain.catalog);
    if (okay) owner->prepared = *ready;
    return okay;
}

static bool prepare_frame(frontend_remote_unified *owner, const qa_unified_document *document,
    bool *ready, qa_error *error)
{
    if (!owner->recipe || !owner->admitted || owner->preparing)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified frame precedes actual world and player admission");
    const qa_json_document *json = qa_unified_document_json(document);
    qa_json_id root = qa_unified_document_root(document), player = qa_json_get(json, root, "player");
    qa_saved_actor_id actor;
    if (!wire_actor(json, qa_json_get(json, player, "actor"), &actor, error) ||
        actor.slot != owner->wire_player.slot || actor.generation != owner->wire_player.generation)
        return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified frame changes its admitted full player");
    if (owner->prepared_frame && !same_document(owner->prepared_frame, document))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified frame preparation changed its retained source bytes");
    if (!owner->prepared_frame) {
        qa_buffer bytes = {0}; qa_unified_document *prediction = NULL, *copy = NULL;
        bool okay = qa_unified_document_bytes(document, qa_json_get(json, root, "prediction"), &bytes, error) &&
            qa_unified_document_decode(QA_UNIFIED_PREDICTION_DOCUMENT, (qa_bytes){bytes.data, bytes.size}, &prediction, error) &&
            frontend_unified_clone(document, &copy, error);
        qa_buffer_free(&bytes);
        if (!okay) { qa_unified_document_destroy(prediction); qa_unified_document_destroy(copy); return false; }
        const qa_json_document *p = qa_unified_document_json(prediction);
        qa_saved_actor_id predicted; int64_t sequence, acknowledged;
        okay = wire_actor(p, qa_json_get(p, qa_unified_document_root(prediction), "actor"), &predicted, error) &&
            qa_json_i64(p, qa_json_get(p, qa_unified_document_root(prediction), "sequence"), &sequence, error) &&
            qa_json_i64(json, qa_json_get(json, root, "acknowledgedInput"), &acknowledged, error) &&
            predicted.slot == actor.slot && predicted.generation == actor.generation && sequence == acknowledged;
        if (!okay) { qa_unified_document_destroy(prediction); qa_unified_document_destroy(copy);
            return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified prediction changes the admitted player or acknowledgement"); }
        owner->prediction = prediction; owner->prepared_frame = copy;
    }
    if (!owner->metadata && !stage_metadata(owner, error)) return false;
    return owner->options.consumers.frame(owner->options.consumers.context, owner,
        owner->prepared_frame, owner->prediction, ready, error) && frontend_remote_unified_current(owner, error);
}

static bool prepare(void *context, qa_net_client_id client, const qa_unified_document *document,
    bool *ready, qa_error *error)
{
    frontend_remote_unified *owner = context;
    if (!owner || owner->busy || !ready || !qa_net_client_id_equal(client, owner->options.domain.client) ||
        !frontend_remote_unified_current(owner, error)) return false;
    *ready = true; owner->busy = true;
    bool okay = true;
    if (qa_unified_document_type(document) == QA_UNIFIED_FRAME_DOCUMENT)
        okay = prepare_frame(owner, document, ready, error);
    else if (qa_json_string_equal(qa_unified_document_json(document),
        qa_json_get(qa_unified_document_json(document), value(document), "kind"), "offer"))
        okay = prepare_offer(owner, document, ready, error);
    owner->busy = false; return okay;
}

static bool ready_document(frontend_remote_unified *owner, qa_unified_document **out, qa_error *error)
{
    const char *userinfo = NULL;
    if (!owner->options.userinfo(owner->options.context, &owner->options.domain, &userinfo, error) || !userinfo) return false;
    char digest[72] = "sha256:"; qa_sha256_hex(qa_executable_recipe_digest(owner->recipe), digest + 7);
    application_unified_json json = {0};
    bool okay = application_unified_json_text(&json, "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"ready\",\"epoch\":", error) &&
        application_unified_json_natural(&json, owner->epoch, error) &&
        application_unified_json_text(&json, ",\"composition\":", error) && application_unified_json_string(&json, digest, error) &&
        application_unified_json_text(&json, ",\"userinfo\":", error) && application_unified_json_string(&json, userinfo, error) &&
        application_unified_json_text(&json, "}}", error) && qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT,
            (qa_bytes){json.bytes.data, json.bytes.size}, out, error);
    application_unified_json_dispose(&json); return okay;
}

static bool resources(frontend_remote_unified *owner, const qa_unified_document *document, qa_error *error)
{
    const qa_json_document *json = qa_unified_document_json(document);
    qa_json_id rows = qa_json_get(json, value(document), "resources");
    for (size_t i = 0; i < qa_json_size(json, rows); ++i) {
        qa_json_id key = qa_json_at(json, rows, i);
        qa_buffer content = {0}, path = {0}, digest = {0}; uint64_t length;
        qa_sha256_digest hash; qa_launch_resource actual; qa_vfs *view; const qa_vfs_acquisition *receipt;
        bool okay = qa_json_string(json, qa_json_get(json, key, "content"), &content, error) &&
            qa_json_string(json, qa_json_get(json, key, "path"), &path, error) &&
            qa_json_string(json, qa_json_get(json, key, "digest"), &digest, error) &&
            qa_sha256_parse((const char *)digest.data, &hash, error) &&
            qa_json_u64(json, qa_json_get(json, key, "byteLength"), &length, error) &&
            qa_executable_recipe_acquire_resource(owner->recipe, (const char *)content.data, (const char *)path.data,
                &hash, length, &actual, &view, &receipt, error);
        qa_buffer_free(&content); qa_buffer_free(&path); qa_buffer_free(&digest);
        if (!okay) return false;
    }
    return true;
}

static bool control(void *context, qa_network_runtime *runtime, qa_net_client_id client,
    uint32_t epoch, const qa_unified_document *document, qa_unified_session_commit *commit, qa_error *error)
{
    frontend_remote_unified *owner = context;
    if (!owner || owner->busy || runtime != owner->options.domain.runtime ||
        !qa_net_client_id_equal(client, owner->options.domain.client) || !commit ||
        !frontend_remote_unified_current(owner, error)) return false;
    const qa_json_document *json = qa_unified_document_json(document);
    qa_json_id v = value(document), kind = qa_json_get(json, v, "kind");
    owner->busy = true; bool okay = true;
    if (qa_json_string_equal(json, kind, "offer")) {
        if (owner->epoch != epoch) {
            okay = owner->prepared && owner->preparing_recipe && same_document(owner->offer, document) &&
                epoch == qa_executable_recipe_epoch(owner->preparing_recipe) &&
                owner->options.consumers.offer_publish(owner->options.consumers.context, owner, owner->preparing_recipe, error);
            if (okay) {
                owner->retiring_recipe = owner->recipe; owner->recipe = owner->preparing_recipe; owner->preparing_recipe = NULL;
                owner->epoch = epoch; owner->admitted = false; owner->player = (qa_actor_id){0}; owner->transport_restarted = false;
                owner->frame_number = 0; owner->preparing = owner->prepared = false;
                qa_unified_document_destroy(owner->frame); owner->frame = NULL;
                qa_unified_document_destroy(owner->prepared_frame); owner->prepared_frame = NULL;
                qa_unified_document_destroy(owner->prediction); owner->prediction = NULL;
                okay = qa_actors_clear(owner->actors, error);
                if (okay) while (owner->identities) { frontend_unified_identity *row = owner->identities;
                    owner->identities = row->next; free(row); }
            }
        }
        if (okay && owner->retiring_recipe) { okay = qa_executable_recipe_close(owner->retiring_recipe, error);
            if (okay) owner->retiring_recipe = NULL; }
        if (okay && !owner->transport_restarted) {
            okay = qa_network_restart(runtime, client, qa_executable_recipe_digest(owner->recipe), error);
            if (okay) owner->transport_restarted = true;
        }
        if (okay) okay = ready_document(owner, &commit->reply, error);
        if (okay) { qa_unified_document_destroy(owner->offer); owner->offer = NULL; commit->applied = true; }
    } else if (epoch != owner->epoch) okay = true;
    else if (qa_json_string_equal(json, kind, "admitted")) {
        qa_saved_actor_id actor, wire_client; uint64_t source;
        okay = wire_actor(json, qa_json_get(json, v, "actor"), &actor, error) &&
            wire_actor(json, qa_json_get(json, v, "client"), &wire_client, error) &&
            qa_json_u64(json, qa_json_get(json, v, "sourceEntity"), &source, error) && source <= UINT32_MAX &&
            frontend_remote_unified_actor(owner, actor.slot, actor.generation, &owner->player, error);
        if (okay) { owner->wire_player = actor; owner->wire_client = wire_client;
            owner->source_entity = (uint32_t)source; owner->admitted = true; commit->applied = true; }
    } else if (qa_json_string_equal(json, kind, "resources")) {
        okay = owner->recipe && resources(owner, document, error);
        if (okay) okay = owner->options.consumers.control(owner->options.consumers.context, owner, document, error);
        commit->applied = okay;
    } else if (qa_json_string_equal(json, kind, "disconnect")) {
        qa_buffer reason = {0};
        okay = qa_json_string(json, qa_json_get(json, v, "reason"), &reason, error) &&
            owner->options.disconnected(owner->options.context, &owner->options.domain, (const char *)reason.data, error);
        qa_buffer_free(&reason); if (okay) owner->retired = true; commit->applied = okay;
    } else { okay = owner->recipe && owner->options.consumers.control(owner->options.consumers.context,
        owner, document, error); commit->applied = okay; }
    owner->busy = false; return okay;
}

static bool frame(void *context, qa_network_runtime *runtime, qa_net_client_id client,
    const qa_unified_document *document, qa_unified_session_commit *commit, qa_error *error)
{
    frontend_remote_unified *owner = context;
    if (!owner || owner->busy || runtime != owner->options.domain.runtime || !commit ||
        !qa_net_client_id_equal(client, owner->options.domain.client) || !frontend_remote_unified_current(owner, error) ||
        !same_document(owner->prepared_frame, document)) return false;
    const qa_json_document *json = qa_unified_document_json(document);
    qa_json_id root = qa_unified_document_root(document), snapshot = qa_json_get(json, qa_json_get(json, root, "output"), "snapshot");
    uint64_t number; int64_t acknowledged;
    if (!qa_json_u64(json, qa_json_get(json, qa_json_get(json, snapshot, "frame"), "frame"), &number, error) ||
        !qa_json_i64(json, qa_json_get(json, root, "acknowledgedInput"), &acknowledged, error)) return false;
    owner->busy = true; bool okay = true;
    if (!owner->frame || number > owner->frame_number) {
        okay = metadata_apply(owner, error);
        if (okay && !owner->options.consumers.publish(owner->options.consumers.context, owner, owner->prepared_frame, error)) {
            metadata_rollback(owner); okay = false;
        }
    }
    if (okay) {
        if (!owner->frame || number > owner->frame_number) { qa_unified_document_destroy(owner->frame);
            owner->frame = owner->prepared_frame; owner->prepared_frame = NULL; owner->frame_number = number; }
        else { qa_unified_document_destroy(owner->prepared_frame); owner->prepared_frame = NULL; }
        qa_unified_document_destroy(owner->prediction); owner->prediction = NULL;
        free(owner->metadata); owner->metadata = NULL; owner->metadata_count = 0;
        commit->applied = true; commit->acknowledged_input = acknowledged;
    }
    owner->busy = false; return okay;
}
static void closed(void *context, qa_net_client_id client)
{
    frontend_remote_unified *owner = context;
    if (owner && qa_net_client_id_equal(client, owner->options.domain.client)) { owner->retired = true; owner->session = NULL; }
}
static bool transport_player(void *context, qa_net_client_id client,
    qa_unified_session_player *out, qa_error *error)
{
    frontend_remote_unified *owner = context;
    if (!owner || !out || !owner->admitted || !owner->frame || owner->retired ||
        !qa_net_client_id_equal(client, owner->options.domain.client))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified player awaits its actual admitted frame");
    const qa_recipe_provider *movement = frontend_remote_unified_provider(owner, QA_ROLE_MOVEMENT, "");
    const qa_recipe_provider *arsenal = frontend_remote_unified_provider(owner, QA_ROLE_ARSENAL, "");
    if (!movement || !movement->source_owner || !arsenal || !arsenal->selection.instance ||
        !qa_actors_get(owner->actors, owner->player))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified player lacks its received selected providers");
    qa_movement_kind kind;
    switch (movement->selection.clock.kind) {
    case QA_CLOCK_NETQUAKE: kind = QA_MOVEMENT_NETQUAKE; break;
    case QA_CLOCK_QUAKEWORLD: kind = QA_MOVEMENT_QUAKEWORLD; break;
    case QA_CLOCK_Q2_CLASSIC: kind = QA_MOVEMENT_Q2_CLASSIC; break;
    case QA_CLOCK_Q2_RERELEASE: kind = QA_MOVEMENT_Q2_RERELEASE; break;
    case QA_CLOCK_Q3: kind = QA_MOVEMENT_Q3; break;
    default: return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified player has an unknown movement clock");
    }
    *out = (qa_unified_session_player){.actor = owner->player, .seat = owner->options.domain.seat,
        .movement = kind, .arsenal = {(const uint8_t *)arsenal->selection.instance, strlen(arsenal->selection.instance)},
        .source_owner = movement->source_owner, .source_slot = owner->source_entity};
    return true;
}
qa_unified_session_hooks frontend_remote_unified_hooks(frontend_remote_unified *owner)
{ return (qa_unified_session_hooks){.context = owner, .player = transport_player,
    .prepare = prepare, .control = control, .frame = frame, .closed = closed}; }

bool frontend_remote_unified_submit(frontend_remote_unified *owner, const qa_unified_input *input,
    double command_time_ms, qa_error *error)
{
    if (!owner || owner->busy || !input || !isfinite(command_time_ms) || !owner->admitted || !owner->frame || !owner->session ||
        !qa_unified_session_idle(owner->session) || !frontend_remote_unified_current(owner, error)) return false;
    owner->busy = true;
    bool okay = owner->options.consumers.input(owner->options.consumers.context, owner, input, command_time_ms, error);
    owner->busy = false;
    return okay && qa_unified_session_input(owner->session, input, error);
}

static bool command_document(frontend_remote_unified *owner, const char *name,
    const qa_unified_document *component, uint64_t generation, const char *const *args, size_t count, qa_error *error)
{
    if (!owner || owner->busy || !owner->admitted || !owner->session || (count && !args) || count > 128 ||
        !qa_unified_session_idle(owner->session) || !frontend_remote_unified_current(owner, error)) return false;
    application_unified_json json = {0}; qa_unified_document *document = NULL;
    bool okay = application_unified_json_text(&json, "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":", error) &&
        application_unified_json_string(&json, component ? "component-command" : "command", error) &&
        application_unified_json_text(&json, ",\"epoch\":", error) && application_unified_json_natural(&json, owner->epoch, error);
    if (okay && component) okay = application_unified_json_text(&json, ",\"owner\":", error) &&
        application_unified_json_document(&json, component, error) && application_unified_json_text(&json, ",\"generation\":", error) &&
        application_unified_json_natural(&json, generation, error);
    else if (okay) okay = name && application_unified_json_text(&json, ",\"name\":", error) && application_unified_json_string(&json, name, error);
    if (okay) okay = application_unified_json_text(&json, ",\"args\":[", error);
    for (size_t i = 0; okay && i < count; ++i) okay = args[i] &&
        (!i || application_unified_json_text(&json, ",", error)) && application_unified_json_string(&json, args[i], error);
    if (okay) okay = application_unified_json_text(&json, "]}}", error) && qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT,
        (qa_bytes){json.bytes.data, json.bytes.size}, &document, error) && qa_unified_session_control(owner->session, document, error);
    qa_unified_document_destroy(document); application_unified_json_dispose(&json); return okay;
}
bool frontend_remote_unified_command(frontend_remote_unified *owner, const char *name,
    const char *const *args, size_t count, qa_error *error)
{ return command_document(owner, name, NULL, 0, args, count, error); }
bool frontend_remote_unified_component_command(frontend_remote_unified *owner,
    const qa_unified_document *component, uint64_t generation, const char *const *args, size_t count, qa_error *error)
{ return component && command_document(owner, NULL, component, generation, args, count, error); }

bool frontend_remote_unified_sample(qa_frontend *frontend, uint64_t now, qa_error *error)
{
    if (!frontend) return false;
    for (frontend_remote_unified *owner = frontend->remote_unified; owner; owner = owner->next) {
        if (owner->retired) continue;
        if (owner->busy || !frontend_remote_unified_current(owner, error)) return false;
        owner->busy = true;
        bool okay = owner->options.consumers.sample(owner->options.consumers.context, owner, now, error);
        owner->busy = false; if (!okay) return false;
    }
    return true;
}
bool frontend_remote_unified_draw(qa_frontend *frontend, uint32_t seat, float stereo,
    qa_audio_listener *listener, bool *rendered, qa_error *error)
{
    if (!frontend || !listener || !rendered || !isfinite(stereo)) return false;
    *rendered = false; frontend_remote_unified *found = NULL;
    for (frontend_remote_unified *owner = frontend->remote_unified; owner; owner = owner->next)
        if (!owner->retired && owner->options.domain.physical_seat == seat) {
            if (found) return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Physical seat has multiple unified replicas");
            found = owner;
        }
    if (!found) return true;
    *rendered = true;
    if (!found->frame) { *listener = (qa_audio_listener){.seat = seat, .actor = QA_AUDIO_NO_ACTOR}; return true; }
    if (found->busy || !frontend_remote_unified_current(found, error)) return false;
    found->busy = true;
    bool okay = found->options.consumers.draw(found->options.consumers.context, found, stereo, listener, error);
    found->busy = false; return okay;
}
bool frontend_remote_unified_idle(const qa_frontend *frontend)
{
    if (!frontend) return true;
    for (const frontend_remote_unified *owner = frontend->remote_unified; owner; owner = owner->next)
        if (owner->busy || !owner->options.consumers.idle(owner->options.consumers.context, owner)) return false;
    return true;
}
bool frontend_remote_unified_destroy(frontend_remote_unified **slot, qa_error *error)
{
    if (!slot || !*slot) return true;
    frontend_remote_unified *owner = *slot;
    if (!linked(owner) || owner->busy || owner->frontend->resource_inventory || owner->session ||
        (owner->bound && !owner->retired) ||
        !owner->options.consumers.idle(owner->options.consumers.context, owner))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified cleanup requires its actually retired returned session");
    if (owner->consumers_live && !owner->options.consumers.close(owner->options.consumers.context, owner, error)) return false;
    owner->consumers_live = false;
    if (owner->retiring_recipe) { if (!qa_executable_recipe_close(owner->retiring_recipe, error)) return false; owner->retiring_recipe = NULL; }
    if (owner->preparing_recipe) { if (!qa_executable_recipe_close(owner->preparing_recipe, error)) return false; owner->preparing_recipe = NULL; }
    if (owner->recipe) { if (!qa_executable_recipe_close(owner->recipe, error)) return false; owner->recipe = NULL; }
    if (!qa_actors_destroy(owner->actors, error)) return false;
    qa_strings_destroy(owner->strings); free(owner->metadata);
    frontend_remote_unified **row = &owner->frontend->remote_unified;
    while (*row != owner) row = &(*row)->next; *row = owner->next;
    while (owner->identities) { frontend_unified_identity *identity = owner->identities;
        owner->identities = identity->next; free(identity); }
    qa_unified_document_destroy(owner->offer); qa_unified_document_destroy(owner->frame);
    qa_unified_document_destroy(owner->prepared_frame); qa_unified_document_destroy(owner->prediction);
    qa_catalog_release(owner->options.domain.catalog); free(owner); *slot = NULL; return true;
}
bool frontend_remote_unified_transport_retired(frontend_remote_unified *owner,
    const qa_unified_session *session, qa_error *error)
{
    if (!owner || !linked(owner) || owner->busy || owner->session != session ||
        !session || !qa_unified_session_source_retired(session))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified retirement still owns physical transport callbacks");
    owner->session = NULL; owner->retired = true;
    return true;
}
bool frontend_remote_unified_destroy_all(qa_frontend *frontend, qa_error *error)
{
    if (!frontend) return true;
    while (frontend->remote_unified) { frontend_remote_unified *owner = frontend->remote_unified;
        if (!frontend_remote_unified_destroy(&owner, error)) return false; }
    return true;
}
bool frontend_remote_unified_content_visit(const qa_frontend *frontend,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!frontend || !visitor || !visitor->pool || !visitor->catalog || !visitor->view) return false;
    for (const frontend_remote_unified *owner = frontend->remote_unified; owner; owner = owner->next) {
        if (!visitor->pool(visitor->context, owner->options.domain.resources, error) ||
            !visitor->catalog(visitor->context, owner->options.domain.catalog, error)) return false;
        const qa_executable_recipe *recipes[] = {owner->recipe, owner->preparing_recipe, owner->retiring_recipe};
        for (size_t i = 0; i < 3; ++i) if (recipes[i] &&
            !qa_executable_recipe_content_visit(recipes[i], visitor, error)) return false;
        if (!owner->options.consumers.content_visit(owner->options.consumers.context, owner, visitor, error)) return false;
    }
    return true;
}
