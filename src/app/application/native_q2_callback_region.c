#include "native_q2_callback_region.h"
#include "qa/binary.h"
#include "qa/native_region_scope.h"
#include <stdlib.h>
#include <string.h>

typedef enum region_location_kind { REGION_REGISTER, REGION_SIMD, REGION_STACK } region_location_kind;
typedef struct region_location {
    region_location_kind kind;
    qa_native_value_type storage;
    size_t bytes, offset;
    uint32_t index;
} region_location;
typedef struct region_recipe {
    qa_native_target target;
    uint64_t frame_entry, entry, join, frame_exit, stack_bytes, argument_bytes;
    region_location result, *inputs;
    size_t count;
    uint32_t declared;
} region_recipe;

static const char *const storage_names[] = {"int8", "uint8", "int16", "uint16", "int32", "uint32",
    "int64", "uint64", "float32", "float64", "pointer"};

static bool fail(qa_error *error, qa_status status, const char *text)
{ qa_error_set(error, status, 0, text); return false; }

static bool integer(const qa_json_document *document, qa_json_id object,
    const char *name, uint64_t *out, qa_error *error)
{
    return qa_json_u64(document, qa_json_get(document, object, name), out, error) &&
        (*out <= UINT64_C(9007199254740991) || fail(error, QA_ERROR_FORMAT, "Native region field exceeds its source integer domain"));
}

