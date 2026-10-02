#include "internal.h"
#include <stdio.h>

const qa_material *q3p_default_material(const qa_q3_presentation *p)
{
    return qa_material_find(p->options.assets->options.provider.materials, "*default");
}

bool q3p_picture(qa_q3_presentation *p, const qa_material *material,
                 qa_scene_rect_f rect, qa_scene_vec4 uv, qa_error *error)
{
    if (!p->frame) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 picture has no frame owner");
    if (!material) material = q3p_default_material(p);
    if (!material) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 renderer has no default material");
    qa_scene_rect target = p->options.viewport;
    rect.x += (float)target.x; rect.y += (float)target.y;
    qa_scene_mesh mesh;
    if (!qa_scene_picture_geometry(p->frame, target, rect, uv, p->color, &mesh, error)) return false;
    if (!mesh.vertex_count) return true;
    qa_material_context context = {.entity_color = p->color,
        .identity_light = p->options.identity_light,
        .source_scratch = p->options.source_scratch,
        .source_white = qa_scene_white(p->options.assets->options.provider.images),
        .source_diagnostics = {.polygon_offset_factor = -1, .polygon_offset_units = -2},
        .milliseconds = p->options.milliseconds ? p->options.milliseconds(p->options.context) : p->render_milliseconds,
        .video_frame = p->options.video_frame, .video_context = p->options.video_context};
    context.seconds = (float)context.milliseconds * .001f;
    if (p->material_view_valid) context.view = p->material_view;
    else {
        context.view.axis[0] = qa_v3(1, 0, 0);
        context.view.axis[1] = qa_v3(0, 1, 0);
        context.view.axis[2] = qa_v3(0, 0, 1);
    }
    context.view.viewport = target; context.view.seat = p->options.seat;
    context.local_view_origin = context.view.origin;
    qa_scene_matrix_identity(&context.model);
    if (p->options.prepare_picture && !p->options.prepare_picture(p->options.context, &context, error)) return false;
    context.source_primitives = true;
    context.source_writer = QA_SOURCE_WRITE_PICTURE;
    context.fog = (qa_scene_fog){0}; context.fog_tc_scale = 0; context.fog_index = 0;
    context.view.clip_enabled = false;
    qa_scene_matrix projection = {.m = {
        2.0f / (float)target.width, 0, 0, 0,
        0, -2.0f / (float)target.height, 0, 0,
        0, 0, 0, 0,
        -1.0f - 2.0f * (float)target.x / (float)target.width,
        1.0f + 2.0f * (float)target.y / (float)target.height, 0, 1}};
    bool first_picture = true;
    if (context.source_scratch) {
        projection.m[10] = -2; projection.m[14] = -1;
        context.source_picture_clock = p->options.milliseconds;
        context.source_picture_clock_context = p->options.context;
        context.source_picture = true; context.source_picture_projection = projection;
        if (!qa_material_source_picture_begin(context.source_scratch, p->frame, &context.view, &first_picture, error)) return false;
    }
    size_t first = p->frame->command_count;
    qa_scene_command view = {.kind = QA_SCENE_COMMAND_VIEW,
        .data.view = {.viewport = target, .seat = p->options.seat}};
    if ((first_picture && !qa_scene_frame_emit(p->frame, &view, error)) ||
        !qa_material_submit(material, &mesh, &context, p->frame, error)) {
        p->frame->command_count = first; return false;
    }
    for (size_t i = first; i < p->frame->command_count; ++i) {
        qa_scene_command *command = &p->frame->commands[i];
        if (command->kind != QA_SCENE_COMMAND_DRAW) continue;
        command->data.draw.mvp = projection;
        if (!context.source_scratch) {
            command->data.draw.state.depth_test = QA_DEPTH_ALWAYS;
            command->data.draw.state.depth_write = false;
            command->data.draw.state.cull = QA_CULL_NONE;
        }
    }
    return true;
}

bool qa_q3_presentation_picture(qa_q3_presentation *p, int32_t shader,
                                qa_scene_rect_f rect, qa_scene_vec4 uv, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    const qa_material *material;
    bool ok = q3p_shader_get(p->options.assets, shader, &material, error) &&
        q3p_picture(p, material, rect, uv, error);
    return q3p_end(p, ok);
}

bool qa_q3_presentation_remap(qa_q3_presentation *p, const char *original,
                              const char *replacement, float offset, qa_error *error)
{
    if (!original || !replacement || !isfinite(offset) || !q3p_begin(p, error)) return false;
    bool ok;
    if (p->options.remap) ok = p->options.remap(p->options.context, original, replacement, offset, error);
    else {
        qa_q3_presentation_provider provider;
        qa_material_source_remap_status status = QA_MATERIAL_SOURCE_REMAP_APPLIED;
        ok = q3p_select(p->options.assets, original, QA_Q3_ASSET_SHADER, &provider, error) &&
            qa_material_remap_source(provider.materials, original, replacement, offset, &status, error);
        if (ok && status == QA_MATERIAL_SOURCE_REMAP_APPLIED && p->world)
            ok = qa_scene_world_remap_source(p->world, original, replacement, offset, &status, error);
        if (ok && status != QA_MATERIAL_SOURCE_REMAP_APPLIED && p->options.print) {
            char warning[512];
            snprintf(warning, sizeof(warning), "WARNING: R_RemapShader: %sshader %s not found\n",
                status == QA_MATERIAL_SOURCE_REMAP_TARGET_DEFAULT ? "new " : "",
                status == QA_MATERIAL_SOURCE_REMAP_TARGET_DEFAULT ? replacement : original);
            p->options.print(p->options.context, warning);
        }
    }
    return q3p_end(p, ok);
}
