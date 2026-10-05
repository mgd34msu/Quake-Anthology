#include "library_internal.h"
#include "qa/material_save.h"
#include "source_scratch_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct qa_material_order_entry {
    qa_material_order *owner;
    const qa_material *material;
    uint64_t ordinal;
    size_t slot;
    uint32_t rank;
    bool published;
};

struct qa_material_order {
    qa_material_order_entry **entries, **sorted;
    size_t count, capacity, references, capture_depth;
    uint64_t ordinal;
    bool dirty;
    qa_material_order_image_policy *image_policy;
    qa_material_source_scratch *source_queue;
};

static bool fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message); return false;
}

bool qa_material_order_idle(const qa_material_order *order)
{ return order && !order->capture_depth && !order->image_policy; }

bool qa_material_order_capture_begin(const qa_material_order *borrowed, qa_error *error)
{
    qa_material_order *order = (qa_material_order *)borrowed;
    if (!order || order->image_policy || order->capture_depth == SIZE_MAX)
        return fail(error, QA_ERROR_ARGUMENT, "Renderer order capture requires its actual retained owner");
    ++order->capture_depth;
    return true;
}

void qa_material_order_capture_end(const qa_material_order *borrowed)
{
    qa_material_order *order = (qa_material_order *)borrowed;
    if (order && order->capture_depth) --order->capture_depth;
}

qa_material_order *qa_material_order_create(qa_error *error)
{
    qa_material_order *order = calloc(1, sizeof(*order));
    if (!order) { fail(error, QA_ERROR_MEMORY, "allocating renderer material order"); return NULL; }
    order->references = 1; return order;
}

bool qa_material_order_retain(qa_material_order *order, qa_error *error)
{
    if (!order || order->references == SIZE_MAX)
        return fail(error, QA_ERROR_ARGUMENT, "material order is absent or reference count exhausted");
    ++order->references; return true;
}

void qa_material_order_destroy(qa_material_order *order)
{
    if (!qa_material_order_idle(order) || --order->references) return;
    free(order->entries); free(order->sorted); free(order);
}

bool qa_material_order_reserve(qa_material_order *order, const qa_material *material,
                                qa_material_order_entry **out, qa_error *error)
{
    if (!qa_material_order_idle(order) || !material || !out || order->ordinal == UINT64_MAX)
        return fail(error, QA_ERROR_ARGUMENT, "invalid renderer material admission");
    if (order->count == order->capacity) {
        size_t capacity = order->capacity ? order->capacity * 2 : 64;
        if (capacity < order->capacity || capacity > SIZE_MAX / sizeof(*order->entries))
            return fail(error, QA_ERROR_MEMORY, "renderer material count exceeds address space");
        qa_material_order_entry **entries = malloc(capacity * sizeof(*entries));
        qa_material_order_entry **sorted = malloc(capacity * sizeof(*sorted));
        if (!entries || !sorted) {
            free(entries); free(sorted); return fail(error, QA_ERROR_MEMORY, "growing renderer material order");
        }
        for (size_t i = 0; i < order->count; ++i) entries[i] = order->entries[i];
        free(order->entries); free(order->sorted);
        order->entries = entries; order->sorted = sorted; order->capacity = capacity;
    }
    qa_material_order_entry *entry = malloc(sizeof(*entry));
    if (!entry) return fail(error, QA_ERROR_MEMORY, "reserving renderer material identity");
    *entry = (qa_material_order_entry){.owner = order, .material = material,
        .ordinal = order->ordinal++, .slot = order->count};
    order->entries[order->count++] = entry;
    order->dirty = true; *out = entry; return true;
}

bool qa_material_order_publish(qa_material_order_entry *entry, qa_error *error)
{
    if (!entry || !qa_material_order_idle(entry->owner) || !isfinite(entry->material->sort))
        return fail(error, QA_ERROR_FORMAT, "material sort priority is not finite");
    bool inserted = !entry->published;
    entry->published = true; entry->owner->dirty = true;
    if (entry->owner->source_queue) {
        if (!qa_material_order_prepare(entry->owner, error)) return false;
        if (inserted) material_source_sort_inserted(entry->owner->source_queue, entry->rank);
    }
    return true;
}
bool material_source_order_attach(qa_material_order *order, qa_material_source_scratch *source, qa_error *error)
{
    if (!order || !source || (order->source_queue && order->source_queue != source))
        return fail(error, QA_ERROR_ARGUMENT, "Source queue requires its actual renderer material order");
    if (!qa_material_order_prepare(order, error)) return false;
    order->source_queue = source; source->queued_order = order; return true;
}
void material_source_order_detach(qa_material_order *order, qa_material_source_scratch *source)
{
    if (order && order->source_queue == source) order->source_queue = NULL;
    if (source && source->queued_order == order) source->queued_order = NULL;
}

