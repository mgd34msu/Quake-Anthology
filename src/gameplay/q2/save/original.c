#include "original_trails.h"
#include "original_internal.h"
#include "original_symbols.h"
#include "original_edicts.h"
#include "qa/game_q2_original_save.h"
#include "qa/game_q2_combat.h"
#include "qa/text.h"

static bool fail(q2_original_record_io *io, size_t offset, const char *message)
{
    qa_error_set(io->error, QA_ERROR_FORMAT, offset, "%s", message);
    return false;
}

static bool written(q2_original_record_io *io)
{
    if (!io->writer->failed) return true;
    if (io->error) *io->error = io->writer->failure;
    return false;
}

static size_t native_size(const q2_original_field *field)
{
    switch (field->kind) {
    case Q2_ORIGINAL_BOOL: return sizeof(bool);
    case Q2_ORIGINAL_VECTOR: return sizeof(qa_vec3);
    case Q2_ORIGINAL_TIME:
    case Q2_ORIGINAL_FRAME_TIME:
    case Q2_ORIGINAL_U64:
    case Q2_ORIGINAL_FRAME_INDEX: return sizeof(uint64_t);
    case Q2_ORIGINAL_TEXT: return field->count;
    default: return sizeof(uint32_t);
    }
}

static bool text_read(q2_original_record_io *io, qa_json_id id, char *text, size_t count)
{
    memset(text, 0, count);
    if (id == QA_JSON_NONE || qa_json_type(io->document, id) == QA_JSON_NULL)
        return true;
    if (qa_json_type(io->document, id) == QA_JSON_ARRAY) {
        size_t length = qa_json_size(io->document, id);
        if (length >= count) return fail(io, length, "Q2 original string exceeds its Source field");
        for (size_t i = 0; i < length; ++i) {
            uint64_t value;
            if (!qa_json_u64(io->document, qa_json_at(io->document, id, i), &value, io->error))
                return false;
            if (!value || value > UINT8_MAX)
                return fail(io, i, "Q2 original byte string contains an invalid byte");
            text[i] = (char)(uint8_t)value;
        }
        return true;
    }
    qa_buffer value = {0};
    bool okay = qa_json_string(io->document, id, &value, io->error);
    if (okay && (value.size >= count || memchr(value.data, 0, value.size)))
        okay = fail(io, value.size, "Q2 original string exceeds its Source field");
    if (okay) memcpy(text, value.data, value.size);
    qa_buffer_free(&value);
    return okay;
}

static bool text_write(q2_original_record_io *io, const char *text, size_t count)
{
    const char *end = memchr(text, 0, count);
    if (!end) return fail(io, count, "Q2 original string is not terminated");
    size_t length = (size_t)(end - text);
    bool high = false;
    for (size_t i = 0; i < length; ++i)
        high = high || ((uint8_t)text[i] & UINT8_C(128)) != 0;
    if (!high) {
        qa_json_writer_bytes(io->writer, (qa_bytes){(const uint8_t *)text, length});
        return written(io);
    }
    qa_json_writer_array(io->writer);
    for (size_t i = 0; i < length; ++i)
        qa_json_writer_number(io->writer, (uint8_t)text[i]);
    qa_json_writer_end(io->writer);
    return written(io);
}

static double value_number(const q2_original_field *field, const void *value)
{
    switch (field->kind) {
    case Q2_ORIGINAL_I32:
    case Q2_ORIGINAL_I16: return *(const int32_t *)value;
    case Q2_ORIGINAL_U32:
    case Q2_ORIGINAL_U8: return *(const uint32_t *)value;
    case Q2_ORIGINAL_BOOL: return *(const bool *)value ? 1 : 0;
    case Q2_ORIGINAL_TIME:
    case Q2_ORIGINAL_FRAME_TIME:
    case Q2_ORIGINAL_FRAME_INDEX: return (double)*(const uint64_t *)value;
    case Q2_ORIGINAL_WEAPON_PHASE: {
        static const int32_t phase[] = {1, 0, 3, 2};
        unsigned native = (unsigned)*(const qa_q2_weapon_phase *)value;
        return native < sizeof(phase) / sizeof(phase[0]) ? phase[native] : -1;
    }
    default: return *(const float *)value;
    }
}

static bool value_store(q2_original_record_io *io, const q2_original_field *field,
    void *value, double number)
{
    if (!isfinite(number)) return fail(io, field->offset, "Nonfinite Q2 original field");
    switch (field->kind) {
    case Q2_ORIGINAL_I32:
    case Q2_ORIGINAL_I16:
        if (number < INT32_MIN || number > INT32_MAX || trunc(number) != number)
            return fail(io, field->offset, "Q2 original integer exceeds its Source field");
        *(int32_t *)value = (int32_t)number;
        return true;
    case Q2_ORIGINAL_U32:
    case Q2_ORIGINAL_U8:
        if (number < 0 || number > UINT32_MAX || trunc(number) != number)
            return fail(io, field->offset, "Q2 original word exceeds its Source field");
        *(uint32_t *)value = (uint32_t)number;
        return true;
    case Q2_ORIGINAL_BOOL:
        *(bool *)value = number != 0;
        return true;
    case Q2_ORIGINAL_TIME:
    case Q2_ORIGINAL_FRAME_TIME:
    case Q2_ORIGINAL_FRAME_INDEX:
        if (number < 0 || number >= 0x1p64)
            return fail(io, field->offset, "Q2 original time exceeds its Source clock");
        *(uint64_t *)value = (uint64_t)number;
        return true;
    case Q2_ORIGINAL_WEAPON_PHASE: {
        static const qa_q2_weapon_phase phase[] = {
            QA_Q2_READY, QA_Q2_ACTIVATING, QA_Q2_DROPPING, QA_Q2_FIRING};
        if (number < 0 || number > 3 || trunc(number) != number)
            return fail(io, field->offset, "Unknown Q2 original weapon phase");
        *(qa_q2_weapon_phase *)value = phase[(unsigned)number];
        return true;
    }
    default:
        if (number > FLT_MAX || number < -FLT_MAX)
            return fail(io, field->offset, "Q2 original float exceeds its Source field");
        *(float *)value = (float)number;
        return true;
    }
}

static bool json_component(q2_original_record_io *io, const q2_original_field *field, void *value, qa_json_id id)
{
    if (field->kind == Q2_ORIGINAL_U64) {
        uint64_t *word = value;
        if (io->reading) {
            if (id == QA_JSON_NONE) { *word = 0; return true; }
            return qa_json_u64(io->document, id, word, io->error);
        }
        qa_json_writer_u64(io->writer, *word);
        return written(io);
    }
    if (io->reading && id == QA_JSON_NONE) {
        memset(value, 0, native_size(field));
        if (field->kind == Q2_ORIGINAL_WEAPON_PHASE)
            *(qa_q2_weapon_phase *)value = QA_Q2_READY;
        return true;
    }
    if (field->kind == Q2_ORIGINAL_TEXT)
        return io->reading ? text_read(io, id, value, field->count) : text_write(io, value, field->count);
    if (field->kind == Q2_ORIGINAL_VECTOR) {
        qa_vec3 *v = value;
        float *components[] = {&v->x, &v->y, &v->z};
        if (io->reading) {
            if (qa_json_type(io->document, id) != QA_JSON_ARRAY || qa_json_size(io->document, id) != 3)
                return fail(io, id, "Q2 original vector must contain three components");
            for (size_t i = 0; i < 3; ++i) {
                double number;
                if (!qa_json_number(io->document, qa_json_at(io->document, id, i), &number, io->error))
                    return false;
                if (!isfinite(number) || number < -FLT_MAX || number > FLT_MAX)
                    return fail(io, i, "Q2 original vector exceeds its Source field");
                *components[i] = (float)number;
            }
            return true;
        }
        qa_json_writer_array(io->writer);
        for (size_t i = 0; i < 3; ++i)
            qa_json_writer_number(io->writer, *components[i]);
        qa_json_writer_end(io->writer);
        return written(io);
    }
    if (field->kind == Q2_ORIGINAL_BOOL) {
        if (io->reading) return qa_json_bool(io->document, id, value, io->error);
        qa_json_writer_bool(io->writer, *(bool *)value);
        return written(io);
    }
    if (field->kind == Q2_ORIGINAL_TIME || field->kind == Q2_ORIGINAL_FRAME_TIME ||
        field->kind == Q2_ORIGINAL_FRAME_INDEX) {
        if (io->reading) {
            int64_t milliseconds;
            if (!qa_json_i64(io->document, id, &milliseconds, io->error)) return false;
            if (milliseconds < 0 || (uint64_t)milliseconds > UINT64_MAX / Q2_MS)
                return fail(io, id, "Q2 original time exceeds its Source clock");
            *(uint64_t *)value = (uint64_t)milliseconds * Q2_MS;
            return true;
        }
        qa_json_writer_number(io->writer, (double)(*(uint64_t *)value / Q2_MS));
        return written(io);
    }
    double number = value_number(field, value);
    if (io->reading && !qa_json_number(io->document, id, &number, io->error)) return false;
    if (field->kind == Q2_ORIGINAL_SECONDS_TIME) {
        if (io->reading) number /= 1000;
        else number = trunc(number * 1000);
    }
    if (field->kind == Q2_ORIGINAL_WEAPON_PHASE && (number < 0 || number > 3))
        return fail(io, field->offset, "Unknown Q2 Source weapon phase");
    if (io->reading) return value_store(io, field, value, number);
    qa_json_writer_number(io->writer, number);
    return written(io);
}


