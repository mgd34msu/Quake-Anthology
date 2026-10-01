#include "internal.h"
#include "qa/scene_model_save.h"
#include "qa/scene_world_save.h"

static bool observable(const qa_q3_presentation_assets *a)
{
    if (a->world && !qa_scene_world_observation_ready(a->world)) return false;
    for (size_t i = 0; i < a->model_count; ++i) {
        const q3p_model *model = a->models[i];
        if (!model) continue;
        if (model->world && !qa_scene_world_observation_ready(model->world)) return false;
        for (unsigned j = 0; j < 3; ++j)
            if (model->scene[j] && !qa_scene_model_observation_ready(model->scene[j])) return false;
    }
    return true;
}
bool q3p_capture_begin(qa_q3_presentation *p, bool *owned_assets, qa_error *error)
{
    qa_q3_presentation_assets *a = p ? p->options.assets : NULL;
    if (!p || p->busy || !a || a->codec_busy || (a->busy && !a->capturing) ||
        !owned_assets || !observable(a))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 presentation capture requires genuine observable owners");
    *owned_assets = !a->capturing;
    if (*owned_assets && !qa_q3_assets_capture_begin(a, error)) return false;
    a->codec_busy = true; p->busy = 1;
    return true;
}
void q3p_capture_end(qa_q3_presentation *p, bool owned_assets)
{
    qa_q3_presentation_assets *a = p->options.assets;
    p->busy = 0; a->codec_busy = false;
    if (owned_assets) qa_q3_assets_capture_end(a);
}
