#include "bank_save_private.h"
#include "qa/audio_bank_graph_save.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct bank_cut {
    qa_vfs *view;
    size_t capacity, count;
    uint64_t registration;
    bank_entry *entries;
} bank_cut;
struct qa_audio_asset_inventory {
    qa_audio_bank **banks;
    bank_cut *cuts;
    size_t bank_count;
    struct asset_row *assets;
    size_t asset_count;
    bool owns_assets;
};
static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
void qa_audio_asset_inventory_destroy(qa_audio_asset_inventory *inventory)
{
    if (!inventory) return;
    if (inventory->owns_assets) for (size_t i = 0; i < inventory->asset_count; ++i)
        qa_audio_asset_release(inventory->assets[i].asset);
    if (inventory->cuts) for (size_t i = 0; i < inventory->bank_count; ++i) free(inventory->cuts[i].entries);
    free(inventory->cuts); free(inventory->banks); free(inventory->assets); free(inventory);
}
static qa_audio_asset_inventory *create(qa_audio_bank *const *banks, size_t count, bool empty, qa_error *error)
{
    if ((count && !banks) || count > SIZE_MAX / sizeof(*banks) || count > SIZE_MAX / sizeof(bank_cut)) {
        fail(error, QA_ERROR_ARGUMENT, "Audio bank graph storage is invalid"); return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (empty && banks[i] && banks[i]->count) { fail(error, QA_ERROR_ARGUMENT, "Audio graph destination bank is occupied"); return NULL; }
        qa_audio_bank_sync(banks[i]);
        if (!qa_bank_cache_valid(banks[i], error)) return NULL;
        for (size_t j = 0; j < i; ++j) if (banks[i] == banks[j]) {
            fail(error, QA_ERROR_ARGUMENT, "Audio bank graph repeats a destructor owner"); return NULL;
        }
    }
    qa_audio_asset_inventory *inventory = calloc(1, sizeof(*inventory));
    if (!inventory) { fail(error, QA_ERROR_MEMORY, "Allocating audio asset graph"); return NULL; }
    inventory->bank_count = count;
    inventory->banks = calloc(count ? count : 1, sizeof(*inventory->banks));
    inventory->cuts = calloc(count ? count : 1, sizeof(*inventory->cuts));
    if (!inventory->banks || !inventory->cuts) { qa_audio_asset_inventory_destroy(inventory); fail(error, QA_ERROR_MEMORY, "Allocating audio bank cuts"); return NULL; }
    if (count) memcpy(inventory->banks, banks, count * sizeof(*banks));
    return inventory;
}
static bool snapshot(bank_cut *cut, const qa_audio_bank *bank, qa_error *error)
{
    *cut = (bank_cut){.view = bank->view, .capacity = bank->capacity, .count = bank->count, .registration = bank->registration};
    cut->entries = bank->count ? malloc(bank->count * sizeof(*cut->entries)) : NULL;
    if (bank->count && !cut->entries) return fail(error, QA_ERROR_MEMORY, "Retaining audio bank physical entries");
    if (bank->count) memcpy(cut->entries, bank->entries, bank->count * sizeof(*cut->entries));
    return true;
}
bool qa_audio_asset_inventory_capture(qa_audio_bank *const *banks, size_t count,
    qa_audio_asset *const *external, size_t external_count, qa_audio_asset_inventory **out, qa_error *error)
{
    if (!out || *out || (external_count && !external)) return fail(error, QA_ERROR_ARGUMENT, "Audio graph requires actual holders and empty inventory output");
    qa_audio_asset_inventory *inventory = create(banks, count, false, error);
    if (!inventory) return false;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        ok = snapshot(&inventory->cuts[i], banks[i], error);
        for (size_t j = 0; ok && j < banks[i]->count; ++j)
            ok = qa_bank_add_asset(&inventory->assets, &inventory->asset_count, banks[i]->entries[j].asset, error);
    }
    for (size_t i = 0; ok && i < external_count; ++i)
        ok = qa_bank_add_asset(&inventory->assets, &inventory->asset_count, external[i], error);
    if (ok) ok = qa_audio_asset_inventory_ready(inventory, error);
    if (!ok) { qa_audio_asset_inventory_destroy(inventory); return false; }
    *out = inventory; return true;
}
bool qa_audio_asset_inventory_index(const qa_audio_asset_inventory *inventory, const qa_audio_asset *asset, uint64_t *out)
{
    if (!inventory || !asset || !out) return false;
    size_t index = qa_bank_asset_index(inventory->assets, inventory->asset_count, asset);
    if (index == SIZE_MAX) return false;
    *out = index; return true;
}
qa_audio_asset *qa_audio_asset_inventory_at(const qa_audio_asset_inventory *inventory, uint64_t index)
{ return inventory && index < inventory->asset_count ? inventory->assets[index].asset : NULL; }
static bool cache_independence(const qa_audio_asset_inventory *inventory, qa_error *error)
{
    for (size_t i = 0; i < inventory->bank_count; ++i)
        for (size_t j = 0; j < inventory->cuts[i].count; ++j) {
            const qa_audio_asset *asset = inventory->cuts[i].entries[j].asset;
            for (size_t k = 0; k < i; ++k)
                for (size_t n = 0; n < inventory->cuts[k].count; ++n) {
                    const qa_audio_asset *other = inventory->cuts[k].entries[n].asset;
                    if (asset == other || asset->sample == other->sample)
                        return fail(error, QA_ERROR_FORMAT, "Distinct audio banks cannot share cached asset or PCM owners");
                }
        }
    return true;
}
static bool sample_aliases(const qa_audio_asset_inventory *inventory, qa_error *error)
{
    for (size_t i = 0; i < inventory->asset_count; ++i) {
        const qa_audio_asset *asset = inventory->assets[i].asset;
        for (size_t j = 0; j < i; ++j) {
            const qa_audio_asset *other = inventory->assets[j].asset;
            if (asset->sample == other->sample && (asset->resource != other->resource || asset->policy != other->policy))
                return fail(error, QA_ERROR_FORMAT, "Shared audio PCM requires the same retained source and decode policy");
        }
    }
    return true;
}
bool qa_audio_asset_inventory_ready(const qa_audio_asset_inventory *inventory, qa_error *error)
{
    if (!inventory) return fail(error, QA_ERROR_ARGUMENT, "Audio asset inventory is absent");
    for (size_t i = 0; i < inventory->bank_count; ++i) {
        const qa_audio_bank *bank = inventory->banks[i]; const bank_cut *cut = &inventory->cuts[i];
        if (!qa_bank_cache_valid(bank, error) || bank->view != cut->view || bank->capacity != cut->capacity ||
            bank->count != cut->count || bank->registration != cut->registration)
            return fail(error, QA_ERROR_FORMAT, "Audio bank changed its captured cut");
        for (size_t j = 0; j < bank->count; ++j)
            if (bank->entries[j].asset != cut->entries[j].asset || bank->entries[j].touched != cut->entries[j].touched)
                return fail(error, QA_ERROR_FORMAT, "Audio bank changed a physical cache entry");
    }
    if (!cache_independence(inventory, error) || !sample_aliases(inventory, error)) return false;
    for (size_t i = 0; i < inventory->asset_count; ++i) {
        const struct asset_row *row = &inventory->assets[i];
        size_t expected = row->holders + (inventory->owns_assets ? 1u : 0u);
        if (!row->holders || row->holders >= UINT_MAX || expected > UINT_MAX || atomic_load_explicit(&row->asset->references, memory_order_relaxed) != expected)
            return fail(error, QA_ERROR_UNSUPPORTED, "Audio asset holders do not match their actual graph");
    }
    return true;
}