static bool json_empty(const q2_original_field *field, const void *value)
{
    if (field->kind == Q2_ORIGINAL_U64) return !*(const uint64_t *)value;
    if (field->kind == Q2_ORIGINAL_TEXT) return !*(const char *)value;
    if (field->kind == Q2_ORIGINAL_VECTOR) {
        const qa_vec3 *v = value;
        return v->x == 0 && v->y == 0 && v->z == 0;
    }
    return value_number(field, value) == 0;
}

static bool json_field(q2_original_record_io *io, const q2_original_field *field, void *value)
{
    bool array = field->kind != Q2_ORIGINAL_TEXT && field->kind != Q2_ORIGINAL_VECTOR && field->count > 1;
    size_t count = array ? field->count : 1, stride = native_size(field);
    qa_json_id id = io->reading ? qa_json_get(io->document, io->object, field->name) : QA_JSON_NONE;
    if (io->reading && array && id != QA_JSON_NONE &&
        (qa_json_type(io->document, id) != QA_JSON_ARRAY || qa_json_size(io->document, id) != count))
        return fail(io, id, "Q2 original array differs from its fixed Source extent");
    if (!io->reading) {
        bool empty = true;
        for (size_t i = 0; i < count; ++i)
            empty = empty && json_empty(field, (const uint8_t *)value + i * stride);
        if (empty) return true;
        qa_json_writer_key(io->writer, field->name);
        if (array) qa_json_writer_array(io->writer);
    }
    for (size_t i = 0; i < count; ++i) {
        qa_json_id element = io->reading && array && id != QA_JSON_NONE ? qa_json_at(io->document, id, i) : id;
        if (!json_component(io, field, (uint8_t *)value + i * stride, element)) return false;
    }
    if (!io->reading && array) qa_json_writer_end(io->writer);
    return io->reading || written(io);
}

static bool classic_field(q2_original_record_io *io, const q2_original_field *field, void *value)
{
    if (field->kind == Q2_ORIGINAL_U64) {
        uint64_t *wide = value;
        if (!io->reading && *wide > UINT32_MAX)
            return fail(io, field->offset, "Q2 Source word exceeds the original 32-bit ABI");
        q2_original_field narrow = *field;
        narrow.kind = Q2_ORIGINAL_U32;
        uint32_t word = io->reading ? 0 : (uint32_t)*wide;
        if (!classic_field(io, &narrow, &word)) return false;
        if (io->reading) *wide = word;
        return true;
    }
    size_t offset = field->classic_offsets[io->product];
    if (offset == UINT16_MAX) {
        if (io->reading) memset(value, 0, native_size(field));
        return true;
    }
    size_t size = field->kind == Q2_ORIGINAL_VECTOR ? 12 :
        field->kind == Q2_ORIGINAL_I16 ? 2 : field->kind == Q2_ORIGINAL_U8 ? 1 : 4;
    if (field->kind == Q2_ORIGINAL_TEXT) {
        size = field->count;
        if (!strcmp(field->name, "userinfo")) size = 512;
        if (!strcmp(field->name, "netname")) size = 16;
    }
    size_t extent = io->reading ? io->input.size : io->output.size;
    if (offset > extent || size > extent - offset)
        return fail(io, offset, "Truncated Q2 original ABI record");
    if (field->kind == Q2_ORIGINAL_TEXT) {
        if (io->reading) {
            const uint8_t *text = io->input.data + offset;
            const uint8_t *end = memchr(text, 0, size);
            if (!end) return fail(io, offset, "Unterminated Q2 original ABI string");
            memset(value, 0, field->count);
            memcpy(value, text, (size_t)(end - text));
            return true;
        }
        const char *end = memchr(value, 0, field->count);
        if (!end || (size_t)(end - (const char *)value) >= size)
            return fail(io, offset, "Q2 Source string exceeds the original ABI field");
        memset(io->output.data + offset, 0, size);
        memcpy(io->output.data + offset, value, (size_t)(end - (const char *)value));
        return true;
    }
    size_t components = field->kind == Q2_ORIGINAL_VECTOR ? 3 : 1;
    for (size_t i = 0; i < components; ++i) {
        void *component = (uint8_t *)value + i * sizeof(float);
        bool floating = field->kind == Q2_ORIGINAL_F32 || field->kind == Q2_ORIGINAL_VECTOR ||
            field->kind == Q2_ORIGINAL_TIME || field->kind == Q2_ORIGINAL_FRAME_TIME;
        double number = field->kind == Q2_ORIGINAL_VECTOR ? *(float *)component : value_number(field, component);
        if (io->reading) {
            uint32_t word = size == 1 ? io->input.data[offset] :
                size == 2 ? qa_load_u16le(io->input.data + offset) :
                qa_load_u32le(io->input.data + offset + i * 4);
            if (floating) { float scalar; memcpy(&scalar, &word, 4); number = scalar; }
            else if (field->kind == Q2_ORIGINAL_I16)
                number = word < 32768 ? (int32_t)word : (int32_t)word - 65536;
            else if (field->kind == Q2_ORIGINAL_U32 || field->kind == Q2_ORIGINAL_U8) number = word;
            else { int32_t scalar; memcpy(&scalar, &word, 4); number = scalar; }
        }
        double scale = field->kind == Q2_ORIGINAL_TIME ? (double)Q2_NS :
            field->kind == Q2_ORIGINAL_FRAME_TIME || field->kind == Q2_ORIGINAL_FRAME_INDEX ?
                (double)(Q2_NS / 10) : 1;
        if (io->reading) {
            number *= scale;
            q2_original_field scalar = *field;
            if (field->kind == Q2_ORIGINAL_VECTOR) scalar.kind = Q2_ORIGINAL_F32;
            if (!value_store(io, &scalar, component, number)) return false;
        } else {
            number /= scale;
            uint32_t word;
            if (!isfinite(number)) return fail(io, offset, "Nonfinite Q2 Source field");
            if (floating) {
                if (number < -FLT_MAX || number > FLT_MAX)
                    return fail(io, offset, "Q2 Source float exceeds the original ABI field");
                float scalar = (float)number; memcpy(&word, &scalar, 4);
            } else if (field->kind == Q2_ORIGINAL_U32 || field->kind == Q2_ORIGINAL_U8)
                word = *(uint32_t *)component;
            else {
                if (number < INT32_MIN || number > INT32_MAX || trunc(number) != number)
                    return fail(io, offset, "Q2 Source integer exceeds the original ABI field");
                int32_t scalar = (int32_t)number; memcpy(&word, &scalar, 4);
            }
            if (size == 1) {
                if (word > UINT8_MAX) return fail(io, offset, "Q2 Source byte exceeds the original ABI field");
                io->output.data[offset] = (uint8_t)word;
            } else if (size == 2) {
                if (number < INT16_MIN || number > INT16_MAX)
                    return fail(io, offset, "Q2 Source short exceeds the original ABI field");
                qa_store_u16le(io->output.data + offset, (uint16_t)word);
            } else qa_store_u32le(io->output.data + offset + i * 4, word);
        }
    }
    return true;
}

bool q2_original_value(q2_original_record_io *io, const q2_original_field *field, void *value)
{
    return io->edition == QA_Q2_RERELEASE ? json_field(io, field, value) :
        classic_field(io, field, value);
}

