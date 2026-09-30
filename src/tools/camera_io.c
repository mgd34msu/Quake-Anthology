#include "camera_internal.h"
#include "qa/tokenizer.h"
#include "qa/text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct camera_parser {
    qa_tokenizer tokenizer;
    qa_token token;
    bool found;
    qa_arena arena;
    qa_error *error;
} camera_parser;
static bool advance(camera_parser *p) { return qa_tokenizer_next(&p->tokenizer, &p->token, &p->found, p->error); }
static bool is(camera_parser *p, const char *word) {
    if (!p->found || strlen(word) != p->token.text.size) return false;
    for (size_t i = 0; i < p->token.text.size; ++i) {
        unsigned c = p->token.text.data[i]; if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)word[i]) return false;
    }
    return true;
}
static bool expect(camera_parser *p, const char *word) {
    if (!is(p, word)) { qa_error_set(p->error, QA_ERROR_FORMAT, p->tokenizer.offset, "expected camera token %s", word); return false; }
    return advance(p);
}
static bool number(camera_parser *p, double *out) {
    if (!p->found || !camera_number(p->token.text, false, out, p->error))
        return camera_fail(p->error, "invalid camera number");
    return advance(p);
}
static bool scalar(camera_parser *p, float *out) {
    double value; if (!number(p, &value)) return false;
    *out = (float)value; return isfinite(*out) || camera_fail(p->error, "camera value exceeds float range");
}
static bool vector(camera_parser *p, qa_vec3 *out) {
    return expect(p, "(") && scalar(p, &out->x) && scalar(p, &out->y) && scalar(p, &out->z) && expect(p, ")");
}
static bool text(camera_parser *p, const char **out) {
    if (!p->found) return camera_fail(p->error, "missing camera text");
    char *value = camera_array(&p->arena, p->token.text.size + 1, 1, 1, p->error);
    if (!value || !qa_token_copy(&p->token, value, p->token.text.size + 1, p->error)) return false;
    *out = value; return advance(p);
}
static void *append(camera_parser *p, void *values, size_t *count, size_t *capacity,
                    const void *value, size_t item, size_t alignment) {
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 8;
        if (next < *capacity || next > SIZE_MAX / item) { camera_fail(p->error, "camera collection overflow"); return NULL; }
        void *copy = camera_array(&p->arena, next, item, alignment, p->error);
        if (!copy) return NULL;
        if (*count) memcpy(copy, values, *count * item);
        values = copy; *capacity = next;
    }
    memcpy((uint8_t *)values + (*count)++ * item, value, item); return values;
}
static bool path(camera_parser *p, qa_camera_path_kind kind, qa_camera_path *out) {
    qa_camera_path value = {.kind = kind, .name = "position"};
    qa_vec3 fixed = {0}, start = {0}, end = {0}, *points = NULL;
    float granularity = .025f; size_t point_count = 0, point_capacity = 0, velocity_capacity = 0;
    qa_camera_velocity *velocities = NULL;
    if (!expect(p, "{")) return false;
    while (!is(p, "}")) {
        if (!p->found) return camera_fail(p->error, "unterminated camera path");
        if (is(p, "name")) { if (!advance(p) || !text(p, &value.name)) return false; }
        else if (is(p, "type")) { double ignored; if (!advance(p) || !number(p, &ignored)) return false; }
        else if (is(p, "time")) { if (!advance(p) || !number(p, &value.time_ms)) return false; }
        else if (is(p, "basevelocity")) { if (!advance(p) || !number(p, &value.base_velocity)) return false; }
        else if (is(p, "velocity")) {
            qa_camera_velocity velocity;
            if (!advance(p) || !number(p, &velocity.start_ms) || !number(p, &velocity.duration_ms) || !number(p, &velocity.speed) ||
                !(velocities = append(p, velocities, &value.velocity_count, &velocity_capacity, &velocity, sizeof velocity, _Alignof(qa_camera_velocity)))) return false;
        } else if (is(p, "pos")) { if (!advance(p) || !vector(p, &fixed)) return false; }
        else if (is(p, "startpos")) { if (!advance(p) || !vector(p, &start)) return false; }
        else if (is(p, "endpos")) { if (!advance(p) || !vector(p, &end)) return false; }
        else if (is(p, "target")) {
            if (!advance(p) || !expect(p, "{")) return false;
            while (!is(p, "}")) {
                if (is(p, "(")) {
                    qa_vec3 point;
                    if (!vector(p, &point) || !(points = append(p, points, &point_count, &point_capacity, &point, sizeof point, _Alignof(qa_vec3)))) return false;
                } else if (is(p, "granularity")) { if (!advance(p) || !scalar(p, &granularity)) return false; }
                else if (is(p, "name")) { const char *ignored; if (!advance(p) || !text(p, &ignored)) return false; }
                else return camera_fail(p->error, "unknown camera spline property");
            }
            if (!expect(p, "}")) return false;
        } else return camera_fail(p->error, "unknown camera path property");
    }
    if (!expect(p, "}")) return false;
    value.velocities = velocities;
    if (kind == QA_CAMERA_FIXED) value.position.fixed = fixed;
    else if (kind == QA_CAMERA_INTERPOLATED) { value.position.interpolated.start = start; value.position.interpolated.end = end; }
    else { value.position.spline.points = points; value.position.spline.count = point_count; value.position.spline.granularity = granularity; }
    *out = value; return true;
}
bool qa_camera_decode(qa_bytes source, qa_camera_document **out, qa_error *error) {
    if (!out) return camera_fail(error, "missing camera output");
    qa_buffer utf8 = {0};
    if (!qa_utf8_repair(source, &utf8, error)) return false;
    camera_parser p = {.error = error};
    const qa_tokenizer_options options = {.punctuation = "{}()", .unicode_whitespace = true, .reject_quoted_newlines = true};
    bool ok = false, have_camera = false;
    qa_camera_definition d = {.seconds = 30, .fov = {.value = 90, .start = 90, .end = 90}};
    qa_camera_path *targets = NULL; qa_camera_event *events = NULL;
    size_t target_capacity = 0, event_capacity = 0;
    if (!qa_tokenizer_init_options(&p.tokenizer, (qa_bytes){utf8.data, utf8.size}, &options, error) || !advance(&p)) goto done;
    if (is(&p, "camera") || is(&p, "camerapathdef")) if (!advance(&p)) goto done;
    if (!expect(&p, "{")) goto done;
    while (!is(&p, "}")) {
        if (!p.found) { camera_fail(error, "unterminated camera definition"); goto done; }
        if (is(&p, "time")) { if (!advance(&p) || !number(&p, &d.seconds)) goto done; }
        else if (is(&p, "camera_fixed") || is(&p, "camera_interpolated") || is(&p, "camera_spline") ||
                 is(&p, "target_fixed") || is(&p, "target_interpolated") || is(&p, "target_spline")) {
            bool target = p.token.text.data[0] == 't' || p.token.text.data[0] == 'T';
            qa_camera_path_kind kind = is(&p, "camera_fixed") || is(&p, "target_fixed") ? QA_CAMERA_FIXED :
                is(&p, "camera_interpolated") || is(&p, "target_interpolated") ? QA_CAMERA_INTERPOLATED : QA_CAMERA_SPLINE;
            qa_camera_path value;
            if (!advance(&p) || !path(&p, kind, &value)) goto done;
            if (target) { if (!(targets = append(&p, targets, &d.target_count, &target_capacity, &value, sizeof value, _Alignof(qa_camera_path)))) goto done; }
            else { d.position = value; have_camera = true; }
        } else if (is(&p, "event")) {
            qa_camera_event value = {.parameter = ""};
            if (!advance(&p) || !expect(&p, "{")) goto done;
            while (!is(&p, "}")) {
                if (is(&p, "type")) {
                    double type;
                    if (!advance(&p) || !number(&p, &type)) goto done;
                    if (type < 0 || type > 9 || floor(type) != type) { camera_fail(error, "invalid camera event type"); goto done; }
                    value.type = (uint32_t)type;
                } else if (is(&p, "time")) { if (!advance(&p) || !number(&p, &value.time_ms)) goto done; }
                else if (is(&p, "param")) { if (!advance(&p) || !text(&p, &value.parameter)) goto done; }
                else { camera_fail(error, "unknown camera event property"); goto done; }
            }
            if (!expect(&p, "}") || !(events = append(&p, events, &d.event_count, &event_capacity, &value, sizeof value, _Alignof(qa_camera_event)))) goto done;
        } else if (is(&p, "fov")) {
            if (!advance(&p) || !expect(&p, "{")) goto done;
            while (!is(&p, "}")) {
                if (is(&p, "fov")) { if (!advance(&p) || !scalar(&p, &d.fov.value)) goto done; }
                else if (is(&p, "startfov")) { if (!advance(&p) || !scalar(&p, &d.fov.start)) goto done; }
                else if (is(&p, "endfov")) { if (!advance(&p) || !scalar(&p, &d.fov.end)) goto done; }
                else if (is(&p, "time")) { if (!advance(&p) || !number(&p, &d.fov.time_ms)) goto done; }
                else { camera_fail(error, "unknown camera FOV property"); goto done; }
            }
            if (!expect(&p, "}")) goto done;
        } else { camera_fail(error, "unknown camera definition property"); goto done; }
    }
    if (!expect(&p, "}")) goto done;
    if (p.found || !have_camera) { camera_fail(error, "invalid camera definition end or missing position"); goto done; }
    d.targets = targets; d.events = events; ok = qa_camera_create(&d, out, error);
done:
    qa_arena_destroy(&p.arena); qa_buffer_free(&utf8); return ok;
}

