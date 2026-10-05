#include "tools_internal.h"
#include "qa/binary.h"
#include <math.h>
#include <string.h>

static bool argument(const qa_native_import_call *call, size_t index, qa_native_value_type type, qa_error *error) {
    return (index < call->argument_count && call->arguments[index].type == type) ||
        tools_fail(error, "native debug import argument has the wrong source type");
}
static bool vector(qa_native_instance *instance, const qa_native_import_call *call, size_t index,
                     qa_vec3 *out, qa_error *error) {
    if (!argument(call, index, QA_NATIVE_ADDRESS, error) || !call->arguments[index].as.address)
        return tools_fail(error, "native debug vector requires a source pointer");
    uint8_t bytes[12];
    if (!qa_native_read(instance, call->arguments[index].as.address, bytes, sizeof bytes, error)) return false;
    uint32_t words[3] = {qa_load_u32le(bytes), qa_load_u32le(bytes + 4), qa_load_u32le(bytes + 8)};
    float values[3]; memcpy(values, words, sizeof values); *out = qa_v3(values[0], values[1], values[2]);
    return qa_vec_finite(*out) || tools_fail(error, "native debug vector must be finite");
}
static bool scalar(const qa_native_import_call *call, size_t index, float *out, qa_error *error) {
    if (!argument(call, index, QA_NATIVE_F32, error)) return false;
    *out = call->arguments[index].as.f32;
    return isfinite(*out) || tools_fail(error, "native debug source float must be finite");
}
static bool color(qa_native_instance *instance, const qa_native_import_call *call, size_t index,
                    qa_scene_vec4 *out, qa_error *error) {
    if (!argument(call, index, QA_NATIVE_ADDRESS, error) || !call->arguments[index].as.address)
        return tools_fail(error, "native debug color requires a source pointer");
    uint8_t bytes[4];
    if (!qa_native_read(instance, call->arguments[index].as.address, bytes, sizeof bytes, error)) return false;
    *out = (qa_scene_vec4){bytes[0] / 255.0f, bytes[1] / 255.0f, bytes[2] / 255.0f, bytes[3] / 255.0f}; return true;
}
bool qa_debug_native_q2(qa_native_instance *instance, const qa_native_import_call *call, qa_arena *scratch,
                         const qa_debug_line **out, size_t *count, uint32_t *lifetime,
                         bool *handled, qa_error *error) {
    if (!instance || !call || !call->name || !scratch || !out || !count || !lifetime || !handled ||
        (call->argument_count && !call->arguments)) return tools_fail(error, "invalid native debug import output");
    *handled = false;
    if (call->profile != QA_NATIVE_Q2_GAME_API2023) return true;
    static const struct { const char *name; qa_debug_shape_kind kind; size_t color, lifetime; } rows[] = {
        {"Draw_Line", QA_DEBUG_LINE, 2, 3}, {"Draw_Point", QA_DEBUG_POINT, 2, 3},
        {"Draw_Circle", QA_DEBUG_CIRCLE, 2, 3}, {"Draw_Bounds", QA_DEBUG_BOUNDS, 2, 3},
        {"Draw_Sphere", QA_DEBUG_SPHERE, 2, 3}, {"Draw_Cylinder", QA_DEBUG_CYLINDER, 3, 4},
        {"Draw_Ray", QA_DEBUG_RAY, 4, 5}, {"Draw_Arrow", QA_DEBUG_ARROW, 3, 5}
    };
    size_t row = 0;
    while (row < sizeof rows / sizeof rows[0] && strcmp(rows[row].name, call->name)) ++row;
    if (row == sizeof rows / sizeof rows[0]) return true;
    *handled = true;
    if (call->argument_count != rows[row].lifetime + 2) return tools_fail(error, "native debug import has the wrong source argument count");
    qa_debug_shape shape = {.kind = rows[row].kind}; bool ok = false;
    switch (shape.kind) {
    case QA_DEBUG_LINE:
        ok = vector(instance, call, 0, &shape.data.line.start, error) && vector(instance, call, 1, &shape.data.line.end, error); break;
    case QA_DEBUG_POINT:
        ok = vector(instance, call, 0, &shape.data.point.origin, error) && scalar(call, 1, &shape.data.point.size, error); break;
    case QA_DEBUG_CIRCLE: case QA_DEBUG_SPHERE:
        ok = vector(instance, call, 0, &shape.data.round.origin, error) && scalar(call, 1, &shape.data.round.radius, error); break;
    case QA_DEBUG_BOUNDS:
        ok = vector(instance, call, 0, &shape.data.bounds.mins, error) && vector(instance, call, 1, &shape.data.bounds.maxs, error); break;
    case QA_DEBUG_CYLINDER:
        ok = vector(instance, call, 0, &shape.data.cylinder.origin, error) && scalar(call, 1, &shape.data.cylinder.half_height, error) && scalar(call, 2, &shape.data.cylinder.radius, error); break;
    case QA_DEBUG_RAY:
        ok = vector(instance, call, 0, &shape.data.ray.origin, error) && vector(instance, call, 1, &shape.data.ray.direction, error) && scalar(call, 2, &shape.data.ray.length, error) && scalar(call, 3, &shape.data.ray.size, error); break;
    case QA_DEBUG_ARROW:
        ok = vector(instance, call, 0, &shape.data.arrow.start, error) && vector(instance, call, 1, &shape.data.arrow.end, error) && scalar(call, 2, &shape.data.arrow.size, error) && color(instance, call, 4, &shape.data.arrow.cap_color, error); break;
    default: return tools_fail(error, "unknown admitted native debug shape");
    }
    float seconds; qa_scene_vec4 rgba;
    if (!ok || !color(instance, call, rows[row].color, &rgba, error) ||
        !scalar(call, rows[row].lifetime, &seconds, error) ||
        !argument(call, rows[row].lifetime + 1, QA_NATIVE_U8, error)) return false;
    float product = seconds * 1000.0f;
    if (!isfinite(product) || product < -0x1p63f || product >= 0x1p63f)
        return tools_fail(error, "native debug lifetime exceeds its unsigned millisecond field");
    uint32_t milliseconds = (uint32_t)(int64_t)product;
    if (!qa_debug_shape_lines(&shape, rgba, call->arguments[rows[row].lifetime + 1].as.u8 != 0, scratch, out, count, error)) return false;
    *lifetime = milliseconds; return true;
}
