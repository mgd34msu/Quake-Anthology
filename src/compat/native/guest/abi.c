#include "abi.h"
#include "internal.h"
#include "unicorn_abi_fp.h"

typedef enum location_kind { INTEGER, VECTOR, STACK } location_kind;
typedef struct location {
    location_kind kind;
    unsigned reg;
    size_t offset, bytes, stack;
} location;
typedef struct argument {
    guest_abi_layout layout;
    location locations[3];
    size_t location_count;
    bool indirect;
} argument;
struct guest_abi_plan {
    qa_native_abi abi;
    guest_abi_convention convention;
    argument *arguments;
    size_t count, fixed, stack_bytes, stack_alignment, callee_pop;
    unsigned word, integers, vectors, ordinal;
    argument result, hidden;
    bool variadic, memory_result, x87_result;
};

size_t guest_abi_result_bytes(const guest_abi_plan *plan)
{ return plan ? plan->result.layout.bytes : 0; }

static bool align_size(size_t size, size_t alignment, size_t *out, qa_error *error)
{
    if (!alignment || alignment & (alignment - 1) || size > SIZE_MAX - (alignment - 1)) {
        guest_fail(error, QA_ERROR_ARGUMENT, size, "guest ABI alignment overflows");
        return false;
    }
    *out = (size + alignment - 1) & ~(alignment - 1);
    return true;
}

static size_t scalar_bytes(qa_native_value_type kind, unsigned word)
{
    switch (kind) {
    case QA_NATIVE_I8: case QA_NATIVE_U8: return 1;
    case QA_NATIVE_I16: case QA_NATIVE_U16: return 2;
    case QA_NATIVE_I32: case QA_NATIVE_U32: case QA_NATIVE_F32: return 4;
    case QA_NATIVE_I64: case QA_NATIVE_U64: case QA_NATIVE_F64: return 8;
    case QA_NATIVE_ADDRESS: return word;
    default: return 0;
    }
}

static bool layout_copy(guest_abi_layout *out, const guest_abi_layout *source,
    unsigned word, bool result, qa_error *error)
{
    if (source->kind == QA_NATIVE_VOID) {
        if (!result || source->bytes || source->fields || source->field_count || source->stack_only)
            return guest_fail(error, QA_ERROR_ARGUMENT, 0, "void guest ABI layout is only a result");
        *out = *source; return true;
    }
    if (!source->bytes || !source->alignment || source->alignment & (source->alignment - 1) ||
        source->alignment > QA_NATIVE_GUEST_PAGE)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "guest ABI layout needs actual extent and alignment");
    if (source->kind != QA_NATIVE_BYTES) {
        if (source->bytes != scalar_bytes(source->kind, word) || source->fields || source->field_count || source->stack_only)
            return guest_fail(error, QA_ERROR_ARGUMENT, 0, "guest ABI scalar layout differs from its width");
        *out = *source; return true;
    }
    if ((source->stack_only && result) || (source->field_count && !source->fields) ||
        source->field_count > SIZE_MAX / sizeof(*source->fields))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "guest ABI aggregate needs actual scalar fields");
    for (size_t i = 0; i < source->field_count; ++i) {
        const guest_abi_field *field = &source->fields[i];
        size_t bytes = scalar_bytes(field->kind, word);
        if (!bytes || field->offset > source->bytes || field->count > (source->bytes - field->offset) / bytes)
            return guest_fail(error, QA_ERROR_ARGUMENT, i, "guest ABI field exceeds its aggregate");
    }
    guest_abi_field *fields = source->field_count ? malloc(source->field_count * sizeof(*fields)) : NULL;
    if (source->field_count && !fields) return guest_fail(error, QA_ERROR_MEMORY, 0, "owning guest ABI scalar fields");
    if (source->field_count) memcpy(fields, source->fields, source->field_count * sizeof(*fields));
    *out = *source; out->fields = fields;
    return true;
}

void guest_abi_plan_destroy(guest_abi_plan *plan)
{
    if (!plan) return;
    for (size_t i = 0; i < plan->count; ++i) free((void *)plan->arguments[i].layout.fields);
    free((void *)plan->result.layout.fields);
    free(plan->arguments); free(plan);
}

size_t guest_abi_argument_count(const guest_abi_plan *plan)
{ return plan ? plan->count : 0; }

size_t guest_abi_argument_bytes(const guest_abi_plan *plan)
{ return plan ? plan->stack_bytes - plan->word : 0; }

/* Classes are empty, integer, SSE. Explicit POD scalar fields require no
 * inferred C++/vector/long-double class. */
static bool system_classes(const guest_abi_layout *layout, unsigned classes[2])
{
    classes[0] = classes[1] = 0;
    if (layout->bytes > 16) return false;
    if (layout->kind != QA_NATIVE_BYTES) {
        classes[0] = layout->kind == QA_NATIVE_F32 || layout->kind == QA_NATIVE_F64 ? 2 : 1;
        return true;
    }
    for (size_t i = 0; i < layout->field_count; ++i) {
        const guest_abi_field *field = &layout->fields[i];
        size_t bytes = scalar_bytes(field->kind, 8);
        if (field->offset % bytes) return false;
        unsigned next = field->kind == QA_NATIVE_F32 || field->kind == QA_NATIVE_F64 ? 2 : 1;
        for (size_t j = 0; j < field->count; ++j) {
            size_t begin = field->offset + j * bytes, end = begin + bytes;
            for (size_t slot = begin / 8; slot < (end + 7) / 8; ++slot)
                classes[slot] = classes[slot] == 1 || next == 1 ? 1 : 2;
        }
    }
    return true;
}

