#include "camera_internal.h"
#include "qa/text.h"
#include "qa/builtin.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool camera_fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message); return false;
}
void *camera_array(qa_arena *arena, size_t count, size_t item, size_t alignment, qa_error *error) {
    if (!count) return NULL;
    if (count > SIZE_MAX / item) { qa_error_set(error, QA_ERROR_MEMORY, 0, "camera array size overflow"); return NULL; }
    return qa_arena_alloc(arena, count * item, alignment, error);
}
char *camera_text(qa_arena *arena, const char *text, qa_error *error) {
    if (!text) text = "";
    size_t n = strlen(text);
    if (n == SIZE_MAX) { camera_fail(error, "camera text too long"); return NULL; }
    char *out = qa_arena_alloc(arena, n + 1, 1, error);
    if (out) memcpy(out, text, n + 1);
    return out;
}
static double distance_between(qa_vec3 a, qa_vec3 b) {
    return hypot(hypot((double)a.x - b.x, (double)a.y - b.y), (double)a.z - b.z);
}
static bool path_copy(qa_camera_document *d, const qa_camera_path *path, qa_camera_path *out,
                      camera_curve *curve, qa_error *error) {
    if (path->kind < QA_CAMERA_FIXED || path->kind > QA_CAMERA_SPLINE || !path->name ||
        !isfinite(path->time_ms) || path->time_ms < 0 || !isfinite(path->base_velocity) ||
        (path->velocity_count && !path->velocities)) return camera_fail(error, "invalid camera path");
    *out = *path; out->name = camera_text(&d->storage, path->name, error);
    if (!out->name) return false;
    qa_camera_velocity *velocities = camera_array(&d->storage, path->velocity_count, sizeof(*velocities), _Alignof(qa_camera_velocity), error);
    if (path->velocity_count && !velocities) return false;
    for (size_t i = 0; i < path->velocity_count; ++i) {
        qa_camera_velocity v = path->velocities[i];
        if (!isfinite(v.start_ms) || !isfinite(v.duration_ms) || !isfinite(v.speed) ||
            v.start_ms < 0 || v.duration_ms < 0 || v.speed < 0) return camera_fail(error, "invalid camera velocity");
        velocities[i] = v;
    }
    out->velocities = velocities;
    size_t count = 1;
    if (path->kind == QA_CAMERA_INTERPOLATED) count = 2;
    else if (path->kind == QA_CAMERA_SPLINE) {
        size_t n = path->position.spline.count;
        float granularity = path->position.spline.granularity;
        if (n < 4 || !path->position.spline.points || !isfinite(granularity) || granularity < .0001f || granularity > 1)
            return camera_fail(error, "invalid camera spline granularity or points");
        qa_vec3 *control = camera_array(&d->storage, n, sizeof(*control), _Alignof(qa_vec3), error);
        if (!control) return false;
        for (size_t i = 0; i < n; ++i) {
            if (!qa_vec_finite(path->position.spline.points[i])) return camera_fail(error, "invalid camera spline point");
            control[i] = path->position.spline.points[i];
        }
        out->position.spline.points = control;
        size_t per_span = 0;
        for (float t = 0; t < 1.001f; t += granularity) ++per_span;
        if (n - 3 > SIZE_MAX / per_span) return camera_fail(error, "camera spline sampling overflow");
        count = (n - 3) * per_span;
    }
    curve->points = camera_array(&d->storage, count, sizeof(*curve->points), _Alignof(qa_vec3), error);
    curve->distances = camera_array(&d->storage, count, sizeof(*curve->distances), _Alignof(double), error);
    if (!curve->points || !curve->distances) return false;
    curve->count = count;
    if (path->kind == QA_CAMERA_FIXED) {
        if (!qa_vec_finite(path->position.fixed)) return camera_fail(error, "invalid fixed camera point");
        curve->points[0] = path->position.fixed;
    } else if (path->kind == QA_CAMERA_INTERPOLATED) {
        if (!qa_vec_finite(path->position.interpolated.start) || !qa_vec_finite(path->position.interpolated.end))
            return camera_fail(error, "invalid interpolated camera points");
        curve->points[0] = path->position.interpolated.start; curve->points[1] = path->position.interpolated.end;
    } else {
        size_t at = 0;
        const qa_vec3 *points = out->position.spline.points;
        for (size_t i = 3; i < out->position.spline.count; ++i) {
            for (float t = 0; t < 1.001f; t += out->position.spline.granularity) {
                float t2 = t * t, t3 = t2 * t, s = 1 - t;
                float weights[4] = {s * s * s / 6, (3 * t3 - 6 * t2 + 4) / 6,
                    (-3 * t3 + 3 * t2 + 3 * t + 1) / 6, t3 / 6};
                qa_vec3 point = {0};
                for (size_t j = 0; j < 4; ++j) point = qa_vec_add(point, qa_vec_scale(points[i - 3 + j], weights[j]));
                if (!qa_vec_finite(point)) return camera_fail(error, "camera spline exceeds vector range");
                curve->points[at++] = point;
            }
        }
    }
    curve->distances[0] = 0;
    for (size_t i = 1; i < count; ++i) {
        curve->distance += distance_between(curve->points[i - 1], curve->points[i]);
        if (!isfinite(curve->distance)) return camera_fail(error, "camera path distance overflow");
        curve->distances[i] = curve->distance;
    }
    return true;
}
bool camera_number(qa_bytes text, bool empty, double *out, qa_error *error) {
    size_t cursor = 0, start = 0, end = 0; uint32_t scalar; bool leading = true;
    while (qa_utf8_next(text, &cursor, &scalar)) {
        if (leading && qa_unicode_whitespace(scalar)) start = cursor;
        else { leading = false; if (!qa_unicode_whitespace(scalar)) end = cursor; }
    }
    text = (qa_bytes){text.data ? text.data + start : NULL, end >= start ? end - start : 0};
    if (!text.size) { if (empty) { *out = 0; return true; } return camera_fail(error, "empty camera number"); }
    unsigned base = text.size >= 2 && text.data[0] == '0' ?
        text.data[1] == 'b' || text.data[1] == 'B' ? 2 : text.data[1] == 'o' || text.data[1] == 'O' ? 8 : 0 : 0;
    if (base) {
        if (text.size == 2) return camera_fail(error, "invalid camera number");
        double number = 0;
        for (size_t i = 2; i < text.size; ++i) {
            unsigned digit = (unsigned)text.data[i] - '0';
            if (digit >= base) return camera_fail(error, "invalid camera number");
            number = number * base + digit;
        }
        if (!isfinite(number)) return camera_fail(error, "camera number exceeds native range");
        *out = number; return true;
    }
    return qa_parse_number(text, out, error) && (isfinite(*out) || camera_fail(error, "invalid camera number"));
}
static bool wait_seconds(const char *text, double *out, qa_error *error) {
    if (!camera_number((qa_bytes){(const uint8_t *)text, strlen(text)}, true, out, error) || *out < 0)
        return camera_fail(error, "invalid camera wait duration");
    return true;
}
bool qa_camera_create(const qa_camera_definition *source, qa_camera_document **out, qa_error *error) {
    if (!source || !out || !isfinite(source->seconds) || source->seconds <= 0 ||
        !isfinite(source->fov.time_ms) || source->fov.time_ms < 0 ||
        !isfinite(source->fov.value) || source->fov.value <= 0 || source->fov.value >= 180 ||
        !isfinite(source->fov.start) || source->fov.start <= 0 || source->fov.start >= 180 ||
        !isfinite(source->fov.end) || source->fov.end <= 0 || source->fov.end >= 180 ||
        (source->target_count && !source->targets) || (source->event_count && !source->events))
        return camera_fail(error, "invalid camera definition");
    qa_camera_document *d = calloc(1, sizeof *d);
    if (!d) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating camera document"); return false; }
    d->references = 1; d->definition = *source;
    qa_camera_path *targets = camera_array(&d->storage, source->target_count, sizeof(*targets), _Alignof(qa_camera_path), error);
    d->targets = camera_array(&d->storage, source->target_count, sizeof(*d->targets), _Alignof(camera_curve), error);
    qa_camera_event *events = camera_array(&d->storage, source->event_count, sizeof(*events), _Alignof(qa_camera_event), error);
    if ((source->target_count && (!targets || !d->targets)) || (source->event_count && !events)) goto failed;
    d->definition.targets = targets; d->definition.events = events;
    if (!path_copy(d, &source->position, &d->definition.position, &d->position, error)) goto failed;
    for (size_t i = 0; i < source->target_count; ++i) {
        d->targets[i] = (camera_curve){0};
        if (!path_copy(d, &source->targets[i], &targets[i], &d->targets[i], error)) goto failed;
    }
    for (size_t i = 0; i < source->event_count; ++i) {
        qa_camera_event event = source->events[i];
        if (event.type > 9 || !event.parameter || !isfinite(event.time_ms) || event.time_ms < 0) {
            camera_fail(error, "invalid camera event"); goto failed;
        }
        event.parameter = camera_text(&d->storage, event.parameter, error); if (!event.parameter) goto failed;
        if (event.type == 1) { double seconds; if (!wait_seconds(event.parameter, &seconds, error)) goto failed; }
        if (event.type == 4) {
            bool found = false;
            for (size_t j = 0; j < source->target_count; ++j) if (!strcmp(targets[j].name, event.parameter)) { found = true; break; }
            if (!found) { camera_fail(error, "unknown camera target"); goto failed; }
        }
        events[i] = event;
    }
    *out = d; return true;
failed:
    qa_camera_release(d); return false;
}
void qa_camera_retain(qa_camera_document *d) { if (d) ++d->references; }
void qa_camera_release(qa_camera_document *d) {
    if (!d || --d->references) return;
    qa_arena_destroy(&d->storage); free(d);
}
const qa_camera_definition *qa_camera_describe(const qa_camera_document *d) { return d ? &d->definition : NULL; }

