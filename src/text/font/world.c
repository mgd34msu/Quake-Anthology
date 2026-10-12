#include "internal.h"
#include "qa/text.h"

#include <math.h>
#include <string.h>

static bool finite_color(qa_vec4 value) {
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z) && isfinite(value.w);
}

static bool world_quad(qa_scene_frame *frame, const qa_scene_view *view, const qa_font_glyph *glyph,
                       qa_vec4 color, qa_vec3 origin, qa_vec3 right, qa_vec3 down,
                       float column, float row, bool depth_test, qa_error *error) {
    qa_scene_vertex *vertices =
        qa_arena_alloc(&frame->storage, 4 * sizeof(*vertices), _Alignof(qa_scene_vertex), error);
    uint32_t *indices =
        qa_arena_alloc(&frame->storage, 6 * sizeof(*indices), _Alignof(uint32_t), error);
    if (!vertices || !indices)
        return false;
    qa_vec3 base =
        qa_vec_add(origin, qa_vec_add(qa_vec_scale(right, column), qa_vec_scale(down, row)));
    qa_vec3 positions[4] = {
        base,
        qa_vec_add(base, right),
        qa_vec_add(qa_vec_add(base, right), down),
        qa_vec_add(base, down),
    };
    qa_vec2 uv[4] = {
        {glyph->uv.x, glyph->uv.y},
        {glyph->uv.z, glyph->uv.y},
        {glyph->uv.z, glyph->uv.w},
        {glyph->uv.x, glyph->uv.w},
    };
    qa_vec3 normal = qa_vec_scale(view->axis[0], -1);
    for (size_t i = 0; i < 4; ++i)
        vertices[i] = (qa_scene_vertex){.position = positions[i],
                                        .normal = normal,
                                        .texcoord = uv[i],
                                        .lightmap = uv[i],
                                        .color = color};
    const uint32_t order[6] = {0, 1, 2, 0, 2, 3};
    memcpy(indices, order, sizeof(order));
    qa_bounds bounds = {positions[0], positions[0]};
    for (size_t i = 1; i < 4; ++i)
        bounds = qa_bounds_union(bounds, (qa_bounds){positions[i], positions[i]});
    qa_scene_draw draw = {0};
    draw.mesh = (qa_scene_mesh){.vertices = vertices,
                                .indices = indices,
                                .vertex_count = 4,
                                .index_count = 6,
                                .bounds = bounds,
                                .primitive = QA_SCENE_TRIANGLES};
    qa_scene_matrix_identity(&draw.model);
    draw.mvp = qa_scene_matrix_multiply(view->projection, qa_scene_view_matrix(view));
    draw.textures[0] = glyph->image;
    draw.texture_count = 1;
    draw.environment = QA_TEXTURE_MODULATE;
    draw.lighting = QA_LIGHT_VERTEX;
    qa_scene_state_default(&draw.state);
    draw.state.blend_source = QA_BLEND_SRC_ALPHA;
    draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
    draw.state.depth_test = depth_test ? QA_DEPTH_LEQUAL : QA_DEPTH_ALWAYS;
    draw.state.depth_write = false;
    draw.state.cull = QA_CULL_NONE;
    return qa_scene_frame_draw(frame, &draw, error);
}

static size_t line_end_and_count(qa_bytes text, size_t begin, size_t *characters, bool *nul) {
    size_t at = begin, count = 0;
    *nul = false;
    while (at < text.size) {
        size_t next = at;
        uint32_t codepoint;
        if (!qa_utf8_next(text, &next, &codepoint))
            break;
        if (!codepoint) {
            *nul = true;
            break;
        }
        if (codepoint == '\n')
            break;
        at = next;
        ++count;
    }
    *characters = count;
    return at;
}

bool qa_font_world_draw(qa_scene_frame *frame, const qa_scene_view *view,
                        const qa_font_world_snapshot *snapshot, const qa_font_selection *selection,
                        float distance_cull_override, bool has_distance_cull_override,
                        qa_error *error) {
    if (!frame || !view || !snapshot || !selection || !selection->classic ||
        (snapshot->count && !snapshot->texts) || view->seat != selection->seat ||
        (has_distance_cull_override && !isfinite(distance_cull_override)))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid world text draw request");
    qa_font_selection classic = {.seat = selection->seat, .classic = selection->classic};
    for (size_t index = 0; index < snapshot->count; ++index) {
        const qa_font_world_text *text = &snapshot->texts[index];
        if ((!text->text.data && text->text.size) || !qa_vec_finite(text->origin) ||
            (text->orientation == QA_FONT_WORLD_FIXED && !qa_vec_finite(text->angles)) ||
            !finite_color(text->color) || !isfinite(text->cell_size) || text->cell_size <= 0 ||
            (text->has_distance_cull && !isfinite(text->distance_cull_factor)) ||
            text->orientation < QA_FONT_WORLD_BILLBOARD ||
            text->orientation > QA_FONT_WORLD_FIXED || text->font < QA_FONT_WORLD_CLASSIC ||
            text->font > QA_FONT_WORLD_SELECTED)
            return qa_font_fail(error, QA_ERROR_ARGUMENT, index,
                                "Invalid world text snapshot entry");
        float depth = qa_vec_dot(qa_vec_sub(text->origin, view->origin), view->axis[0]);
        bool cull = text->has_distance_cull;
        float factor =
            has_distance_cull_override ? distance_cull_override : text->distance_cull_factor;
        if (cull && text->cell_size < depth * factor)
            continue;
        const qa_font_selection *font = text->font == QA_FONT_WORLD_CLASSIC ? &classic : selection;
        qa_vec3 axis[3];
        if (text->orientation == QA_FONT_WORLD_BILLBOARD) {
            axis[0] = view->axis[0];
            axis[1] = view->axis[1];
            axis[2] = view->axis[2];
        } else {
            qa_angle_axis(text->angles, axis,QA_ANGLE_FLOAT_MATH);
        }
        qa_vec3 right = qa_vec_scale(axis[1], -text->cell_size);
        qa_vec3 down = qa_vec_scale(axis[2], -text->cell_size);
        size_t line_begin = 0;
        float row = 0;
        while (line_begin <= text->text.size) {
            size_t count = 0;
            bool nul = false;
            size_t line_end = line_end_and_count(text->text, line_begin, &count, &nul);
            size_t at = line_begin, column = 0;
            while (at < line_end) {
                size_t source_offset = at;
                uint32_t codepoint;
                if (!qa_utf8_next(text->text, &at, &codepoint))
                    break;
                qa_font_glyph glyph;
                if (!qa_font_resolve(font, codepoint, false, &glyph))
                    return qa_font_fail(error, QA_ERROR_FORMAT, source_offset,
                                        "World font cannot resolve replacement glyph");
                if (glyph.visible && glyph.image &&
                    !world_quad(frame, view, &glyph, text->color, text->origin, right, down,
                                (float)column - (float)count * 0.5f, row, text->depth_test, error))
                    return false;
                ++column;
            }
            if (nul || line_end == text->text.size)
                break;
            line_begin = line_end;
            uint32_t newline;
            if (!qa_utf8_next(text->text, &line_begin, &newline))
                break;
            row += 1.0f;
        }
    }
    return true;
}
