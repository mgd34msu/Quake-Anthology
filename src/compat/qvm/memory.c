#include "internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct qa_qvm_write_watch {
    struct qa_qvm_write_watch *next;
    qa_qvm_binding id;
    qa_qvm_write_range *ranges;
    size_t count;
    qa_qvm_write_observer publish, after;
    void *context;
    bool active;
} qa_qvm_write_watch;
typedef struct write_delivery {
    struct write_delivery *next;
    qa_qvm_write_watch *watch;
    qa_qvm_committed_write event;
    qa_qvm_committed_range *ranges;
    uint8_t *bytes;
} write_delivery;

bool qa_qvm_live(const qa_qvm *vm, qa_error *error)
{
    return (vm != NULL && !vm->retired) || qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"QVM instance is retired or absent");
}
bool qa_qvm_mutable(const qa_qvm *vm, qa_error *error)
{
    if (!qa_qvm_live(vm,error)) return false;
    return vm->publication_depth == 0 || qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"QVM publication permits bookkeeping only");
}
bool qa_qvm_raw_range(const qa_qvm *vm, uint32_t offset, size_t length, qa_error *error)
{
    if (!qa_qvm_live(vm,error)) return false;
    return (offset <= vm->data_size && length <= vm->data_size - offset)
        || qa_qvm_error(error,QA_ERROR_ARGUMENT,offset,"QVM memory range exceeds allocation");
}

uint32_t qa_qvm_mask_address(const qa_qvm *vm, int32_t address)
{
    return vm == NULL ? 0 : (uint32_t)address & vm->data_mask;
}
size_t qa_qvm_memory_size(const qa_qvm *vm) { return vm == NULL ? 0 : vm->data_size; }

bool qa_qvm_read(const qa_qvm *vm, uint32_t offset, void *out, size_t length, qa_error *error)
{
    if (length > 0 && out == NULL) return qa_qvm_error(error,QA_ERROR_ARGUMENT,offset,"missing QVM read destination");
    if (!qa_qvm_raw_range(vm,offset,length,error)) return false;
    if (length > 0) memmove(out,vm->data + offset,length);
    return true;
}

bool qa_qvm_span(const qa_qvm *vm, int32_t pointer, int64_t relative, size_t length, qa_bytes *out, qa_error *error)
{
    if (out == NULL || pointer == 0) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"QVM span requires a nonnull pointer");
    if (!qa_qvm_live(vm,error)) return false;
    uint32_t base = qa_qvm_mask_address(vm,pointer);
    if (relative < -(int64_t)base || relative > (int64_t)vm->data_size - base)
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,base,"QVM relative pointer exceeds allocation");
    uint32_t offset = (uint32_t)((int64_t)base + relative);
    if (!qa_qvm_raw_range(vm,offset,length,error)) return false;
    *out = (qa_bytes){vm->data + offset,length};
    return true;
}

bool qa_qvm_read_string(const qa_qvm *vm, int32_t pointer, qa_bytes *out, qa_error *error)
{
    qa_bytes start;
    if (out == NULL) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"missing QVM string output");
    if (!qa_qvm_span(vm,pointer,0,0,&start,error)) return false;
    size_t offset = (size_t)(start.data - vm->data), available = vm->data_size - offset;
    const uint8_t *end = memchr(start.data,0,available);
    if (end == NULL) return qa_qvm_error(error,QA_ERROR_FORMAT,offset,"unterminated QVM string");
    *out = (qa_bytes){start.data,(size_t)(end - start.data)};
    return true;
}

static int range_order(const void *left, const void *right)
{
    const qa_qvm_write_range *a = left, *b = right;
    return a->offset < b->offset ? -1 : a->offset > b->offset ? 1 : 0;
}

static void collect_watches(qa_qvm *vm)
{
    if (vm->write_delivery_depth != 0) return;
    qa_qvm_write_watch **link = &vm->watches;
    while (*link != NULL) {
        qa_qvm_write_watch *watch = *link;
        if (watch->active) { link = &watch->next; continue; }
        *link = watch->next;
        free(watch->ranges); free(watch);
    }
}

