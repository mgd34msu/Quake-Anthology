/* Port of src/network/q1/qw-recording.ts. The TS foundation StringTable/
 * IdIndex interning is reproduced with plain arrays; retained order and
 * seed bytes are identical. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_QW_RECORDING_H
#define QA_NETWORK_Q1_QW_RECORDING_H
#include <stddef.h>
#include "qa/network/q1/demos.h"
#include "qa/network/q1/quakeworld.h"
typedef struct qa_quakeworld_recording_state qa_quakeworld_recording_state;
void qa_quakeworld_recording_state_init(qa_quakeworld_recording_state *s);
void qa_quakeworld_recording_state_destroy(qa_quakeworld_recording_state *s);
/* Heap instance for callers that cannot see the struct size; NULL on OOM. */
qa_quakeworld_recording_state *qa_quakeworld_recording_state_create(void);
void qa_quakeworld_recording_state_free(qa_quakeworld_recording_state *s);
void qa_quakeworld_recording_state_observe(qa_quakeworld_recording_state *s,
                                           const qa_quakeworld_message *messages, size_t count);
qa_quakeworld_demo_record *qa_quakeworld_recording_state_seed(qa_quakeworld_recording_state *s,
                                                              const qa_quakeworld_message *data,
                                                              const char *const *models, size_t model_count,
                                                              const char *const *sounds, size_t sound_count,
                                                              double seconds, double outgoing, double incoming,
                                                              size_t *out_count);
#endif
