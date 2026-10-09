#include "internal.h"
#include "qa/q3_host_save.h"

enum { GAME_HEADER = 60, ACTOR_BYTES = 104, PORTAL_BYTES = 12 };

bool qa_q3_host_checkpoint_input_retired(const qa_q3_host *host, uint32_t slot, bool *out, qa_error *error)
{
    if (!host || host->retired || host->calls || !host->game || !out ||
        slot >= host->options.server.maximum_clients || slot >= 1022)
        return q3_fail(error, QA_ERROR_ARGUMENT, slot, "Q3 input admission requires its actual idle client owner");
    *out = host->game->slots[slot].input_retired;
    return true;
}

static bool portal_extent(size_t count, size_t capacity)
{
    size_t maximum = 8;
    while (maximum <= count && maximum <= SIZE_MAX / 2) maximum *= 2;
    return count <= capacity && (!capacity ? !count :
        capacity >= 8 && !(capacity & (capacity - 1)) && capacity <= maximum) &&
        capacity <= SIZE_MAX / sizeof(q3_portal_reference);
}

void q3_game_checkpoint_free(q3_game_data *game)
{
    if (!game) return;
    for (size_t i = 0; i < game->portal_count; ++i) free(game->portals[i].targets);
    free(game->portals); free(game);
}

bool q3_game_checkpoint_capture(qa_q3_host *host, qa_buffer *out, qa_error *error)
{
    if (!host->game) { *out = (qa_buffer){0}; return true; }
    q3_game_data *game = host->game;
    if (!portal_extent(game->portal_count, game->portal_capacity))
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 portal allocation differs from its original growth policy");
    uint32_t actors = 0;
    for (size_t i = 0; i < 1022; ++i) {
        q3_entity_slot *slot = game->slots + i;
        if (slot->input_motion || (slot->input_retired && !slot->actor.registry))
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "Q3 input must finish before checkpointing");
        actors += slot->actor.registry != 0;
    }
    size_t size = GAME_HEADER + (size_t)actors * ACTOR_BYTES;
    if (game->portal_count > UINT32_MAX || game->portal_count > (SIZE_MAX - size) / PORTAL_BYTES)
        return q3_fail(error, QA_ERROR_MEMORY, 0, "Q3 game checkpoint is too large");
    size += game->portal_count * PORTAL_BYTES;
    qa_buffer bytes = {.data = calloc(1, size), .size = size};
    if (!bytes.data) return q3_fail(error, QA_ERROR_MEMORY, 0, "Capturing Q3 game host");
    uint8_t *header = bytes.data;
    memcpy(header, "Q3GD", 4);
    qa_store_u32le(header + 4, game->entity_count); qa_store_u32le(header + 8, game->entity_stride);
    qa_store_u32le(header + 12, game->client_stride);
    qa_store_u32le(header + 16, host->options.server.maximum_clients);
    qa_store_u64le(header + 20, game->entities); qa_store_u64le(header + 28, game->clients);
    qa_store_u64le(header + 36, qa_collision_map_identity(qa_world_geometry(host->options.world)));
    qa_store_u32le(header + 44, actors); qa_store_u32le(header + 48, (uint32_t)game->portal_count);
    qa_store_u64le(header + 52, game->portal_capacity);
    uint8_t *row = header + GAME_HEADER;
    for (uint32_t i = 0; i < 1022; ++i) {
        q3_entity_slot *slot = game->slots + i;
        if (!slot->actor.registry) continue;
        qa_saved_actor_id saved;
        if (!qa_actors_save_reference(qa_session_actors(host->options.session), slot->actor, &saved, error)) {
            qa_buffer_free(&bytes); return false;
        }
        qa_store_u32le(row, i); qa_store_u32le(row + 4, (slot->borrowed ? 1u : 0u) |
            (slot->has_visibility ? 2u : 0u) | (slot->input_retired ? 4u : 0u));
        qa_store_u64le(row + 8, saved.generation); qa_store_u32le(row + 16, saved.slot);
        if (slot->has_visibility) {
            qa_store_u32le(row + 20, slot->cluster_count);
            qa_store_u32le(row + 24, (uint32_t)slot->area); qa_store_u32le(row + 28, (uint32_t)slot->area2);
            qa_store_u32le(row + 32, (uint32_t)slot->last_cluster);
            for (uint32_t j = 0; j < 16; ++j) qa_store_u32le(row + 40 + j * 4, (uint32_t)slot->clusters[j]);
        }
        row += ACTOR_BYTES;
    }
    for (size_t i = 0; i < game->portal_count; ++i, row += PORTAL_BYTES) {
        qa_store_u32le(row, game->portals[i].first); qa_store_u32le(row + 4, game->portals[i].second);
        qa_store_u32le(row + 8, game->portals[i].count);
    }
    *out = bytes; return true;
}

