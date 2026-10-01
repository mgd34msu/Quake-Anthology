#ifndef QA_APPLICATION_NATIVE_Q3_MATCH_H
#define QA_APPLICATION_NATIVE_Q3_MATCH_H

#include "internal.h"

/* Pure views of GAME's retained level state. Selected match phase, deadlines
 * and source slot numbers do not reconstruct these source fields. */
bool application_native_q3_match_intermission(application_provider *, int32_t *, qa_error *);
bool application_native_q3_match_warmup(application_provider *, int32_t *, qa_error *);
/* Source Init records the copied warmup metadata before authored map effects.
 * One genuine END pass applies the selected native Q3 rule controls; other
 * selected rule families retain their own control pass. */
bool application_native_q3_match_init(application_provider *, qa_error *);
bool application_native_q3_match_frame(application_provider *, const qa_source_frame *, qa_error *);
bool application_native_q3_match_check_exit(application_provider *, qa_error *);
bool application_native_q3_match_log_exit(application_provider *, qa_mode_id,
    qa_string_id reason, qa_error *);
/* Explicit source commands can request the true intermission client effects.
 * This neither substitutes Q3 limits nor runs Q3 readiness for foreign rules. */
bool application_native_q3_match_begin_intermission(application_provider *, qa_error *);
bool application_native_q3_match_exit_level(application_provider *, qa_error *);

#endif
