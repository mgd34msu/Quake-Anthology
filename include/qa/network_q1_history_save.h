#ifndef QA_NETWORK_Q1_HISTORY_SAVE_H
#define QA_NETWORK_Q1_HISTORY_SAVE_H
#include "qa/network_q1.h"

/* Complete native command history, including invalid retained physical slots.
 * The enclosing owner qualifies the idle candidate seat and channel cut.
 * Restore replaces only the supplied candidate value after full validation;
 * it does not acknowledge, bundle, replay or invoke movement services. */
bool qa_qw_history_checkpoint(const qa_qw_history *, qa_buffer *, qa_error *);
bool qa_qw_history_restore_checkpoint(qa_bytes, qa_qw_history *, qa_error *);
#endif
