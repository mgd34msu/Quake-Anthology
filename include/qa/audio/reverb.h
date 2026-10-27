/*
 * Port of src/audio/reverb.ts: Freeverb-style stereo network and the
 * underwater high-shelf. Samples are double arrays (TS Float64Array),
 * processed in place. init returns false only on OOM.
 * GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_REVERB_H
#define QA_AUDIO_REVERB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/audio/reverb_presets.h"

typedef struct qa_reverb_comb {
  double *buffer;
  size_t length;
  size_t index;
  double store;
  double feedback;
  double damping;
} qa_reverb_comb;

typedef struct qa_reverb_allpass {
  double *buffer;
  size_t length;
  size_t index;
  double feedback;
} qa_reverb_allpass;

typedef struct qa_reverb_network {
  qa_reverb_comb combs[8];
  qa_reverb_allpass allpasses[4];
} qa_reverb_network;

typedef struct qa_stereo_reverb {
  int64_t sample_rate;
  qa_reverb_network left;
  qa_reverb_network right;
  double *delay_l;
  double *delay_r;
  size_t delay_length;
  size_t position;
  double shelf_l;
  double shelf_r;
} qa_stereo_reverb;

bool qa_stereo_reverb_init(qa_stereo_reverb *reverb, int64_t sample_rate);
void qa_stereo_reverb_destroy(qa_stereo_reverb *reverb);
void qa_stereo_reverb_process(qa_stereo_reverb *reverb, double *samples,
                              size_t sample_count,
                              const qa_efx_reverb_params *params);
void qa_stereo_reverb_reset(qa_stereo_reverb *reverb);

typedef struct qa_underwater_filter {
  int64_t sample_rate;
  double left_z1, left_z2, right_z1, right_z2;
} qa_underwater_filter;

void qa_underwater_filter_init(qa_underwater_filter *filter,
                               int64_t sample_rate);
/* high_frequency_gain defaults to 0.25 in TS; pass it explicitly. */
void qa_underwater_filter_process(qa_underwater_filter *filter,
                                  double *samples, size_t sample_count,
                                  double high_frequency_gain);
void qa_underwater_filter_reset(qa_underwater_filter *filter);

#endif
