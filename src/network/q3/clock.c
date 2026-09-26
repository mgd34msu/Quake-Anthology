#include "qa/network_q3.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool clock_value(int64_t value, int32_t *out, qa_error *error) {
    if (value < INT32_MIN || value > INT32_MAX) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q3 client clock exceeds signed 32-bit time"); return false;
    }
    *out = (int32_t)value; return true;
}
void qa_q3_clock_clear(qa_q3_client_clock *clock) { memset(clock, 0, sizeof(*clock)); }
void qa_q3_clock_publish(qa_q3_client_clock *clock, const qa_q3_snapshot *snapshot) {
    clock->snapshot_time = snapshot->server_time; clock->snapshot_flags = snapshot->flags;
    clock->pending = true; clock->has_snapshot = true;
}
bool qa_q3_clock_advance(qa_q3_client_clock *clock, int32_t now, const qa_q3_clock_options *options,
                          bool *active, int32_t *server_time, qa_error *error) {
    if (!clock || !options || !active || !server_time) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 client clock arguments"); return false;
    }
    *active = false;
    if (!clock->active) {
        if (!clock->pending || !clock->has_snapshot || (clock->snapshot_flags & 2)) return true;
        if (!clock_value((int64_t)clock->snapshot_time - now, &clock->delta, error)) return false;
        clock->pending = false; clock->active = true;
        clock->old_time = clock->snapshot_time; clock->demo_base_time = clock->snapshot_time;
    }
    *active = true;
    if (options->paused) { *server_time = clock->time; return true; }
    if (clock->snapshot_time < clock->old_frame_server_time) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q3 snapshot time moved backwards"); return false;
    }
    clock->old_frame_server_time = clock->snapshot_time;
    if (!options->demo || !options->freeze_demo) {
        int32_t nudge = options->time_nudge < -30 ? -30 : options->time_nudge > 30 ? 30 : options->time_nudge;
        if (!clock_value((int64_t)now + clock->delta - nudge, &clock->time, error)) return false;
        if (clock->time < clock->old_time) clock->time = clock->old_time;
        clock->old_time = clock->time;
        if ((int64_t)now + clock->delta >= (int64_t)clock->snapshot_time - 5) clock->extrapolated = true;
    }
    if (clock->pending) {
        clock->pending = false;
        if (!options->demo) {
            int32_t next;
            if (!clock_value((int64_t)clock->snapshot_time - now, &next, error)) return false;
            int64_t distance = llabs((int64_t)next - clock->delta);
            if (distance > 500) { clock->delta = next; clock->old_time = clock->snapshot_time; clock->time = clock->snapshot_time; }
            else if (distance > 100) {
                int64_t sum = (int64_t)clock->delta + next;
                clock->delta = (int32_t)(sum >= 0 ? sum / 2 : -((-sum + 1) / 2));
            } else if (options->timescale == 0 || options->timescale == 1) {
                if (!clock_value((int64_t)clock->delta + (clock->extrapolated ? -2 : 1), &clock->delta, error)) return false;
                clock->extrapolated = false;
            }
        }
    }
    if (options->demo && options->timedemo) {
        if (!clock->demo_start) clock->demo_start = now;
        if (!clock_value((int64_t)clock->demo_frames + 1, &clock->demo_frames, error)
            || !clock_value((int64_t)clock->demo_base_time + (int64_t)clock->demo_frames * 50, &clock->time, error)) return false;
    }
    *server_time = clock->time; return true;
}

bool qa_q3_clock_needs_demo_message(const qa_q3_client_clock *clock) {
    return clock && clock->has_snapshot && clock->time >= clock->snapshot_time;
}
bool qa_q3_clock_demo_timing(const qa_q3_client_clock *clock, int32_t now, bool *present,
                              int32_t *frames, int32_t *elapsed_ms, qa_error *error) {
    if (!clock || !present || !frames || !elapsed_ms) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 demo timing output"); return false;
    }
    if (!clock_value((int64_t)now - clock->demo_start, elapsed_ms, error)) return false;
    *present = *elapsed_ms > 0; *frames = clock->demo_frames; return true;
}
