#ifndef QA_Q2PRO_INTERNAL_H
#define QA_Q2PRO_INTERNAL_H
#include "internal.h"
#include "qa/network_q2_batch.h"
bool qa_q2pro_extensions(const qa_q2_codec *);
bool qa_q2pro_extensions_v2(const qa_q2_codec *);
bool qa_q2pro_entity_header(qa_q2_codec *, qa_net_reader *, uint32_t *, uint64_t *);
bool qa_q2pro_entity_read(qa_q2_codec *, qa_net_reader *, const qa_q2_entity *, uint32_t, uint64_t, qa_q2_entity *);
bool qa_q2pro_entity_write(qa_q2_codec *, qa_net_writer *, const qa_q2_entity *, const qa_q2_entity *, bool, bool);
bool qa_q2pro_entity_remove(qa_q2_codec *, qa_net_writer *, uint32_t);
#endif