bool q2_original_record(q2_original_record_io *io, q2_original_record_kind kind, void *value)
{
    const q2_original_layout *layout = q2_original_layout_for(kind);
    if (!io || !value || !layout || (unsigned)io->edition > QA_Q2_RERELEASE || (unsigned)io->product > QA_Q2_N64 ||
        (io->edition == QA_Q2_CLASSIC && io->product > QA_Q2_ROGUE))
        return false;
    if (io->edition == QA_Q2_RERELEASE &&
        (io->reading ? qa_json_type(io->document, io->object) != QA_JSON_OBJECT : !io->writer))
        return fail(io, io->object, "Q2 original record requires an object");
    for (size_t i = 0; i < layout->count; ++i) {
        const q2_original_field *field = &layout->fields[i];
        if (io->edition == QA_Q2_RERELEASE && kind == Q2_ORIGINAL_GAME &&
            !strcmp(field->name, "num_items")) continue; /* Original RR GAME has no such member. */
        void *member = (uint8_t *)value + field->offset;
        if (!q2_original_value(io, field, member))
            return false;
    }
    return true;
}

bool q2_original_scalar(q2_original_record_io *io, const char *name,
    q2_original_field_kind kind, uint16_t base, uint16_t xatrix, uint16_t rogue, void *value)
{
    const q2_original_field field = {name, 0, 1, kind, {base, xatrix, rogue}};
    return q2_original_value(io, &field, value);
}

bool q2_original_source_text(q2_original_record_io *io, qa_q2_game *g, const char *name,
    qa_string_id *value, size_t capacity)
{
    if (io->edition != QA_Q2_RERELEASE)
        return fail(io, 0, "Variable Q2 strings require their original string field");
    char *text = calloc(capacity, 1);
    if (!text) {
        qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Reading Q2 original Source text");
        return false;
    }
    if (!io->reading && *value) {
        qa_bytes source = qa_strings_text(qa_session_strings(g->services.session), *value);
        if (!source.data || source.size >= capacity) {
            free(text);
            return fail(io, 0, "Q2 Source text exceeds its original field");
        }
        memcpy(text, source.data, source.size);
    }
    const q2_original_field field = {name, 0, capacity, Q2_ORIGINAL_TEXT, {65535, 65535, 65535}};
    bool okay = q2_original_value(io, &field, text);
    if (okay && io->reading)
        okay = *text ? qa_strings_intern_cstr(qa_session_strings(g->services.session), text,
            value, io->error) : (*value = QA_STRING_NONE, true);
    free(text);
    return okay;
}

static const char *original_item_name(qa_q2_edition edition,
    const qa_q2_item_definition *definition)
{
    if (edition == QA_Q2_CLASSIC &&
        (definition->kind == QA_Q2_ITEM_HEALTH || definition->kind == QA_Q2_ITEM_FOOD))
        return "item_health";
    return definition->classname;
}

bool q2_original_item(qa_q2_game *g, q2_original_record_io *io, const char *name,
    uint16_t base, uint16_t xatrix, uint16_t rogue, qa_item_id *item)
{
    const qa_q2_item_definition *definition = *item ? q2_item_by_id(g, *item) : NULL;
    if (!io->reading && *item && !definition)
        return fail(io, *item, "Q2 original item has no actual GAME definition");
    if (io->edition == QA_Q2_CLASSIC) {
        int32_t index = -1;
        if (!io->reading && definition &&
            !q2_original_symbol_encode(Q2_ORIGINAL_ITEM, g->options.product,
                original_item_name(io->edition, definition), &index, io->error)) return false;
        if (!q2_original_scalar(io, name, Q2_ORIGINAL_I32, base, xatrix, rogue, &index)) return false;
        if (io->reading) {
            if (index == -1 || index == 0) { *item = 0; return true; }
            const char *classname = q2_original_symbol_next(Q2_ORIGINAL_ITEM,
                g->options.product, index, NULL);
            if (definition && classname && !strcmp(classname, original_item_name(io->edition, definition))) {
                *item = definition->item;
                return true;
            }
            definition = classname ? qa_q2_item_lookup(g, classname) : NULL;
            if (!definition) return fail(io, (size_t)(uint32_t)index,
                "Original Q2 item index has no actual GAME definition");
            *item = definition->item;
        }
        return true;
    }
    char classname[128] = {0};
    if (definition) {
        size_t length = strlen(definition->classname);
        if (length >= sizeof(classname)) return fail(io, length, "Q2 item classname exceeds Source field");
        memcpy(classname, definition->classname, length);
    }
    const q2_original_field field = {name, 0, sizeof(classname), Q2_ORIGINAL_TEXT, {base, xatrix, rogue}};
    if (!q2_original_value(io, &field, classname)) return false;
    if (io->reading) {
        definition = *classname ? qa_q2_item_lookup(g, classname) : NULL;
        if (*classname && !definition) return fail(io, 0, "Original Q2 item classname is unavailable");
        *item = definition ? definition->item : 0;
    }
    return true;
}

typedef struct original_ammo {
    const char *classname;
    uint16_t offsets[3];
} original_ammo;

/* The same order is the original rerelease ammo_t/max_ammo array. */
static const original_ammo ammo_fields[] = {
    {"ammo_bullets", {1764, 1764, 1764}}, {"ammo_shells", {1768, 1768, 1768}},
    {"ammo_rockets", {1772, 1772, 1772}}, {"ammo_grenades", {1776, 1776, 1776}},
    {"ammo_cells", {1780, 1780, 1780}}, {"ammo_slugs", {1784, 1784, 1784}},
    {"ammo_magslug", {65535, 1788, 65535}}, {"ammo_trap", {65535, 1792, 65535}},
    {"ammo_flechettes", {65535, 65535, 1788}}, {"ammo_tesla", {65535, 65535, 1796}},
    {"ammo_disruptor", {65535, 65535, 1800}}, {"ammo_prox", {65535, 65535, 1792}},
};

static qa_inventory_entry *inventory_entry(qa_q2_player_carry *carry, qa_item_id item)
{
    for (size_t i = 0; i < carry->count; ++i)
        if (carry->inventory[i].item == item) return carry->inventory + i;
    return NULL;
}

