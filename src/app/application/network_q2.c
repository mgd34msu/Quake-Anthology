#include "network_q2_private.h"
#include <math.h>
#include <stdio.h>

char *application_network_q2_copy(const char *text, qa_error *error)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (!copy) { application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 source text"); return NULL; }
    memcpy(copy, text, size);
    return copy;
}

application_provider *application_network_q2_provider(qa_application_network_q2 *owner)
{
    application_provider *provider = owner ? application_world_provider(owner->app, QA_ROLE_ENTITIES, "") : NULL;
    return provider && provider->owner == owner->host.source.source_owner ? provider : NULL;
}

bool qa_application_network_q2_host_source(qa_application *app, qa_net_protocol_id protocol,
    qa_application_network_q2_host *out, qa_error *error)
{
    qa_q2_codec codec;
    qa_application_network_q2_host value = {.protocol = protocol};
    bool found = false;
    if (!out || !qa_q2_codec_init(&codec, protocol, error) ||
        !(app && app->operation == APPLICATION_PERSISTING ?
            qa_application_native_q2_presentation_retained_selected(app, &value.source, &found, error) :
            qa_application_native_q2_presentation_selected(app, &value.source, &found, error))) return false;
    if (!found || protocol.kind == QA_NET_Q2KEX_DEMO_2022)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q2 wire requires a physical live Q2 GAME");
    bool rerelease = protocol.kind == QA_NET_Q2REPRO_1038 || protocol.kind == QA_NET_Q2KEX_2023 ||
        protocol.kind == QA_NET_Q2PRIVATE_4038;
    if (rerelease != (value.source.edition == QA_Q2_RERELEASE))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q2 wire dialect differs from its physical GAME API");
    application_provider *provider = application_world_provider(app, QA_ROLE_ENTITIES, "");
    value.content = value.source.content;
    if (value.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN) {
        value.cvars = application_native_q2_console_registry(provider);
        if (!qa_q2_wire_policy(value.source.source.game, &value.entity_slots, &value.client_slots, error)) return false;
    } else {
        struct application_native_q2 *engine = provider->state.native.q2_engine;
        qa_native_entity_table table;
        const qa_cvar_view *clients = engine ? qa_cvars_find(engine->cvars, "maxclients") : NULL;
        if (!engine || !clients || clients->integer < 1 || clients->integer > 256 ||
            !qa_native_entity_table_get(qa_native_host_instance(provider->state.native.host), &table, error))
            return application_fail(error, QA_ERROR_FORMAT, "Original Q2 wire lost its source client/entity policy");
        value.cvars = engine->cvars; value.client_slots = (uint32_t)clients->integer;
        value.entity_slots = table.capacity;
    }
    if (!value.cvars || !value.content || value.entity_slots <= value.client_slots || value.entity_slots > 65536)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 publication has no representable source namespace");
    *out = value;
    return true;
}

bool application_network_q2_current(qa_application_network_q2 *owner, qa_error *error)
{
    qa_application_network_q2_host actual;
    if (!owner || !qa_application_network_q2_host_source(owner->app, owner->host.protocol, &actual, error)) return false;
    const qa_application_native_q2_presentation *saved = &owner->host.source, *now = &actual.source;
    bool same = saved->session == now->session && saved->publication == now->publication &&
        saved->launch == now->launch && saved->content == now->content &&
        saved->source_owner == now->source_owner && saved->content_product == now->content_product &&
        saved->edition == now->edition && saved->kind == now->kind &&
        saved->publication_generation == now->publication_generation && saved->map_revision == now->map_revision &&
        owner->host.cvars == actual.cvars && owner->host.client_slots == actual.client_slots &&
        owner->host.entity_slots == actual.entity_slots && qa_sha256_equal(&owner->identity, &now->launch->identity);
    if (same) same = now->kind == QA_APPLICATION_NATIVE_Q2_BUILTIN ?
        saved->source.game == now->source.game : saved->source.original.host == now->source.original.host;
    if (!same) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 publication belongs to a retired physical GAME");
    owner->host = actual;
    return true;
}