static bool location_read(const qa_json_document *document, qa_json_id id,
    const region_recipe *recipe, region_location *out, qa_error *error)
{
    static const size_t widths[] = {1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 0};
    qa_json_id storage = qa_json_get(document, id, "storage"), kind = qa_json_get(document, id, "kind");
    region_location value = {0}; bool found = false;
    for (size_t i = 0; i < sizeof(storage_names) / sizeof(*storage_names); ++i)
        if (qa_json_string_equal(document, storage, storage_names[i])) {
            value.storage = (qa_native_value_type)(QA_NATIVE_I8 + i);
            value.bytes = widths[i] ? widths[i] : recipe->target.pointer_bytes;
            found = true; break;
        }
    if (!found) return fail(error, QA_ERROR_FORMAT, "Native region field has no scalar storage");
    if (qa_json_string_equal(document, kind, "register")) {
        static const char *const registers[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
            "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
        qa_json_id name = qa_json_get(document, id, "register"); found = false;
        for (uint32_t i = 0; i < QA_NATIVE_REGISTER_COUNT; ++i)
            if (i != QA_NATIVE_RSP && qa_json_string_equal(document, name, registers[i])) {
                value.index = i; found = true; break;
            }
        if (!found || (recipe->target.pointer_bytes == 4 && (value.bytes > 4 || value.index >= QA_NATIVE_R8)))
            return fail(error, QA_ERROR_FORMAT, "Native region register exceeds its source architecture");
        value.kind = REGION_REGISTER;
    } else {
        uint64_t offset;
        if (!integer(document, id, "offset", &offset, error)) return false;
        if (offset > SIZE_MAX) return fail(error, QA_ERROR_FORMAT, "Native region offset exceeds native storage");
        value.offset = (size_t)offset;
        if (qa_json_string_equal(document, kind, "simd")) {
            uint64_t index;
            if (value.storage == QA_NATIVE_ADDRESS || !integer(document, id, "index", &index, error) ||
                index >= (recipe->target.pointer_bytes == 4 ? 8u : 16u) || offset > 16 || value.bytes > 16 - offset)
                return fail(error, QA_ERROR_FORMAT, "Native region SIMD field exceeds its register");
            value.kind = REGION_SIMD; value.index = (uint32_t)index;
        } else if (qa_json_string_equal(document, kind, "stack")) {
            uint64_t end = recipe->stack_bytes + recipe->target.pointer_bytes + recipe->argument_bytes;
            if (offset > end || value.bytes > end - offset ||
                (offset < recipe->stack_bytes + recipe->target.pointer_bytes && offset + value.bytes > recipe->stack_bytes))
                return fail(error, QA_ERROR_FORMAT, "Native region stack field exceeds its frame or overlaps its return address");
            value.kind = REGION_STACK;
        } else return fail(error, QA_ERROR_FORMAT, "Native region field has no declared location");
    }
    *out = value; return true;
}

static bool input_storage(const qa_json_document *document, qa_json_id value,
    const region_location *location, qa_error *error)
{
    qa_json_id kind = qa_json_get(document, value, "kind");
    const char *expected = storage_names[location->storage - QA_NATIVE_I8];
    bool matches;
    if (qa_json_string_equal(document, kind, "time"))
        matches = location->storage != QA_NATIVE_ADDRESS &&
            qa_json_string_equal(document, qa_json_get(document, value, "encoding"), expected);
    else if (qa_json_string_equal(document, kind, "client")) matches = location->storage == QA_NATIVE_I32;
    else if (qa_json_string_equal(document, kind, "actor") || qa_json_string_equal(document, kind, "address") ||
        qa_json_string_equal(document, kind, "vector") || qa_json_string_equal(document, kind, "string") ||
        qa_json_string_equal(document, kind, "userinfo") || qa_json_string_equal(document, kind, "user-command"))
        matches = location->storage == QA_NATIVE_ADDRESS;
    else matches = qa_json_string_equal(document, kind, expected) && location->storage != QA_NATIVE_ADDRESS;
    return matches || fail(error, QA_ERROR_FORMAT, "Native region input differs from its declared machine storage");
}

static bool overlap(const region_location *a, const region_location *b)
{
    if (a->kind != b->kind || (a->kind != REGION_STACK && a->index != b->index)) return false;
    return a->offset < b->offset + b->bytes && b->offset < a->offset + a->bytes;
}

static bool recipe_read(qa_native_instance *instance, const qa_native_declaration *declaration, const qa_json_document *document,
    qa_json_id definition, region_recipe *out, qa_error *error)
{
    const qa_native_module *module = instance ? qa_native_get_module(instance) : NULL;
    qa_bytes acquired = qa_native_declaration_callbacks(declaration);
    qa_bytes source = document ? qa_json_source(document, qa_json_root(document)) : (qa_bytes){0};
    qa_bytes root = {0};
    if (!acquired.data) return fail(error, QA_ERROR_ARGUMENT, "Native region has no acquired callback declaration");
    if (!qa_native_declaration_source(declaration, "", &root, error)) return false;
    if (!module || !document || source.data != root.data || source.size != root.size ||
        !qa_json_string_equal(document, qa_json_get(document, definition, "abi"), "source-region"))
        return fail(error, QA_ERROR_ARGUMENT, "Native region requires its actual source instance and acquired declaration");
    region_recipe value = {.target = qa_native_module_describe(module).image.target};
    if (value.target.arch != QA_NATIVE_ARCH_I386 && value.target.arch != QA_NATIVE_ARCH_X86_64)
        return fail(error, QA_ERROR_UNSUPPORTED, "Native region requires the declared x86 processor");
    qa_json_id frame = qa_json_get(document, definition, "frame");
    if (!integer(document, frame, "entry", &value.frame_entry, error) ||
        !integer(document, frame, "exit", &value.frame_exit, error) ||
        !integer(document, frame, "stackBytes", &value.stack_bytes, error) ||
        !integer(document, frame, "argumentBytes", &value.argument_bytes, error) ||
        !integer(document, definition, "entry", &value.entry, error) ||
        !integer(document, definition, "join", &value.join, error)) return false;
    uint64_t boundaries[] = {value.frame_entry, value.entry, value.join, value.frame_exit};
    for (size_t i = 0; i < sizeof(boundaries) / sizeof(*boundaries); ++i) {
        for (size_t j = 0; j < i; ++j)
            if (boundaries[i] == boundaries[j]) return fail(error, QA_ERROR_FORMAT, "Native region frame boundaries overlap");
        qa_native_address address;
        if (!qa_native_rva(instance, boundaries[i], 1, &address, error) ||
            !qa_native_range_check(instance, address, 1, QA_NATIVE_MEMORY_EXECUTE, error)) return false;
    }
    bool admitted = false;
    for (size_t i = 0; i < qa_native_declaration_region_count(declaration); ++i) {
        qa_native_declared_region region;
        if (!qa_native_declaration_region(declaration, i, &region, error)) return false;
        if (region.has_frame && region.entry_rva == value.entry && region.join_rva == value.join &&
            region.frame_entry_rva == value.frame_entry && region.frame_exit_rva == value.frame_exit) {
            value.declared = region.id; admitted = true; break;
        }
    }
    if (!admitted) return fail(error, QA_ERROR_FORMAT, "Native region differs from its acquired instruction frame");
    if (value.stack_bytes % value.target.pointer_bytes || value.argument_bytes % value.target.pointer_bytes ||
        value.stack_bytes > UINT64_MAX - value.target.pointer_bytes - value.argument_bytes)
        return fail(error, QA_ERROR_FORMAT, "Native region frame extents are not source-pointer aligned");
    qa_json_id call = qa_json_get(document, definition, "call"), skips = qa_json_get(document, call, "skips");
    if (skips != QA_JSON_NONE && (qa_json_type(document, skips) != QA_JSON_ARRAY || qa_json_size(document, skips)))
        return fail(error, QA_ERROR_FORMAT, "Native standalone region cannot contain whole-call skips");
    qa_json_id inputs = qa_json_get(document, definition, "inputs");
    if (qa_json_type(document, inputs) != QA_JSON_ARRAY)
        return fail(error, QA_ERROR_FORMAT, "Native region has no source input roster");
    value.count = qa_json_size(document, inputs);
    if (value.count > SIZE_MAX / sizeof(*value.inputs)) return fail(error, QA_ERROR_MEMORY, "Native region input roster exceeds storage");
    value.inputs = value.count ? calloc(value.count, sizeof(*value.inputs)) : NULL;
    if (value.count && !value.inputs) return fail(error, QA_ERROR_MEMORY, "Owning native region input locations");
    bool okay = location_read(document, qa_json_get(document, definition, "result"), &value, &value.result, error);
    if (okay && value.result.storage == QA_NATIVE_ADDRESS) okay = fail(error, QA_ERROR_FORMAT, "Native region result cannot be a pointer");
    for (size_t i = 0; okay && i < value.count; ++i) {
        qa_json_id input = qa_json_at(document, inputs, i);
        okay = location_read(document, qa_json_get(document, input, "target"), &value, value.inputs + i, error) &&
            input_storage(document, qa_json_get(document, input, "value"), value.inputs + i, error);
        for (size_t j = 0; okay && j < i; ++j)
            if (overlap(value.inputs + i, value.inputs + j)) okay = fail(error, QA_ERROR_FORMAT, "Native region input locations overlap");
    }
    if (!okay) { free(value.inputs); return false; }
    *out = value; return true;
}

bool application_native_q2_callback_region_validate(qa_native_instance *instance, const qa_native_declaration *declaration,
    const qa_json_document *document, qa_json_id definition, qa_error *error)
{
    region_recipe recipe;
    if (!recipe_read(instance, declaration, document, definition, &recipe, error)) return false;
    free(recipe.inputs); return true;
}

static bool argument_area(const region_recipe *recipe, const qa_native_signature *signature, qa_error *error)
{
    size_t bytes;
    return qa_native_region_scope_argument_bytes(signature, &bytes, error) &&
        (recipe->argument_bytes == bytes ||
            fail(error, QA_ERROR_FORMAT, "Native region argument area differs from its original call ABI"));
}

static bool call_target(qa_native_instance *instance, const qa_json_document *document,
    qa_json_id definition, qa_native_address actual, qa_error *error)
{
    qa_json_id entry = qa_json_get(document, qa_json_get(document, definition, "call"), "entry");
    qa_json_id kind = qa_json_get(document, entry, "kind");
    qa_native_address expected;
    if (qa_json_string_equal(document, kind, "rva")) {
        uint64_t rva;
        if (!integer(document, entry, "rva", &rva, error) ||
            !qa_native_rva(instance, rva, 1, &expected, error)) return false;
    } else {
        qa_buffer name = {0};
        if (!qa_json_string(document, qa_json_get(document, entry, "name"), &name, error)) return false;
        bool okay = !memchr(name.data, 0, name.size);
        if (okay && qa_json_string_equal(document, kind, "export"))
            okay = qa_native_export(instance, (char *)name.data, &expected, error);
        else if (okay && qa_json_string_equal(document, kind, "game-export"))
            okay = qa_native_entry_address(instance, (char *)name.data, &expected, error);
        else okay = fail(error, QA_ERROR_FORMAT, "Native region call has no admitted source entry");
        qa_buffer_free(&name);
        if (!okay) return false;
    }
    return expected == actual || fail(error, QA_ERROR_ARGUMENT, "Native region target differs from its acquired original call");
}

bool application_native_q2_callback_region_call_validate(qa_native_instance *instance,
    const qa_native_declaration *declaration, const qa_json_document *document,
    qa_json_id definition, const qa_native_signature *signature, qa_error *error)
{
    region_recipe recipe;
    if (!recipe_read(instance, declaration, document, definition, &recipe, error)) return false;
    bool okay = argument_area(&recipe, signature, error);
    free(recipe.inputs); return okay;
}

typedef enum region_phase { REGION_PROLOGUE, REGION_INPUTS, REGION_EXECUTING, REGION_EPILOGUE } region_phase;
typedef struct region_stack_input {
    qa_native_address address;
    uint8_t before[8];
    size_t bytes;
} region_stack_input;
struct application_native_q2_callback_region {
    qa_native_instance *instance;
    const qa_native_module *module;
    qa_native_region_scope *lower;
    qa_native_region_snapshot *before;
    region_recipe recipe;
    qa_native_value *values, output;
    region_stack_input *stack;
    size_t stack_count;
    application_native_q2_region_current_fn current;
    void *context;
    uint64_t entry_stack;
    region_phase phase;
    bool has_entry, has_output, invoking;
};

static bool scope_current(application_native_q2_callback_region *scope, qa_error *error)
{
    return scope && qa_native_get_module(scope->instance) == scope->module &&
        qa_native_region_scope_current(scope->lower) && scope->current(scope->context, error);
}

static bool encode(region_location location, qa_native_value value, uint8_t bytes[8], qa_error *error)
{
    uint64_t raw;
    if (location.storage == QA_NATIVE_ADDRESS) {
        if (value.type != QA_NATIVE_ADDRESS) return fail(error, QA_ERROR_FORMAT, "Native region pointer input has no actual source pointer");
        raw = value.as.address;
        if (location.bytes == 4 && raw > UINT32_MAX) return fail(error, QA_ERROR_FORMAT, "Native region pointer exceeds its source address width");
    } else if (location.storage == QA_NATIVE_F32 || location.storage == QA_NATIVE_F64) {
        double number;
        if (value.type == QA_NATIVE_F32) number = value.as.f32;
        else if (value.type == QA_NATIVE_F64) number = value.as.f64;
        else return fail(error, QA_ERROR_FORMAT, "Native region floating input has no floating value");
        if (location.storage == QA_NATIVE_F32) {
            float narrowed = (float)number; uint32_t bits; memcpy(&bits, &narrowed, sizeof(bits));
            qa_store_u32le(bytes, bits); return true;
        }
        memcpy(&raw, &number, sizeof(raw));
    } else {
        switch (value.type) {
        case QA_NATIVE_I8: raw = (uint64_t)value.as.i8; break;
        case QA_NATIVE_U8: raw = value.as.u8; break;
        case QA_NATIVE_I16: raw = (uint64_t)value.as.i16; break;
        case QA_NATIVE_U16: raw = value.as.u16; break;
        case QA_NATIVE_I32: raw = (uint64_t)value.as.i32; break;
        case QA_NATIVE_U32: raw = value.as.u32; break;
        case QA_NATIVE_I64: raw = (uint64_t)value.as.i64; break;
        case QA_NATIVE_U64: raw = value.as.u64; break;
        default: return fail(error, QA_ERROR_FORMAT, "Native region integer input has no integer value");
        }
    }
    qa_store_u64le(bytes, raw); return true;
}

static qa_native_value decode(region_location location, const uint8_t bytes[8])
{
    qa_native_value value = {.type = location.storage};
    switch (location.storage) {
    case QA_NATIVE_I8: value.as.i8 = (int8_t)bytes[0]; break;
    case QA_NATIVE_U8: value.as.u8 = bytes[0]; break;
    case QA_NATIVE_I16: value.as.i16 = (int16_t)qa_load_u16le(bytes); break;
    case QA_NATIVE_U16: value.as.u16 = qa_load_u16le(bytes); break;
    case QA_NATIVE_I32: value.as.i32 = (int32_t)qa_load_u32le(bytes); break;
    case QA_NATIVE_U32: value.as.u32 = qa_load_u32le(bytes); break;
    case QA_NATIVE_I64: value.as.i64 = (int64_t)qa_load_u64le(bytes); break;
    case QA_NATIVE_U64: value.as.u64 = qa_load_u64le(bytes); break;
    case QA_NATIVE_F32: { uint32_t raw = qa_load_u32le(bytes); memcpy(&value.as.f32, &raw, sizeof(raw)); break; }
    case QA_NATIVE_F64: { uint64_t raw = qa_load_u64le(bytes); memcpy(&value.as.f64, &raw, sizeof(raw)); break; }
    default: break;
    }
    return value;
}

static bool stack_address(const application_native_q2_callback_region *scope,
    const qa_native_processor_state *state, size_t offset, qa_native_address *out, qa_error *error)
{
    uint64_t stack = state->registers[QA_NATIVE_RSP];
    uint64_t maximum = scope->recipe.target.pointer_bytes == 4 ? UINT32_MAX : UINT64_MAX;
    if (!stack || stack > maximum || offset > maximum - stack)
        return fail(error, QA_ERROR_FORMAT, "Native region has no actual stack address");
    *out = stack + offset; return true;
}

static bool register_available(const application_native_q2_callback_region *scope,
    region_location location, qa_error *error)
{
    return !(scope->recipe.target.pointer_bytes == 4 && location.bytes == 1 && location.index > QA_NATIVE_RBX) ||
        fail(error, QA_ERROR_FORMAT, "Native region low byte register is unavailable in i386 mode");
}

static bool input_write(application_native_q2_callback_region *scope, region_location location,
    qa_native_value value, const qa_native_region_scope_event *event,
    qa_native_processor_state *state, qa_error *error)
{
    uint8_t bytes[8] = {0};
    if (!encode(location, value, bytes, error)) return false;
    if (location.kind == REGION_REGISTER) {
        if (!register_available(scope, location, error)) return false;
        uint64_t raw = qa_load_u64le(bytes);
        if (location.bytes == 8) state->registers[location.index] = raw;
        else if (location.bytes == 4) state->registers[location.index] = (uint32_t)raw;
        else {
            uint64_t mask = location.bytes == 2 ? UINT16_MAX : UINT8_MAX;
            state->registers[location.index] = (state->registers[location.index] & ~mask) | (raw & mask);
        }
    } else if (location.kind == REGION_SIMD) memcpy(state->simd[location.index] + location.offset, bytes, location.bytes);
    else {
        region_stack_input *saved = scope->stack + scope->stack_count;
        if (!stack_address(scope, state, location.offset, &saved->address, error) ||
            !qa_native_read(scope->instance, saved->address, saved->before, location.bytes, error) ||
            !scope_current(scope, error)) return false;
        saved->bytes = location.bytes; ++scope->stack_count;
        if (!qa_native_write(scope->instance, saved->address, (qa_bytes){bytes, location.bytes}, error) ||
            !scope_current(scope, error)) return false;
    }
    return location.kind == REGION_STACK ||
        (qa_native_region_scope_write(scope->lower, event, state, error) && scope_current(scope, error));
}

static bool result_read(application_native_q2_callback_region *scope,
    const qa_native_processor_state *state, qa_error *error)
{
    region_location location = scope->recipe.result;
    uint8_t bytes[8] = {0};
    if (location.kind == REGION_REGISTER) {
        if (!register_available(scope, location, error)) return false;
        qa_store_u64le(bytes, state->registers[location.index]);
    } else if (location.kind == REGION_SIMD) memcpy(bytes, state->simd[location.index] + location.offset, location.bytes);
    else {
        qa_native_address address;
        if (!stack_address(scope, state, location.offset, &address, error) ||
            !qa_native_read(scope->instance, address, bytes, location.bytes, error) || !scope_current(scope, error)) return false;
    }
    scope->output = decode(location, bytes); scope->has_output = true; return true;
}

static bool frame_current(const application_native_q2_callback_region *scope,
    const qa_native_processor_state *state, qa_error *error)
{
    return (scope->has_entry && scope->entry_stack >= scope->recipe.stack_bytes &&
        state->registers[QA_NATIVE_RSP] == scope->entry_stack - scope->recipe.stack_bytes) ||
        fail(error, QA_ERROR_FORMAT, "Native donor stack differs from its declared region frame");
}

static bool boundary(void *context, qa_native_instance *instance,
    const qa_native_region_scope_event *event, qa_native_region_scope_decision *decision, qa_error *error)
{
    application_native_q2_callback_region *scope = context;
    if (instance != scope->instance || event->scope != scope->lower || !scope->invoking || !scope_current(scope, error)) return false;
    if (event->phase == QA_NATIVE_SCOPE_TARGET_ENTRY) {
        if (!scope->has_entry) { scope->entry_stack = event->state.registers[QA_NATIVE_RSP]; scope->has_entry = true; }
    } else if (event->phase == QA_NATIVE_SCOPE_FRAME_ENTRY && scope->phase == REGION_PROLOGUE) {
        if (!frame_current(scope, &event->state, error) ||
            !qa_native_region_scope_capture(scope->lower, event, &scope->before, error)) return false;
        decision->state = event->state; decision->replace_state = true;
        for (size_t i = 0; i < scope->recipe.count; ++i)
            if (!input_write(scope, scope->recipe.inputs[i], scope->values[i], event, &decision->state, error)) return false;
        if (!scope_current(scope, error)) return false;
        scope->phase = REGION_INPUTS; decision->action = QA_NATIVE_SCOPE_SKIP_TO_ENTRY;
    } else if (event->phase == QA_NATIVE_SCOPE_REGION_ENTER && scope->phase == REGION_INPUTS) {
        if (!frame_current(scope, &event->state, error)) return false;
        scope->phase = REGION_EXECUTING;
    } else if (event->phase == QA_NATIVE_SCOPE_REGION_JOIN && scope->phase == REGION_EXECUTING) {
        if (!frame_current(scope, &event->state, error) ||
            !result_read(scope, &event->state, error) || !scope->before) return false;
        while (scope->stack_count) {
            region_stack_input *saved = scope->stack + scope->stack_count - 1;
            if (!qa_native_write(scope->instance, saved->address, (qa_bytes){saved->before, saved->bytes}, error) ||
                !scope_current(scope, error)) return false;
            --scope->stack_count;
        }
        if (!qa_native_region_scope_restore(scope->before, event, decision, error) || !scope_current(scope, error)) return false;
        scope->phase = REGION_EPILOGUE; decision->action = QA_NATIVE_SCOPE_SKIP_TO_FRAME_EXIT;
    }
    return true;
}

bool application_native_q2_callback_region_execute(qa_native_instance *instance, const qa_native_declaration *declaration,
    const qa_json_document *document, qa_json_id definition, qa_native_address target,
    const qa_native_signature *signature, const qa_native_value *arguments, size_t argument_count,
    const qa_native_value *inputs, size_t input_count,
    application_native_q2_region_current_fn current, void *context,
    application_native_q2_callback_region **out, qa_native_value *result, bool *entered, qa_error *error)
{
    if (entered) *entered = false;
    if (!out || *out || !result || !entered || !current || !signature || (input_count && !inputs))
        return fail(error, QA_ERROR_ARGUMENT, "Native region requires its actual invocation owner and borrowed inputs");
    if (!current(context, error)) return false;
    region_recipe recipe;
    if (!recipe_read(instance, declaration, document, definition, &recipe, error)) return false;
    if (!argument_area(&recipe, signature, error) ||
        !call_target(instance, document, definition, target, error)) { free(recipe.inputs); return false; }
    if (input_count != recipe.count || input_count > SIZE_MAX / sizeof(qa_native_value) || input_count > SIZE_MAX / sizeof(region_stack_input)) {
        free(recipe.inputs); return fail(error, QA_ERROR_FORMAT, "Native region input count differs from its source declaration");
    }
    application_native_q2_callback_region *scope = calloc(1, sizeof(*scope));
    if (!scope) { free(recipe.inputs); return fail(error, QA_ERROR_MEMORY, "Owning native source region execution"); }
    scope->instance = instance; scope->module = qa_native_get_module(instance); scope->recipe = recipe;
    scope->current = current; scope->context = context;
    *out = scope;
    scope->values = input_count ? malloc(input_count * sizeof(*scope->values)) : NULL;
    scope->stack = input_count ? calloc(input_count, sizeof(*scope->stack)) : NULL;
    if (input_count && (!scope->values || !scope->stack)) return fail(error, QA_ERROR_MEMORY, "Owning lowered native region inputs");
    if (input_count) memcpy(scope->values, inputs, input_count * sizeof(*inputs));
    if (!qa_native_region_scope_open(instance, declaration, recipe.declared, target, boundary, scope, &scope->lower, error)) return false;
    qa_native_value ignored = {0}; scope->invoking = true;
    bool okay = qa_native_region_scope_invoke(scope->lower, signature, arguments, argument_count,
        signature->result.kind == QA_NATIVE_VOID ? NULL : &ignored, entered, error);
    scope->invoking = false;
    if (!okay || !scope_current(scope, error)) return false;
    if (scope->phase != REGION_EPILOGUE || !scope->has_output)
        return fail(error, QA_ERROR_FORMAT, "Native donor did not complete its declared region");
    *result = scope->output; return true;
}

bool application_native_q2_callback_region_close(application_native_q2_callback_region **owner, qa_error *error)
{
    if (!owner || !*owner) return true;
    application_native_q2_callback_region *scope = *owner;
    if (scope->invoking) return fail(error, QA_ERROR_ARGUMENT, "Native region close overlaps its actual source invocation");
    if (!qa_native_region_scope_close(&scope->lower, error)) return false;
    free(scope->recipe.inputs); free(scope->values); free(scope->stack); free(scope); *owner = NULL; return true;
}
