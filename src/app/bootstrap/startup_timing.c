/* Port of src/app/bootstrap/startup-timing.ts. performance.now -> clock_gettime. */
#include "qa/app/bootstrap/startup_timing.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "qa/foundation/js.h"
static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
static double started = -1, last = -1;
void qa_app_startup_stamp(const char *label) {
  if (started < 0) { started = now_ms(); last = started; }
  if (getenv("QTS_STARTUP_TIMING") == NULL) return;
  double now = now_ms();
  char delta[QA_JS_TO_FIXED_CAPACITY], total[QA_JS_TO_FIXED_CAPACITY]; /* toFixed(0) */
  qa_js_to_fixed(now - last, 0, delta, sizeof delta);
  qa_js_to_fixed(now - started, 0, total, sizeof total);
  fprintf(stderr, "[startup] %s: +%sms (total %sms)\n", label, delta, total);
  last = now;
}
