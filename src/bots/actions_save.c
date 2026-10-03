#include "actions_private.h"
#include "save_fields.h"
#include "qa/bot_actions_save.h"
#include "qa/bots_allocator_save.h"
#include <limits.h>

static const uint8_t source_magic[8]={'Q','A','E','A','S','R','C',0};
static const uint8_t owner_magic[8]={'Q','A','B','A','C','T','N',0};
static bool owner_signature(qa_source_save_io *io) {
    uint8_t magic[8];memcpy(magic,owner_magic,8);
    if(!qa_source_save_bytes(io,magic,8)) return false;
    return !memcmp(magic,owner_magic,8) ? true :
        bot_save_fail(io,QA_ERROR_FORMAT,"Invalid standalone action allocator signature");
}

static bool alias_fields(qa_source_save_io *io,qa_bot_actions *actions)
{
    bool initialized=actions->initialized;
    uint32_t capacity=actions->capacity;
    size_t ordinal=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE && initialized &&
       !qa_bot_memory_reference(actions->memory,actions->inputs,&ordinal,io->error)) return false;
    if(!qa_source_save_bool(io,&initialized) || !qa_source_save_u32(io,&capacity) ||
       !qa_source_save_count(io,&ordinal,SIZE_MAX)) return false;
    if(capacity>INT32_MAX/40 || (!initialized && ordinal))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid source action capacity or allocation reference");
    if(io->direction==QA_SOURCE_SAVE_READ) {
        qa_bot_memory_allocation allocation={0};
        if(initialized) {
            qa_bot_memory_span span;qa_bot_memory_kind kind;
            if(!qa_bot_memory_resolve(actions->memory,ordinal,&allocation,io->error) ||
               !qa_bot_memory_bytes(actions->memory,allocation,&span,io->error) ||
               !qa_bot_memory_kind_read(actions->memory,allocation,&kind,io->error)) return false;
            if(span.size!=capacity*40 || kind!=QA_BOT_MEMORY_HUNK)
                return bot_save_fail(io,QA_ERROR_FORMAT,"Source action allocation extent or kind differs");
        }
        actions->inputs=allocation;actions->capacity=capacity;actions->initialized=initialized;
    }
    return true;
}

bool qa_bot_actions_source_capture(const qa_bot_actions *actions,qa_buffer *out,qa_error *error)
{
    bot_action_snapshot snapshot;
    if(!actions || !out || !qa_bot_actions_idle(actions)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Action capture requires its idle source owner and output");return false;
    }
    if(!bot_action_snapshot_capture((qa_bot_actions *)actions,&snapshot,error)) return false;
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && bot_save_signature(&io,source_magic) &&
        alias_fields(&io,(qa_bot_actions *)actions) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return ok;
}

bool qa_bot_actions_source_restore(qa_bot_actions *actions,qa_bytes bytes,qa_error *error)
{
    if(!actions || !qa_bot_actions_idle(actions) || actions->initialized) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Bot actions are absent or restoring");return false;
    }
    qa_bot_actions candidate=*actions;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && bot_save_signature(&io,source_magic) &&
        alias_fields(&io,&candidate) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(ok) {actions->inputs=candidate.inputs;actions->capacity=candidate.capacity;
        actions->initialized=candidate.initialized;}
    return ok;
}

static bool buffer_fields(qa_source_save_io *io,qa_buffer *buffer)
{
    size_t size=buffer->size;
    if(!qa_source_save_count(io,&size,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(io->offset>io->input.size || size>io->input.size-io->offset)
            return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated standalone action section");
        buffer->data=size?malloc(size):NULL;buffer->size=size;
        if(size && !buffer->data) return bot_save_fail(io,QA_ERROR_MEMORY,"Restoring standalone action section");
    }
    return qa_source_save_bytes(io,buffer->data,size);
}

bool qa_bot_actions_capture(const qa_bot_actions *actions,qa_buffer *out,qa_error *error)
{
    if(!actions || !actions->owns_memory || !out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Standalone action capture requires its actual allocator");return false;
    }
    qa_buffer memory={0},aliases={0};qa_source_save_io io={0};
    bool ok=qa_bot_actions_source_capture(actions,&aliases,error) &&
        qa_bot_memory_capture(actions->memory,&memory,error) &&
        qa_source_save_writer(&io,NULL,error) && owner_signature(&io) &&
        buffer_fields(&io,&memory) && buffer_fields(&io,&aliases) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);qa_buffer_free(&memory);qa_buffer_free(&aliases);return ok;
}

bool qa_bot_actions_restore_bytes(qa_bot_actions *actions,qa_bytes bytes,qa_error *error)
{
    if(!actions || !actions->owns_memory || !qa_bot_actions_idle(actions)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Standalone action restore requires an idle owned allocator");return false;
    }
    qa_buffer memory={0},aliases={0};qa_source_save_io io={0};qa_bot_actions *candidate=NULL;
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && owner_signature(&io) &&
        buffer_fields(&io,&memory) && buffer_fields(&io,&aliases) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(ok) ok=qa_bot_actions_create_source(NULL,&actions->services,&candidate,error) &&
        qa_bot_memory_restore(candidate->memory,(qa_bytes){memory.data,memory.size},error) &&
        qa_bot_actions_source_restore(candidate,(qa_bytes){aliases.data,aliases.size},error);
    if(ok) {
        qa_bot_memory *previous=actions->memory;
        actions->memory=candidate->memory;candidate->memory=previous;
        actions->inputs=candidate->inputs;actions->capacity=candidate->capacity;
        actions->initialized=candidate->initialized;
    }
    qa_bot_actions_destroy(candidate);qa_buffer_free(&memory);qa_buffer_free(&aliases);return ok;
}
