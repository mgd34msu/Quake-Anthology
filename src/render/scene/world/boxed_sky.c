#include "boxed_sky.h"
#include "legacy/internal.h"
#include "qa/scene_effects.h"
#include <string.h>

struct qa_scene_boxed_sky {
    qa_scene_world *world;
    qa_scene_frame *frame;
    uint64_t sequence;
    qa_scene_view view;
    qa_scene_sky_bounds bounds[6];
    const qa_scene_image *images[6];
    qa_scene_recipient_image_fn recipient;
    void *recipient_context;
    qa_vec3 axis;
    qa_scene_fog fog;
    float angle;
    size_t insertion;
    bool rotating, positioned, finished;
};

static bool fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}

bool qa_scene_world_boxed_sky_begin(qa_scene_world *world,
    const qa_scene_world_input *input, qa_scene_frame *frame,
    qa_scene_boxed_sky **out, qa_error *error)
{
    if (!world || !input || !frame || !out || *out)
        return fail(error, "Boxed sky requires its actual world, view and frame");
    if (input->no_world || input->q1_sky || input->source_order ||
        input->source_scratch || frame->source_pending ||
        (world->bsp.family != QA_BSP_Q2 && !input->override_sky && !world->options.q2_sky))
        return true;
    if (!isfinite(input->sky_rotation) || !qa_vec_finite(input->sky_axis))
        return fail(error, "Boxed sky has nonfinite rotation");
    qa_scene_boxed_sky *sky = qa_arena_alloc(&frame->storage, sizeof(*sky),
        _Alignof(qa_scene_boxed_sky), error);
    if (!sky) return false;
    *sky = (qa_scene_boxed_sky){.world = world, .frame = frame,
        .sequence = frame->sequence, .view = input->view, .axis = input->sky_axis,
        .fog = world->bsp.family == QA_BSP_Q3 ? (qa_scene_fog){0} : input->fog,
        .recipient = world->bsp.family == QA_BSP_Q3 ? NULL : input->source_recipient_image,
        .recipient_context = input->source_recipient_context,
        .angle = world->bsp.family == QA_BSP_Q3 ? input->sky_rotation *
            (input->sky_auto_rotate ? (float)input->seconds : 1) :
            input->sky_auto_rotate ? (float)(input->seconds * input->sky_rotation) : input->sky_rotation,
        .rotating = input->sky_rotation != 0};
    qa_scene_sky_bounds_reset(sky->bounds);
    const qawl_world *legacy = world->legacy_data;
    for (size_t i = 0; i < 6; ++i) {
        const qa_scene_image *image = input->override_sky ? input->sky_images[i] :
            legacy ? legacy->sky[i] : NULL;
        sky->images[i] = image;
    }
    *out = sky;
    return true;
}

bool qaw_boxed_sky_world_end(qa_scene_boxed_sky *sky, const qa_scene_world *world,
    qa_scene_frame *frame, size_t insertion, qa_error *error)
{
    if (!sky || sky->frame != frame || sky->sequence != frame->sequence ||
        sky->finished || sky->world != world || insertion > frame->command_count)
        return fail(error, "Boxed sky insertion lost its actual opaque world");
    sky->insertion = insertion;
    sky->positioned = true;
    return true;
}

