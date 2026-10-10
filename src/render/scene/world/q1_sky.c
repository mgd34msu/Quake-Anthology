#include "q1_sky.h"
#include "legacy/internal.h"
#include "../effects/internal.h"
#include "qa/scene_effects.h"
#include <limits.h>
#include <string.h>

typedef struct q1_sky_polygon {
    struct q1_sky_polygon *next;
    qa_scene_mesh mesh;
} q1_sky_polygon;
struct qa_scene_q1_sky {
    qa_scene_world *world;
    qa_scene_frame *frame;
    uint64_t sequence, identity, revision;
    size_t first;
    qa_scene_world_input input;
    qa_scene_q1_sky_environment environment;
    qa_scene_sky_bounds bounds[6];
    qa_vec4 flat;
    const qa_scene_image *layers[2];
    q1_sky_polygon *head, *tail;
    bool finished;
};
static bool fail(qa_error *error, const char *text)
{ qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text); return false; }
static bool current(const qa_scene_q1_sky *sky, const qa_scene_frame *frame)
{
    return sky && sky->frame == frame && sky->sequence == frame->sequence &&
        sky->world->identity == sky->identity && sky->world->revision == sky->revision && !sky->finished;
}
static bool same_view(const qa_scene_view *a, const qa_scene_view *b)
{
    return !memcmp(&a->viewport, &b->viewport, sizeof(a->viewport)) &&
        !memcmp(&a->origin, &b->origin, sizeof(a->origin)) &&
        !memcmp(a->axis, b->axis, sizeof(a->axis)) &&
        !memcmp(&a->projection, &b->projection, sizeof(a->projection)) &&
        a->clear_color == b->clear_color && a->clear_depth == b->clear_depth &&
        a->clear_stencil == b->clear_stencil && a->clip_enabled == b->clip_enabled &&
        a->mirror == b->mirror && !memcmp(&a->color, &b->color, sizeof(a->color)) &&
        !memcmp(&a->depth, &b->depth, sizeof(a->depth)) &&
        !memcmp(&a->clip_plane, &b->clip_plane, sizeof(a->clip_plane)) && a->seat == b->seat;
}
bool qa_scene_world_q1_sky_begin(qa_scene_world *world, const qa_scene_world_input *input,
    qa_scene_frame *frame, qa_scene_q1_sky **out, qa_error *error)
{
    if (!world || !input || !frame || !out || *out || world->bsp.family != QA_BSP_Q1 ||
        !input->q1_sky_environment) return fail(error, "Q1 sky requires its actual world, view and environment");
    const qa_scene_q1_sky_environment *environment = input->q1_sky_environment;
    if (!isfinite(environment->quality) || !isfinite(environment->alpha) || !isfinite(environment->fog) ||
        !isfinite(environment->far_clip) || environment->far_clip <= 0 ||
        environment->quality >= (float)(INT_MAX / 2)) return fail(error, "Q1 sky controls exceed their native geometry domain");
    qa_scene_q1_sky *sky = qa_arena_alloc(&frame->storage, sizeof(*sky), _Alignof(qa_scene_q1_sky), error);
    if (!sky) return false;
    *sky = (qa_scene_q1_sky){.world = world, .frame = frame, .sequence = frame->sequence,
        .identity = world->identity, .revision = world->revision, .first = frame->command_count,
        .input = *input, .environment = *environment, .flat = {0, 0, 0, 1}};
    qa_scene_sky_bounds_reset(sky->bounds);
    /* Sky_LoadTexture publishes the last admitted classic sky layers globally. */
    const qawl_world *data = world->legacy_data;
    for (size_t i = 0; i < data->texture_count; ++i)
        if (data->textures[i].sky[0] && data->textures[i].sky[1]) {
            sky->layers[0] = data->textures[i].sky[0]; sky->layers[1] = data->textures[i].sky[1];
        }
    if (sky->layers[1]) {
        const qa_scene_image_level *level = sky->layers[1]->levels;
        const uint8_t *pixels = level->pixels;
        double sum[3] = {0}; size_t opaque = 0, count = (size_t)level->width * level->height;
        for (size_t i = 0; i < count; ++i) if (pixels[i * 4 + 3]) {
            for (unsigned c = 0; c < 3; ++c) sum[c] += pixels[i * 4 + c];
            ++opaque;
        }
        if (opaque) {
            double denominator = (double)opaque * 255.0;
            sky->flat = (qa_vec4){(float)(sum[0] / denominator),
                (float)(sum[1] / denominator), (float)(sum[2] / denominator), 1};
        }
    }
    if (input->fog.density > 0) sky->flat = (qa_vec4){input->fog.color.x, input->fog.color.y, input->fog.color.z, 1};
    *out = sky; return true;
}
bool qaw_q1_sky_collect(qa_scene_q1_sky *sky, const qa_scene_mesh *source,
    const qa_material_context *context, qa_scene_frame *frame, qa_error *error)
{
    if (!current(sky, frame) || !source || !context ||
        !same_view(&context->view, &sky->input.view))
        return fail(error, "Q1 sky footprint belongs to another actual view");
    q1_sky_polygon *polygon = qa_arena_alloc(&frame->storage, sizeof(*polygon), _Alignof(q1_sky_polygon), error);
    if (!polygon) return false;
    qa_scene_vertex *vertices; uint32_t *indices;
    *polygon = (q1_sky_polygon){0};
    if (!qa_effect_mesh(frame, source->vertex_count, source->index_count,
        &polygon->mesh, &vertices, &indices, error)) return false;
    for (size_t i = 0; i < source->vertex_count; ++i) {
        vertices[i] = source->vertices[i];
        vertices[i].position = qa_effect_point(context->model, vertices[i].position);
        vertices[i].color = sky->flat;
    }
    if (source->index_count) memcpy(indices, source->indices, source->index_count * sizeof(*indices));
    qa_effect_bounds(&polygon->mesh);
    if (!sky->environment.fast && !qa_scene_sky_clip(&polygon->mesh, 1, sky->input.view.origin, sky->bounds, error)) return false;
    if (sky->tail) sky->tail->next = polygon; else sky->head = polygon;
    sky->tail = polygon; return true;
}
static bool draw_mesh(qa_scene_q1_sky *sky, const qa_scene_mesh *mesh,
    const qa_scene_image *image, bool overlay, bool flat, qa_error *error)
{
    qa_scene_draw draw;
    qa_effect_draw(&draw, &sky->input.view, mesh, image, false);
    draw.state.blend_source = overlay ? QA_BLEND_SRC_ALPHA : QA_BLEND_ONE;
    draw.state.blend_destination = overlay ? QA_BLEND_ONE_MINUS_SRC_ALPHA : QA_BLEND_ZERO;
    draw.state.depth_test = flat ? QA_DEPTH_LEQUAL : QA_DEPTH_GEQUAL;
    draw.state.depth_write = flat;
    return qa_scene_frame_draw(sky->frame, &draw, error);
}
static bool colored_mesh(qa_scene_q1_sky *sky, const qa_scene_mesh *source,
    qa_vec4 color, unsigned layer, bool cloud, qa_scene_mesh *out, qa_error *error)
{
    qa_scene_vertex *vertices; uint32_t *indices;
    if (!qa_effect_mesh(sky->frame, source->vertex_count, source->index_count, out, &vertices, &indices, error)) return false;
    float scroll = (float)(sky->input.seconds * (layer ? 16 : 8));
    if (cloud) {
        if (!isfinite(scroll) || (double)scroll < INT_MIN || (double)scroll > INT_MAX)
            return fail(error, "Q1 cloud scroll exceeds the source integer domain");
        scroll -= (float)((int)scroll & ~127);
    }
    for (size_t i = 0; i < source->vertex_count; ++i) {
        vertices[i] = source->vertices[i]; vertices[i].color = color;
        if (cloud) {
            qa_vec3 direction = qa_vec_sub(vertices[i].position, sky->input.view.origin);
            direction.z *= 3;
            float length = qa_vec_length(direction), scale = length > 0 ? 378 / length : 0;
            vertices[i].texcoord = (qa_vec2){(float)(scroll + direction.x * scale) / 128,
                (float)(scroll + direction.y * scale) / 128};
        }
    }
    if (source->index_count) memcpy(indices, source->indices, source->index_count * sizeof(*indices));
    qa_effect_bounds(out); return true;
}
static bool fog_mesh(qa_scene_q1_sky *sky, const qa_scene_mesh *mesh, qa_error *error)
{
    if (!(sky->input.fog.density > 0 && sky->environment.fog > 0)) return true;
    qa_scene_mesh overlay;
    qa_vec4 color = {sky->input.fog.color.x, sky->input.fog.color.y,
        sky->input.fog.color.z, fminf(1, sky->environment.fog)};
    return colored_mesh(sky, mesh, color, 0, false, &overlay, error) &&
        draw_mesh(sky, &overlay, NULL, true, false, error);
}
static bool face_mesh(qa_scene_q1_sky *sky, unsigned face, const qa_scene_sky_bounds *bounds,
    bool boxed, qa_scene_mesh *mesh, qa_error *error)
{
    int quality = boxed ? 1 : sky->environment.quality < 1 ? 1 : (int)sky->environment.quality;
    int vertical = !boxed && face < 4 ? quality * 2 : quality;
    uint64_t cells = (uint64_t)quality * (uint32_t)vertical;
    if (cells > UINT32_MAX / 4 || cells > SIZE_MAX / 6)
        return fail(error, "Q1 sky subdivision exceeds frame geometry storage");
    float qi = 1.0f / (float)quality, qj = 1.0f / (float)vertical;
    size_t count = 0;
    for (int i = 0; i < quality; ++i) for (int j = 0; j < vertical; ++j) {
        float s = (float)i * qi, t = (float)j * qj;
        if (!boxed && (s < bounds->min_s / 2 + .5f - qi || s > bounds->max_s / 2 + .5f ||
            t < bounds->min_t / 2 + .5f - qj || t > bounds->max_t / 2 + .5f)) continue;
        ++count;
    }
    qa_scene_vertex *vertices; uint32_t *indices;
    if (!qa_effect_mesh(sky->frame, count * 4, count * 6, mesh, &vertices, &indices, error)) return false;
    float radius = (float)(sky->environment.far_clip / sqrt(3.0));
    const qa_scene_image *image = boxed ? sky->environment.images[face] : NULL;
    if (boxed && (!image || !image->level_count || !image->levels[0].width || !image->levels[0].height))
        return fail(error, "Q1 boxed sky lost its actual face image");
    size_t cell = 0;
    for (int i = 0; i < quality; ++i) for (int j = 0; j < vertical; ++j) {
        float s = (float)i * qi, t = (float)j * qj;
        if (!boxed && (s < bounds->min_s / 2 + .5f - qi || s > bounds->max_s / 2 + .5f ||
            t < bounds->min_t / 2 + .5f - qj || t > bounds->max_t / 2 + .5f)) continue;
        float next_s = (float)(i + 1) * qi, next_t = (float)(j + 1) * qj;
        float horizontal[4] = {s, s, next_s, next_s};
        float height[4] = {t, next_t, next_t, t};
        uint32_t base = (uint32_t)(cell * 4);
        for (unsigned v = 0; v < 4; ++v) {
            qa_scene_vertex *vertex = vertices + base + v;
            qa_vec3 direction = qa_effect_sky_vector(face, horizontal[v] * 2 - 1, height[v] * 2 - 1, radius);
            vertex->position = qa_vec_add(sky->input.view.origin, direction);
            vertex->color = (qa_vec4){1, 1, 1, 1};
            if (boxed) {
                float w = (float)image->levels[0].width, h = (float)image->levels[0].height;
                vertex->texcoord = (qa_vec2){horizontal[v] * (w - 1) / w + .5f / w,
                    1 - (height[v] * (h - 1) / h + .5f / h)};
            }
        }
        const uint32_t quad[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        memcpy(indices + cell * 6, quad, sizeof(quad)); ++cell;
    }
    qa_effect_bounds(mesh); return true;
}
bool qa_scene_world_q1_sky_finish(qa_scene_q1_sky *sky, qa_scene_frame *frame, qa_error *error)
{
    if (!current(sky, frame)) return fail(error, "Q1 sky finish lost its actual frame and world");
    if (!sky->head) { sky->finished = true; return true; }
    if (sky->first >= frame->command_count || frame->commands[sky->first].kind != QA_SCENE_COMMAND_VIEW ||
        !same_view(&frame->commands[sky->first].data.view, &sky->input.view))
        return fail(error, "Q1 sky has no reached view command");
    size_t old_count = frame->command_count;
    for (q1_sky_polygon *polygon = sky->head; polygon; polygon = polygon->next)
        if (!draw_mesh(sky, &polygon->mesh, NULL, false, true, error)) goto failed;
    if (!sky->environment.fast && !(sky->input.fog.density > 0 && sky->environment.fog >= 1)) {
        if (!sky->environment.boxed && (!sky->layers[0] || !sky->layers[1])) {
            fail(error, "Q1 scrolling sky lost its admitted classic layers"); goto failed;
        }
        for (unsigned face = 0; face < 6; ++face) {
            const qa_scene_sky_bounds *bounds = sky->bounds + face;
            if (!(bounds->min_s < bounds->max_s && bounds->min_t < bounds->max_t)) continue;
            qa_scene_mesh mesh;
            if (!face_mesh(sky, face, bounds, sky->environment.boxed, &mesh, error)) goto failed;
            if (sky->environment.boxed) {
                if (!draw_mesh(sky, &mesh, sky->environment.images[face], false, false, error)) goto failed;
            } else for (unsigned layer = 0; layer < 2; ++layer) {
                qa_scene_mesh clouds;
                if (!colored_mesh(sky, &mesh, (qa_vec4){1, 1, 1,
                    layer && sky->environment.alpha < 1 ? sky->environment.alpha : 1}, layer, true, &clouds, error) ||
                    !draw_mesh(sky, &clouds, sky->layers[layer], layer != 0, false, error)) goto failed;
            }
            if (!fog_mesh(sky, &mesh, error)) goto failed;
        }
    }
    size_t added = frame->command_count - old_count, insertion = sky->first + 1;
    if (added > SIZE_MAX / sizeof(qa_scene_command)) { fail(error, "Q1 sky command storage overflow"); goto failed; }
    qa_scene_command *commands = qa_arena_alloc(&frame->storage, added * sizeof(*commands), _Alignof(qa_scene_command), error);
    if (!commands) goto failed;
    memcpy(commands, frame->commands + old_count, added * sizeof(*commands));
    memmove(frame->commands + insertion + added, frame->commands + insertion,
        (old_count - insertion) * sizeof(*commands));
    memcpy(frame->commands + insertion, commands, added * sizeof(*commands));
    for (size_t i = 0; i < frame->group_count; ++i)
        if (frame->groups[i].first >= insertion) frame->groups[i].first += added;
    sky->world->sky_drawn = true; sky->finished = true; return true;
failed:
    frame->command_count = old_count; return false;
}