static bool stack_argument(guest_abi_plan *plan, argument *arg, qa_error *error)
{
    size_t bytes = arg->indirect ? plan->word : arg->layout.bytes;
    if (arg->layout.kind != QA_NATIVE_BYTES && bytes < 4) bytes = 4;
    size_t alignment = plan->abi == QA_NATIVE_ABI_CDECL_I386 ? 4 :
        arg->indirect ? plan->word : arg->layout.alignment > plan->word ? arg->layout.alignment : plan->word;
    size_t offset, rounded;
    if (!align_size(plan->stack_bytes - plan->word, alignment, &offset, error) ||
        !align_size(bytes, plan->word, &rounded, error) || offset > SIZE_MAX - plan->word ||
        rounded > SIZE_MAX - offset - plan->word)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "guest ABI stack extent overflows");
    offset += plan->word;
    arg->locations[arg->location_count++] = (location){STACK, 0, 0, bytes, offset};
    plan->stack_bytes = offset + rounded;
    if (plan->stack_alignment < alignment) plan->stack_alignment = alignment;
    return true;
}

static bool assign(guest_abi_plan *plan, argument *arg, bool hidden, size_t index, qa_error *error)
{
    static const unsigned microsoft[4] = {QA_NATIVE_RCX, QA_NATIVE_RDX, QA_NATIVE_R8, QA_NATIVE_R9};
    static const unsigned system[6] = {QA_NATIVE_RDI, QA_NATIVE_RSI, QA_NATIVE_RDX,
        QA_NATIVE_RCX, QA_NATIVE_R8, QA_NATIVE_R9};
    bool floating = arg->layout.kind == QA_NATIVE_F32 || arg->layout.kind == QA_NATIVE_F64;
    size_t bytes = arg->layout.kind != QA_NATIVE_BYTES && arg->layout.bytes < 4 ? 4 : arg->layout.bytes;
    if (arg->layout.stack_only) {
        if (plan->abi != QA_NATIVE_ABI_SYSTEM_V_X64 && plan->abi != QA_NATIVE_ABI_SYSTEM_V_I386)
            return guest_fail(error, QA_ERROR_ARGUMENT, index, "stack-class argument requires its actual System V ABI");
        return stack_argument(plan, arg, error);
    }
    if (plan->abi == QA_NATIVE_ABI_MICROSOFT_X64) {
        unsigned position = plan->ordinal++;
        arg->indirect = arg->layout.kind == QA_NATIVE_BYTES && bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8;
        if (position >= 4) return stack_argument(plan, arg, error);
        arg->locations[arg->location_count++] = (location){floating ? VECTOR : INTEGER,
            floating ? position : microsoft[position], 0, arg->indirect ? 8 : bytes, 0};
        if (floating && plan->variadic)
            arg->locations[arg->location_count++] = (location){INTEGER, microsoft[position], 0, bytes, 0};
        if (floating && plan->vectors < position + 1) plan->vectors = position + 1;
        return true;
    }
    if (plan->abi == QA_NATIVE_ABI_SYSTEM_V_X64) {
        unsigned classes[2];
        if (!system_classes(&arg->layout, classes)) return stack_argument(plan, arg, error);
        unsigned integers = (classes[0] == 1 ? 1u : 0u) + (classes[1] == 1 ? 1u : 0u);
        unsigned vectors = (classes[0] == 2 ? 1u : 0u) + (classes[1] == 2 ? 1u : 0u);
        if (plan->integers + integers > 6 || plan->vectors + vectors > 8)
            return stack_argument(plan, arg, error);
        for (unsigned i = 0; i < 2; ++i) if (classes[i]) {
            size_t width = bytes - i * 8; if (width > 8) width = 8;
            unsigned reg = classes[i] == 1 ? system[plan->integers++] : plan->vectors++;
            arg->locations[arg->location_count++] = (location){classes[i] == 1 ? INTEGER : VECTOR, reg, i * 8, width, 0};
        }
        return true;
    }
    if (plan->abi == QA_NATIVE_ABI_CDECL_I386 && !plan->variadic) {
        if (plan->convention == GUEST_ABI_THISCALL && !hidden && !index) {
            if (arg->layout.kind != QA_NATIVE_ADDRESS)
                return guest_fail(error, QA_ERROR_ARGUMENT, 0, "thiscall requires its explicit source this pointer");
            arg->locations[arg->location_count++] = (location){INTEGER, QA_NATIVE_RCX, 0, 4, 0};
            return true;
        }
        if (plan->convention == GUEST_ABI_FASTCALL && arg->layout.kind != QA_NATIVE_BYTES &&
            !floating && bytes <= 4 && plan->integers < 2) {
            arg->locations[arg->location_count++] = (location){INTEGER,
                plan->integers++ ? QA_NATIVE_RDX : QA_NATIVE_RCX, 0, bytes < 4 ? 4 : bytes, 0};
            return true;
        }
    }
    return stack_argument(plan, arg, error);
}

