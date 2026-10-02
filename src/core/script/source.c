#include "internal.h"
#include <stdio.h>

bool script_fail(qa_script *s, qa_script_location location, const char *message, qa_error *e) {
    s->source_failure = true;
    qa_error_set(e, QA_ERROR_FORMAT, location.offset, "%s:%u:%u: %s",
                 location.path == NULL ? "<script>" : location.path, location.line, location.column,
                 message);
    if ((s->options.lexer_flags & QA_SCRIPT_NO_ERRORS) == 0 && s->services.diagnostic != NULL) {
        qa_script_diagnostic d = {QA_SCRIPT_ERROR, location, message};
        s->services.diagnostic(s->services.context, &d);
    }
    return false;
}
void script_warn(qa_script *s, qa_script_location location, const char *message) {
    if ((s->options.lexer_flags & QA_SCRIPT_NO_WARNINGS) == 0 && s->services.diagnostic != NULL) {
        qa_script_diagnostic d = {QA_SCRIPT_WARNING, location, message};
        s->services.diagnostic(s->services.context, &d);
    }
}
static bool same_path(const char *a, const char *b) {
    for (;;) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z')
            x = (unsigned char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = (unsigned char)(y - 'A' + 'a');
        if (x != y)
            return false;
        if (x == 0)
            return true;
    }
}
bool script_include(qa_script *s, const qa_script_include *request, qa_error *e) {
    if(s->frame_count>=UINT32_MAX) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"Source script pointer identities exhausted");return false;
    }
    if (s->stack_count >= s->options.maximum_include_depth)
        return script_fail(s, qa_script_position(s),
                           "Script include depth exceeds configured limit", e);
    qa_script_resource resource = {0};
    qa_script_file file={0};
    qa_script_lexer *lexer=NULL;
    bool staged=s->services.file_open!=NULL,found=false;
    qa_script_lexer_options options = {.flags=s->options.lexer_flags,
        .token_limit=s->options.token_limit,.context=s->services.context,
        .diagnostic=s->services.diagnostic,.memory=s->services.memory};
    bool opened=staged?s->services.file_open(s->services.context,request,&file,&found,e):
        s->services.read(s->services.context,request,&resource,&found,e);
    if(!opened) {qa_buffer_free(&file.path);return false;}
    if(!found) {
        qa_buffer_free(&file.path);
        if(request->kind!=QA_SCRIPT_ROOT) {
            char message[256];snprintf(message,sizeof(message),"file %s not found",request->requested_path);
            return script_fail(s,qa_script_position(s),message,e);
        }
        qa_error_set(e,QA_ERROR_NOT_FOUND,0,"Script resource not found: %s",request->requested_path);return false;
    }
    if(staged) {
        if(!file.path.data || !file.path.size || file.path.data[file.path.size-1] ||
           !file.path.data[0] || !memchr(file.path.data,0,file.path.size)) {
            qa_buffer_free(&file.path);
            return script_fail(s,qa_script_position(s),"Script open returned an invalid filename",e);
        }
        bool loaded=script_lexer_open_file(&file,&s->services,&options,&lexer,e);
        qa_buffer_free(&file.path);
        if(!loaded) return false;
        const char *text=script_string(&s->arena,lexer->input.data,lexer->input.size,e);
        if(!text) {script_lexer_dispose(lexer);return false;}
        resource=(qa_script_resource){.path=lexer->path,.bytes={(const uint8_t *)text,lexer->input.size}};
        if(s->services.file_text) script_lexer_compress(lexer);
    } else if(!resource.path || !resource.path[0] || (resource.bytes.size && !resource.bytes.data)) {
        s->services.release(s->services.context,&resource);
        return script_fail(s,qa_script_position(s),"Script resolver returned an invalid resource",e);
    }
    for(size_t i=0;i<s->stack_count;++i)
        if(same_path(s->frames[s->stack[i]].resource.path,resource.path)) {
            script_warn(s,qa_script_position(s),"Recursive script include ignored");
            if(staged) qa_script_lexer_close(lexer);
            else s->services.release(s->services.context,&resource);
            return true;
        }
    if(!script_grow((void **)&s->frames,&s->frame_capacity,s->frame_count+1,sizeof(*s->frames),e) ||
       !script_grow((void **)&s->stack,&s->stack_capacity,s->stack_count+1,sizeof(*s->stack),e)) {
        if(staged) script_lexer_dispose(lexer);
        else s->services.release(s->services.context,&resource);
        return false;
    }
    if(!staged && !qa_script_lexer_open(resource.path,resource.bytes,&options,&lexer,e)) {
        s->services.release(s->services.context,&resource);return false;
    }
    if(!staged && s->services.file_text) script_lexer_compress(lexer);
    qa_store_u32le(lexer->record.bytes+SCRIPT_LEXER_NEXT,script_source_pointer(s));
    s->frames[s->frame_count] = (script_frame){
        .resource = resource, .lexer = lexer, .owned=staged, .condition_base = s->condition_count, .active = true};
    s->stack[s->stack_count++] = s->frame_count++;
    script_source_stack(s);
    return true;
}
qa_script_location qa_script_position(const qa_script *s) {
    if (s == NULL)
        return (qa_script_location){0};
    if (script_source_pointer(s) && script_source_pointer(s)<=s->frame_count)
        return qa_script_lexer_position(s->frames[script_current_frame(s)].lexer);
    return s->last_location;
}
qa_script_location qa_script_source_position(const qa_script *s) {
    if (!s || !s->frame_count) return (qa_script_location){0};
    qa_script_location location=qa_script_position(s);
    location.path=s->source_record.bytes?(char *)s->source_record.bytes:s->frames[0].resource.path;
    if (!script_source_pointer(s)) location.line=0;
    return location;
}
bool script_raw(qa_script *s, script_queued_token *out, bool *found, qa_error *e) {
    if (qa_load_u32le(s->source_record.bytes+SCRIPT_SOURCE_TOKENS)) {
        if (!script_queue_pop(s,out,e)) return false;
        *found = true;
        return true;
    }
    while (script_source_pointer(s) != 0) {
        if(script_current_frame(s)>=s->frame_count || !s->stack_count ||
           script_current_frame(s)!=s->stack[s->stack_count-1]) {
            qa_error_set(e,QA_ERROR_FORMAT,0,"Source script pointer does not name its actual active frame");return false;
        }
        script_frame *frame = s->frames + script_current_frame(s);
        qa_script_location context=out->token.location;
        qa_bytes whitespace=out->token.leading_whitespace;
        const char *unsupported=out->unsupported;
        *out = (script_queued_token){.raw=true,.token.location=context,
            .token.leading_whitespace=whitespace,.unsupported=unsupported};
        qa_script_token parsed;
        bool ok = script_lexer_next_into(frame->lexer, &parsed, found, out->bytes, e);
        out->token=parsed;
        if(*found) out->unsupported=NULL;
        if(!*found) {
            out->token.location.path=context.path;out->token.location.column=context.column;
            out->token.location.offset=context.offset;out->token.leading_whitespace=whitespace;
            out->unsupported=unsupported;
        }
        if (!ok && !frame->lexer->source_failure)
            return false;
        if (*found) {
            if (frame->token_count >= s->options.maximum_source_tokens)
                return script_fail(s, out->token.location, "Source token limit exceeded", e);
            ++frame->token_count;
            s->last_location = out->token.location;
            return true;
        }
        s->last_location = qa_script_lexer_position(frame->lexer);
        while (script_peek(frame->lexer, 0) == 0 && s->condition_count != 0) {
            script_condition condition;
            if (!script_condition_top(s,&condition,e)) return false;
            if (condition.frame != script_current_frame(s)) break;
            script_warn(s, s->last_location, "Missing #endif at end of script");
            if (!script_condition_pop(s,e)) return false;
        }
        uint32_t next=qa_load_u32le(frame->lexer->record.bytes+SCRIPT_LEXER_NEXT);
        uint32_t expected=s->stack_count>1?(uint32_t)s->stack[s->stack_count-2]+1:0;
        if(next!=expected) {
            qa_error_set(e,QA_ERROR_FORMAT,0,"Script next pointer does not name its actual parent frame");return false;
        }
        if (s->stack_count == 1) {
            *found = false;
            s->source_failure = !ok;
            return ok;
        }
        frame->active = false;
        --s->stack_count;
        qa_store_u32le(s->source_record.bytes+SCRIPT_SOURCE_STACK,next);
        if(!script_lexer_retire(frame->lexer,e)) return false;
        s->last_location=qa_script_lexer_position(frame->lexer);
        s->source_failure = false;
        if (e != NULL)
            *e = (qa_error){0};
    }
    *out = (script_queued_token){0};
    *found = false;
    return true;
}
bool script_line_token(qa_script *s, script_queued_token *out, bool *found, qa_error *e) {
    bool continuation = false;
    for (;;) {
        if (!script_raw(s, out, found, e))
            return false;
        if (!*found)
            return true;
        if (out->token.lines_crossed > (continuation ? 1u : 0u)) {
            if (!script_push(s, *out, e))
                return false;
            *found = false;
            return true;
        }
        if (qa_script_token_is(&out->token, "\\")) {
            continuation = true;
            continue;
        }
        return true;
    }
}
bool script_line(qa_script *s, qa_script_token **out, size_t *count, qa_error *e) {
    qa_script_token *tokens = NULL;
    size_t length = 0, capacity = 0;
    for (;;) {
        script_queued_token token=script_local_token();
        bool found;
        if (!script_line_token(s, &token, &found, e)) {
            free(tokens);
            return false;
        }
        if (!found) {
            *out = tokens;
            *count = length;
            return true;
        }
        if (length >= s->options.maximum_queued_tokens) {
            free(tokens);
            return script_fail(s, token.token.location, "Script line exceeds configured limit", e);
        }
        if (!script_grow((void **)&tokens, &capacity, length + 1, sizeof(*tokens), e)) {
            free(tokens);
            return false;
        }
        tokens[length++] = token.token;
    }
}
static bool install_builtins(qa_script *source,qa_error *error) {
    static const char *names[]={"__LINE__","__FILE__","__DATE__","__TIME__"};
    for(size_t i=0;i<sizeof(names)/sizeof(*names);++i) {
        if(source->macros.count>=source->options.maximum_defines)
            return script_fail(source,qa_script_position(source),"Script builtin count exceeds configured define limit",error);
        script_macro *macro;
        if(!script_macro_allocate(&source->macros,script_bytes(names[i]),true,&macro,error)) return false;
        script_macro_word_set(macro,8,(uint32_t)i+1);script_macro_word_set(macro,4,1);
        if(!script_macro_publish(&source->macros,macro,error)) return false;
    }
    return true;
}
bool qa_script_open(const char *path, const qa_script_services *services,
                    const qa_script_options *options, qa_script **out, qa_error *e) {
    if (path == NULL || !qa_script_services_valid(services) ||
        out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid script source services");
        return false;
    }
    qa_script *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating script source");
        return false;
    }
    s->output=script_local_token();
    s->services = *services;
    if (!script_memory_bind(s,e)) goto fail;
    if (options != NULL)
        s->options = *options;
    if (s->options.token_limit == 0)
        s->options.token_limit = 1024;
    if (s->options.maximum_include_depth == 0)
        s->options.maximum_include_depth = SIZE_MAX;
    if (s->options.maximum_expansions == 0)
        s->options.maximum_expansions = SIZE_MAX;
    if (s->options.maximum_queued_tokens == 0)
        s->options.maximum_queued_tokens = SIZE_MAX;
    if (s->options.maximum_output_tokens == 0)
        s->options.maximum_output_tokens = SIZE_MAX;
    if (s->options.maximum_defines == 0)
        s->options.maximum_defines = SIZE_MAX;
    if (s->options.maximum_expression_tokens == 0)
        s->options.maximum_expression_tokens = SIZE_MAX;
    if (s->options.maximum_source_tokens == 0)
        s->options.maximum_source_tokens = SIZE_MAX;
    const char *include_path = s->options.include_path == NULL ? "" : s->options.include_path;
    s->options.include_path = script_string(&s->arena, include_path, strlen(include_path), e);
    if (s->options.include_path == NULL)
        goto fail;
    if (s->services.date != NULL) {
        s->services.date = script_string(&s->arena, services->date, strlen(services->date), e);
        if (s->services.date == NULL)
            goto fail;
    }
    if (s->services.time != NULL) {
        s->services.time = script_string(&s->arena, services->time, strlen(services->time), e);
        if (s->services.time == NULL)
            goto fail;
    }
    qa_script_include request={QA_SCRIPT_ROOT,NULL,path,s->options.include_path};
    if(!script_include(s,&request,e) || !script_source_create(s,e) ||
       !script_table_open(&s->macros,s->memory.context?&s->memory:NULL,false,e)) goto fail;
    qa_store_u32le(s->source_record.bytes+SCRIPT_SOURCE_HASH,1);
    s->macros.source=s;
    if(s->options.globals) {
        s->globals=(qa_script_defines *)s->options.globals;qa_script_defines_retain(s->globals);
        if(!script_globals_import(&s->macros,s->globals,e)) goto fail;
    }
    if(s->macros.count>s->options.maximum_defines) {
        script_fail(s,(qa_script_location){path,1,1,0},"Initial defines exceed configured limit",e);goto fail;
    }
    if(s->options.builtins && !install_builtins(s,e)) goto fail;
    *out = s;
    return true;
