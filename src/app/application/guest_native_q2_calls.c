#include "guest_native_q2_calls.h"
#include "qa/binary.h"
#include <stdlib.h>
#include <string.h>

typedef enum argument_kind { ARGUMENT_FIELD, ARGUMENT_VALUE, ARGUMENT_ADDRESS } argument_kind;
struct application_q2_call_argument {
    argument_kind kind;
    application_q2_call_field field;
    qa_buffer bytes;
    uint32_t rva, *indirections;
    size_t indirection_count;
    size_t value_bytes;
    bool null_address;
};
static const char *const field_names[] = {
    "target", "inflictor", "attacker", "direction", "point", "normal", "amount",
    "knockback", "flags", "cause", "sparks", "kick"
};
static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
static bool word(const qa_json_document *doc, qa_json_id value, uint32_t *out, qa_error *error)
{
    uint64_t number;
    if (!qa_json_u64(doc, value, &number, error)) return false;
    if (number > UINT32_MAX) return fail(error, QA_ERROR_FORMAT, "Native combat word exceeds its declared source range");
    *out = (uint32_t)number; return true;
}
static qa_native_type scalar(qa_native_value_type kind)
{ return (qa_native_type){.kind = kind, .count = 1}; }
static bool scalar_read(const qa_json_document *doc, qa_json_id value,
    qa_native_type *out, uint32_t *bytes, qa_error *error)
{
    static const char *const names[] = {"int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64", "float32", "float64"};
    static const qa_native_value_type kinds[] = {QA_NATIVE_I8, QA_NATIVE_U8, QA_NATIVE_I16, QA_NATIVE_U16,
        QA_NATIVE_I32, QA_NATIVE_U32, QA_NATIVE_I64, QA_NATIVE_U64, QA_NATIVE_F32, QA_NATIVE_F64};
    static const uint32_t sizes[] = {1, 1, 2, 2, 4, 4, 8, 8, 4, 8};
    for (size_t i = 0; i < sizeof(kinds) / sizeof(*kinds); ++i)
        if (qa_json_string_equal(doc, value, names[i])) { *out = scalar(kinds[i]); *bytes = sizes[i]; return true; }
    return fail(error, QA_ERROR_FORMAT, "Native combat defaults require a declared nonpointer scalar");
}
static uint32_t required(application_q2_call_operation operation, uint8_t pointers)
{
    switch (operation) {
    case APPLICATION_Q2_DAMAGE: return (1u << (APPLICATION_Q2_CAUSE + 1)) - 1u;
    case APPLICATION_Q2_REGULAR_ARMOR: return (1u << APPLICATION_Q2_TARGET) | (1u << APPLICATION_Q2_POINT) |
        (1u << APPLICATION_Q2_NORMAL) | (1u << APPLICATION_Q2_AMOUNT) | (1u << APPLICATION_Q2_SPARKS) | (1u << APPLICATION_Q2_FLAGS);
    case APPLICATION_Q2_POWER_ARMOR: return (1u << APPLICATION_Q2_TARGET) | (1u << APPLICATION_Q2_POINT) |
        (1u << APPLICATION_Q2_NORMAL) | (1u << APPLICATION_Q2_AMOUNT) | (1u << APPLICATION_Q2_FLAGS);
    case APPLICATION_Q2_PAIN: return (1u << APPLICATION_Q2_TARGET) | (1u << APPLICATION_Q2_ATTACKER) |
        (1u << APPLICATION_Q2_KICK) | (1u << APPLICATION_Q2_AMOUNT) | (pointers == 8 ? 1u << APPLICATION_Q2_CAUSE : 0);
    case APPLICATION_Q2_DEATH: return (1u << APPLICATION_Q2_TARGET) | (1u << APPLICATION_Q2_INFLICTOR) |
        (1u << APPLICATION_Q2_ATTACKER) | (1u << APPLICATION_Q2_AMOUNT) | (1u << APPLICATION_Q2_POINT) |
        (pointers == 8 ? 1u << APPLICATION_Q2_CAUSE : 0);
    case APPLICATION_Q2_PROCESS_PAIN: return 1u << APPLICATION_Q2_TARGET;
    }
    return 0;
}
static bool field_type(application_q2_call_field field, uint8_t pointers,
    qa_native_type *out, qa_error *error)
{
    if (field <= APPLICATION_Q2_NORMAL) *out = scalar(QA_NATIVE_ADDRESS);
    else if (field == APPLICATION_Q2_KICK) *out = scalar(QA_NATIVE_F32);
    else if (field == APPLICATION_Q2_CAUSE && pointers == 8) {
        qa_native_type *members = calloc(3, sizeof(*members));
        if (!members) return fail(error, QA_ERROR_MEMORY, "Retaining original Q2 mod_t ABI");
        for (size_t i = 0; i < 3; ++i) members[i] = scalar(QA_NATIVE_U8);
        *out = (qa_native_type){QA_NATIVE_BYTES, members, 3, 1};
    } else *out = scalar(QA_NATIVE_I32);
    return true;
}
static bool layout_read(const qa_json_document *doc, qa_json_id value, uint8_t pointers,
    qa_native_type *out, uint32_t *bytes, qa_error *error)
{
    if (qa_json_string_equal(doc, qa_json_get(doc, value, "kind"), "scalar"))
        return scalar_read(doc, qa_json_get(doc, value, "storage"), out, bytes, error);
    if (!qa_json_string_equal(doc, qa_json_get(doc, value, "kind"), "aggregate"))
        return fail(error, QA_ERROR_FORMAT, "Native combat argument layout is absent");
    qa_json_id layout = qa_json_get(doc, value, "layout"), rows = qa_json_get(doc, layout, "fields");
    uint32_t declared_pointers, alignment;
    if (!word(doc, qa_json_get(doc, layout, "pointerBytes"), &declared_pointers, error) ||
        !word(doc, qa_json_get(doc, layout, "byteLength"), bytes, error) ||
        !word(doc, qa_json_get(doc, layout, "alignment"), &alignment, error)) return false;
    if (declared_pointers != pointers || !*bytes || *bytes > 65536 ||
        !qa_json_string_equal(doc, qa_json_get(doc, layout, "byteOrder"), "little-endian") ||
        qa_json_type(doc, rows) != QA_JSON_ARRAY || !qa_json_size(doc, rows) || qa_json_size(doc, rows) > 256)
        return fail(error, QA_ERROR_FORMAT, "Native combat aggregate differs from its source ABI");
    size_t count = qa_json_size(doc, rows);
    qa_native_type *members = calloc(count, sizeof(*members));
    uint32_t *offsets = calloc(count, sizeof(*offsets)), *sizes = calloc(count, sizeof(*sizes));
    if (!members || !offsets || !sizes) { free(members); free(offsets); free(sizes);
        return fail(error, QA_ERROR_MEMORY, "Retaining native combat aggregate fields"); }
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_json_id row = qa_json_at(doc, rows, i); uint32_t repeats;
        ok = scalar_read(doc, qa_json_get(doc, row, "storage"), &members[i], &sizes[i], error) &&
            word(doc, qa_json_get(doc, row, "byteOffset"), &offsets[i], error) &&
            word(doc, qa_json_get(doc, row, "count"), &repeats, error);
        if (ok && pointers == 4 && sizes[i] == 8)
            ok = fail(error, QA_ERROR_UNSUPPORTED, "Native i386 aggregate eight-byte member alignment requires explicit ABI qualification");
        if (ok && (!repeats || repeats > *bytes / sizes[i])) ok = fail(error, QA_ERROR_FORMAT, "Native combat aggregate field extent is invalid");
        if (ok) members[i].count = repeats;
    }
    /* libffi describes natural records. Reject packed, overlapping, sparse or
     * over-aligned records rather than replacing their ABI classification. */
    for (size_t i = 1; ok && i < count; ++i) for (size_t j = i; j && offsets[j] < offsets[j - 1]; --j) {
        uint32_t offset = offsets[j]; offsets[j] = offsets[j - 1]; offsets[j - 1] = offset;
        uint32_t size = sizes[j]; sizes[j] = sizes[j - 1]; sizes[j - 1] = size;
        qa_native_type member = members[j]; members[j] = members[j - 1]; members[j - 1] = member;
    }
    uint64_t next = 0; uint32_t natural_alignment = 1;
    for (size_t i = 0; ok && i < count; ++i) {
        uint32_t align = sizes[i] < pointers ? sizes[i] : pointers;
        if (align > natural_alignment) natural_alignment = align;
        next = (next + align - 1) & ~(uint64_t)(align - 1);
        if (next != offsets[i]) ok = fail(error, QA_ERROR_UNSUPPORTED, "Native combat aggregate requires an explicit packed or sparse ABI producer");
        next += (uint64_t)sizes[i] * members[i].count;
    }
    next = (next + natural_alignment - 1) & ~(uint64_t)(natural_alignment - 1);
    if (ok && (next != *bytes || alignment != natural_alignment))
        ok = fail(error, QA_ERROR_UNSUPPORTED, "Native combat aggregate alignment differs from its native ABI descriptor");
    free(offsets); free(sizes);
    if (!ok) { free(members); return false; }
    *out = (qa_native_type){QA_NATIVE_BYTES, members, count, 1}; return true;
}
void application_q2_call_free(application_q2_call *call)
{
    if (!call) return;
    for (size_t i = 0; i < call->signature.parameter_count; ++i) {
        free((void *)call->signature.parameters[i].fields);
        if (call->arguments) { qa_buffer_free(&call->arguments[i].bytes); free(call->arguments[i].indirections); }
    }
    free((void *)call->signature.parameters); free(call->arguments); memset(call, 0, sizeof(*call));
}
bool application_q2_call_read(const qa_json_document *doc, qa_json_id value,
    application_q2_call_operation operation, qa_native_target target, application_q2_call *out, qa_error *error)
{
    if (!doc || !out || !required(operation, target.pointer_bytes))
        return fail(error, QA_ERROR_ARGUMENT, "Native combat call requires its original source operation");
    qa_json_id convention = qa_json_get(doc, value, "convention"), rows = qa_json_get(doc, value, "arguments");
    if (target.os != QA_NATIVE_OS_WINDOWS ||
        !((target.arch == QA_NATIVE_ARCH_I386 && target.pointer_bytes == 4 && target.abi == QA_NATIVE_ABI_CDECL_I386 &&
            qa_json_string_equal(doc, convention, "cdecl")) ||
          (target.arch == QA_NATIVE_ARCH_X86_64 && target.pointer_bytes == 8 && target.abi == QA_NATIVE_ABI_MICROSOFT_X64 &&
            qa_json_string_equal(doc, convention, "microsoft-x64"))))
        return fail(error, QA_ERROR_UNSUPPORTED, "Native combat convention requires its declared Windows ABI backend");
    if (qa_json_type(doc, rows) != QA_JSON_ARRAY || !qa_json_size(doc, rows) || qa_json_size(doc, rows) > 64)
        return fail(error, QA_ERROR_FORMAT, "Native combat argument roster is invalid");
    application_q2_call call = {.pointer_bytes = target.pointer_bytes};
    for (size_t i = 0; i < APPLICATION_Q2_FIELD_COUNT; ++i) call.field_index[i] = SIZE_MAX;
    size_t count = qa_json_size(doc, rows);
    qa_native_type *parameters = calloc(count, sizeof(*parameters));
    call.arguments = calloc(count, sizeof(*call.arguments));
    if (!parameters || !call.arguments) { free(parameters); free(call.arguments);
        return fail(error, QA_ERROR_MEMORY, "Retaining original native combat call"); }
    call.signature = (qa_native_signature){target.abi, parameters, count,
        scalar(operation == APPLICATION_Q2_REGULAR_ARMOR || operation == APPLICATION_Q2_POWER_ARMOR ? QA_NATIVE_I32 : QA_NATIVE_VOID), false};
    uint32_t seen = 0, needed = required(operation, target.pointer_bytes); bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_json_id row = qa_json_at(doc, rows, i), kind = qa_json_get(doc, row, "kind");
        application_q2_call_argument *argument = &call.arguments[i];
        if (qa_json_string_equal(doc, kind, "field")) {
            size_t field = 0;
            while (field < APPLICATION_Q2_FIELD_COUNT && !qa_json_string_equal(doc, qa_json_get(doc, row, "field"), field_names[field])) ++field;
            if (field == APPLICATION_Q2_FIELD_COUNT || !(needed & (1u << field)) || (seen & (1u << field))) {
                ok = fail(error, QA_ERROR_FORMAT, "Native combat semantic fields must occur exactly once"); continue;
            }
            argument->kind = ARGUMENT_FIELD; argument->field = (application_q2_call_field)field;
            call.field_index[field] = i; seen |= 1u << field;
            ok = field_type(argument->field, target.pointer_bytes, &parameters[i], error);
            if (ok && parameters[i].kind == QA_NATIVE_BYTES) argument->value_bytes = 3;
        } else if (qa_json_string_equal(doc, kind, "value")) {
            argument->kind = ARGUMENT_VALUE; uint32_t bytes;
            ok = layout_read(doc, qa_json_get(doc, row, "layout"), target.pointer_bytes, &parameters[i], &bytes, error);
            qa_json_id source = qa_json_get(doc, row, "bytes");
            if (ok && (qa_json_type(doc, source) != QA_JSON_ARRAY || qa_json_size(doc, source) != bytes))
                ok = fail(error, QA_ERROR_FORMAT, "Native combat default bytes differ from their layout");
            if (!ok) continue;
            argument->value_bytes = bytes;
            argument->bytes = (qa_buffer){.data = malloc(bytes), .size = bytes};
            if (!argument->bytes.data) { ok = fail(error, QA_ERROR_MEMORY, "Retaining native combat default bytes"); continue; }
            for (size_t j = 0; ok && j < bytes; ++j) {
                uint32_t byte; ok = word(doc, qa_json_at(doc, source, j), &byte, error);
                if (ok && byte > UINT8_MAX) ok = fail(error, QA_ERROR_FORMAT, "Native combat default byte exceeds uint8");
                if (ok) argument->bytes.data[j] = (uint8_t)byte;
            }
        } else if (qa_json_string_equal(doc, kind, "address")) {
            argument->kind = ARGUMENT_ADDRESS; parameters[i] = scalar(QA_NATIVE_ADDRESS);
            qa_json_id address = qa_json_get(doc, row, "address");
            argument->null_address = qa_json_type(doc, address) == QA_JSON_NULL;
            if (argument->null_address) continue;
            qa_json_id offsets = qa_json_get(doc, address, "indirections");
            ok = word(doc, qa_json_get(doc, address, "rva"), &argument->rva, error);
            if (ok && (qa_json_type(doc, offsets) != QA_JSON_ARRAY || qa_json_size(doc, offsets) > 64))
                ok = fail(error, QA_ERROR_FORMAT, "Native combat address chain is invalid");
            if (!ok) continue;
            argument->indirection_count = qa_json_size(doc, offsets);
            if (argument->indirection_count) {
                argument->indirections = calloc(argument->indirection_count, sizeof(*argument->indirections));
                if (!argument->indirections) { ok = fail(error, QA_ERROR_MEMORY, "Retaining native combat address chain"); continue; }
            }
            for (size_t j = 0; ok && j < argument->indirection_count; ++j)
                ok = word(doc, qa_json_at(doc, offsets, j), &argument->indirections[j], error);
        } else ok = fail(error, QA_ERROR_FORMAT, "Native combat argument has no declared producer");
    }
    if (ok && seen != needed) ok = fail(error, QA_ERROR_FORMAT, "Native combat call omits a required source field");
    if (!ok) { application_q2_call_free(&call); return false; }
    *out = call; return true;
}
static bool value_matches(const application_q2_call *call, size_t index, const qa_native_value *value)
{
    const qa_native_type *type = &call->signature.parameters[index];
    if (type->kind != value->type) return false;
    if (type->kind == QA_NATIVE_ADDRESS)
        return call->pointer_bytes == 8 || value->as.address <= UINT32_MAX;
    if (type->kind != QA_NATIVE_BYTES) return true;
    return value->as.bytes.data != NULL && value->as.bytes.size == call->arguments[index].value_bytes;
}
bool application_q2_call_project(const application_q2_call *call, const qa_native_value *arguments, size_t count,
    qa_native_value fields[APPLICATION_Q2_FIELD_COUNT], qa_error *error)
{
    if (!call || !arguments || !fields || count != call->signature.parameter_count)
        return fail(error, QA_ERROR_ARGUMENT, "Native combat invocation differs from its declared extent");
    for (size_t i = 0; i < count; ++i)
        if (!value_matches(call, i, &arguments[i]))
            return fail(error, QA_ERROR_ARGUMENT, "Native combat invocation differs from its declared argument type");
    memset(fields, 0, APPLICATION_Q2_FIELD_COUNT * sizeof(*fields));
    for (size_t i = 0; i < APPLICATION_Q2_FIELD_COUNT; ++i)
        if (call->field_index[i] != SIZE_MAX) fields[i] = arguments[call->field_index[i]];
    return true;
}
static qa_native_value decode(const qa_native_type *type, qa_buffer bytes)
{
    qa_native_value out = {.type = type->kind};
    switch (out.type) {
    case QA_NATIVE_I8: out.as.i8 = (int8_t)bytes.data[0]; break;
    case QA_NATIVE_U8: out.as.u8 = bytes.data[0]; break;
    case QA_NATIVE_I16: out.as.i16 = (int16_t)qa_load_u16le(bytes.data); break;
    case QA_NATIVE_U16: out.as.u16 = qa_load_u16le(bytes.data); break;
    case QA_NATIVE_I32: out.as.i32 = qa_load_i32le(bytes.data); break;
    case QA_NATIVE_U32: out.as.u32 = qa_load_u32le(bytes.data); break;
    case QA_NATIVE_I64: out.as.i64 = (int64_t)qa_load_u64le(bytes.data); break;
    case QA_NATIVE_U64: out.as.u64 = qa_load_u64le(bytes.data); break;
    case QA_NATIVE_F32: { uint32_t bits = qa_load_u32le(bytes.data); memcpy(&out.as.f32, &bits, 4); break; }
    case QA_NATIVE_F64: { uint64_t bits = qa_load_u64le(bytes.data); memcpy(&out.as.f64, &bits, 8); break; }
    case QA_NATIVE_BYTES: out.as.bytes = (qa_native_memory){bytes.data, bytes.size}; break;
    case QA_NATIVE_VOID: case QA_NATIVE_ADDRESS: break;
    }
    return out;
}
bool application_q2_call_lower(const application_q2_call *call, qa_native_instance *instance,
    const qa_native_value fields[APPLICATION_Q2_FIELD_COUNT], const qa_native_value *captured,
    size_t captured_count, qa_native_value *out, qa_error *error)
{
    if (!call || !instance || !fields || !out || (captured ? captured_count != call->signature.parameter_count : captured_count != 0))
        return fail(error, QA_ERROR_ARGUMENT, "Native combat continuation changed its source argument extent");
    for (size_t i = 0; i < call->signature.parameter_count; ++i) {
        const application_q2_call_argument *argument = &call->arguments[i];
        if (argument->kind == ARGUMENT_FIELD) out[i] = fields[argument->field];
        else if (captured) out[i] = captured[i];
        else if (argument->kind == ARGUMENT_VALUE) out[i] = decode(&call->signature.parameters[i], argument->bytes);
        else {
            qa_native_address address = 0;
            if (!argument->null_address && !qa_native_rva(instance, argument->rva, 1, &address, error)) return false;
            for (size_t j = 0; j < argument->indirection_count; ++j) {
                uint8_t bytes[8];
                if (!address || address > UINT64_MAX - argument->indirections[j])
                    return fail(error, QA_ERROR_ARGUMENT, "Native combat default address dereferences null or overflows");
                if (!qa_native_read(instance, address + argument->indirections[j], bytes, call->pointer_bytes, error)) return false;
                address = call->pointer_bytes == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes);
            }
            out[i] = (qa_native_value){.type = QA_NATIVE_ADDRESS, .as.address = address};
        }
        if (!value_matches(call, i, &out[i]))
            return fail(error, QA_ERROR_ARGUMENT, "Native combat continuation changed an original argument type");
    }
    return true;
}
