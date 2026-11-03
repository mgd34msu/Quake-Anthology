/*
 * Port of src/platform/signal-hint.ts. GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_SIGNAL_HINT_H
#define QA_PLATFORM_SIGNAL_HINT_H

/* TS requireNoSignalHandlers: SDL_SetHint returns 0 when the environment
 * already supplies the hint at default priority; that value is acceptable
 * when it is the one required. Raises Error otherwise. */
void qa_require_no_signal_handlers(int set_result);

#endif
