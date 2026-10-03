#include "internal.h"
#include "qa/binary.h"

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
static bool copy_profile(qa_arena *arena,const char *input,const char **out,qa_error *e) {
    *out=input?script_string(arena,input,strlen(input),e):NULL;
    return !input || *out;
}
static bool copy_options(qa_arena *arena, const qa_script_options *source, qa_script_options *out,
                         qa_error *e) {
    *out = *source;
    out->globals = NULL;
    const char *path = source->include_path == NULL ? "" : source->include_path;
    out->include_path = script_string(arena, path, strlen(path), e);
    return out->include_path != NULL;
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
    qa_script_checkpoint result = {.storage = storage,
                                   .frame_count = s->frame_count,
                                   .stack_count = s->stack_count,
                                   .queue_count = s->macros.queue_count,
                                   .condition_count = s->condition_count,
                                   .expansions = s->expansions,
                                   .outputs = s->outputs,
                                   .next_condition_pointer = s->next_condition_pointer,
                                   .next_token_pointer = s->macros.next_token_pointer,
                                   .empty_expansion = s->empty_expansion,
                                   .source_failure = s->source_failure,
                                   .file_text = s->services.file_text};
    qa_arena *arena = &storage->arena;
    if(!script_table_capture((script_macro_table *)&s->macros,&result,arena,e)) {qa_script_checkpoint_free(&result);return false;}
    qa_script_queued_state *qs=(qa_script_queued_state *)result.queue;
    const script_expansion **expansions=NULL,**chain=NULL;size_t ec=0,ecap=0,chaincap=0;
    for(size_t i=0;i<s->macros.queue_records;++i) {
        if(!s->macros.queue[i].record.bytes) continue;
        size_t count=0;
        for(const script_expansion *p=s->macros.queue[i].expansion;
            p && find_expansion(expansions,ec,p)==SIZE_MAX;p=p->parent) {
            if(!script_grow((void **)&chain,&chaincap,count+1,sizeof(*chain),e)) goto fail;
            chain[count++]=p;
        }
        while(count) {
            if(!script_grow((void **)&expansions,&ecap,ec+1,sizeof(*expansions),e)) goto fail;
            expansions[ec++]=chain[--count];
        }
    }
    result.expansion_count = ec;
    qa_script_expansion_state *xs =
        array(arena, ec, sizeof(*xs), _Alignof(qa_script_expansion_state), e);
    qa_script_frame_state *fs =
        array(arena, s->frame_count, sizeof(*fs), _Alignof(qa_script_frame_state), e);
    qa_script_condition_state *cs =
        array(arena, s->condition_count, sizeof(*cs), _Alignof(qa_script_condition_state), e);
    size_t *stack = array(arena, s->stack_count, sizeof(*stack), _Alignof(size_t), e);
    if ((ec != 0 && xs == NULL) || (s->frame_count != 0 && fs == NULL) ||
        (s->condition_count != 0 && cs == NULL) ||
        (s->stack_count != 0 && stack == NULL))
        goto fail;
    result.expansion_states = xs;
    result.frames = fs;
    result.queue = qs;
    result.conditions = cs;
    result.stack = stack;
    for(size_t i=0;i<ec;++i)
        xs[i]=(qa_script_expansion_state){expansions[i]->macro,find_expansion(expansions,ec,expansions[i]->parent)};
    for (size_t i = 0; i < s->frame_count; ++i) {
        const script_frame *f = s->frames + i;
        fs[i] = (qa_script_frame_state){.condition_base = f->condition_base,
                                        .token_count = f->token_count,
                                        .active = f->active};
        fs[i].path = script_string(arena, f->resource.path, strlen(f->resource.path), e);
        if (fs[i].path == NULL || !copy_bytes(arena, f->resource.bytes, &fs[i].source, e) ||
            !qa_script_lexer_capture(f->lexer, &fs[i].lexer, e) ||
            !script_lexer_frame_capture(f->lexer,&fs[i],arena,e))
            goto fail;
        if (!copy_token(arena, &fs[i].lexer.token, &fs[i].lexer.token, e))
            goto fail;
    }
    if (s->stack_count != 0)
        memcpy(stack, s->stack, s->stack_count * sizeof(*stack));
    for (size_t i=0,j=0;i<s->macros.queue_records;++i) {
        if(!s->macros.queue[i].record.bytes) continue;
        qs[j].expansion=find_expansion(expansions,ec,s->macros.queue[i].expansion);
        if(!copy_token(arena,&qs[j].token,&qs[j].token,e)) goto fail;
        ++j;
    }
    if (!script_source_capture(s,&result,arena,e) || !script_conditions_capture(s,cs,e)) goto fail;
    if (!copy_options(arena, &s->options, &result.options, e) ||
        !copy_location(arena, s->last_location, &result.last_location, e) ||
        !copy_token(arena, &s->output.token, &result.raw_token, e) ||
        !copy_profile(arena,s->output.unsupported,&result.output_unsupported,e) ||
        !copy_profile(arena,s->source_unsupported,&result.source_unsupported,e))
        goto fail;
    memcpy(result.output_record,s->output.bytes,SCRIPT_TOKEN_BYTES);
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
    free(expansions);
    free(chain);
    *out = result;
    return true;
fail:
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
    if (c == NULL || c->options.globals != NULL ||
        c->options.token_limit < 4 || c->options.token_limit > UINT32_MAX ||
        c->options.maximum_include_depth == 0 || c->options.maximum_expansions == 0 ||
        c->options.maximum_queued_tokens == 0 || c->options.maximum_output_tokens == 0 ||
        c->options.maximum_defines == 0 || c->options.maximum_expression_tokens == 0 ||
        c->options.maximum_source_tokens == 0 ||
        c->stack_count > c->options.maximum_include_depth ||
        c->expansions > c->options.maximum_expansions ||
        c->outputs > c->options.maximum_output_tokens || !c->next_condition_pointer || !c->next_token_pointer || !c->next_define_pointer || c->define_first ||
        c->define_hash.size!=4096 || !c->define_hash.data ||
        c->source_record.size!=SCRIPT_SOURCE_BYTES || !c->source_record.data ||
        qa_load_u32le(c->source_record.data+SCRIPT_SOURCE_HASH)!=1 ||
        !memchr(c->source_record.data,0,1024) || !memchr(c->source_record.data+SCRIPT_SOURCE_INCLUDE,0,1024) ||
        !c->options.include_path || strcmp(c->options.include_path,(const char *)c->source_record.data+SCRIPT_SOURCE_INCLUDE) ||
        (c->macro_count != 0 && c->macros == NULL) || (c->frame_count != 0 && c->frames == NULL) ||
        (c->stack_count != 0 && c->stack == NULL) ||
        (c->expansion_count != 0 && c->expansion_states == NULL) ||
        (c->queue_count != 0 && c->queue == NULL) ||
        (c->condition_count != 0 && c->conditions == NULL) ||
        !raw_token_valid(&c->raw_token, c->options.token_limit))
        goto bad;
    {
        size_t extent=c->raw_token.text.size && memchr(c->raw_token.text.data,0,c->raw_token.text.size)?c->raw_token.text.size:SIZE_MAX;
        qa_script_queued_state output={.token=c->raw_token,.text_extent=extent};
        memcpy(output.bytes,c->output_record,SCRIPT_TOKEN_BYTES);
        if(!script_token_saved_valid(&output,true,e)) return false;
    }
    if(c->source_unsupported &&
       (qa_load_u32le(c->source_record.data+SCRIPT_SOURCE_TOKEN+1024)>QA_SCRIPT_PUNCTUATION ||
        !memchr(c->source_record.data+SCRIPT_SOURCE_TOKEN,0,1024))) goto bad;
    if(!script_table_saved_valid(c,false,e)) return false;
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
            !token_valid(&f->lexer.token, c->options.token_limit) ||
            f->token_count > c->options.maximum_source_tokens ||
            (f->active && f->condition_base > c->condition_count))
            goto bad;
        if (!script_lexer_frame_valid(f,e)) return false;
        if (qa_load_u32le(f->script_record.data+SCRIPT_LEXER_NEXT)>i) goto bad;
        if (f->active)
            ++active;
    }
    if (active != c->stack_count)
        goto bad;
    for (size_t i = 0; i < c->stack_count; ++i) {
        if (c->stack[i] >= c->frame_count || !c->frames[c->stack[i]].active)
            goto bad;
        if (qa_load_u32le(c->frames[c->stack[i]].script_record.data+SCRIPT_LEXER_NEXT)!=
            (i?c->stack[i-1]+1:0)) goto bad;
        for (size_t j = 0; j < i; ++j)
            if (c->stack[i] == c->stack[j])
                goto bad;
    }
    for (size_t i = 0; i < c->expansion_count; ++i) {
        const qa_script_expansion_state *x = c->expansion_states + i;
        if (!x->macro || x->macro>=c->next_define_pointer || (x->parent != SIZE_MAX && x->parent >= i))
            goto bad;
    }
    for (size_t i = 0; i < c->queue_count; ++i) {
        const qa_script_queued_state *queued=c->queue+i;
        if(!queued->pointer || queued->pointer>=c->next_token_pointer ||
           (queued->text_extent!=SIZE_MAX && queued->text_extent>=1024) ||
           !memchr(queued->bytes,0,1024) || qa_load_u32le(queued->bytes+1024)>QA_SCRIPT_PUNCTUATION ||
           !token_valid(&c->queue[i].token, c->options.token_limit) ||
           !script_token_saved_valid(queued,false,e) ||
            (c->queue[i].expansion != SIZE_MAX && c->queue[i].expansion >= c->expansion_count))
            goto bad;
        uint32_t next=qa_load_u32le(queued->bytes+1064);bool resolved=!next;
        for(size_t j=0;j<c->queue_count;++j) {
            if(j!=i && (c->queue[j].pointer==queued->pointer ||
                (queued->memory_reference!=SIZE_MAX && c->queue[j].memory_reference==queued->memory_reference))) goto bad;
            if(c->queue[j].pointer==next) resolved=true;
        }
        if(!resolved) goto bad;
    }
    uint32_t token_pointer=qa_load_u32le(c->source_record.data+SCRIPT_SOURCE_TOKENS);
    size_t token_walk=0;
    while(token_pointer) {
        if(token_walk++>=c->queue_count || token_walk>c->options.maximum_queued_tokens) goto bad;
        const qa_script_queued_state *cell=NULL;
        for(size_t i=0;i<c->queue_count;++i) if(c->queue[i].pointer==token_pointer) cell=c->queue+i;
        if(!cell) goto bad;
        token_pointer=qa_load_u32le(cell->bytes+1064);
    }
    /* A recognized lexer failure can discard an included frame before EOF,
     * leaving its conditions in source order until later directive handling. */
    for (size_t i = 0; i < c->condition_count; ++i) {
        const qa_script_condition_state *condition=c->conditions+i;
        size_t frame=condition->frame;
        uint32_t type=qa_load_u32le(condition->bytes);
        if (frame >= c->frame_count || frame>=UINT32_MAX || c->frames[frame].condition_base > i ||
            !condition->pointer || condition->pointer>=c->next_condition_pointer ||
            (type!=1 && type!=2 && type!=4 && type!=8 && type!=16) ||
            qa_load_u32le(condition->bytes+4)!=(uint32_t)condition->skip ||
            (type==2)!=condition->was_else || qa_load_u32le(condition->bytes+8)!=frame+1 ||
            qa_load_u32le(condition->bytes+12)!=(i?c->conditions[i-1].pointer:0)) goto bad;
        for(size_t j=0;j<i;++j) if(c->conditions[j].pointer==condition->pointer) goto bad;
    }
    uint32_t skipping=0;
    for(size_t i=0;i<c->condition_count;++i) skipping+=(uint32_t)c->conditions[i].skip;
    if(qa_load_u32le(c->source_record.data+SCRIPT_SOURCE_SKIP)!=skipping ||
       qa_load_u32le(c->source_record.data+SCRIPT_SOURCE_INDENT)!=(c->condition_count?c->conditions[c->condition_count-1].pointer:0) ||
       qa_load_u32le(c->source_record.data+SCRIPT_SOURCE_STACK)!=(c->stack_count?c->stack[c->stack_count-1]+1:0)) goto bad;
    return true;
