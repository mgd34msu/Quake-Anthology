#include "internal.h"
#include "qa/q3_asset_shader.h"

bool qa_q3_assets_shader_read(const qa_q3_presentation_assets *assets,
    int32_t handle, const qa_material **out, qa_error *error)
{
    if (!assets || !out || (assets->busy && (!assets->capturing || assets->codec_busy)) ||
        handle < 0 || (size_t)handle > assets->shader_count)
        return q3p_fail(error, QA_ERROR_ARGUMENT,
            "Q3 shader observation requires its actual idle registered handle");
    const qa_material *material = handle ? assets->shaders[handle - 1] : NULL;
    if (handle && !material)
        return q3p_fail(error, QA_ERROR_NOT_FOUND, "Q3 shader handle has no actual material holder");
    *out = material;
    return true;
}
