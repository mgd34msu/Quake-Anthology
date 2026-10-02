#include "wire_internal.h"
#include "qa/game_q1_maps.h"
#include "maps/internal.h"
#include "qa/game_q1_bots.h"
#include <math.h>
#include <stdio.h>

static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static bool id1(const qa_q1_game *g) {
    return g->options.program == QA_Q1_ID1 && g->options.edition == QA_Q1_CLASSIC &&
           !g->options.quakeworld;
}
void q1_wire_changed(q1_wire_state *wire) {
    if (wire && wire->revision != UINT64_MAX) ++wire->revision;
}
static void state_free(q1_wire_state *wire) {
    if (!wire) return;
    free(wire->models.rows);
    free(wire->sounds.rows);
    free(wire->damage);
    free(wire->board);
    free(wire);
}
void q1_wire_destroy(qa_q1_game *g) {
    state_free(g->wire);
    g->wire = NULL;
}
void q1_wire_map_reset(qa_q1_game *g) {
    if (g->wire) {
        g->wire->loading = true;
        g->wire->damage_count = 0;
        q1_wire_changed(g->wire);
    }
}
void q1_wire_actor_released(qa_q1_game *g, qa_actor_id actor) {
    q1_wire_state *wire = g->wire;
    for (size_t i = 0; wire && i < wire->damage_count;)
        if (qa_actor_id_equal(wire->damage[i].recipient, actor)) {
            wire->damage[i] = wire->damage[--wire->damage_count];
            q1_wire_changed(wire);
        }
        else ++i;
}
static bool append(qa_q1_game *g, q1_wire_table *table, const char *path, qa_error *error) {
    if (!path || (unsigned char)path[0] <= 32)
        return fail(error, "Q1 precache declaration requires a resource path");
    qa_string_id resource;
    if (!qa_builtin_resource(&g->services, path, &resource, error)) return false;
    for (size_t i = 1; i < table->count; ++i)
        if (table->rows[i] == resource) return true;
    if (table->count == table->capacity) {
        size_t next = table->capacity ? table->capacity * 2 : 16;
        if (next < table->capacity || next > SIZE_MAX / sizeof(*table->rows))
            return fail(error, "Q1 precache declaration exceeds its source extent");
        qa_string_id *rows = realloc(table->rows, next * sizeof(*rows));
        if (!rows) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "growing ordered Q1 precaches");
            return false;
        }
        table->rows = rows;
        table->capacity = next;
        if (!table->count) table->rows[table->count++] = 0;
    }
    table->rows[table->count++] = resource;
    q1_wire_changed(g->wire);
    return true;
}
bool qa_q1_wire_declare_model(qa_q1_game *g, const char *path, qa_error *error) {
    if (!g || !g->wire || !g->wire->loading || g->destroy_pending)
        return fail(error, "Q1 model declaration requires the native loading stage");
    return append(g, &g->wire->models, path, error);
}
bool qa_q1_wire_declare_sound(qa_q1_game *g, const char *path, qa_error *error) {
    if (!g || !g->wire || !g->wire->loading || g->destroy_pending)
        return fail(error, "Q1 sound declaration requires the native loading stage");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    bool okay = append(g, &g->wire->sounds, path, error);
    if (okay && g->host.sound_precache)
        okay = g->host.sound_precache(g->host.context, path, error);
    if (okay && !qa_q1_game_operation_live(&operation))
        okay = fail(error, "Q1 source retired during its sound precache");
    qa_q1_game_operation_end(&operation);
    return okay;
}
bool qa_q1_wire_begin_world(qa_q1_game *g, const char *path, uint32_t inline_models,
                            uint32_t authored_entities, qa_error *error) {
    if (!g || !path || !*path || !authored_entities || g->destroy_pending ||
        g->observation_depth || !qa_session_safe(g->services.session) ||
        qa_actors_count(qa_session_actors(g->services.session)) ||
        authored_entities > UINT32_MAX - g->options.max_clients)
        return fail(error, "Q1 wire world requires an idle actual native map load");
    uint64_t generation = g->wire ? g->wire->generation : 0;
    if (generation == UINT64_MAX) return fail(error, "Q1 wire map generation exhausted");
    q1_wire_state *wire = calloc(1, sizeof(*wire));
    if (!wire) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 source wire state");
        return false;
    }
    wire->generation = generation + 1;
    wire->inline_models = inline_models;
    wire->authored_entities = authored_entities;
    wire->next_dynamic = g->options.max_clients + authored_entities;
    wire->loading = true;
    wire->id1 = id1(g);
    if (g->options.max_clients) {
        if ((size_t)g->options.max_clients > SIZE_MAX / sizeof(*wire->board)) {
            state_free(wire);
            return fail(error, "Q1 source client observation extent exhausted");
        }
        wire->board = calloc(g->options.max_clients, sizeof(*wire->board));
        if (!wire->board) {
            state_free(wire);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 source client observations");
            return false;
        }
    }
    if (!qa_builtin_resource(&g->services, path, &wire->map_path, error)) {
        state_free(wire);
        return false;
    }
    if (g->host.precache_reset &&
        !g->host.precache_reset(g->host.context, error)) {
        state_free(wire);
        return false;
    }
    q1_wire_state *previous = g->wire;
    g->wire = wire;
    bool okay = qa_q1_wire_declare_model(g, path, error);
    for (uint32_t i = 1; okay && i <= inline_models; ++i) {
        char model[32];
        snprintf(model, sizeof(model), "*%u", i);
        okay = qa_q1_wire_declare_model(g, model, error);
        if (i == UINT32_MAX) break;
    }
    if (okay) {
        wire->sounds.rows = calloc(1, sizeof(*wire->sounds.rows));
        okay = wire->sounds.rows != NULL;
        if (okay) wire->sounds.count = wire->sounds.capacity = 1;
        else qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 empty sound slot");
    }
    if (!okay) {
        g->wire = previous;
        state_free(wire);
        return false;
    }
    state_free(previous);
    return true;
}
bool qa_q1_wire_freeze(qa_q1_game *g, qa_error *error) {
    if (!g || g->destroy_pending || g->observation_depth)
        return fail(error, "Q1 wire freeze requires the native map finish stage");
    if (g->wire) { g->wire->loading = false; q1_wire_changed(g->wire); }
    return true;
}
bool q1_wire_allocate_slot(qa_q1_game *g, bool *has_source, uint32_t *slot, qa_error *error) {
    *has_source = g->wire != NULL;
    if (!g->wire) return true;
    if (g->wire->next_dynamic == UINT32_MAX)
        return fail(error, "Q1 physical source entity extent exhausted");
    *slot = g->wire->next_dynamic++;
    q1_wire_changed(g->wire);
    return true;
}
bool qa_q1_wire_authored_slot(const qa_q1_game *g, size_t ordinal, uint32_t *out) {
    if (!g || !g->wire || !g->wire->loading || !out ||
        ordinal >= g->wire->authored_entities || g->destroy_pending) return false;
    *out = ordinal ? g->options.max_clients + (uint32_t)ordinal : 0;
    return true;
}
bool qa_q1_wire_read_begin(qa_q1_game *g, qa_q1_wire_receipt *out, qa_error *error) {
    if (!out || !g || !g->wire || g->wire->loading || !g->wire->id1)
        return fail(error, "Q1 ordered wire source is not frozen classic id1");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    const q1_wire_state *wire = g->wire;
    *out = (qa_q1_wire_receipt){.operation = operation, .generation = wire->generation,
        .owner = g->options.provider, .client_slots = g->options.max_clients,
        .entity_slots = wire->next_dynamic, .models = wire->models.rows,
        .sounds = wire->sounds.rows, .model_count = wire->models.count,
        .sound_count = wire->sounds.count, .map_path = wire->map_path,
        .seconds = g->time, .deathmatch = g->options.deathmatch};
    return true;
}
bool qa_q1_wire_receipt_current(const qa_q1_wire_receipt *receipt) {
    return receipt && qa_q1_game_operation_live(&receipt->operation) &&
        receipt->operation.game->wire &&
        receipt->operation.game->wire->generation == receipt->generation &&
        !receipt->operation.game->wire->loading;
}
void qa_q1_wire_read_end(qa_q1_wire_receipt *receipt) {
    if (!receipt) return;
    qa_q1_game_operation_end(&receipt->operation);
    *receipt = (qa_q1_wire_receipt){0};
}
bool qa_q1_wire_index(const qa_q1_wire_receipt *receipt, bool models, qa_string_id resource,
                       uint32_t *out) {
    if (!out || !qa_q1_wire_receipt_current(receipt)) return false;
    const qa_string_id *rows = models ? receipt->models : receipt->sounds;
    size_t count = models ? receipt->model_count : receipt->sound_count;
    for (size_t i = 0; i < count && i <= UINT32_MAX; ++i)
        if (rows[i] == resource) { *out = (uint32_t)i; return true; }
    return false;
}
bool qa_q1_wire_emission_index(const qa_q1_game *g, bool models, qa_string_id resource,
                               uint32_t *out) {
    if (!g || !g->wire || !g->wire->id1 || g->destroy_pending || !out) return false;
    const q1_wire_table *table = models ? &g->wire->models : &g->wire->sounds;
    for (size_t i = 0; i < table->count && i <= UINT32_MAX; ++i)
        if (table->rows[i] == resource) { *out = (uint32_t)i; return true; }
    return false;
}
bool qa_q1_wire_emission_slot(const qa_q1_game *g, qa_actor_id actor, uint32_t *out) {
    if (!g || !g->wire || !g->wire->id1 || g->destroy_pending || !out) return false;
    uint32_t client;
    if (qa_q1_native_client_slot(g, actor, &client, NULL)) { *out = client + 1; return true; }
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), actor);
    if (!record || record->owner != g->options.provider || !record->has_source ||
        record->source_slot >= g->wire->next_dynamic ||
        (record->source_slot && record->source_slot <= g->options.max_clients)) return false;
    *out = record->source_slot;
    return true;
}
bool qa_q1_wire_registration_state(const qa_q1_game *g, uint64_t *generation, bool *loading) {
    if (!g || g->destroy_pending || g->continuation_pending || !g->wire ||
        !g->wire->id1 || !generation || !loading) return false;
    *generation = g->wire->generation;
    *loading = g->wire->loading;
    return true;
}