typedef struct camera_writer { qa_buffer buffer; size_t capacity; qa_error *error; } camera_writer;
static bool write_text(camera_writer *w, const char *text) {
    size_t n = strlen(text);
    if (n > SIZE_MAX - w->buffer.size - 1) return camera_fail(w->error, "camera encoding size overflow");
    size_t need = w->buffer.size + n + 1;
    if (need > w->capacity) {
        size_t capacity = w->capacity ? w->capacity : 512;
        while (capacity < need) { if (capacity > SIZE_MAX / 2) { capacity = need; break; } capacity *= 2; }
        void *copy = realloc(w->buffer.data, capacity);
        if (!copy) { qa_error_set(w->error, QA_ERROR_MEMORY, 0, "allocating camera encoding"); return false; }
        w->buffer.data = copy; w->capacity = capacity;
    }
    memcpy(w->buffer.data + w->buffer.size, text, n + 1); w->buffer.size += n; return true;
}
static bool write_number(camera_writer *w, double number) {
    char text[32]; return qa_format_number(number, text, w->error) && write_text(w, text);
}
static bool write_quote(camera_writer *w, const char *text) {
    if (strpbrk(text, "\"\r\n")) return camera_fail(w->error, "camera names cannot contain quotes or newlines");
    return write_text(w, "\"") && write_text(w, text) && write_text(w, "\"");
}
static bool write_vector(camera_writer *w, qa_vec3 v) {
    return write_text(w, "( ") && write_number(w, v.x) && write_text(w, " ") && write_number(w, v.y) &&
        write_text(w, " ") && write_number(w, v.z) && write_text(w, " )\n");
}
static bool write_path(camera_writer *w, const qa_camera_path *p, const char *prefix) {
    const char *kind = p->kind == QA_CAMERA_FIXED ? "_fixed" : p->kind == QA_CAMERA_INTERPOLATED ? "_interpolated" : "_spline";
    if (!write_text(w, prefix) || !write_text(w, kind) || !write_text(w, " {\nname ") || !write_quote(w, p->name) ||
        !write_text(w, "\ntime ") || !write_number(w, p->time_ms) || !write_text(w, "\nbaseVelocity ") || !write_number(w, p->base_velocity) || !write_text(w, "\n")) return false;
    for (size_t i = 0; i < p->velocity_count; ++i) {
        qa_camera_velocity v = p->velocities[i];
        if (!write_text(w, "velocity ") || !write_number(w, v.start_ms) || !write_text(w, " ") || !write_number(w, v.duration_ms) ||
            !write_text(w, " ") || !write_number(w, v.speed) || !write_text(w, "\n")) return false;
    }
    if (p->kind == QA_CAMERA_FIXED) { if (!write_text(w, "pos ") || !write_vector(w, p->position.fixed)) return false; }
    else if (p->kind == QA_CAMERA_INTERPOLATED) {
        if (!write_text(w, "startPos ") || !write_vector(w, p->position.interpolated.start) || !write_text(w, "endPos ") || !write_vector(w, p->position.interpolated.end)) return false;
    } else {
        if (!write_text(w, "target {\ngranularity ") || !write_number(w, p->position.spline.granularity) || !write_text(w, "\n")) return false;
        for (size_t i = 0; i < p->position.spline.count; ++i) if (!write_vector(w, p->position.spline.points[i])) return false;
        if (!write_text(w, "}\n")) return false;
    }
    return write_text(w, "}\n");
}
bool qa_camera_encode(const qa_camera_document *document, qa_buffer *out, qa_error *error) {
    if (!document || !out) return camera_fail(error, "invalid camera encoding");
    camera_writer w = {.error = error}; const qa_camera_definition *d = &document->definition;
    if (!write_text(&w, "cameraPathDef {\ntime ") || !write_number(&w, d->seconds) || !write_text(&w, "\n") || !write_path(&w, &d->position, "camera")) goto failed;
    for (size_t i = 0; i < d->target_count; ++i) if (!write_path(&w, &d->targets[i], "target")) goto failed;
    for (size_t i = 0; i < d->event_count; ++i) {
        qa_camera_event e = d->events[i];
        if (!write_text(&w, "event {\ntype ") || !write_number(&w, e.type) || !write_text(&w, "\nparam ") || !write_quote(&w, e.parameter) ||
            !write_text(&w, "\ntime ") || !write_number(&w, e.time_ms) || !write_text(&w, "\n}\n")) goto failed;
    }
    if (!write_text(&w, "fov {\nfov ") || !write_number(&w, d->fov.value) || !write_text(&w, "\nstartFOV ") || !write_number(&w, d->fov.start) ||
        !write_text(&w, "\nendFOV ") || !write_number(&w, d->fov.end) || !write_text(&w, "\ntime ") || !write_number(&w, d->fov.time_ms) || !write_text(&w, "\n}\n}\n")) goto failed;
    *out = w.buffer; return true;
failed:
    qa_buffer_free(&w.buffer); return false;
}
