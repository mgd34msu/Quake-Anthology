#include "original_internal.h"
#include "qa/game_q2_original_save.h"
#include "original_symbols.h"
#include "original_edicts.h"
#include "original_entities.h"
#include "original_monsters.h"
#include "original_items.h"
#include "original_projectiles.h"
#include "original_trails.h"
#include "internal.h"

static bool level_copy_text(char *out, size_t capacity, const char *text, qa_error *error)
{
    size_t size = strlen(text);
    if (size >= capacity) {
        qa_error_set(error, QA_ERROR_FORMAT, size, "Original Q2 level text exceeds its Source field");
        return false;
    }
    memcpy(out, text, size + 1);
    return true;
}

static bool level_error(qa_error *error, size_t offset, const char *message)
{
    qa_error_set(error, QA_ERROR_FORMAT, offset, "%s", message);
    return false;
}

static bool string_extent(qa_bytes fields, uint16_t member, qa_bytes bytes,
    size_t *position, qa_error *error)
{
    if (member > fields.size || 4 > fields.size - member)
        return level_error(error, member, "Original Q2 string member is outside its edict");
    int32_t length;
    uint32_t word = qa_load_u32le(fields.data + member);
    memcpy(&length, &word, sizeof(length));
    if (length < 0 || *position > bytes.size || (size_t)length > bytes.size - *position)
        return level_error(error, *position, "Truncated original Q2 string tail");
    if (length && (bytes.data[*position + (size_t)length - 1] ||
        memchr(bytes.data + *position, 0, (size_t)length - 1)))
        return level_error(error, *position, "Original Q2 string tail has an invalid terminator");
    *position += (size_t)length;
    return true;
}

void q2_original_level_close(q2_original_level_file *file)
{
    if (!file) return;
    qa_json_destroy(file->document);
    free(file->rows);
    *file = (q2_original_level_file){0};
}

static bool level_row(q2_original_level_file *file, size_t *capacity,
    q2_original_edict_row row, qa_error *error)
{
    if (file->count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 32;
        if (next < *capacity || next > SIZE_MAX / sizeof(*file->rows)) {
            qa_error_set(error, QA_ERROR_MEMORY, file->count, "Original Q2 live edict extent overflows");
            return false;
        }
        void *rows = realloc(file->rows, next * sizeof(*file->rows));
        if (!rows) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Reading original Q2 live edict rows");
            return false;
        }
        file->rows = rows;
        *capacity = next;
    }
    file->rows[file->count++] = row;
    return true;
}

static int level_row_order(const void *left, const void *right)
{
    uint32_t a = ((const q2_original_edict_row *)left)->number;
    uint32_t b = ((const q2_original_edict_row *)right)->number;
    return (a > b) - (a < b);
}

bool q2_original_level_open(qa_q2_game *game, qa_bytes bytes,
    q2_original_level_file *out, qa_error *error)
{
    if (!game || !out || !bytes.data || !bytes.size ||
        (unsigned)game->options.product > QA_Q2_N64 ||
        (game->options.edition == QA_Q2_CLASSIC && game->options.product > QA_Q2_ROGUE)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original Q2 LEVEL requires its selected GAME and file");
        return false;
    }
    q2_original_level_file file = {0};
    q2_original_record_io io = {.reading = true, .edition = game->options.edition,
        .product = game->options.product, .error = error};
    size_t capacity = 0;
    bool okay = true;
    if (io.edition == QA_Q2_RERELEASE) {
        okay = qa_json_parse(bytes, &file.document, error);
        if (!okay) goto done;
        qa_json_id root = qa_json_root(file.document);
        file.level_object = qa_json_get(file.document, root, "level");
        qa_json_id entities = qa_json_get(file.document, root, "entities");
        if (qa_json_type(file.document, file.level_object) != QA_JSON_OBJECT ||
            qa_json_type(file.document, entities) != QA_JSON_OBJECT) {
            okay = level_error(error, 0, "Original rerelease LEVEL requires level and entities objects");
            goto done;
        }
        io.document = file.document;
        io.object = file.level_object;
        okay = q2_original_record(&io, Q2_ORIGINAL_LEVEL, &file.state);
        for (size_t i = 0; okay && i < qa_json_size(file.document, entities); ++i) {
            qa_buffer key = {0};
            okay = qa_json_string(file.document, qa_json_key_at(file.document, entities, i),
                &key, error);
            if (!okay) break;
            uint32_t number = 0;
            if (!key.size) okay = false;
            for (size_t j = 0; okay && j < key.size; ++j) {
                uint8_t byte = key.data[j];
                if (byte < '0' || byte > '9' || number > (UINT32_MAX - (uint32_t)(byte - '0')) / 10)
                    okay = false;
                else number = number * 10 + (uint32_t)(byte - '0');
            }
            qa_buffer_free(&key);
            qa_json_id object = qa_json_at(file.document, entities, i);
            if (!okay || number >= game->wire_capacity ||
                qa_json_type(file.document, object) != QA_JSON_OBJECT) {
                okay = level_error(error, i, "Original Q2 entity requires its physical edict number and object");
                break;
            }
            okay = level_row(&file, &capacity,
                (q2_original_edict_row){.number = number, .object = object}, error);
        }
    } else {
        size_t edict_size = q2_original_entity_size(game->options.product);
        size_t level_size = q2_original_level_size(game->options.product);
        const q2_original_library *library = q2_original_library_for(game->options.product);
        if (bytes.size < 8 + level_size || qa_load_u32le(bytes.data) != edict_size) {
            okay = level_error(error, 0, "Original Q2 LEVEL edict layout differs from its selected Source");
            goto done;
        }
        if (qa_load_u32le(bytes.data + 4) != library->init_game) {
            qa_error_set(error, QA_ERROR_UNSUPPORTED, 4,
                "Original Q2 LEVEL callbacks have no proved module identity");
            okay = false;
            goto done;
        }
        file.level_fields = (qa_bytes){bytes.data + 8, level_size};
        io.input = file.level_fields;
        okay = q2_original_record(&io, Q2_ORIGINAL_LEVEL, &file.state);
        if (!okay) goto done;
        file.state.frame = qa_load_u32le(file.level_fields.data);
        file.state.health_image = (int32_t)qa_load_u32le(file.level_fields.data + 264);
        size_t position = 8 + level_size, start = position;
        okay = string_extent(file.level_fields, 204, bytes, &position, error);
        if (!okay) goto done;
        file.level_strings = (qa_bytes){bytes.data + start, position - start};
        bool terminal = false;
        while (okay && position < bytes.size) {
            if (4 > bytes.size - position) {
                okay = level_error(error, position, "Truncated original Q2 physical edict number");
                break;
            }
            uint32_t number = qa_load_u32le(bytes.data + position);
            position += 4;
            if (number == UINT32_MAX) { terminal = true; break; }
            if (number >= game->wire_capacity || edict_size > bytes.size - position) {
                okay = level_error(error, position, "Original Q2 physical edict is outside its actual GAME table");
                break;
            }
            q2_original_edict_row row = {.number = number,
                .fields = {bytes.data + position, edict_size}};
            position += edict_size;
            start = position;
            for (size_t i = 0; okay && i < sizeof(q2_original_edict_strings) / sizeof(q2_original_edict_strings[0]); ++i)
                okay = string_extent(row.fields, q2_original_edict_strings[i].offset, bytes, &position, error);
            row.strings = (qa_bytes){bytes.data + start, position - start};
            if (okay) okay = level_row(&file, &capacity, row, error);
        }
        if (okay && (!terminal || position != bytes.size))
            okay = level_error(error, position, "Original Q2 LEVEL lacks its final sentinel or has trailing bytes");
    }
    if (okay && file.count > 1) qsort(file.rows, file.count, sizeof(*file.rows), level_row_order);
    for (size_t i = 1; okay && i < file.count; ++i)
        if (file.rows[i - 1].number == file.rows[i].number)
            okay = level_error(error, i, "Original Q2 LEVEL edicts are duplicated");
    if (okay && (!file.count || file.rows[0].number != 0))
        okay = level_error(error, 0, "Original Q2 LEVEL has no actual world edict");
done:
    if (!okay) q2_original_level_close(&file);
    else *out = file;
    return okay;
}

