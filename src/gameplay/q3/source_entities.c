#include "map/internal.h"
#include "qa/game_q3_source.h"

q3_client_state *q3_client_at(qa_q3_game *game, uint32_t slot) {
    return game && slot < QA_Q3_NATIVE_CLIENTS ? &game->clients[slot] : NULL;
}
const q3_client_state *q3_client_const(const qa_q3_game *game, uint32_t slot) {
    return game && slot < QA_Q3_NATIVE_CLIENTS ? &game->clients[slot] : NULL;
}
const char *q3_client_name(const qa_q3_game *game, const q3_client_state *client) {
    return client->source_name ? (const char *)qa_strings_text(
        qa_session_strings(game->options.services.session), client->source_name).data : "";
}
void q3_client_name_bytes(const qa_q3_game *game, const q3_client_state *client,
    char out[QA_Q3_NATIVE_NETNAME]) {
    if (client->source_name) memcpy(out, q3_client_name(game, client), QA_Q3_NATIVE_NETNAME);
    else memset(out, 0, QA_Q3_NATIVE_NETNAME);
}
bool q3_client_name_store(qa_q3_game *game, q3_client_state *client,
    const char name[QA_Q3_NATIVE_NETNAME], qa_error *error) {
    return qa_strings_intern(qa_session_strings(game->options.services.session),
        (qa_bytes){(const uint8_t *)name, QA_Q3_NATIVE_NETNAME}, &client->source_name, error);
}
int32_t q3_client_ping_value(const q3_client_state *client) {
    return client->rule.has_followed_player ? client->rule.followed_player.ping
        : client->player ? client->player->ping : 0;
}
void q3_client_projection(const qa_q3_game *game, const q3_client_state *client,
    qa_q3_native_client *out) {
    *out = (qa_q3_native_client){.rule = client->rule, .ping = q3_client_ping_value(client)};
    q3_client_name_bytes(game, client, out->netname);
}
void q3_source_state_reset(qa_q3_game *game) {
    q3_wire_reset(game);
    game->new_session = false;
    game->fry_sound_index = 0;
    game->portal_sequence = 0;
    game->last_team_location_time = 0;
    game->client_counts = (qa_q3_source_client_counts){0};
    game->team_state = (qa_q3_source_team_state){0};
    game->match_state = (qa_q3_source_match_state){0};
    for (size_t i = 0; i < 3; ++i) game->podium_players[i] = QA_Q3_SOURCE_NONE;
    memset(game->source_actor_ran, 0, sizeof(game->source_actor_ran));
    memset(game->death_continuations, 0, sizeof(game->death_continuations));
    memset(game->current_origins, 0, sizeof(game->current_origins));
    memset(game->clients, 0, sizeof(game->clients));
    memset(game->client_actors, 0, sizeof(game->client_actors));
    for (uint32_t i = 0; i < QA_Q3_NATIVE_CLIENTS; ++i) {
        game->client_actors[i] = (q3_actor){.kind = Q3_ACTOR_PLAYER, .alpha = 1};
        game->clients[i].rule.source_model_shape = QA_SHAPE_BOX;
    }
    memset(game->source_entities, 0, sizeof(game->source_entities));
    for (uint32_t i = 0; i < QA_Q3_SOURCE_ENTITIES; ++i)
        game->source_entities[i].client_slot = i < game->options.max_clients ? (int32_t)i : -1;
    memset(game->source_numbers, 0xff, game->capacity * sizeof(*game->source_numbers));
    game->source_count = QA_Q3_SOURCE_CLIENTS;
}
bool qa_q3_source_entity_count(const qa_q3_game *game, uint32_t *out, qa_error *error) {
    if (!game || !out) return q3_fail(error, "Q3 entity count needs its actual source");
    *out = game->source_count;
    return true;
}
bool qa_q3_source_max_clients(const qa_q3_game *game, uint32_t *out, qa_error *error) {
    if (!game || !out) return q3_fail(error, "Q3 client capacity needs its actual source");
    *out = game->options.max_clients;
    return true;
}
bool qa_q3_source_new_session_read(const qa_q3_game *game, bool *out, qa_error *error) {
    if (!game || !out) return q3_fail(error, "Q3 newSession observation needs its source level");
    *out = game->new_session;
    return true;
}
bool qa_q3_source_new_session_set(qa_q3_game *game, bool value, qa_error *error) {
    if (!game || game->source_restored)
        return q3_fail(error, "Q3 newSession mutation needs its source level");
    game->new_session = value;
    return true;
}
bool qa_q3_source_team_location_time_read(const qa_q3_game *game, int32_t *out,
                                          qa_error *error) {
    if (!game || !out) return q3_fail(error, "Q3 team location time needs its source level");
    *out = game->last_team_location_time;
    return true;
}
bool qa_q3_source_team_location_time_set(qa_q3_game *game, int32_t value,
                                         qa_error *error) {
    if (!game || game->source_restored)
        return q3_fail(error, "Q3 team location time mutation needs its source level");
    game->last_team_location_time = value;
    return true;
}
bool q3_client_counts_valid(const qa_q3_source_client_counts *value, uint32_t max_clients) {
    if (!value || !max_clients || max_clients > QA_Q3_NATIVE_CLIENTS) return false;
    const int32_t counts[] = {value->num_connected, value->num_non_spectator,
        value->num_playing, value->num_voting, value->num_team_voting[0], value->num_team_voting[1]};
    for (size_t i = 0; i < sizeof(counts) / sizeof(*counts); ++i)
        if (counts[i] < 0 || counts[i] > (int32_t)max_clients) return false;
    if (value->num_voting > value->num_playing ||
        value->num_playing > value->num_non_spectator ||
        value->num_non_spectator > value->num_connected ||
        value->num_team_voting[0] + value->num_team_voting[1] > value->num_voting)
        return false;
    if (value->follow1 < -1 || value->follow1 >= (int32_t)QA_Q3_NATIVE_CLIENTS ||
        value->follow2 < -1 || value->follow2 >= (int32_t)QA_Q3_NATIVE_CLIENTS) return false;
    uint64_t seen = 0;
    for (uint32_t i = 0; i < QA_Q3_NATIVE_CLIENTS; ++i) {
        uint32_t slot = value->sorted_clients[i];
        if (slot >= QA_Q3_NATIVE_CLIENTS) return false;
        if (i < (uint32_t)value->num_connected) {
            if (slot >= max_clients || (seen & (UINT64_C(1) << slot))) return false;
            seen |= UINT64_C(1) << slot;
        }
    }
    return true;
}
bool qa_q3_source_client_counts_read(const qa_q3_game *game,
    qa_q3_source_client_counts *out, qa_error *error) {
    if (!game || !out) return q3_fail(error, "Q3 rank counts need their source level");
    *out = game->client_counts;
    return true;
}
bool qa_q3_source_match_context_read(const qa_q3_game *game, qa_q3_product *product,
                                     int32_t *start, qa_error *error) {
    if (!game || !game->map || !product || !start)
        return q3_fail(error, "Q3 match context needs its installed source map");
    *product = game->options.product;
    *start = game->map->options.start_time_ms;
    return true;
}
bool qa_q3_source_client_counts_write(qa_q3_game *game,
    const qa_q3_source_client_counts *value, qa_error *error) {
    if (!game || game->source_restored || !q3_client_counts_valid(value, game->options.max_clients))
        return q3_fail(error, "Q3 rank counts exceed their source clients");
    game->client_counts = *value;
    return true;
}
bool qa_q3_source_spawnflags_read(const qa_q3_game *game, qa_actor_id actor,
                                  int32_t *out, qa_error *error) {
    uint32_t slot;
    if (!out || !qa_q3_source_actor_slot(game, actor, &slot, error)) return false;
    const qa_q3_map_actor_state *map = q3_map_const(game, actor);
    if (map) {
        memcpy(out, &map->spawnflags, sizeof(*out));
        return true;
    }
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry) return q3_fail(error, "Q3 dynamic spawnflags have no source entity owner");
    *out = entry->spawnflags;
    return true;
}
bool qa_q3_source_binding_read(const qa_q3_game *game, uint32_t slot,
    qa_q3_source_binding *out, qa_error *error) {
    if (!game || !out || slot >= QA_Q3_SOURCE_ENTITIES)
        return q3_fail(error, "Q3 entity observation exceeds the physical source rows");
    *out = game->source_entities[slot];
    return true;
}
bool qa_q3_source_classname_read(const qa_q3_game *game, uint32_t slot,
                                 const char **out, qa_error *error) {
    if (!game || !out || slot >= QA_Q3_SOURCE_ENTITIES)
        return q3_fail(error, "Q3 classname observation exceeds the source rows");
    const qa_q3_source_binding *binding = &game->source_entities[slot];
    if (binding->actor.registry && !qa_actors_get(
            qa_session_actors(game->options.services.session), binding->actor))
        return q3_fail(error, "Q3 classname has a stale source generation");
    const q3_actor *entry = q3_actor_const(game, binding->actor);
    if (entry && entry->kind == Q3_ACTOR_VICTORY_MODEL) {
        if (binding->client_slot < 0 || binding->client_slot >= (int32_t)QA_Q3_NATIVE_CLIENTS)
            return q3_fail(error, "Q3 victory classname lost its borrowed client pointer");
        *out = q3_client_name(game, &game->clients[binding->client_slot]);
        return true;
    }
    *out = qa_strings_cstr(qa_session_strings(game->options.services.session),
                          binding->classname);
    return true;
}
bool qa_q3_source_actor_slot(const qa_q3_game *game, qa_actor_id actor,
    uint32_t *out, qa_error *error) {
    if (!game || !out || actor.slot >= game->capacity ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return q3_fail(error, "Q3 source entity has no current actor");
    uint32_t slot = game->source_numbers[actor.slot];
    if (slot >= QA_Q3_SOURCE_ENTITIES ||
        !qa_actor_id_equal(game->source_entities[slot].actor, actor))
        return q3_fail(error, "Actor has no physical Q3 source row");
    *out = slot;
    return true;
}
bool qa_q3_native_client_slot(const qa_q3_game *game, qa_actor_id actor,
    uint32_t *out, qa_error *error) {
    uint32_t slot;
    if (!qa_q3_source_actor_slot(game, actor, &slot, error)) return false;
    if (slot >= game->options.max_clients)
        return q3_fail(error, "Actor is outside the configured Q3 source clients");
    *out = slot;
    return true;
}
static bool bind(qa_q3_game *game, uint32_t slot, qa_actor_id actor, qa_error *error) {
    if (!game || slot >= QA_Q3_SOURCE_NONE || actor.slot >= game->capacity ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return q3_fail(error, "Q3 physical row admission needs a current canonical actor");
    qa_q3_source_binding *row = &game->source_entities[slot];
    if (qa_actor_id_equal(row->actor, actor)) return true;
    if (row->actor.registry || game->source_numbers[actor.slot] != UINT16_MAX)
        return q3_fail(error, "Q3 physical row or actor is already admitted");
    *row = slot < QA_Q3_SOURCE_CLIENTS
        ? (qa_q3_source_binding){.actor = actor, .server_flags = row->server_flags,
            .number = row->number, .owner_number = row->owner_number,
            .classname = row->classname, .in_use = row->in_use,
            .client_slot = (int32_t)slot, .body_attached = true}
        : (qa_q3_source_binding){.actor = actor, .number = (int32_t)slot,
            .owner_number = QA_Q3_SOURCE_NONE, .in_use = true,
            .free_time_ms = row->free_time_ms, .classname = game->source_noclass,
            .server_flags = row->server_flags, .never_free = row->never_free,
            .client_slot = row->client_slot, .body_attached = true};
    game->source_numbers[actor.slot] = (uint16_t)slot;
    game->source_actor_ran[slot] = false;
    if (slot < QA_Q3_SOURCE_CLIENTS)
        game->clients[slot].player = qa_actors_player(
            qa_session_actors(game->options.services.session), actor);
    q3_wire_bind(game, slot, actor);
    return true;
}
bool q3_source_client_pointer(const qa_q3_game *game, qa_actor_id actor, uint32_t *out) {
    uint32_t slot;
    if (!out || !qa_q3_source_actor_slot(game, actor, &slot, NULL)) return false;
    int32_t client = game->source_entities[slot].client_slot;
    if (client < 0 || client >= (int32_t)QA_Q3_SOURCE_CLIENTS) return false;
    *out = (uint32_t)client;
    return true;
}
int32_t q3_source_team(qa_q3_game *game, qa_actor_id actor) {
    uint32_t client;
    if (q3_source_client_pointer(game, actor, &client)) return game->clients[client].rule.session.team;
    return game->options.hooks.source_team
        ? game->options.hooks.source_team(game->options.hooks.context, actor) : 0;
}
static bool source_components_create(qa_q3_game *game, qa_actor_id actor,
                                      qa_error *error) {
    const qa_builtin_services *services = &game->options.services;
    qa_combat_state combat = {.mass = 200, .armor.regular =
        {.kind = QA_ARMOR_Q3, .protection.q3_protection = 0.66f}};
    return qa_world_body_create(services->world, actor, &(qa_body_state){0}, error) &&
        qa_combat_create_actor(services->combat, actor, &combat, error) &&
        qa_inventory_create_actor(services->inventory, actor, NULL, 0, error);
}
bool q3_source_client_body_ensure(qa_q3_game *game, uint32_t slot,
                                 qa_actor_id *out, qa_error *error) {
    if (!game || !out || slot >= game->options.max_clients || game->source_restored)
        return q3_fail(error, "Q3 PS body write exceeds its fixed clients");
    qa_q3_source_binding *row = &game->source_entities[slot];
    qa_actor_id actor = row->actor;
    if (!actor.registry) {
        qa_string_id definition;
        if (!qa_builtin_resource(&game->options.services, "q3:player", &definition, error) ||
            !qa_session_allocate(game->options.services.session, game->options.owner,
                definition, true, slot, &actor, error)) return false;
        if (!bind(game, slot, actor, error)) return q3_rollback_spawn(game, actor, error);
        if (!source_components_create(game, actor, error))
            return q3_rollback_spawn(game, actor, error);
        game->client_actors[slot].actor = actor;
    } else if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return q3_fail(error, "Q3 PS body write found a stale fixed actor");
    if (!row->body_attached &&
        !qa_world_body_write(game->options.services.world, actor, &(qa_body_state){0}, error))
        return false;
    if (!qa_actor_id_equal(game->source_entities[slot].actor, actor))
        return q3_fail(error, "Q3 fixed PS body changed during allocation");
    game->source_entities[slot].body_attached = true;
    *out = actor;
    return true;
}
bool q3_source_row_body_ensure(qa_q3_game *game, uint32_t slot,
                               qa_actor_id *out, qa_error *error) {
    if (!game || !out || game->source_restored || slot < QA_Q3_SOURCE_CLIENTS ||
        slot >= game->source_count)
        return q3_fail(error, "Q3 retained row body write exceeds its source extent");
    qa_q3_source_binding saved = game->source_entities[slot];
    q3_wire_entity_source source = *q3_wire_entity_slot(game, slot);
    if (saved.actor.registry) {
        if (!qa_actors_get(qa_session_actors(game->options.services.session), saved.actor))
            return q3_fail(error, "Q3 retained row body write has a stale physical actor");
        *out = saved.actor;
        return true;
    }
    qa_string_id definition;
    qa_actor_id actor;
    if (!qa_builtin_resource(&game->options.services, "q3:entity", &definition, error) ||
        !qa_session_allocate(game->options.services.session, game->options.owner,
            definition, true, slot, &actor, error))
        return false;
    if (!bind(game, slot, actor, error)) return q3_rollback_spawn(game, actor, error);
    *q3_wire_entity_slot(game, slot) = source;
    game->source_entities[slot] = saved;
    game->source_entities[slot].actor = actor;
    game->source_entities[slot].body_attached = true;
    if (!source_components_create(game, actor, error) ||
        !q3_wire_entity_ready(game, actor, error))
        return q3_rollback_spawn(game, actor, error);
    *out = actor;
    return true;
}
bool qa_q3_source_physics_object_read(const qa_q3_game *game, uint32_t slot,
                                      bool *out, qa_error *error) {
    if (!game || !out || slot >= QA_Q3_SOURCE_NONE || game->source_restored)
        return q3_fail(error, "Q3 physicsObject observation exceeds its actual source rows");
    const qa_q3_source_binding *binding = &game->source_entities[slot];
    if (binding->actor.registry && !qa_actors_get(
            qa_session_actors(game->options.services.session), binding->actor))
        return q3_fail(error, "Q3 physicsObject observation has a stale source generation");
    const q3_actor *entry = q3_actor_const(game, binding->actor);
    *out = entry && ((entry->kind == Q3_ACTOR_CORPSE && entry->state.corpse.physics_object) ||
        ((entry->kind == Q3_ACTOR_PODIUM || entry->kind == Q3_ACTOR_VICTORY_MODEL) &&
         entry->state.postgame.physics_object));
    return true;
}
bool qa_q3_source_dropped_read(const qa_q3_game *game, uint32_t slot,
                               bool *out, qa_error *error) {
    if (!game || !out || slot >= QA_Q3_SOURCE_NONE || game->source_restored)
        return q3_fail(error, "Q3 dropped flag read exceeds its physical source rows");
    const qa_q3_source_binding *binding = &game->source_entities[slot];
    if (binding->actor.registry && !qa_actors_get(
            qa_session_actors(game->options.services.session), binding->actor))
        return q3_fail(error, "Q3 dropped flag read has a stale source generation");
    const q3_actor *entry = q3_actor_const(game, binding->actor);
    *out = entry && entry->kind == Q3_ACTOR_ITEM && entry->state.item.spawn.dropped;
    return true;
}
bool qa_q3_source_bind_client(qa_q3_game *game, uint32_t slot,
    qa_actor_id actor, qa_error *error) {
    if (!game || game->source_restored || slot >= game->options.max_clients)
        return q3_fail(error, "Q3 client admission exceeds actual source capacity");
    q3_actor *entry = q3_actor_get(game, actor);
    if (entry && entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "Q3 source client actor has another native kind");
    if (!bind(game, slot, actor, error)) return false;
    game->source_entities[slot].client_slot = (int32_t)slot;
    game->source_entities[slot].body_attached = true;
    q3_actor *client = &game->client_actors[slot];
    if (entry && entry != client) {
        qa_actor_id enemy = client->enemy;
        uint32_t enemy_source_slot = client->enemy_source_slot;
        bool enemy_source_present = client->enemy_source_present;
        bool force_gesture = client->force_gesture;
        *client = *entry;
        client->enemy = enemy;
        client->enemy_source_slot = enemy_source_slot;
        client->enemy_source_present = enemy_source_present;
        client->force_gesture = force_gesture;
        *entry = (q3_actor){0};
    } else if (!entry) client->state.player.selections = 0;
    client->actor = actor;
    return true;
}
bool qa_q3_source_bind_world(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game || game->source_restored)
        return q3_fail(error, "Q3 world admission conflicts with source restoration");
    return bind(game, QA_Q3_SOURCE_WORLD, actor, error);
}
void q3_source_actor_released(qa_q3_game *game, qa_actor_id actor) {
    if (!game || actor.slot >= game->capacity) return;
    uint32_t slot = game->source_numbers[actor.slot];
    if (slot >= QA_Q3_SOURCE_ENTITIES ||
        !qa_actor_id_equal(game->source_entities[slot].actor, actor)) return;
    uint32_t flags = game->source_entities[slot].server_flags;
    int32_t number = game->source_entities[slot].number;
    int32_t owner = game->source_entities[slot].owner_number;
    qa_string_id classname = game->source_entities[slot].classname;
    int32_t client_slot = game->source_entities[slot].client_slot;
    q3_wire_release(game, slot, actor);
    if (slot < QA_Q3_SOURCE_CLIENTS) {
        game->death_continuations[slot] = (q3_death_continuation){0};
        game->current_origins[slot] = (q3_current_origin){0};
        game->clients[slot].player = NULL;
    }
    game->source_entities[slot] = (qa_q3_source_binding){
        .free_time_ms = slot < QA_Q3_SOURCE_CLIENTS ? 0 : game->now_ms,
        .server_flags = slot < QA_Q3_SOURCE_CLIENTS ? flags : 0,
        .number = slot < QA_Q3_SOURCE_CLIENTS ? number : 0,
        .owner_number = slot < QA_Q3_SOURCE_CLIENTS ? owner : 0,
        .classname = slot < QA_Q3_SOURCE_CLIENTS ? classname : game->source_freed,
        .client_slot = slot < QA_Q3_SOURCE_CLIENTS ? client_slot : -1};
    game->source_numbers[actor.slot] = UINT16_MAX;
    if (slot < QA_Q3_SOURCE_CLIENTS) game->client_actors[slot].actor = (qa_actor_id){0};
}

bool q3_source_level_init(qa_q3_game *game, qa_error *error) {
    qa_string_id world_name, body_name;
    char start[32];
    snprintf(start, sizeof(start), "%d", game->map->options.start_time_ms);
    if (!qa_q3_configstring_write(game, 20, "baseq3-1", error) ||
        !qa_q3_configstring_write(game, 21, start, error) ||
        !qa_builtin_resource(&game->options.services, "worldspawn", &world_name, error) ||
        !qa_builtin_resource(&game->options.services, "bodyque", &body_name, error)) return false;
    qa_actor_id world;
    if (!qa_session_allocate(game->options.services.session, game->options.owner,
            world_name, true, QA_Q3_SOURCE_WORLD, &world, error)) return false;
    if (!bind(game, QA_Q3_SOURCE_WORLD, world, error) ||
        !qa_world_body_create(game->options.services.world, world,
                              &(qa_body_state){0}, error))
        return q3_rollback_spawn(game, world, error);
    game->source_entities[QA_Q3_SOURCE_WORLD].classname = world_name;
    if (!q3_wire_entity_ready(game, world, error) ||
        !qa_q3_sound_index(game, "sound/player/fry.wav", &game->fry_sound_index, error) ||
        (game->options.hooks.source_world_init &&
         !game->options.hooks.source_world_init(game->options.hooks.context, error))) return false;
    for (size_t i = 0; i < 8; ++i) {
        qa_actor_id actor;
        if (!q3_spawn_actor(game, &(qa_builtin_spawn){.owner = game->options.owner,
                .definition = body_name}, &actor, error)) return false;
        uint32_t slot = game->source_numbers[actor.slot];
        game->source_entities[slot].never_free = true;
        game->actors[actor.slot] = (q3_actor){.actor = actor,
            .kind = Q3_ACTOR_CORPSE, .alpha = 1};
        game->body_queue[i] = actor;
        if (!q3_wire_entity_ready(game, actor, error)) return false;
    }
    return true;
}
static bool allocate_slot(qa_q3_game *game, uint32_t *out, qa_error *error) {
    int32_t start = game->map ? game->map->options.start_time_ms : 0;
    for (uint32_t slot = QA_Q3_SOURCE_CLIENTS; slot < game->source_count; ++slot) {
        const qa_q3_source_binding *row = &game->source_entities[slot];
        if (row->in_use) continue;
        if (row->free_time_ms > q3_add_time(start, 2000) &&
            q3_sub_time(game->now_ms, row->free_time_ms) < 1000) continue;
        *out = slot;
        return true;
    }
    if (game->source_count == QA_Q3_SOURCE_WORLD) {
        if (!game->options.hooks.console_print || game->observation_depth == SIZE_MAX)
            return q3_fail(error, "G_Spawn exhaustion requires its actual console print service");
        ++game->observation_depth;
        bool ok = true;
        for (uint32_t slot = 0; ok && slot < QA_Q3_SOURCE_ENTITIES; ++slot) {
            const char *classname;
            char text[32000];
            ok = qa_q3_source_classname_read(game, slot, &classname, error);
            if (ok) {
                int length = snprintf(text, sizeof(text), "%4u: %s\n", slot,
                                      classname ? classname : "(null)");
                if (length < 0 || (size_t)length >= sizeof(text))
                    ok = q3_fail(error, "game format exceeds the 32000-byte Com_sprintf buffer");
                else ok = game->options.hooks.console_print(game->options.hooks.context, text, error);
            }
        }
        --game->observation_depth;
        if (!ok) return false;
        return q3_fail(error, "G_Spawn: no free entities");
    }
    *out = game->source_count++;
    return true;
}
bool q3_spawn_raw_actor(qa_q3_game *game, qa_string_id definition,
                        qa_actor_id *out, qa_error *error) {
    if (!game || !out || game->source_restored)
        return q3_fail(error, "Q3 raw entity allocation needs its actual source owner");
    uint32_t slot;
    if (!allocate_slot(game, &slot, error)) return false;
    qa_actor_id actor = game->source_entities[slot].actor;
    if (!actor.registry) {
        if (!qa_session_allocate(game->options.services.session, game->options.owner,
                definition, true, slot, &actor, error)) return false;
        if (!bind(game, slot, actor, error)) return q3_rollback_spawn(game, actor, error);
        if (!source_components_create(game, actor, error))
            return q3_rollback_spawn(game, actor, error);
    } else if (!qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
               game->source_numbers[actor.slot] != slot)
        return q3_fail(error, "Q3 raw source row has a stale physical actor");
    qa_q3_source_binding *row = &game->source_entities[slot];
    row->in_use = true;
    row->number = (int32_t)slot;
    row->owner_number = QA_Q3_SOURCE_NONE;
    row->classname = game->source_noclass;
    if (!q3_wire_entity_ready(game, actor, error)) return false;
    *out = actor;
    return true;
}
bool q3_spawn_actor(qa_q3_game *game, const qa_builtin_spawn *input,
    qa_actor_id *out, qa_error *error) {
    if (!game || !input || !out || input->owner != game->options.owner ||
        (input->inventory_count && !input->inventory))
        return q3_fail(error, "Q3 entity allocation needs its actual source owner");
    qa_builtin_spawn spawn = *input;
    qa_actor_id actor;
    if (!q3_spawn_raw_actor(game, spawn.definition, &actor, error)) return false;
    uint32_t slot = game->source_numbers[actor.slot];
    if (spawn.definition) game->source_entities[slot].classname = spawn.definition;
    if (spawn.collision && qa_actor_reference_present(spawn.collision->owner))
        game->source_entities[slot].owner_number = spawn.collision->owner.kind == QA_ACTOR_REFERENCE_SOURCE &&
            spawn.collision->owner.value.source.owner == game->options.owner ? (int32_t)spawn.collision->owner.value.source.slot :
            q3_entity_number(game, qa_actor_reference_resolve(qa_session_actors(game->options.services.session), spawn.collision->owner));
    const qa_builtin_services *s = &game->options.services;
    for (size_t i = 0; i < spawn.inventory_count; ++i)
        if (!qa_inventory_configure(s->inventory, actor, &spawn.inventory[i], NULL, NULL, error))
            return q3_rollback_spawn(game, actor, error);
    if (!qa_world_body_write(s->world, actor, &spawn.body, error) ||
        (spawn.collision && !qa_world_set_collision(s->world, actor, spawn.collision, error)) ||
        (spawn.combat && (!qa_combat_set_health(s->combat, actor, spawn.combat->health, error) ||
            !qa_combat_set_armor(s->combat, actor, &spawn.combat->armor, error) ||
            !qa_combat_set_traits(s->combat, actor, spawn.combat, error))) ||
        (spawn.link && !qa_q3_wire_link(game, actor, NULL, error)))
        return q3_rollback_spawn(game, actor, error);
    *out = actor;
    return true;
}
bool q3_source_prepare(qa_q3_game *game, const qa_q3_checkpoint *saved,
    uint16_t **out, q3_client_names *names, qa_error *error) {
    if (saved->source_count < QA_Q3_SOURCE_CLIENTS || saved->source_count > QA_Q3_SOURCE_WORLD)
        return q3_fail(error, "Invalid Q3 physical entity count");
    uint16_t *numbers = malloc(game->capacity * sizeof(*numbers));
    if (!numbers) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 physical source index");
        return false;
    }
    memset(numbers, 0xff, game->capacity * sizeof(*numbers));
    for (uint32_t slot = 0; slot < QA_Q3_SOURCE_ENTITIES; ++slot) {
        const qa_q3_source_binding *row = &saved->source_entities[slot];
        if (row->client_slot < -1 || row->client_slot >= (int32_t)QA_Q3_SOURCE_CLIENTS ||
            (slot < QA_Q3_SOURCE_CLIENTS && row->client_slot !=
                (slot < saved->max_clients ? (int32_t)slot : -1)) ||
            (row->body_attached && !row->actor.registry) ||
            (row->in_use && !row->body_attached)) goto invalid;
        if (slot >= saved->source_count && slot != QA_Q3_SOURCE_WORLD &&
            (row->actor.registry || row->free_time_ms || row->server_flags || row->number ||
             row->owner_number || row->classname || row->in_use || row->never_free ||
             row->client_slot != -1 || row->body_attached)) goto invalid;
        if (row->classname && !qa_strings_cstr(
                qa_session_strings(game->options.services.session), row->classname)) goto invalid;
        if (slot < QA_Q3_SOURCE_CLIENTS && row->never_free) goto invalid;
        if (!row->actor.registry) {
            if (row->actor.generation || row->actor.slot || row->in_use || row->never_free) goto invalid;
            continue;
        }
        const qa_actor_record *record = qa_actors_get(
            qa_session_actors(game->options.services.session), row->actor);
        if (!record || row->actor.slot >= game->capacity ||
            numbers[row->actor.slot] != UINT16_MAX ||
            (slot < QA_Q3_SOURCE_CLIENTS && slot >= saved->max_clients) ||
            (slot >= QA_Q3_SOURCE_CLIENTS && slot < QA_Q3_SOURCE_WORLD &&
             (record->owner != game->options.owner || !record->has_source ||
              record->source_slot != slot)) ||
            (slot == QA_Q3_SOURCE_WORLD &&
             (record->owner != game->options.owner || !record->has_source ||
              record->source_slot != slot))) goto invalid;
        numbers[row->actor.slot] = (uint16_t)slot;
    }
    for (uint32_t slot = 0; slot < QA_Q3_NATIVE_CLIENTS; ++slot) {
        const qa_q3_native_client *client = &saved->clients[slot];
        const q3_actor *actor = &saved->source_clients[slot];
        if (client->rule.connected < QA_Q3_CLIENT_DISCONNECTED ||
            client->rule.connected > QA_Q3_CLIENT_CONNECTED ||
            client->rule.team_state < 0 || client->rule.team_state > 1 ||
            !memchr(client->netname, 0, sizeof(client->netname)) ||
            (client->rule.source_model_shape != QA_SHAPE_BOX &&
             client->rule.source_model_shape != QA_SHAPE_CAPSULE) ||
            !isfinite(client->rule.team.last_hurt_carrier_ms) ||
            !isfinite(client->rule.team.last_returned_flag_ms) ||
            !isfinite(client->rule.team.flag_since_ms) ||
            !isfinite(client->rule.team.last_fragged_carrier_ms) ||
            actor->kind != Q3_ACTOR_PLAYER || !isfinite(actor->alpha) ||
            !qa_actor_id_equal(actor->actor, saved->source_entities[slot].actor) ||
            !q3_player_state_valid_source_client(&actor->state.player, &client->rule)) goto invalid;
    }
    qa_strings *strings = qa_session_strings(game->options.services.session);
    for (uint32_t slot = 0; slot < QA_Q3_NATIVE_CLIENTS; ++slot) {
        const char *name = saved->clients[slot].netname;
        if (!qa_strings_intern(strings, (qa_bytes){(const uint8_t *)name, QA_Q3_NATIVE_NETNAME},
                &names->source[slot], error) ||
            !qa_strings_intern_cstr(strings, name, &names->player[slot], error)) {
            free(numbers);
            return false;
        }
    }
    *out = numbers;
    return true;
invalid:
    free(numbers);
    return q3_fail(error, "Invalid Q3 physical entity or retained client continuation");
}
void q3_source_commit(qa_q3_game *game, const qa_q3_checkpoint *saved,
    uint16_t *numbers, const q3_client_names *names) {
    free(game->source_numbers);
    game->source_numbers = numbers;
    game->source_count = saved->source_count;
    memcpy(game->source_entities, saved->source_entities, sizeof(game->source_entities));
    for (uint32_t slot = 0; slot < QA_Q3_NATIVE_CLIENTS; ++slot) {
        qa_actor_player *player = qa_actors_player(qa_session_actors(game->options.services.session),
            saved->source_entities[slot].actor);
        game->clients[slot] = (q3_client_state){.rule = saved->clients[slot].rule,
            .source_name = names->source[slot], .player = player};
        if (player && !player->present) {
            player->name = names->player[slot];
            player->ping = saved->clients[slot].ping;
            player->bot = (saved->source_entities[slot].server_flags & 8u) != 0;
            player->spectator = saved->clients[slot].rule.session.team == 3;
            player->present = true;
        }
    }
    memcpy(game->client_actors, saved->source_clients, sizeof(game->client_actors));
}
