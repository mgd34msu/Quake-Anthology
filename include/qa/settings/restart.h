/*
 * Port of src/settings/restart.ts: RestartControls over a bounded
 * pending queue. Resource owners perform their own restart,
 * retaining session state outside their lease. C accommodations:
 *  - RestartService.restart() (void | Promise<void>) becomes a
 *    synchronous function pointer plus ctx; drain() is therefore
 *    synchronous too (see docs/PORTING.md on async flows).
 *  - The services/latched Records become arrays indexed by
 *    qa_restart_kind; latched name lists are borrowed.
 *  - register() returned an unsubscribe closure; here it fills a
 *    qa_restart_registration the caller unregisters explicitly.
 *  - Pending kinds live in the foundation BoundedQueue (capacity 4,
 *    drop-oldest on overflow, exactly as the TS queue behaves).
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_RESTART_H
#define QA_SETTINGS_RESTART_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/foundation/bounded_queue.h"

/* TS RestartKind: "video" | "input" | "audio". */
typedef enum qa_restart_kind {
  QA_RESTART_VIDEO = 0,
  QA_RESTART_INPUT = 1,
  QA_RESTART_AUDIO = 2,
  QA_RESTART_KIND_COUNT = 3
} qa_restart_kind;

/** TS RestartService. */
typedef struct qa_restart_service {
  void *ctx;
  void (*restart)(void *ctx);
} qa_restart_service;

typedef struct qa_restart_controls {
  qa_restart_service services[QA_RESTART_KIND_COUNT];
  qa_cvar_registry *cvars; /* borrowed */
  const char *const *latched[QA_RESTART_KIND_COUNT]; /* borrowed */
  size_t latched_count[QA_RESTART_KIND_COUNT];
  qa_bounded_queue pending;
  bool running;
} qa_restart_controls;

/* False only on allocation failure (the pending queue). */
bool qa_restart_controls_init(
    qa_restart_controls *controls,
    const qa_restart_service services[QA_RESTART_KIND_COUNT],
    qa_cvar_registry *cvars,
    const char *const *const latched[QA_RESTART_KIND_COUNT],
    const size_t latched_count[QA_RESTART_KIND_COUNT]);
void qa_restart_controls_destroy(qa_restart_controls *controls);
void qa_restart_controls_request(qa_restart_controls *controls,
                                 qa_restart_kind kind);
/* Aborts (TS Error) when a drain is already active. */
void qa_restart_controls_drain(qa_restart_controls *controls);

/** The unsubscribe closure state: which of vid_restart / in_restart /
 * snd_restart were actually registered, with their handler ctx. */
typedef struct qa_restart_command {
  qa_restart_controls *controls;
  qa_restart_kind kind;
} qa_restart_command;
typedef struct qa_restart_registration {
  qa_command_buffer *commands; /* borrowed */
  qa_restart_command handlers[QA_RESTART_KIND_COUNT];
  bool registered[QA_RESTART_KIND_COUNT];
} qa_restart_registration;

void qa_restart_controls_register(qa_restart_controls *controls,
                                  qa_command_buffer *commands,
                                  qa_restart_registration *out);
void qa_restart_registration_unregister(qa_restart_registration *registration);

#endif
