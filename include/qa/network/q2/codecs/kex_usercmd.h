/* Port of src/network/q2/codecs/kex-usercmd.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q2_CODECS_KEX_USERCMD_H
#define QA_NETWORK_Q2_CODECS_KEX_USERCMD_H

#include "qa/network/q2/message.h"
#include "qa/network/q2/state.h"

/* TS readKexUsercmd / writeKexUsercmd. */
void qa_q2_read_kex_usercmd(qa_q2_sizebuf *message, const qa_q2_wire_usercmd *from,
                            qa_q2_wire_usercmd *to);
void qa_q2_write_kex_usercmd(qa_q2_sizebuf *message, const qa_q2_wire_usercmd *from,
                             const qa_q2_wire_usercmd *to);

#endif
