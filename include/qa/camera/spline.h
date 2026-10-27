/* Port of src/camera/spline.ts. Camera files and cubic B-splines
 * adapted from Q3 code/splines/splines.cpp. GPL-2.0-or-later.
 * C accommodations:
 *  - Plain TS `Error` throws abort printing the identical message
 *    (the src/contracts pattern); the one RangeError (monotonic
 *    clock) uses qa_range_error. Allocation failure has no TS
 *    counterpart: parse/serialize abort (or return NULL for
 *    serialize), playback_init returns false.
 *  - Strings are owned NUL-terminated copies (TS UTF-16 strings;
 *    non-ASCII bytes pass through, toLowerCase is ASCII-only).
 *  - serialize numbers use shortest round-trip `%g` (integral values
 *    print without a decimal point, as JS); JS chooses exponent form
 *    at different magnitudes than `%g` for extreme values.
 *  - CameraPlayback borrows its definition (must outlive it), as the
 *    TS class holds the object; sample events point into it.
 *  - TS `sample(): CameraSample | null` becomes a bool (false=null).
 */
#ifndef QA_CAMERA_SPLINE_H
#define QA_CAMERA_SPLINE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"

typedef struct qa_camera_velocity {
  double start;
  double duration;
  double speed;
} qa_camera_velocity;

typedef enum qa_camera_position_kind {
  QA_CAMERA_POSITION_FIXED,
  QA_CAMERA_POSITION_INTERPOLATED,
  QA_CAMERA_POSITION_SPLINE
} qa_camera_position_kind;

typedef struct qa_camera_position {
  qa_camera_position_kind kind;
  char *name;
  double time;
  double base_velocity;
  qa_camera_velocity *velocities;
  size_t velocity_count;
  qa_vec3 point; /* fixed */
  qa_vec3 start; /* interpolated */
  qa_vec3 end;   /* interpolated */
  double granularity; /* spline */
  qa_vec3 *points;    /* spline */
  size_t point_count;
} qa_camera_position;

typedef struct qa_camera_event {
  double type;
  char *param;
  double time;
} qa_camera_event;

typedef struct qa_camera_fov {
  double value;
  double start;
  double end;
  double time;
} qa_camera_fov;

typedef struct qa_camera_definition {
  double seconds;
  qa_camera_position position;
  qa_camera_position *targets;
  size_t target_count;
  qa_camera_event *events;
  size_t event_count;
  qa_camera_fov fov;
} qa_camera_definition;

void qa_camera_definition_destroy(qa_camera_definition *definition);

/* TS parseCamera; invalid input aborts with the TS Error message. */
void qa_parse_camera(const char *text, qa_camera_definition *out);

/* TS serializeCamera; malloc'd, caller frees. NULL only on OOM. */
char *qa_serialize_camera(const qa_camera_definition *camera);

/* Storage for the unexported TS PositionPlayback. */
typedef struct qa_camera_position_playback {
  const qa_camera_position *path;
  double duration;
  double start_time;
  double last_time;
  double traveled;
  qa_vec3 *points;
  size_t point_count;
  double *distances;
  size_t distance_count;
  double distance;
  qa_camera_velocity *velocities;
  size_t velocity_count;
} qa_camera_position_playback;

typedef struct qa_camera_sample {
  qa_vec3 origin;
  qa_vec3 direction;
  double fov;
  const qa_camera_event **events; /* playback-owned, next sample invalidates */
  size_t event_count;
} qa_camera_sample;

typedef struct qa_camera_playback {
  const qa_camera_definition *definition;
  double start_time;
  qa_camera_position_playback camera;
  qa_camera_position_playback *targets;
  size_t target_count;
  bool *triggered;
  long active_target;
  bool stopped;
  double total_milliseconds;
  double last_time;
  const qa_camera_event **sample_events;
  size_t sample_event_capacity;
} qa_camera_playback;

/* Returns false only on OOM. */
bool qa_camera_playback_init(qa_camera_playback *playback,
                             const qa_camera_definition *definition,
                             double start_time);
void qa_camera_playback_destroy(qa_camera_playback *playback);

/* TS CameraPlayback.sample; false is the TS null. */
bool qa_camera_playback_sample(qa_camera_playback *playback, double time,
                               qa_camera_sample *out);

#endif
