/* Port of src/content/catalog/timing.ts. GPL-2.0-or-later. */
#include "qa/content/catalog/timing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool qa_native_provider_timing(qa_provider_reference provider,
                               qa_game_family family, bool rerelease,
                               qa_provider_timing *out) {
  const char *name = qa_game_family_name(family);
  size_t length = strlen(name) + strlen(":binary32") + 1;
  char *id = (char *)malloc(length);
  if (id == NULL) return false;
  (void)snprintf(id, length, "%s:binary32", name);
  memset(out, 0, sizeof *out);
  out->provider = provider.provider;
  out->numeric.id = id;
  out->numeric.arithmetic.kind = QA_ARITHMETIC_BINARY32;
  out->numeric.float_to_int = QA_FLOAT_TO_INT_CHECKED_C_TRUNCATION;
  if (family == QA_GAME_FAMILY_Q1) {
    out->clock.kind = QA_CLOCK_Q1_NETQUAKE;
    out->clock.minimum_frame_seconds = 0.001;
    out->clock.maximum_frame_seconds = 0.1;
    out->clock.has_fixed_frame_seconds = false;
  } else if (family == QA_GAME_FAMILY_Q2 && rerelease) {
    out->clock.kind = QA_CLOCK_Q2_RERELEASE;
    out->clock.frame_milliseconds = 25;
  } else if (family == QA_GAME_FAMILY_Q2) {
    out->clock.kind = QA_CLOCK_Q2_CLASSIC;
    out->clock.frame_milliseconds = 100;
  } else {
    out->clock.kind = QA_CLOCK_Q3;
    out->clock.server_frame_milliseconds = 50;
    out->clock.has_fixed_movement_milliseconds = false;
    out->clock.maximum_command_milliseconds = 200;
  }
  return true;
}

void qa_native_provider_timing_free(qa_provider_timing *timing) {
  free((void *)timing->numeric.id);
  timing->numeric.id = NULL;
}
