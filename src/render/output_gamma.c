#include "output_gamma.h"

#include <math.h>
#include <stddef.h>

void qa_output_gamma_table(float gamma, qa_output_gamma_precision precision,
    uint8_t table[256])
{
    for (size_t i = 0; i < 256; ++i) {
        if (precision == QA_OUTPUT_GAMMA_FLOAT) {
            float value = powf((float)i / 255.0f, 1.0f / gamma);
            table[i] = (uint8_t)floorf(fminf(fmaxf(value, 0), 1) * 255 + 0.5f);
        } else {
            table[i] = gamma == 1 ? (uint8_t)i :
                (uint8_t)floor(pow((float)i / 255, 1.0f / gamma) * 255 + .5);
        }
    }
}
