/* Q1 r_sprite.c and Q2 gl_rmain.c placement.
 * Copyright (C) 1996-2001 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"

static bool sprite_axes(const qa_scene_model *model, const qa_scene_model_input *input,
                          qa_vec3 axes[3]) {
    memcpy(axes, input->view.axis, sizeof(qa_vec3) * 3);
    if (model->source->format != QA_MODEL_SPR) return true;
    qa_vec3 direction;
    switch (model->source->orientation) {
    case 0:
        direction = qa_vec_normalize(qa_vec_sub(model_origin(input), input->view.origin));
        break;
    case 1:
        direction = input->view.axis[0];
        break;
    case 2:
        return true;
    case 3:
        for (unsigned i = 0; i < 3; ++i) axes[i] = model_vec(input->transform.axes[i]);
        return true;
    case 4: {
        double angle = input->rotation * 0.01745329251994329577;
        float sine = (float)sin(angle), cosine = (float)cos(angle);
        qa_vec3 right = qa_vec_scale(input->view.axis[1], -1), up = input->view.axis[2];
        axes[1] = qa_vec_scale(qa_vec_add(qa_vec_scale(right, cosine), qa_vec_scale(up, sine)), -1);
        axes[2] = qa_vec_add(qa_vec_scale(right, -sine), qa_vec_scale(up, cosine));
        return true;
    }
    default:
        return false;
    }
    if (fabsf(direction.z) > 0.999848f) return false;
    qa_vec3 right = qa_vec_normalize(qa_v3(direction.y, -direction.x, 0));
    axes[0] = qa_v3(-right.y, right.x, 0);
    axes[1] = qa_vec_scale(right, -1);
    axes[2] = qa_v3(0, 0, 1);
    return true;
}

bool scene_model_sprite_submit(qa_scene_model *model, const qa_scene_model_input *input,
                                uint32_t selected, qa_scene_frame *frame, qa_error *error) {
    if (selected >= model->source->sprite_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, selected, "sprite frame is outside the retained model"); return false;
    }
    const qa_model_sprite *sprite = &model->source->sprites[selected];
    qa_vec3 axes[3];
    if (!sprite_axes(model, input, axes)) return true;
    qa_vec3 origin = model_origin(input);
    bool q1 = model->source->format == QA_MODEL_SPR;
    if (q1) {
        origin = qa_vec_sub(origin, qa_vec_scale(axes[0], model->source->beam_length));
        if (qa_vec_dot(axes[0], qa_vec_sub(input->view.origin, origin)) >= 0) return true;
    }
    float left, right, top, bottom;
    if (q1) {
        left = (float)sprite->origin_x * input->transform.scale[0];
        right = ((float)sprite->origin_x + (float)sprite->width) * input->transform.scale[0];
        top = (float)sprite->origin_y * input->transform.scale[2];
        bottom = ((float)sprite->origin_y - (float)sprite->height) * input->transform.scale[2];
    } else {
        left = -(float)sprite->origin_x * input->transform.scale[0];
        right = ((float)sprite->width - (float)sprite->origin_x) * input->transform.scale[0];
        top = ((float)sprite->height - (float)sprite->origin_y) * input->transform.scale[2];
        bottom = -(float)sprite->origin_y * input->transform.scale[2];
    }
    qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage, 4 * sizeof(*vertices), _Alignof(qa_scene_vertex), error);
    if (!vertices) return false;
    static const uint32_t indices[6] = {0, 1, 3, 3, 1, 2};
    qa_vec3 right_axis = qa_vec_scale(axes[1], input->view.mirror ? 1 : -1);
    float xs[4] = {left, right, right, left}, ys[4] = {top, top, bottom, bottom};
    const qa_scene_vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    qa_bounds bounds = model_bounds_empty();
    for (unsigned i = 0; i < 4; ++i) {
        vertices[i] = (qa_scene_vertex){.position = qa_vec_add(origin,
            qa_vec_add(qa_vec_scale(right_axis, xs[i]), qa_vec_scale(axes[2], ys[i]))),
            .normal = qa_vec_scale(axes[0], -1), .texcoord = uv[i], .lightmap = uv[i], .color = input->color};
        model_bounds_add(&bounds, vertices[i].position);
    }
    if (!input->no_cull) {
        qa_scene_plane planes[6];
        size_t plane_count = qa_scene_frustum(&input->view, planes);
        if (!qa_scene_bounds_visible(bounds, planes, plane_count)) return true;
    }
    qa_scene_mesh mesh = {.vertices = vertices, .indices = indices, .vertex_count = 4,
        .index_count = 6, .bounds = bounds, .primitive = QA_SCENE_TRIANGLES};
    return scene_model_emit(model, input, &mesh, model->sprites[selected], true, true, frame, error);
}