static bool inventory_record(qa_q2_game *g, q2_original_record_io *io,
    qa_q2_player_carry *carry)
{
    if (io->reading) {
        size_t count = qa_q2_item_count(g);
        carry->inventory = count ? calloc(count, sizeof(*carry->inventory)) : NULL;
        if (count && !carry->inventory) {
            qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Reading original Q2 client inventory");
            return false;
        }
        carry->count = count;
        for (size_t i = 0; i < count; ++i) {
            const qa_q2_item_definition *definition = qa_q2_item_at(g, i);
            carry->inventory[i] = (qa_inventory_entry){definition->item, 0,
                definition->capacity, QA_COUNT_SOURCE_INT32};
        }
        if (io->edition == QA_Q2_CLASSIC) {
            for (int32_t ordinal = 0; ordinal < 256; ++ordinal) {
                uint16_t offset = (uint16_t)(740 + ordinal * 4);
                int32_t value = 0;
                if (!q2_original_scalar(io, "inventory", Q2_ORIGINAL_I32, offset, offset, offset, &value))
                    return false;
                const char *name = q2_original_symbol_next(Q2_ORIGINAL_ITEM,
                    g->options.product, ordinal, NULL);
                if (value && (!name || !qa_q2_item_lookup(g, name)))
                    return fail(io, (unsigned)ordinal,
                        "Original Q2 inventory item has no actual GAME definition");
            }
        }
    }
    if (io->edition == QA_Q2_RERELEASE && io->reading) {
        qa_json_id inventory = qa_json_get(io->document, io->object, "inventory");
        if (inventory != QA_JSON_NONE && qa_json_type(io->document, inventory) != QA_JSON_NULL) {
            if (qa_json_type(io->document, inventory) != QA_JSON_OBJECT)
                return fail(io, inventory, "Original Q2 inventory requires an object");
            for (size_t i = 0; i < qa_json_size(io->document, inventory); ++i) {
                qa_buffer key = {0};
                if (!qa_json_string(io->document, qa_json_key_at(io->document, inventory, i),
                    &key, io->error)) return false;
                const qa_q2_item_definition *definition = memchr(key.data, 0, key.size) ? NULL :
                    qa_q2_item_lookup(g, (const char *)key.data);
                qa_inventory_entry *entry = definition ? inventory_entry(carry, definition->item) : NULL;
                int64_t count;
                bool okay = entry && qa_json_i64(io->document,
                    qa_json_at(io->document, inventory, i), &count, io->error);
                qa_buffer_free(&key);
                if (!okay) return entry ? false : fail(io, i,
                    "Original Q2 inventory item has no actual GAME definition");
                if (count < INT32_MIN || count > INT32_MAX)
                    return fail(io, i, "Original Q2 inventory count exceeds its Source word");
                entry->count = (double)count;
            }
        }
    } else if (io->edition == QA_Q2_CLASSIC) {
        int32_t values[256] = {0};
        bool present[256] = {0};
        for (size_t i = 0; i < carry->count; ++i) {
            qa_inventory_entry *entry = carry->inventory + i;
            const qa_q2_item_definition *definition = q2_item_by_id(g, entry->item);
            if (!definition) {
                if (entry->count != 0) return fail(io, entry->item,
                    "Q2 Source inventory item cannot be represented by the original GAME");
                continue;
            }
            const char *classname = original_item_name(io->edition, definition);
            if (io->reading && strcmp(classname, definition->classname)) continue;
            if (!isfinite(entry->count) || trunc(entry->count) != entry->count ||
                entry->count < INT32_MIN || entry->count > INT32_MAX)
                return fail(io, i, "Q2 Source inventory exceeds its original signed count");
            int32_t ordinal;
            qa_error unmapped = {0};
            if (!q2_original_symbol_encode(Q2_ORIGINAL_ITEM, g->options.product,
                classname, &ordinal, &unmapped)) {
                if (entry->count == 0) continue;
                if (io->error) *io->error = unmapped;
                return false;
            }
            if (ordinal < 0 || ordinal >= 256) return fail(io, i, "Invalid original Q2 item ordinal");
            if (io->reading) {
                uint16_t offset = (uint16_t)(740 + ordinal * 4);
                int32_t count = 0;
                if (!q2_original_scalar(io, classname, Q2_ORIGINAL_I32, offset, offset, offset, &count)) return false;
                entry->count = count;
            } else {
                int64_t sum = (int64_t)values[ordinal] + (int32_t)entry->count;
                if (sum < INT32_MIN || sum > INT32_MAX)
                    return fail(io, i, "Q2 Source inventory exceeds its original signed count");
                values[ordinal] = (int32_t)sum;
                present[ordinal] = true;
            }
        }
        for (int32_t ordinal = 0; !io->reading && ordinal < 256; ++ordinal) {
            if (!present[ordinal]) continue;
            uint16_t offset = (uint16_t)(740 + ordinal * 4);
            if (!q2_original_scalar(io, "inventory", Q2_ORIGINAL_I32, offset, offset, offset,
                values + ordinal)) return false;
        }
    } else {
        qa_json_writer_key(io->writer, "inventory");
        qa_json_writer_object(io->writer);
        for (size_t i = 0; i < carry->count; ++i) {
            qa_inventory_entry *entry = carry->inventory + i;
            const qa_q2_item_definition *definition = q2_item_by_id(g, entry->item);
            if (!definition) {
                if (entry->count != 0) return fail(io, entry->item,
                    "Q2 Source inventory item cannot be represented by the original GAME");
                continue;
            }
            if (!isfinite(entry->count) || trunc(entry->count) != entry->count ||
                entry->count < INT32_MIN || entry->count > INT32_MAX)
                return fail(io, i, "Q2 Source inventory exceeds its original signed count");
            if (entry->count != 0) {
                qa_json_writer_key(io->writer, definition->classname);
                qa_json_writer_number(io->writer, entry->count);
            }
        }
        qa_json_writer_end(io->writer);
        if (!written(io)) return false;
    }
    qa_json_id maxima = io->reading && io->edition == QA_Q2_RERELEASE ?
        qa_json_get(io->document, io->object, "max_ammo") : QA_JSON_NONE;
    if (io->edition == QA_Q2_RERELEASE && io->reading && maxima != QA_JSON_NONE &&
        (qa_json_type(io->document, maxima) != QA_JSON_ARRAY ||
         qa_json_size(io->document, maxima) != sizeof(ammo_fields) / sizeof(ammo_fields[0])))
        return fail(io, maxima, "Original Q2 max_ammo requires its actual fixed array");
    if (io->edition == QA_Q2_RERELEASE && !io->reading) {
        qa_json_writer_key(io->writer, "max_ammo");
        qa_json_writer_array(io->writer);
    }
    for (size_t i = 0; i < sizeof(ammo_fields) / sizeof(ammo_fields[0]); ++i) {
        const original_ammo *field = ammo_fields + i;
        const qa_q2_item_definition *definition = qa_q2_item_lookup(g, field->classname);
        qa_inventory_entry *entry = definition ? inventory_entry(carry, definition->item) : NULL;
        if (entry && (!isfinite(entry->capacity) || entry->capacity < 0 ||
            entry->capacity > INT32_MAX || trunc(entry->capacity) != entry->capacity))
            return fail(io, i, "Q2 Source ammo capacity exceeds its original field");
        int32_t maximum = entry ? (int32_t)entry->capacity : 0;
        if (io->edition == QA_Q2_CLASSIC) {
            uint16_t offset = field->offsets[io->product];
            if (offset == UINT16_MAX) continue;
            if (!q2_original_scalar(io, field->classname, Q2_ORIGINAL_I32, offset, offset, offset, &maximum)) return false;
        } else if (io->reading) {
            int64_t value = 0;
            if (maxima != QA_JSON_NONE &&
                !qa_json_i64(io->document, qa_json_at(io->document, maxima, i), &value, io->error))
                return false;
            if (value < 0 || value > INT16_MAX)
                return fail(io, i, "Original rerelease ammo capacity exceeds its Source short");
            maximum = (int32_t)value;
        } else {
            if (maximum > INT16_MAX)
                return fail(io, i, "Q2 Source ammo capacity exceeds the original rerelease short");
            qa_json_writer_number(io->writer, maximum);
        }
        if (io->reading && entry) entry->capacity = maximum;
        if (io->reading && maximum < 0) return fail(io, i, "Negative original Q2 ammo capacity");
    }
    if (io->edition == QA_Q2_RERELEASE && !io->reading) {
        qa_json_writer_end(io->writer);
        if (!written(io)) return false;
    }
    return true;
}

static bool original_weapon(qa_q2_game *g, q2_original_record_io *io, const char *name,
    uint16_t base, uint16_t xatrix, uint16_t rogue, bool raw_pointer, qa_q2_weapon *weapon)
{
    if ((unsigned)*weapon >= QA_Q2_WEAPON_COUNT)
        return fail(io, (unsigned)*weapon, "Q2 Source weapon exceeds its actual catalog");
    qa_item_id item = io->reading ? 0 : g->items[*weapon];
    if (io->edition == QA_Q2_CLASSIC && raw_pointer) {
        int32_t ordinal = -1;
        const q2_original_library *library = q2_original_library_for(g->options.product);
        uint32_t pointer = 0;
        if (!io->reading && item) {
            const qa_q2_item_definition *definition = q2_item_by_id(g, item);
            if (!definition || !library)
                return fail(io, item, "Original coop item pointer has no known module identity");
            if (!q2_original_symbol_encode(Q2_ORIGINAL_ITEM, g->options.product,
                original_item_name(io->edition, definition), &ordinal, io->error)) return false;
            pointer = library->item_list + (uint32_t)ordinal * 76;
        }
        if (!q2_original_scalar(io, name, Q2_ORIGINAL_U32, base, xatrix, rogue, &pointer)) return false;
        if (io->reading && pointer) {
            if (!library || pointer < library->item_list ||
                (pointer - library->item_list) % 76)
                return fail(io, pointer, "Original coop item pointer has no known module identity");
            uint32_t index = (pointer - library->item_list) / 76;
            const char *classname = index <= INT32_MAX ? q2_original_symbol_next(Q2_ORIGINAL_ITEM,
                g->options.product, (int32_t)index, NULL) : NULL;
            const qa_q2_item_definition *definition = classname ? qa_q2_item_lookup(g, classname) : NULL;
            if (!definition) return fail(io, pointer, "Original coop pointer is outside the actual item list");
            item = definition->item;
        }
    } else if (!q2_original_item(g, io, name, base, xatrix, rogue, &item)) return false;
    if (io->reading) {
        *weapon = QA_Q2_WEAPON_NONE;
        for (unsigned i = 1; i < QA_Q2_WEAPON_COUNT; ++i)
            if (g->items[i] == item && item) { *weapon = (qa_q2_weapon)i; break; }
        if (item && *weapon == QA_Q2_WEAPON_NONE)
            return fail(io, item, "Original Q2 weapon is not in the admitted arsenal");
    } else if (*weapon != QA_Q2_WEAPON_NONE && !item)
        return fail(io, (unsigned)*weapon, "Q2 Source weapon has no canonical item");
    return true;
}

