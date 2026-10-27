/* Port of src/network/q1/profile.ts. Identities reuse the contracts
 * qa_protocol_identity (kind string + version + flags). Errors abort
 * via qa_range_error with the TS message. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_PROFILE_H
#define QA_NETWORK_Q1_PROFILE_H
#include "qa/contracts/protocol.h"
#include "qa/network/q1/codecs/codec.h"
typedef qa_protocol_identity qa_quakeworld_profile;
/* RereleaseMessages: "known-retail" | "q1-rerelease-private" (string). */
qa_q1_protocol_identity qa_netquake_profile(double version, double flags);
qa_q1_protocol_identity qa_default_netquake_profile(double version);
qa_quakeworld_profile qa_quakeworld_profile_make(double version, double flags);
void qa_create_netquake_codec(qa_q1_protocol_identity profile, qa_message_reader *reader, qa_protocol_codec *out);
void qa_create_quakeworld_codec(qa_quakeworld_profile profile, qa_message_reader *reader, qa_qw_protocol_codec *out);
double qa_q1_protocol_flags(qa_protocol_identity profile);
#endif