bool application_network_q2_config(qa_application_network_q2 *owner, uint32_t index,
    const char *text, qa_error *error)
{
    if (!owner || index >= owner->config_count || !text || strlen(text) >= 2048)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 source configstring leaves its wire table");
    const char *old = owner->configs[index];
    if ((!old && !*text) || (old && !strcmp(old, text))) return true;
    char *copy = *text ? application_network_q2_copy(text, error) : NULL;
    if (*text && !copy) return false;
    free(owner->configs[index]); owner->configs[index] = copy;
    return true;
}

bool application_network_q2_layout(qa_application_network_q2 *owner, qa_error *error)
{
    bool rr = owner->host.protocol.kind == QA_NET_Q2REPRO_1038 || owner->host.protocol.kind == QA_NET_Q2KEX_2023 ||
        owner->host.protocol.kind == QA_NET_Q2PRIVATE_4038;
    owner->config_count = rr ? 12448 : 2080;
    owner->item_base = rr ? 11326 : 1056; owner->skin_base = rr ? 11582 : 1312;
    owner->light_base = rr ? 10814 : 800; owner->checksum_index = rr ? 61 : 31;
    owner->clients_index = rr ? 60 : 30; owner->air_index = rr ? 59 : 29;
    owner->n64_index = rr ? 12103 : UINT32_MAX;
    const uint32_t bases[] = {rr ? 62u : 32u, rr ? 8254u : 288u, rr ? 10302u : 544u};
    const uint32_t maximum[] = {rr ? 8192u : 256u, rr ? 2048u : 256u, rr ? 512u : 256u};
    owner->configs = calloc(owner->config_count, sizeof(*owner->configs));
    owner->entries = calloc(owner->config_count, sizeof(*owner->entries));
    if (!owner->configs || !owner->entries) return application_fail(error, QA_ERROR_MEMORY, "Allocating Q2 source configstrings");
    for (unsigned i = 0; i < 3; ++i) {
        owner->resources[i] = (application_q2_resource_table){.base = bases[i], .maximum = maximum[i]};
        owner->resources[i].paths = calloc(maximum[i], sizeof(char *));
        if (!owner->resources[i].paths) return application_fail(error, QA_ERROR_MEMORY, "Allocating Q2 source resource indices");
    }
    return true;
}

void application_network_q2_free_tables(qa_application_network_q2 *owner)
{
    if (owner->configs) for (uint32_t i = 0; i < owner->config_count; ++i) free(owner->configs[i]);
    free(owner->configs); free(owner->entries);
    for (unsigned i = 0; i < 3; ++i) {
        application_q2_resource_table *table = &owner->resources[i];
        if (table->paths) for (uint32_t j = 1; j <= table->count; ++j) free(table->paths[j]);
        free(table->paths); table->paths = NULL; table->count = 0;
    }
    owner->configs = NULL; owner->entries = NULL;
}

bool application_network_q2_resource(qa_application_network_q2 *owner, unsigned kind,
    const char *path, uint32_t *out, qa_error *error)
{
    if (!out || kind > 2) return application_fail(error, QA_ERROR_ARGUMENT, "Invalid Q2 source resource kind");
    if (!path || !*path) { *out = 0; return true; }
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL) {
        application_provider *provider = application_network_q2_provider(owner);
        struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
        if (!application_native_q2_wire_resource(engine, kind, path, out, error)) return false;
        return application_network_q2_config(owner, owner->resources[kind].base + *out, path, error);
    }
    application_q2_resource_table *table = &owner->resources[kind];
    for (uint32_t i = 1; i <= table->count; ++i)
        if (!strcmp(table->paths[i], path)) { *out = i; return true; }
    if (table->count + 1 >= table->maximum)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 source resource table is full");
    char *copy = application_network_q2_copy(path, error);
    uint32_t index = table->count + 1;
    if (!copy) return false;
    if (!application_network_q2_config(owner, table->base + index, path, error)) { free(copy); return false; }
    table->paths[index] = copy; table->count = index; *out = index;
    return true;
}

static application_player_record *roster_player(qa_application_network_q2 *owner, qa_actor_id actor)
{
    struct application_player_roster *roster = owner->app->players;
    application_player_record *found = NULL;
    for (size_t i = 0; roster && i < roster->count; ++i) {
        application_player_record *row = &roster->records[i];
        if (row->retiring || row->deferred || !qa_actor_id_equal(row->actor, actor)) continue;
        if (found) return NULL;
        found = row;
    }
    return found;
}

