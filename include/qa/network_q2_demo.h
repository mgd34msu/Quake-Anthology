#ifndef QA_NETWORK_Q2_DEMO_H
#define QA_NETWORK_Q2_DEMO_H

#include "qa/network_q2_mvd.h"

typedef enum qa_q2_recording_kind {
    QA_Q2_RECORDING_CLIENT, QA_Q2_RECORDING_MVD, QA_Q2_RECORDING_SERVER
} qa_q2_recording_kind;
typedef struct qa_q2_recording_header {
    qa_q2_recording_kind kind;
    uint32_t recorded_version;
    qa_net_protocol_id protocol;
    qa_q2_serverdata data;
} qa_q2_recording_header;

/* Native DM2 records borrow their file. EOF and the -1 footer are distinct;
 * either returns present=false. MVD keeps its existing canonical framer. */
bool qa_q2_demo_read_packet(qa_net_reader *, qa_bytes *, bool *present, bool *terminated);
/* Admission-only, non-consuming protocol/kind detection across the recorded
 * preamble. No callback is run and no recorded camera is synthesized. */
bool qa_q2_recording_probe(qa_bytes, qa_q2_recording_header *, qa_error *);
/* Non-frame messages are returned intact in multicasts for the one ordinary
 * message decoder. A frame borrows caller arena storage, has zero players,
 * and returns only its trailing raw multicast bytes in multicasts. */
bool qa_q2_server_demo_read(qa_bytes, qa_arena *, qa_q2_wire_frame *,
    qa_bytes *multicasts, bool *is_frame, qa_error *);
/* Payload encoders. The existing DM2 framer supplies each signed32 length.
 * Configs/entities are source-number ordered and unique. The caller excludes
 * SVF_NOCLIENT actors; the original model/effects/sound/event filter is shared. */
bool qa_q2_server_demo_signon_write(qa_net_writer *, const qa_q2_serverdata *,
    const qa_q2_mvd_config *, size_t);
bool qa_q2_server_demo_frame_write(qa_net_writer *, int32_t server_frame,
    qa_q2_entity_span, qa_bytes multicasts);

#endif
