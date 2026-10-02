#include "internal.h"
#include "qa/binary.h"

typedef struct script_codec {
    bool decoding;
    const uint8_t *input;
    uint8_t *output;
    size_t size, offset, capacity;
    qa_arena *arena;
    qa_error *error;
} script_codec;

static bool codec_bad(script_codec *c, const char *message) {
    qa_error_set(c->error, QA_ERROR_FORMAT, c->offset, "%s", message);
    return false;
}
static bool codec_bytes(script_codec *c, void *bytes, size_t size) {
    if (c->decoding) {
        if (size > c->size - c->offset)
            return codec_bad(c, "Truncated script checkpoint");
        if (size)
            memcpy(bytes, c->input + c->offset, size);
    } else {
        if (size > SIZE_MAX - c->offset) {
            qa_error_set(c->error, QA_ERROR_MEMORY, c->offset,
                         "Script checkpoint encoding size overflow");
            return false;
        }
        if (!script_grow((void **)&c->output, &c->capacity, c->offset + size, 1, c->error))
            return false;
        if (size)
            memcpy(c->output + c->offset, bytes, size);
    }
    c->offset += size;
    return true;
}
static bool codec_u32(script_codec *c, uint32_t *value) {
    uint8_t bytes[4];
    if (!c->decoding)
        qa_store_u32le(bytes, *value);
    if (!codec_bytes(c, bytes, sizeof(bytes)))
        return false;
    if (c->decoding)
        *value = qa_load_u32le(bytes);
    return true;
}
static bool codec_u64(script_codec *c, uint64_t *value) {
    uint8_t bytes[8];
    if (!c->decoding)
        qa_store_u64le(bytes, *value);
    if (!codec_bytes(c, bytes, sizeof(bytes)))
        return false;
    if (c->decoding)
        *value = qa_load_u64le(bytes);
    return true;
}
static bool codec_size(script_codec *c, size_t *value) {
    uint64_t wide = *value;
    if (!codec_u64(c, &wide))
        return false;
    if (c->decoding) {
        if (wide > SIZE_MAX)
            return codec_bad(c, "Script checkpoint size exceeds native address space");
        *value = (size_t)wide;
    }
    return true;
}
static bool codec_index(script_codec *c, size_t *value) {
    uint64_t wide = *value == SIZE_MAX ? UINT64_MAX : (uint64_t)*value;
    if (!codec_u64(c, &wide))
        return false;
    if (c->decoding) {
        if (wide != UINT64_MAX && wide >= SIZE_MAX)
            return codec_bad(c, "Script checkpoint index exceeds native address space");
        *value = wide == UINT64_MAX ? SIZE_MAX : (size_t)wide;
    }
    return true;
}
static bool codec_bool(script_codec *c, bool *value) {
    uint8_t byte = *value ? 1 : 0;
    if (!codec_bytes(c, &byte, 1))
        return false;
    if (byte > 1)
        return codec_bad(c, "Invalid script checkpoint boolean");
    *value = byte != 0;
    return true;
}
static bool codec_span(script_codec *c, qa_bytes *value) {
    size_t size = value->size;
    if (!codec_size(c, &size))
        return false;
    if (c->decoding) {
        if (size > c->size - c->offset)
            return codec_bad(c, "Truncated script checkpoint byte span");
        *value = (qa_bytes){size ? c->input + c->offset : NULL, size};
        c->offset += size;
        return true;
    }
    return codec_bytes(c, (void *)value->data, size);
}
static bool codec_string(script_codec *c, const char **value) {
    uint64_t size = *value ? strlen(*value) : UINT64_MAX;
    if (!codec_u64(c, &size))
        return false;
    if (size == UINT64_MAX) {
        *value = NULL;
        return true;
    }
    if (size >= SIZE_MAX)
        return codec_bad(c, "Script checkpoint string is too large");
    size_t bytes = (size_t)size + 1;
    if (c->decoding) {
        if (bytes > c->size - c->offset)
            return codec_bad(c, "Truncated script checkpoint string");
        const uint8_t *text = c->input + c->offset;
        if (text[size] != 0 || (size && memchr(text, 0, (size_t)size)))
            return codec_bad(c, "Invalid script checkpoint string terminator");
        *value = (const char *)text;
        c->offset += bytes;
        return true;
    }
    return codec_bytes(c, (void *)*value, bytes);
}
static bool codec_array(script_codec *c, size_t count, size_t stride, size_t alignment,
                        size_t minimum_bytes, void **out) {
    if (!c->decoding)
        return true;
    if (count > (c->size - c->offset) / minimum_bytes || count > SIZE_MAX / stride)
        return codec_bad(c, "Script checkpoint array count exceeds its payload");
    *out = count ? qa_arena_alloc(c->arena, count * stride, alignment, c->error) : NULL;
    if (count && !*out)
        return false;
    if (count)
        memset(*out, 0, count * stride);
    return true;
}
static bool codec_location(script_codec *c, qa_script_location *value) {
    return codec_string(c, &value->path) && codec_u32(c, &value->line) &&
           codec_u32(c, &value->column) && codec_size(c, &value->offset);
}
static bool codec_token(script_codec *c, qa_script_token *value) {
    uint32_t kind = (uint32_t)value->kind, integer;
    uint64_t number;
    _Static_assert(sizeof(value->number) == sizeof(number), "Script numbers require binary64");
    memcpy(&integer, &value->integer, sizeof(integer));
    memcpy(&number, &value->number, sizeof(number));
    if (!codec_u32(c, &kind) || !codec_u32(c, &value->subtype) ||
        !codec_u32(c, &value->lines_crossed) || !codec_u32(c, &integer) || !codec_u64(c, &number) ||
        !codec_span(c, &value->text) || !codec_span(c, &value->leading_whitespace) ||
        !codec_location(c, &value->location))
        return false;
    if (kind > QA_SCRIPT_PUNCTUATION)
        return codec_bad(c, "Invalid script checkpoint token kind");
    value->kind = (qa_script_token_kind)kind;
    memcpy(&value->integer, &integer, sizeof(integer));
    memcpy(&value->number, &number, sizeof(number));
    return true;
}
static bool codec_options(script_codec *c, qa_script_options *value) {
    return codec_u32(c, &value->lexer_flags) && codec_size(c, &value->token_limit) &&
           codec_size(c, &value->maximum_include_depth) &&
           codec_size(c, &value->maximum_expansions) &&
           codec_size(c, &value->maximum_queued_tokens) &&
           codec_size(c, &value->maximum_output_tokens) && codec_size(c, &value->maximum_defines) &&
           codec_size(c, &value->maximum_expression_tokens) &&
           codec_size(c, &value->maximum_source_tokens) && codec_bool(c, &value->builtins) &&
           codec_string(c, &value->include_path);
}
static bool codec_macro(script_codec *c, qa_script_macro_state *value) {
    uint32_t builtin = value->builtin;
    if (!codec_span(c, &value->name) || !codec_u32(c, &builtin) ||
        !codec_bool(c, &value->function) || !codec_bool(c, &value->fixed) ||
        !codec_bool(c, &value->active) || !codec_u32(c,&value->pointer) ||
        !codec_index(c,&value->memory_reference) || !codec_span(c,&value->record) || !codec_size(c, &value->parameter_count))
        return false;
    if (value->parameter_count > 128)
        return codec_bad(c, "Too many script checkpoint macro parameters");
    value->builtin = builtin;
    qa_bytes *parameters = NULL;
    if (!codec_array(c, value->parameter_count, sizeof(*parameters), _Alignof(qa_bytes), 8,
                     (void **)&parameters))
        return false;
    for (size_t i = 0; i < value->parameter_count; ++i) {
        qa_bytes parameter = c->decoding ? (qa_bytes){0} : value->parameters[i];
        if (!codec_span(c, &parameter))
            return false;
        if (c->decoding)
            parameters[i] = parameter;
    }
    if (c->decoding)
        value->parameters = parameters;
    if (!codec_size(c, &value->token_count))
        return false;
    qa_script_token *tokens = NULL;
    if (!codec_array(c, value->token_count, sizeof(*tokens), _Alignof(qa_script_token), 64,
                     (void **)&tokens))
        return false;
    for (size_t i = 0; i < value->token_count; ++i) {
        qa_script_token token = c->decoding ? (qa_script_token){0} : value->tokens[i];
        if (!codec_token(c, &token))
            return false;
        if (c->decoding)
            tokens[i] = token;
    }
    if (c->decoding)
        value->tokens = tokens;
    return true;
}
static bool codec_frame(script_codec *c, qa_script_frame_state *value) {
    return codec_string(c, &value->path) && codec_span(c, &value->source) &&
           codec_size(c, &value->lexer.offset) && codec_u32(c, &value->lexer.line) &&
           codec_u32(c, &value->lexer.column) && codec_bool(c, &value->lexer.unread) &&
           codec_token(c, &value->lexer.token) &&
           codec_size(c, &value->condition_base) && codec_size(c, &value->token_count) &&
           codec_bool(c, &value->active) && codec_span(c,&value->script_record) &&
           codec_span(c,&value->punctuation_record) && codec_index(c,&value->script_reference) &&
           codec_index(c,&value->punctuation_reference) && codec_bool(c,&value->script_released) &&
           codec_bool(c,&value->punctuation_released);
}
/* A single field walk defines both directions; no structure bytes, pointer
 * values, native padding, or size_t widths enter the format. */
