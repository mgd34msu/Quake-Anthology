/* Port of src/network/q1/profile.ts. GPL-2.0-or-later. */
#include "qa/network/q1/profile.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/codecs/nq15.h"
#include "qa/network/q1/codecs/qw28.h"
#include "qa/network/q1/codecs/qw29.h"
#include "qa/network/q1/codecs/wide.h"
#include <math.h>
static int32_t ji(double n) { double m = fmod(trunc(n), 4294967296.0); if (m < 0) m += 4294967296.0; return (int32_t)(uint32_t)m; }
qa_q1_protocol_identity qa_netquake_profile(double version, double flags) {
  qa_q1_protocol_identity id; id.kind = ""; id.version = (int64_t)version; id.flags = 0; id.revision = 0;
  if (version == 15) { id.kind = "q1-netquake"; return id; }
  if (version == 666) { id.kind = "q1-fitzquake"; return id; }
  if (version == 999) {
    if ((ji(flags) & ~QA_Q1_PRFL_SUPPORTED) != 0) qa_range_error("Unsupported RMQ flags %g", flags);
    id.kind = "q1-rmq"; id.flags = (int64_t)flags; return id;
  }
  qa_range_error("Unsupported NetQuake protocol %g", version);
}
qa_q1_protocol_identity qa_default_netquake_profile(double version) {
  return qa_netquake_profile(version, version == 999 ? (QA_Q1_PRFL_INT32COORD | QA_Q1_PRFL_SHORTANGLE) : 0);
}
qa_quakeworld_profile qa_quakeworld_profile_make(double version, double flags) {
  qa_quakeworld_profile id; id.kind = ""; id.version = (int64_t)version; id.flags = 0; id.revision = 0;
  if (version == 28) { id.kind = "q1-quakeworld"; return id; }
  if (version == 29 && (ji(flags) & ~QA_Q1_PRFL_SUPPORTED) == 0) {
    id.kind = "q1-quakeworld-donor-wide"; id.flags = (int64_t)flags; return id;
  }
  qa_range_error("Unsupported QuakeWorld protocol %g / flags %g", version, flags);
}
void qa_create_netquake_codec(qa_q1_protocol_identity profile, qa_message_reader *reader, qa_protocol_codec *out) {
  if (profile.version == 15) { qa_create_nq15_codec(reader, out); return; }
  if (profile.version == 666) { qa_make_wide_codec(reader, 666, "FitzQuake", 0, false, out); return; }
  if (profile.version == 999) { qa_make_wide_codec(reader, 999, "RMQ", QA_Q1_PRFL_INT32COORD | QA_Q1_PRFL_SHORTANGLE, true, out); return; }
  /* TS falls through returning undefined; fail loudly instead. */
  qa_range_error("Unsupported NetQuake protocol %lld", (long long)profile.version);
}
void qa_create_quakeworld_codec(qa_quakeworld_profile profile, qa_message_reader *reader, qa_qw_protocol_codec *out) {
  if (profile.version == 28) qa_create_qw28_codec(reader, out);
  else qa_create_qw29_codec(reader, out);
}
double qa_q1_protocol_flags(qa_protocol_identity profile) { return (double)profile.flags; }
