/* See include/qa/compat/qvm/legacy_presentation.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/legacy_presentation.h"

#include <string.h>

#include "qa/foundation/support.h"

/* Modern index -> legacy value, verbatim from the TS table. */
static const int32_t events[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 43, 44, 45, 46, 48, 49, 50, 51, 53, 54, 55, 56, 57,
    58, 59, 60, 61, 62, 63, 64, 66, 68,
};
#define EVENT_COUNT (sizeof events / sizeof events[0])

static bool modern(qa_qvm_abi_profile profile) {
  return profile != NULL && strcmp(profile, "q3-modern") == 0;
}

/* Remote servers choose these values, so the client paths pass lenient and unrepresentable ones become none/general/absent. */
int32_t qa_qvm_legacy_event(int32_t value, qa_qvm_abi_profile profile, bool reverse) {
  return qa_qvm_legacy_event_ex(value, profile, reverse, false);
}
int32_t qa_qvm_legacy_event_ex(int32_t value, qa_qvm_abi_profile profile, bool reverse, bool lenient) {
  if (modern(profile)) return value;
  int32_t event = value & 255;
  int32_t flags = value & ~255;
  int32_t mapped = -1;
  if (reverse) {
    for (size_t i = 0; i < EVENT_COUNT; i++) {
      if (events[i] == event) {
        mapped = (int32_t)i;
        break;
      }
    }
  } else if (event >= 0 && (size_t)event < EVENT_COUNT) {
    mapped = events[event];
  }
  if (mapped < 0) {
    if (lenient) return 0;
    qa_range_error("QVM event %d is not represented by the selected legacy ABI", (int)event);
  }
  return mapped | flags;
}

int32_t qa_qvm_legacy_entity_type(int32_t value, qa_qvm_abi_profile profile, bool reverse) {
  return qa_qvm_legacy_entity_type_ex(value, profile, reverse, false);
}
int32_t qa_qvm_legacy_entity_type_ex(int32_t value, qa_qvm_abi_profile profile, bool reverse, bool lenient) {
  if (modern(profile)) return value;
  int32_t source_events = reverse ? 13 : 12;
  int32_t target_events = reverse ? 12 : 13;
  if (value >= source_events) {
    const int32_t event = qa_qvm_legacy_event_ex(value - source_events, profile, reverse, lenient);
    return event == 0 && lenient ? 0 : target_events + event;
  }
  if (reverse && value == 12) {
    if (lenient) return 0;
    qa_range_error("Legacy QVM has no team entity type");
  }
  return value;
}

void qa_qvm_legacy_persistent(const double *values, size_t count, qa_qvm_abi_profile profile, double out[16]) {
  if (modern(profile)) {
    for (size_t i = 0; i < 16; i++) out[i] = i < count ? values[i] : 0.0;
    return;
  }
  for (size_t i = 0; i < 16; i++) out[i] = 0.0;
  const int source[] = {0, 1, 2, 3, 4, -1, 7, -1, 8, 9, 10, -1, -1, 11, -1, -1};
  for (size_t i = 0; i < 16; i++)
    if (source[i] >= 0 && (size_t)source[i] < count) out[i] = values[source[i]];
}

int32_t qa_qvm_legacy_configstring(int32_t index, qa_qvm_abi_profile profile) {
  if (modern(profile)) return index;
  if (index >= 12 && index <= 15) return index + 8;
  if (index >= 16 && index <= 26)
    qa_range_error("Legacy private configstring %d has no declared modern presentation mapping", (int)index);
  return index;
}

int32_t qa_qvm_legacy_powerup_bits(int32_t bits, qa_qvm_abi_profile profile) {
  return qa_qvm_legacy_powerup_bits_ex(bits, profile, false);
}
int32_t qa_qvm_legacy_powerup_bits_ex(int32_t bits, qa_qvm_abi_profile profile, bool lenient) {
  if (!modern(profile) && lenient) return bits & 0x1ff;
  if (!modern(profile) && (bits & ~0x1ff) != 0)
    qa_range_error("Legacy ball or private powerup has no modern presentation mapping");
  return bits;
}

qa_double_view qa_qvm_legacy_powerups(const double *values, size_t count, qa_qvm_abi_profile profile) {
  if (!modern(profile)) {
    for (size_t i = 9; i < count; i++) {
      if (values[i] != 0.0)
        qa_range_error("Legacy ball or private powerup has no modern presentation mapping");
    }
  }
  qa_double_view result = {values, count};
  return result;
}

/* TS qvmPowerups(values, profile, true): slots 9.. read as zero on legacy profiles. */
void qa_qvm_legacy_powerups_lenient(const double *values, size_t count, qa_qvm_abi_profile profile, double *out) {
  for (size_t i = 0; i < count; i++) out[i] = !modern(profile) && i >= 9 ? 0.0 : values[i];
}
