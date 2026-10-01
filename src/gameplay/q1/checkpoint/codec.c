#include "internal.h"
#include "../wire_internal.h"

static const uint8_t signature[8] = {'Q', 'A', 'Q', '1', 'S', 'A', 'V', 'E'};

struct qa_q1_restore {
    qa_q1_game *destination;
    qa_q1_game before, candidate;
    q1_map_runtime map_before;
    uint64_t registry_revision, wire_revision, wire_generation;
};

static bool boundary(const qa_q1_game *g, bool empty, qa_error *error) {
    if (!g || g->destroy_pending || g->observation_depth || !qa_session_safe(g->services.session) ||
        !qa_world_idle(g->services.world) || !qa_combat_idle(g->services.combat) ||
        !qa_inventory_idle(g->services.inventory) ||
        (g->services.pickups && !qa_pickups_idle(g->services.pickups)) ||
        g->capacity != qa_actors_capacity(qa_session_actors(g->services.session)) ||
        (g->services.physics && g->services.physics->push_transaction)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 checkpoint requires a session safe point");
        return false;
    }
    for (const q1_actor_snapshot *s = g->snapshots; s; s = s->next)
        if (s->borrowed) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 checkpoint has an active actor query");
            return false;
        }
    if (empty)
        for (uint32_t i = 0; i < g->capacity; ++i)
            if (g->actors[i] || g->players[i]) {
                qa_error_set(error, QA_ERROR_ARGUMENT, i,
                             "Q1 restore requires an empty native provider");
                return false;
            }
    return true;
}
static bool same_target(const qa_target_binding *a, const qa_target_binding *b) {
    return qa_actor_id_equal(a->actor, b->actor) && a->source == b->source &&
        a->context == b->context && a->read == b->read && a->use == b->use &&
        a->field == b->field && a->set_target == b->set_target &&
        a->set_delay == b->set_delay && a->set_targetname == b->set_targetname &&
        a->remap_shader == b->remap_shader;
}
/* Candidate storage never owns shared services or published bindings. */
static void storage_free(qa_q1_game *g) {
    q1_wire_destroy(g);
    while (g->allocated_actors) {
        q1_actor *next = g->allocated_actors->allocation_next;
        free(g->allocated_actors);
        g->allocated_actors = next;
    }
    while (g->allocated_players) {
        q1_player *next = g->allocated_players->allocation_next;
        q1_source_client_clear(g->allocated_players);
        free(g->allocated_players);
        g->allocated_players = next;
    }
    if (g->maps) {
        while (g->maps->allocated) {
            q1_map_state *next = g->maps->allocated->allocated_next;
            free(g->maps->allocated);
            g->maps->allocated = next;
        }
        while (g->maps->door_groups) {
            q1_door_group *next = g->maps->door_groups->next;
            free(g->maps->door_groups->members);
            free(g->maps->door_groups);
            g->maps->door_groups = next;
        }
        free(g->maps->rotated_targets);
        free(g->maps->frame_ticks);
        free(g->maps->addon_contacts);
        free(g->maps);
        g->maps = NULL;
    }
    free(g->actors);
    free(g->players);
    g->actors = NULL;
    g->players = NULL;
}
static void *allocate(q1_save_io *io, size_t count, size_t size) {
    if (count > SIZE_MAX / size) {
        q1_save_fail(io, "Q1 checkpoint allocation size overflow");
        return NULL;
    }
    void *data = calloc(count, size);
    if (!data)
        qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Allocating Q1 checkpoint state");
    return data;
}
static bool groups(q1_save_io *io, qa_q1_game *g, q1_door_group ***out, size_t *out_count) {
    uint32_t count = 0;
    if (!io->reading && g->maps)
        for (q1_door_group *group = g->maps->door_groups; group; group = group->next) {
            if (count == UINT32_MAX)
                return q1_save_fail(io, "Too many Q1 door groups");
            ++count;
        }
    if (!q1_save_u32(io, &count))
        return false;
    if (!count) {
        *out = NULL;
        *out_count = 0;
        return true;
    }
    if (!g->maps || (io->reading && count > (io->input.size - io->offset) / 4))
        return q1_save_fail(io, "Invalid Q1 checkpoint door group count");
    q1_door_group **index = allocate(io, count, sizeof(*index));
    if (!index)
        return false;
    q1_door_group *group = g->maps->door_groups, **tail = &g->maps->door_groups;
    for (uint32_t i = 0; i < count; ++i) {
        if (io->reading) {
            group = allocate(io, 1, sizeof(*group));
            if (!group)
                goto fail;
            *tail = group;
        }
        index[i] = group;
        uint32_t members = io->reading ? 0 : (uint32_t)group->count;
        if ((!io->reading && group->count > UINT32_MAX) || !q1_save_u32(io, &members))
            goto fail;
        if (!members || members > g->capacity ||
            (io->reading && members > (io->input.size - io->offset) / 13)) {
            q1_save_fail(io, "Invalid Q1 checkpoint door group members");
            goto fail;
        }
        if (io->reading) {
            group->count = members;
            group->members = allocate(io, members, sizeof(*group->members));
            if (!group->members)
                goto fail;
        }
        for (uint32_t j = 0; j < members; ++j)
            if (!q1_save_actor(io, &group->members[j]) || !group->members[j].registry) {
                if (!group->members[j].registry)
                    q1_save_fail(io, "Null Q1 door group member");
                goto fail;
            }
        tail = &group->next;
        group = group->next;
    }
    *out = index;
    *out_count = count;
    return true;
fail:
    free(index);
    return false;
}
static bool actors(q1_save_io *io, qa_q1_game *g, q1_door_group **index, size_t group_count) {
    uint32_t count = 0;
    if (!io->reading)
        for (uint32_t i = 0; i < g->capacity; ++i)
            count += g->actors[i] != NULL;
    Q1_SAVE(io, u32, count);
    if (count > g->capacity || (io->reading && count > (io->input.size - io->offset) / 13))
        return q1_save_fail(io, "Invalid Q1 checkpoint actor count");
    uint32_t cursor = 0;
    for (uint32_t i = 0; i < count; ++i) {
        q1_actor *actor;
        if (io->reading) {
            actor = allocate(io, 1, sizeof(*actor));
            if (!actor)
                return false;
            actor->allocation_next = g->allocated_actors;
            g->allocated_actors = actor;
        } else {
            while (cursor < g->capacity && !g->actors[cursor])
                ++cursor;
            actor = g->actors[cursor++];
            if (!actor->active)
                return q1_save_fail(io, "Inactive Q1 actor in live slot");
        }
        if (!q1_save_entity(io, actor))
            return false;
        const qa_actor_record *shared =
            qa_actors_get(qa_session_actors(g->services.session), actor->id);
        if (!shared || (actor->native && shared->owner != g->options.provider))
            return q1_save_fail(io, "Q1 continuation disagrees with shared actor ownership");
        bool target = false;
        if (!io->reading && g->maps) {
            qa_target_binding actual, expected;
            if (qa_persistence_targets_binding(g->maps->options.targets, actor->id, &actual) &&
                actual.context == g) {
                if (!qa_q1_game_target_binding(g, actor->id, &expected, io->error) ||
                    !same_target(&actual, &expected))
                    return q1_save_fail(io, "Q1 target has no matching source declaration");
                target = true;
            }
        }
        Q1_SAVE(io, bool, target);
        if (target && (!g->maps || !actor->native))
            return q1_save_fail(io, "Q1 target has no matching native continuation");
        if (io->reading)
            actor->restored_target = target;
        if (io->reading) {
            if (actor->id.slot >= g->capacity || g->actors[actor->id.slot])
                return q1_save_fail(io, "Duplicate Q1 actor continuation");
            g->actors[actor->id.slot] = actor;
        }
        bool map = actor->map != NULL;
        Q1_SAVE(io, bool, map);
        if (map) {
            if (!g->maps)
                return q1_save_fail(io, "Q1 map actor has no map services");
            if (io->reading) {
                actor->map = allocate(io, 1, sizeof(*actor->map));
                if (!actor->map)
                    return false;
                actor->map->allocated_next = g->maps->allocated;
                g->maps->allocated = actor->map;
            }
            if (!q1_save_map(io, actor->map, index, group_count))
                return false;
        }
        if ((actor->kind == Q1_MAP || actor->think == Q1_THINK_MAP ||
             actor->think == Q1_THINK_SPAWN_TEMPLATE) &&
            !map)
            return q1_save_fail(io, "Q1 authored continuation lacks its map state");
    }
    return true;
}
static bool players(q1_save_io *io, qa_q1_game *g) {
    uint32_t count = 0;
    if (!io->reading)
        for (uint32_t i = 0; i < g->capacity; ++i)
            count += g->players[i] != NULL;
    Q1_SAVE(io, u32, count);
    if (count > g->capacity || (io->reading && count > (io->input.size - io->offset) / 13))
        return q1_save_fail(io, "Invalid Q1 checkpoint player count");
    uint32_t cursor = 0;
    for (uint32_t i = 0; i < count; ++i) {
        q1_player *player;
        if (io->reading) {
            player = allocate(io, 1, sizeof(*player));
            if (!player)
                return false;
            player->allocation_next = g->allocated_players;
            g->allocated_players = player;
        } else {
            while (cursor < g->capacity && !g->players[cursor])
                ++cursor;
            player = g->players[cursor++];
            if (!player->active)
                return q1_save_fail(io, "Inactive Q1 player in live slot");
        }
        if (!q1_save_player(io, player))
            return false;
        if (player->source_client)
            for (uint32_t j = 0; j < g->capacity; ++j) {
                const q1_player *other = g->players[j];
                if (other && other != player && other->source_client &&
                    other->client_slot == player->client_slot)
                    return q1_save_fail(io, "Duplicate Q1 source client slot continuation");
            }
        if (io->reading) {
            if (player->id.slot >= g->capacity || g->players[player->id.slot])
                return q1_save_fail(io, "Duplicate Q1 player continuation");
            g->players[player->id.slot] = player;
        }
    }
    return true;
}
static bool payload(q1_save_io *io, qa_q1_game *g) {
    if (!q1_save_runtime(io, g) || !q1_save_wire(io, g))
        return false;
    bool map = g->maps != NULL;
    Q1_SAVE(io, bool, map);
    if (map != (g->maps != NULL))
        return q1_save_fail(io, "Q1 checkpoint map services do not match the prepared provider");
    if (map && !q1_save_map_runtime(io, g->maps))
        return false;
    q1_door_group **index;
    size_t count;
    if (!groups(io, g, &index, &count))
        return false;
    bool ok = actors(io, g, index, count) && players(io, g) && q1_save_wire_validate(io, g);
    free(index);
    return ok;
}
bool qa_q1_game_capture(qa_q1_game *g, qa_buffer *out, qa_error *error) {
    if (!out || !boundary(g, false, error))
        return false;
    if (g->continuation_pending) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 restored bindings are not connected");
        return false;
    }
    q1_save_io body = {.game = g, .error = error};
    q1_save_io file = {.game = g, .error = error};
    bool ok = qa_strings_create(&body.dictionary, error) && payload(&body, g);
    if (ok) {
        uint8_t magic[sizeof(signature)];
        memcpy(magic, signature, sizeof(magic));
        uint32_t version = Q1_SAVE_VERSION;
        size_t dictionary_count = qa_strings_count(body.dictionary);
        uint32_t count = (uint32_t)dictionary_count;
        ok = dictionary_count <= UINT32_MAX && q1_save_bytes(&file, magic, sizeof(magic)) &&
             q1_save_u32(&file, &version) && q1_save_u32(&file, &count);
        for (uint32_t i = 0; ok && i < count; ++i) {
            qa_bytes word = qa_strings_text(body.dictionary, i + 1);
            uint32_t size = (uint32_t)word.size;
            ok = word.size <= UINT32_MAX && q1_save_u32(&file, &size) &&
                 q1_save_bytes(&file, (void *)word.data, word.size);
        }
        if (ok)
            ok = q1_save_bytes(&file, body.output.data, body.output.size);
    }
    qa_strings_destroy(body.dictionary);
    qa_buffer_free(&body.output);
    if (!ok) {
        qa_buffer_free(&file.output);
        return false;
    }
    *out = file.output;
    return true;
}
bool qa_q1_game_restore_prepare(qa_q1_game *g, qa_bytes bytes, qa_q1_restore **out,
                                qa_error *error) {
    if (!out || (bytes.size && !bytes.data) || !boundary(g, true, error))
        return false;
    if (g->continuation_pending) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 restoration is already pending");
        return false;
    }
    qa_q1_restore *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q1 restoration");
        return false;
    }
    ticket->destination = g;
    memcpy(&ticket->before, g, sizeof(*g));
    ticket->registry_revision = qa_actors_revision(qa_session_actors(g->services.session));
    ticket->wire_revision = g->wire ? g->wire->revision : 0;
    ticket->wire_generation = g->wire ? g->wire->generation : 0;
    if (g->maps)
        memcpy(&ticket->map_before, g->maps, sizeof(*g->maps));
    qa_q1_game *candidate = &ticket->candidate;
    *candidate = *g;
    candidate->actors = NULL;
    candidate->players = NULL;
    candidate->allocated_actors = candidate->spare_actors = candidate->retired_actors = NULL;
    candidate->allocated_players = candidate->spare_players = candidate->retired_players = NULL;
    candidate->maps = NULL;
    candidate->wire = NULL;
    q1_save_io io = {.game = candidate, .input = bytes, .error = error, .reading = true};
    qa_string_id *strings = NULL;
    candidate->actors = allocate(&io, g->capacity, sizeof(*g->actors));
    candidate->players = allocate(&io, g->capacity, sizeof(*g->players));
    if (!candidate->actors || !candidate->players)
        goto fail;
    if (g->maps) {
        candidate->maps = allocate(&io, 1, sizeof(*candidate->maps));
        if (!candidate->maps)
            goto fail;
        candidate->maps->options = g->maps->options;
    }
    uint8_t magic[sizeof(signature)];
    uint32_t version = 0, count = 0;
    if (!q1_save_bytes(&io, magic, sizeof(magic)) || memcmp(magic, signature, sizeof(magic)) ||
        !q1_save_u32(&io, &version) || version != Q1_SAVE_VERSION || !q1_save_u32(&io, &count) ||
        count == UINT32_MAX || count > (bytes.size - io.offset) / 4) {
        q1_save_fail(&io, "Invalid Q1 checkpoint header or dictionary");
        goto fail;
    }
    strings = allocate(&io, (size_t)count + 1, sizeof(*strings));
    if (!strings)
        goto fail;
    io.strings = strings;
    io.string_count = (size_t)count + 1;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t length = 0;
        if (!q1_save_u32(&io, &length) || length > bytes.size - io.offset) {
            q1_save_fail(&io, "Truncated Q1 checkpoint string");
            goto fail;
        }
        qa_bytes text = {bytes.data + io.offset, length};
        if (!qa_strings_intern(qa_session_strings(g->services.session), text, &strings[i + 1],
                               error))
            goto fail;
        io.offset += length;
    }
    if (!payload(&io, candidate))
        goto fail;
    if (candidate->maps)
        for (uint32_t i = 0; i < candidate->maps->frame_tick_count; ++i) {
            qa_actor_id id = candidate->maps->frame_ticks[i];
            q1_actor *entity = candidate->actors[id.slot];
            if (!entity || !qa_actor_id_equal(entity->id, id) || !entity->map ||
                (entity->map->kind != Q1_MAP_LIGHT_RAMP && entity->map->kind != Q1_MAP_ROPE &&
                 entity->map->kind != Q1_MAP_ADDON_BOB && entity->map->kind != Q1_MAP_ADDON_ROTATE)) {
                q1_save_fail(&io, "Q1 authored frame actor has no matching continuation");
                goto fail;
            }
        }
    if (io.offset != bytes.size) {
        q1_save_fail(&io, "Trailing bytes in Q1 checkpoint");
        goto fail;
    }
    free(strings);
    *out = ticket;
    return true;
