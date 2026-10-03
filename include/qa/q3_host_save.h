#ifndef QA_Q3_HOST_SAVE_H
#define QA_Q3_HOST_SAVE_H

#include "qa/q3_host.h"

/* The application role owner must qualify every borrowed service and keep its
 * candidate address stable. This inventory contains values and presence only;
 * it never serializes a callback or heap address or invokes a service. */
bool qa_q3_host_checkpoint_services(const qa_q3_host *, qa_buffer *, qa_error *);
/* QVM or an actual returned owned-native executor supplies mutable memory.
 * Writable streams and shared global script defines retain separate detached
 * service owners; their installed state cannot be certified by path alone. */
bool qa_q3_host_checkpoint_portable_ready(const qa_q3_host *, qa_error *);
/* Qualify the complete immutable incoming host stream before a coupled VM
 * import can reach ordinary file restoration. No resources are opened. An
 * optional output borrows the exact entity source span after full validation;
 * the caller must qualify it against its retained source before binding it. */
bool qa_q3_host_checkpoint_portable_state(qa_bytes, qa_bytes *entity_source, qa_error *);
/* Source admission retained by an idle pending client disconnect. */
bool qa_q3_host_checkpoint_input_retired(const qa_q3_host *, uint32_t, bool *, qa_error *);

#endif
