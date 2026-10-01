#include "internal.h"
#include "../save_fields.h"
#include "qa/bots_allocator_save.h"

static const uint8_t magic[8]={'Q','A','B','M','E','M',0,0};

static bool fields(qa_source_save_io *io,qa_bot_memory *memory)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=reading?0:memory->live_count;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/9))
        return bot_save_fail(io,QA_ERROR_FORMAT,"Truncated ordered bot memory allocations");
    uint32_t link=memory->first;
    for(size_t i=0;i<count;++i) {
        bot_memory_record *record=reading?NULL:&memory->records[link-1];
        uint32_t kind=reading?0:(uint32_t)record->kind,size=reading?0:record->size;
        bool has_provenance=!reading && record->has_provenance;
        const char *file=!reading?record->file:NULL,*label=!reading?record->label:NULL;
        uint32_t line=reading?0:record->line;
        bool ok=qa_source_save_u32(io,&kind) && kind<=QA_BOT_MEMORY_HUNK &&
            qa_source_save_u32(io,&size) && size<=INT32_MAX-4 &&
            qa_source_save_bool(io,&has_provenance);
        if(ok && has_provenance) ok=bot_save_text(io,&file) && file &&
            qa_source_save_u32(io,&line) && bot_save_text(io,&label) && label;
        if(ok && reading) {
            if(io->offset>io->input.size || size>io->input.size-io->offset)
                ok=bot_save_fail(io,QA_ERROR_FORMAT,"Truncated retained bot allocation bytes");
            qa_bot_memory_provenance provenance={file,line,label};
            qa_bot_memory_allocation allocation={0};
            if(ok && !bot_memory_allocate(memory,size,(qa_bot_memory_kind)kind,false,
                has_provenance?&provenance:NULL,&allocation,io->error)) {
                io->failed=true;ok=false;
            }
            if(ok) record=bot_memory_record_get(memory,allocation);
        }
        if(ok) ok=qa_source_save_bytes(io,record->backing+4,size);
        if(reading) {free((void *)file);free((void *)label);}
        if(!ok) {
            if(!io->failed) bot_save_fail(io,QA_ERROR_FORMAT,"Invalid true bot allocation continuation");
            return false;
        }
        if(!reading) link=record->next;
    }
    return true;
}
bool qa_bot_memory_capture(const qa_bot_memory *memory,qa_buffer *out,qa_error *error)
{
    if(!memory || !out || memory->busy || memory->disposed)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory capture requires its actual idle live owner");
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && bot_save_signature(&io,magic) &&
        fields(&io,(qa_bot_memory *)memory) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return ok;
}
bool qa_bot_memory_restore(qa_bot_memory *memory,qa_bytes bytes,qa_error *error)
{
    if(!bot_memory_mutable(memory,error) || memory->live_count)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory import requires its actual fresh owner");
    qa_bot_memory scratch={.options=memory->options,.owner=memory->owner,
        .next_generation=memory->next_generation,.busy=true};
    qa_source_save_io io={0};memory->busy=true;
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && bot_save_signature(&io,magic) &&
        fields(&io,&scratch) && qa_source_save_finish(&io,NULL);
    if(ok) {
        bot_memory_clear(memory);
        memory->records=scratch.records;memory->capacity=scratch.capacity;memory->slots=scratch.slots;
        memory->first=scratch.first;memory->last=scratch.last;memory->free_head=scratch.free_head;
        memory->next_generation=scratch.next_generation;
        memory->live_count=scratch.live_count;memory->allocated_bytes=scratch.allocated_bytes;
    } else bot_memory_clear(&scratch);
    memory->busy=false;qa_source_save_dispose(&io);return ok;
}
bool qa_bot_memory_reference(const qa_bot_memory *memory,qa_bot_memory_allocation allocation,
    size_t *out,qa_error *error)
{
    if(!memory || memory->busy || memory->disposed || !out || !bot_memory_owned(memory,allocation))
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot allocation reference requires its actual owner/output");
    size_t index=0;
    for(uint32_t link=memory->first;link;link=memory->records[link-1].next,++index)
        if(link-1==allocation.slot) {*out=index;return true;}
    return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot checkpoint references an unowned or freed allocation");
}
bool qa_bot_memory_resolve(const qa_bot_memory *memory,size_t reference,
    qa_bot_memory_allocation *out,qa_error *error)
{
    if(!memory || memory->busy || memory->disposed || !out || reference>=memory->live_count)
        return bot_memory_fail(error,QA_ERROR_FORMAT,"Saved bot allocation reference is outside its true owner");
    uint32_t link=memory->first;
    for(size_t i=0;i<reference;++i) link=memory->records[link-1].next;
    *out=bot_memory_handle(memory,link-1);return true;
}