const q2_original_edict_row *q2_original_level_actor(const q2_original_level_file *file,
    uint32_t number)
{
    if (!file) return NULL;
    size_t low = 0, high = file->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (file->rows[middle].number < number) low = middle + 1;
        else high = middle;
    }
    return low < file->count && file->rows[low].number == number ? file->rows + low : NULL;
}

static q2_original_record_io edict_reader(qa_q2_game *game,
    const q2_original_level_file *file, const q2_original_edict_row *row, qa_error *error)
{
    return (q2_original_record_io){.reading = true, .edition = game->options.edition,
        .product = game->options.product, .document = file->document,
        .object = row->object, .input = row->fields, .strings = row->strings, .error = error};
}

static bool original_edict(qa_q2_game *, q2_original_record_io *, q2_actor *,
    const qa_q2_save_level *, qa_error *);
static bool level_perception(qa_q2_game *, q2_original_record_io *);

bool qa_q2_game_original_read_client(qa_q2_game *game, uint32_t slot,
    qa_actor_id actor, qa_bytes game_bytes, qa_bytes level_bytes,
    const qa_q2_save_level *engine_level, bool *restored, qa_error *error)
{
    q2_actor *source = game ? q2_actor_get(game, actor, false, NULL) : NULL;
    if (!game || !restored || !source || !source->client ||
        slot >= game->wire_clients || !source->wire_bound || source->wire_slot != slot + 1 ||
        !q2_checkpoint_idle(game, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot,
            "Original Q2 client requires its admitted physical slot and GAME owner");
        return false;
    }
    qa_json_document *document = NULL;
    q2_original_record_io io;
    q2_original_game_state globals = {0};
    q2_original_client_state client = {0};
    q2_original_level_file level = {0};
    bool live_level = false;
    q2_original_record_io client_io = {0};
    bool okay = q2_original_game_open(game, game_bytes, &document, &io, &globals, error);
    if (okay && slot >= globals.clients)
        okay = level_error(error, slot, "Original Q2 client is outside its GAME file");
    if (okay) {
        if (game->options.edition == QA_Q2_RERELEASE) {
            qa_json_id clients = qa_json_get(document, qa_json_root(document), "clients");
            io.object = qa_json_at(document, clients, slot);
        } else {
            size_t stride = q2_original_client_size(game->options.product);
            io.input = (qa_bytes){game_bytes.data + 16 + 1564 + (size_t)slot * stride, stride};
        }
        okay = q2_original_client_record(game, &io, engine_level, &client);
        client_io = io;
    }
    qa_body_state body = {0};
    int32_t view_height = 22, health = 0, maximum_health = 0, dead = 0, motion = 0;
    uint64_t flags = client.persistent.flags;
    if (okay && level_bytes.size) {
        okay = q2_original_level_open(game, level_bytes, &level, error);
        const q2_original_edict_row *row = okay ? q2_original_level_actor(&level, slot + 1) : NULL;
        if (row) {
            io = edict_reader(game, &level, row, error);
            okay = q2_original_body(game, &io, &body) &&
                q2_original_scalar(&io, "viewheight", Q2_ORIGINAL_I32, 508, 508, 508, &view_height) &&
                q2_original_scalar(&io, "health", Q2_ORIGINAL_I32, 480, 480, 480, &health) &&
                q2_original_scalar(&io, "max_health", Q2_ORIGINAL_I32, 484, 484, 484, &maximum_health) &&
                q2_original_scalar(&io, "deadflag", Q2_ORIGINAL_I32, 492, 492, 492, &dead) &&
                q2_original_scalar(&io, "movetype", Q2_ORIGINAL_I32, 260, 260, 260, &motion) &&
                q2_original_scalar(&io, "flags", Q2_ORIGINAL_U64, 264, 264, 264, &flags);
            live_level = okay;
        }
    }
    if (okay) {
        const q2_client_state *current = source->client;
        /* Client admission owns the current seat, character and service bindings.
         * The original file owns the player's gameplay fields and inventory. */
        client.player.info.slot = current->info.slot;
        client.player.info.seat = current->info.seat;
        client.player.info.connected = true;
        client.player.info.view_height = (float)view_height;
        client.player.info.dead = dead != 0;
        client.player.info.noclip = motion == 1;
        client.player.info.god = (flags & 16u) != 0;
        client.player.info.notarget = (flags & 32u) != 0;
        snprintf(client.player.info.skin, sizeof(client.player.info.skin), "%s", q2_player_source_skin(game, current));
        client.player.rule.use_weapons = current->rule.use_weapons;
        client.player.rule.use_inventory = current->rule.use_inventory;
        client.player.bot = current->player->bot;
        client.player.rule.gender = current->rule.gender;
        client.player.rule.visual = current->rule.visual;
        client.player.rule.character_configured = current->rule.character_configured;
        client.player.rule.character_model = current->rule.character_model;
        client.player.rule.character_skin = current->rule.character_skin;
        client.player.rule.spawned = live_level;
        client.player.rule.pending_start_items = false;
        client.player.info.chase_target = (qa_actor_id){0};
        client.persistent.flags = (uint32_t)flags;
        if (live_level) {
            client.persistent.health = (float)health;
            client.persistent.maximum_health = (float)maximum_health;
        }
        qa_q2_player_checkpoint saved = {.present = true, .value = client.player};
        okay = qa_q2_player_restore(game, actor, &saved, error) &&
            qa_q2_player_carry_restore(game, actor, &client.persistent, error);
        if (okay && source->weapon_bound)
            okay = qa_q2_weapon_restore(game, actor, &client.weapon, error);
        if (okay) {
            source->silencer = client.silencer;
            if (source->powers) {
                source->powers->values = client.powers;
                if (game->options.edition == QA_Q2_CLASSIC && game->options.product == QA_Q2_ROGUE)
                    source->powers->sphere = client.sphere;
            }
        }
        if (okay && live_level) {
            client.movement.bounds = body.bounds;
            client.movement.view_height = (float)view_height;
            okay = qa_q2_player_movement_restore(game, actor, &body, &client.movement, &client.command_angles, error);
            if (okay) {
                source->wire_view = (qa_q2_wire_view){.present = true, .view = client.view,
                    .frame = game->wire_frame, .time_ns = game->now_ns};
            }
            const q2_original_edict_row *row = q2_original_level_actor(&level, slot + 1);
            io = edict_reader(game, &level, row, error);
            if (okay) okay = original_edict(game, &io, source, engine_level, error);
        }
    }
    /* Original clients are admitted later than map edicts. Rejoin pointers
     * from the same original LEVEL bytes; no second pending state owner. */
    for (size_t i = 0; okay && i < level.count; ++i) {
        const q2_original_edict_row *row = level.rows + i;
        q2_actor *target = q2_actor_get(game, game->wire_actors[row->number], false, NULL);
        if (!target) continue;
        io = edict_reader(game, &level, row, error);
        io.references_only = true;
        okay = original_edict(game, &io, target, engine_level, error);
        if (okay && (!target->client || target->client->rule.corpse)) {
            io.references_only = false;
            okay = q2_original_edict_visual(game, &io, target, engine_level, error);
        }
    }
    if (okay) okay = q2_original_trail_client(game, &client_io, actor);
    if (okay && live_level) {
        io = (q2_original_record_io){.reading = true, .edition = game->options.edition,
            .product = game->options.product, .document = level.document, .object = level.level_object,
            .input = level.level_fields, .strings = level.level_strings, .error = error};
        okay = level_perception(game, &io);
    }
    qa_json_destroy(document);
    q2_original_level_close(&level);
    q2_original_client_free(&client);
    if (okay) *restored = live_level;
    return okay;
}

