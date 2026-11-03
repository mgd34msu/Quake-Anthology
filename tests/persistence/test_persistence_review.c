/*
 * Confirmation-gate review checks for src/persistence. Each expected value
 * comes from running the TypeScript under bun. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

static bool encodes_as(const qa_save_value *value, const char *expected) {
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  bool same = bytes != NULL && length == strlen(expected) && memcmp(bytes, expected, length) == 0;
  if (!same && bytes != NULL) fprintf(stderr, "  got %.*s\n", (int)length, (const char *)bytes);
  free(bytes);
  return same;
}

/* TS encodeCheckpointValue writes Object.keys order: array-index keys
 * ascending, then insertion order; decoding yields the same order (bun). */
static void test_checkpoint_key_order(void) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value_set(object, "b", qa_save_value_number(1));
  qa_save_value_set(object, "2", qa_save_value_number(2));
  qa_save_value_set(object, "a", qa_save_value_number(3));
  qa_save_value_set(object, "1", qa_save_value_number(4));
  qa_save_value_set(object, "01", qa_save_value_number(5));
  CHECK(encodes_as(object, "{\"1\":4,\"2\":2,\"b\":1,\"a\":3,\"01\":5}"));
  qa_save_value_free(object);
  const char *text = "{\"z\":{\"x\":1,\"7\":2},\"3\":null}";
  qa_save_value *decoded = qa_decode_checkpoint_value((const uint8_t *)text, strlen(text));
  CHECK(decoded != NULL && decoded->entry_count == 2 && strcmp(decoded->entries[0].key, "3") == 0);
  CHECK(encodes_as(decoded, "{\"3\":null,\"z\":{\"7\":2,\"x\":1}}"));
  qa_save_value_free(decoded);
}

typedef struct decode_job {
  const char *text;
  size_t length;
} decode_job;

static void decode_run(void *raw) {
  decode_job *job = raw;
  qa_save_value_free(qa_decode_checkpoint_value((const uint8_t *)job->text, job->length));
}

/* TS wraps decoder and JSON.parse failures as SaveFormatError; messages are bun's
   (JSON.parse texts from bun 2026-11-03, JavaScriptCore LiteralParser). */
