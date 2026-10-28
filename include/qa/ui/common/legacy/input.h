/*
 * Port of src/ui/common/legacy/input.ts: LegacyUiSeat, the
 * per-seat adapter from SeatInputEvent to the legacy UiRuntime.
 * C accommodations:
 *  - Async drain/input are synchronous (PORTING.md). Pending
 *    events are copied by value (text duplicated) on route().
 *  - Text is decoded as UTF-8 codepoints (the SDL text form);
 *    codepoints above 255 are dropped, as in TS.
 *  - Plain TS Error throws abort via qa_ui_script_plain_error.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_LEGACY_INPUT_H
#define QA_UI_COMMON_LEGACY_INPUT_H

#include <stdbool.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/ui.h"
#include "qa/text/draw2d.h"
#include "qa/ui/common/legacy/runtime.h"

/** TS LegacyUiSeat. Runtime borrowed (disposed by close()). */
typedef struct qa_legacy_ui_seat qa_legacy_ui_seat;

/* NULL only on OOM. */
qa_legacy_ui_seat *qa_legacy_ui_seat_create(qa_seat_id seat,
                                            qa_ui_runtime *runtime);
void qa_legacy_ui_seat_destroy(qa_legacy_ui_seat *seat);

qa_seat_id qa_legacy_ui_seat_seat(const qa_legacy_ui_seat *seat);
qa_ui_runtime *qa_legacy_ui_seat_runtime(const qa_legacy_ui_seat *seat);
qa_vec2 qa_legacy_ui_seat_cursor(const qa_legacy_ui_seat *seat);

bool qa_legacy_ui_seat_route(qa_legacy_ui_seat *seat,
                             const qa_seat_input_event *event);
void qa_legacy_ui_seat_drain(qa_legacy_ui_seat *seat, const qa_draw2d *draw);
bool qa_legacy_ui_seat_input(qa_legacy_ui_seat *seat,
                             const qa_seat_input_event *event,
                             const qa_draw2d *draw);
void qa_legacy_ui_seat_close(qa_legacy_ui_seat *seat);

#endif
