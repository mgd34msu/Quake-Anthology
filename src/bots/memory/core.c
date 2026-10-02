#include "internal.h"
#include "qa/bots_allocator_save.h"
#include <stdio.h>
#include <stdatomic.h>
#include "qa/text.h"

enum { PREFIX_BYTES=4 };
#define HEAP_ID UINT32_C(0x12345678)
#define HUNK_ID UINT32_C(0x87654321)
static _Atomic uint64_t next_owner=1;

bool bot_memory_fail(qa_error *error,qa_status code,const char *message)
{
    qa_error_set(error,code,0,"%s",message);return false;
}
bool bot_memory_mutable(qa_bot_memory *memory,qa_error *error)
{
    return memory && !memory->busy && !memory->disposed ? true :
        bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory is absent, disposed or executing a callback");
}
bot_memory_record *bot_memory_record_get(const qa_bot_memory *memory,qa_bot_memory_allocation allocation)
{
    if(!memory || allocation.owner!=memory->owner || allocation.slot>=memory->slots) return NULL;
    bot_memory_record *record=&memory->records[allocation.slot];
    return record->live && record->generation==allocation.generation?record:NULL;
}
bool bot_memory_owned(const qa_bot_memory *memory,qa_bot_memory_allocation allocation)
{
    return bot_memory_record_get(memory,allocation)!=NULL;
}
qa_bot_memory_allocation bot_memory_handle(const qa_bot_memory *memory,uint32_t slot)
{
    return (qa_bot_memory_allocation){memory->owner,memory->records[slot].generation,slot};
}
static bool reserve_slot(qa_bot_memory *memory,qa_error *error)
{
    if(memory->free_head || memory->slots<memory->capacity) return true;
    if(memory->capacity==UINT32_MAX)
        return bot_memory_fail(error,QA_ERROR_MEMORY,"Bot allocation slots exhausted");
    uint32_t capacity=memory->capacity?memory->capacity:16;
    if(memory->capacity) capacity=capacity>UINT32_MAX/2?UINT32_MAX:capacity*2;
    if((uint64_t)capacity*sizeof(*memory->records)>SIZE_MAX)
        return bot_memory_fail(error,QA_ERROR_MEMORY,"Bot allocation slot size overflow");
    bot_memory_record *records=realloc(memory->records,(size_t)capacity*sizeof(*records));
    if(!records) return bot_memory_fail(error,QA_ERROR_MEMORY,"Growing bot allocation slots");
    memset(records+memory->capacity,0,(size_t)(capacity-memory->capacity)*sizeof(*records));
    memory->records=records;memory->capacity=capacity;return true;
}
static char *text_copy(const char *text,qa_error *error)
{
    if(!text) {bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory provenance requires its actual text");return NULL;}
    size_t length=strlen(text);
    if(length==SIZE_MAX) {bot_memory_fail(error,QA_ERROR_MEMORY,"Bot memory provenance size overflow");return NULL;}
    if(!qa_utf8_valid((qa_bytes){(const uint8_t *)text,length})) {
        bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot allocation provenance requires valid UTF-8 text");return NULL;
    }
    char *copy=malloc(length+1);
    if(!copy) {bot_memory_fail(error,QA_ERROR_MEMORY,"Retaining bot allocation provenance");return NULL;}
    memcpy(copy,text,length+1);return copy;
}
static void prefix_write(uint8_t *bytes,uint32_t value)
{
    for(uint32_t i=0;i<4;++i) bytes[i]=(uint8_t)(value>>(i*8));
}
static uint32_t prefix_read(const uint8_t *bytes)
{
    return (uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
}
bool qa_bot_memory_create(const qa_bot_memory_options *options,qa_bot_memory **out,qa_error *error)
{
    if(!out || *out || (options && ((unsigned)options->profile>QA_BOT_MEMORY_DEBUG ||
       (!!options->host.allocate!=!!options->host.release) ||
       (!!options->host.allocate!=!!options->host.available) ||
       (options->profile!=QA_BOT_MEMORY_RELEASE && (!options->print || !options->log)))))
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Invalid bot memory owner services/output");
    qa_bot_memory *memory=calloc(1,sizeof(*memory));
    if(!memory) return bot_memory_fail(error,QA_ERROR_MEMORY,"Allocating bot memory owner");
    if(options) memory->options=*options;
    uint64_t owner=atomic_load_explicit(&next_owner,memory_order_relaxed);
    for(;;) {
        if(owner==UINT64_MAX) {free(memory);return bot_memory_fail(error,QA_ERROR_MEMORY,"Bot allocation owner identities exhausted");}
        if(atomic_compare_exchange_weak_explicit(&next_owner,&owner,owner+1,
            memory_order_relaxed,memory_order_relaxed)) break;
    }
    memory->owner=owner;memory->next_generation=1;
    memory->references=1;
    *out=memory;return true;
}
bool bot_memory_allocate(qa_bot_memory *memory,uint32_t size,qa_bot_memory_kind kind,bool clear,
    const qa_bot_memory_provenance *provenance,qa_bot_memory_allocation *out,qa_error *error)
{
    if(!out || (unsigned)kind>QA_BOT_MEMORY_HUNK || size>INT32_MAX-PREFIX_BYTES ||
       memory->live_count==SIZE_MAX || !memory->next_generation || size>UINT64_MAX-memory->allocated_bytes)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot allocation exceeds its source size or owner capacity");
    if(!reserve_slot(memory,error)) return false;
    bot_memory_record pending={0},*record=&pending;
    record->kind=kind;record->size=size;
    if(provenance) {
        record->file=text_copy(provenance->file,error);
        if(record->file) record->label=text_copy(provenance->label,error);
        if(!record->file || !record->label) {free(record->file);free(record->label);return false;}
        record->line=provenance->line;record->has_provenance=true;
    }
    qa_bot_memory_backing backing={0};
    bool ok;
    if(memory->options.host.allocate) {
        ok=memory->options.host.allocate(memory->options.host.context,size+PREFIX_BYTES,kind,clear,&backing,error);
        if(ok && (backing.size!=size+PREFIX_BYTES || !backing.bytes))
            ok=bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory host returned an allocation of the wrong size");
    } else {
        backing=(qa_bot_memory_backing){.bytes=calloc((size_t)size+PREFIX_BYTES,1),.size=size+PREFIX_BYTES};
        ok=backing.bytes!=NULL;
        if(!ok) bot_memory_fail(error,QA_ERROR_MEMORY,"Allocating bot source bytes");
    }
    if(!ok) {free(record->file);free(record->label);return false;}
    prefix_write(backing.bytes,kind==QA_BOT_MEMORY_HEAP?HEAP_ID:HUNK_ID);
    if(clear && size) memset(backing.bytes+PREFIX_BYTES,0,size);
    record->backing=backing.bytes;record->host_allocation=backing.token;record->live=true;
    uint32_t slot;
    if(memory->free_head) {slot=memory->free_head-1;memory->free_head=memory->records[slot].free_next;}
    else slot=memory->slots++;
    record->generation=memory->next_generation++;
    record->previous=memory->last;
    if(memory->last) memory->records[memory->last-1].next=slot+1;else memory->first=slot+1;
    memory->last=slot+1;memory->records[slot]=*record;
    ++memory->live_count;memory->allocated_bytes+=size;
    *out=bot_memory_handle(memory,slot);return true;
}
bool qa_bot_memory_allocate(qa_bot_memory *memory,uint32_t size,qa_bot_memory_kind kind,bool clear,
    const qa_bot_memory_provenance *provenance,qa_bot_memory_allocation *out,qa_error *error)
{
    if(!bot_memory_mutable(memory,error)) return false;
    memory->busy=true;
    bool ok=bot_memory_allocate(memory,size,kind,clear,provenance,out,error);
    memory->busy=false;return ok;
}
bool qa_bot_memory_bytes(const qa_bot_memory *memory,qa_bot_memory_allocation allocation,
    qa_bot_memory_span *out,qa_error *error)
{
    bot_memory_record *record=bot_memory_record_get(memory,allocation);
    if(!memory || memory->disposed || !out || !record)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot allocation is unowned, freed or disposed");
    *out=(qa_bot_memory_span){record->backing+PREFIX_BYTES,record->size};return true;
}
bool qa_bot_memory_kind_read(const qa_bot_memory *memory,qa_bot_memory_allocation allocation,
    qa_bot_memory_kind *out,qa_error *error)
{
    bot_memory_record *record=bot_memory_record_get(memory,allocation);
    if(!memory || memory->disposed || !record || !out)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot allocation kind requires its actual live owner/output");
    *out=record->kind;return true;
}
bool bot_memory_release(qa_bot_memory *memory,qa_bot_memory_allocation allocation,bool force,qa_error *error)
{
    bot_memory_record *record=bot_memory_record_get(memory,allocation);
    if(!record) {
        if(memory->options.profile!=QA_BOT_MEMORY_RELEASE)
            return memory->options.print(memory->options.context,QA_SCRIPT_FATAL,"FreeMemory: invalid memory block\n",error);
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot allocation belongs to another owner or has been freed");
    }
    if(!force) {
        if(prefix_read(record->backing)!=HEAP_ID) return true;
        if(record->kind!=QA_BOT_MEMORY_HEAP)
            return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot heap ID does not belong to a heap allocation");
    }
    qa_bot_memory_backing backing={record->backing,record->size+PREFIX_BYTES,record->host_allocation};
    if(memory->options.host.release) memory->options.host.release(memory->options.host.context,&backing,record->kind);
    else {memset(record->backing,0xaa,(size_t)record->size+PREFIX_BYTES);free(record->backing);}
    if(record->previous) memory->records[record->previous-1].next=record->next;else memory->first=record->next;
    if(record->next) memory->records[record->next-1].previous=record->previous;else memory->last=record->previous;
    --memory->live_count;memory->allocated_bytes-=record->size;
    free(record->file);free(record->label);memset(record,0,sizeof(*record));
    record->free_next=memory->free_head;memory->free_head=allocation.slot+1;
    return true;
}
bool qa_bot_memory_free(qa_bot_memory *memory,qa_bot_memory_allocation allocation,qa_error *error)
{
    if(!memory || memory->busy)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory free owner is absent or active");
    memory->busy=true;bool ok=bot_memory_release(memory,allocation,false,error);memory->busy=false;return ok;
}
bool qa_bot_memory_reset_hunk(qa_bot_memory *memory,qa_error *error)
{
    if(!memory || memory->busy)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot hunk reset owner is absent or active");
    memory->busy=true;
    uint32_t link=memory->first;
    bool ok=true;
    while(ok && link) {
        bot_memory_record *record=&memory->records[link-1];uint32_t next=record->next;
        if(record->kind==QA_BOT_MEMORY_HUNK) ok=bot_memory_release(memory,bot_memory_handle(memory,link-1),true,error);
        link=next;
    }
    memory->busy=false;return ok;
}
void bot_memory_clear(qa_bot_memory *memory)
{
    while(memory->first) (void)bot_memory_release(memory,bot_memory_handle(memory,memory->first-1),true,NULL);
    free(memory->records);memory->records=NULL;
    free(memory->script_restored);memory->script_restored=NULL;memory->script_restored_count=0;
    memory->first=memory->last=memory->free_head=memory->slots=memory->capacity=0;
}
bool qa_bot_memory_dispose(qa_bot_memory *memory,qa_error *error)
{
    if(memory && memory->disposed) return true;
    if(!bot_memory_mutable(memory,error)) return false;
    memory->busy=true;bot_memory_clear(memory);memory->disposed=true;memory->busy=false;return true;
}
bool qa_bot_memory_destroy(qa_bot_memory *memory,qa_error *error)
{
    if(!memory) return true;
    if(!qa_bot_memory_dispose(memory,error)) return false;
    return qa_bot_memory_release(memory,error);
}
bool qa_bot_memory_retain(qa_bot_memory *memory,qa_error *error)
{
    if(!memory || memory->references==SIZE_MAX)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory owner reference is unavailable");
    ++memory->references;return true;
}
bool qa_bot_memory_release(qa_bot_memory *memory,qa_error *error)
{
    if(!memory) return true;
    if(memory->references>1) {--memory->references;return true;}
    if(!qa_bot_memory_dispose(memory,error)) return false;
    free(memory);return true;
}
bool qa_bot_memory_idle(const qa_bot_memory *memory) {return !memory || !memory->busy;}
bool qa_bot_memory_disposed(const qa_bot_memory *memory) {return memory && memory->disposed;}
size_t qa_bot_memory_live_allocations(const qa_bot_memory *memory) {return memory?memory->live_count:0;}
uint64_t qa_bot_memory_allocated_bytes(const qa_bot_memory *memory) {return memory?memory->allocated_bytes:0;}
bool qa_bot_memory_available(qa_bot_memory *memory,uint64_t *out,qa_error *error)
{
    if(!memory || memory->busy || !out || !memory->options.host.available)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot AvailableMemory requires an actual host arena");
    memory->busy=true;
    bool ok=memory->options.host.available(memory->options.host.context,out,error);
    memory->busy=false;return ok;
}
bool qa_bot_memory_byte_size(const qa_bot_memory *memory,qa_bot_memory_allocation allocation,
    uint32_t *out,qa_error *error)
{
    if(!memory || !out || memory->options.profile==QA_BOT_MEMORY_RELEASE)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"MemoryByteSize requires the actual bot memory manager profile");
    bot_memory_record *record=bot_memory_record_get(memory,allocation);
    *out=record?record->size+PREFIX_BYTES:0;
    return true;
}
static int32_t source_kilobytes(uint64_t size)
{
    uint32_t word=(uint32_t)size;
    if(!(word&UINT32_C(0x80000000))) return (int32_t)(word>>10);
    return -(int32_t)(((~word)+1+1023u)>>10);
}
static bool used_size(qa_bot_memory *memory,qa_error *error)
{
    if(memory->options.profile==QA_BOT_MEMORY_RELEASE) return true;
    char text[128];
    (void)snprintf(text,sizeof(text),"total allocated memory: %d KB\n",source_kilobytes(memory->allocated_bytes));
    if(!memory->options.print(memory->options.context,QA_SCRIPT_INFO,text,error)) return false;
    (void)snprintf(text,sizeof(text),"total botlib memory: %d KB\n",
        source_kilobytes(memory->allocated_bytes+PREFIX_BYTES*(uint64_t)memory->live_count));
    if(!memory->options.print(memory->options.context,QA_SCRIPT_INFO,text,error)) return false;
    (void)snprintf(text,sizeof(text),"total memory blocks: %zu\n",memory->live_count);
    return memory->options.print(memory->options.context,QA_SCRIPT_INFO,text,error);
}
bool qa_bot_memory_print_used(qa_bot_memory *memory,qa_error *error)
{
    if(!memory || memory->busy) return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory report owner is absent or active");
    memory->busy=true;bool ok=used_size(memory,error);memory->busy=false;return ok;
}
static size_t source_padding(const char *text,size_t width)
{
    qa_bytes bytes={(const uint8_t *)text,strlen(text)};size_t cursor=0,count=0;uint32_t scalar;
    while(qa_utf8_next(bytes,&cursor,&scalar)) count+=scalar>0xffff?2u:1u;
    return count<width?width-count:0;
}
static bool labels(qa_bot_memory *memory,qa_error *error)
{
    if(!used_size(memory,error)) return false;
    if(memory->options.profile==QA_BOT_MEMORY_RELEASE) return true;
    if(!memory->options.log(memory->options.context,"============= Botlib memory log ==============\r\n\r\n",error)) return false;
    if(memory->options.profile!=QA_BOT_MEMORY_DEBUG) return true;
    size_t index=0;
    for(uint32_t link=memory->last;link;++index) {
        const bot_memory_record *record=&memory->records[link-1];link=record->previous;
        const char *file=record->has_provenance?record->file:"<unattributed>";
        const char *label=record->has_provenance?record->label:"";
        size_t file_size=strlen(file),label_size=strlen(label);
        if(file_size>SIZE_MAX-label_size || file_size+label_size>SIZE_MAX-128)
            return bot_memory_fail(error,QA_ERROR_MEMORY,"Bot memory label output size overflow");
        size_t capacity=file_size+label_size+128;
        char *text=malloc(capacity);
        if(!text) return bot_memory_fail(error,QA_ERROR_MEMORY,"Formatting actual bot memory label");
        int written;
        if(record->has_provenance)
            written=snprintf(text,capacity,"%6zu, %s, %8u: %*s%s line %6u: %s\r\n",
                index,record->kind==QA_BOT_MEMORY_HEAP?"heap":"hunk",record->size+PREFIX_BYTES,
                (int)source_padding(file,24),"",file,record->line,label);
        else written=snprintf(text,capacity,"%6zu, %s, %8u: <unattributed>\r\n",
                index,record->kind==QA_BOT_MEMORY_HEAP?"heap":"hunk",record->size+PREFIX_BYTES);
        bool ok=written>=0 && (size_t)written<capacity;
        if(ok) ok=memory->options.log(memory->options.context,text,error);
        else bot_memory_fail(error,QA_ERROR_MEMORY,"Actual bot memory label exceeds its output");
        free(text);if(!ok) return false;
    }
    return true;
}
bool qa_bot_memory_print_labels(qa_bot_memory *memory,qa_error *error)
{
    if(!memory || memory->busy) return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory label owner is absent or active");
    memory->busy=true;bool ok=labels(memory,error);memory->busy=false;return ok;
}
bool qa_bot_memory_dump(qa_bot_memory *memory,qa_error *error)
{
    if(!memory || memory->busy)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"Bot memory dump owner is absent or active");
    if(memory->options.profile==QA_BOT_MEMORY_RELEASE)
        return bot_memory_fail(error,QA_ERROR_ARGUMENT,"DumpMemory requires the actual bot memory manager profile");
    memory->busy=true;bool ok=true;
    uint32_t link=memory->last;
    while(ok && link) {
        uint32_t previous=memory->records[link-1].previous;
        ok=bot_memory_release(memory,bot_memory_handle(memory,link-1),false,error);link=previous;
    }
    memory->busy=false;return ok;
}

