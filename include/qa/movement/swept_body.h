/*
 * Port of src/movement/swept-body.ts: the shared swept-movement solver.
 * Source adapters own state access, contact effects and arithmetic.
 * C accommodations:
 *  - The TS generic Trace becomes qa_swept_body_trace carrying the four
 *    solver fields plus a borrowed `native` pointer to the adapter's
 *    trace object (valid until the adapter's next trace call; the
 *    solver only uses it synchronously within one bump, as in the TS).
 *  - Plane identity (TS `first === second`) is preserved by passing
 *    same_slot to same_plane: true when both entries are the same normal
 *    object in TS. Without plane_identity every normal is a fresh object
 *    (same planes-array slot); with it, two entries are also the same
 *    object when their nonzero identities are equal (the trace's
 *    qa_trace_result.plane_identity). Adapters that used identity
 *    return same_slot.
 *  - Optional policy/response members use has_* flags; enum defaults
 *    match the TS undefined behavior exactly.
 * GPL-2.0-or-later.
 */
#ifndef QA_MOVEMENT_SWEPT_BODY_H
#define QA_MOVEMENT_SWEPT_BODY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"

/** TS SweptBodyState. */
typedef struct qa_swept_body_state {
  qa_vec3 origin;
  qa_vec3 velocity;
} qa_swept_body_state;

/** TS SweptBodyTrace plus the adapter's native trace object. */
typedef struct qa_swept_body_trace {
  double fraction;
  qa_vec3 end;
  bool all_solid;
  bool start_solid;
  void *native; /* borrowed; adapter-defined */
} qa_swept_body_trace;

/** TS writeVelocity components: "all" | "vertical" (omitted = "all"). */
typedef enum qa_swept_velocity_components {
  QA_SWEPT_VELOCITY_ALL,
  QA_SWEPT_VELOCITY_VERTICAL
} qa_swept_velocity_components;

/* TS collisionPolicy discriminants (exact strings in comments). */
typedef enum qa_swept_original_velocity {
  QA_SWEPT_ORIGINAL_LAST_PROGRESS, /* "last-progress" (also undefined) */
  QA_SWEPT_ORIGINAL_INITIAL        /* "initial" */
} qa_swept_original_velocity;
typedef enum qa_swept_all_solid_velocity {
  QA_SWEPT_ALL_SOLID_ZERO,   /* "zero" (also undefined) */
  QA_SWEPT_ALL_SOLID_ZERO_Z  /* "zero-z" */
} qa_swept_all_solid_velocity;
typedef enum qa_swept_candidate_velocity {
  QA_SWEPT_CANDIDATE_ORIGINAL,   /* "original" (also undefined) */
  QA_SWEPT_CANDIDATE_SEQUENTIAL  /* "sequential" */
} qa_swept_candidate_velocity;
typedef enum qa_swept_crease_velocity {
  QA_SWEPT_CREASE_CURRENT,       /* "current" (also undefined) */
  QA_SWEPT_CREASE_LAST_CANDIDATE /* "last-candidate" */
} qa_swept_crease_velocity;

typedef struct qa_swept_body_math {
  void *ctx;
  qa_vec3 (*advance)(void *ctx, qa_vec3 origin, double time, qa_vec3 velocity);
  double (*remaining)(void *ctx, double time, double fraction);
  qa_vec3 (*clip)(void *ctx, qa_vec3 velocity, qa_vec3 normal);
  double (*dot)(void *ctx, qa_vec3 first, qa_vec3 second);
  qa_vec3 (*cross)(void *ctx, qa_vec3 first, qa_vec3 second);
  qa_vec3 (*scale)(void *ctx, qa_vec3 vector, double amount);
} qa_swept_body_math;

/** TS planeResponse (kind "paired"). Present iff has_plane_response. */
typedef struct qa_swept_plane_response {
  void *ctx;
  const qa_vec3 *seeds;
  size_t seed_count;
  double enter_threshold;
  qa_vec3 (*end_velocity)(void *ctx);
  void (*write_end_velocity)(void *ctx, qa_vec3 velocity);
  qa_vec3 (*normalize)(void *ctx, qa_vec3 direction);
  void (*impact_speed)(void *ctx, double speed);
} qa_swept_plane_response;

/** TS SweptBodyServices. prepare_trace/solid/touch may be NULL. */
typedef struct qa_swept_body_services {
  void *ctx;
  /* False is the TS null (actor removed). */
  bool (*read)(void *ctx, qa_swept_body_state *out);
  void (*write_origin)(void *ctx, qa_vec3 origin);
  void (*write_velocity)(void *ctx, qa_vec3 velocity, qa_swept_velocity_components components);
  void (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, qa_swept_body_trace *out);
  void (*prepare_trace)(void *ctx, qa_swept_body_trace *trace);
  void (*solid)(void *ctx, const qa_swept_body_trace *trace);
  void (*touch)(void *ctx, const qa_swept_body_trace *trace);
  qa_vec3 (*normal)(void *ctx, const qa_swept_body_trace *trace);
  /* JS identity of the object normal() returns (0 = fresh); may be NULL. */
  uintptr_t (*plane_identity)(void *ctx, const qa_swept_body_trace *trace);
  void (*impact)(void *ctx, const qa_swept_body_trace *trace, qa_vec3 normal);
  bool stop_when_still;
  bool has_collision_policy;
  bool stop_on_start_solid;
  qa_swept_original_velocity original_velocity;
  qa_swept_all_solid_velocity all_solid_velocity;
  qa_swept_candidate_velocity candidate_velocity;
  qa_swept_crease_velocity crease_velocity;
  bool has_plane_response;
  qa_swept_plane_response plane_response;
  bool has_duplicate_plane;
  double duplicate_threshold;
  void (*duplicate_recover)(void *ctx, qa_vec3 normal);
  bool (*same_plane)(void *ctx, qa_vec3 first, qa_vec3 second, bool same_slot);
  qa_swept_body_math math;
} qa_swept_body_services;

/** TS SweepStop (exact strings in comments). */
typedef enum qa_sweep_stop {
  QA_SWEEP_COMPLETE,       /* "complete" */
  QA_SWEEP_REMOVED,        /* "removed" */
  QA_SWEEP_SOLID,          /* "solid" */
  QA_SWEEP_PLANE_LIMIT,    /* "plane-limit" */
  QA_SWEEP_CREASE_BLOCKED, /* "crease-blocked" */
  QA_SWEEP_REVERSED        /* "reversed" */
} qa_sweep_stop;

qa_sweep_stop qa_sweep_body(const qa_swept_body_services *services, double elapsed);

#endif
