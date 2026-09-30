#include "library_internal.h"
#include "library_save_private.h"
#include "qa/material_save.h"
#include "qa/source_save.h"

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

static bool order_signature(qa_source_save_io *io)
{
    uint8_t magic[4] = {'Q', 'A', 'M', 'O'}; uint32_t version = 1;
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QAMO", 4) &&
        qa_source_save_u32(io, &version) && version == 1;
}
bool qa_material_order_has_record(const qa_material_order *order, const qa_material *material)
{
    const qa_material_order_entry *entry = material ? material->order_entry : NULL;
    return order && entry && entry->owner == order && entry->material == material &&
        entry->slot < order->count && order->entries[entry->slot] == entry;
}

bool qa_material_order_checkpoint(const qa_material_order *order, const qa_material_checkpoint_refs *refs,
                                   qa_buffer *out, qa_error *error)
{
    if (!order || !refs || !refs->material_encode || !out)
        return fail(error, QA_ERROR_ARGUMENT, "material order checkpoint requires qualified records");
    qa_source_save_io io = {0}; size_t count = order->count;
    uint64_t ordinal = order->ordinal; bool dirty = order->dirty;
    bool ok = qa_source_save_writer(&io, NULL, error) && order_signature(&io) &&
        qa_source_save_count(&io, &count, SIZE_MAX) && qa_source_save_u64(&io, &ordinal) &&
        qa_source_save_bool(&io, &dirty);
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_material_order_entry *entry = order->entries[i]; uint64_t key = 0;
        ok = entry && entry->owner == order && entry->slot == i && entry->material &&
            entry->material->order_entry == entry && entry->ordinal < ordinal &&
            (!entry->published || isfinite(entry->material->sort)) && refs->material_encode(refs->context, entry->material, &key, error);
        if (!ok) break;
        uint64_t sequence = entry->ordinal; uint32_t rank = entry->rank; bool published = entry->published;
        ok = qa_source_save_u64(&io, &key) && qa_source_save_u64(&io, &sequence) &&
            qa_source_save_u32(&io, &rank) && qa_source_save_bool(&io, &published);
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "invalid renderer material order");
    qa_source_save_dispose(&io); return ok;
}

bool qa_material_order_restore(qa_bytes bytes, const qa_material_checkpoint_refs *refs,
                                qa_material_order **out, qa_error *error)
{
    if (!out || *out || !refs || !refs->material_decode)
        return fail(error, QA_ERROR_ARGUMENT, "material order restore requires detached qualified records");
    *out = NULL;
    qa_source_save_io io = {0}; size_t count = 0; uint64_t ordinal = 0; bool dirty = false;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && order_signature(&io) &&
        qa_source_save_count(&io, &count, bytes.size / 21) && qa_source_save_u64(&io, &ordinal) &&
        qa_source_save_bool(&io, &dirty) && count <= SIZE_MAX / sizeof(qa_material_order_entry *);
    qa_material_order *order = ok ? qa_material_order_create(error) : NULL;
    if (ok && !order) ok = false;
    if (ok && count) {
        order->entries = calloc(count, sizeof(*order->entries));
        order->sorted = malloc(count * sizeof(*order->sorted));
        if (!order->entries || !order->sorted) ok = fail(error, QA_ERROR_MEMORY, "allocating restored material order");
    }
    if (order) order->capacity = count, order->ordinal = ordinal, order->dirty = dirty;
    for (size_t i = 0; ok && i < count; ++i) {
        uint64_t key = 0, sequence = 0; uint32_t rank = 0; bool published = false; qa_material *material = NULL;
        ok = qa_source_save_u64(&io, &key) && qa_source_save_u64(&io, &sequence) && sequence < ordinal &&
            qa_source_save_u32(&io, &rank) && qa_source_save_bool(&io, &published) &&
            refs->material_decode(refs->context, key, &material, error) && material && !material->order_entry &&
            (!published || isfinite(material->sort));
        for (size_t j = 0; ok && j < i; ++j)
            if (order->entries[j]->material == material || order->entries[j]->ordinal == sequence) ok = false;
        if (!ok) break;
        qa_material_order_entry *entry = malloc(sizeof(*entry));
        if (!entry) { ok = fail(error, QA_ERROR_MEMORY, "allocating restored material registration"); break; }
        *entry = (qa_material_order_entry){.owner = order, .material = material, .ordinal = sequence,
            .slot = i, .rank = rank, .published = published};
        order->entries[order->count++] = entry;
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (ok && !dirty) {
        size_t published = 0;
        for (size_t i = 0; i < count; ++i) if (order->entries[i]->published) order->sorted[published++] = order->entries[i];
        if (published > UINT32_MAX) ok = false;
        if (ok && published > 1) qsort(order->sorted, published, sizeof(*order->sorted), compare);
        for (size_t i = 0; ok && i < published; ++i) if (order->sorted[i]->rank != i) ok = false;
    }
    if (ok) {
        for (size_t i = 0; i < count; ++i) ((qa_material *)order->entries[i]->material)->order_entry = order->entries[i];
        *out = order;
    } else {
        if (order) {
            for (size_t i = 0; i < order->count; ++i) free(order->entries[i]);
            qa_material_order_destroy(order);
        }
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "invalid saved renderer material order");
    }
    qa_source_save_dispose(&io); return ok;
}
