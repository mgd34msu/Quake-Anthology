#include "tools_internal.h"
#include "save_internal.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct shape_lines { qa_debug_line *lines; size_t count, capacity; qa_scene_vec4 color; bool depth; } shape_lines;
static bool finite_color(qa_scene_vec4 c) { return isfinite(c.x) && isfinite(c.y) && isfinite(c.z) && isfinite(c.w); }
static bool valid_line(qa_debug_line line) { return qa_vec_finite(line.start) && qa_vec_finite(line.end) && finite_color(line.color); }
static void line(shape_lines *s, qa_vec3 a, qa_vec3 b, qa_scene_vec4 color) {
    /* Counts are fixed by the bounded source tessellation below. */
    if (s->count < s->capacity) s->lines[s->count++] = (qa_debug_line){a, b, color, s->depth};
}
static void arrow(shape_lines *s, qa_vec3 start, qa_vec3 end, float size, qa_scene_vec4 cap_color) {
    qa_vec3 delta = qa_vec_sub(end, start); float length = qa_vec_length(delta);
    qa_vec3 dir = qa_vec_normalize(delta), apex = length > size ? qa_vec_add(start, qa_vec_scale(dir, length - size)) : end;
    if (length > size) line(s, start, apex, s->color);
    float extent = length > size ? size : length;
    qa_vec3 tip = qa_vec_add(apex, qa_vec_scale(dir, extent)), rotated = {dir.z, -dir.x, dir.y};
    qa_vec3 right = qa_vec_normalize(qa_vec_sub(rotated, qa_vec_scale(dir, qa_vec_dot(rotated, dir))));
    line(s, apex, tip, cap_color); line(s, qa_vec_add(apex, qa_vec_scale(right, extent)), tip, cap_color);
    line(s, qa_vec_add(apex, qa_vec_scale(right, -extent)), tip, cap_color);
}
static qa_vec3 ring(qa_vec3 origin, float radius, int i, int count, float z) {
    double angle = i * 6.2831853071795864769 / count;
    return qa_v3(origin.x + (float)cos(angle) * radius, origin.y + (float)sin(angle) * radius, z);
}
static qa_vec3 sphere_ring(qa_vec3 origin, float radius, int stack, int slice, int stacks, int slices) {
    double phi = 3.14159265358979323846 * (stack + 1) / stacks, theta = 6.2831853071795864769 * slice / slices;
    return qa_vec_add(origin, qa_vec_scale(qa_v3((float)(sin(phi) * cos(theta)), (float)(sin(phi) * sin(theta)), (float)cos(phi)), radius));
}
static qa_vec3 corner(qa_bounds bounds, int i, float z) {
    return qa_v3(i > 1 ? bounds.mins.x : bounds.maxs.x, (i + 1) % 4 > 1 ? bounds.mins.y : bounds.maxs.y, z);
}
bool qa_debug_shape_lines(const qa_debug_shape *shape, qa_scene_vec4 color, bool depth,
                           qa_arena *scratch, const qa_debug_line **out, size_t *count, qa_error *error) {
    if (!shape || !scratch || !out || !count || !finite_color(color)) return tools_fail(error, "invalid debug shape output");
    shape_lines s = {.capacity = 608, .color = color, .depth = depth};
    s.lines = qa_arena_alloc(scratch, s.capacity * sizeof(*s.lines), _Alignof(qa_debug_line), error);
    if (!s.lines) return false;
    switch (shape->kind) {
    case QA_DEBUG_LINE: line(&s, shape->data.line.start, shape->data.line.end, color); break;
    case QA_DEBUG_POINT: {
        float h = shape->data.point.size * .5f;
        if (!isfinite(h)) return tools_fail(error, "invalid debug point size");
        for (int i = 0; i < 3; ++i) {
            qa_vec3 axis = qa_v3(i == 0 ? h : 0, i == 1 ? h : 0, i == 2 ? h : 0);
            line(&s, qa_vec_sub(shape->data.point.origin, axis), qa_vec_add(shape->data.point.origin, axis), color);
        }
        break;
    }
    case QA_DEBUG_BOUNDS: {
        qa_bounds b = shape->data.bounds;
        for (int i = 0; i < 4; ++i) {
            line(&s, corner(b, i, b.mins.z), corner(b, i, b.maxs.z), color);
            line(&s, corner(b, i, b.mins.z), corner(b, (i + 1) % 4, b.mins.z), color);
            line(&s, corner(b, i, b.maxs.z), corner(b, (i + 1) % 4, b.maxs.z), color);
        }
        break;
    }
    case QA_DEBUG_CIRCLE: case QA_DEBUG_CYLINDER: {
        qa_vec3 origin = shape->kind == QA_DEBUG_CIRCLE ? shape->data.round.origin : shape->data.cylinder.origin;
        float radius = shape->kind == QA_DEBUG_CIRCLE ? shape->data.round.radius : shape->data.cylinder.radius;
        float h = shape->kind == QA_DEBUG_CIRCLE ? 0 : shape->data.cylinder.half_height;
        if (!isfinite(radius) || !isfinite(h) || !qa_vec_finite(origin)) return tools_fail(error, "invalid debug circle or cylinder");
        double slice_count = trunc(fmin(5 + radius / 8, 16));
        if (slice_count <= 0) break;
        int slices = (int)slice_count;
        for (int i = 0; i < slices; ++i) {
            line(&s, ring(origin, radius, i, slices, origin.z - h), ring(origin, radius, (i + 1) % slices, slices, origin.z - h), color);
            if (shape->kind == QA_DEBUG_CYLINDER) {
                line(&s, ring(origin, radius, i, slices, origin.z + h), ring(origin, radius, (i + 1) % slices, slices, origin.z + h), color);
                line(&s, ring(origin, radius, i, slices, origin.z - h), ring(origin, radius, i, slices, origin.z + h), color);
            }
        }
        break;
    }
    case QA_DEBUG_SPHERE: {
        qa_vec3 origin = shape->data.round.origin; float radius = shape->data.round.radius;
        if (!isfinite(radius) || !qa_vec_finite(origin)) return tools_fail(error, "invalid debug sphere");
        double slice_count = trunc(fmin(6 + radius / 32, 16));
        if (slice_count <= 0) break;
        int stacks = (int)trunc(fmin(4 + radius / 32, 10)), slices = (int)slice_count;
        if (!stacks) return tools_fail(error, "debug sphere has a zero stack divisor");
        qa_vec3 north = qa_vec_add(origin, qa_v3(0, 0, radius)), south = qa_vec_sub(origin, qa_v3(0, 0, radius));
        for (int i = 0; i < slices; ++i) {
            int next = (i + 1) % slices;
            qa_vec3 a = sphere_ring(origin, radius, 0, next, stacks, slices), b = sphere_ring(origin, radius, 0, i, stacks, slices);
            line(&s, north, a, color); line(&s, a, b, color); line(&s, b, north, color);
            a = sphere_ring(origin, radius, stacks - 2, i, stacks, slices); b = sphere_ring(origin, radius, stacks - 2, next, stacks, slices);
            line(&s, south, a, color); line(&s, a, b, color); line(&s, b, south, color);
        }
        for (int j = 0; j < stacks - 2; ++j) for (int i = 0; i < slices; ++i) {
            int next = (i + 1) % slices;
            qa_vec3 a = sphere_ring(origin, radius, j, i, stacks, slices), b = sphere_ring(origin, radius, j, next, stacks, slices);
            qa_vec3 c = sphere_ring(origin, radius, j + 1, next, stacks, slices), d = sphere_ring(origin, radius, j + 1, i, stacks, slices);
            line(&s, a, b, color); line(&s, b, c, color); line(&s, c, d, color); line(&s, d, a, color);
        }
        break;
    }
    case QA_DEBUG_ARROW:
        if (!isfinite(shape->data.arrow.size) || !finite_color(shape->data.arrow.cap_color)) return tools_fail(error, "invalid debug arrow");
        arrow(&s, shape->data.arrow.start, shape->data.arrow.end, shape->data.arrow.size, shape->data.arrow.cap_color); break;
    case QA_DEBUG_RAY:
        if (!isfinite(shape->data.ray.length) || !isfinite(shape->data.ray.size)) return tools_fail(error, "invalid debug ray");
        arrow(&s, shape->data.ray.origin, qa_vec_add(shape->data.ray.origin, qa_vec_scale(shape->data.ray.direction, shape->data.ray.length)), shape->data.ray.size, color); break;
    default: return tools_fail(error, "unknown debug shape kind");
    }
    for (size_t i = 0; i < s.count; ++i) if (!valid_line(s.lines[i])) return tools_fail(error, "debug geometry exceeds finite vector range");
    *out = s.lines; *count = s.count; return true;
}