bool q2_original_config_layout(qa_q2_edition edition, const qa_q2_save_level *level,
    qa_q2_config_layout *layout, qa_error *error)
{
    if (level && level->rerelease != (edition == QA_Q2_RERELEASE)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Original Q2 resource table differs from its actual GAME edition");
        return false;
    }
    /* The shared layout reads only protocol and wire_flags, not frame state. */
    qa_q2_codec codec;
    codec.protocol = (qa_net_protocol_id){.kind = edition == QA_Q2_RERELEASE ? QA_NET_Q2KEX_2023 : QA_NET_Q2_34};
    codec.wire_flags = 0;
    return qa_q2_config_layout_read(&codec, layout, error);
}

bool q2_original_resource(qa_q2_game *g, q2_original_record_io *io,
    const qa_q2_save_level *level, const char *name, uint16_t base, uint16_t xatrix,
    uint16_t rogue, uint32_t table_base, qa_string_id *resource)
{
    qa_q2_config_layout layout;
    if (!q2_original_config_layout(io->edition, level, &layout, io->error)) return false;
    uint32_t count;
    switch (table_base) {
        case 32: table_base = layout.models; count = layout.max_models; break;
        case 288: table_base = layout.sounds; count = layout.max_sounds; break;
        case 544: table_base = layout.images; count = layout.max_images; break;
        default: return fail(io, table_base, "Original Q2 resource has no actual engine namespace");
    }
    uint32_t width = io->edition == QA_Q2_RERELEASE ? QA_Q2_SAVE_RERELEASE_CONFIGSTRING_BYTES : QA_Q2_SAVE_CONFIGSTRING_BYTES;
    int32_t index = 0;
    if (!io->reading && *resource) {
        const char *text = qa_strings_cstr(qa_session_strings(g->services.session), *resource);
        if (!level || !text) return fail(io, *resource, "Q2 original resource has no actual engine table");
        for (uint32_t i = 1; i < count; ++i)
            if (!strcmp(qa_q2_save_configstring(level, table_base + i), text)) {
                index = (int32_t)i; break;
            }
        if (!index) {
            qa_error_set(io->error, QA_ERROR_UNSUPPORTED, *resource,
                "Q2 Source %s resource '%s' is absent from engine table %u", name, text, table_base);
            return false;
        }
    }
    if (!q2_original_scalar(io, name, Q2_ORIGINAL_I32, base, xatrix, rogue, &index)) return false;
    if (io->reading) {
        if (index < 0 || (uint32_t)index >= count) return fail(io, (size_t)(uint32_t)index,
            "Original Q2 resource exceeds its engine table");
        if (!index) { *resource = 0; return true; }
        if (!level) return fail(io, (size_t)index, "Original Q2 resource has no saved engine table");
        const char *text = qa_q2_save_configstring(level, table_base + (uint32_t)index);
        if (!*text || !memchr(text, 0, width)) return fail(io, (size_t)index,
            "Original Q2 resource index has no terminated engine string");
        return qa_strings_intern_cstr(qa_session_strings(g->services.session), text, resource, io->error);
    }
    return true;
}

static bool persistent_record(qa_q2_game *g, q2_original_record_io *io,
    qa_q2_player_state *player, qa_q2_player_carry *carry, bool coop, qa_q2_weapon *last_weapon)
{
    if (!q2_original_record(io, Q2_ORIGINAL_PERSISTENT, player)) return false;
    int32_t health = 0, maximum_health = 0;
    if (!io->reading) {
        if (!isfinite(carry->health) || truncf(carry->health) != carry->health ||
            carry->health < INT32_MIN || (double)carry->health > INT32_MAX ||
            !isfinite(carry->maximum_health) || truncf(carry->maximum_health) != carry->maximum_health ||
            carry->maximum_health < 0 || (double)carry->maximum_health > INT32_MAX)
            return fail(io, 0, "Q2 Source health exceeds the original integer storage");
        health = (int32_t)carry->health;
        maximum_health = (int32_t)carry->maximum_health;
    }
    if (!q2_original_scalar(io, "health", Q2_ORIGINAL_I32, 724, 724, 724, &health) ||
        !q2_original_scalar(io, "max_health", Q2_ORIGINAL_I32, 728, 728, 728, &maximum_health) ||
        !q2_original_scalar(io, "savedFlags", Q2_ORIGINAL_U32, 732, 732, 732, &carry->flags) ||
        !q2_original_scalar(io, "power_cubes", Q2_ORIGINAL_U32, 1796, 1804, 1812, &carry->power_cubes) ||
        !q2_original_item(g, io, "selected_item", 736, 736, 736, &carry->selected_item) ||
        !original_weapon(g, io, "weapon", 1788, 1796, 1804, coop, &carry->weapon) ||
        !original_weapon(g, io, "lastweapon", 1792, 1800, 1808, coop, last_weapon) ||
        !inventory_record(g, io, carry)) return false;
    if (io->reading) {
        carry->health = (float)health;
        carry->maximum_health = (float)maximum_health;
        carry->score = player->info.score;
        player->info.selected_item = carry->selected_item;
        const char *armor_classes[] = {"item_armor_jacket", "item_armor_combat", "item_armor_body"};
        for (size_t i = 0; i < sizeof(armor_classes) / sizeof(armor_classes[0]); ++i) {
            const qa_q2_item_definition *definition = qa_q2_item_lookup(g, armor_classes[i]);
            const qa_inventory_entry *entry = definition ? inventory_entry(carry, definition->item) : NULL;
            if (entry && entry->count > 0) {
                carry->armor.regular = (qa_regular_armor){.kind = QA_ARMOR_Q2,
                    .item = entry->item, .points = entry->count,
                    .protection.q2 = {definition->normal_protection, definition->energy_protection}};
                break;
            }
        }
        if (carry->flags & 4096u) {
            const char *power_classes[] = {"item_power_shield", "item_power_screen"};
            for (size_t i = 0; i < sizeof(power_classes) / sizeof(power_classes[0]); ++i) {
                const qa_q2_item_definition *definition = qa_q2_item_lookup(g, power_classes[i]);
                const qa_inventory_entry *entry = definition ? inventory_entry(carry, definition->item) : NULL;
                if (entry && entry->count > 0) {
                    const qa_q2_item_definition *cells = qa_q2_item_lookup(g, "ammo_cells");
                    const qa_inventory_entry *ammo = cells ? inventory_entry(carry, cells->item) : NULL;
                    carry->armor.powered = (qa_powered_armor){.kind = definition->powered_armor,
                        .cells = ammo ? ammo->count : 0};
                    qa_q2_combat_power_armor_source(g, &carry->armor.powered);
                    break;
                }
            }
        }
    }
    return true;
}

