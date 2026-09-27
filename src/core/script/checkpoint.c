#include "internal.h"

static void *array(qa_arena *arena, size_t count, size_t stride, size_t alignment, qa_error *e) {
    if (count == 0)
        return NULL;
    if (count > SIZE_MAX / stride) {
        qa_error_set(e, QA_ERROR_MEMORY, count, "Script checkpoint array overflow");
        return NULL;
    }
    void *out = qa_arena_alloc(arena, count * stride, alignment, e);
    if (out != NULL)
        memset(out, 0, count * stride);
    return out;
}
static bool copy_bytes(qa_arena *arena, qa_bytes input, qa_bytes *out, qa_error *e) {
    if (input.size == 0) {
        *out = (qa_bytes){0};
        return true;
    }
    char *text = script_string(arena, input.data, input.size, e);
    if (text == NULL)
        return false;
    *out = (qa_bytes){(const uint8_t *)text, input.size};
    return true;
}
static bool copy_location(qa_arena *arena, qa_script_location source, qa_script_location *out,
                          qa_error *e) {
    *out = source;
    if (source.path == NULL)
        return true;
    out->path = script_string(arena, source.path, strlen(source.path), e);
    return out->path != NULL;
}
static bool copy_token(qa_arena *arena, const qa_script_token *source, qa_script_token *out,
                       qa_error *e) {
    *out = *source;
    return copy_bytes(arena, source->text, &out->text, e) &&
           copy_bytes(arena, source->leading_whitespace, &out->leading_whitespace, e) &&
           copy_location(arena, source->location, &out->location, e);
}
static bool copy_options(qa_arena *arena, const qa_script_options *source, qa_script_options *out,
                         qa_error *e) {
    *out = *source;
    out->globals = NULL;
    const char *path = source->include_path == NULL ? "" : source->include_path;
    out->include_path = script_string(arena, path, strlen(path), e);
    return out->include_path != NULL;
}
static size_t find_macro(const script_macro *const *macros, size_t count,
                         const script_macro *macro) {
    for (size_t i = 0; i < count; ++i)
        if (macros[i] == macro)
            return i;
    return SIZE_MAX;
}
static size_t find_expansion(const script_expansion *const *expansions, size_t count,
                             const script_expansion *expansion) {
    for (size_t i = 0; i < count; ++i)
        if (expansions[i] == expansion)
            return i;
    return SIZE_MAX;
}
bool qa_script_capture(const qa_script *s, qa_script_checkpoint *out, qa_error *e) {
    if (s == NULL || out == NULL || s->read_count != 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid or active script checkpoint source/output");
        return false;
    }
    script_checkpoint_storage *storage = calloc(1, sizeof(*storage));
    if (storage == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating script checkpoint");
        return false;
    }
    qa_script_checkpoint result = {.version = SCRIPT_CHECKPOINT_VERSION,
                                   .storage = storage,
                                   .frame_count = s->frame_count,
                                   .stack_count = s->stack_count,
                                   .queue_count = s->queue_count,
                                   .condition_count = s->condition_count,
                                   .expansions = s->expansions,
                                   .outputs = s->outputs,
                                   .empty_expansion = s->empty_expansion,
                                   .source_failure = s->source_failure};
    qa_arena *arena = &storage->arena;
    const script_macro **macros = NULL;
    size_t mc = 0, mcap = 0;
    const script_expansion **expansions = NULL, **chain = NULL;
    size_t ec = 0, ecap = 0, chaincap = 0;
    for (size_t bucket = 0; bucket < 1024; ++bucket)
        for (const script_macro *m = s->macros.buckets[bucket]; m != NULL; m = m->next) {
            if (!script_grow((void **)&macros, &mcap, mc + 1, sizeof(*macros), e))
                goto fail;
            macros[mc++] = m;
        }
    for (size_t i = 0; i < s->queue_count; ++i) {
        size_t count = 0;
        for (const script_expansion *p = s->queue[i].expansion;
             p != NULL && find_expansion(expansions, ec, p) == SIZE_MAX; p = p->parent) {
            if (!script_grow((void **)&chain, &chaincap, count + 1, sizeof(*chain), e))
                goto fail;
            chain[count++] = p;
        }
        while (count != 0) {
            const script_expansion *p = chain[--count];
            if (!script_grow((void **)&expansions, &ecap, ec + 1, sizeof(*expansions), e))
                goto fail;
            expansions[ec++] = p;
            if (find_macro(macros, mc, p->macro) == SIZE_MAX) {
                if (!script_grow((void **)&macros, &mcap, mc + 1, sizeof(*macros), e))
                    goto fail;
                macros[mc++] = p->macro;
            }
        }
    }
    result.macro_count = mc;
    result.expansion_count = ec;
    qa_script_macro_state *ms = array(arena, mc, sizeof(*ms), _Alignof(qa_script_macro_state), e);
    qa_script_expansion_state *xs =
        array(arena, ec, sizeof(*xs), _Alignof(qa_script_expansion_state), e);
    qa_script_frame_state *fs =
        array(arena, s->frame_count, sizeof(*fs), _Alignof(qa_script_frame_state), e);
    qa_script_queued_state *qs =
        array(arena, s->queue_count, sizeof(*qs), _Alignof(qa_script_queued_state), e);
    qa_script_condition_state *cs =
        array(arena, s->condition_count, sizeof(*cs), _Alignof(qa_script_condition_state), e);
    size_t *stack = array(arena, s->stack_count, sizeof(*stack), _Alignof(size_t), e);
    if ((mc != 0 && ms == NULL) || (ec != 0 && xs == NULL) || (s->frame_count != 0 && fs == NULL) ||
        (s->queue_count != 0 && qs == NULL) || (s->condition_count != 0 && cs == NULL) ||
        (s->stack_count != 0 && stack == NULL))
        goto fail;
    result.macros = ms;
    result.expansion_states = xs;
    result.frames = fs;
    result.queue = qs;
    result.conditions = cs;
    result.stack = stack;
    for (size_t i = 0; i < mc; ++i) {
        const script_macro *m = macros[i];
        ms[i] = (qa_script_macro_state){.parameter_count = m->parameter_count,
                                        .token_count = m->token_count,
                                        .builtin = m->builtin,
                                        .function = m->function,
                                        .fixed = m->fixed,
                                        .active = script_macro_find(&s->macros, m->name) == m};
        if (!copy_bytes(arena, m->name, &ms[i].name, e))
            goto fail;
        qa_bytes *parameters =
            array(arena, m->parameter_count, sizeof(*parameters), _Alignof(qa_bytes), e);
        qa_script_token *tokens =
            array(arena, m->token_count, sizeof(*tokens), _Alignof(qa_script_token), e);
        if ((m->parameter_count != 0 && parameters == NULL) ||
            (m->token_count != 0 && tokens == NULL))
            goto fail;
        ms[i].parameters = parameters;
        ms[i].tokens = tokens;
        for (size_t j = 0; j < m->parameter_count; ++j)
            if (!copy_bytes(arena, m->parameters[j], parameters + j, e))
                goto fail;
        for (size_t j = 0; j < m->token_count; ++j)
            if (!copy_token(arena, m->tokens + j, tokens + j, e))
                goto fail;
    }
    for (size_t i = 0; i < ec; ++i)
        xs[i] = (qa_script_expansion_state){find_macro(macros, mc, expansions[i]->macro),
                                            find_expansion(expansions, ec, expansions[i]->parent)};
    for (size_t i = 0; i < s->frame_count; ++i) {
        const script_frame *f = s->frames + i;
        fs[i] = (qa_script_frame_state){.condition_base = f->condition_base,
                                        .token_count = f->token_count,
                                        .active = f->active};
        fs[i].path = script_string(arena, f->resource.path, strlen(f->resource.path), e);
        if (fs[i].path == NULL || !copy_bytes(arena, f->resource.bytes, &fs[i].source, e) ||
            !qa_script_lexer_capture(f->lexer, &fs[i].lexer, e))
            goto fail;
        if (fs[i].lexer.unread && !copy_token(arena, &f->lexer->state.token, &fs[i].lexer.token, e))
            goto fail;
        if (!fs[i].lexer.unread)
            fs[i].lexer.token = (qa_script_token){0};
    }
    if (s->stack_count != 0)
        memcpy(stack, s->stack, s->stack_count * sizeof(*stack));
    for (size_t i = 0; i < s->queue_count; ++i) {
        qs[i].expansion = find_expansion(expansions, ec, s->queue[i].expansion);
        if (!copy_token(arena, &s->queue[i].token, &qs[i].token, e))
            goto fail;
    }
    for (size_t i = 0; i < s->condition_count; ++i)
        cs[i] = (qa_script_condition_state){s->conditions[i].frame, s->conditions[i].skip,
                                            s->conditions[i].was_else};
    if (!copy_options(arena, &s->options, &result.options, e) ||
        !copy_location(arena, s->last_location, &result.last_location, e) ||
        !copy_token(arena, &s->raw_token, &result.raw_token, e))
        goto fail;
    if (s->services.date != NULL) {
        result.date = script_string(arena, s->services.date, strlen(s->services.date), e);
        if (result.date == NULL)
            goto fail;
    }
    if (s->services.time != NULL) {
        result.time = script_string(arena, s->services.time, strlen(s->services.time), e);
        if (result.time == NULL)
            goto fail;
    }
    free(macros);
    free(expansions);
    free(chain);
    *out = result;
    return true;
fail:
    free(macros);
    free(expansions);
    free(chain);
    qa_script_checkpoint_free(&result);
    return false;
}
void qa_script_checkpoint_free(qa_script_checkpoint *checkpoint) {
    if (checkpoint == NULL)
        return;
    script_checkpoint_storage *storage = checkpoint->storage;
    if (storage != NULL) {
        qa_arena_destroy(&storage->arena);
        free(storage);
    }
    *checkpoint = (qa_script_checkpoint){0};
}
static bool name_valid(qa_bytes name, size_t limit) {
    if (name.size == 0 || name.size >= limit || name.data == NULL || !script_alpha(name.data[0]))
        return false;
    for (size_t i = 1; i < name.size; ++i)
        if (!script_name(name.data[i]))
            return false;
    return true;
}
static bool token_valid(const qa_script_token *t, size_t limit) {
    return t->kind >= QA_SCRIPT_PRIMITIVE && t->kind <= QA_SCRIPT_PUNCTUATION &&
           t->text.size < limit && (t->text.size == 0 || t->text.data != NULL) &&
           (t->leading_whitespace.size == 0 || t->leading_whitespace.data != NULL);
}
static bool raw_token_valid(const qa_script_token *t, size_t limit) {
    return t->kind >= QA_SCRIPT_PRIMITIVE && t->kind <= QA_SCRIPT_PUNCTUATION &&
           t->text.size <= limit && (t->text.size == 0 || t->text.data != NULL) &&
           (t->leading_whitespace.size == 0 || t->leading_whitespace.data != NULL);
}
bool script_checkpoint_valid(const qa_script_checkpoint *c, qa_error *e) {
    if (c == NULL || c->version != SCRIPT_CHECKPOINT_VERSION || c->options.globals != NULL ||
        c->options.token_limit < 4 || c->options.token_limit > UINT32_MAX ||
        c->options.maximum_include_depth == 0 || c->options.maximum_expansions == 0 ||
        c->options.maximum_queued_tokens == 0 || c->options.maximum_output_tokens == 0 ||
        c->options.maximum_defines == 0 || c->options.maximum_expression_tokens == 0 ||
        c->options.maximum_source_tokens == 0 ||
        c->stack_count > c->options.maximum_include_depth ||
        c->queue_count > c->options.maximum_queued_tokens ||
        c->expansions > c->options.maximum_expansions ||
        c->outputs > c->options.maximum_output_tokens ||
        (c->macro_count != 0 && c->macros == NULL) || (c->frame_count != 0 && c->frames == NULL) ||
        (c->stack_count != 0 && c->stack == NULL) ||
        (c->expansion_count != 0 && c->expansion_states == NULL) ||
        (c->queue_count != 0 && c->queue == NULL) ||
        (c->condition_count != 0 && c->conditions == NULL) ||
        !raw_token_valid(&c->raw_token, c->options.token_limit))
        goto bad;
    size_t active = 0;
    for (size_t i = 0; i < c->macro_count; ++i) {
        const qa_script_macro_state *m = c->macros + i;
        if (!name_valid(m->name, c->options.token_limit) || m->parameter_count > 128 ||
            (m->parameter_count != 0 && (!m->function || m->parameters == NULL)) ||
            (m->token_count != 0 && m->tokens == NULL) ||
            m->token_count > SIZE_MAX / sizeof(qa_script_token) || m->builtin > 4)
            goto bad;
        for (size_t j = 0; j < m->parameter_count; ++j) {
            if (!name_valid(m->parameters[j], c->options.token_limit))
                goto bad;
            for (size_t k = 0; k < j; ++k)
                if (script_bytes_equal(m->parameters[j], m->parameters[k]))
                    goto bad;
        }
        for (size_t j = 0; j < m->token_count; ++j)
            if (!token_valid(m->tokens + j, c->options.token_limit))
                goto bad;
        if (m->active && ++active > c->options.maximum_defines)
            goto bad;
    }
    active = 0;
    for (size_t i = 0; i < c->frame_count; ++i) {
        const qa_script_frame_state *f = c->frames + i;
        if (f->path == NULL || f->path[0] == 0 || (f->source.size != 0 && f->source.data == NULL) ||
            f->lexer.offset > f->source.size || f->lexer.line == 0 || f->lexer.column == 0 ||
            (f->lexer.unread && !token_valid(&f->lexer.token, c->options.token_limit)) ||
            f->token_count > c->options.maximum_source_tokens ||
            (f->active && f->condition_base > c->condition_count))
            goto bad;
        if (f->active)
            ++active;
    }
    if (active != c->stack_count)
        goto bad;
    for (size_t i = 0; i < c->stack_count; ++i) {
        if (c->stack[i] >= c->frame_count || !c->frames[c->stack[i]].active)
            goto bad;
        for (size_t j = 0; j < i; ++j)
            if (c->stack[i] == c->stack[j])
                goto bad;
    }
    for (size_t i = 0; i < c->expansion_count; ++i) {
        const qa_script_expansion_state *x = c->expansion_states + i;
        if (x->macro >= c->macro_count || (x->parent != SIZE_MAX && x->parent >= i))
            goto bad;
    }
    for (size_t i = 0; i < c->queue_count; ++i) {
        if (!token_valid(&c->queue[i].token, c->options.token_limit) ||
            (c->queue[i].expansion != SIZE_MAX && c->queue[i].expansion >= c->expansion_count))
            goto bad;
    }
    /* A recognized lexer failure can discard an included frame before EOF,
     * leaving its conditions in source order until later directive handling. */
    for (size_t i = 0; i < c->condition_count; ++i) {
        size_t frame = c->conditions[i].frame;
        if (frame >= c->frame_count || c->frames[frame].condition_base > i)
            goto bad;
    }
    return true;
bad:
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid script checkpoint");
    return false;
}
bool qa_script_restore(const qa_script_services *services, const qa_script_checkpoint *c,
                       qa_script **out, qa_error *e) {
    if (services == NULL || services->read == NULL || services->release == NULL || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid restored script services/output");
        return false;
    }
    if (!script_checkpoint_valid(c, e))
        return false;
    qa_script *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating restored script");
        return false;
    }
    s->services = *services;
    s->expansions = c->expansions;
    s->outputs = c->outputs;
    s->empty_expansion = c->empty_expansion;
    s->source_failure = c->source_failure;
    script_macro **macros = NULL;
    script_expansion *expansions = NULL;
    if (!copy_options(&s->arena, &c->options, &s->options, e) ||
        !copy_location(&s->arena, c->last_location, &s->last_location, e) ||
        !copy_token(&s->arena, &c->raw_token, &s->raw_token, e))
        goto fail;
    if (c->date != NULL) {
        s->services.date = script_string(&s->arena, c->date, strlen(c->date), e);
        if (s->services.date == NULL)
            goto fail;
    } else
        s->services.date = NULL;
    if (c->time != NULL) {
        s->services.time = script_string(&s->arena, c->time, strlen(c->time), e);
        if (s->services.time == NULL)
            goto fail;
    } else
        s->services.time = NULL;
    macros = array(&s->arena, c->macro_count, sizeof(*macros), _Alignof(script_macro *), e);
    expansions =
        array(&s->arena, c->expansion_count, sizeof(*expansions), _Alignof(script_expansion), e);
    if ((c->macro_count != 0 && macros == NULL) || (c->expansion_count != 0 && expansions == NULL))
        goto fail;
    for (size_t i = 0; i < c->macro_count; ++i) {
        if (c->macros[i].active && script_macro_find(&s->macros, c->macros[i].name) != NULL) {
            qa_error_set(e, QA_ERROR_FORMAT, i, "Duplicate active script macro in checkpoint");
            goto fail;
        }
        if (!script_macro_copy(&s->macros, c->macros + i, macros + i, e))
            goto fail;
    }
    for (size_t i = 0; i < c->expansion_count; ++i)
        expansions[i] = (script_expansion){macros[c->expansion_states[i].macro],
                                           c->expansion_states[i].parent == SIZE_MAX
                                               ? NULL
                                               : expansions + c->expansion_states[i].parent};
    if (!script_grow((void **)&s->frames, &s->frame_capacity, c->frame_count, sizeof(*s->frames),
                     e) ||
        !script_grow((void **)&s->stack, &s->stack_capacity, c->stack_count, sizeof(*s->stack),
                     e) ||
        !script_grow((void **)&s->queue, &s->queue_capacity, c->queue_count, sizeof(*s->queue),
                     e) ||
        !script_grow((void **)&s->conditions, &s->condition_capacity, c->condition_count,
                     sizeof(*s->conditions), e))
        goto fail;
    for (size_t i = 0; i < c->frame_count; ++i) {
        const qa_script_frame_state *saved = c->frames + i;
        qa_bytes bytes;
        if (!copy_bytes(&s->arena, saved->source, &bytes, e))
            goto fail;
        qa_script_lexer_options options = {.flags = s->options.lexer_flags,
                                           .token_limit = s->options.token_limit,
                                           .context = services->context,
                                           .diagnostic = services->diagnostic};
        qa_script_lexer *lexer;
        if (!qa_script_lexer_open(saved->path, bytes, &options, &lexer, e))
            goto fail;
        script_frame frame = {.resource = {qa_script_lexer_position(lexer).path, bytes, NULL},
                              .lexer = lexer,
                              .condition_base = saved->condition_base,
                              .token_count = saved->token_count,
                              .active = saved->active,
                              .owned = true};
        s->frames[s->frame_count++] = frame;
        if (!qa_script_lexer_restore(lexer, &saved->lexer, e))
            goto fail;
    }
    if (c->stack_count != 0)
        memcpy(s->stack, c->stack, c->stack_count * sizeof(*s->stack));
    s->stack_count = c->stack_count;
    for (size_t i = 0; i < c->queue_count; ++i) {
        script_queued_token *q = s->queue + i;
        if (!copy_token(&s->arena, &c->queue[i].token, &q->token, e))
            goto fail;
        q->expansion =
            c->queue[i].expansion == SIZE_MAX ? NULL : expansions + c->queue[i].expansion;
        ++s->queue_count;
    }
    for (size_t i = 0; i < c->condition_count; ++i) {
        s->conditions[s->condition_count++] = (script_condition){
            c->conditions[i].skip, c->conditions[i].was_else, c->conditions[i].frame};
        if (c->conditions[i].skip)
            ++s->skipping;
    }
    *out = s;
    return true;
fail:
    qa_script_close(s);
    return false;
}