void qa_material_order_remove(qa_material_order_entry *entry)
{
    if (!entry || !qa_material_order_idle(entry->owner)) return;
    qa_material_order *order = entry->owner;
    qa_material_order_entry *last = order->entries[--order->count];
    if (last != entry) { order->entries[entry->slot] = last; last->slot = entry->slot; }
    order->dirty = true; free(entry);
}

void qa_material_order_changed(qa_material_order_entry *entry)
{
    if (entry && qa_material_order_idle(entry->owner)) entry->owner->dirty = true;
}

static int compare(const void *left, const void *right)
{
    const qa_material_order_entry *a = *(qa_material_order_entry *const *)left;
    const qa_material_order_entry *b = *(qa_material_order_entry *const *)right;
    if (a->material->sort < b->material->sort) return -1;
    if (a->material->sort > b->material->sort) return 1;
    return a->ordinal < b->ordinal ? -1 : a->ordinal > b->ordinal;
}

bool qa_material_order_prepare(qa_material_order *order, qa_error *error)
{
    if (!qa_material_order_idle(order)) return fail(error, QA_ERROR_ARGUMENT, "renderer material order is retained by capture");
    if (!order->dirty) return true;
    size_t count = 0;
    for (size_t i = 0; i < order->count; ++i) {
        qa_material_order_entry *entry = order->entries[i];
        if (!entry->published) continue;
        if (!isfinite(entry->material->sort))
            return fail(error, QA_ERROR_FORMAT, "material sort priority is not finite");
        order->sorted[count++] = entry;
    }
    if (count > UINT32_MAX) return fail(error, QA_ERROR_MEMORY, "renderer material ranks exceed capacity");
    if (count > 1) qsort(order->sorted, count, sizeof(*order->sorted), compare);
    for (size_t i = 0; i < count; ++i) order->sorted[i]->rank = (uint32_t)i;
    for (size_t i = count; i < order->count; ++i) order->sorted[i] = NULL;
    order->dirty = false; return true;
}

bool qa_material_order_rank(const qa_material_order *order, const qa_material *material,
                             uint32_t *rank, qa_error *error)
{
    const qa_material_order_entry *entry = material ? material->order_entry : NULL;
    if (!order || !rank || order->dirty || !entry || entry->owner != order ||
        !entry->published || entry->material != material)
        return fail(error, QA_ERROR_ARGUMENT, "source material belongs to another renderer or unpublished order");
    *rank = entry->rank; return true;
}
const qa_material *qa_material_order_sorted_at(const qa_material_order *order, uint32_t rank)
{
    if (!order || order->dirty || rank >= order->count) return NULL;
    const qa_material_order_entry *entry = order->sorted[rank];
    return entry && entry->published && entry->rank == rank ? entry->material : NULL;
}

static int registration_compare(const void *left, const void *right)
{
    const qa_material_order_entry *a = *(qa_material_order_entry *const *)left;
    const qa_material_order_entry *b = *(qa_material_order_entry *const *)right;
    return a->ordinal < b->ordinal ? -1 : a->ordinal > b->ordinal;
}
bool qa_material_order_snapshot(const qa_material_order *order, bool sorted, qa_arena *scratch,
                                const qa_material *const **out, size_t *count, qa_error *error)
{
    if (!order || !scratch || !out || !count || order->count > SIZE_MAX / sizeof(qa_material_order_entry *))
        return fail(error, QA_ERROR_ARGUMENT, "invalid material inventory observation");
    qa_material_order_entry **entries = order->count ? qa_arena_alloc(scratch, order->count * sizeof(*entries), _Alignof(qa_material_order_entry *), error) : NULL;
    const qa_material **rows = order->count ? qa_arena_alloc(scratch, order->count * sizeof(*rows), _Alignof(qa_material *), error) : NULL;
    if (order->count && (!entries || !rows)) return false;
    size_t n = 0;
    for (size_t i = 0; i < order->count; ++i) if (order->entries[i]->published) entries[n++] = order->entries[i];
    if (n > 1) qsort(entries, n, sizeof(*entries), sorted ? compare : registration_compare);
    for (size_t i = 0; i < n; ++i) rows[i] = entries[i]->material;
    *out = rows; *count = n; return true;
}

