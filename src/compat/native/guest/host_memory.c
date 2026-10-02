#define _GNU_SOURCE
#include "host_memory.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#include <sys/personality.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct host_backing {
    guest_host_backing_view view;
    int descriptor;
    size_t references;
} host_backing;
struct guest_host_memory {
    host_backing *backings; size_t backing_count, backing_capacity;
    qa_native_guest_mapping *mappings; size_t mapping_count, mapping_capacity;
};

static bool fail(qa_error *error, qa_status code, uint64_t where, const char *message)
{ qa_error_set(error,code,(size_t)where,"%s",message); return false; }
static bool grow(void **pointer, size_t *capacity, size_t count, size_t size, qa_error *error)
{
    if (count <= *capacity) return true;
    if (count > SIZE_MAX / size) return fail(error,QA_ERROR_MEMORY,count,"host memory table overflows");
    size_t next = *capacity < 16 ? 16 : *capacity;
    while (next < count) { if (next > SIZE_MAX / 2) { next = count; break; } next *= 2; }
    if (next > SIZE_MAX / size) next = count;
    void *data = realloc(*pointer,next*size);
    if (!data) return fail(error,QA_ERROR_MEMORY,count,"retaining host memory records");
    *pointer = data; *capacity = next; return true;
}
static host_backing *backing_at(const guest_host_memory *memory, uint64_t id)
{
    for (size_t i = 0; memory && i < memory->backing_count; ++i)
        if (memory->backings[i].view.id == id) return memory->backings + i;
    return NULL;
}
static const qa_native_guest_mapping *mapping_at(const guest_host_memory *memory, uint64_t address)
{
    for (size_t i = 0; memory && i < memory->mapping_count; ++i) {
        const qa_native_guest_mapping *mapping = memory->mappings + i;
        if (address >= mapping->base && address - mapping->base < mapping->bytes) return mapping;
    }
    return NULL;
}
static bool extent(uint64_t base, uint64_t bytes)
{ return base && !(base & 4095) && bytes && !(bytes & 4095) && bytes <= SIZE_MAX && bytes <= UINT64_MAX - base; }
static bool file_valid(const guest_host_backing_view *view)
{
    if (!view->file) return true;
    uint64_t remaining = view->source.offset < view->source.bytes ? view->source.bytes - view->source.offset : 0;
    uint64_t accessible = remaining > view->bytes.size ? view->bytes.size : remaining;
    if (accessible & 4095) accessible = (accessible + 4095) & ~UINT64_C(4095);
    if (accessible > view->bytes.size) accessible = view->bytes.size;
    return !(view->source.offset & 4095) && view->source.accessible_bytes == accessible;
}

bool guest_host_memory_create(guest_host_memory **out, qa_error *error)
{
    if (!out || *out) return fail(error,QA_ERROR_ARGUMENT,0,"host memory needs empty owner output");
    guest_host_memory *memory = calloc(1,sizeof(*memory));
    if (!memory) return fail(error,QA_ERROR_MEMORY,0,"creating host backing owner");
    *out = memory; return true;
}