static bool source_span(qa_q3_host *host, uint64_t address, size_t bytes, qa_error *error)
{
    if (host->vm) {
        qa_bytes admitted;
        return q3_vm_span(host->vm, address, bytes, &admitted, error);
    }
    return host->native && qa_native_get_backend(host->native) == QA_NATIVE_BACKEND_OWNED_PROCESS &&
        qa_native_range_check(host->native, address, bytes, QA_NATIVE_MEMORY_READ | QA_NATIVE_MEMORY_WRITE, error);
}

static bool descriptor(qa_q3_host *host, const q3_game_data *game, qa_error *error)
{
    if (!game->entities && !game->clients && !game->entity_count && !game->entity_stride && !game->client_stride) return true;
    if ((!host->vm && !host->native) || !game->entities || !game->clients || game->entity_count > 1024 ||
        game->entity_stride < qa_qvm_shared_entity_bytes(host->options.abi) ||
        game->client_stride < qa_qvm_player_bytes(host->options.abi) ||
        (game->entity_stride & 3u) || (game->client_stride & 3u) || (game->entities & 3u) || (game->clients & 3u))
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 checkpoint game-data descriptor is invalid");
    uint64_t size = (uint64_t)game->entity_count * game->entity_stride;
    return (size <= SIZE_MAX || q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 checkpoint entity table is too large")) &&
        source_span(host, game->entities, (size_t)size, error) &&
        source_span(host, game->clients, game->client_stride, error);
}

bool q3_game_checkpoint_decode(qa_q3_host *host, qa_bytes bytes, q3_game_data **out, qa_error *error)
{
    if (!host->game) {
        if (bytes.size) return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 client host checkpoint contains server records");
        *out = NULL; return true;
    }
    if (bytes.size < GAME_HEADER || memcmp(bytes.data, "Q3GD", 4) ||
        qa_load_u32le(bytes.data + 16) != host->options.server.maximum_clients ||
        qa_load_u64le(bytes.data + 36) != qa_collision_map_identity(qa_world_geometry(host->options.world)))
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 game checkpoint map or source identity mismatch");
    uint32_t actors = qa_load_u32le(bytes.data + 44), portals = qa_load_u32le(bytes.data + 48);
    uint64_t portal_capacity = qa_load_u64le(bytes.data + 52);
    if (portal_capacity > SIZE_MAX || !portal_extent(portals, (size_t)portal_capacity))
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 checkpoint portal allocation is invalid");
    size_t remaining = bytes.size - GAME_HEADER;
    if (actors > 1022 || actors > remaining / ACTOR_BYTES)
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 game checkpoint actor table is invalid");
    remaining -= (size_t)actors * ACTOR_BYTES;
    if (portals > remaining / PORTAL_BYTES || remaining != (size_t)portals * PORTAL_BYTES)
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 game checkpoint portal table is invalid");
    q3_game_data *game = calloc(1, sizeof(*game));
    if (!game) return q3_fail(error, QA_ERROR_MEMORY, 0, "Restoring Q3 game bindings");
    game->entity_count = qa_load_u32le(bytes.data + 4); game->entity_stride = qa_load_u32le(bytes.data + 8);
    game->client_stride = qa_load_u32le(bytes.data + 12); game->entities = qa_load_u64le(bytes.data + 20);
    game->clients = qa_load_u64le(bytes.data + 28);
    bool ok = descriptor(host, game, error);
    for (uint32_t i = 0; i < 1024; ++i) game->slots[i] = (q3_entity_slot){.host = host, .number = i};
    const uint8_t *row = bytes.data + GAME_HEADER;
    for (uint32_t i = 0; ok && i < actors; ++i, row += ACTOR_BYTES) {
        uint32_t number = qa_load_u32le(row), flags = qa_load_u32le(row + 4), clusters = qa_load_u32le(row + 20);
        qa_saved_actor_id saved = {qa_load_u64le(row + 8), qa_load_u32le(row + 16)};
        const qa_actor_record *actor = qa_actors_resolve_saved(qa_session_actors(host->options.session), saved);
        if (number >= 1022 || number >= game->entity_count || flags > 7 || clusters > 16 ||
            ((flags & 4u) && number >= host->options.server.maximum_clients) ||
            qa_load_u32le(row + 36) || !actor || game->slots[number].actor.registry ||
            (!(flags & 1u) && (actor->owner != host->options.owner || !actor->has_source || actor->source_slot != number))) {
            ok = q3_fail(error, QA_ERROR_FORMAT, i, "Q3 checkpoint actor ownership or source slot is invalid"); break;
        }
        for (uint32_t j = 0; j < 1022; ++j) if (qa_actor_id_equal(game->slots[j].actor, actor->id)) {
            ok = q3_fail(error, QA_ERROR_FORMAT, i, "Q3 checkpoint actor has duplicate source projections"); break;
        }
        if (!ok) break;
        q3_entity_slot *slot = game->slots + number;
        slot->actor = actor->id; slot->borrowed = (flags & 1u) != 0; slot->has_visibility = (flags & 2u) != 0;
        slot->input_retired = (flags & 4u) != 0;
        if (!slot->borrowed && number < host->options.server.maximum_clients) {
            uint64_t displacement = (uint64_t)number * game->client_stride;
            if (displacement > INT32_MAX || game->clients > UINT64_MAX - displacement ||
                !source_span(host, game->clients + displacement, game->client_stride, error)) {
                ok = q3_fail(error, QA_ERROR_FORMAT, i, "Q3 checkpoint client binding leaves its source memory"); break;
            }
        }
        slot->cluster_count = clusters; slot->area = qa_load_i32le(row + 24);
        slot->area2 = qa_load_i32le(row + 28); slot->last_cluster = qa_load_i32le(row + 32);
        if (slot->has_visibility && (slot->area < -1 || slot->area2 < -1 || slot->last_cluster < -1)) {
            ok = q3_fail(error, QA_ERROR_FORMAT, i, "Q3 checkpoint visibility metadata is invalid"); break;
        }
        for (uint32_t j = 0; j < 16; ++j) {
            slot->clusters[j] = qa_load_i32le(row + 40 + j * 4);
            if (j < clusters && slot->clusters[j] < 0) ok = q3_fail(error, QA_ERROR_FORMAT, i, "Q3 checkpoint cluster is negative");
        }
        if (!slot->has_visibility && (clusters || slot->area || slot->area2 || slot->last_cluster))
            ok = q3_fail(error, QA_ERROR_FORMAT, i, "Q3 checkpoint has unowned visibility state");
    }
    uint32_t cursor = 0; const qa_actor_record *actor;
    while (ok && qa_actors_next(qa_session_actors(host->options.session), &cursor, &actor)) {
        if (actor->owner != host->options.owner || (actor->has_source && actor->source_slot == 1022)) continue;
        if (!actor->has_source || actor->source_slot >= 1022 ||
            !qa_actor_id_equal(game->slots[actor->source_slot].actor, actor->id))
            ok = q3_fail(error, QA_ERROR_FORMAT, cursor, "Q3 checkpoint omits an owned actor");
    }
    if (ok && portal_capacity) {
        game->portals = calloc((size_t)portal_capacity, sizeof(*game->portals));
        game->portal_capacity = (size_t)portal_capacity;
        if (!game->portals) ok = q3_fail(error, QA_ERROR_MEMORY, 0, "Restoring Q3 portal ownership");
    }
    for (uint32_t i = 0; ok && i < portals; ++i, row += PORTAL_BYTES) {
        uint32_t first = qa_load_u32le(row), second = qa_load_u32le(row + 4), count = qa_load_u32le(row + 8);
        for (uint32_t j = 0; j < i; ++j)
            if (game->portals[j].first == first && game->portals[j].second == second)
                ok = q3_fail(error, QA_ERROR_FORMAT, i, "Duplicate Q3 checkpoint portal ownership");
        if (ok) ok = q3_game_portal_reference(host, first, second, game->portals + i, error);
        if (ok) { game->portals[i].count = count; ++game->portal_count; }
    }
    if (!ok) { q3_game_checkpoint_free(game); return false; }
    *out = game; return true;
}

bool q3_game_checkpoint_install(qa_q3_host *host, q3_game_data *game, qa_error *error)
{
    if (!game) return true;
    q3_game_fields_clear(host);
    q3_game_checkpoint_free(host->game); host->game = game;
    host->restore_pending = true;
    /* Binding allocation failure leaves this candidate owned by the host.
     * Candidate abort releases shared actors before destroying their context. */
    return q3_game_bind_restored(host, error);
}