static bool codec_checkpoint(script_codec *c, qa_script_checkpoint *value) {
    if (!codec_u32(c, &value->version))
        return false;
    if (value->version != SCRIPT_CHECKPOINT_VERSION) {
        qa_error_set(c->error, QA_ERROR_UNSUPPORTED, c->offset - 4,
                     "Unsupported script checkpoint version");
        return false;
    }
    if (!codec_options(c, &value->options) || !codec_string(c, &value->date) ||
        !codec_string(c, &value->time) || !codec_size(c, &value->expansions) ||
        !codec_size(c, &value->outputs) || !codec_u32(c, &value->next_condition_pointer) || !codec_u32(c,&value->next_token_pointer) || !codec_u32(c,&value->next_define_pointer) ||
        !codec_u32(c,&value->define_first) || !codec_span(c,&value->define_hash) || !codec_index(c,&value->hash_reference) ||
        !codec_span(c, &value->source_record) || !codec_index(c, &value->source_reference) || !codec_bool(c, &value->empty_expansion) ||
        !codec_location(c, &value->last_location) || !codec_token(c, &value->raw_token) ||
        !codec_bool(c, &value->source_failure) || !codec_bool(c, &value->file_text) ||
        !codec_size(c, &value->macro_count))
        return false;
    qa_script_macro_state *macros = NULL;
    if (!codec_array(c, value->macro_count, sizeof(*macros), _Alignof(qa_script_macro_state), 31,
                     (void **)&macros))
        return false;
    for (size_t i = 0; i < value->macro_count; ++i) {
        qa_script_macro_state macro = c->decoding ? (qa_script_macro_state){0} : value->macros[i];
        if (!codec_macro(c, &macro))
            return false;
        if (c->decoding)
            macros[i] = macro;
    }
    if (c->decoding)
        value->macros = macros;
    if (!codec_size(c, &value->frame_count))
        return false;
    qa_script_frame_state *frames = NULL;
    if (!codec_array(c, value->frame_count, sizeof(*frames), _Alignof(qa_script_frame_state), 50,
                     (void **)&frames))
        return false;
    for (size_t i = 0; i < value->frame_count; ++i) {
        qa_script_frame_state frame = c->decoding ? (qa_script_frame_state){0} : value->frames[i];
        if (!codec_frame(c, &frame))
            return false;
        if (c->decoding)
            frames[i] = frame;
    }
    if (c->decoding)
        value->frames = frames;
    if (!codec_size(c, &value->stack_count))
        return false;
    size_t *stack = NULL;
    if (!codec_array(c, value->stack_count, sizeof(*stack), _Alignof(size_t), 8, (void **)&stack))
        return false;
    for (size_t i = 0; i < value->stack_count; ++i) {
        size_t index = c->decoding ? 0 : value->stack[i];
        if (!codec_size(c, &index))
            return false;
        if (c->decoding)
            stack[i] = index;
    }
    if (c->decoding)
        value->stack = stack;
    if (!codec_size(c, &value->expansion_count))
        return false;
    qa_script_expansion_state *expansions = NULL;
    if (!codec_array(c, value->expansion_count, sizeof(*expansions),
                     _Alignof(qa_script_expansion_state), 16, (void **)&expansions))
        return false;
    for (size_t i = 0; i < value->expansion_count; ++i) {
        qa_script_expansion_state expansion =
            c->decoding ? (qa_script_expansion_state){0} : value->expansion_states[i];
        if (!codec_size(c, &expansion.macro) || !codec_index(c, &expansion.parent))
            return false;
        if (c->decoding)
            expansions[i] = expansion;
    }
    if (c->decoding)
        value->expansion_states = expansions;
    if (!codec_size(c, &value->queue_count))
        return false;
    qa_script_queued_state *queue = NULL;
    if (!codec_array(c, value->queue_count, sizeof(*queue), _Alignof(qa_script_queued_state), 1160,
                     (void **)&queue))
        return false;
    for (size_t i = 0; i < value->queue_count; ++i) {
        qa_script_queued_state queued = c->decoding ? (qa_script_queued_state){0} : value->queue[i];
        if (!codec_token(c, &queued.token) || !codec_index(c, &queued.expansion) ||
            !codec_u32(c,&queued.pointer) || !codec_index(c,&queued.memory_reference) ||
            !codec_index(c,&queued.text_extent) || !codec_bytes(c,queued.bytes,sizeof(queued.bytes)))
            return false;
        if (c->decoding)
            queue[i] = queued;
    }
    if (c->decoding)
        value->queue = queue;
    if (!codec_size(c, &value->condition_count))
        return false;
    qa_script_condition_state *conditions = NULL;
    if (!codec_array(c, value->condition_count, sizeof(*conditions),
                     _Alignof(qa_script_condition_state), 38, (void **)&conditions))
        return false;
    for (size_t i = 0; i < value->condition_count; ++i) {
        qa_script_condition_state condition =
            c->decoding ? (qa_script_condition_state){0} : value->conditions[i];
        if (!codec_size(c, &condition.frame) || !codec_bool(c, &condition.skip) ||
            !codec_bool(c, &condition.was_else) || !codec_u32(c, &condition.pointer) ||
            !codec_index(c, &condition.memory_reference) || !codec_bytes(c, condition.bytes,16))
            return false;
        if (c->decoding)
            conditions[i] = condition;
    }
    if (c->decoding)
        value->conditions = conditions;
    return true;
}
bool qa_script_checkpoint_encode(const qa_script_checkpoint *checkpoint, qa_buffer *out,
                                 qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing script checkpoint encoding output");
        return false;
    }
    if (!script_checkpoint_valid(checkpoint, error))
        return false;
    script_codec codec = {.error = error};
    qa_script_checkpoint copy = *checkpoint;
    uint8_t magic[8] = {'Q', 'A', 'S', 'C', 'P', 0, 0, 0};
    if (!codec_bytes(&codec, magic, sizeof(magic)) || !codec_checkpoint(&codec, &copy)) {
        free(codec.output);
        return false;
    }
    *out = (qa_buffer){codec.output, codec.offset};
    return true;
}
bool qa_script_checkpoint_decode(qa_bytes encoded, qa_script_checkpoint *out, qa_error *error) {
    if (!out || !encoded.data || encoded.size < 12 || memcmp(encoded.data, "QASCP\0\0\0", 8)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid script checkpoint encoding header");
        return false;
    }
    script_checkpoint_storage *storage = calloc(1, sizeof(*storage));
    if (!storage) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating decoded script checkpoint");
        return false;
    }
    qa_script_checkpoint checkpoint = {.storage = storage};
    /* Spans and terminated strings share this immutable payload allocation. */
    char *bytes = script_string(&storage->arena, encoded.data, encoded.size, error);
    if (!bytes)
        goto fail;
    script_codec codec = {.decoding = true,
                          .input = (const uint8_t *)bytes,
                          .size = encoded.size,
                          .offset = 8,
                          .arena = &storage->arena,
                          .error = error};
    if (!codec_checkpoint(&codec, &checkpoint))
        goto fail;
    if (codec.offset != encoded.size) {
        codec_bad(&codec, "Trailing bytes after script checkpoint");
        goto fail;
    }
    if (!script_checkpoint_valid(&checkpoint, error))
        goto fail;
    *out = checkpoint;
    return true;
fail:
    qa_script_checkpoint_free(&checkpoint);
    return false;
}