bool guest_abi_plan_create(const guest_abi_signature *signature, const guest_abi_layout *extra,
    size_t extra_count, guest_abi_plan **out, qa_error *error)
{
    if (!signature || !out || *out || (signature->parameter_count && !signature->parameters) ||
        (extra_count && (!extra || !signature->variadic)) ||
        extra_count > SIZE_MAX - signature->parameter_count ||
        signature->convention < GUEST_ABI_DEFAULT || signature->convention > GUEST_ABI_THISCALL ||
        (signature->abi != QA_NATIVE_ABI_CDECL_I386 && signature->convention != GUEST_ABI_DEFAULT) ||
        (signature->abi != QA_NATIVE_ABI_CDECL_I386 && signature->abi != QA_NATIVE_ABI_MICROSOFT_X64 &&
         signature->abi != QA_NATIVE_ABI_SYSTEM_V_I386 && signature->abi != QA_NATIVE_ABI_SYSTEM_V_X64))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "guest ABI plan requires its actual x86 convention and layouts");
    size_t count = signature->parameter_count + extra_count;
    if (count > SIZE_MAX / sizeof(argument) || count > UINT_MAX - 1)
        return guest_fail(error, QA_ERROR_MEMORY, count, "guest ABI argument extent overflows");
    guest_abi_plan *plan = calloc(1, sizeof(*plan));
    if (!plan) return guest_fail(error, QA_ERROR_MEMORY, 0, "allocating guest ABI plan");
    plan->abi = signature->abi; plan->convention = signature->convention;
    plan->word = signature->abi == QA_NATIVE_ABI_CDECL_I386 || signature->abi == QA_NATIVE_ABI_SYSTEM_V_I386 ? 4 : 8;
    plan->fixed = signature->parameter_count; plan->variadic = signature->variadic;
    plan->stack_bytes = plan->word + (signature->abi == QA_NATIVE_ABI_MICROSOFT_X64 ? 32 : 0);
    plan->stack_alignment = signature->abi == QA_NATIVE_ABI_CDECL_I386 ? 4 : 16;
    plan->arguments = count ? calloc(count, sizeof(*plan->arguments)) : NULL;
    if (count && !plan->arguments) {
        guest_abi_plan_destroy(plan);
        return guest_fail(error, QA_ERROR_MEMORY, count, "allocating guest ABI argument locations");
    }
    bool okay = layout_copy(&plan->result.layout, &signature->result, plan->word, true, error);
    for (size_t i = 0; okay && i < count; ++i) {
        const guest_abi_layout *layout = i < signature->parameter_count ? &signature->parameters[i] : &extra[i - signature->parameter_count];
        okay = layout_copy(&plan->arguments[i].layout, layout, plan->word, false, error);
        if (okay && i >= signature->parameter_count && (layout->kind == QA_NATIVE_F32 ||
            layout->kind == QA_NATIVE_I8 || layout->kind == QA_NATIVE_U8 ||
            layout->kind == QA_NATIVE_I16 || layout->kind == QA_NATIVE_U16))
            okay = guest_fail(error, QA_ERROR_ARGUMENT, i, "variadic layout must carry its actual promoted width");
        if (okay) ++plan->count;
    }
    guest_abi_layout *result = &plan->result.layout;
    unsigned classes[2] = {0};
    if (okay && result->kind == QA_NATIVE_BYTES)
        plan->memory_result = plan->abi == QA_NATIVE_ABI_SYSTEM_V_I386 ||
            (plan->abi == QA_NATIVE_ABI_CDECL_I386 && plan->convention == GUEST_ABI_THISCALL) ||
            (plan->abi == QA_NATIVE_ABI_SYSTEM_V_X64 ? !system_classes(result, classes) :
             result->bytes != 1 && result->bytes != 2 && result->bytes != 4 && result->bytes != 8);
    plan->x87_result = plan->word == 4 && (result->kind == QA_NATIVE_F32 || result->kind == QA_NATIVE_F64);
    bool first = plan->memory_result && plan->abi == QA_NATIVE_ABI_CDECL_I386 &&
        plan->convention == GUEST_ABI_THISCALL && plan->variadic;
    if (okay && first) {
        if (!count || plan->arguments[0].layout.kind != QA_NATIVE_ADDRESS)
            okay = guest_fail(error, QA_ERROR_ARGUMENT, 0, "variadic thiscall requires its explicit source this pointer");
        else okay = assign(plan, &plan->arguments[0], false, 0, error);
    }
    if (okay && plan->memory_result) {
        plan->hidden.layout = (guest_abi_layout){.kind = QA_NATIVE_ADDRESS, .bytes = plan->word, .alignment = plan->word};
        okay = assign(plan, &plan->hidden, true, 0, error);
    }
    for (size_t i = first ? 1 : 0; okay && i < count; ++i)
        okay = assign(plan, &plan->arguments[i], false, i, error);
    if (okay && result->kind != QA_NATIVE_VOID && !plan->memory_result && !plan->x87_result) {
        if (plan->abi == QA_NATIVE_ABI_SYSTEM_V_X64) {
            system_classes(result, classes);
            unsigned integers = 0, vectors = 0;
            for (unsigned i = 0; i < 2; ++i) if (classes[i]) {
                size_t width = result->bytes - i * 8; if (width > 8) width = 8;
                unsigned reg = classes[i] == 1 ? (integers++ ? QA_NATIVE_RDX : QA_NATIVE_RAX) : vectors++;
                plan->result.locations[plan->result.location_count++] = (location){classes[i] == 1 ? INTEGER : VECTOR, reg, i * 8, width, 0};
            }
        } else {
            bool vector = plan->word == 8 && (result->kind == QA_NATIVE_F32 || result->kind == QA_NATIVE_F64);
            size_t width = result->bytes < plan->word ? result->bytes : plan->word;
            plan->result.locations[plan->result.location_count++] = (location){vector ? VECTOR : INTEGER, vector ? 0 : QA_NATIVE_RAX, 0, width, 0};
            if (result->bytes > plan->word)
                plan->result.locations[plan->result.location_count++] = (location){INTEGER, QA_NATIVE_RDX, plan->word, result->bytes - plan->word, 0};
        }
    }
    plan->callee_pop = plan->abi == QA_NATIVE_ABI_SYSTEM_V_I386 && plan->memory_result ? 4 :
        plan->abi == QA_NATIVE_ABI_CDECL_I386 && !plan->variadic && plan->convention != GUEST_ABI_DEFAULT ? plan->stack_bytes - plan->word : 0;
    if (!okay) { guest_abi_plan_destroy(plan); return false; }
    *out = plan; return true;
}

typedef struct type_chain { const qa_native_type *type; const struct type_chain *parent; } type_chain;
typedef struct field_owner { guest_abi_field *fields; size_t count, capacity; } field_owner;

