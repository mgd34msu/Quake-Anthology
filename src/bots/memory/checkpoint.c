#include "internal.h"
#include "qa/bots_allocator_checkpoint.h"

typedef struct memory_image_record {
    qa_bot_memory_allocation allocation;
    uint32_t size,line;
    qa_bot_memory_kind kind;
    bool has_provenance;
    char *file,*label;
    uint8_t *bytes;
} memory_image_record;
struct qa_bot_memory_checkpoint {
    qa_bot_memory *owner;
    memory_image_record *records;
    size_t count,leases;
    bool destroy_pending;
};
struct qa_bot_memory_prepared {
    qa_bot_memory *owner;
    qa_bot_memory_checkpoint *image;
    bot_memory_record *records;
    bool *borrowed;
    qa_bot_memory_allocation *aliases;
    uint32_t slots,capacity,first,last,free_head;
    uint64_t next_generation,allocated_bytes;
};
static char *copy_text(const char *text,qa_error *error)
{
    size_t size=strlen(text)+1;
    char *copy=malloc(size);
    if(!copy) {bot_memory_fail(error,QA_ERROR_MEMORY,"Retaining bot memory checkpoint provenance");return NULL;}
    memcpy(copy,text,size);return copy;
}
static void release_record(qa_bot_memory *owner,bot_memory_record *record)
{
    if(!record->backing) return;
    qa_bot_memory_backing backing={record->backing,record->size+4,record->host_allocation};
    if(owner->options.host.release)
        owner->options.host.release(owner->options.host.context,&backing,record->kind);
    else free(record->backing);
    free(record->file);free(record->label);
}
void qa_bot_memory_checkpoint_destroy(qa_bot_memory_checkpoint *image)
{
    if(!image) return;
    if(image->leases) {image->destroy_pending=true;return;}
    for(size_t i=0;i<image->count;++i) {
        free(image->records[i].bytes);free(image->records[i].file);free(image->records[i].label);
    }
    free(image->records);
    (void)qa_bot_memory_release(image->owner,NULL);free(image);
}
bool qa_bot_memory_checkpoint_capture(qa_bot_memory *memory,qa_bot_memory_checkpoint **out,qa_error *error)
{
    if(!bot_memory_mutable(memory,error) || !out || *out)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory checkpoint requires its idle owner and empty output");
    if(memory->live_count>SIZE_MAX/sizeof(memory_image_record))
        return bot_memory_fail(error,QA_ERROR_MEMORY,"Bot memory checkpoint extent overflow");
    qa_bot_memory_checkpoint *image=calloc(1,sizeof(*image));
    if(!image) return bot_memory_fail(error,QA_ERROR_MEMORY,"Allocating bot memory checkpoint");
    if(!qa_bot_memory_retain(memory,error)) {free(image);return false;}
    image->owner=memory;
    if(memory->live_count && !(image->records=calloc(memory->live_count,sizeof(*image->records)))) {
        bot_memory_fail(error,QA_ERROR_MEMORY,"Capturing ordered bot allocations");goto failed;
    }
    uint32_t previous=0;uint64_t total=0;
    for(uint32_t link=memory->first;link;) {
        if(link>memory->slots || image->count>=memory->live_count) goto invalid;
        bot_memory_record *record=&memory->records[link-1];
        if(!record->live || !record->backing || record->previous!=previous ||
           record->size>INT32_MAX-4 || (unsigned)record->kind>QA_BOT_MEMORY_HUNK ||
           !record->generation || (record->has_provenance && (!record->file || !record->label))) goto invalid;
        memory_image_record *row=&image->records[image->count++];
        row->allocation=bot_memory_handle(memory,link-1);row->size=record->size;
        row->kind=record->kind;row->line=record->line;row->has_provenance=record->has_provenance;
        row->bytes=malloc((size_t)row->size+4);
        if(!row->bytes) {bot_memory_fail(error,QA_ERROR_MEMORY,"Capturing bot allocation bytes");goto failed;}
        memcpy(row->bytes,record->backing,(size_t)row->size+4);
        if(row->has_provenance) {
            row->file=copy_text(record->file,error);
            if(row->file) row->label=copy_text(record->label,error);
            if(!row->file || !row->label) goto failed;
        }
        total+=row->size;previous=link;link=record->next;
    }
    if(image->count!=memory->live_count || previous!=memory->last || total!=memory->allocated_bytes) goto invalid;
    *out=image;return true;
invalid:
    bot_memory_fail(error,QA_ERROR_FORMAT,"Invalid ordered bot memory checkpoint owner");
failed:
    qa_bot_memory_checkpoint_destroy(image);return false;
}
static bool stage_record(qa_bot_memory_prepared *plan,const memory_image_record *image,
    bot_memory_record *record,qa_error *error)
{
    record->size=image->size;record->kind=image->kind;record->line=image->line;
    record->has_provenance=image->has_provenance;
    if(image->has_provenance) {
        record->file=copy_text(image->file,error);
        if(record->file) record->label=copy_text(image->label,error);
        if(!record->file || !record->label) return false;
    }
    qa_bot_memory_backing backing={0};
    bool ok;
    if(plan->owner->options.host.allocate)
        ok=plan->owner->options.host.allocate(plan->owner->options.host.context,image->size+4,
            image->kind,false,&backing,error);
    else {
        backing.bytes=malloc((size_t)image->size+4);backing.size=image->size+4;
        ok=backing.bytes!=NULL;
        if(!ok) bot_memory_fail(error,QA_ERROR_MEMORY,"Preparing missing captured bot allocation");
    }
    if(!ok) return false;
    if(!backing.bytes || backing.size!=image->size+4) {
        if(plan->owner->options.host.release)
            plan->owner->options.host.release(plan->owner->options.host.context,&backing,image->kind);
        else free(backing.bytes);
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot checkpoint host returned the wrong backing extent");
    }
    record->backing=backing.bytes;record->host_allocation=backing.token;
    memcpy(record->backing,image->bytes,(size_t)image->size+4);return true;
}
bool qa_bot_memory_checkpoint_prepare(qa_bot_memory *memory,const qa_bot_memory_checkpoint *captured,
    qa_bot_memory_prepared **out,qa_error *error)
{
    if(!bot_memory_mutable(memory,error) || !captured || captured->owner!=memory ||
       captured->destroy_pending || captured->leases==SIZE_MAX || !out || *out)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory restore requires its captured owner and empty output");
    if(captured->count>UINT32_MAX-memory->slots ||
       (uint64_t)(memory->slots+captured->count)*sizeof(bot_memory_record)>SIZE_MAX ||
       captured->count>SIZE_MAX/sizeof(qa_bot_memory_allocation))
        return bot_memory_fail(error,QA_ERROR_MEMORY,"Prepared bot memory extent overflow");
    qa_bot_memory_prepared *plan=calloc(1,sizeof(*plan));
    if(!plan) return bot_memory_fail(error,QA_ERROR_MEMORY,"Preparing bot memory checkpoint");
    plan->owner=memory;plan->image=(qa_bot_memory_checkpoint *)captured;
    ++plan->image->leases;memory->busy=true;
    plan->capacity=(uint32_t)(memory->slots+captured->count);plan->slots=memory->slots;
    plan->next_generation=memory->next_generation;
    if(plan->capacity) {
        plan->records=calloc(plan->capacity,sizeof(*plan->records));
        plan->borrowed=calloc(plan->capacity,sizeof(*plan->borrowed));
        if(!plan->records || !plan->borrowed) goto memory_failure;
    }
    if(captured->count && !(plan->aliases=calloc(captured->count,sizeof(*plan->aliases)))) goto memory_failure;
    for(size_t i=0;i<captured->count;++i) {
        const memory_image_record *image=&captured->records[i];
        bot_memory_record *record=bot_memory_record_get(memory,image->allocation);
        if(!record) continue;
        if(record->size!=image->size || record->kind!=image->kind ||
           record->has_provenance!=image->has_provenance ||
           (image->has_provenance && (record->line!=image->line ||
            strcmp(record->file,image->file) || strcmp(record->label,image->label)))) {
            bot_memory_fail(error,QA_ERROR_FORMAT,"Captured bot backing changed allocation identity");goto failed;
        }
        uint32_t slot=image->allocation.slot;
        plan->records[slot]=*record;plan->borrowed[slot]=true;
        plan->aliases[i]=image->allocation;
    }
    uint32_t unused=0;
    for(size_t i=0;i<captured->count;++i) {
        const memory_image_record *image=&captured->records[i];
        if(!plan->aliases[i].owner) {
            while(unused<plan->capacity && plan->records[unused].live) ++unused;
            if(unused==plan->capacity || !plan->next_generation) {
                bot_memory_fail(error,QA_ERROR_MEMORY,"Restored bot allocation identities exhausted");goto failed;
            }
            bot_memory_record *record=&plan->records[unused];
            if(!stage_record(plan,image,record,error)) goto failed;
            record->live=true;record->generation=plan->next_generation++;
            plan->aliases[i]=(qa_bot_memory_allocation){memory->owner,record->generation,unused};
            if(unused>=plan->slots) plan->slots=unused+1;
        }
        uint32_t link=plan->aliases[i].slot+1;
        bot_memory_record *record=&plan->records[link-1];
        record->previous=plan->last;record->next=0;record->free_next=0;
        if(plan->last) plan->records[plan->last-1].next=link;else plan->first=link;
        plan->last=link;plan->allocated_bytes+=record->size;
    }
    for(uint32_t slot=plan->slots;slot>0;--slot)
        if(!plan->records[slot-1].live) {
            plan->records[slot-1].free_next=plan->free_head;plan->free_head=slot;
        }
    *out=plan;return true;
memory_failure:
    bot_memory_fail(error,QA_ERROR_MEMORY,"Allocating prepared bot memory topology");
failed:
    qa_bot_memory_checkpoint_finish(plan,false);return false;
}
bool qa_bot_memory_checkpoint_resolve(const qa_bot_memory_prepared *plan,qa_bot_memory_allocation captured,
    qa_bot_memory_allocation *out,qa_error *error)
{
    if(!plan || !out) return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Prepared bot alias output is absent");
    for(size_t i=0;i<plan->image->count;++i) {
        qa_bot_memory_allocation allocation=plan->image->records[i].allocation;
        if(allocation.owner==captured.owner && allocation.slot==captured.slot && allocation.generation==captured.generation) {
            *out=plan->aliases[i];return true;
        }
    }
    return bot_memory_fail(error,QA_ERROR_FORMAT,"Bot alias is outside the captured live allocations");
}
void qa_bot_memory_checkpoint_finish(qa_bot_memory_prepared *plan,bool commit)
{
    if(!plan) return;
    qa_bot_memory *memory=plan->owner;
    if(commit) {
        for(uint32_t slot=0;slot<memory->slots;++slot)
            if(memory->records[slot].live && !plan->borrowed[slot]) release_record(memory,&memory->records[slot]);
        for(size_t i=0;i<plan->image->count;++i) {
            bot_memory_record *record=&plan->records[plan->aliases[i].slot];
            memcpy(record->backing,plan->image->records[i].bytes,(size_t)record->size+4);
        }
        free(memory->records);memory->records=plan->records;plan->records=NULL;
        memory->slots=plan->slots;memory->capacity=plan->capacity;memory->free_head=plan->free_head;
        memory->first=plan->first;memory->last=plan->last;memory->next_generation=plan->next_generation;
        memory->live_count=plan->image->count;memory->allocated_bytes=plan->allocated_bytes;
        free(memory->script_restored);memory->script_restored=plan->aliases;plan->aliases=NULL;
        memory->script_restored_count=plan->image->count;
    } else if(plan->records) {
        for(uint32_t slot=0;slot<plan->capacity;++slot)
            if(!plan->borrowed || !plan->borrowed[slot]) {
                bot_memory_record *record=&plan->records[slot];
                if(record->backing) release_record(memory,record);
                else {free(record->file);free(record->label);}
            }
    }
    free(plan->records);free(plan->borrowed);free(plan->aliases);
    memory->busy=false;
    qa_bot_memory_checkpoint *image=plan->image;
    --image->leases;
    if(!image->leases && image->destroy_pending) qa_bot_memory_checkpoint_destroy(image);
    free(plan);
}
