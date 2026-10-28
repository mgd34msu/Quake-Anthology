/* Port of src/app/bootstrap/simulation/provider-frames.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/provider_frames.h"
static bool starts_interval(qa_clock_profile p) { return p.kind == QA_CLOCK_Q1_NETQUAKE || p.kind == QA_CLOCK_Q1_QUAKEWORLD; }
qa_frame_context qa_app_sim_provider_frame(qa_frame_context frame, qa_clock_profile world_profile, qa_clock_profile provider_profile) {
  if (world_profile.kind == provider_profile.kind) return frame;
  double milliseconds = frame.time.kind == QA_SOURCE_TIME_MILLISECONDS ? frame.time.value : frame.time.value * 1000;
  double elapsed_ms = frame.elapsed.kind == QA_SOURCE_TIME_MILLISECONDS ? frame.elapsed.value : frame.elapsed.value * 1000;
  double start = starts_interval(world_profile) ? milliseconds : milliseconds - elapsed_ms;
  double time = starts_interval(provider_profile) ? start : start + elapsed_ms;
  qa_source_time_kind kind = (provider_profile.kind == QA_CLOCK_Q2_RERELEASE || provider_profile.kind == QA_CLOCK_Q3)
      ? QA_SOURCE_TIME_MILLISECONDS : QA_SOURCE_TIME_SECONDS;
  double divisor = kind == QA_SOURCE_TIME_SECONDS ? 1000 : 1;
  frame.time = (qa_source_time){kind, time / divisor};
  frame.elapsed = (qa_source_time){kind, elapsed_ms / divisor};
  return frame;
}