bool qa_qvm_observe_writes(qa_qvm *vm, const qa_qvm_write_range *ranges, size_t count,
                          qa_qvm_write_observer publish, qa_qvm_write_observer after,
                          void *context, qa_qvm_binding *out, qa_error *error)
{
    if (!qa_qvm_live(vm,error)) return false;
    if (publish == NULL || out == NULL || (count > 0 && ranges == NULL) || count > SIZE_MAX / sizeof(*ranges))
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM write observer");
    if (vm->next_watch == UINT64_MAX) return qa_qvm_error(error,QA_ERROR_MEMORY,0,"QVM observer identity exhausted");
    qa_qvm_write_watch *watch = calloc(1,sizeof(*watch));
    if (watch == NULL) return qa_qvm_error(error,QA_ERROR_MEMORY,0,"allocating QVM write observer");
    if (count > 0) watch->ranges = malloc(count * sizeof(*ranges));
    if (count > 0 && watch->ranges == NULL) { free(watch); return qa_qvm_error(error,QA_ERROR_MEMORY,0,"allocating QVM observer ranges"); }
    for (size_t i = 0; i < count; ++i) {
        if (!qa_qvm_raw_range(vm,ranges[i].offset,ranges[i].length,error)) { free(watch->ranges); free(watch); return false; }
        if (ranges[i].length > 0) watch->ranges[watch->count++] = ranges[i];
    }
    if (watch->count > 1) qsort(watch->ranges,watch->count,sizeof(*ranges),range_order);
    size_t merged = 0;
    for (size_t i = 0; i < watch->count; ++i) {
        qa_qvm_write_range range = watch->ranges[i];
        if (merged > 0) {
            qa_qvm_write_range *previous = &watch->ranges[merged - 1];
            size_t end = (size_t)previous->offset + previous->length;
            if (end >= range.offset) {
                size_t range_end = (size_t)range.offset + range.length;
                if (range_end > end) previous->length = range_end - previous->offset;
                continue;
            }
        }
        watch->ranges[merged++] = range;
    }
    watch->count = merged; watch->publish = publish; watch->after = after; watch->context = context;
    watch->active = true; watch->id = ++vm->next_watch;
    qa_qvm_write_watch **tail = &vm->watches;
    while (*tail != NULL) tail = &(*tail)->next;
    *tail = watch; *out = watch->id;
    return true;
}

bool qa_qvm_unobserve_writes(qa_qvm *vm, qa_qvm_binding id, qa_error *error)
{
    if (!qa_qvm_live(vm,error)) return false;
    for (qa_qvm_write_watch *watch = vm->watches; watch != NULL; watch = watch->next)
        if (watch->id == id && watch->active) { watch->active = false; collect_watches(vm); return true; }
    return qa_qvm_error(error,QA_ERROR_NOT_FOUND,0,"QVM write observer not found");
}

static void free_deliveries(write_delivery *head)
{
    while (head != NULL) {
        write_delivery *next = head->next;
        free(head->bytes); free(head->ranges); free(head);
        head = next;
    }
}

