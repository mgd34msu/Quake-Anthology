#include "original_internal.h"

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
    case Q2_ORIGINAL_I32: return *(const int32_t *)value;
    case Q2_ORIGINAL_U32: return *(const uint32_t *)value;
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
        if (number < INT32_MIN || number > INT32_MAX || trunc(number) != number)
            return fail(io, field->offset, "Q2 original integer exceeds its Source field");
        *(int32_t *)value = (int32_t)number;
        return true;
    case Q2_ORIGINAL_U32:
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

static bool json_field(q2_original_record_io *io, const q2_original_field *field, void *value)
{
    qa_json_id id = io->reading ? qa_json_get(io->document, io->object, field->name) : QA_JSON_NONE;
    if (io->reading && id == QA_JSON_NONE) {
        memset(value, 0, native_size(field));
        if (field->kind == Q2_ORIGINAL_WEAPON_PHASE)
            *(qa_q2_weapon_phase *)value = QA_Q2_READY;
        return true;
    }
    if (!io->reading) {
        bool empty = false;
        if (field->kind == Q2_ORIGINAL_TEXT) empty = !*(const char *)value;
        else if (field->kind == Q2_ORIGINAL_VECTOR) {
            const qa_vec3 *v = value;
            empty = v->x == 0 && v->y == 0 && v->z == 0;
        } else empty = value_number(field, value) == 0;
        if (empty) return true;
        qa_json_writer_key(io->writer, field->name);
        if (!written(io)) return false;
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

static bool classic_field(q2_original_record_io *io, const q2_original_field *field, void *value)
{
    size_t offset = field->classic_offsets[io->product];
    if (offset == UINT16_MAX) {
        if (io->reading) memset(value, 0, native_size(field));
        return true;
    }
    size_t size = field->kind == Q2_ORIGINAL_VECTOR ? 12 : 4;
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
            uint32_t word = qa_load_u32le(io->input.data + offset + i * 4);
            if (floating) { float scalar; memcpy(&scalar, &word, 4); number = scalar; }
            else if (field->kind == Q2_ORIGINAL_U32) number = word;
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
            } else if (field->kind == Q2_ORIGINAL_U32) word = *(uint32_t *)component;
            else {
                if (number < INT32_MIN || number > INT32_MAX || trunc(number) != number)
                    return fail(io, offset, "Q2 Source integer exceeds the original ABI field");
                int32_t scalar = (int32_t)number; memcpy(&word, &scalar, 4);
            }
            qa_store_u32le(io->output.data + offset + i * 4, word);
        }
    }
    return true;
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
        void *member = (uint8_t *)value + field->offset;
        if (io->edition == QA_Q2_RERELEASE ? !json_field(io, field, member) : !classic_field(io, field, member))
            return false;
    }
    return true;
}