static bool natural_type(const qa_native_type *type, qa_native_abi abi,
    const type_chain *parent, guest_abi_layout *out, qa_error *error)
{
    for (const type_chain *p = parent; p; p = p->parent)
        if (p->type == type) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "recursive native ABI descriptor");
    unsigned word = abi == QA_NATIVE_ABI_CDECL_I386 || abi == QA_NATIVE_ABI_SYSTEM_V_I386 ? 4 : 8;
    if (type->kind != QA_NATIVE_BYTES) {
        size_t bytes = scalar_bytes(type->kind, word);
        if ((!bytes && type->kind != QA_NATIVE_VOID) || type->fields || type->field_count)
            return guest_fail(error, QA_ERROR_ARGUMENT, 0, "invalid native ABI scalar descriptor");
        size_t alignment = abi == QA_NATIVE_ABI_SYSTEM_V_I386 && bytes > 4 ? 4 : bytes;
        *out = (guest_abi_layout){.kind = type->kind, .bytes = bytes, .alignment = alignment ? alignment : 1};
        return true;
    }
    if (!type->fields || !type->field_count)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "empty native ABI aggregate descriptor");
    type_chain chain = {type, parent}; field_owner owner = {0};
    size_t extent = 0, alignment = 1; bool okay = true;
    for (size_t i = 0; okay && i < type->field_count; ++i) {
        const qa_native_type *source = &type->fields[i];
        for (const type_chain *p = &chain; p; p = p->parent)
            if (p->type == source) { okay = guest_fail(error, QA_ERROR_ARGUMENT, i, "recursive native ABI field"); break; }
        guest_abi_layout field = {0};
        if (!okay) break;
        okay = source->count && natural_type(source, abi, &chain, &field, error);
        if (!okay) { if (!source->count) guest_fail(error, QA_ERROR_ARGUMENT, i, "empty native ABI repeated field"); break; }
        if (!field.bytes || !align_size(extent, field.alignment, &extent, error) ||
            source->count > (SIZE_MAX - extent) / field.bytes) {
            free((void *)field.fields); okay = guest_fail(error, QA_ERROR_ARGUMENT, i, "native ABI aggregate extent overflows"); break;
        }
        size_t additions = field.kind == QA_NATIVE_BYTES ? field.field_count : 1;
        if (field.kind == QA_NATIVE_BYTES && source->count > SIZE_MAX / additions) {
            free((void *)field.fields); okay = guest_fail(error, QA_ERROR_MEMORY, i, "native ABI field table overflows"); break;
        }
        if (field.kind == QA_NATIVE_BYTES) additions *= source->count;
        if (additions > SIZE_MAX - owner.count) {
            free((void *)field.fields); okay = guest_fail(error, QA_ERROR_MEMORY, i, "native ABI flattened field count overflows"); break;
        }
        if (!guest_grow((void **)&owner.fields, &owner.capacity, owner.count + additions, sizeof(*owner.fields), error)) {
            free((void *)field.fields); okay = false; break;
        }
        if (field.kind != QA_NATIVE_BYTES)
            owner.fields[owner.count++] = (guest_abi_field){field.kind, extent, source->count};
        else for (size_t repeat = 0; repeat < source->count; ++repeat)
            for (size_t j = 0; j < field.field_count; ++j) {
                guest_abi_field flat = field.fields[j]; flat.offset += extent + repeat * field.bytes;
                owner.fields[owner.count++] = flat;
            }
        extent += field.bytes * source->count;
        if (alignment < field.alignment) alignment = field.alignment;
        free((void *)field.fields);
    }
    if (okay) okay = align_size(extent, alignment, &extent, error);
    if (!okay) { free(owner.fields); return false; }
    *out = (guest_abi_layout){.kind = QA_NATIVE_BYTES, .bytes = extent, .alignment = alignment,
        .fields = owner.fields, .field_count = owner.count};
    return true;
}

bool guest_abi_plan_native(const qa_native_signature *source, const guest_abi_layout *extra,
    size_t extra_count, guest_abi_plan **out, qa_error *error)
{
    if (!source || source->result.count != 1 || (source->parameter_count && !source->parameters) ||
        source->parameter_count > SIZE_MAX / sizeof(guest_abi_layout))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native ABI signature is required");
    guest_abi_layout *parameters = source->parameter_count ? calloc(source->parameter_count, sizeof(*parameters)) : NULL;
    if (source->parameter_count && !parameters)
        return guest_fail(error, QA_ERROR_MEMORY, 0, "owning native ABI layouts");
    guest_abi_signature signature = {source->abi, GUEST_ABI_DEFAULT, parameters,
        source->parameter_count, {0}, source->variadic};
    bool okay = natural_type(&source->result, source->abi, NULL, &signature.result, error);
    for (size_t i = 0; okay && i < source->parameter_count; ++i) {
        if (source->parameters[i].count != 1)
            okay = guest_fail(error, QA_ERROR_ARGUMENT, i, "native ABI parameter must have count one");
        else okay = natural_type(&source->parameters[i], source->abi, NULL, &parameters[i], error);
    }
    if (okay) okay = guest_abi_plan_create(&signature, extra, extra_count, out, error);
    for (size_t i = 0; i < source->parameter_count; ++i) free((void *)parameters[i].fields);
    free((void *)signature.result.fields); free(parameters);
    return okay;
}

static uint64_t load_integer(const uint8_t *data, size_t bytes)
{
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i) value |= (uint64_t)data[i] << (i * 8);
    return value;
}

static void store_integer(uint8_t *data, size_t bytes, uint64_t value)
{
    for (size_t i = 0; i < bytes; ++i) data[i] = (uint8_t)(value >> (i * 8));
}

