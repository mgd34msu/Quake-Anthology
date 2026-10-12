#ifndef QA_RENDER_OUTPUT_GAMMA_H
#define QA_RENDER_OUTPUT_GAMMA_H

#include <stdint.h>

typedef enum qa_output_gamma_precision {
    /* Float sample and reciprocal, double pow/floor; identity bypasses math. */
    QA_OUTPUT_GAMMA_MIXED,
    /* Float pow, clamping and floor. */
    QA_OUTPUT_GAMMA_FLOAT
} qa_output_gamma_precision;

/* The output owner supplies its already admitted gamma in 0.5..3. */
void qa_output_gamma_table(float gamma, qa_output_gamma_precision,
    uint8_t table[256]);

#endif