bool qa_q1_wire_enabled(const qa_q1_game *g) {
    return g && !g->destroy_pending && g->wire && g->wire->id1;
}
bool qa_q1_wire_lightstyle(qa_q1_game *g, int32_t style, qa_string_id pattern, qa_error *error) {
    if (!g || g->destroy_pending || style < 0 || style >= 64 ||
        !qa_strings_cstr(qa_session_strings(g->services.session), pattern))
        return fail(error, "Q1 lightstyle requires its actual source pattern");
    if (!g->wire) return true;
    g->wire->lightstyles[style] = pattern;
    q1_wire_changed(g->wire);
    return true;
}
bool qa_q1_source_lightstyle_read(const qa_q1_game *g, uint32_t style,
    qa_string_id *out, qa_error *error) {
    if (!g || !out || style >= 64 || g->destroy_pending || g->observation_depth ||
        !g->wire || g->wire->loading || !qa_session_safe(g->services.session))
        return fail(error, "Q1 lightstyle observation requires its returned native map owner");
    *out = g->wire->lightstyles[style];
    return true;
}
bool qa_q1_wire_world_read(const qa_q1_wire_receipt *receipt, qa_q1_wire_world *out) {
    if (!out || !qa_q1_wire_receipt_current(receipt)) return false;
    qa_q1_game *g = receipt->operation.game;
    if (!g->maps || !g->maps->options.server_flags) return false;
    q1_actor *world = q1_entity(g, g->maps->world_actor);
    if (!world) return false;
    *out = (qa_q1_wire_world){.map = g->maps->options.current_map, .level = world->message,
        .total_secrets = g->maps->total_secrets, .found_secrets = g->maps->found_secrets,
        .total_monsters = g->total_monsters, .killed_monsters = g->killed_monsters,
        .server_flags = *g->maps->options.server_flags};
    memcpy(out->lightstyles, g->wire->lightstyles, sizeof(out->lightstyles));
    return true;
}
bool qa_q1_wire_player_read(const qa_q1_wire_receipt *receipt, qa_actor_id actor,
    qa_q1_wire_player *out, qa_error *error) {
    if (!out || !qa_q1_wire_receipt_current(receipt))
        return fail(error, "Q1 source player observation lost its held owner");
    const qa_q1_game *g = receipt->operation.game;
    uint32_t slot;
    if (!qa_q1_native_client_slot(g, actor, &slot, error)) return false;
    const q1_player *player = g->players[actor.slot];
    qa_q1_weapon weapon = player->weapon;
    int32_t frame = player->weapon_frame;
    double powers[QA_Q1_POWER_COUNT], seconds = g->time;
    memcpy(powers, player->power_expires, sizeof(powers));
    if (weapon < QA_Q1_AXE || weapon > QA_Q1_LIGHTNING)
        return fail(error, "Q1 source weapon leaves its authentic id1 table");
    static const uint32_t bits[] = {4096, 1, 2, 4, 8, 16, 32, 64};
    qa_q1_wire_player value = {.weapon_model = g->weapon_models[weapon],
        .weapon_frame = frame, .weapon = bits[weapon]};
    if (!qa_inventory_count_read(g->services.inventory, actor, g->ammo[QA_Q1_SHELLS], &value.shells, error) ||
        !qa_inventory_count_read(g->services.inventory, actor, g->ammo[QA_Q1_NAILS], &value.nails, error) ||
        !qa_inventory_count_read(g->services.inventory, actor, g->ammo[QA_Q1_ROCKETS], &value.rockets, error) ||
        !qa_inventory_count_read(g->services.inventory, actor, g->ammo[QA_Q1_CELLS], &value.cells, error)) return false;
    for (size_t i = 0; i < sizeof(bits)/sizeof(*bits); ++i) {
        double count;
        if (!qa_inventory_count_read(g->services.inventory, actor, g->weapons[i], &count, error)) return false;
        if (count > 0) value.weapons |= bits[i];
    }
    if (!qa_q1_wire_receipt_current(receipt) || !qa_q1_native_client_slot(g, actor, &slot, error)) return false;
    player = g->players[actor.slot];
    if (player->weapon != weapon || player->weapon_frame != frame || g->time != seconds ||
        memcmp(player->power_expires, powers, sizeof(powers)))
        return fail(error, "Q1 source player changed during canonical inventory observation");
    if (powers[QA_Q1_QUAD] > seconds) value.powers |= 4194304;
    if (powers[QA_Q1_INVULNERABILITY] > seconds) value.powers |= 1048576;
    if (powers[QA_Q1_INVISIBILITY] > seconds) value.powers |= 524288;
    if (powers[QA_Q1_SUIT] > seconds) value.powers |= 2097152;
    value.ammo = weapon == QA_Q1_SHOTGUN || weapon == QA_Q1_SUPER_SHOTGUN ? value.shells :
                 weapon == QA_Q1_NAILGUN || weapon == QA_Q1_SUPER_NAILGUN ? value.nails :
                 weapon == QA_Q1_GRENADE || weapon == QA_Q1_ROCKET ? value.rockets :
                 weapon == QA_Q1_LIGHTNING ? value.cells : 0;
    *out = value;
    return true;
}
bool qa_q1_wire_board_observe(const qa_q1_wire_receipt *receipt, uint32_t slot,
    qa_q1_wire_board_change *out, qa_error *error) {
    if (!out || !qa_q1_wire_receipt_current(receipt) || slot >= receipt->client_slots)
        return fail(error, "Q1 client observation leaves the actual source extent");
    qa_q1_game *g = receipt->operation.game;
    q1_wire_client value = {0};
    qa_actor_id actor;
    if (qa_q1_source_client_actor(g, slot, &actor)) {
        qa_q1_source_client_view view;
        if (!qa_q1_source_client_read(g, actor, &view) || view.slot != slot || !isfinite(view.frags))
            return fail(error, "Q1 source client observation lost its actual publication");
        value = (q1_wire_client){.actor = actor, .frags = view.frags,
            .colors = (uint8_t)((view.shirt << 4) | view.pants), .present = true};
        if (!qa_strings_intern_cstr(qa_session_strings(g->services.session), view.name, &value.name, error))
            return false;
    }
    q1_wire_client *previous = &g->wire->board[slot];
    bool membership = previous->present != value.present ||
        (value.present && !qa_actor_id_equal(previous->actor, value.actor));
    *out = (qa_q1_wire_board_change){.actor = value.present ? value.actor : previous->actor,
        .name = value.name, .frags = value.frags, .slot = slot, .colors = value.colors, .present = value.present,
        .name_changed = membership || previous->name != value.name,
        .frags_changed = membership || previous->frags != value.frags,
        .colors_changed = membership || previous->colors != value.colors};
    return true;
}
bool qa_q1_wire_board_commit(const qa_q1_wire_receipt *receipt, const qa_q1_wire_board_change *change) {
    if (!change || !qa_q1_wire_receipt_current(receipt) || change->slot >= receipt->client_slots) return false;
    q1_wire_state *wire = receipt->operation.game->wire;
    wire->board[change->slot] = (q1_wire_client){.actor = change->present ? change->actor : (qa_actor_id){0},
        .name = change->name, .frags = change->frags, .colors = change->colors, .present = change->present};
    if (change->name_changed || change->frags_changed || change->colors_changed) q1_wire_changed(wire);
    return true;
}
bool qa_q1_wire_actor_slot(const qa_q1_wire_receipt *receipt, qa_actor_id actor, uint32_t *out) {
    if (!out || !qa_q1_wire_receipt_current(receipt)) return false;
    qa_q1_game *g = receipt->operation.game;
    uint32_t client;
    if (qa_q1_native_client_slot(g, actor, &client, NULL)) {
        *out = client + 1;
        return true;
    }
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), actor);
    if (!record || record->owner != receipt->owner || !record->has_source ||
        record->source_slot >= receipt->entity_slots ||
        (record->source_slot && record->source_slot <= receipt->client_slots)) return false;
    *out = record->source_slot;
    return true;
}
bool qa_q1_wire_actor_at(const qa_q1_wire_receipt *receipt, uint32_t slot, qa_actor_id *out) {
    if (!out || !qa_q1_wire_receipt_current(receipt) || slot >= receipt->entity_slots)
        return false;
    qa_q1_game *g = receipt->operation.game;
    if (slot && slot <= receipt->client_slots)
        return qa_q1_source_client_actor(g, slot - 1, out);
    const qa_actor_record *record = qa_actors_at_source(
        qa_session_actors(g->services.session), receipt->owner, slot);
    if (!record)
        return false;
    qa_actor_id actor = record->id;
    uint32_t actual;
    if (!qa_q1_wire_actor_slot(receipt, actor, &actual) || actual != slot) return false;
    *out = actor;
    return true;
}
bool qa_q1_wire_feedback_add(qa_q1_game *g, qa_actor_id recipient, qa_actor_id inflictor,
                             float armor, float blood, const double origin[3], qa_error *error) {
    uint32_t slot;
    if (!g || !g->wire || !g->wire->id1 || g->wire->revision == UINT64_MAX || g->destroy_pending || !origin ||
        !isfinite(armor) || !isfinite(blood) ||
        !isfinite(origin[0]) || !isfinite(origin[1]) || !isfinite(origin[2]) ||
        !qa_q1_native_client_slot(g, recipient, &slot, NULL))
        return fail(error, "Q1 wire feedback requires an actual native source client");
    q1_wire_state *wire = g->wire;
    size_t index = 0;
    while (index < wire->damage_count &&
           !qa_actor_id_equal(wire->damage[index].recipient, recipient)) ++index;
    if (index == wire->damage_count) {
        if (wire->damage_count == wire->damage_capacity) {
            size_t next = wire->damage_capacity ? wire->damage_capacity * 2 : 8;
            if (next < wire->damage_capacity || next > SIZE_MAX / sizeof(*wire->damage))
                return fail(error, "Q1 wire feedback source extent exhausted");
            q1_wire_damage *rows = realloc(wire->damage, next * sizeof(*rows));
            if (!rows) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "growing Q1 source feedback");
                return false;
            }
            wire->damage = rows;
            wire->damage_capacity = next;
        }
        wire->damage[wire->damage_count++] = (q1_wire_damage){.recipient = recipient};
    }
    qa_q1_wire_feedback *value = &wire->damage[index].value;
    if (!isfinite(value->armor + armor) || !isfinite(value->blood + blood))
        return fail(error, "Q1 source damage feedback exceeds its float fields");
    value->armor += armor;
    value->blood += blood;
    value->inflictor = inflictor;
    memcpy(value->origin, origin, sizeof(value->origin));
    q1_wire_changed(wire);
    wire->damage[index].revision = wire->revision;
    return true;
}
bool qa_q1_wire_feedback_consume(const qa_q1_wire_receipt *receipt, qa_actor_id recipient,
                                 qa_q1_wire_feedback *out) {
    if (!out || !qa_q1_wire_receipt_current(receipt)) return false;
    uint32_t slot;
    if (!qa_q1_native_client_slot(receipt->operation.game, recipient, &slot, NULL)) return false;
    qa_q1_game *game = receipt->operation.game;
    q1_wire_state *wire = game->wire;
    if (wire->revision == UINT64_MAX) return false;
    for (size_t i = 0; i < wire->damage_count; ++i)
        if (qa_actor_id_equal(wire->damage[i].recipient, recipient)) {
            q1_wire_damage pending = wire->damage[i];
            qa_q1_wire_feedback value = pending.value;
            qa_body_state body;
            uint64_t body_serial = qa_world_body_storage_serial(game->services.world, value.inflictor);
            if (value.inflictor.registry && body_serial && qa_world_body_read(
                    game->services.world, value.inflictor, &body, NULL) &&
                qa_world_body_storage_serial(game->services.world, value.inflictor) == body_serial) {
                value.origin[0] = (double)body.origin.x + .5 * ((double)body.bounds.mins.x + body.bounds.maxs.x);
                value.origin[1] = (double)body.origin.y + .5 * ((double)body.bounds.mins.y + body.bounds.maxs.y);
                value.origin[2] = (double)body.origin.z + .5 * ((double)body.bounds.mins.z + body.bounds.maxs.z);
            }
            if (!qa_q1_wire_receipt_current(receipt) ||
                !qa_q1_native_client_slot(game, recipient, &slot, NULL)) return false;
            wire = game->wire;
            if (wire->revision == UINT64_MAX) return false;
            size_t current = 0;
            while (current < wire->damage_count &&
                   !qa_actor_id_equal(wire->damage[current].recipient, recipient)) ++current;
            if (current == wire->damage_count || wire->damage[current].revision != pending.revision)
                return false;
            wire->damage[current] = wire->damage[--wire->damage_count];
            q1_wire_changed(wire);
            *out = value;
            return true;
        }
    *out = (qa_q1_wire_feedback){0};
    return true;
}

