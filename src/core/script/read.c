#include "internal.h"
#include <stdio.h>

static size_t string_size(qa_bytes text) {
    const uint8_t *zero = text.size ? memchr(text.data, 0, text.size) : NULL;
    return zero ? (size_t)(zero - text.data) : text.size;
}
static bool concatenate(qa_script *s, script_queued_token *output, const qa_script_token *next,
                        qa_error *e) {
    qa_script_token *first=&output->token;
    size_t left = string_size(first->text), right = string_size(next->text);
    if (!left) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, first->location.offset,
                     "Cannot concatenate an empty source string token");
        return false;
    }
    first->text.size = --left;
    if(output->raw) output->bytes[left]=0;
    if (right)
        --right;
    if (right > SIZE_MAX - left || left + right >= s->options.token_limit - 1)
        return script_fail(s, first->location, "Adjacent strings exceed token limit", e);
    size_t size = left + right;
    char *text = qa_arena_alloc(&s->arena, size + 1, 1, e);
    if (!text)
        return false;
    if (left)
        memcpy(text, first->text.data, left);
    if (right)
        memcpy(text + left, next->text.data + 1, right);
    text[size] = 0;
    first->text = (qa_bytes){(const uint8_t *)text, size};
    return true;
}

/* Source string lookahead is recursive and evaluates directives before the
 * outer token's skip/expansion decision. A retained stack keeps that ordering
 * without growing the native call stack for long adjacent-string runs. */
