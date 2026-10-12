#include "qa/event_ring.h"

#include <stdlib.h>
#include <string.h>

#define EVENT_PAGE_NONE SIZE_MAX

typedef struct event_page {
    size_t capacity, used, records, next;
    size_t contiguous_end;
    uint8_t *payload;
} event_page;

typedef struct event_storage {
    struct event_storage *next;
    _Alignas(max_align_t) uint8_t payload[];
} event_storage;

struct qa_event_ring {
    event_storage *storage;
    event_page *pages;
    qa_event_lease **records;
    qa_event_lease *first_lease, *last_lease;
    size_t page_bytes, page_count, record_capacity, tail, live_leases;
    size_t payload_bytes, bytes_used, free_bytes, free_pages;
    uint64_t first, next;
    bool retired, writing;
};

struct qa_event_lease {
    qa_event_ring *owner;
    struct qa_event_lease *previous, *next;
    void *record;
    void (*final_record)(void *);
    uint64_t id;
    size_t first_page, last_page, references;
};

static void destroy_storage(qa_event_ring *owner)
{
    while (owner->storage) {
        event_storage *next = owner->storage->next;
        free(owner->storage);
        owner->storage = next;
    }
    free(owner->pages);
    free(owner->records);
    free(owner);
}

size_t qa_event_ring_record_storage_size(size_t payload_bytes)
{
    size_t overhead = sizeof(qa_event_lease) + _Alignof(qa_event_lease) - 1;
    return payload_bytes > SIZE_MAX - overhead ? SIZE_MAX : payload_bytes + overhead;
}

bool qa_event_ring_reserve(qa_event_ring *owner, size_t payload_bytes,
    size_t record_capacity, qa_error *error)
{
    if (!owner || owner->retired) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Event reservation requires its load-owned ring");
        return false;
    }
    bool payload_growth = payload_bytes > owner->payload_bytes;
    bool record_growth = record_capacity > owner->record_capacity;
    if (!payload_growth && !record_growth) return true;
    if (owner->writing) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Event reservation cannot grow an active transaction");
        return false;
    }
    size_t added_pages = payload_growth ? 1 + (payload_bytes - 1) / owner->page_bytes : 0;
    if (payload_bytes > (size_t)PTRDIFF_MAX ||
        payload_bytes > SIZE_MAX - sizeof(event_storage) ||
        (payload_growth && owner->payload_bytes > SIZE_MAX - payload_bytes) ||
        added_pages > SIZE_MAX / sizeof(event_page) - owner->page_count ||
        record_capacity > SIZE_MAX / sizeof(qa_event_lease *)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Event reservation exceeds addressable storage");
        return false;
    }
    size_t page_count = owner->page_count + added_pages;
    event_storage *storage = payload_growth ? malloc(sizeof(*storage) + payload_bytes) : NULL;
    event_page *pages = payload_growth ? calloc(page_count, sizeof(*pages)) : owner->pages;
    qa_event_lease **records = record_growth ? calloc(record_capacity, sizeof(*records)) : owner->records;
    if ((payload_growth && (!storage || !pages)) || (record_growth && !records)) {
        free(storage);
        if (payload_growth) free(pages);
        if (record_growth) free(records);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating load-sized event pages");
        return false;
    }
    if (payload_growth) {
        if (owner->page_count) memcpy(pages, owner->pages, owner->page_count * sizeof(*pages));
        for (size_t i = owner->page_count; i < page_count; ++i) {
            size_t offset = (i - owner->page_count) * owner->page_bytes;
            pages[i] = (event_page){
                .capacity = payload_bytes - offset < owner->page_bytes ? payload_bytes - offset : owner->page_bytes,
                .next = EVENT_PAGE_NONE, .contiguous_end = page_count,
                .payload = storage->payload + offset};
        }
    }
    if (record_growth)
        for (uint64_t id = owner->first; id < owner->next; ++id)
            records[(size_t)((id - 1) % record_capacity)] =
                owner->records[(size_t)((id - 1) % owner->record_capacity)];
    if (payload_growth) {
        storage->next = owner->storage;
        owner->storage = storage;
        free(owner->pages);
        owner->pages = pages;
        owner->page_count = page_count;
        owner->payload_bytes += payload_bytes;
        owner->free_bytes += payload_bytes;
        owner->free_pages += added_pages;
    }
    if (record_growth) {
        free(owner->records);
        owner->records = records;
        owner->record_capacity = record_capacity;
    }
    return true;
}

