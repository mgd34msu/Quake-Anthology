/*
 * Port of src/ui/hud/q2-rerelease-layout.ts: NativeQ2HudTable,
 * NativeQ2HudEnvironment, q2RereleaseLayout, q2RereleaseInventory.
 * Original rerelease cg_screen.cpp layout and inventory grammar.
 * C accommodations:
 *  - The table is a mutable owned structure (TS rows string[][] is
 *    spliced/pushed by start_table/table_row); create it with
 *    _init and free with _destroy. environment.table NULL is the
 *    TS absent table (a fresh empty table is used per call).
 *  - measure returns qa_vec2; localize returns a borrowed string
 *    and receives the TS args array. Both share environment ctx.
 *  - The TS cell() UTF-8 truncation to 23 bytes with lossy decode
 *    is reproduced as: cut at 23 bytes, then drop a trailing
 *    incomplete UTF-8 sequence (TextDecoder would emit U+FFFD for
 *    it; only non-ASCII cells cut mid-sequence can differ).
 *  - Plain TS Errors and RangeErrors abort via qa_range_error.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_HUD_Q2_RERELEASE_LAYOUT_H
#define QA_UI_HUD_Q2_RERELEASE_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/ui/hud/q2_native.h"

/** TS NativeQ2HudTable (mutable, owned). */
typedef struct qa_native_q2_hud_table {
  char ***rows; /* rows[r][c] owned */
  size_t *row_cell_counts;
  size_t row_count, row_capacity;
  double *columns;
  size_t column_count, column_capacity;
} qa_native_q2_hud_table;

void qa_native_q2_hud_table_init(qa_native_q2_hud_table *table);
void qa_native_q2_hud_table_destroy(qa_native_q2_hud_table *table);

/** TS NativeQ2HudEnvironment. */
struct qa_native_q2_hud_environment {
  qa_native_q2_hud_table *table; /* optional, borrowed */
  bool use_font;
  double font_line_height;
  void *ctx;
  qa_vec2 (*measure)(void *ctx, const char *text);
  const char *(*localize)(void *ctx, const char *text,
                          const char *const *args, size_t arg_count);
};

/* TS q2RereleaseLayout. Appends to out. False only on OOM. */
bool qa_q2_rerelease_layout_into(const char *source,
                                 const qa_native_q2_hud_frame *frame,
                                 double width, double height,
                                 qa_native_q2_hud_environment *environment,
                                 const qa_native_q2_hud_arsenal *arsenal,
                                 qa_native_q2_hud_operations *out);
bool qa_q2_rerelease_layout(const char *source,
                            const qa_native_q2_hud_frame *frame, double width,
                            double height,
                            qa_native_q2_hud_environment *environment,
                            const qa_native_q2_hud_arsenal *arsenal,
                            qa_native_q2_hud_operations *out);

/* TS q2RereleaseInventory. inventory NULL is TS undefined.
 * Appends to out (_into) or fills a fresh result. False only on OOM. */
bool qa_q2_rerelease_inventory_into(
    const qa_native_q2_hud_frame *frame, double width, double height,
    qa_native_q2_hud_environment *environment,
    const qa_native_q2_inventory_readout *inventory,
    qa_native_q2_hud_operations *out);
bool qa_q2_rerelease_inventory(const qa_native_q2_hud_frame *frame,
                               double width, double height,
                               qa_native_q2_hud_environment *environment,
                               const qa_native_q2_inventory_readout *inventory,
                               qa_native_q2_hud_operations *out);

#endif
