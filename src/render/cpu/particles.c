#include "particles.h"

/* WinQuake d_part.c / Q2 ref_soft r_part.c: projected squares, inverse-depth
 * size, a one-pixel minimum and a depth test for every covered pixel. */
bool cpu_particles(qa_cpu_renderer *renderer, const qa_scene_particle_batch *batch, qa_error *error)
{
    (void)error;
    if (renderer->opacity_skip) return true;
    cpu_framebuffer *target = renderer->current;
    const qa_scene_view *view = &batch->view;
    qa_scene_matrix mvp = qa_scene_matrix_multiply(view->projection, qa_scene_view_matrix(view));
    int minimum = (int)(view->viewport.width / 320);
    if (minimum < 1) minimum = 1;
    int maximum = (int)((float)view->viewport.width / 80.0f + .5f);
    if (maximum < 1) maximum = 1;
    int shift = 8 - (int)((float)view->viewport.width / 320.0f + .5f);
    double shrink = view->viewport.width > 6 ? (double)(view->viewport.width - 6) / view->viewport.width : 1;
    double pixel_aspect = fabs((double)view->viewport.height * view->projection.m[5] /
        ((double)view->viewport.width * view->projection.m[0]));
    unsigned y_shift = batch->family == QA_GAME_Q1 && pixel_aspect > 1.4 ? 1u : 0u;
    /* ldexp avoids undefined negative shifts at large modern resolutions. */
    double size_scale = ldexp(1, -shift);
    int64_t right = (int64_t)view->viewport.x + view->viewport.width - maximum;
    int64_t bottom = (int64_t)view->viewport.y + view->viewport.height - (int64_t)maximum * (1u << y_shift);
    qa_scene_state_default(&renderer->pipeline);
    renderer->pipeline.cull = QA_CULL_NONE;
    renderer->pipeline.blend_source = QA_BLEND_SRC_ALPHA;
    renderer->pipeline.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
    for (size_t i = 0; i < batch->count; ++i) {
        const qa_scene_particle_sample *sample = batch->samples + i;
        qa_vec3 delta = qa_vec_sub(sample->origin, view->origin);
        float eye_depth = qa_vec_dot(delta, view->axis[0]);
        if (eye_depth < 8 || (view->clip_enabled &&
            qa_vec_dot(sample->origin, view->clip_plane.normal) < view->clip_plane.distance)) continue;
        double clip[4];
        for (unsigned c = 0; c < 4; ++c)
            clip[c] = (double)mvp.m[c] * sample->origin.x +
                (double)mvp.m[4 + c] * sample->origin.y +
                (double)mvp.m[8 + c] * sample->origin.z + mvp.m[12 + c];
        if (clip[3] <= 0 || clip[2] < -clip[3] || clip[2] > clip[3]) continue;
        double projected_x = view->viewport.x + view->viewport.width * .5 +
            clip[0] / clip[3] * view->viewport.width * .5 * shrink;
        double projected_y = view->viewport.y + view->viewport.height * .5 -
            clip[1] / clip[3] * view->viewport.height * .5 * shrink;
        if (projected_x < view->viewport.x || projected_x > (double)right + 1 ||
            projected_y < view->viewport.y || projected_y > (double)bottom + 1) continue;
        int64_t x = (int64_t)projected_x, y = (int64_t)projected_y;
        if (x < view->viewport.x || y < view->viewport.y || x > right || y > bottom) continue;
        double inverse_depth = 1.0 / eye_depth;
        int inverse_word = (int)(inverse_depth * 32768);
        double sized = inverse_word * size_scale;
        int pixels = sized > maximum ? maximum : sized < minimum ? minimum : (int)sized;
        float depth = cpu_clamp((float)((clip[2] / clip[3] + 1) * .5));
        unsigned blend = batch->family == QA_GAME_Q1 || sample->color.w > .66f ? 3u :
            sample->color.w > .33f ? 2u : 1u;
        uint8_t color[3] = {cpu_byte(sample->color.x), cpu_byte(sample->color.y), cpu_byte(sample->color.z)};
        if (renderer->preblend_gamma && renderer->gamma_enabled)
            for (unsigned c = 0; c < 3; ++c) color[c] = renderer->gamma[color[c]];
        for (int py = 0; py < pixels * (int)(1u << y_shift); ++py) for (int px = 0; px < pixels; ++px) {
            int64_t sx = x + px, sy = y + py;
            if (sx < 0 || sy < 0 || (uint64_t)sx >= target->width || (uint64_t)sy >= target->height) continue;
            size_t at = (size_t)sy * target->width + (size_t)sx;
            if (depth > target->depth[at]) continue;
            target->depth[at] = depth;
            if (target->depth_only) continue;
            uint8_t *destination = target->color + at * 4;
            for (unsigned c = 0; c < 3; ++c)
                destination[c] = (uint8_t)((color[c] * blend + destination[c] * (3 - blend)) / 3);
            destination[3] = 255;
        }
    }
    return true;
}
