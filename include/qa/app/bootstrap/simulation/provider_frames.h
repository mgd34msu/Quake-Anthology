/* Port of src/app/bootstrap/simulation/provider-frames.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PROVIDER_FRAMES_H
#define QA_APP_BOOTSTRAP_SIMULATION_PROVIDER_FRAMES_H
#include "qa/contracts/time.h"
qa_frame_context qa_app_sim_provider_frame(qa_frame_context frame, qa_clock_profile world_profile, qa_clock_profile provider_profile);
#endif