static bool source_strings(qa_q2_game *g, q2_original_record_io *io, q2_actor *actor)
{
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), actor->id);
    qa_entity_visual visual = {0};
    (void)qa_q2_presentation_read(g, actor->id, &visual);
    for (size_t i = 0; i < sizeof(q2_original_edict_strings) / sizeof(q2_original_edict_strings[0]); ++i) {
        const q2_original_string_field *field = q2_original_edict_strings + i;
        qa_string_id key = qa_strings_find(qa_session_strings(g->services.session),
            (qa_bytes){(const uint8_t *)field->name, strlen(field->name)});
        qa_string_id value = q2_actor_field(g, actor->id, key);
        if (i == 1 && !value && actor->entity && actor->entity->has_inline) value = visual.models[0];
        if (i == 0) value = actor->projectile.classname ? actor->projectile.classname :
            actor->entity ? actor->entity->classname : actor->item && actor->item->definition ?
            actor->item->definition->classname_id : record ? record->definition : 0;
        if (i == 0 && actor->item && actor->item->companion &&
            actor->item->companion->kind == Q2_DOPPLEGANGER_BODY &&
            !qa_builtin_resource(&g->services, "noclass", &value, io->error)) return false;
        else if (actor->entity) {
            if (i == 2) value = actor->entity->target;
            else if (i == 3) value = actor->entity->targetname;
            else if (i == 6) value = actor->entity->killtarget;
            else if (i == 8) value = actor->entity->message;
            else if (i == 9) value = actor->entity->team;
            else if (i == 10) value = actor->entity->map;
        } else if (actor->item) {
            if (i == 2) value = actor->item->spawn.target;
            else if (i == 6) value = actor->item->spawn.killtarget;
            else if (i == 8) value = actor->item->spawn.message;
            else if (i == 9) value = actor->item->spawn.team;
        }
        if (i == 7 && actor->monster) value = actor->monster->combat_target;
        if (!q2_original_string(g, io, field->name, field->offset, &value)) return false;
    }
    return true;
}

