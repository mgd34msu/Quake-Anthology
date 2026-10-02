#ifndef QA_NETWORK_KEX_SAVE_H
#define QA_NETWORK_KEX_SAVE_H

#include "qa/network_q2_kex.h"

/* Channel restoration retains complete backing extents, ordered reliable
 * packets, serial state, acknowledgements, fragment bytes and retry clocks.
 * The real emitter is borrowed; decoding does not invoke it. */
bool qa_kex_channel_checkpoint(const qa_kex_channel *, qa_buffer *, qa_error *);
bool qa_kex_channel_restore(qa_bytes, qa_kex_emit_fn, void *, qa_kex_channel **, qa_error *);

/* LAN decode is detached: no transport, sends, callbacks or lobby mutations.
 * Binding takes a genuine transport only after qualification; close remains
 * the published owner's transport and disconnect lifecycle. */
bool qa_kex_lan_checkpoint(const qa_kex_lan *, qa_buffer *, qa_error *);
bool qa_kex_lan_restore(qa_bytes, qa_kex_lan **, qa_error *);
bool qa_kex_lan_bind(qa_kex_lan *, qa_net_transport *, qa_error *);
void qa_kex_lan_destroy_detached(qa_kex_lan *);

#endif