static bool encode_value(const guest_abi_layout *layout, const qa_native_value *value,
    uint8_t *data, qa_error *error)
{
    if (!value || value->type != layout->kind)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "guest ABI value type differs from its descriptor");
    uint64_t bits = 0;
    switch (value->type) {
    case QA_NATIVE_VOID: return true;
    case QA_NATIVE_I8: bits = (uint64_t)(int64_t)value->as.i8; break;
    case QA_NATIVE_U8: bits = value->as.u8; break;
    case QA_NATIVE_I16: bits = (uint64_t)(int64_t)value->as.i16; break;
    case QA_NATIVE_U16: bits = value->as.u16; break;
    case QA_NATIVE_I32: bits = (uint64_t)(int64_t)value->as.i32; break;
    case QA_NATIVE_U32: bits = value->as.u32; break;
    case QA_NATIVE_I64: bits = (uint64_t)value->as.i64; break;
    case QA_NATIVE_U64: bits = value->as.u64; break;
    case QA_NATIVE_F32: { uint32_t raw; memcpy(&raw, &value->as.f32, 4); bits = raw; break; }
    case QA_NATIVE_F64: memcpy(&bits, &value->as.f64, 8); break;
    case QA_NATIVE_ADDRESS:
        bits = value->as.address;
        if (layout->bytes == 4 && bits > UINT32_MAX)
            return guest_fail(error, QA_ERROR_ARGUMENT, bits, "guest ABI pointer exceeds its address width");
        break;
    case QA_NATIVE_BYTES:
        if (!value->as.bytes.data || value->as.bytes.size != layout->bytes)
            return guest_fail(error, QA_ERROR_ARGUMENT, 0, "guest ABI aggregate needs its exact bytes");
        memcpy(data, value->as.bytes.data, layout->bytes); return true;
    default: return guest_fail(error, QA_ERROR_ARGUMENT, 0, "invalid guest ABI value");
    }
    /* Scratch scalars retain signed extension for widened i386 arguments. */
    store_integer(data, 8, bits); return true;
}

static void decode_value(const guest_abi_layout *layout, const uint8_t *data, qa_native_value *out)
{
    qa_native_value value = {.type = layout->kind};
    uint64_t bits = layout->kind == QA_NATIVE_BYTES ? 0 : load_integer(data, layout->bytes);
    switch (layout->kind) {
    case QA_NATIVE_I8: value.as.i8 = (int8_t)bits; break;
    case QA_NATIVE_U8: value.as.u8 = (uint8_t)bits; break;
    case QA_NATIVE_I16: value.as.i16 = (int16_t)bits; break;
    case QA_NATIVE_U16: value.as.u16 = (uint16_t)bits; break;
    case QA_NATIVE_I32: value.as.i32 = (int32_t)bits; break;
    case QA_NATIVE_U32: value.as.u32 = (uint32_t)bits; break;
    case QA_NATIVE_I64: value.as.i64 = (int64_t)bits; break;
    case QA_NATIVE_U64: value.as.u64 = bits; break;
    case QA_NATIVE_F32: { uint32_t raw = (uint32_t)bits; memcpy(&value.as.f32, &raw, 4); break; }
    case QA_NATIVE_F64: memcpy(&value.as.f64, &bits, 8); break;
    case QA_NATIVE_ADDRESS: value.as.address = bits; break;
    case QA_NATIVE_BYTES: value.as.bytes = (qa_native_memory){(void *)data, layout->bytes}; break;
    default: break;
    }
    *out = value;
}

static bool plan_guest(const guest_abi_plan *plan, const qa_native_guest *guest, qa_error *error)
{
    return guest_ready(guest, error) && ((plan && plan->abi == guest->options.image.target.abi) ||
        guest_fail(error, QA_ERROR_ARGUMENT, 0, "guest ABI plan differs from its actual target"));
}

static bool read_locations(const argument *arg, const qa_native_guest *guest,
    const qa_native_guest_cpu *cpu, uint8_t *data, qa_error *error)
{
    for (size_t i = 0; i < arg->location_count; ++i) {
        const location *loc = &arg->locations[i];
        if (loc->kind == STACK) {
            uint64_t sp = cpu->registers[QA_NATIVE_RSP];
            if (loc->stack > UINT64_MAX - sp) return guest_fail(error, QA_ERROR_ARGUMENT, sp, "guest ABI stack argument address overflows");
            if (!qa_native_guest_read(guest, sp + loc->stack, data + loc->offset, loc->bytes, error)) return false;
        } else store_integer(data + loc->offset, loc->bytes,
            loc->kind == INTEGER ? cpu->registers[loc->reg] : cpu->xmm[loc->reg][0]);
    }
    return true;
}

static void write_registers(const argument *arg, const uint8_t *data, qa_native_guest_cpu *cpu)
{
    for (size_t i = 0; i < arg->location_count; ++i) {
        const location *loc = &arg->locations[i];
        if (loc->kind == INTEGER) cpu->registers[loc->reg] = load_integer(data + loc->offset, loc->bytes);
        else if (loc->kind == VECTOR) {
            uint64_t mask = loc->bytes == 8 ? UINT64_MAX : (UINT64_C(1) << (loc->bytes * 8)) - 1;
            cpu->xmm[loc->reg][0] = (cpu->xmm[loc->reg][0] & ~mask) | load_integer(data + loc->offset, loc->bytes);
        }
    }
}

bool guest_abi_decode_argument(const guest_abi_plan *plan, const qa_native_guest *guest,
    size_t index, qa_native_value *out, qa_buffer *storage, qa_error *error)
{
    if (!plan_guest(plan, guest, error)) return false;
    if (!out || !storage || storage->data || storage->size || index >= plan->count)
        return guest_fail(error, QA_ERROR_ARGUMENT, index, "single guest ABI decode requires its actual location and empty storage");
    const argument *arg = &plan->arguments[index];
    size_t bytes = arg->layout.bytes < 8 ? 8 : arg->layout.bytes;
    uint8_t *data = calloc(1, bytes), pointer[8] = {0};
    if (!data) return guest_fail(error, QA_ERROR_MEMORY, index, "owning one decoded guest ABI argument");
    qa_native_guest_cpu cpu;
    bool okay = qa_native_guest_cpu_read(guest, &cpu, error) &&
        read_locations(arg, guest, &cpu, arg->indirect ? pointer : data, error);
    if (okay && arg->indirect)
        okay = qa_native_guest_read(guest, load_integer(pointer, plan->word), data, arg->layout.bytes, error);
    if (!okay) { free(data); return false; }
    decode_value(&arg->layout, data, out);
    *storage = (qa_buffer){data, bytes}; return true;
}

