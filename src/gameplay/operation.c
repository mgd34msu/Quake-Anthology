/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qa/operation.h"

#include <stdlib.h>
#include <string.h>

typedef struct operation_entry {
    struct operation_entry *next;
    qa_operation_hook hook;
    uint64_t sequence;
    bool active;
} operation_entry;

typedef struct operation_frame {
    struct operation_frame *previous;
    void *request;
    qa_operation_canonical_fn canonical;
    void *context;
    uint64_t invocation;
    bool continuation_open, called, failed;
    qa_error failure;
} operation_frame;

struct qa_operation {
    operation_entry *entries;
    operation_frame *current, *spare;
    size_t request_size, result_size, active_count;
    uint64_t next_sequence, next_invocation;
    size_t depth;
};

static bool argument(qa_error *error, const char *message) {
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message); return false;
}

bool qa_operation_create(size_t request_size, size_t result_size, qa_operation **out, qa_error *error) {
    if (!out || !request_size) return argument(error,"operation requires a request layout and output");
    qa_operation *operation=calloc(1,sizeof(*operation));
    if (!operation) { qa_error_set(error,QA_ERROR_MEMORY,0,"allocating gameplay operation"); return false; }
    operation->request_size=request_size; operation->result_size=result_size;
    operation->next_sequence=1; operation->next_invocation=1;
    *out=operation; return true;
}

static void sweep(qa_operation *operation) {
    if (operation->depth) return;
    operation_entry **link=&operation->entries;
    while (*link) {
        operation_entry *entry=*link;
        if (!entry->active) { *link=entry->next; free(entry); }
        else link=&entry->next;
    }
}

bool qa_operation_destroy(qa_operation *operation, qa_error *error) {
    if (!operation) return true;
    if (operation->depth) return argument(error,"cannot destroy an operation during a callback");
    qa_operation_clear(operation);
    while (operation->spare) {
        operation_frame *frame=operation->spare;
        operation->spare=frame->previous;
        free(frame->request); free(frame);
    }
    free(operation); return true;
}

bool qa_operation_register(qa_operation *operation, const qa_operation_hook *hook,
                           qa_operation_registration *out, qa_error *error) {
    if (!operation || !hook || !out) return argument(error,"invalid operation registration");
    switch (hook->kind) {
    case QA_OPERATION_TRANSFORM: if (!hook->call.transform) return argument(error,"missing transform callback"); break;
    case QA_OPERATION_OBSERVE: if (!hook->call.observe) return argument(error,"missing observer callback"); break;
    case QA_OPERATION_REPLACE: if (!hook->call.replace) return argument(error,"missing replacement callback"); break;
    default: return argument(error,"invalid operation hook kind");
    }
    for (operation_entry *entry=operation->entries;entry;entry=entry->next) {
        if (!entry->active) continue;
        if (entry->hook.owner==hook->owner && entry->hook.name==hook->name)
            return argument(error,"duplicate operation registration");
        if (entry->hook.kind==QA_OPERATION_REPLACE && hook->kind==QA_OPERATION_REPLACE)
            return argument(error,"operation already has a replacement");
    }
    if (operation->next_sequence==UINT64_MAX) return argument(error,"operation registration identity exhausted");
    operation_entry *entry=malloc(sizeof(*entry));
    if (!entry) { qa_error_set(error,QA_ERROR_MEMORY,0,"allocating operation registration"); return false; }
    *entry=(operation_entry){.hook=*hook,.sequence=operation->next_sequence++,.active=true};
    operation_entry **link=&operation->entries;
    while (*link && ((*link)->hook.order<hook->order ||
           ((*link)->hook.order==hook->order && (*link)->sequence<entry->sequence))) link=&(*link)->next;
    entry->next=*link; *link=entry; ++operation->active_count;
    *out=entry->sequence; return true;
}

bool qa_operation_unregister(qa_operation *operation, qa_operation_registration registration) {
    if (!operation) return false;
    for (operation_entry *entry=operation->entries;entry;entry=entry->next) {
        if (entry->sequence!=registration || !entry->active) continue;
        entry->active=false; --operation->active_count; sweep(operation); return true;
    }
    return false;
}
void qa_operation_remove_owner(qa_operation *operation, qa_actor_owner owner) {
    if (!operation) return;
    for (operation_entry *entry=operation->entries;entry;entry=entry->next)
        if (entry->active && entry->hook.owner==owner) { entry->active=false; --operation->active_count; }
    sweep(operation);
}
void qa_operation_clear(qa_operation *operation) {
    if (!operation) return;
    for (operation_entry *entry=operation->entries;entry;entry=entry->next) entry->active=false;
    operation->active_count=0; sweep(operation);
}
bool qa_operation_active(const qa_operation *operation) { return operation && operation->active_count!=0; }

