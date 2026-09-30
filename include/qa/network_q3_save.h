#ifndef QA_NETWORK_Q3_SAVE_H
#define QA_NETWORK_Q3_SAVE_H
#include "qa/network_q3.h"

/* Native field records, separate from live wire messages. Channel restoration
 * owns fresh buffers and publishes only after complete validation. Callback,
 * transport and process-local identity pointers never enter these records. */
bool qa_q3_channel_checkpoint(const qa_q3_channel *, qa_net_writer *);
bool qa_q3_channel_restore(qa_net_reader *, qa_q3_channel **);
qa_q3_role qa_q3_channel_role(const qa_q3_channel *);

/* Capture at a drained runtime boundary. The complete source command/player/
 * entity history and reliable/fragment state survive; parser output scratch
 * and borrowed datagram views do not. Restoring calls no source initialization,
 * signon, send or snapshot hook. The caller admits a new canonical connection
 * identity and qualified candidate callbacks before publication. Continuation
 * against an external endpoint requires the matching remote session cut. */
bool qa_q3_client_peer_checkpoint(const qa_q3_client_peer *, qa_buffer *, qa_error *);
bool qa_q3_client_peer_restore(qa_bytes, qa_q3_identity,
    const qa_q3_client_hooks *, qa_q3_client_peer **, qa_error *);
#endif
