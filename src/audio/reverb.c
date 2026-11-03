/* Port of src/audio/reverb.ts. GPL-2.0-or-later. */
#include "qa/audio/reverb.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

static double clampd(double v, double lo, double hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

static bool comb_init(qa_reverb_comb *comb, int64_t length) {
  size_t len = length < 1 ? 1 : (size_t)length;
  comb->buffer = calloc(len, sizeof *comb->buffer);
  if (comb->buffer == NULL) return false;
  comb->length = len;
  comb->index = 0;
  comb->store = 0;
  comb->feedback = 0;
  comb->damping = 0;
  return true;
}

static double comb_process(qa_reverb_comb *comb, double input) {
  double output = comb->buffer[comb->index];
  comb->store = output * (1 - comb->damping) + comb->store * comb->damping;
  comb->buffer[comb->index] = input + comb->store * comb->feedback;
  comb->index = (comb->index + 1) % comb->length;
  return output;
}

static void comb_reset(qa_reverb_comb *comb) {
  memset(comb->buffer, 0, comb->length * sizeof *comb->buffer);
  comb->store = 0;
  comb->index = 0;
}

static bool allpass_init(qa_reverb_allpass *filter, int64_t length) {
  size_t len = length < 1 ? 1 : (size_t)length;
  filter->buffer = calloc(len, sizeof *filter->buffer);
  if (filter->buffer == NULL) return false;
  filter->length = len;
  filter->index = 0;
  filter->feedback = 0.5;
  return true;
}

static double allpass_process(qa_reverb_allpass *filter, double input) {
  double old = filter->buffer[filter->index];
  filter->buffer[filter->index] = input + old * filter->feedback;
  filter->index = (filter->index + 1) % filter->length;
  return old - input;
}

static void allpass_reset(qa_reverb_allpass *filter) {
  memset(filter->buffer, 0, filter->length * sizeof *filter->buffer);
  filter->index = 0;
}

static bool network_init(qa_reverb_network *network, int64_t rate,
                         int64_t spread) {
  static const int comb_tunings[8] = {1116, 1188, 1277, 1356,
                                      1422, 1491, 1557, 1617};
  static const int allpass_tunings[4] = {556, 441, 341, 225};
  memset(network, 0, sizeof *network);
  for (int i = 0; i < 8; i++)
    if (!comb_init(&network->combs[i],
                   (int64_t)qa_js_round((comb_tunings[i] + spread) * (double)rate / 44100)))
      return false;
  for (int i = 0; i < 4; i++)
    if (!allpass_init(&network->allpasses[i],
                      (int64_t)qa_js_round((allpass_tunings[i] + spread) * (double)rate / 44100)))
      return false;
  return true;
}

static void network_destroy(qa_reverb_network *network) {
  for (int i = 0; i < 8; i++) free(network->combs[i].buffer);
  for (int i = 0; i < 4; i++) free(network->allpasses[i].buffer);
}

static double network_process(qa_reverb_network *network, double input) {
  double output = 0;
  for (int i = 0; i < 8; i++) output += comb_process(&network->combs[i], input);
  for (int i = 0; i < 4; i++)
    output = allpass_process(&network->allpasses[i], output);
  return output;
}

static void network_configure(qa_reverb_network *network,
                              const qa_efx_reverb_params *p, int64_t rate) {
  double damping = clampd(1 - p->decay_hf_ratio, 0, 0.99) +
                   (p->decay_hf_limit ? 0.05 : 0);
  if (damping > 0.99) damping = 0.99;
  /* TS: Math.min(0.99, Math.max(0, 1 - ratio) + (limit ? 0.05 : 0)). */
  for (int i = 0; i < 8; i++) {
    qa_reverb_comb *comb = &network->combs[i];
    comb->feedback = qa_js_min(0.98, qa_js_pow(10, -3 * (double)comb->length /
                                            (qa_js_max(0.001, p->decay_time) * rate)));
    comb->damping = damping;
  }
  double diffusion = clampd(p->diffusion * 0.7 + p->density * 0.3, 0, 1);
  for (int i = 0; i < 4; i++) network->allpasses[i].feedback = 0.6 * diffusion;
}

static void network_reset(qa_reverb_network *network) {
  for (int i = 0; i < 8; i++) comb_reset(&network->combs[i]);
  for (int i = 0; i < 4; i++) allpass_reset(&network->allpasses[i]);
}

bool qa_stereo_reverb_init(qa_stereo_reverb *reverb, int64_t sample_rate) {
  if (sample_rate < 8000 || sample_rate > 192000)
    qa_range_error("Invalid reverb sample rate");
  memset(reverb, 0, sizeof *reverb);
  reverb->sample_rate = sample_rate;
  if (!network_init(&reverb->left, sample_rate, 0)) goto oom;
  if (!network_init(&reverb->right, sample_rate, 23)) goto oom;
  reverb->delay_length = (size_t)ceil(sample_rate * 0.3) + 1;
  reverb->delay_l = calloc(reverb->delay_length, sizeof *reverb->delay_l);
  reverb->delay_r = calloc(reverb->delay_length, sizeof *reverb->delay_r);
  if (reverb->delay_l == NULL || reverb->delay_r == NULL) goto oom;
  return true;
oom:
  qa_stereo_reverb_destroy(reverb);
  return false;
}

void qa_stereo_reverb_destroy(qa_stereo_reverb *reverb) {
  network_destroy(&reverb->left);
  network_destroy(&reverb->right);
  free(reverb->delay_l);
  free(reverb->delay_r);
  memset(reverb, 0, sizeof *reverb);
}

void qa_stereo_reverb_process(qa_stereo_reverb *reverb, double *samples,
                              size_t sample_count,
                              const qa_efx_reverb_params *params) {
  if (sample_count % 2 != 0) qa_range_error("Reverb requires stereo frames");
  network_configure(&reverb->left, params, reverb->sample_rate);
  network_configure(&reverb->right, params, reverb->sample_rate);
  int64_t delay = (int64_t)qa_js_round(params->late_reverb_delay * reverb->sample_rate);
  if (delay < 0) delay = 0;
  if (delay > (int64_t)reverb->delay_length - 1)
    delay = (int64_t)reverb->delay_length - 1;
  double shelf = 1 - exp(-2 * 3.141592653589793 *
                         qa_js_min(reverb->sample_rate * 0.45,
                              qa_js_max(20, params->hf_reference)) /
                         reverb->sample_rate);
  double wet_gain = clampd(params->gain * (params->late_reverb_gain +
                                            0.25 * params->reflections_gain),
                           0, 4) *
                    0.12;
  for (size_t i = 0; i < sample_count; i += 2) {
    double dry_l = samples[i], dry_r = samples[i + 1];
    reverb->delay_l[reverb->position] = dry_l;
    reverb->delay_r[reverb->position] = dry_r;
    size_t index = (reverb->position - (size_t)delay + reverb->delay_length) %
                   reverb->delay_length;
    double wet_l = network_process(&reverb->left, reverb->delay_l[index] * 0.015);
    double wet_r = network_process(&reverb->right, reverb->delay_r[index] * 0.015);
    reverb->position = (reverb->position + 1) % reverb->delay_length;
    reverb->shelf_l += shelf * (wet_l - reverb->shelf_l);
    reverb->shelf_r += shelf * (wet_r - reverb->shelf_r);
    wet_l = reverb->shelf_l + params->gain_hf * (wet_l - reverb->shelf_l);
    wet_r = reverb->shelf_r + params->gain_hf * (wet_r - reverb->shelf_r);
    samples[i] = dry_l + wet_l * wet_gain;
    samples[i + 1] = dry_r + wet_r * wet_gain;
  }
}

void qa_stereo_reverb_reset(qa_stereo_reverb *reverb) {
  network_reset(&reverb->left);
  network_reset(&reverb->right);
  memset(reverb->delay_l, 0, reverb->delay_length * sizeof *reverb->delay_l);
  memset(reverb->delay_r, 0, reverb->delay_length * sizeof *reverb->delay_r);
  reverb->position = 0;
  reverb->shelf_l = 0;
  reverb->shelf_r = 0;
}

void qa_underwater_filter_init(qa_underwater_filter *filter,
                               int64_t sample_rate) {
  memset(filter, 0, sizeof *filter);
  filter->sample_rate = sample_rate;
}

void qa_underwater_filter_process(qa_underwater_filter *filter,
                                  double *samples, size_t sample_count,
                                  double high_frequency_gain) {
  double gain = clampd(high_frequency_gain, 0.001, 1);
  double w = 2 * 3.141592653589793 *
             qa_js_min(5000, filter->sample_rate * 0.45) / filter->sample_rate;
  double c = cos(w), alpha = sin(w) / 2 * 1.4142135623730951;
  double k = 2 * sqrt(gain) * alpha;
  double a0 = gain + 1 - (gain - 1) * c + k;
  double b0 = gain * (gain + 1 + (gain - 1) * c + k) / a0;
  double b1 = -2 * gain * (gain - 1 + (gain + 1) * c) / a0;
  double b2 = gain * (gain + 1 + (gain - 1) * c - k) / a0;
  double a1 = 2 * (gain - 1 - (gain + 1) * c) / a0;
  double a2 = (gain + 1 - (gain - 1) * c - k) / a0;
  for (size_t i = 0; i < sample_count; i++) {
    double *z1 = i % 2 == 0 ? &filter->left_z1 : &filter->right_z1;
    double *z2 = i % 2 == 0 ? &filter->left_z2 : &filter->right_z2;
    double input = samples[i], output = input * b0 + *z1;
    *z1 = input * b1 - output * a1 + *z2;
    *z2 = input * b2 - output * a2;
    samples[i] = output;
  }
}

void qa_underwater_filter_reset(qa_underwater_filter *filter) {
  filter->left_z1 = filter->left_z2 = filter->right_z1 = filter->right_z2 = 0;
}
