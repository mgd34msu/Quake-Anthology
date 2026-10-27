/*
 * Port of src/audio/reverb-presets.ts: EFX preset table (values from
 * /usr/include/AL/efx-presets.h via the TS transcription, in
 * REVERB_PRESET_NAMES order). GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_REVERB_PRESETS_H
#define QA_AUDIO_REVERB_PRESETS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct qa_efx_reverb_params {
  double density;
  double diffusion;
  double gain;
  double gain_hf;
  double gain_lf;
  double decay_time;
  double decay_hf_ratio;
  double decay_lf_ratio;
  double reflections_gain;
  double reflections_delay;
  double late_reverb_gain;
  double late_reverb_delay;
  double echo_time;
  double echo_depth;
  double modulation_time;
  double modulation_depth;
  double air_absorption_gain_hf;
  double hf_reference;
  double lf_reference;
  double room_rolloff_factor;
  bool decay_hf_limit;
} qa_efx_reverb_params;

extern const char *const qa_reverb_preset_names[];
extern const size_t qa_reverb_preset_name_count;
extern const qa_efx_reverb_params qa_reverb_presets[];
extern const size_t qa_reverb_preset_count;

#define QA_REVERB_PRESET_PLAIN 19

#endif