qa_event_ring *qa_event_ring_create(size_t payload_bytes,
    size_t page_bytes, size_t record_capacity, qa_error *error)
{
    if (payload_bytes < sizeof(qa_event_lease) || !page_bytes || !record_capacity ||
        payload_bytes > (size_t)PTRDIFF_MAX ||
        record_capacity > SIZE_MAX / sizeof(qa_event_lease *)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Event pages require load-sized payload and record storage");
        return NULL;
    }
    qa_event_ring *owner = calloc(1, sizeof(*owner));
    if (!owner) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating event page owner");
        return NULL;
    }
    owner->page_bytes = page_bytes;
    owner->tail = EVENT_PAGE_NONE;
    owner->first = owner->next = 1;
    if (!qa_event_ring_reserve(owner, payload_bytes, record_capacity, error)) {
        destroy_storage(owner);
        return NULL;
    }
    return owner;
}

static qa_event_lease *record_at(const qa_event_ring *owner, uint64_t id)
{
    if (id < owner->first || id >= owner->next) return NULL;
    return owner->records[(size_t)((id - 1) % owner->record_capacity)];
}

uint64_t qa_event_ring_first(const qa_event_ring *owner) { return owner->first; }
uint64_t qa_event_ring_next(const qa_event_ring *owner) { return owner->next; }
const void *qa_event_ring_at(const qa_event_ring *owner, uint64_t id)
{
    const qa_event_lease *lease = record_at(owner, id);
    return lease ? lease->record : NULL;
}

static void reset_page(qa_event_ring *owner, size_t index)
{
    event_page *page = owner->pages + index;
    if (page->used) {
        owner->bytes_used -= page->used;
        owner->free_bytes += page->capacity;
        ++owner->free_pages;
    }
    page->used = page->records = 0;
    page->next = EVENT_PAGE_NONE;
    if (owner->tail == index) owner->tail = EVENT_PAGE_NONE;
}

void qa_event_lease_retain(qa_event_lease *lease) { ++lease->references; }

void qa_event_lease_release(qa_event_lease *lease)
{
    if (--lease->references) return;
    qa_event_ring *owner = lease->owner;
    size_t last = lease->last_page;
    if (lease->previous) lease->previous->next = lease->next;
    else owner->first_lease = lease->next;
    if (lease->next) lease->next->previous = lease->previous;
    else owner->last_lease = lease->previous;
    if (lease->final_record) lease->final_record(lease->record);
    for (size_t index = lease->first_page;;) {
        event_page *page = owner->pages + index;
        size_t next = page->next;
        if (!--page->records) reset_page(owner, index);
        if (index == last) break;
        index = next;
    }
    if (!--owner->live_leases && owner->retired) destroy_storage(owner);
}

qa_event_lease *qa_event_ring_retain(qa_event_ring *owner, uint64_t id)
{
    qa_event_lease *lease = record_at(owner, id);
    if (lease) qa_event_lease_retain(lease);
    return lease;
}

uint64_t qa_event_lease_id(const qa_event_lease *lease) { return lease->id; }
const void *qa_event_lease_record(const qa_event_lease *lease) { return lease->record; }

void qa_event_ring_visit_retained(const qa_event_ring *owner,
    void (*visit)(void *, const void *), void *context)
{
    if (!owner) return;
    for (const qa_event_lease *lease = owner->first_lease; lease; lease = lease->next)
        visit(context, lease->record);
}

