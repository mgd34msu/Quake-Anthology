/* Port of src/network/q1/profile.ts. GPL-2.0-or-later. */
#include "qa/network/q1/profile.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/codecs/nq15.h"
#include "qa/network/q1/codecs/qw28.h"
#include "qa/network/q1/codecs/qw29.h"
#include "qa/network/q1/codecs/wide.h"
#include <math.h>
#include "qa/foundation/js.h"
/* ToInt32 (NaN and infinities are 0; the fmod form cast NaN to uint32_t). */
static int32_t ji(double n) { return qa_js_to_int32(n); }
qa_q1_protocol_identity qa_netquake_profile(double version, double flags) {
  qa_q1_protocol_identity id; id.kind = ""; id.version = 0; id.flags = 0; id.revision = 0;
  if (version == 15 || version == 666 || version == 999) id.version = (int64_t)version;
  if (version == 15) { id.kind = "q1-netquake"; return id; }
  if (version == 666) { id.kind = "q1-fitzquake"; return id; }
  if (version == 999) {
    if ((ji(flags) & ~QA_Q1_PRFL_SUPPORTED) != 0) qa_raise("Error", 0, 0, "Unsupported RMQ flags %s", QA_JS_NUMBER(flags));
    id.kind = "q1-rmq"; id.flags = (int64_t)(uint32_t)ji(flags); return id;
  }
  qa_raise("Error", 0, 0, "Unsupported NetQuake protocol %s", QA_JS_NUMBER(version));
}
qa_q1_protocol_identity qa_default_netquake_profile(double version) {
  return qa_netquake_profile(version, version == 999 ? (QA_Q1_PRFL_INT32COORD | QA_Q1_PRFL_SHORTANGLE) : 0);
}
qa_quakeworld_profile qa_quakeworld_profile_make(double version, double flags) {
  qa_quakeworld_profile id; id.kind = ""; id.version = 0; id.flags = 0; id.revision = 0;
  if (version == 28 || version == 29) id.version = (int64_t)version;
  if (version == 28) { id.kind = "q1-quakeworld"; return id; }
  if (version == 29 && (ji(flags) & ~QA_Q1_PRFL_SUPPORTED) == 0) {
    id.kind = "q1-quakeworld-donor-wide"; id.flags = (int64_t)(uint32_t)ji(flags); return id;
  }
  qa_raise("Error", 0, 0, "Unsupported QuakeWorld protocol %s / flags %s", QA_JS_NUMBER(version), QA_JS_NUMBER(flags));
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