fail:
    qa_script_close(s);
    return false;
}
static void close_source(qa_script *s,bool source) {
    if (s == NULL)
        return;
    if (source && s->memory_deferred) (void)script_memory_enter(s,NULL);
    for (size_t remaining=s->frame_count;remaining;--remaining) {
        size_t i=remaining-1;
        bool reached=source && (!s->source_record.bytes || script_source_pointer(s)==i+1);
        if(reached && s->source_record.bytes) {
            uint32_t next=qa_load_u32le(s->frames[i].lexer->record.bytes+SCRIPT_LEXER_NEXT);
            qa_store_u32le(s->source_record.bytes+SCRIPT_SOURCE_STACK,next);
            s->frames[i].active=false;
            if(s->stack_count) --s->stack_count;
        }
        if(reached) qa_script_lexer_close(s->frames[i].lexer);
        else script_lexer_dispose(s->frames[i].lexer);
        if (!s->frames[i].owned)
            s->services.release(s->services.context, &s->frames[i].resource);
    }
    free(s->frames);
    free(s->stack);
    script_queue_close(s,source);
    if(source) (void)script_table_clear(&s->macros,NULL);
    script_conditions_close(s,source);
    script_table_dispose(&s->macros,source);
    script_source_close(s,source);
    if(s->memory.context) s->memory.release(s->memory.context);
    free(s->reads);
    qa_arena_destroy(&s->arena);
    qa_script_defines_release(s->globals);
    free(s);
}
void qa_script_close(qa_script *s) { close_source(s,true); }
void qa_script_dispose(qa_script *s) { close_source(s,false); }
bool qa_script_define(qa_script *s, const char *text, qa_error *e) {
    if (s == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing script source");
        return false;
    }
    return script_memory_enter(s,e) && script_macro_text(&s->macros, text, s->options.maximum_defines, e);
}
bool qa_script_undefine(qa_script *s, const char *name, qa_error *e) {
    if (s == NULL || name == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing script define name/source");
        return false;
    }
    bool fixed;
    if(!script_memory_enter(s,e)) return false;
    qa_error failure={0};
    (void)script_macro_remove(&s->macros,script_bytes(name),&fixed,&failure);
    if(failure.code!=QA_OK) {if(e) *e=failure;return false;}
    if (fixed)
        script_warn(s, qa_script_position(s), "Cannot undefine a fixed script macro");
    return true;
}
bool qa_script_is_defined(const qa_script *s, const char *name) {
    return s != NULL && name != NULL && script_macro_find(&s->macros, script_bytes(name)) != NULL;
}
