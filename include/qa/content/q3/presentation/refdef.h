/* Port of src/content/q3/presentation/refdef.ts. GPL-2.0-or-later.
 * refdef_t from id Software's code/cgame/tr_types.h.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 * TS RenderText rows are byte strings (may embed NUL); each row here is
 * bytes+length, length may exceed 32 only to reach the TS RangeError. */
#ifndef QA_CONTENT_Q3_PRESENTATION_REFDEF_H
#define QA_CONTENT_Q3_PRESENTATION_REFDEF_H
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
enum { QA_Q3_PRES_RDF_NOWORLDMODEL = 1, QA_Q3_PRES_RDF_HYPERSPACE = 4 };
typedef struct qa_q3_pres_render_row {
  uint8_t bytes[32];
  size_t length; /* TS row.length; bytes beyond 32 are not stored */
} qa_q3_pres_render_row;
typedef struct qa_q3_pres_render_text { qa_q3_pres_render_row rows[8]; } qa_q3_pres_render_text;
/** refdef_t has eight 32-byte rows. Unbounded strlen on an unterminated row
 * is outside the supported source domain. Aborts (qa_range_error) with the
 * exact TS RangeError messages. */
void qa_q3_pres_copy_render_text(const qa_q3_pres_render_row *rows, size_t count, qa_q3_pres_render_text *out);
typedef struct qa_q3_pres_refdef {
  double x, y, width, height, fov_x, fov_y;
  qa_vec3 view_origin;
  qa_axis view_axis;
  double time, render_flags;
  uint8_t area_mask[32];
  size_t area_mask_length; /* TS areaMask.length; create sets 32 */
  qa_q3_pres_render_text text;
} qa_q3_pres_refdef;
qa_q3_pres_refdef qa_q3_pres_create_refdef(void);
/** Scene publication owns arrays and preserves the caller's exact basis and FOV. */
qa_q3_pres_refdef qa_q3_pres_copy_refdef(const qa_q3_pres_refdef *source);
#endif