typedef struct timed_line { qa_debug_line line; double expires; uint64_t first_frame; bool instant, presented; } timed_line;
struct qa_debug_store { timed_line *lines; size_t count, capacity; bool pending_restore; };
void tools_debug_pending(qa_debug_store *store) { store->pending_restore = true; }
void tools_debug_exchange(qa_debug_store *stable, qa_debug_store *candidate) {
    qa_debug_store old = *stable; *stable = *candidate; *candidate = old;
}
bool tools_debug_fields(qa_source_save_io *io, qa_debug_store **holder) {
    qa_debug_store *store = *holder;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        store = calloc(1, sizeof *store);
        if (!store) { io->failed = true; qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating restored debug store"); return false; }
        *holder = store;
    }
    if (!qa_source_save_count(io, &store->capacity, SIZE_MAX / sizeof *store->lines) || !store->capacity ||
        !qa_source_save_count(io, &store->count, store->capacity)) return tool_save_fail(io, "invalid debug continuation capacity");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        store->lines = calloc(store->capacity, sizeof *store->lines);
        if (!store->lines) return tool_save_fail(io, "allocating debug continuation lines");
    }
    for (size_t i = 0; i < store->count; ++i) {
        timed_line *v = &store->lines[i]; qa_scene_vec4 *color = &v->line.color;
        if (!qa_source_save_vec3(io, &v->line.start) || !qa_source_save_vec3(io, &v->line.end) ||
            !qa_source_save_f32(io, &color->x) || !qa_source_save_f32(io, &color->y) ||
            !qa_source_save_f32(io, &color->z) || !qa_source_save_f32(io, &color->w) ||
            !qa_source_save_bool(io, &v->line.depth_test) || !qa_source_save_f64(io, &v->expires) ||
            !qa_source_save_u64(io, &v->first_frame) || !qa_source_save_bool(io, &v->instant) ||
            !qa_source_save_bool(io, &v->presented) || !valid_line(v->line) || !isfinite(v->expires) ||
            v->expires < 0 || v->expires > UINT32_MAX || v->instant != (v->expires == 0))
            return tool_save_fail(io, "invalid debug line continuation");
    }
    return true;
}
static bool source_milliseconds(double value, uint32_t *out, qa_error *error) {
    if (!isfinite(value) || value < -0x1p63 || value >= 0x1p63) {
        (void)tools_fail(error, "debug clock exceeds its native millisecond field");
        return false;
    }
    *out = (uint32_t)(int64_t)value;
    return true;
}
bool qa_debug_store_create(size_t capacity, qa_debug_store **out, qa_error *error) {
    if (!out || !capacity || capacity > SIZE_MAX / sizeof(timed_line)) return tools_fail(error, "invalid debug line capacity");
    qa_debug_store *store = calloc(1, sizeof *store);
    if (!store) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating debug line store"); return false; }
    store->lines = calloc(capacity, sizeof(*store->lines));
    if (!store->lines) { free(store); qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating debug lines"); return false; }
    store->capacity = capacity; *out = store; return true;
}
void qa_debug_store_destroy(qa_debug_store *store) { if (store) { free(store->lines); free(store); } }
void qa_debug_store_clear(qa_debug_store *store) { if (store && !store->pending_restore) store->count = 0; }
bool qa_debug_store_submit(qa_debug_store *store, const qa_debug_line *lines, size_t count, double server_ms, uint32_t lifetime, qa_error *error) {
    if (!store || store->pending_restore || (count && !lines) || !isfinite(server_ms)) return tools_fail(error, "invalid or pending debug line submission");
    for (size_t i = 0; i < count; ++i) if (!valid_line(lines[i])) return tools_fail(error, "invalid debug line geometry");
    uint32_t now;
    if (!source_milliseconds(server_ms, &now, error)) return false;
    uint32_t deadline = lifetime ? now + lifetime : 0;
    size_t keep = 0;
    for (size_t i = 0; i < store->count; ++i) if (store->lines[i].instant || store->lines[i].expires > now) store->lines[keep++] = store->lines[i];
    size_t skip = count > store->capacity ? count - store->capacity : 0, added = count - skip;
    size_t retained = keep < store->capacity - added ? keep : store->capacity - added;
    if (retained) memmove(store->lines, store->lines + keep - retained, retained * sizeof(*store->lines));
    for (size_t i = 0; i < added; ++i) store->lines[retained + i] = (timed_line){.line = lines[skip + i], .expires = deadline, .instant = deadline == 0};
    store->count = retained + added; return true;
}
bool qa_debug_store_snapshot(qa_debug_store *store, double server_ms, uint64_t frame,
                              qa_arena *scratch, const qa_debug_line **out, size_t *count, qa_error *error) {
    if (!store || store->pending_restore || !scratch || !out || !count || !isfinite(server_ms)) return tools_fail(error, "invalid or pending debug line snapshot");
    uint32_t now;
    if (!source_milliseconds(server_ms, &now, error)) return false;
    qa_debug_line *lines = store->count ? qa_arena_alloc(scratch, store->count * sizeof(*lines), _Alignof(qa_debug_line), error) : NULL;
    if (store->count && !lines) return false;
    size_t keep = 0;
    for (size_t i = 0; i < store->count; ++i) {
        timed_line value = store->lines[i];
        if (value.instant ? value.presented && value.first_frame != frame : value.expires <= now) continue;
        if (value.instant && !value.presented) { value.presented = true; value.first_frame = frame; }
        store->lines[keep] = value; lines[keep++] = value.line;
    }
    store->count = keep; *out = lines; *count = keep; return true;
}
bool qa_debug_draw(qa_scene_frame *frame, const qa_scene_view *view, const qa_scene_image *white,
                    const qa_debug_line *lines, size_t count, float width, qa_error *error) {
    if (!frame || !view || !white || (count && !lines) || count > UINT32_MAX / 2 ||
        count > SIZE_MAX / (2 * sizeof(qa_scene_vertex)) || !isfinite(width) || width <= 0)
        return tools_fail(error, "invalid debug line draw");
    for (size_t i = 0; i < count; ++i) if (!valid_line(lines[i])) return tools_fail(error, "invalid debug line geometry");
    size_t checkpoint = frame->command_count;
    for (size_t begin = 0; begin < count;) {
        size_t end = begin + 1;
        while (end < count && lines[end].depth_test == lines[begin].depth_test) ++end;
        size_t n = (end - begin) * 2;
        qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage, n * sizeof(*vertices), _Alignof(qa_scene_vertex), error);
        uint32_t *indices = qa_arena_alloc(&frame->storage, n * sizeof(*indices), _Alignof(uint32_t), error);
        if (!vertices || !indices) goto failed;
        qa_bounds bounds = {lines[begin].start, lines[begin].start};
        for (size_t i = 0; i < end - begin; ++i) {
            qa_debug_line l = lines[begin + i];
            vertices[i * 2] = (qa_scene_vertex){.position = l.start, .color = l.color};
            vertices[i * 2 + 1] = (qa_scene_vertex){.position = l.end, .color = l.color};
            indices[i * 2] = (uint32_t)(i * 2); indices[i * 2 + 1] = (uint32_t)(i * 2 + 1);
            bounds = qa_bounds_union(bounds, (qa_bounds){l.start, l.start}); bounds = qa_bounds_union(bounds, (qa_bounds){l.end, l.end});
        }
        qa_scene_draw draw = {.mesh = {.vertices = vertices, .indices = indices, .vertex_count = n, .index_count = n, .bounds = bounds, .primitive = QA_SCENE_LINES},
            .textures = {white}, .texture_count = 1, .environment = QA_TEXTURE_MODULATE, .lighting = QA_LIGHT_VERTEX, .shade_scale = 1};
        qa_scene_matrix_identity(&draw.model); draw.mvp = qa_scene_matrix_multiply(view->projection, qa_scene_view_matrix(view));
        qa_scene_state_default(&draw.state); draw.state.blend_source = QA_BLEND_SRC_ALPHA; draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
        draw.state.depth_test = lines[begin].depth_test ? QA_DEPTH_LEQUAL : QA_DEPTH_ALWAYS; draw.state.depth_write = false; draw.state.line_width = width;
        if (!qa_scene_frame_draw(frame, &draw, error)) goto failed;
        begin = end;
    }
    return true;
failed:
    frame->command_count = checkpoint; return false;
}
