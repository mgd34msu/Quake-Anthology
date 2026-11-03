/* Port of src/network/q1/recording.ts. Observed messages are deep-copied
 * into retained seed slots (insertion order preserved, like TS Map).
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_RECORDING_H
#define QA_NETWORK_Q1_RECORDING_H
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/protocol.h"
#include "qa/network/q1/netquake.h"
typedef struct qa_netquake_recording_state qa_netquake_recording_state;
void qa_netquake_recording_state_init(qa_netquake_recording_state *s);
void qa_netquake_recording_state_destroy(qa_netquake_recording_state *s);
/* Heap instance for callers that cannot see the struct size; NULL on OOM. */
qa_netquake_recording_state *qa_netquake_recording_state_create(void);
void qa_netquake_recording_state_free(qa_netquake_recording_state *s);
void qa_netquake_recording_state_observe(qa_netquake_recording_state *s,
                                         const qa_netquake_message *messages, size_t count);
/* Malloc'd array of malloc'd datagrams + lengths; caller frees. */
uint8_t **qa_netquake_recording_state_seed(qa_netquake_recording_state *s,
                                           qa_q1_protocol_identity protocol,
                                           size_t **out_lens, size_t *out_count);
#endif
