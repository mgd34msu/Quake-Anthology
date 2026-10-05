#include "bank_save_private.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
size_t qa_bank_asset_index(const struct asset_row *rows, size_t count, const qa_audio_asset *asset)
{
    for (size_t i = 0; i < count; ++i) if (rows[i].asset == asset) return i;
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
static bool capacity_valid(uint64_t capacity)
{
    size_t maximum = SIZE_MAX / sizeof(bank_entry);
    return capacity == 0 || (capacity <= maximum &&
        (capacity == maximum || (capacity >= 16 && !(capacity & (capacity - 1)))));
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