bool guest_abi_decode(const guest_abi_plan *plan, const qa_native_guest *guest,
    qa_native_value *out, size_t count, qa_buffer *storage, qa_error *error)
{
    if (!plan_guest(plan, guest, error)) return false;
    if (count != plan->count || (count && !out) || !storage || storage->data || storage->size)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "guest ABI decode needs empty owned storage and all arguments");
    size_t bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t size = plan->arguments[i].layout.bytes;
        if (size < 8) size = 8;
        if (size > SIZE_MAX - bytes) return guest_fail(error, QA_ERROR_MEMORY, 0, "guest ABI decode extent overflows");
        bytes += size;
    }
    qa_native_value *values = count ? calloc(count, sizeof(*values)) : NULL;
    uint8_t *data = bytes ? calloc(1, bytes) : NULL;
    if ((count && !values) || (bytes && !data)) {
        free(values); free(data); return guest_fail(error, QA_ERROR_MEMORY, 0, "owning decoded guest ABI arguments");
    }
    qa_native_guest_cpu cpu; bool okay = qa_native_guest_cpu_read(guest, &cpu, error); size_t offset = 0;
    for (size_t i = 0; okay && i < count; ++i) {
        const argument *arg = &plan->arguments[i]; uint8_t pointer[8] = {0};
        okay = read_locations(arg, guest, &cpu, arg->indirect ? pointer : data + offset, error);
        if (okay && arg->indirect) okay = qa_native_guest_read(guest, load_integer(pointer, plan->word), data + offset, arg->layout.bytes, error);
        if (okay) decode_value(&arg->layout, data + offset, &values[i]);
        offset += arg->layout.bytes < 8 ? 8 : arg->layout.bytes;
    }
    if (!okay) { free(values); free(data); return false; }
    if (count) memcpy(out, values, count * sizeof(*out));
    *storage = (qa_buffer){data, bytes}; free(values); return true;
}

static bool floating_result(const guest_abi_plan *plan, qa_native_guest *guest,
    uint8_t *data, bool push, qa_error *error)
{
    if (guest->options.backend != QA_NATIVE_GUEST_EMULATED)
        return guest_fail(error, QA_ERROR_UNSUPPORTED, 0,
            "i386 x87 ABI conversion requires its qualified emulated target");
    if (qa_unicorn_abi_fp_revision() != QA_UNICORN_ABI_FP_REVISION)
        return guest_fail(error, QA_ERROR_FORMAT, 0, "guest ABI floating extension differs from its contract");
    uint64_t bits = push ? load_integer(data, plan->result.layout.bytes) : 0;
    if (!guest_uc(guest, qa_unicorn_abi_fp(guest->cpu,
        plan->result.layout.kind == QA_NATIVE_F64, &bits, push), error)) return false;
    if (!push) store_integer(data, plan->result.layout.bytes, bits);
    return true;
}

bool guest_abi_return(const guest_abi_plan *plan, qa_native_guest *guest,
    const qa_native_value *result, qa_error *error)
{
    if (!plan_guest(plan, guest, error) || !guest_mutable(guest, error)) return false;
    const guest_abi_layout *layout = &plan->result.layout;
    size_t bytes = layout->bytes < 8 ? 8 : layout->bytes;
    uint8_t *data = calloc(1, bytes);
    if (!data) return guest_fail(error, QA_ERROR_MEMORY, 0, "retaining guest ABI callback result");
    bool okay = encode_value(layout, result, data, error);
    qa_native_guest_cpu cpu; uint8_t address[8] = {0}, hidden[8] = {0}; uint64_t destination = 0;
    if (okay) okay = qa_native_guest_cpu_read(guest, &cpu, error);
    uint64_t sp = okay ? cpu.registers[QA_NATIVE_RSP] : 0;
    size_t pop = plan->word + plan->callee_pop;
    if (okay && (pop < plan->word || sp > UINT64_MAX - pop ||
        (plan->word == 4 && sp + pop > UINT32_MAX)))
        okay = guest_fail(error, QA_ERROR_ARGUMENT, sp, "guest ABI callback return stack overflows");
    if (okay) okay = qa_native_guest_read(guest, sp, address, plan->word, error);
    uint64_t instruction = load_integer(address, plan->word);
    if (okay) okay = guest_range(guest, instruction, 1, QA_NATIVE_GUEST_EXECUTE, error);
    if (okay && plan->memory_result) {
        okay = read_locations(&plan->hidden, guest, &cpu, hidden, error);
        destination = load_integer(hidden, plan->word);
        if (okay) okay = guest_range(guest, destination, layout->bytes, QA_NATIVE_GUEST_WRITE, error);
    }
    bool effected = false;
    if (okay && plan->memory_result) {
        effected = true;
        okay = qa_native_guest_write(guest, destination, (qa_bytes){data, layout->bytes}, error);
    }
    if (okay && plan->x87_result) {
        effected = true; okay = floating_result(plan, guest, data, true, error);
    }
    /* A committed write observer may execute another genuine guest call. Read
     * its CPU effects before changing just this callback's return registers. */
    if (okay) okay = qa_native_guest_cpu_read(guest, &cpu, error);
    if (okay) {
        if (plan->memory_result) cpu.registers[QA_NATIVE_RAX] = destination;
        else if (!plan->x87_result) write_registers(&plan->result, data, &cpu);
        cpu.registers[QA_NATIVE_RSP] = sp + pop; cpu.instruction = instruction;
        effected = true; okay = qa_native_guest_cpu_write(guest, &cpu, error);
    }
    if (!okay && effected && !guest_callback_cancelled(guest,error)) guest->failed = true;
    free(data); return okay;
}

