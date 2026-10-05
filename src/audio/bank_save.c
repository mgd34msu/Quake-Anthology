#include "bank_save_private.h"
#include <limits.h>

static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
size_t qa_bank_asset_index(const struct asset_row *rows, size_t count, const qa_audio_asset *asset)
{
    for (size_t i = 0; i < count; ++i) if (rows[i].asset == asset) return i;
    return SIZE_MAX;
}
size_t qa_bank_sample_index(const struct sample_row *rows, size_t count, const qa_audio_sample *sample)
{
    for (size_t i = 0; i < count; ++i) if (rows[i].sample == sample) return i;
    return SIZE_MAX;
}
bool qa_bank_add_asset(struct asset_row **rows, size_t *count, qa_audio_asset *asset, qa_error *error)
{
    if (!asset) return true;
    size_t index = qa_bank_asset_index(*rows, *count, asset);
    if (index == SIZE_MAX) {
        if (*count == SIZE_MAX / sizeof(**rows)) return fail(error, QA_ERROR_MEMORY, "Audio asset inventory overflows");
        struct asset_row *grown = realloc(*rows, (*count + 1) * sizeof(**rows));
        if (!grown) return fail(error, QA_ERROR_MEMORY, "Allocating audio asset inventory");
        *rows = grown; index = (*count)++; grown[index] = (struct asset_row){.asset = asset};
    }
    if ((*rows)[index].holders == UINT_MAX) return fail(error, QA_ERROR_MEMORY, "Audio asset holder count overflows");
    ++(*rows)[index].holders; return true;
}
bool qa_bank_add_sample(struct sample_row **rows, size_t *count, qa_audio_sample *sample, qa_error *error)
{
    size_t index = qa_bank_sample_index(*rows, *count, sample);
    if (index == SIZE_MAX) {
        if (*count == SIZE_MAX / sizeof(**rows)) return fail(error, QA_ERROR_MEMORY, "Audio PCM inventory overflows");
        struct sample_row *grown = realloc(*rows, (*count + 1) * sizeof(**rows));
        if (!grown) return fail(error, QA_ERROR_MEMORY, "Allocating audio PCM inventory");
        *rows = grown; index = (*count)++; grown[index] = (struct sample_row){.sample = sample};
    }
    ++(*rows)[index].holders; return true;
}
static qa_audio_wav_policy source_policy(const qa_resource *source, qa_audio_family family)
{
    qa_bytes bytes = qa_resource_bytes(source);
    return bytes.size && bytes.data[0] == 'R' ? (family == QA_AUDIO_Q3 ? QA_WAV_Q3 : QA_WAV_QUAKE) : QA_WAV_FORMAT;
}
bool qa_bank_asset_valid(const qa_audio_asset *asset, qa_error *error)
{
    qa_vfs_resource_origin origin;
    if (!asset || !asset->files || !asset->resource || !asset->sample || (unsigned)asset->family > QA_AUDIO_Q3 ||
        !asset->mount || asset->resource_id != qa_resource_id(asset->resource) ||
        !qa_vfs_resource_origin_read(asset->files, asset->mount, asset->resource, &origin) ||
        asset->policy != source_policy(asset->resource, asset->family))
        return fail(error, QA_ERROR_FORMAT, "Audio asset identity is not source-qualified");
    return true;
}
static bool capacity_valid(uint64_t capacity)
{
    size_t maximum = SIZE_MAX / sizeof(bank_entry);
    return capacity == 0 || (capacity <= maximum &&
        (capacity == maximum || (capacity >= 16 && !(capacity & (capacity - 1)))));
}
bool qa_bank_write_extent(qa_source_save_io *w, size_t capacity, size_t count)
{
    for (size_t i = 0; i < capacity; ++i) {
        uint8_t occupied = i < count;
        if (!qa_ac_write(w, &occupied, 1)) return false;
    }
    return true;
}
bool qa_bank_read_extent(qa_source_save_io *r, uint64_t capacity, uint64_t count)
{
    if (!capacity_valid(capacity) || count > capacity || capacity > r->input.size - r->offset)
        return qa_ac_bad(r, "Saved audio cache allocation has no physical extent");
    qa_bytes slots;
    if (!qa_ac_read(r, (size_t)capacity, &slots)) return false;
    for (size_t i = 0; i < slots.size; ++i)
        if (slots.data[i] != (i < count)) return qa_ac_bad(r, "Saved audio cache occupancy differs from its live prefix");
    return true;
}
bool qa_bank_cache_valid(const qa_audio_bank *bank, qa_error *error)
{
    if (!bank || !bank->view || !bank->registration || bank->count > bank->capacity ||
        !capacity_valid(bank->capacity) || (bank->capacity && !bank->entries) ||
        bank->lookup_generation != qa_vfs_lookup_generation(bank->view))
        return fail(error, QA_ERROR_ARGUMENT, "Audio bank storage is not qualified");
    for (size_t i = 0; i < bank->count; ++i) {
        const bank_entry *entry = &bank->entries[i];
        if (!entry->asset || entry->asset->files != bank->view || entry->touched > bank->registration)
            return fail(error, QA_ERROR_FORMAT, "Audio registration source or epoch is invalid");
        for (size_t j = 0; j < i; ++j)
            if (entry->asset->resource_id == bank->entries[j].asset->resource_id &&
                entry->asset->family == bank->entries[j].asset->family)
                return fail(error, QA_ERROR_FORMAT, "Duplicate audio bank cache key");
    }
    return true;
}
bool qa_bank_write_asset(qa_source_save_io *w, const struct asset_row *row,
    const struct sample_row *samples, size_t sample_count, const qa_audio_bank_checkpoint_refs *refs)
{
    const qa_audio_asset *a = row->asset; uint64_t view = 0, pool = 0, resource = 0;
    if (!refs->view_encode(refs->context, a->files, &view, w->error) ||
        !refs->resource_encode(refs->context, a->resource, &pool, &resource, w->error)) { w->failed = true; return false; }
    return qa_ac_u64(w, view) && qa_ac_u64(w, pool) && qa_ac_u64(w, resource) &&
        qa_ac_u64(w, a->mount) && qa_ac_u32(w, a->family) && qa_ac_u32(w, a->policy) &&
        qa_ac_blob(w, (qa_bytes){(const uint8_t *)a->name, strlen(a->name)}) &&
        qa_ac_u64(w, qa_bank_sample_index(samples, sample_count, a->sample));
}
bool qa_bank_read_asset(qa_source_save_io *r, struct asset_row *row, struct sample_row *samples,
    size_t sample_count, const qa_audio_bank_checkpoint_refs *refs)
{
    uint64_t view = qa_ac_get64(r), pool = qa_ac_get64(r), resource = qa_ac_get64(r); qa_bytes name;
    uint64_t mount = qa_ac_get64(r); uint32_t family = qa_ac_get32(r), policy = qa_ac_get32(r);
    if (!qa_ac_getblob(r, &name)) return false;
    uint64_t sample = qa_ac_get64(r);
    if (r->failed || !mount || family > QA_AUDIO_Q3 || policy > QA_WAV_Q3 || sample >= sample_count ||
        name.size > SIZE_MAX - sizeof(qa_audio_asset) - 1 || memchr(name.data, 0, name.size))
        return qa_ac_bad(r, "Invalid saved audio asset");
    const qa_resource *source = NULL;
    const qa_vfs *files = NULL;
    if (!refs->view_decode(refs->context, view, &files, r->error)) { r->failed = true; return false; }
    if (!files) return qa_ac_bad(r, "Saved audio source view is absent");
    if (!refs->resource_decode(refs->context, pool, resource, &source, r->error)) { r->failed = true; return false; }
    if (!source) return qa_ac_bad(r, "Saved audio source is absent");
    qa_audio_asset *a = calloc(1, sizeof(*a) + name.size + 1);
    if (!a) { r->failed = true; return fail(r->error, QA_ERROR_MEMORY, "Restoring audio asset"); }
    atomic_init(&a->references, 1); row->asset = a;
    if (!refs->view_retain(refs->context, view, &a->files, r->error)) { r->failed = true; return false; }
    if (a->files != files) return qa_ac_bad(r, "Saved audio source view changed during ownership adoption");
    a->resource = (qa_resource *)source; qa_resource_retain(a->resource);
    a->resource_id = qa_resource_id(source); a->mount = mount; a->family = (qa_audio_family)family; a->policy = (qa_audio_wav_policy)policy;
    memcpy(a->name, name.data, name.size); a->sample = qa_audio_sample_retain(samples[sample].sample);
    if (!a->sample || !qa_bank_asset_valid(a, r->error)) { r->failed = true; return false; }
    ++samples[sample].holders; return true;
}