static bool original_edict(qa_q2_game *g, q2_original_record_io *io,
    q2_actor *actor, const qa_q2_save_level *engine, qa_error *error)
{
    bool trail;
    if (!q2_original_edict_record(g, io, actor, engine, error) ||
        !q2_original_trail_record(g, io, actor, &trail)) return false;
    if (trail) return !io->reading ? source_strings(g, io, actor) :
        io->references_only || q2_original_edict_visual(g, io, actor, engine, error);
    if (!q2_original_monster_record(g, io, actor, engine, error) ||
        !q2_original_item_record(g, io, actor, engine, error) ||
        !q2_original_projectile_record(g, io, actor, engine, error) ||
        !q2_original_entity_record(g, io, actor, engine, error)) return false;
    if (!io->reading) return source_strings(g, io, actor);
    if (io->references_only) return true;
    return q2_original_edict_visual(g, io, actor, engine, error);
}

static bool level_perception(qa_q2_game *g, q2_original_record_io *io)
{
    q2_players *players = g->player_runtime;
    if (io->edition != QA_Q2_CLASSIC) return true;
    qa_actor_id sight = {0}, observer = {0};
    int32_t sight_frame = 0;
    qa_q2_monsters_checkpoint perception = {0};
    if (!qa_q2_monsters_capture(g, &perception, io->error)) return false;
    if (!io->reading) {
        if (!q2_resolve_reference(g, perception.sight_client, &sight, io->error) ||
            !q2_resolve_reference(g, perception.sight_observer, &observer, io->error)) {
            qa_q2_monsters_checkpoint_free(&perception);
            return false;
        }
        sight_frame = (int32_t)(perception.sight_time_ns / (Q2_NS / 10));
    }
    bool okay = q2_original_reference(g, io, "sight_client", 236, &sight) &&
        q2_original_reference(g, io, "sight_entity", 240, &observer) &&
        q2_original_scalar(io, "sight_entity_framenum", Q2_ORIGINAL_I32, 244, 244, 244, &sight_frame);
    if (okay && io->reading) {
        okay = q2_save_reference(g, sight, &perception.sight_client, io->error) &&
            q2_save_reference(g, observer, &perception.sight_observer, io->error);
        perception.sight_time_ns = sight_frame > 0 ?
            (uint64_t)(uint32_t)sight_frame * (Q2_NS / 10) : 0;
        perception.last_frame_ns = g->now_ns;
        perception.began_frame = false;
        if (okay) okay = qa_q2_monsters_restore(g, &perception, io->error);
    }
    qa_q2_monsters_checkpoint_free(&perception);
    if (!okay) return false;
    for (unsigned i = 0; i < 2; ++i) {
        qa_actor_id sound = {0};
        if (!io->reading && players->noise[i].present) {
            q2_actor *owner = q2_actor_get(g, players->noise[i].owner, false, NULL);
            if (owner && owner->client) sound = owner->client->rule.noise[i];
        }
        int32_t sound_frame = io->reading ? 0 :
            (int32_t)(players->noise[i].time_ns / (Q2_NS / 10));
        uint16_t pointer = (uint16_t)(248 + i * 8), frame = (uint16_t)(252 + i * 8);
        if (!q2_original_reference(g, io, i ? "sound2_entity" : "sound_entity", pointer, &sound) ||
            !q2_original_scalar(io, i ? "sound2_entity_framenum" : "sound_entity_framenum",
                Q2_ORIGINAL_I32, frame, frame, frame, &sound_frame)) return false;
        if (io->reading) {
            qa_body_state body = {0};
            qa_actor_id owner = {0};
            if (sound.registry) {
                if (!qa_world_body_read(g->services.world, sound, &body, io->error)) return false;
                q2_actor *noise = q2_actor_get(g, sound, false, NULL);
                if (noise && noise->entity) owner = noise->entity->owner;
            }
            players->noise[i] = (qa_q2_player_noise_record){.owner = owner, .origin = body.origin,
                .time_ns = sound_frame > 0 ? (uint64_t)(uint32_t)sound_frame * (Q2_NS / 10) : 0,
                .present = sound.registry != 0};
        }
    }
    return true;
}