static void test_checkpoint_decode_errors(void) {
  const struct {
    const char *text;
    size_t length;
    const char *message;
  } cases[] = {{"\xff", 1, "checkpoint: Invalid byte sequence"},
               {"", 0, "checkpoint: JSON Parse error: Unexpected EOF"},
               {"{} x", 4, "checkpoint: JSON Parse error: Unable to parse JSON string"},
               {"not json", sizeof "not json" - 1, "checkpoint: JSON Parse error: Unexpected identifier \"not\""},
               {"{", sizeof "{" - 1, "checkpoint: JSON Parse error: Expected '}'"},
               {"[", sizeof "[" - 1, "checkpoint: JSON Parse error: Unexpected EOF"},
               {"[1", sizeof "[1" - 1, "checkpoint: JSON Parse error: Expected ']'"},
               {"{\"a\"", sizeof "{\"a\"" - 1, "checkpoint: JSON Parse error: Expected ':' before value in object property definition"},
               {"{\"a\":1,", sizeof "{\"a\":1," - 1, "checkpoint: JSON Parse error: Property name must be a string literal"},
               {"\"abc", sizeof "\"abc" - 1, "checkpoint: JSON Parse error: Unterminated string"},
               {"\"a\\qb\"", sizeof "\"a\\qb\"" - 1, "checkpoint: JSON Parse error: Invalid escape character q"},
               {"\"a\\u12\"", sizeof "\"a\\u12\"" - 1, "checkpoint: JSON Parse error: \\u must be followed by 4 hex digits"},
               {"[\"a\\u12\"]", sizeof "[\"a\\u12\"]" - 1, "checkpoint: JSON Parse error: \"\\u12\"]\" is not a valid unicode escape"},
               {"\"a\x01" "b\"", sizeof "\"a\x01" "b\"" - 1, "checkpoint: JSON Parse error: Unterminated string"},
               {"01", sizeof "01" - 1, "checkpoint: JSON Parse error: Unable to parse JSON string"},
               {"-", sizeof "-" - 1, "checkpoint: JSON Parse error: Invalid number"},
               {"1.", sizeof "1." - 1, "checkpoint: JSON Parse error: Invalid digits after decimal point"},
               {"1e", sizeof "1e" - 1, "checkpoint: JSON Parse error: Unable to parse JSON string"},
               {"[1e]", sizeof "[1e]" - 1, "checkpoint: JSON Parse error: Expected ']'"},
               {"+1", sizeof "+1" - 1, "checkpoint: JSON Parse error: Unrecognized token '+'"},
               {".5", sizeof ".5" - 1, "checkpoint: JSON Parse error: Unexpected token '.'"},
               {"tru", sizeof "tru" - 1, "checkpoint: JSON Parse error: Unexpected identifier \"tru\""},
               {"truex", sizeof "truex" - 1, "checkpoint: JSON Parse error: Unable to parse JSON string"},
               {"}", sizeof "}" - 1, "checkpoint: JSON Parse error: Unexpected token '}'"},
               {"[1,]", sizeof "[1,]" - 1, "checkpoint: JSON Parse error: Unexpected comma at the end of array expression"},
               {"{,}", sizeof "{,}" - 1, "checkpoint: JSON Parse error: Expected '}'"},
               {"'a'", sizeof "'a'" - 1, "checkpoint: JSON Parse error: Single quotes (') are not allowed in JSON"},
               {"[1@]", sizeof "[1@]" - 1, "checkpoint: JSON Parse error: Unrecognized token '@'"},
               {"\xc3\xa9", sizeof "\xc3\xa9" - 1, "checkpoint: JSON Parse error: Unrecognized token '\xc3\xa9'"},
               {"[\"\\u12\xc3\xa9" "4\"]", sizeof "[\"\\u12\xc3\xa9" "4\"]" - 1, "checkpoint: JSON Parse error: \"\\u12\xc3\xa9" "4\" is not a valid unicode escape"},
               {"$a1_b", sizeof "$a1_b" - 1, "checkpoint: JSON Parse error: Unexpected identifier \"$a1_b\""},
               {"a-b", sizeof "a-b" - 1, "checkpoint: JSON Parse error: Unexpected identifier \"a\""},
               {"{\"a\":1)", sizeof "{\"a\":1)" - 1, "checkpoint: JSON Parse error: Expected '}'"},
               {"[1;]", sizeof "[1;]" - 1, "checkpoint: JSON Parse error: Expected ']'"},
               {"1;", sizeof "1;" - 1, "checkpoint: JSON Parse error: Unable to parse JSON string"},
               {"[\t1\x0b]", sizeof "[\t1\x0b]" - 1, "checkpoint: JSON Parse error: Unrecognized token '\x0b'"},
               {"{\"a\" : 1, \"b\" :}", sizeof "{\"a\" : 1, \"b\" :}" - 1, "checkpoint: JSON Parse error: Unexpected token '}'"},
               {"[1,,2]", sizeof "[1,,2]" - 1, "checkpoint: JSON Parse error: Unexpected token ','"},
               {"(", sizeof "(" - 1, "checkpoint: JSON Parse error: Unexpected token '('"}};
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    decode_job job = {cases[i].text, cases[i].length};
    qa_caught_error caught;
    CHECK(!qa_try(decode_run, &job, &caught));
    CHECK(strcmp(caught.name, "SaveFormatError") == 0);
    CHECK(strcmp(caught.message, cases[i].message) == 0);
    if (strcmp(caught.message, cases[i].message) != 0) fprintf(stderr, "  got \"%s\"\n", caught.message);
  }
}

int main(void) {
  test_checkpoint_key_order();
  test_checkpoint_decode_errors();
  printf("%d checks, %d failures\n", checks, failures);
  fflush(stdout);
  return failures == 0 ? 0 : 1;
}
