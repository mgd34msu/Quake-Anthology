/*
 * Port of src/network/q2/codecs/kex-write.ts (createKexWriter).
 * The TS protocol getter closure becomes a pointer to the live
 * protocol value; the returned Writers become vtable slots filled by
 * qa_q2_kex_fill_writers (codec->ctx must point at the binding).
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CODECS_KEX_WRITE_H
#define QA_NETWORK_Q2_CODECS_KEX_WRITE_H

#include "qa/network/q2/codecs/codec.h"

/* Writer binding: TS `protocol: () => number`. */
typedef struct qa_q2_kex_writer_binding {
  const double *protocol_value;
} qa_q2_kex_writer_binding;

/* TS createKexWriter: fills the writer slots of `codec`. */
void qa_q2_kex_fill_writers(qa_q2_protocol_codec *codec,
                            qa_q2_kex_writer_binding *binding);

#endif