/* progs106 W_Precache/worldspawn, followed by each admitted spawn function.
 * Interner order is deliberately irrelevant to these two source tables. */
static const char *const world_sounds[] = {
    "weapons/r_exp3.wav", "weapons/rocket1i.wav", "weapons/sgun1.wav",
    "weapons/guncock.wav", "weapons/ric1.wav", "weapons/ric2.wav", "weapons/ric3.wav",
    "weapons/spike2.wav", "weapons/tink1.wav", "weapons/grenade.wav", "weapons/bounce.wav",
    "weapons/shotgn2.wav", "demon/dland2.wav", "misc/h2ohit1.wav", "items/itembk2.wav",
    "player/plyrjmp8.wav", "player/land.wav", "player/land2.wav", "player/drown1.wav",
    "player/drown2.wav", "player/gasp1.wav", "player/gasp2.wav", "player/h2odeath.wav",
    "misc/talk.wav", "player/teledth1.wav", "misc/r_tele1.wav", "misc/r_tele2.wav",
    "misc/r_tele3.wav", "misc/r_tele4.wav", "misc/r_tele5.wav", "weapons/lock4.wav",
    "weapons/pkup.wav", "items/armor1.wav", "weapons/lhit.wav", "weapons/lstart.wav",
    "items/damage3.wav", "misc/power.wav", "player/gib.wav", "player/udeath.wav",
    "player/tornoff2.wav", "player/pain1.wav", "player/pain2.wav", "player/pain3.wav",
    "player/pain4.wav", "player/pain5.wav", "player/pain6.wav", "player/death1.wav",
    "player/death2.wav", "player/death3.wav", "player/death4.wav", "player/death5.wav",
    "weapons/ax1.wav", "player/axhit1.wav", "player/axhit2.wav", "player/h2ojump.wav",
    "player/slimbrn2.wav", "player/inh2o.wav", "player/inlava.wav", "misc/outwater.wav",
    "player/lburn1.wav", "player/lburn2.wav", "misc/water1.wav", "misc/water2.wav"
};
static const char *const world_models[] = {
    "progs/player.mdl", "progs/eyes.mdl", "progs/h_player.mdl", "progs/gib1.mdl",
    "progs/gib2.mdl", "progs/gib3.mdl", "progs/s_bubble.spr", "progs/s_explod.spr",
    "progs/v_axe.mdl", "progs/v_shot.mdl", "progs/v_nail.mdl", "progs/v_rock.mdl",
    "progs/v_shot2.mdl", "progs/v_nail2.mdl", "progs/v_rock2.mdl", "progs/bolt.mdl",
    "progs/bolt2.mdl", "progs/bolt3.mdl", "progs/lavaball.mdl", "progs/missile.mdl",
    "progs/grenade.mdl", "progs/spike.mdl", "progs/s_spike.mdl", "progs/backpack.mdl",
    "progs/zom_gib.mdl", "progs/v_light.mdl"
};
typedef struct declarations {
    const char *classname;
    const char *models[6];
    const char *sounds[11];
} declarations;
static const declarations monster_declarations[] = {
    {"monster_army", {"progs/soldier.mdl", "progs/h_guard.mdl", "progs/gib1.mdl",
                      "progs/gib2.mdl", "progs/gib3.mdl"},
        {"soldier/death1.wav", "soldier/idle.wav", "soldier/pain1.wav", "soldier/pain2.wav",
         "soldier/sattck1.wav", "soldier/sight1.wav", "player/udeath.wav"}},
    {"monster_dog", {"progs/h_dog.mdl", "progs/dog.mdl"},
        {"dog/dattack1.wav", "dog/ddeath.wav", "dog/dpain1.wav", "dog/dsight.wav", "dog/idle.wav"}},
    {"monster_knight", {"progs/knight.mdl", "progs/h_knight.mdl"},
        {"knight/kdeath.wav", "knight/khurt.wav", "knight/ksight.wav", "knight/sword1.wav",
         "knight/sword2.wav", "knight/idle.wav"}},
    {"monster_enforcer", {"progs/enforcer.mdl", "progs/h_mega.mdl", "progs/laser.mdl"},
        {"enforcer/death1.wav", "enforcer/enfire.wav", "enforcer/enfstop.wav", "enforcer/idle1.wav",
         "enforcer/pain1.wav", "enforcer/pain2.wav", "enforcer/sight1.wav", "enforcer/sight2.wav",
         "enforcer/sight3.wav", "enforcer/sight4.wav"}},
    {"monster_demon1", {"progs/demon.mdl", "progs/h_demon.mdl"},
        {"demon/ddeath.wav", "demon/dhit2.wav", "demon/djump.wav", "demon/dpain1.wav",
         "demon/idle1.wav", "demon/sight2.wav"}},
    {"monster_ogre", {"progs/ogre.mdl", "progs/h_ogre.mdl", "progs/grenade.mdl"},
        {"ogre/ogdrag.wav", "ogre/ogdth.wav", "ogre/ogidle.wav", "ogre/ogidle2.wav",
         "ogre/ogpain1.wav", "ogre/ogsawatk.wav", "ogre/ogwake.wav"}},
    {"monster_hell_knight", {"progs/hknight.mdl", "progs/k_spike.mdl", "progs/h_hellkn.mdl"},
        {"hknight/attack1.wav", "hknight/death1.wav", "hknight/pain1.wav", "hknight/sight1.wav",
         "hknight/hit.wav", "hknight/slash1.wav", "hknight/idle.wav", "hknight/grunt.wav",
         "knight/sword1.wav", "knight/sword2.wav"}},
    {"monster_shambler", {"progs/shambler.mdl", "progs/s_light.mdl", "progs/h_shams.mdl",
                         "progs/bolt.mdl"},
        {"shambler/sattck1.wav", "shambler/sboom.wav", "shambler/sdeath.wav", "shambler/shurt2.wav",
         "shambler/sidle.wav", "shambler/ssight.wav", "shambler/melee1.wav", "shambler/melee2.wav",
         "shambler/smack.wav"}},
    {"monster_wizard", {"progs/wizard.mdl", "progs/h_wizard.mdl", "progs/w_spike.mdl"},
        {"wizard/hit.wav", "wizard/wattack.wav", "wizard/wdeath.wav", "wizard/widle1.wav",
         "wizard/widle2.wav", "wizard/wpain.wav", "wizard/wsight.wav"}},
    {"monster_shalrath", {"progs/shalrath.mdl", "progs/h_shal.mdl", "progs/v_spike.mdl"},
        {"shalrath/attack.wav", "shalrath/attack2.wav", "shalrath/death.wav", "shalrath/idle.wav",
         "shalrath/pain.wav", "shalrath/sight.wav"}},
    {"monster_tarbaby", {"progs/tarbaby.mdl"},
        {"blob/death1.wav", "blob/hit1.wav", "blob/land1.wav", "blob/sight1.wav"}},
    {"monster_fish", {"progs/fish.mdl"}, {"fish/death.wav", "fish/bite.wav", "fish/idle.wav"}},
    {"monster_zombie", {"progs/zombie.mdl", "progs/h_zombie.mdl", "progs/zom_gib.mdl"},
        {"zombie/z_idle.wav", "zombie/z_idle1.wav", "zombie/z_shot1.wav", "zombie/z_gib.wav",
         "zombie/z_pain.wav", "zombie/z_pain1.wav", "zombie/z_fall.wav", "zombie/z_miss.wav",
         "zombie/z_hit.wav", "zombie/idle_w2.wav"}},
    {"monster_boss", {"progs/boss.mdl", "progs/lavaball.mdl"},
        {"weapons/rocket1i.wav", "boss1/out1.wav", "boss1/sight1.wav", "misc/power.wav",
         "boss1/throw.wav", "boss1/pain.wav", "boss1/death.wav"}},
    {"monster_oldone", {"progs/oldone.mdl"},
        {"boss2/death.wav", "boss2/idle.wav", "boss2/sight.wav", "boss2/pop2.wav"}}
};
static bool declare_rows(qa_q1_game *g, bool models, const char *const *rows, size_t count,
                          qa_error *error) {
    for (size_t i = 0; i < count && rows[i]; ++i)
        if (!(models ? qa_q1_wire_declare_model(g, rows[i], error)
                     : qa_q1_wire_declare_sound(g, rows[i], error))) return false;
    return true;
}
#define MODEL(path) do { if (!qa_q1_wire_declare_model(g, (path), error)) return false; } while (0)
#define SOUND(path) do { if (!qa_q1_wire_declare_sound(g, (path), error)) return false; } while (0)
bool q1_wire_spawn_declarations(qa_q1_game *g, const qa_q1_spawn *spawn, qa_error *error) {
    if (!g->wire || !g->wire->id1 || !g->wire->loading) return true;
    const char *name = spawn->classname;
    int32_t sounds = spawn->map_fields ? spawn->map_fields->sounds : 0;
    uint32_t flags = spawn->spawnflags;
    if (!strcmp(name, "worldspawn"))
        return declare_rows(g, false, world_sounds, sizeof(world_sounds)/sizeof(*world_sounds), error) &&
               declare_rows(g, true, world_models, sizeof(world_models)/sizeof(*world_models), error);
    const char *monster = !strcmp(name, "monster_ogre_marksman") ? "monster_ogre" : name;
    for (size_t i = 0; i < sizeof(monster_declarations)/sizeof(*monster_declarations); ++i) {
        const declarations *row = &monster_declarations[i];
        if (!strcmp(monster, row->classname))
            return declare_rows(g, true, row->models, 6, error) &&
                   declare_rows(g, false, row->sounds, 11, error);
    }
    if (!strcmp(name, "item_health")) {
        bool big = flags & 1, mega = !big && (flags & 2);
        MODEL(big ? "maps/b_bh10.bsp" : mega ? "maps/b_bh100.bsp" : "maps/b_bh25.bsp");
        SOUND(big ? "items/r_item1.wav" : mega ? "items/r_item2.wav" : "items/health1.wav");
    } else if (!strcmp(name, "item_armor1") || !strcmp(name, "item_armor2") || !strcmp(name, "item_armorInv")) {
        MODEL("progs/armor.mdl");
    } else if (!strcmp(name, "weapon_supershotgun")) { MODEL("progs/g_shot.mdl");
    } else if (!strcmp(name, "weapon_nailgun")) { MODEL("progs/g_nail.mdl");
    } else if (!strcmp(name, "weapon_supernailgun")) { MODEL("progs/g_nail2.mdl");
    } else if (!strcmp(name, "weapon_grenadelauncher")) { MODEL("progs/g_rock.mdl");
    } else if (!strcmp(name, "weapon_rocketlauncher")) { MODEL("progs/g_rock2.mdl");
    } else if (!strcmp(name, "weapon_lightning")) { MODEL("progs/g_light.mdl");
    } else if (!strcmp(name, "item_weapon")) {
        if (flags & 1) MODEL(flags & 8 ? "maps/b_shell1.bsp" : "maps/b_shell0.bsp");
        if (flags & 4) MODEL(flags & 8 ? "maps/b_nail1.bsp" : "maps/b_nail0.bsp");
        if (flags & 2) MODEL(flags & 8 ? "maps/b_rock1.bsp" : "maps/b_rock0.bsp");
    } else if (!strcmp(name, "item_shells")) { MODEL(flags & 1 ? "maps/b_shell1.bsp" : "maps/b_shell0.bsp");
    } else if (!strcmp(name, "item_spikes")) { MODEL(flags & 1 ? "maps/b_nail1.bsp" : "maps/b_nail0.bsp");
    } else if (!strcmp(name, "item_rockets")) { MODEL(flags & 1 ? "maps/b_rock1.bsp" : "maps/b_rock0.bsp");
    } else if (!strcmp(name, "item_cells")) { MODEL(flags & 1 ? "maps/b_batt1.bsp" : "maps/b_batt0.bsp");
    } else if (!strcmp(name, "item_key1") || !strcmp(name, "item_key2")) {
        char path[32];
        snprintf(path, sizeof(path), "progs/%c_%c_key.mdl",
                 g->options.world_type == 0 ? 'w' : g->options.world_type == 1 ? 'm' : 'b',
                 !strcmp(name, "item_key1") ? 's' : 'g');
        MODEL(path);
        SOUND(g->options.world_type == 2 ? "misc/basekey.wav" :
              g->options.world_type == 1 ? "misc/runekey.wav" : "misc/medkey.wav");
    } else if (!strcmp(name, "item_artifact_invulnerability")) {
        MODEL("progs/invulner.mdl"); SOUND("items/protect.wav");
        SOUND("items/protect2.wav"); SOUND("items/protect3.wav");
    } else if (!strcmp(name, "item_artifact_envirosuit")) {
        MODEL("progs/suit.mdl"); SOUND("items/suit.wav"); SOUND("items/suit2.wav");
    } else if (!strcmp(name, "item_artifact_invisibility")) {
        MODEL("progs/invisibl.mdl"); SOUND("items/inv1.wav"); SOUND("items/inv2.wav"); SOUND("items/inv3.wav");
    } else if (!strcmp(name, "item_artifact_super_damage")) {
        MODEL("progs/quaddama.mdl"); SOUND("items/damage.wav"); SOUND("items/damage2.wav"); SOUND("items/damage3.wav");
    } else if (!strcmp(name, "func_door")) {
        static const char *const keys[][2] = {{"doors/medtry.wav", "doors/meduse.wav"},
            {"doors/runetry.wav", "doors/runeuse.wav"}, {"doors/basetry.wav", "doors/baseuse.wav"}};
        if (g->options.world_type >= 0 && g->options.world_type < 3)
            if (!declare_rows(g, false, keys[g->options.world_type], 2, error)) return false;
        static const char *const rows[][2] = {{"misc/null.wav", "misc/null.wav"},
            {"doors/drclos4.wav", "doors/doormv1.wav"}, {"doors/hydro1.wav", "doors/hydro2.wav"},
            {"doors/stndr1.wav", "doors/stndr2.wav"}, {"doors/ddoor1.wav", "doors/ddoor2.wav"}};
        if (sounds >= 0 && sounds < 5 && !declare_rows(g, false, rows[sounds], 2, error)) return false;
    } else if (!strcmp(name, "func_button")) {
        static const char *const rows[] = {"buttons/airbut1.wav", "buttons/switch21.wav", "buttons/switch02.wav", "buttons/switch04.wav"};
        if (sounds >= 0 && sounds < 4) SOUND(rows[sounds]);
    } else if (!strcmp(name, "func_door_secret")) {
        if (sounds == 1) { SOUND("doors/latch2.wav"); SOUND("doors/winch2.wav"); SOUND("doors/drclos4.wav"); }
        else if (sounds == 2) { SOUND("doors/airdoor1.wav"); SOUND("doors/airdoor2.wav"); }
        else if (!sounds || sounds == 3) { SOUND("doors/basesec1.wav"); SOUND("doors/basesec2.wav"); }
    } else if (!strcmp(name, "func_plat")) {
        if (sounds == 1) { SOUND("plats/plat1.wav"); SOUND("plats/plat2.wav"); }
        else if (!sounds || sounds == 2) { SOUND("plats/medplat1.wav"); SOUND("plats/medplat2.wav"); }
    } else if (!strcmp(name, "func_train") || !strcmp(name, "misc_teleporttrain")) {
        if (!strcmp(name, "misc_teleporttrain") || !sounds) SOUND("misc/null.wav");
        else if (sounds == 1) { SOUND("plats/train2.wav"); SOUND("plats/train1.wav"); }
        if (!strcmp(name, "misc_teleporttrain")) MODEL("progs/teleport.mdl");
    } else if (!strcmp(name, "trigger_secret") || !strcmp(name, "trigger_once") || !strcmp(name, "trigger_multiple")) {
        if (!strcmp(name, "trigger_secret") && !sounds) sounds = 1;
        if (sounds == 1) SOUND("misc/secret.wav");
        else if (sounds == 2) SOUND("misc/talk.wav");
        else if (sounds == 3) SOUND("misc/trigger1.wav");
    } else if (!strcmp(name, "trigger_teleport")) {
        if (!(flags & 2)) SOUND("ambience/hum1.wav");
    } else if (!strcmp(name, "trigger_push")) { SOUND("ambience/windfly.wav");
    } else if (!strcmp(name, "trigger_onlyregistered")) { SOUND("misc/talk.wav");
    } else if (!strcmp(name, "misc_explobox") || !strcmp(name, "misc_explobox2")) {
        MODEL(!strcmp(name, "misc_explobox2") ? "maps/b_exbox2.bsp" : "maps/b_explob.bsp"); SOUND("weapons/r_exp3.wav");
    } else if (!strcmp(name, "item_sigil")) {
        SOUND("misc/runekey.wav");
        for (unsigned i = 0; i < 4; ++i) if (flags & (1u << i)) {
            char path[32]; snprintf(path, sizeof(path), "progs/end%u.mdl", i + 1); MODEL(path);
        }
    } else if (!strcmp(name, "trap_spikeshooter") || !strcmp(name, "trap_shooter")) {
        if (flags & 2) { MODEL("progs/laser.mdl"); SOUND("enforcer/enfire.wav"); SOUND("enforcer/enfstop.wav"); }
        else SOUND("weapons/spike2.wav");
    } else if (!strcmp(name, "misc_fireball")) { MODEL("progs/lavaball.mdl");
    } else if (!strcmp(name, "air_bubbles")) { if (!g->options.deathmatch) MODEL("progs/s_bubble.spr");
    } else if (!strcmp(name, "light_globe")) { MODEL("progs/s_light.spr");
    } else if (!strcmp(name, "light_torch_small_walltorch") || !strcmp(name, "light_flame_large_yellow") ||
               !strcmp(name, "light_flame_small_yellow") || !strcmp(name, "light_flame_small_white")) {
        MODEL(!strcmp(name, "light_torch_small_walltorch") ? "progs/flame.mdl" : "progs/flame2.mdl"); SOUND("ambience/fire1.wav");
    } else if (!strcmp(name, "light_fluoro")) { SOUND("ambience/fl_hum1.wav");
    } else if (!strcmp(name, "light_fluorospark")) { SOUND("ambience/buzz1.wav");
    } else if (!strcmp(name, "ambient_suck_wind")) { SOUND("ambience/suck1.wav");
    } else if (!strcmp(name, "ambient_flouro_buzz")) { SOUND("ambience/buzz1.wav");
    } else if (!strcmp(name, "ambient_drip")) { SOUND("ambience/drip1.wav");
    } else if (!strcmp(name, "ambient_thunder")) { SOUND("ambience/thunder1.wav");
    } else if (!strcmp(name, "ambient_light_buzz")) { SOUND("ambience/fl_hum1.wav");
    } else if (!strcmp(name, "ambient_swamp1")) { SOUND("ambience/swamp1.wav");
    } else if (!strcmp(name, "ambient_swamp2")) { SOUND("ambience/swamp2.wav");
    } else if (!strcmp(name, "ambient_drone")) { SOUND("ambience/drone6.wav");
    } else if (!strcmp(name, "ambient_comp_hum")) { SOUND("ambience/comp1.wav");
    } else if (!strcmp(name, "viewthing")) { MODEL("progs/player.mdl");
    } else if (!strcmp(name, "misc_noisemaker")) {
        static const char *const rows[] = {"enforcer/enfire.wav", "enforcer/enfstop.wav", "enforcer/sight1.wav",
            "enforcer/sight2.wav", "enforcer/sight3.wav", "enforcer/sight4.wav", "enforcer/pain1.wav",
            "enforcer/pain2.wav", "enforcer/death1.wav", "enforcer/idle1.wav"};
        return declare_rows(g, false, rows, sizeof(rows)/sizeof(*rows), error);
    }
    return true;
}
#undef MODEL
#undef SOUND
