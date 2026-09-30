#include "library_internal.h"

#include <math.h>
#include <stdlib.h>

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
    size_t count, capacity, references;
    uint64_t ordinal;
    bool dirty;
};

static bool fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message); return false;
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
    if (!order || --order->references) return;
    free(order->entries); free(order->sorted); free(order);
}

bool qa_material_order_reserve(qa_material_order *order, const qa_material *material,
                                qa_material_order_entry **out, qa_error *error)
{
    if (!order || !material || !out || order->ordinal == UINT64_MAX)
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
    if (!entry || !isfinite(entry->material->sort))
        return fail(error, QA_ERROR_FORMAT, "material sort priority is not finite");
    entry->published = true; entry->owner->dirty = true; return true;
}

void qa_material_order_remove(qa_material_order_entry *entry)
{
    if (!entry) return;
    qa_material_order *order = entry->owner;
    qa_material_order_entry *last = order->entries[--order->count];
    if (last != entry) { order->entries[entry->slot] = last; last->slot = entry->slot; }
    order->dirty = true; free(entry);
}

void qa_material_order_changed(qa_material_order_entry *entry)
{
    if (entry) entry->owner->dirty = true;
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
    if (!order) return fail(error, QA_ERROR_ARGUMENT, "renderer material order is absent");
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