bool qaw_boxed_sky_collect(qa_scene_boxed_sky *sky, const qa_scene_mesh *source,
    const qa_material_context *context, qa_scene_frame *frame, qa_error *error)
{
    if (!sky || sky->frame != frame || sky->sequence != frame->sequence ||
        sky->finished || !source || !context || source->index_count % 3 ||
        (source->index_count && (!source->vertices || !source->indices)))
        return fail(error, "Boxed sky footprint lost its view or polygon");
    if (context->view.seat != sky->view.seat ||
        memcmp(&context->view.viewport, &sky->view.viewport, sizeof(sky->view.viewport)) ||
        memcmp(&context->view.origin, &sky->view.origin, sizeof(sky->view.origin)) ||
        memcmp(context->view.axis, sky->view.axis, sizeof(sky->view.axis)) ||
        memcmp(&context->view.projection, &sky->view.projection, sizeof(sky->view.projection)))
        return fail(error, "Boxed sky footprint belongs to another view");
    qa_scene_vertex vertices[3] = {0};
    const uint32_t indices[3] = {0, 1, 2};
    const qa_scene_mesh triangle = {.vertices = vertices, .indices = indices,
        .vertex_count = 3, .index_count = 3, .primitive = QA_SCENE_TRIANGLES};
    for (size_t i = 0; i < source->index_count; i += 3) {
        for (size_t j = 0; j < 3; ++j) {
            uint32_t index = source->indices[i + j];
            if (index >= source->vertex_count)
                return fail(error, "Boxed sky polygon has an invalid vertex");
            qa_scene_vec4 point = qa_scene_matrix_point(context->model,
                source->vertices[index].position);
            vertices[j].position = qa_v3(point.x, point.y, point.z);
        }
        if (!qa_scene_sky_clip(&triangle, 1, sky->view.origin, sky->bounds, error))
            return false;
    }
    return true;
}

bool qa_scene_world_boxed_sky_finish(qa_scene_boxed_sky *sky,
    qa_scene_frame *frame, qa_error *error)
{
    if (!sky || sky->frame != frame || sky->sequence != frame->sequence || sky->finished)
        return fail(error, "Boxed sky finish lost its actual view and frame");
    bool visible = false;
    for (size_t i = 0; i < 6; ++i)
        visible |= sky->bounds[i].min_s < sky->bounds[i].max_s &&
            sky->bounds[i].min_t < sky->bounds[i].max_t;
    if (!visible) { sky->finished = true; return true; }
    if (!sky->positioned || sky->insertion > frame->command_count)
        return fail(error, "Boxed sky has no opaque-world insertion point");
    size_t first = frame->command_count;
    for (size_t i = 0; sky->recipient && i < 6; ++i)
        if (sky->images[i]) {
            const qa_scene_image *image = NULL;
            if (!sky->recipient(sky->recipient_context, sky->images[i], false,
                false, &image, error)) return false;
            if (!image) return fail(error, "Boxed sky recipient lost its reached image");
            sky->images[i] = image;
        }
    if (!qa_scene_q2_sky(frame, &sky->view, sky->images, sky->bounds,
        sky->angle, sky->axis, sky->rotating, (qa_scene_vec4){1, 1, 1, 1}, error))
        return false;
    if (frame->command_count != first) {
        if (sky->fog.kind == QA_FOG_EXP2 && sky->fog.density > 0)
            for (size_t i = first; i < frame->command_count; ++i)
                if (frame->commands[i].kind == QA_SCENE_COMMAND_DRAW)
                    frame->commands[i].data.draw.fog = (qa_scene_fog){
                        .kind = QA_FOG_CONSTANT, .effect = QA_FOG_COLOR,
                        .color = sky->fog.color, .amount = sky->fog.sky_factor};
        size_t added = frame->command_count - first;
        if (added > SIZE_MAX / sizeof(qa_scene_command))
            return fail(error, "Boxed sky command storage overflows");
        qa_scene_command *commands = qa_arena_alloc(&frame->storage,
            added * sizeof(*commands), _Alignof(qa_scene_command), error);
        if (!commands) return false;
        memcpy(commands, frame->commands + first, added * sizeof(*commands));
        memmove(frame->commands + sky->insertion + added,
            frame->commands + sky->insertion, (first - sky->insertion) * sizeof(*commands));
        memcpy(frame->commands + sky->insertion, commands, added * sizeof(*commands));
        for (size_t i = 0; i < frame->group_count; ++i)
            if (frame->groups[i].first >= sky->insertion) frame->groups[i].first += added;
        sky->world->sky_drawn = true;
    }
    sky->finished = true;
    return true;
}
