/* Port of src/persistence/shared.ts. GPL-2.0-or-later. */
#include "qa/persistence/shared.h"

#include <stdlib.h>
#include <string.h>

/* Field reader or abort (OOM has no TS counterpart). */
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *field_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *field_choice(const qa_save_reader *reader, const char *name,
                                const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_choice_number(const qa_save_reader *reader, const char *name,
                                  const double *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_choice_number(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *field_literal(const qa_save_reader *reader, const char *name,
                                 const char *expected) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_literal_string(&f, expected);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_literal_number(const qa_save_reader *reader, const char *name,
                                   double expected) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_literal_number(&f, expected);
  qa_save_reader_destroy(&f);
  return value;
}
static bool field_boolean(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  bool value = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_number(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_finite(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_integer_min(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer_min(&f, minimum);
  qa_save_reader_destroy(&f);
  return value;
}
/* TS nullable<finite>: false when the field value is null. */
static bool field_nullable_finite(const qa_save_reader *reader, const char *name, double *out) {
  qa_save_reader f = field(reader, name);
  bool present = !qa_save_reader_is_null(&f);
  if (present) *out = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return present;
}
static qa_floating_rounding rounding_of(const char *text) {
  if (strcmp(text, "toward-zero") == 0) return QA_FLOATING_ROUNDING_TOWARD_ZERO;
  if (strcmp(text, "toward-positive") == 0) return QA_FLOATING_ROUNDING_TOWARD_POSITIVE;
  if (strcmp(text, "toward-negative") == 0) return QA_FLOATING_ROUNDING_TOWARD_NEGATIVE;
  return QA_FLOATING_ROUNDING_NEAREST_EVEN;
}
static const char *const rounding_choices[] = {
    "nearest-even", "toward-zero", "toward-positive", "toward-negative"};

qa_content_digest qa_read_digest(const qa_save_reader *reader) {
  const qa_save_value *value = reader->value;
  if (value == NULL || value->kind != QA_SAVE_STRING || !qa_is_content_digest(value->text))
    qa_save_reader_fail(reader, "expected a SHA-256 content digest");
  return value->text;
}
qa_source_time qa_read_time(const qa_save_reader *reader) {
  static const char *const kinds[] = {"seconds", "milliseconds"};
  const char *kind = field_choice(reader, "kind", kinds, 2);
  qa_source_time time;
  time.kind = strcmp(kind, "seconds") == 0 ? QA_SOURCE_TIME_SECONDS
                                           : QA_SOURCE_TIME_MILLISECONDS;
  time.value = field_finite(reader, "value");
  return time;
}
qa_vec3 qa_read_vector(const qa_save_reader *reader) {
  qa_vec3 vector;
  vector.x = field_number(reader, "x");
  vector.y = field_number(reader, "y");
  vector.z = field_number(reader, "z");
  return vector;
}
qa_bounds qa_read_bounds(const qa_save_reader *reader) {
  qa_bounds bounds;
  qa_save_reader min = field(reader, "min");
  bounds.min = qa_read_vector(&min);
  qa_save_reader_destroy(&min);
  qa_save_reader max = field(reader, "max");
  bounds.max = qa_read_vector(&max);
  qa_save_reader_destroy(&max);
  return bounds;
}
qa_frame_context qa_read_frame(const qa_save_reader *reader) {
  static const char *const phases[] = {"frame-entry", "client-command", "entity-prethink",
      "entity-physics", "entity-think", "client-end-frame", "frame-exit"};
  qa_frame_context frame;
  frame.frame = field_integer_min(reader, "frame", 0);
  qa_save_reader time = field(reader, "time");
  frame.time = qa_read_time(&time);
  qa_save_reader_destroy(&time);
  qa_save_reader elapsed = field(reader, "elapsed");
  frame.elapsed = qa_read_time(&elapsed);
  qa_save_reader_destroy(&elapsed);
  const char *phase = field_choice(reader, "phase", phases, 7);
  frame.phase = QA_FRAME_PHASE_FRAME_ENTRY;
  for (int i = 0; i < 7; i++)
    if (phase == phases[i]) frame.phase = (qa_frame_phase)i;
  return frame;
}
qa_clock_profile qa_read_clock(const qa_save_reader *reader) {
  static const char *const kinds[] = {
      "q1-netquake", "q1-quakeworld", "q2-classic", "q2-rerelease", "q3"};
  const char *kind = field_choice(reader, "kind", kinds, 5);
  qa_clock_profile clock;
  memset(&clock, 0, sizeof clock);
  if (strcmp(kind, "q1-netquake") == 0) {
    clock.kind = QA_CLOCK_Q1_NETQUAKE;
    clock.minimum_frame_seconds = field_finite(reader, "minimumFrameSeconds");
    clock.maximum_frame_seconds = field_finite(reader, "maximumFrameSeconds");
    clock.has_fixed_frame_seconds =
        field_nullable_finite(reader, "fixedFrameSeconds", &clock.fixed_frame_seconds);
  } else if (strcmp(kind, "q1-quakeworld") == 0) {
    clock.kind = QA_CLOCK_Q1_QUAKEWORLD;
    clock.maximum_command_milliseconds = field_finite(reader, "maximumCommandMilliseconds");
  } else if (strcmp(kind, "q2-classic") == 0) {
    clock.kind = QA_CLOCK_Q2_CLASSIC;
    clock.frame_milliseconds = field_literal_number(reader, "frameMilliseconds", 100);
  } else if (strcmp(kind, "q2-rerelease") == 0) {
    clock.kind = QA_CLOCK_Q2_RERELEASE;
    clock.frame_milliseconds = field_finite(reader, "frameMilliseconds");
    (void)field_literal(reader, "preparation", "before-frame");
  } else {
    clock.kind = QA_CLOCK_Q3;
    clock.server_frame_milliseconds = field_finite(reader, "serverFrameMilliseconds");
    clock.has_fixed_movement_milliseconds = field_nullable_finite(
        reader, "fixedMovementMilliseconds", &clock.fixed_movement_milliseconds);
    clock.maximum_command_milliseconds =
        field_literal_number(reader, "maximumCommandMilliseconds", 200);
  }
  return clock;
}
qa_arithmetic_profile qa_read_arithmetic(const qa_save_reader *reader) {
  static const char *const kinds[] = {"binary32", "donor-binary64", "x87", "sse"};
  const char *kind = field_choice(reader, "kind", kinds, 4);
  qa_arithmetic_profile profile;
  memset(&profile, 0, sizeof profile);
  if (strcmp(kind, "binary32") == 0) {
    profile.kind = QA_ARITHMETIC_BINARY32;
    (void)field_literal(reader, "round", "each-operation");
  } else if (strcmp(kind, "donor-binary64") == 0) {
    static const char *const sources[] = {"q1-ts", "q2-ts"};
    profile.kind = QA_ARITHMETIC_DONOR_BINARY64;
    profile.source = field_choice(reader, "source", sources, 2);
  } else if (strcmp(kind, "x87") == 0) {
    static const double bits[] = {24, 53, 64};
    profile.kind = QA_ARITHMETIC_X87;
    profile.precision_bits = (int64_t)field_choice_number(reader, "precisionBits", bits, 3);
    profile.rounding = rounding_of(field_choice(reader, "rounding", rounding_choices, 4));
  } else {
    profile.kind = QA_ARITHMETIC_SSE;
    profile.flush_to_zero = field_boolean(reader, "flushToZero");
    profile.denormals_are_zero = field_boolean(reader, "denormalsAreZero");
    profile.rounding = rounding_of(field_choice(reader, "rounding", rounding_choices, 4));
  }
  return profile;
}
qa_numeric_profile qa_read_numeric(const qa_save_reader *reader) {
  static const char *const conversions[] = {
      "qvm-indefinite", "x86-indefinite", "checked-c-truncation"};
  qa_numeric_profile profile;
  qa_save_reader id = field(reader, "id");
  profile.id = qa_namespaced(&id);
  qa_save_reader_destroy(&id);
  qa_save_reader arithmetic = field(reader, "arithmetic");
  profile.arithmetic = qa_read_arithmetic(&arithmetic);
  qa_save_reader_destroy(&arithmetic);
  (void)field_literal(reader, "scalarStorage", "binary32");
  const char *conversion = field_choice(reader, "floatToInt", conversions, 3);
  profile.float_to_int = strcmp(conversion, "qvm-indefinite") == 0
      ? QA_FLOAT_TO_INT_QVM_INDEFINITE
      : strcmp(conversion, "x86-indefinite") == 0 ? QA_FLOAT_TO_INT_X86_INDEFINITE
                                                  : QA_FLOAT_TO_INT_CHECKED_C_TRUNCATION;
  (void)field_literal(reader, "integerOverflow", "wrap32");
  return profile;
}
static double *field_double_list(const qa_save_reader *reader, const char *name,
                                 bool minimum_zero, size_t *out_count) {
  qa_save_reader list = field(reader, name);
  size_t count = qa_save_reader_list_count(&list);
  double *values = count > 0 ? (double *)malloc(count * sizeof *values) : NULL;
  if (count > 0 && values == NULL) qa_save_reader_fail(&list, "out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&list, i, &item)) qa_save_reader_fail(&list, "out of memory");
    values[i] = minimum_zero ? qa_save_reader_integer_min(&item, 0)
                             : qa_save_reader_integer(&item);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return values;
}
qa_random_state qa_read_random(const qa_save_reader *reader) {
  static const char *const kinds[] = {
      "q3-lcg", "msvcrt-rand", "glibc-random", "q2-rerelease-mt19937", "guest"};
  double draws = field_integer_min(reader, "draws", 0);
  const char *kind = field_choice(reader, "kind", kinds, 5);
  qa_random_state state;
  memset(&state, 0, sizeof state);
  state.draws = draws;
  if (strcmp(kind, "q3-lcg") == 0 || strcmp(kind, "msvcrt-rand") == 0) {
    state.kind = strcmp(kind, "q3-lcg") == 0 ? QA_RANDOM_STATE_Q3_LCG
                                             : QA_RANDOM_STATE_MSVCRT_RAND;
    state.seed = field_integer(reader, "seed");
  } else if (strcmp(kind, "glibc-random") == 0) {
    state.kind = QA_RANDOM_STATE_GLIBC_RANDOM;
    size_t count = 0;
    state.words.values = field_double_list(reader, "words", false, &count);
    state.words.count = count;
    state.front = field_integer_min(reader, "front", 0);
    state.rear = field_integer_min(reader, "rear", 0);
  } else if (strcmp(kind, "q2-rerelease-mt19937") == 0) {
    state.kind = QA_RANDOM_STATE_Q2_RERELEASE_MT19937;
    qa_save_reader words = field(reader, "words");
    size_t count = qa_save_reader_list_count(&words);
    double *values = count > 0 ? (double *)malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&words, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&words, i, &item)) qa_save_reader_fail(&words, "out of memory");
      double word = qa_save_reader_integer_min(&item, 0);
      if (!(word <= 4294967295.0))
        qa_save_reader_fail(&item, "expected a uint32 MT19937 word");
      values[i] = word;
      qa_save_reader_destroy(&item);
    }
    if (count != 624) qa_save_reader_fail(&words, "expected 624 MT19937 words");
    qa_save_reader_destroy(&words);
    qa_save_reader index = field(reader, "index");
    double index_value = qa_save_reader_integer_min(&index, 0);
    if (index_value > 624)
      qa_save_reader_fail(&index, "expected MT19937 index 0 through 624");
    qa_save_reader_destroy(&index);
    state.words.values = values;
    state.words.count = count;
    state.index = index_value;
    (void)field_literal(reader, "distribution", "msvc-2022-17.6");
  } else {
    state.kind = QA_RANDOM_STATE_GUEST;
    state.module = field_string(reader, "module");
    qa_save_reader bytes = field(reader, "bytes");
    size_t length = 0;
    state.bytes.bytes = qa_save_reader_bytes(&bytes, &length);
    state.bytes.length = length;
    qa_save_reader_destroy(&bytes);
  }
  return state;
}
qa_frame_ordering qa_read_ordering(const qa_save_reader *reader) {
  static const char *const kinds[] = {"native", "mixed"};
  const char *kind = field_choice(reader, "kind", kinds, 2);
  qa_frame_ordering ordering;
  memset(&ordering, 0, sizeof ordering);
  if (strcmp(kind, "native") == 0) {
    ordering.kind = QA_FRAME_ORDERING_NATIVE;
    (void)field_literal(reader, "traversal", "source-slot-order");
    qa_save_reader clock = field(reader, "clock");
    ordering.clock = qa_read_clock(&clock);
    qa_save_reader_destroy(&clock);
  } else {
    ordering.kind = QA_FRAME_ORDERING_MIXED;
    qa_save_reader providers = field(reader, "providers");
    size_t count = qa_save_reader_list_count(&providers);
    const char **values = count > 0 ? (const char **)malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&providers, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&providers, i, &item))
        qa_save_reader_fail(&providers, "out of memory");
      values[i] = qa_namespaced(&item);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&providers);
    ordering.providers = (const qa_provider_id *)values;
    ordering.provider_count = count;
    (void)field_literal(reader, "entityOrder", "source-slot-order");
    (void)field_literal(reader, "ties", "provider-entity-invocation");
  }
  return ordering;
}