fail:
    free(strings);
    qa_q1_game_restore_abort(ticket);
    return false;
}
bool qa_q1_game_restore_prepare_source(qa_q1_game *g, qa_bytes bytes, qa_q1_restore **out,
                                       qa_error *error) {
    if (!qa_q1_game_restore_prepare(g, bytes, out, error))
        return false;
    (*out)->candidate.continuation_pending = true;
    return true;
}
bool qa_q1_game_restore_finish(qa_q1_game *g, qa_error *error) {
    if (!boundary(g, false, error))
        return false;
    if (!g->continuation_pending) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 has no pending source restoration");
        return false;
    }
    for (uint32_t i = 0; i < g->capacity; ++i) {
        q1_player *player = g->players[i];
        if (player && player->weapon_definitions.serial &&
            (player->inventory_game != g ||
             !qa_inventory_lease_current(g->services.inventory, player->weapon_definitions))) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Q1 saved weapon definitions are missing");
            return false;
        }
    }
    for (uint32_t i = 0; i < g->capacity; ++i) {
        q1_actor *actor = g->actors[i];
        if (!actor)
            continue;
        if (actor->pickup_observation.serial &&
            !qa_pickups_observation_current(g->services.pickups, actor->pickup_observation)) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Q1 saved pickup observation is missing");
            return false;
        }
        qa_target_binding actual, expected;
        bool bound = g->maps &&
            qa_persistence_targets_binding(g->maps->options.targets, actor->id, &actual);
        if (actor->restored_target) {
            if (!bound || !qa_q1_game_target_binding(g, actor->id, &expected, error) ||
                !same_target(&actual, &expected)) {
                qa_error_set(error, QA_ERROR_FORMAT, i, "Q1 saved target declaration differs");
                return false;
            }
        } else if (bound && actual.context == g) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Q1 imported an unsaved target declaration");
            return false;
        }
    }
    g->continuation_pending = false;
    return true;
}
bool qa_q1_game_restore_validate(const qa_q1_restore *ticket, qa_error *error) {
    if (!ticket || !boundary(ticket->destination, true, error))
        return false;
    const qa_q1_game *g = ticket->destination;
    if (memcmp(g, &ticket->before, sizeof(*g)) ||
        (g->maps && memcmp(g->maps, &ticket->map_before, sizeof(*g->maps))) ||
        qa_actors_revision(qa_session_actors(g->services.session)) != ticket->registry_revision ||
        (g->wire && (g->wire->generation != ticket->wire_generation ||
                    g->wire->revision == UINT64_MAX || g->wire->revision != ticket->wire_revision))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 restoration candidate is stale");
        return false;
    }
    return true;
}
bool qa_q1_game_restore_commit(qa_q1_restore *ticket, qa_error *error) {
    if (!qa_q1_game_restore_validate(ticket, error))
        return false;
    qa_q1_game *g = ticket->destination;
    storage_free(g);
    *g = ticket->candidate;
    for (uint32_t i = 0; i < g->capacity; ++i)
        if (g->players[i] && g->players[i]->weapon_definitions.serial)
            g->players[i]->inventory_game = g;
    free(ticket);
    return true;
}
void qa_q1_game_restore_abort(qa_q1_restore *ticket) {
    if (!ticket)
        return;
    storage_free(&ticket->candidate);
    free(ticket);
}
