#include "event_pages.h"

#include <stdlib.h>

#define EVENT_PAGE_NONE SIZE_MAX

typedef struct event_page {
    size_t capacity, used, records, next;
} event_page;

struct application_event_pages {
    uint8_t *payload;
    event_page *pages;
    application_event_lease **records;
    size_t page_bytes, page_count, record_capacity, tail, live_leases;
    size_t payload_bytes, bytes_used, free_bytes, free_pages;
    uint64_t first, next;
    bool retired;
};

struct application_event_lease {
    application_event_pages *owner;
    void *record;
    uint64_t id;
    size_t first_page, last_page, references;
};

static void destroy_storage(application_event_pages *owner)
{
    free(owner->payload);
    free(owner->pages);
    free(owner->records);
    free(owner);
}

application_event_pages *application_event_pages_create(size_t payload_bytes,
    size_t page_bytes, size_t record_capacity, qa_error *error)
{
    if (payload_bytes < sizeof(application_event_lease) || !page_bytes || !record_capacity ||
        payload_bytes > (size_t)PTRDIFF_MAX ||
        record_capacity > SIZE_MAX / sizeof(application_event_lease *)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Event pages require load-sized payload and record storage");
        return NULL;
    }
    size_t count = 1 + (payload_bytes - 1) / page_bytes;
    if (count > SIZE_MAX / sizeof(event_page)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Event page metadata exceeds addressable storage");
        return NULL;
    }
    application_event_pages *owner = calloc(1, sizeof(*owner));
    if (!owner) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating event page owner");
        return NULL;
    }
    owner->payload = malloc(payload_bytes);
    owner->pages = calloc(count, sizeof(*owner->pages));
    owner->records = calloc(record_capacity, sizeof(*owner->records));
    if (!owner->payload || !owner->pages || !owner->records) {
        destroy_storage(owner);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating load-sized event pages");
        return NULL;
    }
    owner->page_bytes = page_bytes;
    owner->page_count = count;
    owner->record_capacity = record_capacity;
    owner->payload_bytes = owner->free_bytes = payload_bytes;
    owner->free_pages = count;
    owner->tail = EVENT_PAGE_NONE;
    owner->first = owner->next = 1;
    for (size_t i = 0; i < count; ++i) {
        size_t offset = i * page_bytes;
        owner->pages[i].capacity = payload_bytes - offset < page_bytes ? payload_bytes - offset : page_bytes;
        owner->pages[i].next = EVENT_PAGE_NONE;
    }
    return owner;
}

static application_event_lease *record_at(const application_event_pages *owner, uint64_t id)
{
    if (id < owner->first || id >= owner->next) return NULL;
    return owner->records[(size_t)((id - 1) % owner->record_capacity)];
}

uint64_t application_event_pages_first(const application_event_pages *owner) { return owner->first; }
uint64_t application_event_pages_next(const application_event_pages *owner) { return owner->next; }
const void *application_event_pages_at(const application_event_pages *owner, uint64_t id)
{
    const application_event_lease *lease = record_at(owner, id);
    return lease ? lease->record : NULL;
}

static void reset_page(application_event_pages *owner, size_t index)
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

void application_event_lease_retain(application_event_lease *lease) { ++lease->references; }

void application_event_lease_release(application_event_lease *lease)
{
    if (--lease->references) return;
    application_event_pages *owner = lease->owner;
    size_t last = lease->last_page;
    for (size_t index = lease->first_page;;) {
        event_page *page = owner->pages + index;
        size_t next = page->next;
        if (!--page->records) reset_page(owner, index);
        if (index == last) break;
        index = next;
    }
    if (!--owner->live_leases && owner->retired) destroy_storage(owner);
}

application_event_lease *application_event_pages_retain(application_event_pages *owner, uint64_t id)
{
    application_event_lease *lease = record_at(owner, id);
    if (lease) application_event_lease_retain(lease);
    return lease;
}

uint64_t application_event_lease_id(const application_event_lease *lease) { return lease->id; }
const void *application_event_lease_record(const application_event_lease *lease) { return lease->record; }

void application_event_pages_retire(application_event_pages *owner, uint64_t next)
{
    if (next > owner->next) next = owner->next;
    while (owner->first < next) {
        size_t slot = (size_t)((owner->first - 1) % owner->record_capacity);
        application_event_lease *lease = owner->records[slot];
        owner->records[slot] = NULL;
        ++owner->first;
        application_event_lease_release(lease);
    }
}