bool qa_application_network_q2_player(qa_application_network_q2 *owner, qa_actor_id actor,
    qa_network_q2_player *out, qa_error *error)
{
    qa_application_control_view control;
    if (!out || !application_network_q2_current(owner, error) || !roster_player(owner, actor) ||
        !qa_application_control_read(owner->app, actor, &control))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 wire player has no admitted full actor/control owner");
    uint32_t slot = 0;
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN) {
        qa_q2_wire_binding binding;
        qa_builtin_player_info player;
        qa_q2_game *game = (qa_q2_game *)owner->host.source.source.game;
        if (!qa_q2_wire_actor(game, actor, &binding, error) ||
            !qa_q2_player_projection(game, actor, &player) || binding.source_slot != player.slot + 1)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 player lost its physical compiled GAME admission");
        slot = binding.source_slot;
    } else {
        application_provider *provider = application_network_q2_provider(owner);
        struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
        for (uint32_t i = 1; engine && i <= owner->host.client_slots; ++i) {
            if (!engine->clients[i].connected || !qa_actor_id_equal(engine->clients[i].actor, actor)) continue;
            if (slot) return application_fail(error, QA_ERROR_FORMAT, "Original Q2 player aliases physical client rows");
            slot = i;
        }
        qa_native_host_q2_entity physical;
        if (!slot || !qa_native_host_q2_wire_entity(provider->state.native.host, slot, &physical, error) ||
            !qa_actor_id_equal(physical.binding.actor, actor) || physical.binding.source_slot != slot ||
            physical.binding.owner != owner->host.source.source_owner)
            return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 player lost its real edict binding");
    }
    if (!slot || slot > owner->host.client_slots ||
        !qa_application_native_q2_presentation_current(owner->app, &owner->host.source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 source player changed during publication");
    *out = (qa_network_q2_player){.actor = actor, .source_owner = owner->host.source.source_owner,
        .source_slot = slot, .movement = control.state.kind};
    return true;
}

bool qa_application_network_q2_create(qa_application *app, qa_net_protocol_id protocol,
    int32_t server_count, qa_application_network_q2 **out, qa_error *error)
{
    if (!out || *out) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 publication creation requires an empty owner");
    qa_application_network_q2 *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Allocating actual Q2 publication owner");
    owner->app = app; owner->server_count = server_count;
    bool ok = qa_application_network_q2_host_source(app, protocol, &owner->host, error);
    if (ok) owner->identity = owner->host.source.launch->identity;
    if (ok) ok = application_network_q2_layout(owner, error);
    if (ok) {
        owner->entity_capacity = owner->host.entity_slots;
        owner->entities = calloc(owner->entity_capacity, sizeof(*owner->entities));
        owner->baselines = calloc(owner->entity_capacity, sizeof(*owner->baselines));
        owner->motion_rows = calloc(owner->entity_capacity, sizeof(*owner->motion_rows));
        owner->status_players = calloc(owner->host.client_slots, sizeof(*owner->status_players));
        owner->status_names = calloc(owner->host.client_slots, sizeof(*owner->status_names));
        owner->event_actors = calloc(owner->entity_capacity, sizeof(*owner->event_actors));
        owner->events = calloc(owner->entity_capacity, sizeof(*owner->events));
        ok = owner->entities && owner->baselines && owner->motion_rows && owner->status_players && owner->status_names &&
            owner->event_actors && owner->events;
        if (!ok) application_fail(error, QA_ERROR_MEMORY, "Retaining genuine Q2 publication rows");
    }
    if (ok) *out = owner;
    else qa_application_network_q2_destroy(owner);
    return ok;
}

bool qa_application_network_q2_slot(qa_application_network_q2 *owner, uint32_t slot,
    qa_application_network_q2_client_slot *out, qa_error *error)
{
    if (!out || !application_network_q2_current(owner, error)) return false;
    if (!slot || slot > owner->host.client_slots)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 client inventory leaves its actual Source reservation");
    qa_application_network_q2_client_slot value = {.source_owner = owner->host.source.source_owner, .source_slot = slot};
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN) {
        qa_q2_wire_binding binding;
        qa_q2_game *game = (qa_q2_game *)owner->host.source.source.game;
        if (!qa_q2_wire_binding_read(game, slot, &binding, error)) return false;
        value.actor = binding.actor; value.occupied = binding.in_use; value.reserved = binding.in_use;
        qa_builtin_player_info player;
        if (binding.in_use && qa_q2_player_projection(game, binding.actor, &player)) {
            if (player.slot + 1 != slot)
                return application_fail(error, QA_ERROR_FORMAT, "Q2 client inventory aliases its real Source client slot");
            value.connected = player.connected;
        }
    } else {
        application_provider *provider = application_network_q2_provider(owner);
        struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
        qa_native_slot_binding binding;
        if (!engine || !qa_native_slot(qa_native_host_instance(provider->state.native.host), slot, &binding, error)) return false;
        const application_native_q2_client *client = &engine->clients[slot];
        if (binding.kind != QA_NATIVE_SLOT_FREE) {
            const qa_actor_record *actor = qa_actors_get(qa_session_actors(owner->app->session), binding.actor);
            if (!actor || binding.owner != value.source_owner || binding.source_slot != slot || binding.slot != slot ||
                (binding.kind == QA_NATIVE_SLOT_OWNED &&
                    (actor->owner != binding.owner || !actor->has_source || actor->source_slot != slot)))
                return application_fail(error, QA_ERROR_FORMAT, "Original Q2 client inventory lost its actual SDK binding");
            value.actor = binding.actor; value.occupied = true;
        }
        value.reserved = client->reserved; value.connected = client->connected;
        value.occupied |= client->reserved || client->actor.registry != 0;
        if (client->actor.registry) {
            if (!qa_actors_get(qa_session_actors(owner->app->session), client->actor) ||
                (value.actor.registry && !qa_actor_id_equal(value.actor, client->actor)) ||
                (client->connected && !value.actor.registry))
                return application_fail(error, QA_ERROR_FORMAT, "Original Q2 reservation lost its actual Source client actor");
            value.actor = client->actor;
        } else if (client->connected)
            return application_fail(error, QA_ERROR_FORMAT, "Original Q2 connected reservation has no Source actor");
    }
    if (!qa_application_native_q2_presentation_current(owner->app, &owner->host.source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 client inventory changed its actual Source during observation");
    *out = value;
    return true;
}

void qa_application_network_q2_destroy(qa_application_network_q2 *owner)
{
    if (!owner) return;
    application_network_q2_unbind(owner);
    application_network_q2_free_tables(owner);
    application_network_q2_resources_free(owner);
    free(owner->entities); free(owner->baselines); free(owner->status_players); free(owner->status_names);
    free(owner->motion_rows);
    free(owner->event_actors); free(owner->events);
    qa_buffer_free(&owner->status_info); free(owner);
}

bool qa_application_network_q2_game_state(qa_application_network_q2 *owner,
    const qa_actor_id *actors, size_t seats, qa_q2_game_state *out, qa_error *error)
{
    if (!out || !actors || !seats || seats > QA_Q2_MAX_SEATS ||
        (seats > 1 && owner && owner->host.protocol.kind != QA_NET_Q2KEX_2023) ||
        !application_network_q2_current(owner, error) || !application_network_q2_observe(owner, error) ||
        !application_network_q2_entities(owner, error)) return false;
    qa_q2_game_state value = {.data = {.servercount = owner->server_count,
        .client_count = seats, .server_state = 2}};
    for (size_t i = 0; i < seats; ++i) {
        qa_network_q2_player player;
        if (!qa_application_network_q2_player(owner, actors[i], &player, error)) return false;
        for (size_t j = 0; j < i; ++j)
            if (value.data.clientnums[j] == (int32_t)(player.source_slot - 1))
                return application_fail(error, QA_ERROR_FORMAT, "Q2 signon seats alias a physical source player");
        value.data.clientnums[i] = (int32_t)(player.source_slot - 1);
    }
    value.data.clientnum = value.data.clientnums[0];
    application_provider *provider = application_network_q2_provider(owner);
    const char *directory = provider->product->directory;
    const char *last = directory ? strrchr(directory, '/') : NULL;
    directory = last ? last + 1 : directory;
    const char *level = owner->configs[0] ? owner->configs[0] :
        qa_strings_cstr(qa_session_strings(owner->app->session), owner->app->current_map);
    if (!directory || strlen(directory) >= sizeof(value.data.gamedir) || !level || strlen(level) >= sizeof(value.data.levelname))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 signon Source directory or level name is too long");
    strcpy(value.data.gamedir, directory); strcpy(value.data.levelname, level);
    uint64_t interval = owner->host.source.clock_config.interval_ns;
    if (!interval || UINT64_C(1000000000) / interval > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 signon source frame interval is invalid");
    value.data.server_fps = (uint32_t)(UINT64_C(1000000000) / interval);
    value.data.protocol_revision = owner->host.protocol.revision;
    value.data.wire_flags = owner->host.protocol.flags;
    size_t configs = 0;
    for (uint32_t i = 0; i < owner->config_count; ++i)
        if (owner->configs[i]) owner->entries[configs++] = (qa_q2_config_entry){(uint16_t)i, owner->configs[i]};
    owner->baseline_count = 0;
    for (size_t i = 0; i < owner->entity_count; ++i) {
        const qa_q2_entity *entity = &owner->entities[i];
        if (entity->modelindex || entity->modelindex2 || entity->modelindex3 || entity->modelindex4 || entity->sound || entity->effects)
            owner->baselines[owner->baseline_count++] = *entity;
    }
    if (!qa_application_native_q2_presentation_current(owner->app, &owner->host.source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 signon changed its physical source during capture");
    value.configs = owner->entries; value.config_count = configs;
    value.baselines = (qa_q2_entity_span){owner->baselines, owner->baseline_count};
    *out = value;
    return true;
}

bool qa_application_network_q2_download_source(qa_application_network_q2 *owner,
    qa_network_q2_download_source *out, qa_error *error)
{
    if (!out || !application_network_q2_current(owner, error)) return false;
    *out = (qa_network_q2_download_source){.content = owner->host.content, .cvars = owner->host.cvars,
        .resource_context = owner, .resource = application_network_q2_download_resource};
    return true;
}

static void original_name(const char *userinfo, char out[32])
{
    out[0] = 0;
    const char *cursor = userinfo;
    if (*cursor == '\\') ++cursor;
    while (*cursor) {
        const char *key = cursor;
        while (*cursor && *cursor != '\\') ++cursor;
        size_t key_size = (size_t)(cursor - key);
        if (!*cursor) return;
        const char *value = ++cursor;
        while (*cursor && *cursor != '\\') ++cursor;
        if (key_size == 4 && !memcmp(key, "name", 4)) {
            size_t size = (size_t)(cursor - value);
            if (size > 31) size = 31;
            memcpy(out, value, size); out[size] = 0;
            return;
        }
        if (*cursor) ++cursor;
    }
}

bool qa_application_network_q2_status(qa_application_network_q2 *owner, qa_q2_status *out, qa_error *error)
{
    if (!out || !application_network_q2_current(owner, error)) return false;
    qa_buffer info = {0};
    if (!qa_cvars_info(owner->host.cvars, QA_CVAR_SERVERINFO, 8192, &info, error)) return false;
    size_t count = 0;
    struct application_player_roster *roster = owner->app->players;
    for (size_t i = 0; roster && i < roster->count; ++i) {
        application_player_record *row = &roster->records[i];
        if (row->retiring || row->deferred) continue;
        if (count >= owner->host.client_slots) { qa_buffer_free(&info); return application_fail(error, QA_ERROR_FORMAT, "Q2 status exceeds real source client capacity"); }
        qa_network_q2_player player;
        if (!qa_application_network_q2_player(owner, row->actor, &player, error)) { qa_buffer_free(&info); return false; }
        int32_t score, ping;
        if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN) {
            qa_q2_player_info actual;
            if (!qa_q2_player_read((qa_q2_game *)owner->host.source.source.game, row->actor, &actual) || !actual.connected) {
                qa_buffer_free(&info); return application_fail(error, QA_ERROR_ARGUMENT, "Q2 status lost a physical source player");
            }
            score = actual.score; ping = actual.ping;
            snprintf(owner->status_names[count], 32, "%s", actual.name);
        } else {
            qa_q2_player state;
            qa_native_host *host = (qa_native_host *)owner->host.source.source.original.host;
            application_provider *provider = application_network_q2_provider(owner);
            struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
            if (!qa_native_host_q2_wire_player(host, player.source_slot, player.actor, &state, error) ||
                !qa_native_host_q2_wire_ping(host, player.source_slot, player.actor, &ping, error)) { qa_buffer_free(&info); return false; }
            if (!engine || !engine->clients[player.source_slot].userinfo_present ||
                !qa_actor_id_equal(engine->clients[player.source_slot].actor, player.actor)) {
                qa_buffer_free(&info);
                return application_fail(error, QA_ERROR_ARGUMENT, "Q2 status lost its GAME-returned userinfo");
            }
            score = state.stats[14];
            original_name(engine->clients[player.source_slot].userinfo, owner->status_names[count]);
        }
        owner->status_players[count] = (qa_q2_status_player){score, ping, owner->status_names[count]};
        ++count;
    }
    if (!qa_application_native_q2_presentation_current(owner->app, &owner->host.source)) {
        qa_buffer_free(&info); return application_fail(error, QA_ERROR_ARGUMENT, "Q2 status source changed during observation");
    }
    qa_buffer_free(&owner->status_info); owner->status_info = info;
    *out = (qa_q2_status){(const char *)info.data, owner->status_players, count};
    return true;
}

static const char *source_text(qa_application_network_q2 *owner, qa_string_id id)
{
    return id ? qa_strings_cstr(qa_session_strings(owner->app->session), id) : "";
}

static bool config_number(qa_application_network_q2 *owner, uint32_t index, uint32_t number, qa_error *error)
{
    char text[32]; snprintf(text, sizeof(text), "%u", number);
    return application_network_q2_config(owner, index, text, error);
}

static const char *world_field(qa_application_network_q2 *owner,
    const qa_q2_entity_state *state, const char *name, const char *fallback)
{
    const char *value = fallback;
    for (size_t i = 0; i < state->field_count; ++i) {
        const char *key = source_text(owner, state->fields[i].key);
        if (key && !strcmp(key, name)) value = source_text(owner, state->fields[i].value);
    }
    return value;
}

static bool initialize_builtin(qa_application_network_q2 *owner, qa_error *error)
{
    qa_q2_game *game = (qa_q2_game *)owner->host.source.source.game;
    qa_q2_wire_binding world;
    qa_q2_entity_checkpoint state = {0};
    if (!qa_q2_wire_binding_read(game, 0, &world, error) || !world.in_use ||
        !qa_q2_entity_capture(game, world.actor, &state, error) || !state.present)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 gamestate lost its actual source worldspawn");
    const char *map = source_text(owner, owner->app->current_map);
    const char *name = state.value.message ? source_text(owner, state.value.message) : map;
    bool ok = application_network_q2_config(owner, 0, name, error) &&
        application_network_q2_config(owner, 2, world_field(owner, &state.value, "sky", "unit1_"), error) &&
        application_network_q2_config(owner, 3, world_field(owner, &state.value, "skyaxis", "0 0 0"), error) &&
        application_network_q2_config(owner, 4, world_field(owner, &state.value, "skyrotate", "0"), error);
    qa_q2_entity_checkpoint_free(&state);
    uint32_t ignored;
    if (ok) ok = application_network_q2_resource(owner, 0, qa_resource_path(owner->app->map_resource), &ignored, error);
    uint64_t order = 0;
    bool found = true;
    while (ok && found) {
        qa_q2_wire_binding binding;
        ok = qa_q2_wire_next(game, &order, &binding, &found, error);
        if (!ok || !found) break;
        qa_q2_wire_source_entity entity;
        ok = qa_q2_wire_entity_read(game, binding.source_slot, &entity, error);
        for (unsigned i = 0; ok && !entity.flare && i < 4; ++i)
            ok = application_network_q2_resource(owner, 0, source_text(owner, entity.visual.models[i]), &ignored, error);
        if (ok && entity.flare && (entity.visual.render_flags & 256))
            ok = application_network_q2_resource(owner, 2, source_text(owner, entity.flare_image), &ignored, error);
        if (ok) ok = application_network_q2_resource(owner, 1, source_text(owner, entity.loop_sound), &ignored, error);
    }
    uint32_t weapons = 0;
    if (ok) ok = qa_q2_bot_arsenal_definition_count(game, &weapons, error);
    for (uint32_t i = 0; ok && i < weapons; ++i) {
        const qa_q2_weapon_definition *definition;
        ok = qa_q2_bot_arsenal_definition_read(game, i, &definition, error) &&
            application_network_q2_resource(owner, 0, definition->view_model, &ignored, error) &&
            application_network_q2_resource(owner, 0, definition->world_model, &ignored, error);
    }
    if (ok) ok = application_network_q2_resource(owner, 2, "i_health", &ignored, error);
    size_t count = qa_q2_item_count(game);
    if (count > owner->skin_base - owner->item_base - 1)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 Source item catalog exceeds its wire config range");
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_q2_item_definition *item = qa_q2_item_at(game, i);
        ok = item && application_network_q2_config(owner, owner->item_base + (uint32_t)i + 1, item->name, error);
    }
    return ok;
}

bool application_network_q2_observe(qa_application_network_q2 *owner, qa_error *error)
{
    application_provider *provider = application_network_q2_provider(owner);
    if (!provider || !owner->app->map_resource) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 observation lost its source map content");
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL) {
        struct application_native_q2 *engine = provider->state.native.q2_engine;
        if (!engine || engine->configstring_count != owner->config_count)
            return application_fail(error, QA_ERROR_FORMAT, "Original Q2 Source config layout differs from its wire dialect");
        for (uint32_t i = 0; i < owner->config_count; ++i)
            if (!application_network_q2_config(owner, i, engine->configstrings[i] ? engine->configstrings[i] : "", error)) return false;
    } else if (!owner->initialized && !initialize_builtin(owner, error)) return false;
    if (!config_number(owner, owner->checksum_index, qa_block_checksum(qa_resource_bytes(owner->app->map_resource)), error) ||
        !config_number(owner, owner->clients_index, owner->host.client_slots, error)) return false;
    const qa_cvar_view *air = qa_cvars_find(owner->host.cvars, "sv_airaccelerate");
    if (!air || !application_network_q2_config(owner, owner->air_index, air->value, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 observation lost its physical Source air acceleration");
    if (owner->n64_index != UINT32_MAX && owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN) {
        bool n64 = provider->product->campaign && !strcmp(provider->product->campaign, "n64");
        if (!application_network_q2_config(owner, owner->n64_index, n64 ? "1" : "0", error)) return false;
    }
    if (owner->event_frame != owner->host.source.clock.frame_number) {
        memset(owner->event_actors, 0, owner->entity_capacity * sizeof(*owner->event_actors));
        memset(owner->events, 0, owner->entity_capacity * sizeof(*owner->events));
        owner->event_frame = owner->host.source.clock.frame_number;
    }
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN) {
        qa_q2_game *game = (qa_q2_game *)owner->host.source.source.game;
        qa_q2_wire_map map;
        if (!qa_q2_wire_map_read(game, &map, error)) return false;
        if (map.music_present && !application_network_q2_config(owner, 1, source_text(owner, map.music), error)) return false;
        if (map.sky) {
            char axis[128], rotation[64];
            snprintf(axis, sizeof(axis), "%.9g %.9g %.9g", map.sky_axis.x, map.sky_axis.y, map.sky_axis.z);
            if (owner->host.source.edition == QA_Q2_RERELEASE)
                snprintf(rotation, sizeof(rotation), "%.9g %u", map.sky_rotation, map.sky_auto ? 1u : 0u);
            else snprintf(rotation, sizeof(rotation), "%.9g", map.sky_rotation);
            if (!application_network_q2_config(owner, 2, source_text(owner, map.sky), error) ||
                !application_network_q2_config(owner, 3, axis, error) ||
                !application_network_q2_config(owner, 4, rotation, error)) return false;
        }
        for (uint32_t style = 0; style < 256; ++style) {
            qa_string_id pattern;
            if (!qa_q2_wire_lightstyle_read(game, style, &pattern, error) ||
                !application_network_q2_config(owner, owner->light_base + style, source_text(owner, pattern), error)) return false;
        }
        if (owner->host.source.edition == QA_Q2_RERELEASE) {
            for (uint32_t index = 0; index < 256; ++index) {
                qa_q2_wire_shadow_light light;
                char config[512] = {0};
                if (!qa_q2_wire_shadow_read(game, index, &light, error)) return false;
                if (light.present) {
                    int size = snprintf(config, sizeof(config),
                        "%u;%u;%.9g;%u;%.9g;%.9g;%.9g;%d;%.9g;%.9g;%.9g;%.9g",
                        light.source_slot, light.type, light.radius, light.resolution,
                        light.intensity, light.fade_start, light.fade_end, light.style,
                        light.cone_angle, light.direction.x, light.direction.y, light.direction.z);
                    if (size < 0 || (size_t)size >= sizeof(config))
                        return application_fail(error, QA_ERROR_FORMAT, "Q2 Source shadow-light config exceeds its wire record");
                }
                if (!application_network_q2_config(owner, owner->light_base + 256 + index, config, error)) return false;
            }
        }
        for (size_t i = 0; i < qa_application_event_count(owner->app); ++i) {
            qa_builtin_event event;
            if (!qa_application_event_at(owner->app, i, &event) || event.family != QA_GAME_Q2 ||
                event.provider != owner->host.source.source_owner || event.time_ns != owner->host.source.server_time_ns) continue;
            const char *resource = source_text(owner, event.resource);
            uint32_t ignored;
            if (event.kind == QA_BUILTIN_SOUND &&
                !application_network_q2_resource(owner, 1, resource, &ignored, error)) return false;
            if (event.kind == QA_BUILTIN_EFFECT && resource && !strcmp(resource, "q2:entity-event")) {
                qa_q2_wire_binding binding;
                if (qa_q2_wire_actor(game, event.actor, &binding, NULL)) {
                    if (event.code < 0 || event.code > 255) return application_fail(error, QA_ERROR_FORMAT, "Q2 Source entity event exceeds its literal wire byte");
                    owner->event_actors[binding.source_slot] = event.actor;
                    owner->events[binding.source_slot] = (uint32_t)event.code;
                }
            }
        }
        for (size_t slot = 1; slot < owner->entity_capacity; ++slot) {
            if (!owner->event_actors[slot].registry) continue;
            qa_q2_wire_binding binding;
            qa_error current = {0};
            if (!qa_q2_wire_actor(game, owner->event_actors[slot], &binding, &current)) {
                if (current.code != QA_ERROR_NOT_FOUND) { if (error) *error = current; return false; }
                owner->event_actors[slot] = (qa_actor_id){0}; owner->events[slot] = 0;
            } else if (binding.source_slot != slot)
                return application_fail(error, QA_ERROR_FORMAT, "Q2 retained event changed its physical Source edict");
        }
        for (size_t i = 0; i < qa_application_q2_map_event_count(owner->app); ++i) {
            qa_application_q2_map_event record;
            if (!qa_application_q2_map_event_at(owner->app, i, &record) ||
                record.provider != owner->host.source.source_owner) continue;
            if (record.event.kind == QA_Q2_MAP_LIGHTSTYLE) {
                int style = record.event.style;
                if (style < 0 || (uint32_t)style >= 256 ||
                    !application_network_q2_config(owner, owner->light_base + (uint32_t)style, source_text(owner, record.event.text), error)) return false;
            }
        }
        struct application_player_roster *roster = owner->app->players;
        for (size_t i = 0; roster && i < roster->count; ++i) {
            application_player_record *row = &roster->records[i];
            if (row->retiring || row->deferred) continue;
            qa_builtin_player_info player;
            if (!qa_q2_player_projection(game, row->actor, &player))
                return application_fail(error, QA_ERROR_ARGUMENT, "Q2 gamestate player skin lacks physical Source admission");
            if (player.slot >= owner->host.client_slots) return application_fail(error, QA_ERROR_FORMAT, "Q2 Source player skin leaves its physical client range");
            char text[512];
            int length = snprintf(text, sizeof(text), "%s\\%s", player.name ? player.name : "", player.skin ? player.skin : "");
            if (length < 0 || (size_t)length >= sizeof(text) ||
                !application_network_q2_config(owner, owner->skin_base + player.slot, text, error)) return false;
        }
    }
    owner->initialized = true;
    return qa_application_native_q2_presentation_current(owner->app, &owner->host.source) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Q2 config observation changed its physical GAME publication");
}
