#include "tools_internal.h"
#include "qa/json_writer.h"
#include "qa/text.h"
#include <math.h>
#include <string.h>

bool qa_camera_normalize(qa_bytes bytes, qa_buffer *out, qa_error *error) {
    qa_camera_document *document = NULL;
    if (!qa_camera_decode(bytes, &document, error)) return false;
    bool ok = qa_camera_encode(document, out, error); qa_camera_release(document); return ok;
}
static void vector(qa_json_writer *w, qa_vec3 v) {
    qa_json_writer_object(w); qa_json_writer_key(w, "x"); qa_json_writer_number(w, v.x);
    qa_json_writer_key(w, "y"); qa_json_writer_number(w, v.y); qa_json_writer_key(w, "z"); qa_json_writer_number(w, v.z); qa_json_writer_end(w);
}
bool qa_camera_sample_json(qa_camera_document *document, double step, qa_buffer *out, qa_error *error) {
    if (!document || !out || !isfinite(step) || step <= 0) return tools_fail(error, "camera sample interval must be positive and finite");
    qa_camera_playback *playback = NULL; qa_json_writer w = {0}; qa_arena scratch = {0}; bool ok = false;
    if (!qa_camera_playback_create(document, 0, &playback, error)) return false;
    qa_json_writer_array(&w);
    for (double now = 0;;) {
        qa_camera_sample sample; bool active;
        if (!qa_camera_playback_sample(playback, now, &scratch, &sample, &active, error)) goto done;
        if (!active) break;
        qa_json_writer_object(&w); qa_json_writer_key(&w, "milliseconds"); qa_json_writer_number(&w, now);
        qa_json_writer_key(&w, "origin"); vector(&w, sample.origin); qa_json_writer_key(&w, "direction"); vector(&w, sample.direction);
        qa_json_writer_key(&w, "fov"); qa_json_writer_number(&w, sample.fov); qa_json_writer_key(&w, "events"); qa_json_writer_array(&w);
        for (size_t i = 0; i < sample.event_count; ++i) {
            qa_json_writer_object(&w); qa_json_writer_key(&w, "type"); qa_json_writer_number(&w, sample.events[i].type);
            qa_json_writer_key(&w, "param"); qa_json_writer_string(&w, sample.events[i].parameter);
            qa_json_writer_key(&w, "time"); qa_json_writer_number(&w, sample.events[i].time_ms); qa_json_writer_end(&w);
        }
        qa_json_writer_end(&w); qa_json_writer_end(&w);
        if (w.failed) { if (error) *error = w.failure; goto done; }
        qa_arena_destroy(&scratch);
        double next = now + step;
        if (!isfinite(next) || next <= now) { tools_fail(error, "camera sample clock cannot advance"); goto done; }
        now = next;
    }
    qa_json_writer_end(&w); ok = qa_json_writer_finish(&w, out, error);
done:
    qa_arena_destroy(&scratch); qa_json_writer_destroy(&w); qa_camera_playback_destroy(playback); return ok;
}
bool tools_camera_author(qa_tools *tools, const qa_command_invocation *call, qa_error *error) {
    bool sample = call->argc > 1 && !strcmp(call->argv[1], "sample");
    if ((call->argc != 4 && !(sample && call->argc == 5)) || (!sample && strcmp(call->argv[1], "normalize")))
        return tools_fail(error, "usage: source-camera normalize <input> <output> | sample <input> <output> [step-ms]");
    double step = 16;
    if (sample && call->argc == 5 && (!qa_parse_number((qa_bytes){(const uint8_t *)call->argv[4], strlen(call->argv[4])}, &step, error) || !isfinite(step) || step <= 0)) return tools_fail(error, "camera sample interval must be positive");
    qa_resource *file = NULL; qa_buffer output = {0}; qa_camera_document *document = NULL;
    qa_vfs *files; qa_mount_id mount;
    bool ok = tools_files(tools, &call->context, &files, &mount, error) && qa_vfs_acquire(files, call->argv[2], &file, NULL, error);
    if (ok && sample) ok = qa_camera_decode(qa_resource_bytes(file), &document, error) && qa_camera_sample_json(document, step, &output, error);
    else if (ok) ok = qa_camera_normalize(qa_resource_bytes(file), &output, error);
    if (ok) ok = qa_vfs_write(files, mount, call->argv[3], (qa_bytes){output.data, output.size}, error);
    qa_camera_release(document); qa_buffer_free(&output); qa_resource_release(file); return ok;
}