void application_event_pages_destroy(application_event_pages **out)
{
    if (!out || !*out) return;
    application_event_pages *owner = *out;
    *out = NULL;
    application_event_pages_retire(owner, UINT64_MAX);
    owner->retired = true;
    if (!owner->live_leases) destroy_storage(owner);
}

static void *allocate_run(application_event_transaction *transaction, size_t size, size_t alignment)
{
    application_event_pages *owner = transaction->owner;
    for (size_t first = 0; first < owner->page_count;) {
        if (owner->pages[first].used) { ++first; continue; }
        uintptr_t address = (uintptr_t)(owner->payload + first * owner->page_bytes);
        size_t padding = (size_t)((-address) & (alignment - 1));
        size_t available = 0, last = first;
        for (; last < owner->page_count && !owner->pages[last].used; ++last) {
            available += owner->pages[last].capacity;
            if (padding <= available && size <= available - padding) break;
        }
        if (last == owner->page_count || owner->pages[last].used) { first = last; continue; }
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
        return owner->payload + first * owner->page_bytes + padding;
    }
    transaction->blocked = true;
    return NULL;
}

static void *allocate_tail(application_event_transaction *transaction, size_t size, size_t alignment)
{
    application_event_pages *owner = transaction->owner;
    size_t first = owner->tail, last = first;
    event_page *page = owner->pages + first;
    uintptr_t address = (uintptr_t)(owner->payload + first * owner->page_bytes + page->used);
    size_t padding = (size_t)((-address) & (alignment - 1));
    size_t available = page->capacity - page->used;
    if (padding >= available) return NULL;
    while ((padding > available || size > available - padding) &&
        last + 1 < owner->page_count && !owner->pages[last + 1].used) {
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

void *application_event_pages_alloc(void *context, size_t size, size_t alignment, qa_error *error)
{
    (void)error;
    application_event_transaction *transaction = context;
    if (!transaction->active || transaction->blocked) return NULL;
    application_event_pages *owner = transaction->owner;
    if (owner->tail != EVENT_PAGE_NONE) {
        void *out = allocate_tail(transaction, size, alignment);
        if (out) return out;
    }
    return allocate_run(transaction, size, alignment);
}

bool application_event_pages_begin(application_event_pages *owner, application_event_transaction *transaction)
{
    *transaction = (application_event_transaction){.owner = owner,
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
    transaction->lease = application_event_pages_alloc(transaction, sizeof(application_event_lease),
        _Alignof(application_event_lease), NULL);
    if (transaction->lease) return true;
    application_event_pages_abort(transaction);
    return false;
}

void application_event_pages_abort(application_event_transaction *transaction)
{
    if (!transaction->active) return;
    application_event_pages *owner = transaction->owner;
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
}

uint64_t application_event_pages_commit(application_event_transaction *transaction, void *record)
{
    if (!transaction->active) return 0;
    if (transaction->blocked) { application_event_pages_abort(transaction); return 0; }
    application_event_pages *owner = transaction->owner;
    uint64_t id = owner->next++;
    *transaction->lease = (application_event_lease){.owner = owner, .record = record, .id = id,
        .first_page = transaction->first_page, .last_page = transaction->last_page, .references = 1};
    for (size_t index = transaction->first_page;; index = owner->pages[index].next) {
        ++owner->pages[index].records;
        if (index == transaction->last_page) break;
    }
    owner->records[(size_t)((id - 1) % owner->record_capacity)] = transaction->lease;
    ++owner->live_leases;
    transaction->active = false;
    return id;
}

size_t application_event_pages_bytes_used(const application_event_pages *owner)
{
    return owner->bytes_used;
}

void application_event_pages_capacity_read(const application_event_pages *owner,
    application_event_pages_capacity *out)
{
    size_t records = (size_t)(owner->next - owner->first);
    size_t tail = owner->tail == EVENT_PAGE_NONE ? 0 :
        owner->pages[owner->tail].capacity - owner->pages[owner->tail].used;
    *out = (application_event_pages_capacity){.total_bytes = owner->payload_bytes,
        .available_bytes = owner->free_bytes + tail, .free_pages = owner->free_pages,
        .total_records = owner->record_capacity, .available_records = owner->record_capacity - records,
        .retired_leased_records = owner->live_leases - records};
}

size_t application_event_pages_contiguous_bytes(const application_event_pages *owner)
{
    size_t largest = 0, run = 0;
    for (size_t i = 0; i < owner->page_count; ++i) {
        const event_page *page = owner->pages + i;
        if (!page->used) run += page->capacity;
        else run = i == owner->tail ? page->capacity - page->used : 0;
        if (run > largest) largest = run;
    }
    return largest;
}