typedef struct path_state { double start, last, traveled, duration; } path_state;
struct qa_camera_playback {
    qa_camera_document *document;
    path_state camera, *targets;
    bool *triggered, stopped;
    size_t active_target;
    double start, last, total;
};
static void path_start(path_state *state, double time, double duration) {
    *state = (path_state){.start = time, .last = time, .duration = duration};
}
static size_t target_index(const qa_camera_definition *d, const char *name) {
    for (size_t i = 0; i < d->target_count; ++i) if (!strcmp(d->targets[i].name, name)) return i;
    return SIZE_MAX;
}
bool qa_camera_playback_create(qa_camera_document *document, double start, qa_camera_playback **out, qa_error *error) {
    if (!document || !out || !isfinite(start)) return camera_fail(error, "invalid camera playback start");
    const qa_camera_definition *d = &document->definition;
    if (d->target_count > SIZE_MAX / sizeof(path_state)) return camera_fail(error, "camera target playback overflow");
    qa_camera_playback *p = calloc(1, sizeof *p);
    if (!p) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating camera playback"); return false; }
    p->document = document; qa_camera_retain(document); p->start = p->last = start; p->total = d->seconds * 1000;
    p->targets = d->target_count ? calloc(d->target_count, sizeof(*p->targets)) : NULL;
    p->triggered = d->event_count ? calloc(d->event_count, sizeof(*p->triggered)) : NULL;
    if ((d->target_count && !p->targets) || (d->event_count && !p->triggered)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating camera event/target state"); goto failed;
    }
    for (size_t i = 0; i < d->event_count; ++i) if (d->events[i].type == 1) {
        double seconds; if (!wait_seconds(d->events[i].parameter, &seconds, error)) goto failed;
        p->total += seconds * 1000;
    }
    if (!isfinite(p->total) || !isfinite(start + p->total)) { camera_fail(error, "camera duration exceeds clock range"); goto failed; }
    path_start(&p->camera, start, d->seconds * 1000);
    for (size_t i = 0; i < d->target_count; ++i) path_start(&p->targets[i], start, d->targets[i].time_ms ? d->targets[i].time_ms : p->total);
    double time_so_far = 0;
    for (size_t i = 0; i < d->event_count; ++i) if (d->events[i].type == 4) {
        double duration = p->total - time_so_far;
        for (size_t j = i + 1; j < d->event_count; ++j) if (d->events[j].type == 4) { duration = d->events[j].time_ms; break; }
        size_t target = target_index(d, d->events[i].parameter);
        path_start(&p->targets[target], start, duration); p->active_target = target; time_so_far += duration;
    }
    *out = p; return true;
failed:
    qa_camera_playback_destroy(p); return false;
}
void qa_camera_playback_destroy(qa_camera_playback *p) {
    if (!p) return;
    qa_camera_release(p->document); free(p->targets); free(p->triggered); free(p);
}
static qa_vec3 sample_path(const qa_camera_path *path, const camera_curve *curve, path_state *state,
                           double now, const qa_camera_definition *definition, bool camera) {
    if (path->kind == QA_CAMERA_FIXED) return curve->points[0];
    if (path->kind == QA_CAMERA_INTERPOLATED) {
        double elapsed = now - state->start, speed = state->duration == 0 ? INFINITY : curve->distance / (state->duration / 1000);
        bool specified = false;
        for (size_t i = 0; i < path->velocity_count; ++i) {
            qa_camera_velocity v = path->velocities[i];
            if (elapsed >= v.start_ms && elapsed <= v.start_ms + v.duration_ms) { speed = v.speed; specified = true; break; }
        }
        if (camera && !specified) for (size_t i = 0; i < definition->event_count; ++i) {
            qa_camera_event e = definition->events[i]; if (e.type != 1) continue;
            double seconds = 0; wait_seconds(e.parameter, &seconds, NULL);
            if (elapsed >= e.time_ms && elapsed <= e.time_ms + seconds * 1000) { speed = 0; break; }
        }
        double delta = fmax(0, now - state->last) / 1000;
        if (delta > 0) state->traveled += delta * speed;
        state->last = now;
        float fraction = curve->distance == 0 ? 0 : (float)fmax(0, fmin(1, state->traveled / curve->distance));
        return qa_vec_lerp(path->position.interpolated.start, path->position.interpolated.end, fraction);
    }
    double desired = state->duration == 0 ? curve->distance : (now - state->start) / state->duration * curve->distance;
    size_t low = 0, high = curve->count;
    while (low < high) { size_t mid = low + (high - low) / 2; if (curve->distances[mid] < desired) low = mid + 1; else high = mid; }
    size_t index = low < curve->count ? low : curve->count - 1;
    if (index && index + 1 < curve->count && curve->distances[index + 1] > curve->distances[index - 1]) {
        float fraction = (float)((desired - curve->distances[index - 1]) / (curve->distances[index + 1] - curve->distances[index - 1]));
        return qa_vec_lerp(curve->points[index - 1], curve->points[index + 1], fraction);
    }
    return curve->points[index];
}
bool qa_camera_playback_sample(qa_camera_playback *p, double now, qa_arena *scratch,
                               qa_camera_sample *out, bool *active, qa_error *error) {
    if (!p || !scratch || !out || !active || !isfinite(now) || now < p->last)
        return camera_fail(error, "camera playback requires a monotonic clock");
    const qa_camera_definition *d = &p->document->definition;
    qa_camera_event *events = camera_array(scratch, d->event_count, sizeof(*events), _Alignof(qa_camera_event), error);
    if (d->event_count && !events) return false;
    p->last = now;
    if (p->stopped || trunc((now - p->start) / 1000) > p->total / 1000) { *active = false; return true; }
    size_t count = 0;
    for (size_t i = 0; i < d->event_count; ++i) if (!p->triggered[i] && now >= p->start + d->events[i].time_ms) {
        p->triggered[i] = true; events[count++] = d->events[i];
        if (d->events[i].type == 9) { p->stopped = true; *active = false; return true; }
        if (d->events[i].type == 4) {
            p->active_target = target_index(d, d->events[i].parameter);
            path_start(&p->targets[p->active_target], p->start + d->events[i].time_ms, p->targets[p->active_target].duration);
        }
    }
    qa_vec3 origin = sample_path(&d->position, &p->document->position, &p->camera, now, d, true), target = origin;
    if (p->active_target < d->target_count) target = sample_path(&d->targets[p->active_target], &p->document->targets[p->active_target],
                                                               &p->targets[p->active_target], now, d, false);
    double fraction = d->fov.time_ms == 0 ? 0 : fmax(0, fmin(1, (now - p->start) / d->fov.time_ms));
    *out = (qa_camera_sample){.origin = origin, .direction = qa_vec_normalize(qa_vec_sub(target, origin)),
        .fov = d->fov.time_ms == 0 ? d->fov.value : (float)(d->fov.start + (d->fov.end - d->fov.start) * fraction),
        .events = events, .event_count = count};
    *active = true; return true;
}
bool qa_camera_sample_view(const qa_camera_sample *sample, const qa_scene_view *base, qa_scene_view *out, qa_error *error) {
    if (!sample || !base || !out || !qa_vec_finite(sample->origin) || !qa_vec_finite(sample->direction) ||
        !isfinite(sample->fov) || sample->fov <= 0 || sample->fov >= 180)
        return camera_fail(error, "invalid camera view sample");
    float near_clip = base->projection.m[14] / (base->projection.m[10] - 1);
    float far_clip = base->projection.m[14] / (base->projection.m[10] + 1);
    float aspect = base->projection.m[5] / base->projection.m[0];
    if (!isfinite(near_clip) || !isfinite(far_clip) || !isfinite(aspect) || near_clip <= 0 || far_clip <= near_clip || aspect <= 0)
        return camera_fail(error, "camera view has invalid perspective projection");
    qa_scene_view view = *base; view.origin = sample->origin;
    if (qa_vec_length(sample->direction) > 0) {
        qa_vec3 dir = sample->direction;
        qa_vec3 angles = {-(float)(atan2(dir.z, hypot(dir.x, dir.y)) * 57.295779513082320877),
                          (float)(atan2(dir.y, dir.x) * 57.295779513082320877), 0};
        qa_vec3 right; qa_builtin_angle_vectors(angles, &view.axis[0], &right, &view.axis[2]);
        view.axis[1] = qa_vec_scale(right, -1);
    }
    float fov_y = (float)(atan(tan(sample->fov * .00872664625997164788) / aspect) * 114.59155902616464175);
    view.projection = qa_scene_projection(sample->fov, fov_y, near_clip, far_clip);
    *out = view; return true;
}