static bool level_state(qa_q2_game *g, q2_original_record_io *io,
    const qa_q2_save_level *engine, q2_original_level_state *state)
{
    q2_players *players = g->player_runtime;
    q2_entities *entities = g->entity_runtime;
    if (!io->reading) {
        if (g->wire_frame > UINT32_MAX) return level_error(io->error, 0,
            "Q2 Source frame exceeds the original level counter");
        *state = (q2_original_level_state){.frame = (uint32_t)g->wire_frame, .time_ns = g->now_ns,
            .intermission_ns = players->intermission_ns, .exit_intermission = players->exit,
            .intermission_origin = players->camera_origin, .intermission_angles = players->camera_angles,
            .body_queue = players->corpse_index, .restart_ns = players->restart_ns,
            .total_secrets = entities->total_secrets, .found_secrets = entities->found_secrets,
            .total_goals = entities->total_goals, .found_goals = entities->found_goals,
            .total_monsters = entities->total_monsters, .killed_monsters = entities->killed_monsters,
            .autosave_ns = entities->last_autosave_ns,
            .power_cubes = g->item_runtime ? g->item_runtime->cubes : 0};
        const char *map = engine && engine->name[0] ? engine->name : players->rules.map_name;
        const char *name = engine ? qa_q2_save_configstring(engine, 0) : NULL;
        if ((map && !level_copy_text(state->map, sizeof(state->map), map, io->error)) ||
            (name && !level_copy_text(state->name, sizeof(state->name), name, io->error))) return false;
    }
    if (!q2_original_record(io, Q2_ORIGINAL_LEVEL, state)) return false;
    if (io->edition == QA_Q2_CLASSIC &&
        !q2_original_scalar(io, "framenum", Q2_ORIGINAL_U32, 0, 0, 0, &state->frame)) return false;
    qa_string_id changemap = io->reading ? QA_STRING_NONE : players->next_map;
    if (!q2_original_string(g, io, "changemap", 204, &changemap)) return false;
    if (!level_perception(g, io)) return false;
    if (io->reading) {
        if (state->body_queue >= 8 || state->found_secrets > state->total_secrets ||
            state->found_goals > state->total_goals)
            return level_error(io->error, 0, "Original Q2 level has invalid gameplay counters");
        players->next_map = changemap;
        players->intermission = state->intermission_ns != 0;
        players->intermission_ns = state->intermission_ns; players->exit = state->exit_intermission;
        players->camera_origin = state->intermission_origin; players->camera_angles = state->intermission_angles;
        players->camera_set = players->intermission; players->corpse_index = state->body_queue;
        players->restart_ns = state->restart_ns;
        entities->total_secrets = state->total_secrets; entities->found_secrets = state->found_secrets;
        entities->total_goals = state->total_goals; entities->found_goals = state->found_goals;
        entities->total_monsters = state->total_monsters; entities->killed_monsters = state->killed_monsters;
        entities->last_autosave_ns = state->autosave_ns;
        if (g->item_runtime) g->item_runtime->cubes = state->power_cubes;
        for (q2_actor *actor = g->first_actor; actor; actor = actor->live_next)
            if (actor->entity && actor->entity->kind == Q2E_CAMERA && actor->entity->q64)
                actor->entity->q64->angles = state->intermission_angles;
    }
    return true;
}

/* WriteField1 translates NULL Source edict/item pointers to -1. The record
 * starts with those values even when its native owner has no such member. */
static void classic_null_references(qa_buffer fields, qa_q2_product product, bool level)
{
    static const uint16_t edict[] = {256, 412, 416, 536, 540, 544, 548, 552,
        560, 564, 568, 572, 580, 648};
    static const uint16_t rogue[] = {900, 908, 912, 944, 976, 1020, 1024, 1028, 1032};
    static const uint16_t globals[] = {236, 240, 248, 256};
    const uint16_t *offsets = level ? globals : edict;
    size_t count = level ? sizeof(globals) / sizeof(globals[0]) : sizeof(edict) / sizeof(edict[0]);
    for (size_t i = 0; i < count; ++i) qa_store_u32le(fields.data + offsets[i], UINT32_MAX);
    if (product == QA_Q2_ROGUE) {
        if (level) qa_store_u32le(fields.data + 304, UINT32_MAX);
        else for (size_t i = 0; i < sizeof(rogue) / sizeof(rogue[0]); ++i)
            qa_store_u32le(fields.data + rogue[i], UINT32_MAX);
    }
}