static bool movement_record(q2_original_record_io *io, qa_movement_result *movement)
{
    if (io->edition == QA_Q2_RERELEASE) {
        qa_json_id saved = io->object;
        if (io->reading) {
            io->object = qa_json_get(io->document, saved, "pmove");
            if (io->object == QA_JSON_NONE || qa_json_type(io->document, io->object) == QA_JSON_NULL) {
                movement->state = (qa_movement_state){.kind = QA_RULESET_Q2_RERELEASE};
                io->object = saved;
                return true;
            }
            if (qa_json_type(io->document, io->object) != QA_JSON_OBJECT)
                return fail(io, io->object, "Original rerelease Pmove requires an object");
        } else {
            qa_json_writer_key(io->writer, "pmove");
            qa_json_writer_object(io->writer);
        }
        qa_q2r_movement_state *state = &movement->state.data.q2r;
        int32_t gravity = state->gravity;
        bool okay = q2_original_scalar(io, "pm_type", Q2_ORIGINAL_I32, 0, 0, 0, &state->type) &&
            q2_original_scalar(io, "origin", Q2_ORIGINAL_VECTOR, 0, 0, 0, &state->origin) &&
            q2_original_scalar(io, "velocity", Q2_ORIGINAL_VECTOR, 0, 0, 0, &state->velocity) &&
            q2_original_scalar(io, "pm_flags", Q2_ORIGINAL_U32, 0, 0, 0, &state->flags) &&
            q2_original_scalar(io, "pm_time", Q2_ORIGINAL_U32, 0, 0, 0, &state->time_ms) &&
            q2_original_scalar(io, "gravity", Q2_ORIGINAL_I32, 0, 0, 0, &gravity) &&
            q2_original_scalar(io, "delta_angles", Q2_ORIGINAL_VECTOR, 0, 0, 0, &state->delta_angles) &&
            q2_original_scalar(io, "viewheight", Q2_ORIGINAL_F32, 0, 0, 0, &state->view_height);
        if (okay && (gravity < INT16_MIN || gravity > INT16_MAX))
            okay = fail(io, 0, "Original rerelease Pmove gravity exceeds its Source short");
        if (okay && io->reading) {
            movement->state.kind = QA_RULESET_Q2_RERELEASE;
            state->gravity = (int16_t)gravity;
            movement->view_height = state->view_height;
        }
        io->object = saved;
        if (!io->reading) { qa_json_writer_end(io->writer); okay = okay && written(io); }
        return okay;
    }
    qa_q2_movement_state *state = &movement->state.data.q2;
    int32_t gravity = state->gravity;
    uint32_t flags = state->flags, time = state->time_eight_ms;
    if (!q2_original_scalar(io, "pm_type", Q2_ORIGINAL_I32, 0, 0, 0, &state->type) ||
        !q2_original_scalar(io, "pm_flags", Q2_ORIGINAL_U8, 16, 16, 16, &flags) ||
        !q2_original_scalar(io, "pm_time", Q2_ORIGINAL_U8, 17, 17, 17, &time) ||
        !q2_original_scalar(io, "gravity", Q2_ORIGINAL_I16, 18, 18, 18, &gravity)) return false;
    for (unsigned axis = 0; axis < 3; ++axis) {
        int32_t origin = qa_q2_movement_coordinate(state, false, axis);
        int32_t velocity = qa_q2_movement_coordinate(state, true, axis);
        int32_t angle = state->delta_angle_shorts[axis];
        uint16_t offset = (uint16_t)(4 + axis * 2);
        if (!q2_original_scalar(io, "origin", Q2_ORIGINAL_I16, offset, offset, offset, &origin)) return false;
        offset = (uint16_t)(10 + axis * 2);
        if (!q2_original_scalar(io, "velocity", Q2_ORIGINAL_I16, offset, offset, offset, &velocity)) return false;
        offset = (uint16_t)(20 + axis * 2);
        if (!q2_original_scalar(io, "delta_angles", Q2_ORIGINAL_I16, offset, offset, offset, &angle)) return false;
        if (io->reading) {
            qa_q2_movement_coordinate_set(state, false, axis, origin);
            qa_q2_movement_coordinate_set(state, true, axis, velocity);
            state->delta_angle_shorts[axis] = (int16_t)angle;
        }
    }
    if (io->reading) {
        movement->state.kind = QA_RULESET_Q2_CLASSIC;
        state->gravity = (int16_t)gravity;
        state->flags = flags;
        state->time_eight_ms = (uint8_t)time;
    }
    return true;
}

bool q2_original_object_begin(q2_original_record_io *parent, const char *name,
    q2_original_record_io *child, bool *present)
{
    *child = *parent;
    *present = true;
    if (parent->edition != QA_Q2_RERELEASE) return true;
    if (parent->reading) {
        child->object = qa_json_get(parent->document, parent->object, name);
        *present = child->object != QA_JSON_NONE &&
            qa_json_type(parent->document, child->object) != QA_JSON_NULL;
        return !*present || qa_json_type(parent->document, child->object) == QA_JSON_OBJECT ||
            fail(parent, child->object, "Original Q2 state requires an object");
    }
    qa_json_writer_key(parent->writer, name);
    qa_json_writer_object(parent->writer);
    return written(parent);
}

bool q2_original_object_end(q2_original_record_io *parent)
{
    if (parent->edition == QA_Q2_RERELEASE && !parent->reading) {
        qa_json_writer_end(parent->writer);
        return written(parent);
    }
    return true;
}

bool q2_original_client_record(qa_q2_game *g, q2_original_record_io *io,
    const qa_q2_save_level *level, q2_original_client_state *state)
{
    q2_original_record_io child;
    bool present;
    if (!q2_original_object_begin(io, "ps", &child, &present)) return false;
    if (present && (!movement_record(&child, &state->movement) ||
        !q2_original_record(&child, Q2_ORIGINAL_VIEW, &state->view) ||
        !q2_original_resource(g, &child, level, "gunindex", 88, 88, 88, 32, &state->weapon.view_model) ||
        !q2_original_scalar(&child, "gunframe", Q2_ORIGINAL_I32, 92, 92, 92, &state->weapon.frame) ||
        (io->edition == QA_Q2_RERELEASE &&
         !q2_original_scalar(&child, "gunskin", Q2_ORIGINAL_I32, 65535, 65535, 65535, &state->weapon.view_skin))))
        return false;
    if (!q2_original_object_end(io) || !q2_original_object_begin(io, "pers", &child, &present)) return false;
    if (present && !persistent_record(g, &child, &state->player, &state->persistent,
        false, &state->weapon.last_weapon)) return false;
    if (!q2_original_object_end(io) || !q2_original_object_begin(io, "resp.coop_respawn", &child, &present)) return false;
    if (io->edition == QA_Q2_CLASSIC) {
        static const size_t shifts[] = {1628, 1636, 1644};
        size_t shift = shifts[io->product];
        if (io->reading) {
            if (shift > child.input.size) return fail(io, shift, "Truncated original Q2 coop state");
            child.input.data += shift;
            child.input.size -= shift;
        } else {
            if (shift > child.output.size) return fail(io, shift, "Truncated original Q2 coop state");
            child.output.data += shift;
            child.output.size -= shift;
        }
    }
    if (present) {
        qa_q2_player_state coop = state->player;
        qa_q2_weapon last_weapon = QA_Q2_WEAPON_NONE;
        if (!io->reading) coop.info.score = state->player.coop.score;
        if (!persistent_record(g, &child, &coop, &state->player.coop, true, &last_weapon)) return false;
        if (io->reading) state->player.has_coop = true;
    }
    if (!q2_original_object_end(io) || !q2_original_record(io, Q2_ORIGINAL_CLIENT, &state->player) ||
        !q2_original_record(io, Q2_ORIGINAL_WEAPON, &state->weapon) ||
        !q2_original_record(io, Q2_ORIGINAL_POWERS, &state->powers) ||
        !original_weapon(g, io, "newweapon", 3532, 3548, 3564, false, &state->weapon.pending) ||
        !q2_original_scalar(io, "v_angle", Q2_ORIGINAL_VECTOR, 3636, 3652, 3668, &state->movement.view_angles) ||
        !q2_original_scalar(io, "resp.cmd_angles", Q2_ORIGINAL_VECTOR, 3452, 3468, 3484,
            &state->command_angles) ||
        !q2_original_scalar(io, "silencer_shots", Q2_ORIGINAL_I32, 3732, 3752, 3764, &state->silencer) ||
        !q2_original_resource(g, io, level, "weapon_sound", 3736, 3756, 3768, 288, &state->weapon.loop_sound))
        return false;
    if (io->edition == QA_Q2_CLASSIC) {
        if (!q2_original_scalar(io, "connected", Q2_ORIGINAL_BOOL, 720, 720, 720, &state->player.info.connected) ||
            !q2_original_scalar(io, "showscores", Q2_ORIGINAL_BOOL, 3496, 3512, 3528, &state->player.show_scores) ||
            !q2_original_scalar(io, "showinventory", Q2_ORIGINAL_BOOL, 3500, 3516, 3532, &state->player.show_inventory) ||
            !q2_original_scalar(io, "showhelp", Q2_ORIGINAL_BOOL, 3504, 3520, 3536, &state->player.show_help) ||
            !q2_original_scalar(io, "buttons", Q2_ORIGINAL_U32, 3516, 3532, 3548, &state->player.buttons) ||
            !q2_original_scalar(io, "latched_buttons", Q2_ORIGINAL_U32, 3524, 3540, 3556, &state->player.latched_buttons) ||
            !q2_original_scalar(io, "weapon_thunk", Q2_ORIGINAL_BOOL, 3528, 3544, 3560, &state->player.weapon_thunk))
            return false;
        if (io->product == QA_Q2_ROGUE &&
            !q2_original_reference(g, io, "owned_sphere", 3852, &state->sphere)) return false;
        const struct {const char *name; uint16_t offsets[3]; float *value;} damage[] = {
            {"damage_armor", {3536, 3552, 3568}, &state->player.damage_armor},
            {"damage_parmor", {3540, 3556, 3572}, &state->player.damage_power},
            {"damage_blood", {3544, 3560, 3576}, &state->player.damage_blood},
            {"damage_knockback", {3548, 3564, 3580}, &state->player.damage_knockback},
        };
        for (size_t i = 0; i < sizeof(damage) / sizeof(damage[0]); ++i) {
            int32_t value = io->reading ? 0 : qa_source_float_to_i32(*damage[i].value);
            if (!q2_original_scalar(io, damage[i].name, Q2_ORIGINAL_I32, damage[i].offsets[0],
                damage[i].offsets[1], damage[i].offsets[2], &value)) return false;
            if (io->reading) *damage[i].value = (float)value;
        }
        if (!q2_original_scalar(io, "damage_from", Q2_ORIGINAL_VECTOR, 3552, 3568, 3584,
            &state->player.damage_from)) return false;
    }
    if (io->reading) {
        state->weapon.weapon = state->persistent.weapon;
        state->weapon.gun_rate = 10;
        if (io->edition == QA_Q2_CLASSIC) state->weapon.kick_seconds = .2f;
        state->player.fov = state->view.fov;
        state->player.loop_sound = state->weapon.loop_sound;
        state->movement.view_offset = state->view.offset;
        state->movement.status = QA_MOVEMENT_ACTIVE;
    }
    return true;
}

