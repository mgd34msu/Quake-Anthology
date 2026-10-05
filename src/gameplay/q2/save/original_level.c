#include "original_internal.h"
#include "original_symbols.h"
#include "internal.h"
#include "qa/game_q2_original_save.h"

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
