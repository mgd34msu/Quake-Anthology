#include "internal.h"
#include "qa/source_save.h"

typedef struct script_codec {
    qa_source_save_io io;
    qa_arena *arena;
} script_codec;

static bool codec_bad(script_codec *c, const char *message) {
    qa_error_set(c->io.error, QA_ERROR_FORMAT, c->io.offset, "%s", message);
    return false;
}
static bool codec_size(script_codec *c, size_t *value) {
    uint64_t wide = *value;
    if (!qa_source_save_u64(&c->io, &wide))
        return false;
    if (c->io.direction == QA_SOURCE_SAVE_READ) {
        if (wide > SIZE_MAX)
            return codec_bad(c, "Script checkpoint size exceeds native address space");
        *value = (size_t)wide;
    }
    return true;
}
static bool codec_index(script_codec *c, size_t *value) {
    uint64_t wide = *value == SIZE_MAX ? UINT64_MAX : (uint64_t)*value;
    if (!qa_source_save_u64(&c->io, &wide))
        return false;
    if (c->io.direction == QA_SOURCE_SAVE_READ) {
        if (wide != UINT64_MAX && wide >= SIZE_MAX)
            return codec_bad(c, "Script checkpoint index exceeds native address space");
        *value = wide == UINT64_MAX ? SIZE_MAX : (size_t)wide;
    }
    return true;
}
static bool codec_span(script_codec *c, qa_bytes *value) {
    size_t size = value->size;
    if (!codec_size(c, &size))
        return false;
    if (c->io.direction == QA_SOURCE_SAVE_READ) {
        if (!qa_source_save_span(&c->io, size, value))
            return false;
        if (!size)
            value->data = NULL;
        return true;
    }
    return qa_source_save_bytes(&c->io, (void *)value->data, size);
}
static bool codec_string(script_codec *c, const char **value) {
    uint64_t size = *value ? strlen(*value) : UINT64_MAX;
    if (!qa_source_save_u64(&c->io, &size))
        return false;
    if (size == UINT64_MAX) {
        *value = NULL;
        return true;
    }
    if (size >= SIZE_MAX)
        return codec_bad(c, "Script checkpoint string is too large");
    size_t bytes = (size_t)size + 1;
    if (c->io.direction == QA_SOURCE_SAVE_READ) {
        qa_bytes text;
        if (!qa_source_save_span(&c->io, bytes, &text))
            return false;
        if (text.data[size] != 0 || (size && memchr(text.data, 0, (size_t)size))) {
            c->io.offset -= bytes;
            return codec_bad(c, "Invalid script checkpoint string terminator");
        }
        *value = (const char *)text.data;
        return true;
    }
    return qa_source_save_bytes(&c->io, (void *)*value, bytes);
}
static bool codec_array(script_codec *c, size_t count, size_t stride, size_t alignment,
                        size_t minimum_bytes, void **out) {
    if (c->io.direction != QA_SOURCE_SAVE_READ)
        return true;
    if (count > (c->io.input.size - c->io.offset) / minimum_bytes || count > SIZE_MAX / stride)
        return codec_bad(c, "Script checkpoint array count exceeds its payload");
    *out = count ? qa_arena_alloc(c->arena, count * stride, alignment, c->io.error) : NULL;
    if (count && !*out)
        return false;
    if (count)
        memset(*out, 0, count * stride);
    return true;
}
static bool codec_location(script_codec *c, qa_script_location *value) {
    return codec_string(c, &value->path) && qa_source_save_u32(&c->io, &value->line) &&
           qa_source_save_u32(&c->io, &value->column) && codec_size(c, &value->offset);
}
static bool codec_token(script_codec *c, qa_script_token *value) {
    uint32_t kind = (uint32_t)value->kind, integer;
    uint64_t number;
    _Static_assert(sizeof(value->number) == sizeof(number), "Script numbers require binary64");
    memcpy(&integer, &value->integer, sizeof(integer));
    memcpy(&number, &value->number, sizeof(number));
    if (!qa_source_save_u32(&c->io, &kind) || !qa_source_save_u32(&c->io, &value->subtype) ||
        !qa_source_save_u32(&c->io, &value->lines_crossed) || !qa_source_save_u32(&c->io, &integer) || !qa_source_save_u64(&c->io, &number) ||
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
    return qa_source_save_u32(&c->io, &value->lexer_flags) && codec_size(c, &value->token_limit) &&
           codec_size(c, &value->maximum_include_depth) &&
           codec_size(c, &value->maximum_expansions) &&
           codec_size(c, &value->maximum_queued_tokens) &&
           codec_size(c, &value->maximum_output_tokens) && codec_size(c, &value->maximum_defines) &&
           codec_size(c, &value->maximum_expression_tokens) &&
           codec_size(c, &value->maximum_source_tokens) && qa_source_save_bool(&c->io, &value->builtins) &&
           codec_string(c, &value->include_path);
}
static bool codec_macro(script_codec *c, qa_script_macro_state *value) {
    uint32_t builtin = value->builtin;
    if (!codec_span(c, &value->name) || !qa_source_save_u32(&c->io, &builtin) ||
        !qa_source_save_bool(&c->io, &value->function) || !qa_source_save_bool(&c->io, &value->fixed) ||
        !qa_source_save_bool(&c->io, &value->active) || !qa_source_save_u32(&c->io,&value->pointer) ||
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
        qa_bytes parameter = (c->io.direction == QA_SOURCE_SAVE_READ) ? (qa_bytes){0} : value->parameters[i];
        if (!codec_span(c, &parameter))
            return false;
        if (c->io.direction == QA_SOURCE_SAVE_READ)
            parameters[i] = parameter;
    }
    if (c->io.direction == QA_SOURCE_SAVE_READ)
        value->parameters = parameters;
    if (!codec_size(c, &value->token_count))
        return false;
    qa_script_token *tokens = NULL;
    if (!codec_array(c, value->token_count, sizeof(*tokens), _Alignof(qa_script_token), 64,
                     (void **)&tokens))
        return false;
    for (size_t i = 0; i < value->token_count; ++i) {
        qa_script_token token = (c->io.direction == QA_SOURCE_SAVE_READ) ? (qa_script_token){0} : value->tokens[i];
        if (!codec_token(c, &token))
            return false;
        if (c->io.direction == QA_SOURCE_SAVE_READ)
            tokens[i] = token;
    }
    if (c->io.direction == QA_SOURCE_SAVE_READ)
        value->tokens = tokens;
    return true;
}
static bool codec_frame(script_codec *c, qa_script_frame_state *value) {
    return codec_string(c, &value->path) && codec_span(c, &value->source) &&
           codec_size(c, &value->lexer.offset) && qa_source_save_u32(&c->io, &value->lexer.line) &&
           qa_source_save_u32(&c->io, &value->lexer.column) && qa_source_save_bool(&c->io, &value->lexer.unread) &&
           codec_token(c, &value->lexer.token) &&
           codec_size(c, &value->condition_base) && codec_size(c, &value->token_count) &&
           qa_source_save_bool(&c->io, &value->active) && codec_span(c,&value->script_record) &&
           codec_span(c,&value->punctuation_record) && codec_index(c,&value->script_reference) &&
           codec_index(c,&value->punctuation_reference) && qa_source_save_bool(&c->io,&value->script_released) &&
           qa_source_save_bool(&c->io,&value->punctuation_released);
}
/* A single field walk defines both directions; no structure bytes, pointer
 * values, native padding, or size_t widths enter the format. */
static bool codec_checkpoint(script_codec *c, qa_script_checkpoint *value) {
    if (!codec_options(c, &value->options) || !codec_string(c, &value->date) ||
        !codec_string(c, &value->time) || !codec_size(c, &value->expansions) ||
        !codec_size(c, &value->outputs) || !qa_source_save_u32(&c->io, &value->next_condition_pointer) || !qa_source_save_u32(&c->io,&value->next_token_pointer) || !qa_source_save_u32(&c->io,&value->next_define_pointer) ||
        !qa_source_save_u32(&c->io,&value->define_first) || !codec_span(c,&value->define_hash) || !codec_index(c,&value->hash_reference) ||
        !codec_span(c, &value->source_record) || !codec_index(c, &value->source_reference) || !qa_source_save_bool(&c->io, &value->empty_expansion) ||
        !codec_location(c, &value->last_location) || !codec_token(c, &value->raw_token) ||
        !qa_source_save_bytes(&c->io,value->output_record,sizeof(value->output_record)) ||
        !codec_string(c,&value->output_unsupported) || !codec_string(c,&value->source_unsupported) ||
        !qa_source_save_bool(&c->io, &value->source_failure) || !qa_source_save_bool(&c->io, &value->file_text) ||
        !codec_size(c, &value->macro_count))
        return false;
    qa_script_macro_state *macros = NULL;
    if (!codec_array(c, value->macro_count, sizeof(*macros), _Alignof(qa_script_macro_state), 31,
                     (void **)&macros))
        return false;
    for (size_t i = 0; i < value->macro_count; ++i) {
        qa_script_macro_state macro = (c->io.direction == QA_SOURCE_SAVE_READ) ? (qa_script_macro_state){0} : value->macros[i];
        if (!codec_macro(c, &macro))
            return false;
        if (c->io.direction == QA_SOURCE_SAVE_READ)
            macros[i] = macro;
    }
    if (c->io.direction == QA_SOURCE_SAVE_READ)
        value->macros = macros;
    if (!codec_size(c, &value->frame_count))
        return false;
    qa_script_frame_state *frames = NULL;
    if (!codec_array(c, value->frame_count, sizeof(*frames), _Alignof(qa_script_frame_state), 50,
                     (void **)&frames))
        return false;
    for (size_t i = 0; i < value->frame_count; ++i) {
        qa_script_frame_state frame = (c->io.direction == QA_SOURCE_SAVE_READ) ? (qa_script_frame_state){0} : value->frames[i];
        if (!codec_frame(c, &frame))
            return false;
        if (c->io.direction == QA_SOURCE_SAVE_READ)
            frames[i] = frame;
    }
    if (c->io.direction == QA_SOURCE_SAVE_READ)
        value->frames = frames;
    if (!codec_size(c, &value->stack_count))
        return false;
    size_t *stack = NULL;
    if (!codec_array(c, value->stack_count, sizeof(*stack), _Alignof(size_t), 8, (void **)&stack))
        return false;
    for (size_t i = 0; i < value->stack_count; ++i) {
        size_t index = (c->io.direction == QA_SOURCE_SAVE_READ) ? 0 : value->stack[i];
        if (!codec_size(c, &index))
            return false;
        if (c->io.direction == QA_SOURCE_SAVE_READ)
            stack[i] = index;
    }
    if (c->io.direction == QA_SOURCE_SAVE_READ)
        value->stack = stack;
    if (!codec_size(c, &value->expansion_count))
        return false;
    qa_script_expansion_state *expansions = NULL;
    if (!codec_array(c, value->expansion_count, sizeof(*expansions),
                     _Alignof(qa_script_expansion_state), 16, (void **)&expansions))
        return false;
    for (size_t i = 0; i < value->expansion_count; ++i) {
        qa_script_expansion_state expansion =
            (c->io.direction == QA_SOURCE_SAVE_READ) ? (qa_script_expansion_state){0} : value->expansion_states[i];
        if (!codec_size(c, &expansion.macro) || !codec_index(c, &expansion.parent))
            return false;
        if (c->io.direction == QA_SOURCE_SAVE_READ)
            expansions[i] = expansion;
    }
    if (c->io.direction == QA_SOURCE_SAVE_READ)
        value->expansion_states = expansions;
    if (!codec_size(c, &value->queue_count))
        return false;
    qa_script_queued_state *queue = NULL;
    if (!codec_array(c, value->queue_count, sizeof(*queue), _Alignof(qa_script_queued_state), 1160,
                     (void **)&queue))
        return false;
    for (size_t i = 0; i < value->queue_count; ++i) {
        qa_script_queued_state queued = (c->io.direction == QA_SOURCE_SAVE_READ) ? (qa_script_queued_state){0} : value->queue[i];
        if (!codec_token(c, &queued.token) || !codec_index(c, &queued.expansion) ||
            !qa_source_save_u32(&c->io,&queued.pointer) || !codec_index(c,&queued.memory_reference) ||
            !codec_index(c,&queued.text_extent) || !qa_source_save_bytes(&c->io,queued.bytes,sizeof(queued.bytes)) ||
            !codec_string(c,&queued.unsupported))
            return false;
        if (c->io.direction == QA_SOURCE_SAVE_READ)
            queue[i] = queued;
    }
    if (c->io.direction == QA_SOURCE_SAVE_READ)
        value->queue = queue;
    if (!codec_size(c, &value->condition_count))
        return false;
    qa_script_condition_state *conditions = NULL;
    if (!codec_array(c, value->condition_count, sizeof(*conditions),
                     _Alignof(qa_script_condition_state), 38, (void **)&conditions))
        return false;
    for (size_t i = 0; i < value->condition_count; ++i) {
        qa_script_condition_state condition =
            (c->io.direction == QA_SOURCE_SAVE_READ) ? (qa_script_condition_state){0} : value->conditions[i];
        if (!codec_size(c, &condition.frame) || !qa_source_save_bool(&c->io, &condition.skip) ||
            !qa_source_save_bool(&c->io, &condition.was_else) || !qa_source_save_u32(&c->io, &condition.pointer) ||
            !codec_index(c, &condition.memory_reference) || !qa_source_save_bytes(&c->io, condition.bytes,16))
            return false;
        if (c->io.direction == QA_SOURCE_SAVE_READ)
            conditions[i] = condition;
    }
    if (c->io.direction == QA_SOURCE_SAVE_READ)
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
    script_codec codec = {0};
    if (!qa_source_save_writer(&codec.io, NULL, error))
        return false;
    qa_script_checkpoint copy = *checkpoint;
    uint8_t magic[8] = {'Q', 'A', 'S', 'C', 'P', 0, 0, 0};
    if (!qa_source_save_bytes(&codec.io, magic, sizeof(magic)) || !codec_checkpoint(&codec, &copy)) {
        qa_source_save_dispose(&codec.io);
        return false;
    }
    bool ok = qa_source_save_finish(&codec.io, out);
    qa_source_save_dispose(&codec.io);
    return ok;
}
bool qa_script_checkpoint_decode(qa_bytes encoded, qa_script_checkpoint *out, qa_error *error) {
    if (!out || !encoded.data || encoded.size < 8 || memcmp(encoded.data, "QASCP\0\0\0", 8)) {
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
    script_codec codec = {.arena = &storage->arena};
    if (!qa_source_save_reader(&codec.io, NULL, (qa_bytes){(const uint8_t *)bytes, encoded.size}, error))
        goto fail;
    codec.io.offset = 8;
    if (!codec_checkpoint(&codec, &checkpoint))
        goto fail;
    if (!qa_source_save_finish(&codec.io, NULL))
        goto fail;
    if (!script_checkpoint_valid(&checkpoint, error))
        goto fail;
    *out = checkpoint;
    return true;
fail:
    qa_script_checkpoint_free(&checkpoint);
    return false;
}