void q2_original_client_free(q2_original_client_state *state)
{
    if (!state) return;
    qa_q2_player_carry_free(&state->persistent);
    qa_q2_player_carry_free(&state->player.coop);
    free(state->player.spawn_inventory);
    free(state->player.help_points);
    *state = (q2_original_client_state){0};
}

bool qa_q2_game_original_builtin_game(qa_bytes bytes)
{
    if (!bytes.data || bytes.size<16) return false;
    for (unsigned product=QA_Q2_BASE;product<=QA_Q2_ROGUE;++product) {
        const q2_original_library *library=q2_original_library_for((qa_q2_product)product);
        if (!memcmp(bytes.data,library->date,sizeof(library->date))) return true;
    }
    return false;
}

bool q2_original_game_open(qa_q2_game *g, qa_bytes bytes,
    qa_json_document **document, q2_original_record_io *io,
    q2_original_game_state *state, qa_error *error)
{
    *io = (q2_original_record_io){.edition = g->options.edition, .product = g->options.product,
        .reading = true, .error = error};
    if (io->edition == QA_Q2_RERELEASE) {
        if (!qa_json_parse(bytes, document, error)) return false;
        io->document = *document;
        qa_json_id root = qa_json_root(*document);
        io->object = qa_json_get(*document, root, "game");
        if (!q2_original_record(io, Q2_ORIGINAL_GAME, state)) return false;
        qa_json_id clients = qa_json_get(*document, root, "clients");
        if (qa_json_type(*document, clients) != QA_JSON_ARRAY ||
            qa_json_size(*document, clients) != state->clients)
            return fail(io, clients, "Original Q2 GAME client table does not match maxclients");
        for (size_t i = 0; i < state->clients; ++i)
            if (qa_json_type(*document, qa_json_at(*document, clients, i)) != QA_JSON_OBJECT)
                return fail(io, i, "Original Q2 GAME client requires an object");
    } else {
        if (!q2_original_client_size(g->options.product) || bytes.size < 16 + 1564 ||
            !bytes.data || !memchr(bytes.data, 0, 16))
            return fail(io, 0, "Truncated original Q2 GAME file");
        io->input = (qa_bytes){bytes.data + 16, 1564};
        if (!q2_original_record(io, Q2_ORIGINAL_GAME, state)) return false;
        size_t stride = q2_original_client_size(g->options.product);
        if (state->clients > (bytes.size - 16 - 1564) / stride ||
            (size_t)state->clients * stride != bytes.size - 16 - 1564)
            return fail(io, 16 + 1564, "Original Q2 GAME client table is truncated or has trailing bytes");
        if (state->items != q2_original_item_count(g->options.product))
            return fail(io, 16 + 1556, "Original Q2 GAME item list differs from its actual product");
    }
    if (!state->clients || state->clients > 256 || state->entities <= state->clients ||
        state->entities > 65536)
        return fail(io, 0, "Original Q2 GAME has an invalid physical edict policy");
    return true;
}

static bool campaign_record(qa_q2_game *g, q2_original_record_io *io,
    qa_q2_campaign_level levels[QA_Q2_CAMPAIGN_LEVEL_LIMIT], uint32_t *count)
{
    if (io->edition != QA_Q2_RERELEASE) return true;
    qa_json_id list = io->reading ? qa_json_get(io->document, io->object, "level_entries") : QA_JSON_NONE;
    if (io->reading && (list == QA_JSON_NONE || qa_json_type(io->document, list) == QA_JSON_NULL)) {
        *count = 0;
        return true;
    }
    if (io->reading && (qa_json_type(io->document, list) != QA_JSON_ARRAY ||
        qa_json_size(io->document, list) != QA_Q2_CAMPAIGN_LEVEL_LIMIT))
        return fail(io, list, "Original Q2 campaign requires its actual fixed level array");
    if (*count > QA_Q2_CAMPAIGN_LEVEL_LIMIT)
        return fail(io, *count, "Q2 Source campaign exceeds its original level array");
    if (!io->reading) {
        qa_json_writer_key(io->writer, "level_entries");
        qa_json_writer_array(io->writer);
    }
    uint32_t extent = 0;
    for (uint32_t i = 0; i < QA_Q2_CAMPAIGN_LEVEL_LIMIT; ++i) {
        qa_q2_campaign_level empty = {0};
        qa_q2_campaign_level *level = io->reading || i < *count ? levels + i : &empty;
        q2_original_record_io row = *io;
        if (io->reading) {
            row.object = qa_json_at(io->document, list, i);
            if (qa_json_type(io->document, row.object) != QA_JSON_OBJECT)
                return fail(io, row.object, "Original Q2 campaign level requires an object");
        } else qa_json_writer_object(io->writer);
        uint64_t time = 0;
        if (!io->reading) {
            double nanoseconds = level->time_seconds * (double)Q2_NS;
            if (!isfinite(nanoseconds) || nanoseconds < 0 || nanoseconds >= 0x1p64)
                return fail(io, i, "Q2 Source campaign time exceeds its original clock");
            time = (uint64_t)nanoseconds;
        }
        if (!q2_original_source_text(&row, g, "map_name", &level->map, 64) ||
            !q2_original_source_text(&row, g, "pretty_name", &level->name, 64) ||
            !q2_original_scalar(&row, "total_secrets", Q2_ORIGINAL_U32, 0, 0, 0, &level->total_secrets) ||
            !q2_original_scalar(&row, "found_secrets", Q2_ORIGINAL_U32, 0, 0, 0, &level->found_secrets) ||
            !q2_original_scalar(&row, "total_monsters", Q2_ORIGINAL_U32, 0, 0, 0, &level->total_monsters) ||
            !q2_original_scalar(&row, "killed_monsters", Q2_ORIGINAL_U32, 0, 0, 0, &level->killed_monsters) ||
            !q2_original_scalar(&row, "visit_order", Q2_ORIGINAL_U32, 0, 0, 0, &level->visit_order) ||
            !q2_original_scalar(&row, "time", Q2_ORIGINAL_TIME, 0, 0, 0, &time)) return false;
        if (io->reading) {
            level->time_seconds = (double)time / (double)Q2_NS;
            if (level->map) extent = i + 1;
        } else qa_json_writer_end(io->writer);
    }
    if (io->reading) *count = extent;
    else { qa_json_writer_end(io->writer); if (!written(io)) return false; }
    return true;
}

