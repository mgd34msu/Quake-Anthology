#include "bank_private.h"
#include "checkpoint_internal.h"
#include "qa/audio_bank_save.h"
#include <limits.h>

struct asset_row { qa_audio_asset *asset; size_t holders; };
struct sample_row { qa_audio_sample *sample; size_t holders; };
static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
static size_t asset_index(const struct asset_row *rows, size_t count, const qa_audio_asset *asset)
{
    for (size_t i = 0; i < count; ++i) if (rows[i].asset == asset) return i;
    return SIZE_MAX;
}
static size_t sample_index(const struct sample_row *rows, size_t count, const qa_audio_sample *sample)
{
    for (size_t i = 0; i < count; ++i) if (rows[i].sample == sample) return i;
    return SIZE_MAX;
}
static bool add_asset(struct asset_row **rows, size_t *count, qa_audio_asset *asset, qa_error *error)
{
    if (!asset) return true;
    size_t index = asset_index(*rows, *count, asset);
    if (index == SIZE_MAX) {
        if (*count == SIZE_MAX / sizeof(**rows)) return fail(error, QA_ERROR_MEMORY, "Audio asset inventory overflows");
        struct asset_row *grown = realloc(*rows, (*count + 1) * sizeof(**rows));
        if (!grown) return fail(error, QA_ERROR_MEMORY, "Allocating audio asset inventory");
        *rows = grown; index = (*count)++; grown[index] = (struct asset_row){.asset = asset};
    }
    if ((*rows)[index].holders == UINT_MAX) return fail(error, QA_ERROR_MEMORY, "Audio asset holder count overflows");
    ++(*rows)[index].holders; return true;
}
static bool add_sample(struct sample_row **rows, size_t *count, qa_audio_sample *sample, qa_error *error)
{
    size_t index = sample_index(*rows, *count, sample);
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
static bool pcm_equal(const qa_audio_sample *a, const qa_audio_sample *b)
{
    return a && b && a->sample_rate == b->sample_rate && a->channels == b->channels &&
        a->source_bytes_per_sample == b->source_bytes_per_sample && a->frame_count == b->frame_count &&
        a->loop_start == b->loop_start && a->frame_count <= SIZE_MAX / a->channels / sizeof(int16_t) &&
        !memcmp(a->samples, b->samples, (size_t)a->frame_count * a->channels * sizeof(int16_t));
}
static bool asset_valid(const qa_audio_asset *asset, qa_error *error)
{
    if (!asset || !asset->resource || !asset->sample || (unsigned)asset->family > QA_AUDIO_Q3 ||
        !asset->mount || asset->resource_id != qa_resource_id(asset->resource) ||
        asset->policy != source_policy(asset->resource, asset->family))
        return fail(error, QA_ERROR_FORMAT, "Audio asset identity is not source-qualified");
    qa_audio_sample *parsed = NULL;
    if (!qa_audio_decode(qa_resource_bytes(asset->resource), asset->policy, &parsed, error)) return false;
    bool same = pcm_equal(parsed, asset->sample); qa_audio_sample_release(parsed);
    return same || fail(error, QA_ERROR_FORMAT, "Audio PCM differs from its retained source");
}
static bool cache_valid(const qa_audio_bank *bank, qa_error *error)
{
    if (!bank || !bank->view || !bank->registration || bank->count > bank->capacity ||
        bank->capacity > SIZE_MAX / sizeof(*bank->entries) || (bank->capacity && !bank->entries))
        return fail(error, QA_ERROR_ARGUMENT, "Audio bank storage is not qualified");
    for (size_t i = 0; i < bank->count; ++i) {
        const bank_entry *entry = &bank->entries[i];
        if (!entry->asset || entry->touched > bank->registration)
            return fail(error, QA_ERROR_FORMAT, "Audio registration epoch is invalid");
        for (size_t j = 0; j < i; ++j)
            if (entry->asset->resource_id == bank->entries[j].asset->resource_id &&
                entry->asset->family == bank->entries[j].asset->family)
                return fail(error, QA_ERROR_FORMAT, "Duplicate audio bank cache key");
    }
    return true;
}
static bool write_asset(qa_ac_writer *w, const struct asset_row *row,
    const struct sample_row *samples, size_t sample_count, const qa_audio_bank_checkpoint_refs *refs)
{
    const qa_audio_asset *a = row->asset; uint64_t pool = 0, resource = 0;
    if (!refs->resource_encode(refs->context, a->resource, &pool, &resource, w->error)) { w->failed = true; return false; }
    const qa_sha256_digest *digest = qa_resource_digest(a->resource);
    if (!digest) { w->failed = true; return fail(w->error, QA_ERROR_FORMAT, "Audio source digest is absent"); }
    return qa_ac_u64(w, pool) && qa_ac_u64(w, resource) && qa_ac_write(w, digest, sizeof(*digest)) &&
        qa_ac_u64(w, a->mount) && qa_ac_u32(w, a->family) && qa_ac_u32(w, a->policy) &&
        qa_ac_blob(w, (qa_bytes){(const uint8_t *)a->name, strlen(a->name)}) &&
        qa_ac_u64(w, sample_index(samples, sample_count, a->sample));
}
bool qa_audio_bank_checkpoint(const qa_audio_bank *bank, qa_audio_asset *const *external, size_t count,
    const qa_audio_bank_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || (count && !external) || !refs ||
        !refs->view_encode || !refs->resource_encode)
        return fail(error, QA_ERROR_ARGUMENT, "Audio bank capture requires holders, resolvers and empty output");
    if (!cache_valid(bank, error)) return false;
    struct asset_row *assets = NULL; struct sample_row *samples = NULL;
    size_t asset_count = 0, sample_count = 0; bool ok = true; uint64_t view = 0;
    for (size_t i = 0; ok && i < bank->count; ++i) ok = add_asset(&assets, &asset_count, bank->entries[i].asset, error);
    for (size_t i = 0; ok && i < count; ++i) ok = add_asset(&assets, &asset_count, external[i], error);
    for (size_t i = 0; ok && i < asset_count; ++i) {
        ok = atomic_load_explicit(&assets[i].asset->references, memory_order_relaxed) == assets[i].holders;
        if (!ok) fail(error, QA_ERROR_UNSUPPORTED, "Audio asset has uncoordinated external holders");
        ok = ok && asset_valid(assets[i].asset, error) && add_sample(&samples, &sample_count, assets[i].asset->sample, error);
    }
    ok = ok && refs->view_encode(refs->context, bank->view, &view, error);
    qa_ac_writer w = {.error = error};
    ok = ok && qa_ac_write(&w, "QABK", 4) && qa_ac_u32(&w, 1) && qa_ac_u64(&w, view) &&
        qa_ac_u64(&w, bank->registration) && qa_ac_u64(&w, bank->capacity) && qa_ac_u64(&w, bank->count) &&
        qa_ac_u64(&w, count) && qa_ac_u64(&w, sample_count) && qa_ac_u64(&w, asset_count);
    for (size_t i = 0; ok && i < sample_count; ++i) {
        qa_buffer saved = {0}; ok = qa_audio_sample_checkpoint(samples[i].sample, &saved, error) &&
            qa_ac_blob(&w, (qa_bytes){saved.data, saved.size}); qa_buffer_free(&saved);
    }
    for (size_t i = 0; ok && i < asset_count; ++i) ok = write_asset(&w, &assets[i], samples, sample_count, refs);
    for (size_t i = 0; ok && i < bank->count; ++i) ok = qa_ac_u64(&w, asset_index(assets, asset_count, bank->entries[i].asset)) && qa_ac_u64(&w, bank->entries[i].touched);
    for (size_t i = 0; ok && i < count; ++i) ok = qa_ac_u64(&w, external[i] ? asset_index(assets, asset_count, external[i]) : UINT64_MAX);
    if (ok) ok = qa_ac_finish(&w, out); else qa_buffer_free(&w.buffer);
    free(assets); free(samples);
    if (!ok && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Audio bank capture is not completely qualified");
    return ok;
}
static bool read_asset(qa_ac_reader *r, struct asset_row *row, struct sample_row *samples,
    size_t sample_count, const qa_audio_bank_checkpoint_refs *refs)
{
    uint64_t pool = qa_ac_get64(r), resource = qa_ac_get64(r); qa_bytes digest, name;
    if (!qa_ac_read(r, sizeof(qa_sha256_digest), &digest)) return false;
    uint64_t mount = qa_ac_get64(r); uint32_t family = qa_ac_get32(r), policy = qa_ac_get32(r);
    if (!qa_ac_getblob(r, &name)) return false;
    uint64_t sample = qa_ac_get64(r);
    if (r->failed || !mount || family > QA_AUDIO_Q3 || policy > QA_WAV_Q3 || sample >= sample_count ||
        name.size > SIZE_MAX - sizeof(qa_audio_asset) - 1 || memchr(name.data, 0, name.size))
        return qa_ac_bad(r, "Invalid saved audio asset");
    const qa_resource *source = NULL;
    if (!refs->resource_decode(refs->context, pool, resource, &source, r->error)) { r->failed = true; return false; }
    if (!source || !qa_resource_digest(source) || memcmp(qa_resource_digest(source), digest.data, digest.size))
        return qa_ac_bad(r, "Saved audio source identity changed");
    qa_audio_asset *a = calloc(1, sizeof(*a) + name.size + 1);
    if (!a) { r->failed = true; return fail(r->error, QA_ERROR_MEMORY, "Restoring audio asset"); }
    atomic_init(&a->references, 1); row->asset = a;
    a->resource = (qa_resource *)source; qa_resource_retain(a->resource);
    a->resource_id = qa_resource_id(source); a->mount = mount; a->family = (qa_audio_family)family; a->policy = (qa_audio_wav_policy)policy;
    memcpy(a->name, name.data, name.size); a->sample = qa_audio_sample_retain(samples[sample].sample);
    if (!a->sample || !asset_valid(a, r->error)) { r->failed = true; return false; }
    ++samples[sample].holders; return true;
}
static bool take_holder(qa_ac_reader *r, struct asset_row *rows, size_t count, bool nullable, qa_audio_asset **out)
{
    uint64_t index = qa_ac_get64(r);
    if (nullable && index == UINT64_MAX) return !r->failed;
    if (r->failed || index >= count || rows[index].holders == UINT_MAX - 1) return qa_ac_bad(r, "Invalid audio asset holder");
    *out = qa_audio_asset_retain(rows[index].asset);
    if (!*out) return qa_ac_bad(r, "Audio asset reference count exhausted");
    ++rows[index].holders; return true;
}
bool qa_audio_bank_restore(qa_audio_bank *bank, qa_audio_asset **external, size_t count,
    const qa_audio_bank_checkpoint_refs *refs, qa_bytes bytes, qa_error *error)
{
    if (!bank || !bank->view || bank->count || (count && !external) || !refs || !refs->view_decode ||
        !refs->resource_decode || (bytes.size && !bytes.data))
        return fail(error, QA_ERROR_ARGUMENT, "Audio bank restore requires an empty candidate and content resolvers");
    for (size_t i = 0; i < count; ++i) if (external[i]) return fail(error, QA_ERROR_ARGUMENT, "Audio external holder destination is occupied");
    qa_ac_reader r = {.bytes = bytes, .error = error}; qa_bytes magic;
    bool ok = qa_ac_read(&r, 4, &magic) && !memcmp(magic.data, "QABK", 4) && qa_ac_get32(&r) == 1;
    uint64_t view = qa_ac_get64(&r), registration = qa_ac_get64(&r), capacity = qa_ac_get64(&r), entries = qa_ac_get64(&r);
    uint64_t slots = qa_ac_get64(&r), sample_count = qa_ac_get64(&r), asset_count = qa_ac_get64(&r);
    const qa_vfs *resolved = NULL;
    ok = ok && !r.failed && slots == count && entries <= capacity && capacity <= SIZE_MAX / sizeof(bank_entry) &&
        sample_count <= SIZE_MAX / sizeof(struct sample_row) && asset_count <= SIZE_MAX / sizeof(struct asset_row) &&
        sample_count <= bytes.size / 56 && asset_count <= bytes.size / 80 && entries <= bytes.size / 16 && count <= bytes.size / 8 &&
        refs->view_decode(refs->context, view, &resolved, error) && resolved == bank->view;
    struct sample_row *samples = NULL; struct asset_row *assets = NULL; qa_audio_asset **holders = NULL;
    qa_audio_bank candidate = {.view = bank->view, .registration = registration, .capacity = (size_t)capacity};
    if (ok) {
        samples = calloc(sample_count ? (size_t)sample_count : 1, sizeof(*samples));
        assets = calloc(asset_count ? (size_t)asset_count : 1, sizeof(*assets));
        holders = calloc(count ? count : 1, sizeof(*holders));
        candidate.entries = capacity ? calloc((size_t)capacity, sizeof(*candidate.entries)) : NULL;
        if (!samples || !assets || !holders || (capacity && !candidate.entries)) ok = fail(error, QA_ERROR_MEMORY, "Allocating audio bank continuation");
    }
    for (size_t i = 0; ok && i < sample_count; ++i) {
        qa_bytes saved; ok = qa_ac_getblob(&r, &saved) && qa_audio_sample_restore(saved, &samples[i].sample, error);
    }
    for (size_t i = 0; ok && i < asset_count; ++i) ok = read_asset(&r, &assets[i], samples, (size_t)sample_count, refs);
    for (size_t i = 0; ok && i < entries; ++i) {
        ok = take_holder(&r, assets, (size_t)asset_count, false, &candidate.entries[i].asset);
        if (ok) { ++candidate.count; candidate.entries[i].touched = qa_ac_get64(&r); ok = !r.failed; }
    }
    for (size_t i = 0; ok && i < count; ++i) ok = take_holder(&r, assets, (size_t)asset_count, true, &holders[i]);
    for (size_t i = 0; ok && i < sample_count; ++i) ok = samples[i].holders != 0;
    for (size_t i = 0; ok && i < asset_count; ++i) ok = assets[i].holders != 0;
    ok = ok && !r.failed && r.offset == bytes.size && cache_valid(&candidate, error);
    if (ok) {
        free(bank->entries); *bank = candidate; candidate.entries = NULL; candidate.count = 0;
        for (size_t i = 0; i < count; ++i) { external[i] = holders[i]; holders[i] = NULL; }
    }
    for (size_t i = 0; i < candidate.count; ++i) qa_audio_asset_release(candidate.entries[i].asset);
    free(candidate.entries);
    if (holders) for (size_t i = 0; i < count; ++i) qa_audio_asset_release(holders[i]);
    if (assets) for (size_t i = 0; i < asset_count; ++i) qa_audio_asset_release(assets[i].asset);
    if (samples) for (size_t i = 0; i < sample_count; ++i) qa_audio_sample_release(samples[i].sample);
    free(holders); free(assets); free(samples);
    if (!ok && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Audio bank graph is not completely qualified");
    return ok;
}