static bool write_level(qa_q2_game *g, bool transition,
    const qa_q2_save_level *engine, qa_buffer *out, qa_error *error)
{
    q2_original_record_io io = {.edition = g->options.edition, .product = g->options.product, .error = error};
    q2_save_io raw = {.game = g, .error = error};
    qa_json_writer writer = {0};
    qa_buffer fields = {0};
    bool okay = true;
    if (io.edition == QA_Q2_CLASSIC) {
        size_t size = q2_original_entity_size(io.product);
        uint32_t edict_size = (uint32_t)size, init_game = q2_original_library_for(io.product)->init_game;
        okay = q2_save_u32(&raw, &edict_size) && q2_save_u32(&raw, &init_game);
        fields.size = q2_original_level_size(io.product);
        fields.data = calloc(size > fields.size ? size : fields.size, 1);
        if (!fields.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Writing original Q2 LEVEL fields"); okay = false; }
        io.output = fields;
        if (okay) classic_null_references(fields, io.product, true);
    } else {
        io.writer = &writer;
        qa_json_writer_object(&writer);
        qa_json_writer_key(&writer, "save_version"); qa_json_writer_number(&writer, 1);
        qa_json_writer_key(&writer, "level"); qa_json_writer_object(&writer);
    }
    q2_save_io strings = {.game = g, .error = error};
    io.string_tail = &strings;
    q2_original_level_state state = {0};
    if (okay) okay = level_state(g, &io, engine, &state);
    if (okay && io.edition == QA_Q2_CLASSIC)
        okay = q2_save_raw(&raw, fields.data, fields.size) && q2_save_raw(&raw, strings.output.data, strings.output.size);
    qa_buffer_free(&strings.output); strings = (q2_save_io){.game = g, .error = error};
    io.string_tail = &strings;
    if (io.edition == QA_Q2_RERELEASE) {
        qa_json_writer_end(&writer); qa_json_writer_key(&writer, "entities"); qa_json_writer_object(&writer);
    }
    for (uint32_t slot = 0; okay && slot < g->wire_extent; ++slot) {
        q2_actor *actor = q2_actor_get(g, g->wire_actors[slot], false, NULL);
        if (!actor || (transition && slot > 0 && slot <= g->wire_clients)) continue;
        if (io.edition == QA_Q2_CLASSIC) {
            okay = q2_save_u32(&raw, &slot);
            fields.size = q2_original_entity_size(io.product);
            memset(fields.data, 0, fields.size); io.output = fields;
            classic_null_references(fields, io.product, false);
            uint32_t number = slot; bool live = true;
            okay = okay && q2_original_scalar(&io, "s.number", Q2_ORIGINAL_U32, 0, 0, 0, &number) &&
                q2_original_scalar(&io, "inuse", Q2_ORIGINAL_BOOL, 88, 88, 88, &live);
        } else {
            char number[11]; int count = snprintf(number, sizeof(number), "%u", slot);
            if (count < 0 || (size_t)count >= sizeof(number)) { okay = level_error(error, slot, "Original Q2 physical number exceeds its field"); break; }
            qa_json_writer_key(&writer, number); qa_json_writer_object(&writer);
            uint32_t saved_number = slot; bool live = true;
            okay = q2_original_scalar(&io, "s.number", Q2_ORIGINAL_U32, 0, 0, 0, &saved_number) &&
                q2_original_scalar(&io, "inuse", Q2_ORIGINAL_BOOL, 88, 88, 88, &live);
        }
        if (okay) okay = original_edict(g, &io, actor, engine, error);
        if (okay && io.edition == QA_Q2_CLASSIC)
            okay = q2_save_raw(&raw, fields.data, fields.size) &&
                q2_save_raw(&raw, strings.output.data, strings.output.size);
        strings.offset = strings.output.size = 0;
        if (io.edition == QA_Q2_RERELEASE) qa_json_writer_end(&writer);
    }
    if (okay && io.edition == QA_Q2_CLASSIC) {
        int32_t terminal = -1; okay = q2_save_i32(&raw, &terminal);
    } else if (okay) {
        qa_json_writer_end(&writer); qa_json_writer_end(&writer);
        okay = qa_json_writer_finish(&writer, &raw.output, error);
    }
    qa_json_writer_destroy(&writer); qa_buffer_free(&strings.output); qa_buffer_free(&fields);
    if (!okay) { qa_buffer_free(&raw.output); return false; }
    *out = raw.output;
    return true;
}

static bool original_player_models(qa_q2_game *game, qa_q2_save_level *engine, qa_error *error)
{
    if (game->options.edition != QA_Q2_CLASSIC) return true;
    /* SP_worldspawn order is also the high byte of s.skinnum. */
    static const char *const base[] = {"#w_blaster.md2", "#w_shotgun.md2", "#w_sshotgun.md2",
        "#w_machinegun.md2", "#w_chaingun.md2", "#a_grenades.md2", "#w_glauncher.md2",
        "#w_rlauncher.md2", "#w_hyperblaster.md2", "#w_railgun.md2", "#w_bfg.md2"};
    static const char *const xatrix[] = {"#w_phalanx.md2", "#w_ripper.md2"};
    static const char *const rogue[] = {"#w_disrupt.md2", "#w_etfrifle.md2", "#w_plasma.md2",
        "#w_plauncher.md2", "#w_chainfist.md2"};
    const char *const *extra = game->options.product == QA_Q2_XATRIX ? xatrix : rogue;
    size_t more = game->options.product == QA_Q2_XATRIX ? sizeof(xatrix) / sizeof(xatrix[0]) :
        game->options.product == QA_Q2_ROGUE ? sizeof(rogue) / sizeof(rogue[0]) : 0;
    size_t count = sizeof(base) / sizeof(base[0]) + more, ordinal = 0;
    uint32_t last = 0;
    for (uint32_t i = 1; i < 256; ++i) {
        const char *entry = qa_q2_save_configstring(engine, 32 + i);
        if (*entry) last = i;
        if (*entry != '#') continue;
        const char *expected = ordinal < sizeof(base) / sizeof(base[0]) ? base[ordinal] :
            ordinal < count ? extra[ordinal - sizeof(base) / sizeof(base[0])] : NULL;
        if (!expected || strcmp(entry, expected))
            return level_error(error, i, "Q2 original player model order differs from SP_worldspawn");
        ++ordinal;
    }
    while (ordinal < count) {
        if (++last >= 256) return level_error(error, last, "Q2 original player models exceed CS_MODELS");
        const char *name = ordinal < sizeof(base) / sizeof(base[0]) ? base[ordinal] :
            extra[ordinal - sizeof(base) / sizeof(base[0])];
        if (!qa_q2_save_configstring_set(engine, 32 + last, name, error)) return false;
        ++ordinal;
    }
    return true;
}

bool qa_q2_game_original_capture(qa_q2_game *game, bool autosave, bool transition,
    qa_q2_save_level *engine, qa_buffer *game_out, qa_buffer *level_out, qa_error *error)
{
    if (!game || !game_out || !level_out || !engine || !game->player_runtime || !game->entity_runtime ||
        !q2_checkpoint_idle(game, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original Q2 save requires its completed GAME and engine table");
        return false;
    }
    qa_buffer game_bytes = {0}, level_bytes = {0};
    bool okay = original_player_models(game, engine, error) &&
        (transition || q2_original_write_game(game, autosave, engine, &game_bytes, error));
    if (okay && !autosave) okay = write_level(game, transition, engine, &level_bytes, error);
    if (!okay) { qa_buffer_free(&game_bytes); qa_buffer_free(&level_bytes); return false; }
    *game_out = game_bytes; *level_out = level_bytes;
    return true;
}

static bool level_clock(qa_q2_game *g, const q2_original_level_state *state, qa_error *error)
{
    qa_clock_state clock;
    qa_clock_config recipe;
    uint64_t order;
    if (!qa_session_clock(g->services.session, g->options.owner, &clock) ||
        !qa_session_component_recipe(g->services.session, g->options.owner, &recipe, &order) ||
        state->time_ns < recipe.initial_time_ns)
        return level_error(error, 0, "Original Q2 level has no admitted native Source clock");
    uint64_t duration = recipe.interval_ns ? recipe.interval_ns : Q2_NS / 10;
    uint64_t number = g->options.edition == QA_Q2_RERELEASE ? state->time_ns / duration : state->frame;
    clock.elapsed_ns = state->time_ns - recipe.initial_time_ns;
    clock.debt_ns = 0; clock.frame_number = number;
    clock.frame = (qa_source_frame){.provider = g->options.owner, .kind = recipe.kind,
        .phase = QA_FRAME_EXIT, .number = number, .time_ns = state->time_ns,
        .elapsed_ns = state->time_ns < duration ? state->time_ns : duration};
    clock.frame.start_ns = clock.frame.time_ns - clock.frame.elapsed_ns;
    if (!qa_session_restore_clock(g->services.session, g->options.owner, &clock, error)) return false;
    g->now_ns = state->time_ns; g->frame_ns = duration; g->wire_frame = number;
    return true;
}

static bool level_classname(qa_q2_game *g, const q2_original_level_file *file,
    const q2_original_edict_row *row, qa_string_id *classname, qa_error *error)
{
    q2_original_record_io io = edict_reader(g, file, row, error);
    return q2_original_string(g, &io, "classname", 280, classname) &&
        (*classname || level_error(error, row->number, "Original Q2 live edict has no actual classname"));
}

static bool level_admit(qa_q2_game *g, const q2_original_level_file *file, qa_error *error)
{
    /* Keep genuine authored owners only when the original physical slot names
     * the same class. ReadLevel replaces the rest, without executing SP_* again. */
    for (uint32_t slot = 0; slot < g->wire_extent; ++slot) {
        if (slot > 0 && slot <= g->wire_clients) continue;
        qa_actor_id id = g->wire_actors[slot];
        if (!id.registry) continue;
        const q2_original_edict_row *row = q2_original_level_actor(file, slot);
        qa_string_id classname = 0;
        if (row && !level_classname(g, file, row, &classname, error)) return false;
        const qa_actor_record *actual = qa_actors_get(qa_session_actors(g->services.session), id);
        if (!row || !actual || actual->definition != classname) {
            if (!qa_session_release(g->services.session, id, error)) return false;
        } else if (!qa_world_unlink(g->services.world, id, error)) return false;
    }
    for (size_t i = 0; i < file->count; ++i) {
        const q2_original_edict_row *row = file->rows + i;
        if (row->number > 0 && row->number <= g->wire_clients) continue;
        qa_string_id classname;
        if (!level_classname(g, file, row, &classname, error)) return false;
        qa_actor_id id = g->wire_actors[row->number];
        if (!id.registry) {
            qa_body_state body = {0};
            q2_original_record_io io = edict_reader(g, file, row, error);
            if (!q2_original_body(g, &io, &body) ||
                !qa_builtin_spawn_actor(&g->services, &(qa_builtin_spawn){.owner = g->options.owner,
                    .definition = classname, .has_source = true, .source_slot = row->number,
                    .body = body}, &id, error)) return false;
        }
        q2_actor *actor = q2_actor_get(g, id, true, error);
        if (!actor || !q2_wire_bind(g, actor, row->number, error)) return false;
        const char *name = qa_strings_cstr(qa_session_strings(g->services.session), classname);
        if (name && !strcmp(name, "bodyque") && !actor->client) {
            actor->client = calloc(1, sizeof(*actor->client));
            if (!actor->client) { qa_error_set(error, QA_ERROR_MEMORY, i, "Restoring original Q2 corpse owner"); return false; }
            actor->client->player = qa_actors_player(qa_world_actors(g->services.world), id);
            actor->client->rule.corpse = true;
            qa_combat_state combat = {0};
            if (!qa_combat_read(g->services.combat, id, &combat, NULL) &&
                !qa_combat_create_actor(g->services.combat, id, &combat, error)) return false;
        }
        if (row->number > g->wire_clients && row->number <= g->wire_clients + 8 &&
            actor->client && actor->client->rule.corpse)
            g->player_runtime->corpses[row->number - g->wire_clients - 1] = id;
    }
    return true;
}

static bool level_links(qa_q2_game *g, const q2_original_level_file *file,
    const qa_q2_save_level *engine, qa_error *error)
{
    for (size_t i = 0; i < file->count; ++i) {
        const q2_original_edict_row *row = file->rows + i;
        if (row->number > 0 && row->number <= g->wire_clients) continue;
        q2_actor *actor = q2_actor_get(g, g->wire_actors[row->number], false, NULL);
        if (!actor) return level_error(error, row->number, "Original Q2 restored Source edict disappeared");
        qa_actor_collision collision = {.family = QA_GAME_Q2, .shape = QA_SHAPE_BOX,
            .role = actor->physics.solid == QA_PHYSICS_TRIGGER ? QA_COLLISION_TRIGGER : QA_COLLISION_SOLID,
            .monster = (actor->physics.flags & QA_PHYSICS_MONSTER) != 0,
            .dead_monster = (actor->physics.flags & QA_PHYSICS_DEAD) != 0};
        if (actor->physics.solid == QA_PHYSICS_BRUSH) {
            qa_entity_visual visual = {0};
            (void)qa_q2_presentation_read(g, actor->id, &visual);
            const char *model = qa_strings_cstr(qa_session_strings(g->services.session), visual.models[0]);
            uint32_t number = 0;
            if (row->number == 0) model = "*0";
            if (!model || *model++ != '*' || !*model)
                return level_error(error, row->number, "Original Q2 brush has no Source inline model");
            for (; *model; ++model) {
                unsigned digit = (unsigned)(uint8_t)*model - (unsigned)'0';
                if (digit > 9 || number > (UINT32_MAX - digit) / 10)
                    return level_error(error, row->number, "Original Q2 brush has an invalid inline model");
                number = number * 10 + digit;
            }
            collision.inline_model = true; collision.model = number;
            if (actor->entity) actor->entity->has_inline = true;
        }
        q2_original_record_io io = edict_reader(g, file, row, error);
        uint32_t links = 0, svflags = 0;
        if (!q2_original_scalar(&io, "svflags", Q2_ORIGINAL_U32, 184, 184, 184, &svflags)) return false;
        uint32_t solid = actor->physics.solid == QA_PHYSICS_BRUSH ? 3u :
            actor->physics.solid == QA_PHYSICS_TRIGGER ? 1u :
            actor->physics.solid == QA_PHYSICS_NOT_SOLID ? 0u : 2u;
        collision.contents = qa_collision_q2_source_contents(solid, svflags, g->options.edition == QA_Q2_RERELEASE);
        if (!q2_original_source_reference(g, &io, "owner", 256, &collision.owner) ||
            !q2_original_scalar(&io, "linkcount", Q2_ORIGINAL_U32, 92, 92, 92, &links) ||
            !qa_world_set_collision(g->services.world, actor->id,
                actor->physics.solid == QA_PHYSICS_NOT_SOLID ? NULL : &collision, error)) return false;
        if (actor->entity) actor->entity->collision = collision;
        actor->wire_lifetime.link_count = links;
        qa_body_link_state link = {.link_count = links};
        if (!qa_world_restore_link_state(g->services.world, actor->id, &link, error) ||
            !qa_world_link(g->services.world, actor->id, NULL, error)) return false;
        if ((actor->entity || actor->item) && !q2_entity_bind(g, actor, error)) return false;
        qa_entity_visual visual;
        if (qa_q2_presentation_read(g, actor->id, &visual) &&
            !q2_publish_visual(g, actor->id, &visual, error)) return false;
        if (actor->entity && (actor->entity->kind == Q2E_CROSS_TARGET ||
            actor->entity->kind == Q2E_CROSS_UNIT_TARGET))
            actor->entity->due_ns = g->now_ns + q2_item_seconds(actor->entity->delay);
        (void)engine;
    }
    return true;
}

bool qa_q2_game_original_read_level(qa_q2_game *game, qa_bytes bytes,
    const qa_q2_save_level *engine, qa_error *error)
{
    if (!game || !engine || !game->entity_runtime || !game->player_runtime ||
        !q2_checkpoint_idle(game, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original Q2 ReadLevel requires its authored map and engine table");
        return false;
    }
    q2_original_level_file file = {0};
    bool okay = q2_original_level_open(game, bytes, &file, error) &&
        level_clock(game, &file.state, error) && level_admit(game, &file, error);
    if (okay) q2_player_trail_read_level(game);
    for (size_t i = 0; okay && i < file.count; ++i) {
        const q2_original_edict_row *row = file.rows + i;
        if (row->number > 0 && row->number <= game->wire_clients) continue;
        q2_actor *actor = q2_actor_get(game, game->wire_actors[row->number], false, NULL);
        q2_original_record_io io = edict_reader(game, &file, row, error);
        okay = actor && original_edict(game, &io, actor, engine, error);
    }
    if (okay) {
        q2_original_record_io io = {.reading = true, .edition = game->options.edition,
            .product = game->options.product, .document = file.document, .object = file.level_object,
            .input = file.level_fields, .strings = file.level_strings, .error = error};
        okay = level_state(game, &io, engine, &file.state) && level_links(game, &file, engine, error);
    }
    q2_original_level_close(&file);
    return okay;
}