bool qa_q2_game_original_read_game(qa_q2_game *g, qa_bytes bytes, qa_error *error)
{
    if (!g || !g->entity_runtime || !g->player_runtime || g->first_actor ||
        !q2_checkpoint_idle(g, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original Q2 ReadGame requires its fresh GAME constructor");
        return false;
    }
    qa_json_document *document = NULL;
    q2_original_record_io io;
    q2_original_game_state state = {0};
    qa_q2_campaign_level levels[QA_Q2_CAMPAIGN_LEVEL_LIMIT] = {0};
    uint32_t level_count = 0;
    bool okay = q2_original_game_open(g, bytes, &document, &io, &state, error) &&
        campaign_record(g, &io, levels, &level_count);
    qa_string_id primary = 0, secondary = 0;
    if (okay) okay = qa_strings_intern_cstr(qa_session_strings(g->services.session), state.help[0],
        &primary, error) && qa_strings_intern_cstr(qa_session_strings(g->services.session), state.help[1],
        &secondary, error);
    char *spawnpoint = NULL;
    if (okay) {
        size_t length = strlen(state.spawnpoint) + 1;
        spawnpoint = malloc(length);
        if (!spawnpoint) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring original Q2 GAME spawnpoint");
            okay = false;
        } else memcpy(spawnpoint, state.spawnpoint, length);
    }
    if (okay && (g->wire_capacity != state.entities || g->wire_clients != state.clients))
        okay = qa_q2_wire_configure(g, state.entities, state.clients, error);
    if (okay) okay = q2_entity_flags(g, true, false, &state.level_flags, error);
    if (okay && g->options.edition == QA_Q2_RERELEASE)
        okay = q2_entity_flags(g, true, true, &state.unit_flags, error);
    if (okay) {
        /* ReadGame precedes actual Client services installation. Restore the
         * same rule-string owner; normal player configuration installs services. */
        q2_players *players = g->player_runtime;
        free(players->rule_strings[2]);
        players->rule_strings[2] = spawnpoint;
        players->rules.spawn_point = spawnpoint;
        players->rules.max_clients = state.clients;
        spawnpoint = NULL;
        q2_entities *entities = g->entity_runtime;
        entities->primary = primary;
        entities->secondary = secondary;
        entities->primary_changes = state.help_changes[0];
        entities->secondary_changes = state.help_changes[1];
        entities->level_count = level_count;
        memcpy(entities->levels, levels, sizeof(levels));
    }
    free(spawnpoint);
    qa_json_destroy(document);
    return okay;
}

bool q2_original_client_capture(qa_q2_game *g, q2_actor *actor,
    q2_original_client_state *state, qa_error *error)
{
    *state = (q2_original_client_state){.weapon.phase = QA_Q2_READY};
    if (!actor || !actor->client) return true;
    state->player = *actor->client;
    state->player.coop.inventory = NULL;
    state->player.spawn_inventory = NULL;
    state->player.spawn_count = 0;
    state->player.help_points = NULL;
    state->player.help_count = state->player.help_capacity = 0;
    if (actor->client->coop.count) {
        size_t count = actor->client->coop.count;
        if (count > SIZE_MAX / sizeof(*state->player.coop.inventory)) {
            qa_error_set(error, QA_ERROR_MEMORY, count, "Original Q2 coop inventory extent overflows");
            return false;
        }
        state->player.coop.inventory = malloc(count * sizeof(*state->player.coop.inventory));
        if (!state->player.coop.inventory) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Capturing actual Q2 coop inventory");
            return false;
        }
        memcpy(state->player.coop.inventory, actor->client->coop.inventory,
            count * sizeof(*state->player.coop.inventory));
    }
    if (!qa_q2_player_carry_capture(g, actor->id, &state->persistent, error)) return false;
    for (size_t i = 0; i < state->persistent.count; ++i) {
        qa_inventory_entry *entry = state->persistent.inventory + i;
        const qa_q2_item_definition *definition = q2_item_by_id(g, entry->item);
        if (definition && definition->kind == QA_Q2_ITEM_ARMOR)
            entry->count = entry->item == state->persistent.armor.regular.item ?
                state->persistent.armor.regular.points : 0;
    }
    if (actor->weapon_bound) state->weapon = actor->weapon;
    state->powers = actor->powers ? actor->powers->values : (qa_q2_powerups){0};
    state->sphere = actor->powers ? actor->powers->sphere : (qa_actor_id){0};
    state->silencer = actor->silencer;
    state->view = actor->wire_view.view;
    if (!qa_q2_player_movement_read(g, actor->id, &state->movement, &state->command_angles, error)) return false;
    return true;
}

static bool actual_game_state(qa_q2_game *g, bool autosave,
    q2_original_game_state *state, qa_error *error)
{
    q2_entities *entities = g->entity_runtime;
    *state = (q2_original_game_state){.clients = g->wire_clients,
        .entities = g->wire_capacity, .autosave = autosave,
        .help_changes = {entities->primary_changes, entities->secondary_changes}};
    if (g->options.edition == QA_Q2_CLASSIC)
        state->items = (uint32_t)q2_original_item_count(g->options.product);
    qa_string_id ids[] = {entities->primary, entities->secondary};
    for (size_t i = 0; i < 2; ++i) {
        const char *text = qa_strings_cstr(qa_session_strings(g->services.session), ids[i]);
        if (!text) text = "";
        size_t length = strlen(text);
        if (length >= sizeof(state->help[i])) {
            qa_error_set(error, QA_ERROR_FORMAT, length, "Actual Q2 help exceeds its original Source field");
            return false;
        }
        memcpy(state->help[i], text, length + 1);
    }
    const char *point = g->player_runtime->rules.spawn_point;
    size_t length = point ? strlen(point) : 0;
    if (length >= sizeof(state->spawnpoint)) {
        qa_error_set(error, QA_ERROR_FORMAT, length, "Actual Q2 spawnpoint exceeds its original Source field");
        return false;
    }
    if (length) memcpy(state->spawnpoint, point, length);
    return q2_entity_flags(g, false, false, &state->level_flags, error) &&
        (g->options.edition != QA_Q2_RERELEASE ||
         q2_entity_flags(g, false, true, &state->unit_flags, error));
}

bool q2_original_write_game(qa_q2_game *g, bool autosave,
    const qa_q2_save_level *level, qa_buffer *out, qa_error *error)
{
    if (!g || !out || !g->entity_runtime || !g->player_runtime ||
        !q2_checkpoint_idle(g, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original Q2 WriteGame requires its completed GAME owner");
        return false;
    }
    q2_original_game_state state;
    if (!actual_game_state(g, autosave, &state, error)) return false;
    q2_original_record_io io = {.edition = g->options.edition, .product = g->options.product,
        .error = error};
    qa_buffer encoded = {0};
    qa_json_writer writer = {0};
    if (io.edition == QA_Q2_CLASSIC) {
        size_t stride = q2_original_client_size(g->options.product);
        if (!stride) return fail(&io, 0, "Actual Q2 product has no original classic client ABI");
        encoded.size = 16 + 1564 + (size_t)state.clients * stride;
        encoded.data = calloc(encoded.size, 1);
        if (!encoded.data) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Writing original Q2 GAME file");
            return false;
        }
        const q2_original_library *library = q2_original_library_for(g->options.product);
        const char *date = library->date;
        memcpy(encoded.data, date, strlen(date));
        io.output = (qa_buffer){encoded.data + 16, 1564};
    } else {
        io.writer = &writer;
        qa_json_writer_object(&writer);
        qa_json_writer_key(&writer, "save_version");
        qa_json_writer_number(&writer, 1); /* Original externally authored GAME member. */
        qa_json_writer_key(&writer, "game");
        qa_json_writer_object(&writer);
    }
    bool okay = q2_original_record(&io, Q2_ORIGINAL_GAME, &state);
    if (okay && io.edition == QA_Q2_RERELEASE) {
        qa_q2_campaign_level levels[QA_Q2_CAMPAIGN_LEVEL_LIMIT];
        memcpy(levels, g->entity_runtime->levels, sizeof(levels));
        uint32_t count = g->entity_runtime->level_count;
        okay = campaign_record(g, &io, levels, &count);
        qa_json_writer_end(&writer);
        qa_json_writer_key(&writer, "clients");
        qa_json_writer_array(&writer);
    }
    for (uint32_t slot = 0; okay && slot < state.clients; ++slot) {
        q2_actor *actor = q2_actor_get(g, g->wire_actors[slot + 1], false, NULL);
        q2_original_client_state client = {0};
        okay = q2_original_client_capture(g, actor, &client, error);
        if (io.edition == QA_Q2_CLASSIC) {
            size_t stride = q2_original_client_size(g->options.product);
            io.output = (qa_buffer){encoded.data + 16 + 1564 + (size_t)slot * stride, stride};
        } else qa_json_writer_object(&writer);
        if (okay) okay = q2_original_client_record(g, &io, level, &client) &&
            q2_original_trail_client(g, &io, actor ? actor->id : (qa_actor_id){0});
        if (io.edition == QA_Q2_RERELEASE) qa_json_writer_end(&writer);
        q2_original_client_free(&client);
    }
    if (okay && io.edition == QA_Q2_RERELEASE) {
        qa_json_writer_end(&writer);
        qa_json_writer_end(&writer);
        okay = qa_json_writer_finish(&writer, &encoded, error);
    }
    qa_json_writer_destroy(&writer);
    if (!okay) { qa_buffer_free(&encoded); return false; }
    *out = encoded;
    return true;
}