bad:
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid script checkpoint");
    return false;
}
static bool restore_source(const qa_script_services *services, const qa_script_checkpoint *c,
                           qa_script **out, bool detached, qa_error *e) {
    if (!qa_script_services_valid(services) || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid restored script services/output");
        return false;
    }
    if (!script_checkpoint_valid(c, e))
        return false;
    if (!services->memory && c->source_reference!=SIZE_MAX) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Restored source requires its actual MEMORY owner");return false;
    }
    if (!services->memory) for(size_t i=0;i<c->condition_count;++i)
        if(c->conditions[i].memory_reference!=SIZE_MAX) {
            qa_error_set(e,QA_ERROR_ARGUMENT,i,"Restored indent requires its actual script MEMORY owner");
            return false;
        }
    if(!services->memory) for(size_t i=0;i<c->queue_count;++i)
        if(c->queue[i].memory_reference!=SIZE_MAX) {
            qa_error_set(e,QA_ERROR_ARGUMENT,i,"Restored token requires its actual script MEMORY owner");return false;
        }
    qa_script *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating restored script");
        return false;
    }
    s->services = *services;
    s->services.file_text = c->file_text;
    if (!script_memory_bind(s,e)) {qa_script_dispose(s);return false;}
    s->memory_deferred=detached;
    s->expansions = c->expansions;
    s->outputs = c->outputs;
    s->empty_expansion = c->empty_expansion;
    s->source_failure = c->source_failure;
    script_expansion *expansions = NULL;
    if (!copy_options(&s->arena, &c->options, &s->options, e) ||
        !copy_location(&s->arena, c->last_location, &s->last_location, e) ||
        !copy_token(&s->arena, &c->raw_token, &s->output.token, e) ||
        !copy_profile(&s->arena,c->output_unsupported,&s->output.unsupported,e) ||
        !copy_profile(&s->arena,c->source_unsupported,&s->source_unsupported,e))
        goto fail;
    memcpy(s->output.bytes,c->output_record,SCRIPT_TOKEN_BYTES);s->output.raw=true;
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
    s->macros.memory=s->memory;s->macros.deferred=detached;s->macros.source=s;
    expansions=array(&s->arena,c->expansion_count,sizeof(*expansions),_Alignof(script_expansion),e);
    if(c->expansion_count && !expansions) goto fail;
    for(size_t i=0;i<c->expansion_count;++i)
        expansions[i]=(script_expansion){(uint32_t)c->expansion_states[i].macro,
            c->expansion_states[i].parent==SIZE_MAX?NULL:expansions+c->expansion_states[i].parent};
    if (!script_grow((void **)&s->frames, &s->frame_capacity, c->frame_count, sizeof(*s->frames),
                     e) ||
        !script_grow((void **)&s->stack, &s->stack_capacity, c->stack_count, sizeof(*s->stack),
                     e))
        goto fail;
    for (size_t i = 0; i < c->frame_count; ++i) {
        const qa_script_frame_state *saved = c->frames + i;
        qa_bytes bytes;
        if (!copy_bytes(&s->arena, saved->source, &bytes, e))
            goto fail;
        qa_script_lexer_options options = {.flags = s->options.lexer_flags,
                                           .token_limit = s->options.token_limit,
                                           .context = services->context,
                                           .diagnostic = services->diagnostic,
                                           .memory=s->services.memory};
        qa_script_lexer *lexer;
        if (!script_lexer_frame_restore(&options,saved,detached,&lexer,e))
            goto fail;
        script_frame frame = {.resource = {qa_script_lexer_position(lexer).path, bytes, NULL},
                              .lexer = lexer,
                              .condition_base = saved->condition_base,
                              .token_count = saved->token_count,
                              .active = saved->active,
                              .owned = true};
        s->frames[s->frame_count++] = frame;

    }
    if (c->stack_count != 0)
        memcpy(s->stack, c->stack, c->stack_count * sizeof(*s->stack));
    s->stack_count = c->stack_count;
    if (!script_source_restore(s,c,e) || !script_conditions_restore(s,c,e) ||
        !script_queue_restore(&s->macros,c,expansions,e) || !script_table_restore(&s->macros,c,e)) goto fail;
    *out = s;
    return true;
fail:
    qa_script_dispose(s);
    return false;
}

bool qa_script_restore(const qa_script_services *services,const qa_script_checkpoint *c,
                       qa_script **out,qa_error *error) {
    return restore_source(services,c,out,false,error);
}
bool qa_script_restore_detached(const qa_script_services *services,const qa_script_checkpoint *c,
                                qa_script **out,qa_error *error) {
    return restore_source(services,c,out,true,error);
}
