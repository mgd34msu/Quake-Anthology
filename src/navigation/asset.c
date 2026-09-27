#include "asset_internal.h"

bool qa_nav_asset_read(qa_bytes bytes, const int32_t *checksum, qa_nav_asset **out,
                       qa_error *error) {
    if (out == NULL || bytes.data == NULL || bytes.size < 8) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Truncated navigation header");
        return false;
    }
    if (qa_load_u32le(bytes.data) == UINT32_C(0x53414145))
        return nav_aas_read(bytes, checksum, out, error);
    if (memcmp(bytes.data, "NAV2", 4) == 0 || memcmp(bytes.data, "NAV3", 4) == 0)
        return nav_kex_read(bytes, out, error);
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Unknown navigation asset magic");
    return false;
}
void qa_nav_asset_retain(qa_nav_asset *asset) {
    if (asset != NULL)
        atomic_fetch_add_explicit(&asset->references, 1, memory_order_relaxed);
}
void qa_nav_asset_release(qa_nav_asset *asset) {
    if (asset != NULL &&
        atomic_fetch_sub_explicit(&asset->references, 1, memory_order_acq_rel) == 1) {
        free(asset->storage);
        free(asset);
    }
}
qa_nav_asset_kind qa_nav_asset_type(const qa_nav_asset *asset) { return asset->kind; }
const qa_aas_view *qa_nav_asset_aas(const qa_nav_asset *asset) {
    return asset != NULL && asset->kind == QA_NAV_AAS ? &asset->aas : NULL;
}
const qa_nav_source_view *qa_nav_asset_kex(const qa_nav_asset *asset) {
    return asset != NULL && asset->kind != QA_NAV_AAS ? &asset->kex : NULL;
}