void qa_event_ring_retire(qa_event_ring *owner, uint64_t next)
{
    if (next > owner->next) next = owner->next;
    while (owner->first < next) {
        size_t slot = (size_t)((owner->first - 1) % owner->record_capacity);
        qa_event_lease *lease = owner->records[slot];
        owner->records[slot] = NULL;
        ++owner->first;
        qa_event_lease_release(lease);
    }
}

void qa_event_ring_destroy(qa_event_ring **out)
{
    if (!out || !*out) return;
    qa_event_ring *owner = *out;
    *out = NULL;
    qa_event_ring_retire(owner, UINT64_MAX);
    owner->retired = true;
    if (!owner->live_leases) destroy_storage(owner);
}

static void *allocate_run(qa_event_transaction *transaction, size_t size, size_t alignment)
{
    qa_event_ring *owner = transaction->owner;
    for (size_t first = 0; first < owner->page_count;) {
        if (owner->pages[first].used) { ++first; continue; }
        uintptr_t address = (uintptr_t)owner->pages[first].payload;
        size_t padding = (size_t)((-address) & (alignment - 1));
        size_t available = 0, last = first;
        size_t end = owner->pages[first].contiguous_end;
        for (; last < end && !owner->pages[last].used; ++last) {
            available += owner->pages[last].capacity;
            if (padding <= available && size <= available - padding) break;
        }
        if (last == end || owner->pages[last].used) { first = last; continue; }
        size_t remaining = padding + size;
        for (size_t index = first; index <= last; ++index) {
            event_page *page = owner->pages + index;
            page->used = remaining < page->capacity ? remaining : page->capacity;
            owner->bytes_used += page->used;
            owner->free_bytes -= page->capacity;
            --owner->free_pages;
            remaining -= page->used;
            page->next = index == last ? EVENT_PAGE_NONE : index + 1;
        }
        if (owner->tail != EVENT_PAGE_NONE) owner->pages[owner->tail].next = first;
        owner->tail = last;
        if (transaction->first_page == EVENT_PAGE_NONE) transaction->first_page = first;
        transaction->last_page = last;
        return owner->pages[first].payload + padding;
    }
    transaction->blocked = true;
    return NULL;
}

static void *allocate_tail(qa_event_transaction *transaction, size_t size, size_t alignment)
{
    qa_event_ring *owner = transaction->owner;
    size_t first = owner->tail, last = first;
    event_page *page = owner->pages + first;
    uintptr_t address = (uintptr_t)(page->payload + page->used);
    size_t padding = (size_t)((-address) & (alignment - 1));
    size_t available = page->capacity - page->used;
    if (padding >= available) return NULL;
    while ((padding > available || size > available - padding) &&
        last + 1 < page->contiguous_end && !owner->pages[last + 1].used) {
        ++last;
        available += owner->pages[last].capacity;
    }
    if (padding > available || size > available - padding) return NULL;
    size_t remaining = padding + size;
    for (size_t index = first; index <= last; ++index) {
        page = owner->pages + index;
        size_t added = remaining < page->capacity - page->used ? remaining : page->capacity - page->used;
        if (!page->used) {
            owner->free_bytes -= page->capacity;
            --owner->free_pages;
        }
        page->used += added;
        owner->bytes_used += added;
        remaining -= added;
        page->next = index == last ? EVENT_PAGE_NONE : index + 1;
    }
    owner->tail = last;
    if (transaction->first_page == EVENT_PAGE_NONE) transaction->first_page = first;
    transaction->last_page = last;
    return (uint8_t *)address + padding;
}

void *qa_event_ring_alloc(void *context, size_t size, size_t alignment, qa_error *error)
{
    (void)error;
    qa_event_transaction *transaction = context;
    if (!transaction->active || transaction->blocked) return NULL;
    qa_event_ring *owner = transaction->owner;
    if (owner->tail != EVENT_PAGE_NONE) {
        void *out = allocate_tail(transaction, size, alignment);
        if (out) return out;
    }
    return allocate_run(transaction, size, alignment);
}