static bool continuation_failure(operation_frame *frame, qa_error *error, const char *message) {
    if (!frame->failed) {
        qa_error_set(&frame->failure,QA_ERROR_ARGUMENT,0,"%s",message); frame->failed=true;
    }
    if (error) *error=frame->failure;
    return false;
}
bool qa_operation_continue(qa_operation_next next, const void *request, void *result, qa_error *error) {
    if (!next.operation) return argument(error,"invalid operation continuation");
    operation_frame *frame=next.operation->current;
    while (frame && frame->invocation!=next.invocation) frame=frame->previous;
    if (!frame) return argument(error,"operation continuation is closed");
    if (!frame->continuation_open) return continuation_failure(frame,error,"operation continuation is closed");
    if (frame->called) return continuation_failure(frame,error,"operation continuation was already called");
    if (!request || (next.operation->result_size && !result)) return continuation_failure(frame,error,"invalid continuation request or result");
    frame->called=true;
    memmove(frame->request,request,next.operation->request_size);
    qa_error failure={0};
    if (!frame->canonical(frame->context,frame->request,result,&failure)) {
        if (!frame->failed) {
            frame->failure=failure;
            if (failure.code==QA_OK) qa_error_set(&frame->failure,QA_ERROR_ARGUMENT,0,"canonical gameplay operation failed");
            frame->failed=true;
        }
        if (error) *error=frame->failure;
        return false;
    }
    return true;
}

bool qa_operation_dispatch(qa_operation *operation, const void *request, void *result,
                           qa_operation_canonical_fn canonical, void *canonical_context,
                           qa_operation_committed_fn committed, void *committed_context, qa_error *error) {
    if (!operation || !request || !canonical || (operation->result_size && !result))
        return argument(error,"invalid gameplay operation dispatch");
    /* Hold lifetime during the direct path too: callbacks may register hooks or
     * try to retire their own owner, and destruction must remain forbidden. */
    if (!operation->active_count) {
        ++operation->depth;
        bool success=canonical(canonical_context,request,result,error);
        if (success && committed) success=committed(committed_context,error);
        --operation->depth; sweep(operation); return success;
    }
    if (operation->next_invocation==UINT64_MAX) return argument(error,"operation invocation identity exhausted");
    operation_frame *frame=operation->spare;
    if (frame) operation->spare=frame->previous;
    else {
        frame=calloc(1,sizeof(*frame));
        if (frame) frame->request=malloc(operation->request_size);
        if (!frame || !frame->request) {
            free(frame); qa_error_set(error,QA_ERROR_MEMORY,0,"allocating operation invocation"); return false;
        }
    }
    memcpy(frame->request,request,operation->request_size);
    frame->previous=operation->current; frame->canonical=canonical; frame->context=canonical_context;
    frame->invocation=operation->next_invocation++; frame->called=false; frame->failed=false; frame->continuation_open=false;
    operation->current=frame; ++operation->depth;
    uint64_t limit=operation->next_sequence;
    bool success=false;
    for (operation_entry *entry=operation->entries;entry;entry=entry->next) {
        if (entry->active && entry->sequence<limit && entry->hook.kind==QA_OPERATION_TRANSFORM &&
            !entry->hook.call.transform(entry->hook.context,frame->request,error)) goto finished;
    }
    operation_entry *replacement=NULL;
    for (operation_entry *entry=operation->entries;entry;entry=entry->next)
        if (entry->active && entry->sequence<limit && entry->hook.kind==QA_OPERATION_REPLACE) { replacement=entry; break; }
    if (replacement) {
        frame->continuation_open=true;
        success=replacement->hook.call.replace(replacement->hook.context,frame->request,
                    (qa_operation_next){operation,frame->invocation},result,error);
        frame->continuation_open=false;
        if (frame->failed) { if (error) *error=frame->failure; success=false; }
    } else success=canonical(canonical_context,frame->request,result,error);
    if (!success) goto finished;
    if (committed && !committed(committed_context,error)) { success=false; goto finished; }
    for (operation_entry *entry=operation->entries;entry;entry=entry->next) {
        if (entry->active && entry->sequence<limit && entry->hook.kind==QA_OPERATION_OBSERVE &&
            !entry->hook.call.observe(entry->hook.context,frame->request,result,error)) { success=false; break; }
    }
finished:
    operation->current=frame->previous;
    frame->previous=operation->spare; operation->spare=frame;
    --operation->depth; sweep(operation); return success;
}
