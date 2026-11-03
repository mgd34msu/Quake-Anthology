/*
 * Port of src/audio/source-paint.ts (Q3 snd_mix.c S_WriteLinearBlastStereo16).
 * The ADPCM/wavelet chunk painter, resampler and DMA transfer were removed
 * from TS with audio/adpcm.ts and audio/wavelet.ts.
 * GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_SOURCE_PAINT_H
#define QA_AUDIO_SOURCE_PAINT_H

#include <stddef.h>
#include <stdint.h>

/* Two TS overloads (Int32Array / Float64Array paint). */
void qa_write_linear_blast_stereo16_i32(const int32_t *paint,
                                        size_t paint_length, int16_t *output,
                                        size_t output_length, int64_t count);
void qa_write_linear_blast_stereo16_f64(const double *paint,
                                        size_t paint_length, int16_t *output,
                                        size_t output_length, int64_t count);

#endif
