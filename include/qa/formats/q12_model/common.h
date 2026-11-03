/*
 * Port of src/formats/q12-model/common.ts. The TS generic readTimed<T>
 * becomes a frame callback (fn ptr + ctx) producing an owned element
 * pointer stored in qa_timed_frames. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q12_MODEL_COMMON_H
#define QA_FORMATS_Q12_MODEL_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/core/binary/index.h"

noreturn void qa_q12_fail(qa_binary_reader *reader, const char *message);
double qa_q12_count(qa_binary_reader *reader, const char *label, double minimum);
void qa_q12_version(qa_binary_reader *reader, double expected);
qa_vec3 qa_q12_vector(qa_binary_reader *reader);
/* "synchronized" | "random" */
const char *qa_q12_sync_type(qa_binary_reader *reader);
/* True is TS "group". */
bool qa_q12_group_type(qa_binary_reader *reader);
/* Malloc'd cumulative endpoints. */
double *qa_q12_intervals(qa_binary_reader *reader, double frame_count, size_t *out_count);

/* A frame reader must not leak its own partial frame when it raises. */
typedef void *(*qa_q12_read_frame_fn)(void *ctx);
/* Reads into `out`, which the caller zeroes and releases on a raise (frames are stored as they are read). */
void qa_q12_read_timed(qa_binary_reader *reader, qa_q12_read_frame_fn read_frame, void *ctx, qa_timed_frames *out);

qa_vec3 qa_q12_expand_position(qa_vec3 position, qa_vec3 scale, qa_vec3 translate);
qa_packed_alias_vertex qa_q12_packed_vertex(qa_binary_reader *reader);
qa_vec3 qa_q12_packed_position(qa_packed_alias_vertex vertex, qa_vec3 scale, qa_vec3 translate);
qa_bounds qa_q12_bounds_from_points(const qa_vec3 *points, size_t count);
qa_bounds qa_q12_union_bounds(const qa_bounds *bounds, size_t count);
double qa_q12_index(qa_binary_reader *reader, double value, double limit, const char *label);
/* TS at(): RangeError "Missing <label> <index>" when outside. */
size_t qa_q12_at(double index, size_t count, const char *label);

#endif
