#ifndef QA_Q3_HOST_SAVE_H
#define QA_Q3_HOST_SAVE_H

#include "qa/q3_host.h"

/* The application role owner must qualify every borrowed service and keep its
 * candidate address stable. This inventory contains values and presence only;
 * it never serializes a callback or heap address or invokes a service. */
bool qa_q3_host_checkpoint_services(const qa_q3_host *, qa_buffer *, qa_error *);
/* Writable streams and shared global script defines need separate detached
 * service owners. Their installed state cannot be certified by path alone. */
bool qa_q3_host_checkpoint_portable_ready(const qa_q3_host *, qa_error *);
/* Qualify the complete immutable incoming host stream before a coupled VM
 * import can reach ordinary file restoration. No resources are opened. */
bool qa_q3_host_checkpoint_portable_state(qa_bytes, qa_error *);
/* Source admission retained by an idle pending client disconnect. */
bool qa_q3_host_checkpoint_input_retired(const qa_q3_host *, uint32_t, bool *, qa_error *);

#endif