bool qa_material_order_has_record(const qa_material_order *order, const qa_material *material)
{
    const qa_material_order_entry *entry = material ? material->order_entry : NULL;
    return order && entry && entry->owner == order && entry->material == material &&
        entry->slot < order->count && order->entries[entry->slot] == entry;
}

typedef struct policy_order_child {
    qa_material_order *owner;
    qa_material_order_entry **entries;
    size_t count, capacity;
    uint64_t ordinal;
} policy_order_child;
struct qa_material_order_image_policy {
    qa_material_order *owner;
    qa_material_order_entry **original_entries, **original_sorted;
    size_t count, capacity;
    uint64_t ordinal;
    bool dirty;
    policy_order_child *children;
    size_t child_count;
    qa_material_order_entry **entries, **sorted;
    size_t prepared_count, prepared_capacity;
    bool sealed, published;
};
bool qa_material_order_image_policy_current(const qa_material_order_image_policy *ticket)
{
    if (!ticket || ticket->owner->image_policy != ticket || ticket->owner->capture_depth) return false;
    const qa_material_order *owner = ticket->owner;
    if (!ticket->published && (owner->entries != ticket->original_entries || owner->sorted != ticket->original_sorted ||
        owner->count != ticket->count || owner->capacity != ticket->capacity || owner->ordinal != ticket->ordinal ||
        owner->dirty != ticket->dirty)) return false;
    if (ticket->published && (owner->entries != ticket->entries || owner->sorted != ticket->sorted ||
        owner->count != ticket->prepared_count || owner->capacity != ticket->prepared_capacity ||
        owner->ordinal != ticket->ordinal + ticket->prepared_count - ticket->count ||
        owner->dirty != (ticket->ordinal != 0 || ticket->prepared_count != 0))) return false;
    for (size_t i = 0; i < ticket->child_count; ++i) {
        const policy_order_child *child = ticket->children + i;
        if (child->owner->entries != child->entries || child->owner->capacity != child->capacity ||
            child->owner->ordinal != child->ordinal || child->owner->capture_depth ||
            child->owner->count != (ticket->published ? 0 : child->count)) return false;
    }
    return true;
}
bool qa_material_order_image_policy_prepare(qa_material_order *owner,
    qa_material_order_image_policy **out, qa_error *error)
{
    if (!out || *out || !qa_material_order_idle(owner))
        return fail(error, QA_ERROR_ARGUMENT, "Material order preparation requires its actual idle owner");
    qa_material_order_image_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return fail(error, QA_ERROR_MEMORY, "Retaining prepared material order");
    ticket->owner = owner; ticket->original_entries = owner->entries; ticket->original_sorted = owner->sorted;
    ticket->count = owner->count; ticket->capacity = owner->capacity;
    ticket->ordinal = owner->ordinal; ticket->dirty = owner->dirty;
    owner->image_policy = ticket; *out = ticket; return true;
}
qa_material_order *qa_material_order_image_policy_source(const qa_material_order_image_policy *ticket)
{ return qa_material_order_image_policy_current(ticket) ? ticket->owner : NULL; }
bool qa_material_order_image_policy_associated(const qa_material_order *order)
{ return order && qa_material_order_image_policy_current(order->image_policy); }
bool qa_material_order_image_policy_add(qa_material_order_image_policy *ticket,
    qa_material_order *child, qa_error *error)
{
    if (!qa_material_order_image_policy_current(ticket) || ticket->sealed || ticket->published ||
        !qa_material_order_idle(child) || child == ticket->owner)
        return fail(error, QA_ERROR_ARGUMENT, "Material admission lost its prepared destination order");
    for (size_t i = 0; i < ticket->child_count; ++i)
        if (ticket->children[i].owner == child) return true;
    if (ticket->child_count == SIZE_MAX / sizeof(*ticket->children))
        return fail(error, QA_ERROR_MEMORY, "Prepared material order children exceed address space");
    policy_order_child *children = realloc(ticket->children, (ticket->child_count + 1) * sizeof(*children));
    if (!children) return fail(error, QA_ERROR_MEMORY, "Retaining actual destination material order");
    ticket->children = children;
    children[ticket->child_count++] = (policy_order_child){child, child->entries, child->count, child->capacity, child->ordinal};
    return true;
}
bool qa_material_order_image_policy_ready(qa_material_order_image_policy *ticket, qa_error *error)
{
    if (!qa_material_order_image_policy_current(ticket) || ticket->published)
        return fail(error, QA_ERROR_ARGUMENT, "Prepared material order lost its actual inventories");
    if (ticket->sealed) return qa_material_order_image_policy_ready_is(ticket);
    size_t count = ticket->count;
    for (size_t i = 0; i < ticket->child_count; ++i) {
        if (ticket->children[i].count > SIZE_MAX - count)
            return fail(error, QA_ERROR_MEMORY, "Prepared material order exceeds address space");
        count += ticket->children[i].count;
    }
    if (count > UINT32_MAX || count - ticket->count > UINT64_MAX - ticket->ordinal)
        return fail(error, QA_ERROR_MEMORY, "Prepared material ordinals or ranks exhausted");
    size_t capacity = ticket->capacity;
    if (capacity < count && !capacity) capacity = 64;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2) return fail(error, QA_ERROR_MEMORY, "Prepared material capacity overflows");
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*ticket->entries))
        return fail(error, QA_ERROR_MEMORY, "Prepared material arrays exceed address space");
    qa_material_order_entry **entries = capacity ? malloc(capacity * sizeof(*entries)) : NULL;
    qa_material_order_entry **sorted = capacity ? malloc(capacity * sizeof(*sorted)) : NULL;
    if (capacity && (!entries || !sorted)) {
        free(entries); free(sorted); return fail(error, QA_ERROR_MEMORY, "Preparing renderer material arrays");
    }
    size_t index = 0;
    for (size_t i = 0; i < ticket->count; ++i) entries[index++] = ticket->original_entries[i];
    for (size_t i = 0; i < ticket->child_count; ++i) {
        const policy_order_child *child = ticket->children + i;
        /* Each destination's genuine registration ordinal owns its admission order. */
        size_t begin = index;
        for (size_t j = 0; j < child->count; ++j) entries[index++] = child->entries[j];
        for (size_t j = begin + 1; j < index; ++j) {
            qa_material_order_entry *entry = entries[j]; size_t k = j;
            while (k > begin && entries[k - 1]->ordinal > entry->ordinal) { entries[k] = entries[k - 1]; --k; }
            entries[k] = entry;
        }
    }
    for (size_t i = 0; i < count; ++i)
        if (!entries[i] || (entries[i]->published && !isfinite(entries[i]->material->sort))) {
            free(entries); free(sorted); return fail(error, QA_ERROR_FORMAT, "Prepared material order contains an invalid sort");
        }
    ticket->entries = entries; ticket->sorted = sorted;
    ticket->prepared_count = count; ticket->prepared_capacity = capacity; ticket->sealed = true; return true;
}
bool qa_material_order_image_policy_ready_is(const qa_material_order_image_policy *ticket)
{ return qa_material_order_image_policy_current(ticket) && ticket->sealed && !ticket->published; }
void qa_material_order_image_policy_publish(qa_material_order_image_policy *ticket)
{
    if (!qa_material_order_image_policy_ready_is(ticket)) return;
    for (size_t i = ticket->count; i < ticket->prepared_count; ++i) {
        qa_material_order_entry *entry = ticket->entries[i];
        entry->owner = ticket->owner; entry->slot = i;
        entry->ordinal = ticket->ordinal + i - ticket->count;
    }
    for (size_t i = 0; i < ticket->child_count; ++i) ticket->children[i].owner->count = 0;
    ticket->owner->entries = ticket->entries; ticket->owner->sorted = ticket->sorted;
    ticket->owner->count = ticket->prepared_count; ticket->owner->capacity = ticket->prepared_capacity;
    ticket->owner->ordinal = ticket->ordinal + ticket->prepared_count - ticket->count;
    ticket->owner->dirty = ticket->ordinal != 0 || ticket->prepared_count != 0; ticket->published = true;
}
static bool order_policy_end(qa_material_order_image_policy **address, bool published, qa_error *error)
{
    if (!address || !qa_material_order_image_policy_current(*address) || (*address)->published != published)
        return fail(error, QA_ERROR_ARGUMENT, "Material order cleanup retains its genuine preparation");
    qa_material_order_image_policy *ticket = *address;
    free(published ? ticket->original_entries : ticket->entries);
    free(published ? ticket->original_sorted : ticket->sorted);
    ticket->owner->image_policy = NULL;
    free(ticket->children); free(ticket); *address = NULL; return true;
}
bool qa_material_order_image_policy_finish(qa_material_order_image_policy **ticket, qa_error *error)
{ return order_policy_end(ticket, true, error); }
bool qa_material_order_image_policy_abort(qa_material_order_image_policy **ticket, qa_error *error)
{ return order_policy_end(ticket, false, error); }
