/* Port of src/render/output-gamma.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_OUTPUT_GAMMA_H
#define QA_RENDER_OUTPUT_GAMMA_H
#include <stdbool.h>
#include <stdint.h>
/* TS returns Uint8Array|null (null iff gamma==1). C: false iff null;
 * on true table holds the 256-entry table. RangeError -> qa_range_error. */
bool qa_output_gamma_table(double gamma, uint8_t table[256]);
#endif