bool guest_host_memory_backing(guest_host_memory *memory, const guest_host_backing_view *view, qa_error *error)
{
    if (!memory || !view || !view->id || !view->bytes.data || !view->bytes.size || (view->bytes.size & 4095) ||
        backing_at(memory,view->id) || !file_valid(view))
        return fail(error,QA_ERROR_ARGUMENT,0,"host backing requires actual unique page snapshot and EOF receipt");
#if defined(__linux__) && defined(__x86_64__)
    if (view->bytes.size > INT64_MAX) return fail(error,QA_ERROR_ARGUMENT,view->id,"host backing exceeds actual file extent");
    if (!grow((void **)&memory->backings,&memory->backing_capacity,memory->backing_count+1,sizeof(*memory->backings),error)) return false;
    size_t physical = view->file ? (size_t)view->source.accessible_bytes : view->bytes.size;
    if (view->file) {
        for (size_t i = physical; i < view->bytes.size; ++i)
            if (view->bytes.data[i]) return fail(error,QA_ERROR_FORMAT,view->id,"file snapshot contains bytes beyond its actual EOF");
    }
    int descriptor = memfd_create("qa-native-backing",MFD_CLOEXEC);
    if (descriptor < 0) return fail(error,QA_ERROR_UNSUPPORTED,view->id,"creating actual host snapshot descriptor failed");
    if (ftruncate(descriptor,(off_t)physical) != 0) { close(descriptor); return fail(error,QA_ERROR_MEMORY,view->id,"sizing actual host snapshot backing failed"); }
    void *storage = mmap(NULL,view->bytes.size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (storage == MAP_FAILED) { close(descriptor); return fail(error,QA_ERROR_MEMORY,view->id,"mapping controller snapshot storage failed"); }
    if (physical && mmap(storage,physical,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,descriptor,0) != storage) {
        munmap(storage,view->bytes.size); close(descriptor);
        return fail(error,QA_ERROR_MEMORY,view->id,"mapping owned controller backing failed");
    }
    memcpy(storage,view->bytes.data,view->bytes.size);
    host_backing record = {.view=*view,.descriptor=descriptor}; record.view.bytes.data = storage;
    memory->backings[memory->backing_count++] = record; return true;
#else
    return fail(error,QA_ERROR_UNSUPPORTED,view->id,"fixed host execution requires Linux x86-64 memory capabilities");
#endif
}

bool guest_host_memory_remove_backing(guest_host_memory *memory, uint64_t id, qa_error *error)
{
    host_backing *backing = backing_at(memory,id);
    if (!backing || backing->references) return fail(error,QA_ERROR_ARGUMENT,id,"host backing is absent or held by physical aliases");
#if defined(__linux__) && defined(__x86_64__)
    if (backing->view.bytes.data) {
        if (munmap((void *)backing->view.bytes.data,backing->view.bytes.size) != 0)
            return fail(error,QA_ERROR_ARGUMENT,id,"retiring actual controller backing failed");
        backing->view.bytes.data=NULL;
    }
    if (backing->descriptor>=0) {
        int result=close(backing->descriptor); backing->descriptor=-1;
        if (result != 0) return fail(error,QA_ERROR_ARGUMENT,id,"closing actual controller backing failed");
    }
#endif
    size_t index = (size_t)(backing-memory->backings);
    memmove(backing,backing+1,(memory->backing_count-index-1)*sizeof(*backing)); --memory->backing_count;
    return true;
}

size_t guest_host_memory_backing_count(const guest_host_memory *memory) { return memory ? memory->backing_count : 0; }
bool guest_host_memory_backing_at(const guest_host_memory *memory, size_t index,
    guest_host_backing_view *out, int *descriptor, qa_error *error)
{
    if (!memory || index >= memory->backing_count || !out || !descriptor || !memory->backings[index].view.bytes.data)
        return fail(error,QA_ERROR_ARGUMENT,index,"host backing row is unavailable");
    *out = memory->backings[index].view; *descriptor = memory->backings[index].descriptor; return true;
}

bool guest_host_memory_map(guest_host_memory *memory, const qa_native_guest_mapping *mapping, qa_error *error)
{
    host_backing *backing = mapping ? backing_at(memory,mapping->backing) : NULL;
    if (!backing || !mapping->id || !extent(mapping->base,mapping->bytes) || mapping->permissions > 7 ||
        (mapping->backing_offset & 4095) || mapping->backing_offset > backing->view.bytes.size ||
        mapping->bytes > backing->view.bytes.size-mapping->backing_offset)
        return fail(error,QA_ERROR_ARGUMENT,0,"host alias requires its actual backing span and unique mapping");
    for (size_t i = 0; i < memory->mapping_count; ++i) {
        const qa_native_guest_mapping *prior = memory->mappings+i;
        if (prior->id == mapping->id || (prior->base < mapping->base+mapping->bytes && mapping->base < prior->base+prior->bytes))
            return fail(error,QA_ERROR_ARGUMENT,mapping->base,"host physical mappings overlap or repeat");
    }
    if (!grow((void **)&memory->mappings,&memory->mapping_capacity,memory->mapping_count+1,sizeof(*mapping),error)) return false;
    memory->mappings[memory->mapping_count++] = *mapping; ++backing->references; return true;
}

bool guest_host_memory_change(guest_host_memory *memory, const qa_native_guest_mapping *mapping,
    uint32_t permissions, bool remove, qa_error *error)
{
    if (!memory || !mapping || permissions > 7) return fail(error,QA_ERROR_ARGUMENT,0,"host mapping change requires its physical row");
    for (size_t i = 0; i < memory->mapping_count; ++i) {
        qa_native_guest_mapping *actual = memory->mappings+i;
        if (actual->id != mapping->id) continue;
        if (actual->base != mapping->base || actual->bytes != mapping->bytes || actual->backing != mapping->backing ||
            actual->backing_offset != mapping->backing_offset || actual->permissions != mapping->permissions)
            return fail(error,QA_ERROR_ARGUMENT,mapping->id,"host mapping receipt was replaced");
        if (remove) {
            --backing_at(memory,actual->backing)->references;
            memmove(actual,actual+1,(memory->mapping_count-i-1)*sizeof(*actual)); --memory->mapping_count;
        } else actual->permissions = permissions;
        return true;
    }
    return fail(error,QA_ERROR_NOT_FOUND,mapping->id,"host mapping identity is absent");
}

size_t guest_host_memory_mapping_count(const guest_host_memory *memory) { return memory ? memory->mapping_count : 0; }
bool guest_host_memory_mapping_at(const guest_host_memory *memory, size_t index,
    qa_native_guest_mapping *out, qa_error *error)
{
    if (!memory || index >= memory->mapping_count || !out) return fail(error,QA_ERROR_ARGUMENT,index,"host mapping row is absent");
    *out = memory->mappings[index]; return true;
}

static bool range(const guest_host_memory *memory, uint64_t address, size_t bytes,
    uint32_t access, qa_error *error)
{
    if (!memory || !address || bytes > UINT64_MAX-address) return fail(error,QA_ERROR_ARGUMENT,address,"host guest span exceeds its address space");
    for (size_t offset = 0; offset < bytes; ) {
        const qa_native_guest_mapping *mapping = mapping_at(memory,address+offset);
        if (!mapping || (mapping->permissions & access) != access)
            return fail(error,QA_ERROR_ARGUMENT,address+offset,"host guest mapping denies actual access");
        host_backing *backing = backing_at(memory,mapping->backing);
        uint64_t displacement = address+offset-mapping->base, physical = mapping->backing_offset+displacement;
        size_t amount = bytes-offset;
        if (amount > mapping->bytes-displacement) amount = (size_t)(mapping->bytes-displacement);
        if (backing->view.file && (physical >= backing->view.source.accessible_bytes || amount > backing->view.source.accessible_bytes-physical))
            return fail(error,QA_ERROR_FORMAT,address+offset,"host file mapping access crosses actual EOF pages");
        offset += amount;
    }
    return true;
}

bool guest_host_memory_check(const guest_host_memory *memory,uint64_t address,size_t bytes,uint32_t access,qa_error *error)
{ return access<=7 && range(memory,address,bytes,access,error); }

static void copy_range(const guest_host_memory *memory, uint64_t address, void *data, size_t bytes, bool write)
{
    for (size_t offset = 0; offset < bytes; ) {
        const qa_native_guest_mapping *mapping = mapping_at(memory,address+offset);
        host_backing *backing = backing_at(memory,mapping->backing);
        uint64_t displacement = address+offset-mapping->base;
        size_t amount = bytes-offset;
        if (amount > mapping->bytes-displacement) amount = (size_t)(mapping->bytes-displacement);
        void *physical = (uint8_t *)backing->view.bytes.data+mapping->backing_offset+displacement;
        if (write) memcpy(physical,(uint8_t *)data+offset,amount); else memcpy((uint8_t *)data+offset,physical,amount);
        offset += amount;
    }
}
bool guest_host_memory_read(const guest_host_memory *memory, uint64_t address, void *data, size_t bytes, qa_error *error)
{
    if ((!data && bytes) || !range(memory,address,bytes,QA_NATIVE_GUEST_READ,error)) return false;
    copy_range(memory,address,data,bytes,false); return true;
}
bool guest_host_memory_write(guest_host_memory *memory, uint64_t address, qa_bytes bytes, qa_error *error)
{
    if ((!bytes.data && bytes.size) || !range(memory,address,bytes.size,QA_NATIVE_GUEST_WRITE,error)) return false;
    uint8_t *copy = bytes.size ? malloc(bytes.size) : NULL;
    if (bytes.size && !copy) return fail(error,QA_ERROR_MEMORY,address,"retaining aliased host write input");
    if (bytes.size) memcpy(copy,bytes.data,bytes.size);
    copy_range(memory,address,copy,bytes.size,true); free(copy); return true;
}
bool guest_host_memory_fault(const guest_host_memory *memory, uint64_t address, uint32_t access,
    qa_native_guest_fault *out, qa_error *error)
{
    if (!memory || !out || !(access == 1 || access == 2 || access == 4)) return fail(error,QA_ERROR_ARGUMENT,address,"host fault requires actual access and memory owner");
    const qa_native_guest_mapping *mapping = mapping_at(memory,address);
    *out = (qa_native_guest_fault){.kind=QA_NATIVE_GUEST_FAULT_UNMAPPED,.access=access,.address=address};
    if (mapping) {
        host_backing *backing = backing_at(memory,mapping->backing);
        out->backing = mapping->backing; out->backing_offset = mapping->backing_offset+address-mapping->base;
        out->kind = backing->view.file && out->backing_offset >= backing->view.source.accessible_bytes &&
            (mapping->permissions & access) == access ? QA_NATIVE_GUEST_FAULT_FILE_EOF : QA_NATIVE_GUEST_FAULT_PROTECTION;
    }
    return true;
}
bool guest_host_memory_destroy(guest_host_memory **pointer, qa_error *error)
{
    if (!pointer || !*pointer) return true;
    guest_host_memory *memory = *pointer;
#if defined(__linux__) && defined(__x86_64__)
    for (size_t i = 0; i < memory->backing_count; ++i) {
        host_backing *backing=memory->backings+i;
        if(backing->view.bytes.data) {
            if(munmap((void *)backing->view.bytes.data,backing->view.bytes.size)!=0)
                return fail(error,QA_ERROR_ARGUMENT,backing->view.id,"retiring owned controller memory failed");
            backing->view.bytes.data=NULL;
        }
        if(backing->descriptor>=0) {
            int result=close(backing->descriptor);backing->descriptor=-1;
            if(result!=0)return fail(error,QA_ERROR_ARGUMENT,backing->view.id,"closing owned controller backing failed");
        }
    }
#endif
    free(memory->backings); free(memory->mappings); free(memory); *pointer = NULL; return true;
}

bool guest_host_memory_child_backing(int descriptor, const guest_host_backing_view *view, qa_error *error)
{
#if defined(__linux__) && defined(__x86_64__)
    struct stat actual;
    uint64_t bytes = view && view->file ? view->source.accessible_bytes : view ? view->bytes.size : 0;
    return (descriptor >= 0 && view && view->id && file_valid(view) && fstat(descriptor,&actual) == 0 &&
        actual.st_size >= 0 && (uint64_t)actual.st_size == bytes) ||
        fail(error,QA_ERROR_ARGUMENT,0,"child descriptor differs from actual snapshot extent");
#else
    (void)descriptor; (void)view; return fail(error,QA_ERROR_UNSUPPORTED,0,"host child mapping platform is unavailable");
#endif
}
bool guest_host_memory_child_map(int descriptor, const qa_native_guest_mapping *mapping, qa_error *error)
{
#if defined(__linux__) && defined(__x86_64__)
    if (!mapping || !mapping->id || !mapping->backing || !extent(mapping->base,mapping->bytes) || mapping->permissions > 7 ||
        mapping->backing_offset > INT64_MAX || (mapping->backing_offset & 4095))
        return fail(error,QA_ERROR_ARGUMENT,0,"child fixed mapping has invalid actual bounds");
    if((mapping->permissions & 6) && !(mapping->permissions & 1))
        return fail(error,QA_ERROR_UNSUPPORTED,mapping->base,"host x86 cannot enforce guest write-only or execute-only page rights");
    int persona=personality(0xffffffffUL);
    if(persona<0 || (persona&READ_IMPLIES_EXEC))
        return fail(error,QA_ERROR_UNSUPPORTED,mapping->base,"actual host personality cannot preserve guest execute denial");
    int rights = ((mapping->permissions&1)?PROT_READ:0)|((mapping->permissions&2)?PROT_WRITE:0)|((mapping->permissions&4)?PROT_EXEC:0);
    void *address = mmap((void *)(uintptr_t)mapping->base,(size_t)mapping->bytes,rights,
        MAP_SHARED|MAP_FIXED_NOREPLACE,descriptor,(off_t)mapping->backing_offset);
    if (address == MAP_FAILED) return fail(error,QA_ERROR_ARGUMENT,mapping->base,"child fixed address is occupied or unavailable");
    if ((uint64_t)(uintptr_t)address != mapping->base) { munmap(address,(size_t)mapping->bytes); return fail(error,QA_ERROR_UNSUPPORTED,mapping->base,"kernel did not honor fixed no-replacement mappings"); }
    return true;
#else
    (void)descriptor; (void)mapping; return fail(error,QA_ERROR_UNSUPPORTED,0,"host child mapping platform is unavailable");
#endif
}
bool guest_host_memory_child_change(const qa_native_guest_mapping *mapping, uint32_t permissions, bool remove, qa_error *error)
{
#if defined(__linux__) && defined(__x86_64__)
    if (!mapping || !extent(mapping->base,mapping->bytes) || permissions > 7) return fail(error,QA_ERROR_ARGUMENT,0,"child mapping mutation has invalid bounds");
    if(!remove && (permissions & 6) && !(permissions & 1))
        return fail(error,QA_ERROR_UNSUPPORTED,mapping->base,"host x86 cannot enforce guest write-only or execute-only page rights");
    int rights = ((permissions&1)?PROT_READ:0)|((permissions&2)?PROT_WRITE:0)|((permissions&4)?PROT_EXEC:0);
    int result = remove ? munmap((void *)(uintptr_t)mapping->base,(size_t)mapping->bytes) :
        mprotect((void *)(uintptr_t)mapping->base,(size_t)mapping->bytes,rights);
    return result == 0 || fail(error,QA_ERROR_ARGUMENT,mapping->base,"child physical mapping mutation failed");
#else
    (void)mapping; (void)permissions; (void)remove; return fail(error,QA_ERROR_UNSUPPORTED,0,"host child mapping platform is unavailable");
#endif
}
