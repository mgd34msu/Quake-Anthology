/* Port of src/app/bootstrap/startup-timing.ts. performance.now -> clock_gettime. */
#include "qa/app/bootstrap/startup_timing.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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
  fprintf(stderr, "[startup] %s: +%.0fms (total %.0fms)\n", label, now - last, now - started);
  last = now;
}
