#ifndef QA_REVERB_PRESETS_H
#define QA_REVERB_PRESETS_H

#include "qa/audio.h"

enum { QA_REVERB_PRESET_PLAIN = 19 };

/* Order and values follow src/audio/reverb-presets.ts. The final four
 * floating parameters are common to every donor EFX preset. */
#define QA_PRESET(name_, density_, diffusion_, gain_, hf_, lf_, decay_, dhf_, dlf_, \
                  rg_, rd_, lg_, ld_, et_, ed_, mt_, md_, limit_) \
    {name_, {density_, diffusion_, gain_, hf_, lf_, decay_, dhf_, dlf_, \
             rg_, rd_, lg_, ld_, et_, ed_, mt_, md_, \
             0.9943f, 5000.0f, 250.0f, 0.0f, limit_}}

static const struct {
    const char *name;
    qa_audio_reverb_params params;
} qa_reverb_presets[] = {
    QA_PRESET("generic", 1.0f, 1.0f, 0.3162f, 0.8913f, 1.0f,
              1.49f, 0.83f, 1.0f, 0.05f, 0.007f, 1.2589f, 0.011f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("padded_cell", 0.1715f, 1.0f, 0.3162f, 0.001f, 1.0f,
              0.17f, 0.1f, 1.0f, 0.25f, 0.001f, 1.2691f, 0.002f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("room", 0.4287f, 1.0f, 0.3162f, 0.5929f, 1.0f,
              0.4f, 0.83f, 1.0f, 0.1503f, 0.002f, 1.0629f, 0.003f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("bathroom", 0.1715f, 1.0f, 0.3162f, 0.2512f, 1.0f,
              1.49f, 0.54f, 1.0f, 0.6531f, 0.007f, 3.2734f, 0.011f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("living_room", 0.9766f, 1.0f, 0.3162f, 0.001f, 1.0f,
              0.5f, 0.1f, 1.0f, 0.2051f, 0.003f, 0.2805f, 0.004f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("stone_room", 1.0f, 1.0f, 0.3162f, 0.7079f, 1.0f,
              2.31f, 0.64f, 1.0f, 0.4411f, 0.012f, 1.1003f, 0.017f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("auditorium", 1.0f, 1.0f, 0.3162f, 0.5781f, 1.0f,
              4.32f, 0.59f, 1.0f, 0.4032f, 0.02f, 0.717f, 0.03f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("concert_hall", 1.0f, 1.0f, 0.3162f, 0.5623f, 1.0f,
              3.92f, 0.7f, 1.0f, 0.2427f, 0.02f, 0.9977f, 0.029f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("cave", 1.0f, 1.0f, 0.3162f, 1.0f, 1.0f,
              2.91f, 1.3f, 1.0f, 0.5f, 0.015f, 0.7063f, 0.022f,
              0.25f, 0.0f, 0.25f, 0.0f, false),
    QA_PRESET("arena", 1.0f, 1.0f, 0.3162f, 0.4477f, 1.0f,
              7.24f, 0.33f, 1.0f, 0.2612f, 0.02f, 1.0186f, 0.03f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("hangar", 1.0f, 1.0f, 0.3162f, 0.3162f, 1.0f,
              10.05f, 0.23f, 1.0f, 0.5f, 0.02f, 1.256f, 0.03f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("carpeted_hallway", 0.4287f, 1.0f, 0.3162f, 0.01f, 1.0f,
              0.3f, 0.1f, 1.0f, 0.1215f, 0.002f, 0.1531f, 0.03f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("hallway", 0.3645f, 1.0f, 0.3162f, 0.7079f, 1.0f,
              1.49f, 0.59f, 1.0f, 0.2458f, 0.007f, 1.6615f, 0.011f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("stone_corridor", 1.0f, 1.0f, 0.3162f, 0.7612f, 1.0f,
              2.7f, 0.79f, 1.0f, 0.2472f, 0.013f, 1.5758f, 0.02f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("alley", 1.0f, 0.3f, 0.3162f, 0.7328f, 1.0f,
              1.49f, 0.86f, 1.0f, 0.25f, 0.007f, 0.9954f, 0.011f,
              0.125f, 0.95f, 0.25f, 0.0f, true),
    QA_PRESET("forest", 1.0f, 0.3f, 0.3162f, 0.0224f, 1.0f,
              1.49f, 0.54f, 1.0f, 0.0525f, 0.162f, 0.7682f, 0.088f,
              0.125f, 1.0f, 0.25f, 0.0f, true),
    QA_PRESET("city", 1.0f, 0.5f, 0.3162f, 0.3981f, 1.0f,
              1.49f, 0.67f, 1.0f, 0.073f, 0.007f, 0.1427f, 0.011f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("mountains", 1.0f, 0.27f, 0.3162f, 0.0562f, 1.0f,
              1.49f, 0.21f, 1.0f, 0.0407f, 0.3f, 0.1919f, 0.1f,
              0.25f, 1.0f, 0.25f, 0.0f, false),
    QA_PRESET("quarry", 1.0f, 1.0f, 0.3162f, 0.3162f, 1.0f,
              1.49f, 0.83f, 1.0f, 0.0f, 0.061f, 1.7783f, 0.025f,
              0.125f, 0.7f, 0.25f, 0.0f, true),
    QA_PRESET("plain", 1.0f, 0.21f, 0.3162f, 0.1f, 1.0f,
              1.49f, 0.5f, 1.0f, 0.0585f, 0.179f, 0.1089f, 0.1f,
              0.25f, 1.0f, 0.25f, 0.0f, true),
    QA_PRESET("parking_lot", 1.0f, 1.0f, 0.3162f, 1.0f, 1.0f,
              1.65f, 1.5f, 1.0f, 0.2082f, 0.008f, 0.2652f, 0.012f,
              0.25f, 0.0f, 0.25f, 0.0f, false),
    QA_PRESET("sewer_pipe", 0.3071f, 0.8f, 0.3162f, 0.3162f, 1.0f,
              2.81f, 0.14f, 1.0f, 1.6387f, 0.014f, 3.2471f, 0.021f,
              0.25f, 0.0f, 0.25f, 0.0f, true),
    QA_PRESET("underwater", 0.3645f, 1.0f, 0.3162f, 0.01f, 1.0f,
              1.49f, 0.1f, 1.0f, 0.5963f, 0.007f, 7.0795f, 0.011f,
              0.25f, 0.0f, 1.18f, 0.348f, true),
    QA_PRESET("drugged", 0.4287f, 0.5f, 0.3162f, 1.0f, 1.0f,
              8.39f, 1.39f, 1.0f, 0.876f, 0.002f, 3.1081f, 0.03f,
              0.25f, 0.0f, 0.25f, 1.0f, false),
    QA_PRESET("dizzy", 0.3645f, 0.6f, 0.3162f, 0.631f, 1.0f,
              17.23f, 0.56f, 1.0f, 0.1392f, 0.02f, 0.4937f, 0.03f,
              0.25f, 1.0f, 0.81f, 0.31f, false),
    QA_PRESET("psychotic", 0.0625f, 0.5f, 0.3162f, 0.8404f, 1.0f,
              7.56f, 0.91f, 1.0f, 0.4864f, 0.02f, 2.4378f, 0.03f,
              0.25f, 0.0f, 4.0f, 1.0f, false)
};

_Static_assert(sizeof(qa_reverb_presets) / sizeof(qa_reverb_presets[0]) == 26,
               "Keep the complete donor reverb preset table");

#undef QA_PRESET
#endif
