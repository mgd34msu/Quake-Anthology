#ifndef QA_BOTS_POPULATION_SAVE_H
#define QA_BOTS_POPULATION_SAVE_H

#include "qa/bots.h"

/* Actual population continuation, imported after the complete runtime and
 * shared actor/mode owners. Restores physical client/actor lookup extents,
 * retired cleanup records, scheduler/input state and retained observations.
 * The candidate must come from qa_bots_create_restored. It creates no gameplay
 * admission, source handle, command, observation or schedule callback. */
bool qa_bots_population_capture(const qa_bots *, qa_buffer *, qa_error *);
bool qa_bots_population_restore(qa_bots *, qa_bytes, qa_error *);

#endif