typedef struct call_argument {
    uint8_t *data;
    uint8_t pointer[8];
    uint64_t temporary;
} call_argument;

static bool write_stack(const argument *arg, qa_native_guest *guest, uint64_t sp,
    const uint8_t *data, qa_error *error)
{
    for (size_t i = 0; i < arg->location_count; ++i) {
        const location *loc = &arg->locations[i];
        if (loc->kind == STACK &&
            !qa_native_guest_write(guest, sp + loc->stack, (qa_bytes){data + loc->offset, loc->bytes}, error)) return false;
    }
    return true;
}

static bool invoke(const guest_abi_plan *plan, qa_native_guest *guest, uint64_t target,
    uint64_t return_trap, const qa_native_value *values, size_t count,
    qa_native_value *result, size_t budget, bool native, uint64_t bypass, qa_error *error)
{
    if (!plan_guest(plan, guest, error) || !guest_mutable(guest, error)) return false;
    qa_native_value discarded = {.type = QA_NATIVE_VOID};
    if (!result && plan->result.layout.kind == QA_NATIVE_VOID) result = &discarded;
    if (native != (guest->options.backend == QA_NATIVE_GUEST_HOST_X86_64) ||
        (native && (budget || guest->observe || plan->x87_result || bypass)))
        return guest_fail(error, QA_ERROR_UNSUPPORTED, target,
            "guest ABI execution capability differs from its actual backend");
    if (count != plan->count || (count && !values) || (!native && !budget) || !result ||
        !return_trap || target == return_trap ||
        !guest_range(guest, target, 1, QA_NATIVE_GUEST_EXECUTE, error) ||
        !guest_range(guest, return_trap, 1, QA_NATIVE_GUEST_EXECUTE, error))
        return guest_fail(error, QA_ERROR_ARGUMENT, target, "guest ABI invocation needs actual entry, return trap, values and budget");
    const guest_abi_layout *layout = &plan->result.layout;
    if (layout->kind == QA_NATIVE_BYTES && (result->type != QA_NATIVE_BYTES ||
        !result->as.bytes.data || result->as.bytes.size < layout->bytes))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "guest ABI aggregate result storage is too small");
    size_t owned = layout->bytes < 8 ? 8 : layout->bytes;
    size_t needed = plan->stack_bytes;
    if (plan->memory_result) {
        size_t alignment = layout->alignment < 16 ? 16 : layout->alignment;
        if (needed > SIZE_MAX - alignment || layout->bytes > SIZE_MAX - needed - alignment) return guest_fail(error, QA_ERROR_MEMORY, 0, "guest ABI result extent overflows");
        needed += layout->bytes + alignment;
    }
    for (size_t i = 0; i < count; ++i) {
        size_t bytes = plan->arguments[i].layout.bytes;
        if (bytes < 8) bytes = 8;
        if (bytes > SIZE_MAX - owned) return guest_fail(error, QA_ERROR_MEMORY, i, "guest ABI input extent overflows");
        owned += bytes;
        if (plan->arguments[i].indirect) {
            if (needed > SIZE_MAX - 16 || bytes > SIZE_MAX - needed - 16) return guest_fail(error, QA_ERROR_MEMORY, i, "guest ABI temporary extent overflows");
            needed += bytes + 16;
        }
    }
    if (needed > SIZE_MAX - plan->stack_alignment - plan->word)
        return guest_fail(error, QA_ERROR_MEMORY, 0, "guest ABI stack reserve overflows");
    call_argument *arguments = count ? calloc(count, sizeof(*arguments)) : NULL;
    uint8_t *data = calloc(1, owned);
    if ((count && !arguments) || !data) {
        free(arguments); free(data); return guest_fail(error, QA_ERROR_MEMORY, 0, "retaining guest ABI call values");
    }
    size_t offset = layout->bytes < 8 ? 8 : layout->bytes;
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        arguments[i].data = data + offset;
        okay = encode_value(&plan->arguments[i].layout, &values[i], arguments[i].data, error);
        offset += plan->arguments[i].layout.bytes < 8 ? 8 : plan->arguments[i].layout.bytes;
    }
    qa_native_guest_cpu enclosing;
    if (okay) okay = qa_native_guest_cpu_read(guest, &enclosing, error);
    guest_callback_recovery *recovery=guest->recovery;
    bool recovery_owner=okay&&recovery&&!recovery->invocation&&!native;
    if(recovery_owner) recovery->invocation=&enclosing;
    bool nested = guest->run != NULL || guest->stopped_write_calls != 0;
    guest_host_x86_64_state hardware = {0};
    if (okay && native && nested) okay = guest_host_child_cpu_read(guest->child, &hardware, error);
    uint64_t original_sp = okay ? enclosing.registers[QA_NATIVE_RSP] : 0, sp = 0;
    if (okay && original_sp < needed + plan->stack_alignment + plan->word)
        okay = guest_fail(error, QA_ERROR_ARGUMENT, original_sp, "guest ABI stack reserve underflows");
    if (okay) sp = ((original_sp - needed) & ~(uint64_t)(plan->stack_alignment - 1)) - plan->word;
    uint64_t cursor = sp + plan->stack_bytes, result_address = 0;
    uint8_t hidden[8] = {0}, trap[8] = {0};
    if (okay && plan->memory_result) {
        uint64_t alignment = layout->alignment < 16 ? 16 : layout->alignment;
        cursor = (cursor + alignment - 1) & ~(alignment - 1); result_address = cursor; cursor += layout->bytes;
        store_integer(hidden, plan->word, result_address);
    }
    for (size_t i = 0; okay && i < count; ++i) if (plan->arguments[i].indirect) {
        cursor = (cursor + 15) & ~UINT64_C(15); arguments[i].temporary = cursor;
        cursor += plan->arguments[i].layout.bytes;
        store_integer(arguments[i].pointer, plan->word, arguments[i].temporary);
    }
    if (okay && cursor > original_sp) okay = guest_fail(error, QA_ERROR_ARGUMENT, cursor, "guest ABI temporaries exceed their owned stack reserve");
    if (okay) okay = guest_range(guest, sp, (size_t)(cursor - sp), QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, error);
    store_integer(trap, plan->word, return_trap);
    bool effected = false;
    if (okay) { effected = true; okay = qa_native_guest_write(guest, sp, (qa_bytes){trap, plan->word}, error); }
    if (okay && plan->memory_result) {
        okay = qa_native_guest_write(guest, result_address, (qa_bytes){data, layout->bytes}, error);
        if (okay) okay = write_stack(&plan->hidden, guest, sp, hidden, error);
    }
    for (size_t i = 0; okay && i < count; ++i) {
        const argument *arg = &plan->arguments[i];
        if (arg->indirect) okay = qa_native_guest_write(guest, arguments[i].temporary,
            (qa_bytes){arguments[i].data, arg->layout.bytes}, error);
        if (okay) okay = write_stack(arg, guest, sp, arg->indirect ? arguments[i].pointer : arguments[i].data, error);
    }
    qa_native_guest_cpu cpu;
    if (okay) okay = qa_native_guest_cpu_read(guest, &cpu, error);
    if (okay) {
        if (plan->memory_result) write_registers(&plan->hidden, hidden, &cpu);
        for (size_t i = 0; i < count; ++i) write_registers(&plan->arguments[i],
            plan->arguments[i].indirect ? arguments[i].pointer : arguments[i].data, &cpu);
        if (plan->abi == QA_NATIVE_ABI_SYSTEM_V_X64 && plan->variadic)
            cpu.registers[QA_NATIVE_RAX] = (cpu.registers[QA_NATIVE_RAX] & ~UINT64_C(255)) | plan->vectors;
        cpu.flags &= ~UINT64_C(0x400); cpu.registers[QA_NATIVE_RSP] = sp; cpu.instruction = target;
        okay = qa_native_guest_cpu_write(guest, &cpu, error);
    }
    if (okay) okay = native ? qa_native_guest_run_native(guest, target, return_trap, error) :
        bypass ? qa_native_guest_run_original(guest, bypass, target, return_trap, budget, error) :
        qa_native_guest_run(guest, target, return_trap, budget, error);
    if (okay) okay = qa_native_guest_cpu_read(guest, &cpu, error);
    if (okay && cpu.registers[QA_NATIVE_RSP] != sp + plan->word + plan->callee_pop)
        okay = guest_fail(error, QA_ERROR_FORMAT, cpu.registers[QA_NATIVE_RSP], "guest returned with incorrect actual ABI stack cleanup");
    if (okay && plan->memory_result) okay = qa_native_guest_read(guest, cpu.registers[QA_NATIVE_RAX], data, layout->bytes, error);
    else if (okay && plan->x87_result) okay = floating_result(plan, guest, data, false, error);
    else if (okay) {
        okay = qa_native_guest_cpu_read(guest, &cpu, error);
        if (okay) okay = read_locations(&plan->result, guest, &cpu, data, error);
    }
    if (okay) {
        if (native && nested) okay = guest_host_child_cpu_write(guest->child, &hardware, error);
        else if (nested) cpu = enclosing;
        else {
            okay = qa_native_guest_cpu_read(guest, &cpu, error);
            cpu.registers[QA_NATIVE_RSP] = original_sp; cpu.instruction = enclosing.instruction;
        }
        if (okay && !(native && nested)) okay = qa_native_guest_cpu_write(guest, &cpu, error);
    }
    if (okay) {
        if (layout->kind == QA_NATIVE_BYTES) {
            memcpy(result->as.bytes.data, data, layout->bytes);
            result->type = QA_NATIVE_BYTES; result->as.bytes.size = layout->bytes;
        } else decode_value(layout, data, result);
    }
    if(!okay&&recovery_owner&&recovery->cancelled&&!recovery->restored&&!recovery->resolved&&
        guest_callback_cancelled(guest,error)&&layout->kind==QA_NATIVE_VOID) {
        okay=qa_native_guest_cpu_write(guest,&enclosing,error);
        if(okay) {
            recovery->restored=true; *result=(qa_native_value){.type=QA_NATIVE_VOID};
            if(error) *error=(qa_error){0};
        }
    }
    if (!okay && effected && !guest_callback_cancelled(guest,error)) guest->failed = true;
    guest_host_x86_64_state_free(&hardware);
    free(arguments); free(data); return okay;
}

bool guest_abi_invoke(const guest_abi_plan *plan, qa_native_guest *guest, uint64_t target,
    uint64_t return_trap, const qa_native_value *values, size_t count,
    qa_native_value *result, size_t budget, qa_error *error)
{ return invoke(plan, guest, target, return_trap, values, count, result, budget, false, 0, error); }

bool guest_abi_invoke_original(const guest_abi_plan *plan, qa_native_guest *guest,
    uint64_t id, uint64_t target, uint64_t return_trap, const qa_native_value *values,
    size_t count, qa_native_value *result, size_t budget, qa_error *error)
{
    if (!id) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "original ABI invocation requires its callback identity");
    return invoke(plan, guest, target, return_trap, values, count, result, budget, false, id, error);
}

bool guest_abi_invoke_native(const guest_abi_plan *plan, qa_native_guest *guest, uint64_t target,
    uint64_t return_trap, const qa_native_value *values, size_t count,
    qa_native_value *result, qa_error *error)
{ return invoke(plan, guest, target, return_trap, values, count, result, 0, true, 0, error); }