static bool read_token(qa_script *s, bool *found, qa_error *e) {
    if (!script_grow((void **)&s->reads, &s->read_capacity, 1, sizeof(*s->reads), e))
        return false;
    s->read_count = 1;
    s->reads[0] = script_local_token();
    bool ok;
    script_queued_token *frame;
read_next:
    frame = s->reads + s->read_count - 1;
    ok = script_raw(s, frame, found, e);
    if (!ok || !*found)
        goto completed;
    qa_script_token *token = &frame->token;
    if (token->kind == QA_SCRIPT_PUNCTUATION &&
        (token->subtype == QA_SCRIPT_HASH || token->subtype == QA_SCRIPT_DOLLAR)) {
        ok = script_directive(s, *frame, e);
        if (!ok)
            goto completed;
        goto read_next;
    }
    if (token->kind == QA_SCRIPT_STRING) {
        ok = script_grow((void **)&s->reads, &s->read_capacity, s->read_count + 1,
                         sizeof(*s->reads), e);
        if (!ok)
            goto completed;
        s->reads[s->read_count++] = script_local_token();
        goto read_next;
    }
accept_token:
    frame = s->reads + s->read_count - 1;
    token = &frame->token;
    if (script_skipping(s))
        goto read_next;
    script_macro *macro=NULL;
    if(token->kind==QA_SCRIPT_NAME && !script_macro_lookup(&s->macros,token->text,&macro,e)) {ok=false;goto completed;}
    if (macro) {
        ok = script_expand(s, *frame, macro, e);
        if (!ok)
            goto completed;
        if (s->empty_expansion) {
            *found = false;
            goto completed;
        }
        goto read_next;
    }
    if (!frame->raw && !script_token_store(frame->bytes,token,0,0,e)) {
        ok=false; goto completed;
    }
    frame->raw=true;
    memcpy(s->source_record.bytes+SCRIPT_SOURCE_TOKEN,frame->bytes,SCRIPT_TOKEN_BYTES);
    s->source_unsupported=frame->unsupported;
    *found = true;
    ok = true;
completed:
    if (s->read_count == 1)
        return ok;
    if (!ok && !s->source_failure)
        return false;
    script_queued_token next = s->reads[--s->read_count];
    if (!ok) {
        s->source_failure = false;
        if (e)
            *e = (qa_error){0};
        *found = false;
    }
    ok = true;
    if (*found) {
        frame = s->reads + s->read_count - 1;
        if (next.token.kind == QA_SCRIPT_STRING) {
            ok=concatenate(s,frame,&next.token,e);
            if (ok && frame->raw) {
                memcpy(frame->bytes,frame->token.text.data,frame->token.text.size);
                frame->bytes[frame->token.text.size]=0;
            }
        } else ok=script_push(s,next,e);
        if (!ok)
            goto completed;
    }
    goto accept_token;
}
bool script_read_nested(qa_script *source,script_queued_token *out,bool *found,qa_error *error)
{
    script_queued_token *saved=source->reads;size_t capacity=source->read_capacity,count=source->read_count;
    source->reads=NULL;source->read_capacity=source->read_count=0;
    bool ok=read_token(source,found,error);
    *out=source->read_count?source->reads[0]:(script_queued_token){0};
    free(source->reads);source->reads=saved;source->read_capacity=capacity;source->read_count=count;return ok;
}
bool qa_script_next(qa_script *s, qa_script_token *out, bool *found, qa_error *e) {
    if (s == NULL || out == NULL || found == NULL || s->read_count != 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid or reentrant script read output");
        return false;
    }
    if (!script_memory_enter(s,e)) return false;
    s->output = (script_queued_token){0};
    s->source_failure = false;
    *found = false;
    bool ok = read_token(s, found, e);
    if (s->read_count)
        s->output = s->reads[0];
    if (ok && *found) {
        if (s->outputs >= s->options.maximum_output_tokens)
            ok = script_fail(s, s->output.token.location, "Script output exceeds token limit", e);
        else
            ++s->outputs;
    }
    if(ok && *found && s->output.unsupported) {
        char message[256];
        snprintf(message,sizeof(message),"source token profile is unsupported: %s",s->output.unsupported);
        ok=script_fail(s,s->output.token.location,message,e);
    }
    s->read_count = 0;
    if(!s->output.unsupported) *out = s->output.token;
    if (!ok)
        *found = false;
    return ok;
}
bool qa_script_raw_token(const qa_script *s, qa_script_token *out, qa_error *e) {
    if (!s || !out) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Missing raw source token/output");return false;
    }
    const script_queued_token *token=s->read_count?s->reads:&s->output;
    if(token->raw && !memchr(token->bytes,0,1024)) {
        qa_error_set(e,QA_ERROR_UNSUPPORTED,token->token.location.offset,
            "token_t string lacks a terminator within its 1024-byte field");return false;
    }
    if(token->unsupported) {
        qa_error_set(e,QA_ERROR_UNSUPPORTED,token->token.location.offset,
            "source token profile is unsupported: %s",token->unsupported);return false;
    }
    if(!token->raw) {*out=token->token;return true;}
    size_t extent=token->token.text.size && memchr(token->token.text.data,0,token->token.text.size)?token->token.text.size:SIZE_MAX;
    return script_token_output(token->bytes,extent,token->token.location,token->token.leading_whitespace,
        &((qa_script *)s)->arena,out,e);
}
bool qa_script_source_failure(const qa_script *s) { return s && s->source_failure; }
bool qa_script_unread(qa_script *s, const qa_script_token *token, qa_error *e) {
    if (s == NULL || token == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing unread script token/source");
        return false;
    }
    return script_push(s, (script_queued_token){.token=*token}, e);
}
bool qa_script_expect(qa_script *s, const char *text, qa_error *e) {
    if (text == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing expected script token");
        return false;
    }
    qa_script_token token;
    bool found;
    if (!qa_script_next(s, &token, &found, e))
        return false;
    if (!found || !qa_script_token_is(&token, text)) {
        qa_error_set(e, QA_ERROR_FORMAT, qa_script_position(s).offset, "Expected script token %s",
                     text);
        return false;
    }
    return true;
}
bool qa_script_check(qa_script *s, const char *text, bool *matched, qa_error *e) {
    if (matched == NULL || text == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing script check input/output");
        return false;
    }
    qa_script_token token;
    bool found;
    if (!qa_script_next(s, &token, &found, e))
        return false;
    *matched = found && qa_script_token_is(&token, text);
    if(!found || *matched) return true;
    script_queued_token copied={.token=token,.raw=true,.unsupported=s->source_unsupported};
    memcpy(copied.bytes,s->source_record.bytes+SCRIPT_SOURCE_TOKEN,SCRIPT_TOKEN_BYTES);
    return script_push(s,copied,e);
}
bool qa_script_skip_until(qa_script *s, const char *text, bool *found, qa_error *e) {
    if (text == NULL || found == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing script skip input/output");
        return false;
    }
    qa_script_token token;
    while (qa_script_next(s, &token, found, e)) {
        if (!*found || qa_script_token_is(&token, text))
            return true;
    }
    return false;
}
bool qa_script_read_line(qa_script *s, qa_script_token *out, bool *found, qa_error *e) {
    if (s == NULL || out == NULL || found == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid script line output");
        return false;
    }
    script_queued_token token = script_local_token();
    bool ok = script_line_token(s, &token, found, e);
    *out = token.token;
    return ok;
}
