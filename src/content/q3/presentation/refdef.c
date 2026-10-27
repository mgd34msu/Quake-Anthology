/* Port of src/content/q3/presentation/refdef.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/refdef.h"
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
void qa_q3_pres_copy_render_text(const qa_q3_pres_render_row *rows, size_t count, qa_q3_pres_render_text *out) {
  if (count != 8) qa_range_error("refdef requires eight render strings");
  for (size_t i = 0; i < 8; i++) {
    const qa_q3_pres_render_row *row = &rows[i];
    bool has_nul = false;
    size_t stored = row->length < 32 ? row->length : 32;
    for (size_t j = 0; j < stored; j++) {
      if (row->bytes[j] == 0) has_nul = true;
    }
    if (row->length > 32 || (row->length == 32 && !has_nul))
      qa_range_error("refdef render string requires a NUL within 32 bytes");
    /* TS also rejects charCodeAt > 255; byte rows cannot hold such a unit. */
  }
  for (size_t i = 0; i < 8; i++) out->rows[i] = rows[i];
}
qa_q3_pres_refdef qa_q3_pres_create_refdef(void) {
  qa_q3_pres_refdef refdef;
  memset(&refdef, 0, sizeof refdef);
  refdef.view_origin = qa_math_vec3(0, 0, 0);
  for (int i = 0; i < 3; i++) refdef.view_axis.vectors[i] = qa_math_vec3(0, 0, 0);
  refdef.area_mask_length = 32; /* new Uint8Array(32); text rows are "" */
  return refdef;
}
qa_q3_pres_refdef qa_q3_pres_copy_refdef(const qa_q3_pres_refdef *source) {
  if (source->area_mask_length != 32) qa_range_error("refdef requires 32 area-mask bytes");
  qa_q3_pres_refdef copy = *source;
  qa_q3_pres_copy_render_text(source->text.rows, 8, &copy.text);
  return copy;
}