static qa_bot_memory_allocation script_allocation(qa_script_memory_allocation a) {
    return (qa_bot_memory_allocation){a.owner,a.generation,a.slot};
}
static qa_script_memory_allocation script_alias(qa_bot_memory_allocation a) {
    return (qa_script_memory_allocation){a.owner,a.generation,a.slot};
}
static bool script_retain(void *context,qa_error *error) {return qa_bot_memory_retain(context,error);}
static void script_release(void *context) {(void)qa_bot_memory_release(context,NULL);}
static bool script_allocate(void *context,uint32_t size,bool clear,qa_script_memory_allocation *out,qa_error *error) {
    qa_bot_memory_allocation a;
    if(!qa_bot_memory_allocate(context,size,QA_BOT_MEMORY_HEAP,clear,NULL,&a,error)) return false;
    *out=script_alias(a);return true;
}
static bool script_bytes(void *context,qa_script_memory_allocation a,qa_script_memory_span *out,qa_error *error) {
    qa_bot_memory_span span;
    if(!qa_bot_memory_bytes(context,script_allocation(a),&span,error)) return false;
    *out=(qa_script_memory_span){span.data,span.size};return true;
}
static bool script_free(void *context,qa_script_memory_allocation a,qa_error *error) {
    return qa_bot_memory_free(context,script_allocation(a),error);
}
static bool script_reference(void *context,qa_script_memory_allocation a,size_t *out,qa_error *error) {
    return qa_bot_memory_reference(context,script_allocation(a),out,error);
}
static bool script_resolve(void *context,size_t reference,qa_script_memory_allocation *out,qa_error *error) {
    qa_bot_memory_allocation a;
    if(!qa_bot_memory_resolve(context,reference,&a,error)) return false;
    *out=script_alias(a);return true;
}
static bool script_resolve_history(void *context,size_t reference,qa_script_memory_allocation *out,qa_error *error) {
    qa_bot_memory *memory=context;
    if(!memory || memory->busy || memory->disposed || reference>=memory->script_restored_count ||
       !bot_memory_owned(memory,memory->script_restored[reference]))
        return bot_memory_fail(error,QA_ERROR_FORMAT,"History script requires its committed MEMORY allocation alias");
    *out=script_alias(memory->script_restored[reference]);return true;
}
const qa_script_memory *qa_bot_memory_script_services(qa_bot_memory *memory) {
    if(!memory) return NULL;
    memory->scripts=(qa_script_memory){.context=memory,.retain=script_retain,.release=script_release,
        .allocate=script_allocate,.bytes=script_bytes,.free=script_free,.reference=script_reference,
        .resolve=script_resolve,.resolve_history=script_resolve_history};
    return &memory->scripts;
}