static bool capture_writes(qa_qvm *vm, uint32_t offset, size_t length, write_delivery **out, qa_error *error)
{
    write_delivery *head = NULL, **tail = &head;
    size_t end = (size_t)offset + length;
    for (qa_qvm_write_watch *watch = vm->watches; watch != NULL; watch = watch->next) {
        if (!watch->active) continue;
        size_t count = 0, bytes = 0;
        for (size_t i = 0; i < watch->count; ++i) {
            size_t start = offset > watch->ranges[i].offset ? offset : watch->ranges[i].offset;
            size_t limit = (size_t)watch->ranges[i].offset + watch->ranges[i].length;
            if (end < limit) limit = end;
            if (start < limit) { ++count; bytes += limit - start; }
        }
        if (count == 0) continue;
        if (count > SIZE_MAX / sizeof(qa_qvm_committed_range) || bytes > SIZE_MAX / 2) {
            free_deliveries(head); return qa_qvm_error(error,QA_ERROR_MEMORY,offset,"QVM committed event is too large");
        }
        write_delivery *delivery = calloc(1,sizeof(*delivery));
        if (delivery == NULL) { free_deliveries(head); return qa_qvm_error(error,QA_ERROR_MEMORY,offset,"allocating QVM committed event"); }
        *tail = delivery; tail = &delivery->next;
        delivery->ranges = calloc(count,sizeof(*delivery->ranges));
        delivery->bytes = malloc(bytes * 2);
        if (delivery->ranges == NULL || delivery->bytes == NULL) {
            free_deliveries(head); return qa_qvm_error(error,QA_ERROR_MEMORY,offset,"allocating QVM committed bytes");
        }
        delivery->watch = watch;
        delivery->event.ranges = delivery->ranges;
        size_t cursor = 0;
        for (size_t i = 0; i < watch->count; ++i) {
            size_t start = offset > watch->ranges[i].offset ? offset : watch->ranges[i].offset;
            size_t limit = (size_t)watch->ranges[i].offset + watch->ranges[i].length;
            if (end < limit) limit = end;
            if (start >= limit) continue;
            size_t size = limit - start;
            qa_qvm_committed_range *range = &delivery->ranges[delivery->event.count++];
            *range = (qa_qvm_committed_range){(uint32_t)start,{delivery->bytes + cursor,size},{delivery->bytes + bytes + cursor,size}};
            memcpy(delivery->bytes + cursor,vm->data + start,size);
            cursor += size;
        }
    }
    if (head && vm->write_sequence == UINT64_MAX) {
        free_deliveries(head);
        return qa_qvm_error(error,QA_ERROR_MEMORY,offset,"QVM observed write identity exhausted");
    }
    *out = head;
    return true;
}

typedef struct after_effect { qa_qvm *vm; write_delivery *delivery; } after_effect;
static bool after_publication(void *context, qa_error *error)
{
    after_effect *effect = context;
    qa_qvm_write_watch *watch = effect->delivery->watch;
    return watch->after(watch->context,effect->vm,&effect->delivery->event,error);
}

static bool publish_writes(qa_qvm *vm, write_delivery *head, qa_error *error)
{
    if (head == NULL) return true;
    uint64_t sequence = ++vm->write_sequence;
    ++vm->write_delivery_depth;
    for (write_delivery *delivery = head; delivery != NULL; delivery = delivery->next) {
        delivery->event.sequence = sequence;
        for (size_t i = 0; i < delivery->event.count; ++i) {
            qa_qvm_committed_range *range = &delivery->ranges[i];
            memcpy((uint8_t *)range->after.data,vm->data + range->offset,range->after.size);
        }
    }
    bool success = true;
    qa_error first = {0};
    ++vm->publication_depth;
    for (write_delivery *delivery = head; delivery != NULL; delivery = delivery->next) {
        qa_qvm_write_watch *watch = delivery->watch;
        if (!watch->active) continue;
        qa_error local = {0};
        if (!watch->publish(watch->context,vm,&delivery->event,&local)) { if (success) first = local; success = false; }
    }
    --vm->publication_depth;
    if (success) for (write_delivery *delivery = head; delivery != NULL; delivery = delivery->next) {
        qa_qvm_write_watch *watch = delivery->watch;
        if (!watch->active || watch->after == NULL || vm->retired) continue;
        after_effect effect = {vm,delivery};
        if (!qa_qvm_execution_effect(vm,after_publication,&effect,&first)) { success = false; break; }
    }
    --vm->write_delivery_depth;
    free_deliveries(head); collect_watches(vm);
    if (!success && error != NULL) *error = first;
    return success;
}

typedef enum memory_operation { MEMORY_WRITE, MEMORY_FILL, MEMORY_COPY } memory_operation;
static bool mutate(qa_qvm *vm, uint32_t offset, size_t length, memory_operation operation,
                    const uint8_t *source, uint8_t value, qa_error *error)
{
    if (!qa_qvm_mutable(vm,error) || !qa_qvm_raw_range(vm,offset,length,error)) return false;
    write_delivery *deliveries;
    if (!capture_writes(vm,offset,length,&deliveries,error)) return false;
    if (length > 0) {
        if (operation == MEMORY_FILL) memset(vm->data + offset,value,length);
        else memmove(vm->data + offset,source,length);
    }
    return publish_writes(vm,deliveries,error);
}