bool qa_event_ring_begin(qa_event_ring *owner, qa_event_transaction *transaction)
{
    *transaction = (qa_event_transaction){.owner = owner,
        .first_page = EVENT_PAGE_NONE, .last_page = EVENT_PAGE_NONE, .original_tail = owner->tail,
        .original_next = EVENT_PAGE_NONE};
    if (owner->retired || owner->next == UINT64_MAX || owner->next - owner->first == owner->record_capacity) {
        transaction->blocked = true;
        return false;
    }
    if (owner->tail != EVENT_PAGE_NONE) {
        transaction->original_used = owner->pages[owner->tail].used;
        transaction->original_next = owner->pages[owner->tail].next;
    }
    transaction->active = true;
    owner->writing = true;
    transaction->lease = qa_event_ring_alloc(transaction, sizeof(qa_event_lease),
        _Alignof(qa_event_lease), NULL);
    if (transaction->lease) return true;
    qa_event_ring_abort(transaction);
    return false;
}

void qa_event_ring_abort(qa_event_transaction *transaction)
{
    if (!transaction->active) return;
    qa_event_ring *owner = transaction->owner;
    size_t index = transaction->original_tail == EVENT_PAGE_NONE ? transaction->first_page :
        owner->pages[transaction->original_tail].next;
    while (index != EVENT_PAGE_NONE && index != transaction->original_next) {
        size_t next = owner->pages[index].next;
        reset_page(owner, index);
        index = next;
    }
    if (transaction->original_tail != EVENT_PAGE_NONE) {
        event_page *page = owner->pages + transaction->original_tail;
        owner->bytes_used -= page->used - transaction->original_used;
        page->used = transaction->original_used;
        page->next = transaction->original_next;
    }
    owner->tail = transaction->original_tail;
    transaction->active = false;
    owner->writing = false;
}

uint64_t qa_event_ring_commit(qa_event_transaction *transaction, void *record,
    void (*final_record)(void *))
{
    if (!transaction->active) return 0;
    if (transaction->blocked) { qa_event_ring_abort(transaction); return 0; }
    qa_event_ring *owner = transaction->owner;
    uint64_t id = owner->next++;
    *transaction->lease = (qa_event_lease){.owner = owner, .record = record,
        .previous = owner->last_lease,
        .final_record = final_record, .id = id,
        .first_page = transaction->first_page, .last_page = transaction->last_page, .references = 1};
    if (owner->last_lease) owner->last_lease->next = transaction->lease;
    else owner->first_lease = transaction->lease;
    owner->last_lease = transaction->lease;
    for (size_t index = transaction->first_page;; index = owner->pages[index].next) {
        ++owner->pages[index].records;
        if (index == transaction->last_page) break;
    }
    owner->records[(size_t)((id - 1) % owner->record_capacity)] = transaction->lease;
    ++owner->live_leases;
    transaction->active = false;
    owner->writing = false;
    return id;
}

size_t qa_event_ring_bytes_used(const qa_event_ring *owner)
{
    return owner->bytes_used;
}

void qa_event_capacity_read(const qa_event_ring *owner,
    qa_event_capacity *out)
{
    size_t records = (size_t)(owner->next - owner->first);
    size_t tail = owner->tail == EVENT_PAGE_NONE ? 0 :
        owner->pages[owner->tail].capacity - owner->pages[owner->tail].used;
    *out = (qa_event_capacity){.total_bytes = owner->payload_bytes,
        .available_bytes = owner->free_bytes + tail, .free_pages = owner->free_pages,
        .total_records = owner->record_capacity, .available_records = owner->record_capacity - records,
        .retired_leased_records = owner->live_leases - records};
}

size_t qa_event_ring_contiguous_bytes(const qa_event_ring *owner)
{
    size_t largest = 0, run = 0;
    for (size_t i = 0; i < owner->page_count; ++i) {
        if (i && owner->pages[i - 1].contiguous_end == i) run = 0;
        const event_page *page = owner->pages + i;
        if (!page->used) run += page->capacity;
        else run = i == owner->tail ? page->capacity - page->used : 0;
        if (run > largest) largest = run;
    }
    return largest;
}
