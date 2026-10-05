#include "original_internal.h"
#include "qa/game_q2_original_save.h"
#include "original_symbols.h"
#include "internal.h"

/* These are the original g_save fields, in their string-tail write order. */
typedef struct original_string_field {
    const char *name;
    uint16_t offset;
} original_string_field;
static const original_string_field edict_strings[] = {
    {"classname", 280}, {"model", 268}, {"target", 296}, {"targetname", 300},
    {"pathtarget", 312}, {"deathtarget", 316}, {"killtarget", 304},
    {"combattarget", 320}, {"message", 276}, {"team", 308}, {"map", 504}
};

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
            for (size_t i = 0; okay && i < sizeof(edict_strings) / sizeof(edict_strings[0]); ++i)
                okay = string_extent(row.fields, edict_strings[i].offset, bytes, &position, error);
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
        .object = row->object, .input = row->fields, .error = error};
}

static bool physical_reference(qa_q2_game *game, q2_original_record_io *io,
    const char *name, uint16_t offset, qa_actor_id *actor)
{
    int32_t number = -1;
    if (io->edition == QA_Q2_RERELEASE) {
        qa_json_id value = qa_json_get(io->document, io->object, name);
        if (value != QA_JSON_NONE && qa_json_type(io->document, value) != QA_JSON_NULL) {
            int64_t integer;
            if (!qa_json_i64(io->document, value, &integer, io->error)) return false;
            if (integer < 0 || (uint64_t)integer >= game->wire_capacity)
                return level_error(io->error, value, "Original Q2 pointer exceeds its physical edict table");
            number = (int32_t)integer;
        }
    } else if (!q2_original_scalar(io, name, Q2_ORIGINAL_I32,
        offset, offset, offset, &number)) return false;
    if (number < -1 || (number >= 0 && (uint32_t)number >= game->wire_capacity))
        return level_error(io->error, offset, "Original Q2 pointer exceeds its physical edict table");
    *actor = number < 0 ? (qa_actor_id){0} :
        qa_actor_reference_resolve(qa_session_actors(game->services.session),
            qa_actor_reference_source(game->options.owner, (uint32_t)number));
    return true;
}

static bool edict_body_read(qa_q2_game *game, q2_original_record_io *io,
    qa_body_state *body)
{
    return q2_original_scalar(io, "s.origin", Q2_ORIGINAL_VECTOR, 4, 4, 4, &body->origin) &&
        q2_original_scalar(io, "s.angles", Q2_ORIGINAL_VECTOR, 16, 16, 16, &body->angles) &&
        q2_original_scalar(io, "velocity", Q2_ORIGINAL_VECTOR, 376, 376, 376, &body->velocity) &&
        q2_original_scalar(io, "mins", Q2_ORIGINAL_VECTOR, 188, 188, 188, &body->bounds.mins) &&
        q2_original_scalar(io, "maxs", Q2_ORIGINAL_VECTOR, 200, 200, 200, &body->bounds.maxs) &&
        physical_reference(game, io, "groundentity", 552, &body->ground);
}

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
    }
    qa_body_state body = {0};
    int32_t view_height = 22, health = 0, maximum_health = 0, dead = 0, motion = 0;
    uint32_t flags = client.persistent.flags;
    if (okay && level_bytes.size) {
        okay = q2_original_level_open(game, level_bytes, &level, error);
        const q2_original_edict_row *row = okay ? q2_original_level_actor(&level, slot + 1) : NULL;
        if (row) {
            io = edict_reader(game, &level, row, error);
            okay = edict_body_read(game, &io, &body) &&
                q2_original_scalar(&io, "viewheight", Q2_ORIGINAL_I32, 508, 508, 508, &view_height) &&
                q2_original_scalar(&io, "health", Q2_ORIGINAL_I32, 480, 480, 480, &health) &&
                q2_original_scalar(&io, "max_health", Q2_ORIGINAL_I32, 484, 484, 484, &maximum_health) &&
                q2_original_scalar(&io, "deadflag", Q2_ORIGINAL_I32, 492, 492, 492, &dead) &&
                q2_original_scalar(&io, "movetype", Q2_ORIGINAL_I32, 260, 260, 260, &motion) &&
                q2_original_scalar(&io, "flags", Q2_ORIGINAL_U32, 264, 264, 264, &flags);
            live_level = okay;
        }
    }
    if (okay) {
        const qa_q2_player_state *current = source->client;
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
        memcpy(client.player.info.skin, current->info.skin, sizeof(client.player.info.skin));
        client.player.use_weapons = current->use_weapons;
        client.player.use_inventory = current->use_inventory;
        client.player.bot = current->bot;
        client.player.gender = current->gender;
        client.player.visual = current->visual;
        client.player.character_configured = current->character_configured;
        client.player.character_model = current->character_model;
        client.player.character_skin = current->character_skin;
        client.player.spawned = live_level;
        client.player.pending_start_items = false;
        client.player.info.chase_target = (qa_actor_id){0};
        client.persistent.flags = flags;
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
            if (source->powers) source->powers->values = client.powers;
        }
        if (okay && live_level) {
            okay = qa_world_body_write(game->services.world, actor, &body, error) &&
                q2_player_move(game, source, &(qa_q2_player_motion){.kind = QA_Q2_PLAYER_SPAWN,
                    .origin = body.origin, .velocity = body.velocity,
                    .angles = client.movement.view_angles,
                    .command_angles = client.movement.command_angles,
                    .preserve_view_angles = true, .spectator = client.player.info.spectator}, error);
            if (okay) {
                client.movement.bounds = body.bounds;
                client.movement.view_height = (float)view_height;
                client.movement.frame = game->wire_frame;
                client.movement.time_ns = game->now_ns;
                source->wire_movement = client.movement;
                source->wire_view = (qa_q2_wire_view){.present = true, .view = client.view,
                    .frame = game->wire_frame, .time_ns = game->now_ns};
            }
        }
    }
    qa_json_destroy(document);
    q2_original_level_close(&level);
    q2_original_client_free(&client);
    if (okay) *restored = live_level;
    return okay;
}