bool qa_qvm_write(qa_qvm *vm, uint32_t offset, qa_bytes bytes, qa_error *error)
{
    if (bytes.size > 0 && bytes.data == NULL) return qa_qvm_error(error,QA_ERROR_ARGUMENT,offset,"missing QVM write source");
    return mutate(vm,offset,bytes.size,MEMORY_WRITE,bytes.data,0,error);
}

bool qa_qvm_memory_restore_scratch(qa_qvm *vm, uint32_t offset, qa_bytes bytes, qa_error *error)
{
    if ((bytes.size && !bytes.data) || !qa_qvm_mutable(vm, error) ||
        !qa_qvm_raw_range(vm, offset, bytes.size, error)) return false;
    write_delivery *deliveries = NULL;
    bool captured = capture_writes(vm, offset, bytes.size, &deliveries, error);
    if (bytes.size) memmove(vm->data + offset, bytes.data, bytes.size);
    if (!captured) return false;
    return publish_writes(vm, deliveries, error);
}
bool qa_qvm_memory_restore_words(qa_qvm *vm, const qa_qvm_source_word *words,
    size_t count, bool observed, qa_error *error)
{
    if (!qa_qvm_mutable(vm, error) || (count && !words)) return false;
    for (size_t i = 0; i < count; ++i)
        if (!qa_qvm_raw_range(vm, words[i].offset, 4, error)) return false;
    bool ok = true; qa_error first = {0};
    for (size_t i = 0; i < count; ++i) {
        if (!observed) qa_store_u32le(vm->data + words[i].offset, (uint32_t)words[i].value);
        else {
            uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)words[i].value);
            qa_error publication = {0};
            if (!qa_qvm_memory_restore_scratch(vm, words[i].offset,
                    (qa_bytes){bytes, sizeof(bytes)}, &publication)) {
                if (ok) first = publication;
                ok = false;
            }
        }
    }
    if (!ok && error) *error = first;
    return ok;
}
bool qa_qvm_fill(qa_qvm *vm, uint32_t offset, size_t length, uint8_t value, qa_error *error)
{
    return mutate(vm,offset,length,MEMORY_FILL,NULL,value,error);
}
bool qa_qvm_copy(qa_qvm *vm, uint32_t destination, uint32_t source, size_t length, qa_error *error)
{
    if (!qa_qvm_raw_range(vm,source,length,error)) return false;
    return mutate(vm,destination,length,MEMORY_COPY,vm->data + source,0,error);
}
bool qa_qvm_write_string(qa_qvm *vm, int32_t pointer, qa_bytes string, size_t capacity, qa_error *error)
{
    if (capacity < 1 || (string.size > 0 && string.data == NULL))
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM string write");
    qa_bytes destination;
    if (!qa_qvm_span(vm,pointer,0,capacity,&destination,error) || !qa_qvm_mutable(vm,error)) return false;
    uint32_t offset = (uint32_t)(destination.data - vm->data);
    size_t copied = string.size < capacity - 1 ? string.size : capacity - 1;
    const uint8_t *zero = copied > 0 ? memchr(string.data,0,copied) : NULL;
    if (zero != NULL) copied = (size_t)(zero - string.data);
    write_delivery *deliveries;
    if (!capture_writes(vm,offset,capacity,&deliveries,error)) return false;
    if (copied > 0) memmove(vm->data + offset,string.data,copied);
    memset(vm->data + offset + copied,0,capacity - copied);
    return publish_writes(vm,deliveries,error);
}

void qa_qvm_memory_close(qa_qvm *vm)
{
    qa_qvm_write_watch *watch = vm->watches;
    while (watch != NULL) {
        qa_qvm_write_watch *next = watch->next;
        free(watch->ranges); free(watch); watch = next;
    }
    vm->watches = NULL;
}
