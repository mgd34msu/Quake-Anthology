/*
 * Port of src/contracts/time.ts (types only): clock profiles, source
 * times, and frame ordering. String-union mapping per enumerator.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_TIME_H
#define QA_CONTRACTS_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"

/* TS ClockProfile discriminated union, on `kind`. */
typedef enum qa_clock_profile_kind {
  QA_CLOCK_Q1_NETQUAKE,    /* "q1-netquake" */
  QA_CLOCK_Q1_QUAKEWORLD,  /* "q1-quakeworld" */
  QA_CLOCK_Q2_CLASSIC,     /* "q2-classic", frameMilliseconds 100 */
  QA_CLOCK_Q2_RERELEASE,   /* "q2-rerelease", preparation "before-frame" */
  QA_CLOCK_Q3              /* "q3", maximumCommandMilliseconds 200 */
} qa_clock_profile_kind;

typedef struct qa_clock_profile {
  qa_clock_profile_kind kind;
  double minimum_frame_seconds;        /* q1-netquake */
  double maximum_frame_seconds;        /* q1-netquake */
  bool has_fixed_frame_seconds;        /* q1-netquake: number | null */
  double fixed_frame_seconds;
  double maximum_command_milliseconds; /* q1-quakeworld, q3 (200) */
  double frame_milliseconds;           /* q2-classic (100), q2-rerelease */
  double server_frame_milliseconds;    /* q3 */
  bool has_fixed_movement_milliseconds; /* q3: number | null */
  double fixed_movement_milliseconds;
} qa_clock_profile;

/** TS SourceTime: source clocks retain their units. */
typedef enum qa_source_time_kind {
  QA_SOURCE_TIME_SECONDS,       /* "seconds" */
  QA_SOURCE_TIME_MILLISECONDS   /* "milliseconds" */
} qa_source_time_kind;

typedef struct qa_source_time {
  qa_source_time_kind kind;
  double value;
} qa_source_time;

/* TS FramePhase. */
typedef enum qa_frame_phase {
  QA_FRAME_PHASE_FRAME_ENTRY,      /* "frame-entry" */
  QA_FRAME_PHASE_CLIENT_COMMAND,   /* "client-command" */
  QA_FRAME_PHASE_ENTITY_PRETHINK,  /* "entity-prethink" */
  QA_FRAME_PHASE_ENTITY_PHYSICS,   /* "entity-physics" */
  QA_FRAME_PHASE_ENTITY_THINK,     /* "entity-think" */
  QA_FRAME_PHASE_CLIENT_END_FRAME, /* "client-end-frame" */
  QA_FRAME_PHASE_FRAME_EXIT        /* "frame-exit" */
} qa_frame_phase;

/* TS FrameOrdering discriminated union, on `kind`. The native variant
 * carries traversal "source-slot-order"; mixed carries entityOrder
 * "source-slot-order" and ties "provider-entity-invocation". */
typedef enum qa_frame_ordering_kind {
  QA_FRAME_ORDERING_NATIVE, /* "native" */
  QA_FRAME_ORDERING_MIXED   /* "mixed" */
} qa_frame_ordering_kind;

typedef struct qa_frame_ordering {
  qa_frame_ordering_kind kind;
  qa_clock_profile clock;                       /* native */
  const qa_provider_id *providers;              /* mixed */
  size_t provider_count;
} qa_frame_ordering;

typedef struct qa_frame_context {
  double frame;
  qa_source_time time;
  qa_source_time elapsed;
  qa_frame_phase phase;
} qa_frame_context;

typedef struct qa_invocation_order {
  qa_provider_id provider;
  qa_actor_id actor;
  double sequence;
} qa_invocation_order;

/* TS ThinkTiming boundary: "before-physics" | "during-physics" | "after-physics". */
typedef enum qa_think_boundary {
  QA_THINK_BEFORE_PHYSICS,
  QA_THINK_DURING_PHYSICS,
  QA_THINK_AFTER_PHYSICS
} qa_think_boundary;

typedef struct qa_think_timing {
  bool has_execution_provider;
  qa_provider_id execution_provider;
  qa_source_time due;
  qa_think_boundary boundary;
  qa_invocation_order order;
} qa_think_timing;

#endif
