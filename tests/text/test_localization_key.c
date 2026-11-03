/* Cases for src/text/localization-key.ts (tests/text/localization-key.test.ts).
 * GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/text/localization_key.h"
static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static void round_trip(void) {
  const char *args[] = {"Ranger", "Shub\x1fNiggurath"};
  char *text = qa_encode_localization_key("$qc_death_ax", args, 2);
  qa_localization_key_text decoded;
  CHECK(qa_decode_localization_key(text, &decoded));
  CHECK(strcmp(decoded.key, "$qc_death_ax") == 0);
  CHECK(decoded.arg_count == 2);
  CHECK(decoded.arg_count == 2 && strcmp(decoded.args[0], "Ranger") == 0);
  CHECK(decoded.arg_count == 2 && strcmp(decoded.args[1], "Shub Niggurath") == 0);
  qa_localization_key_text_release(&decoded);
  free(text);
  text = qa_encode_localization_key("$qc_entered", NULL, 0);
  CHECK(qa_decode_localization_key(text, &decoded));
  CHECK(strcmp(decoded.key, "$qc_entered") == 0 && decoded.arg_count == 0);
  qa_localization_key_text_release(&decoded);
  free(text);
}
static void ordinary_and_unknown(void) {
  qa_localization_key_text decoded;
  CHECK(!qa_decode_localization_key("plain text\n", &decoded));
  const char *args[] = {"a"};
  char *future = qa_encode_localization_key("$k", args, 1);
  /* .replace("QLK1", "QLK9") */
  char *at = strstr(future, "QLK1");
  CHECK(at != NULL);
  if (at) at[3] = '9';
  CHECK(!qa_decode_localization_key(future, &decoded));
  char *plain = qa_plain_localization_text(future);
  CHECK(strcmp(plain, "$k a") == 0);
  free(plain);
  free(future);
  plain = qa_plain_localization_text("plain");
  CHECK(strcmp(plain, "plain") == 0);
  free(plain);
}
int main(void) {
  round_trip();
  ordinary_and_unknown();
  printf("test_localization_key: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
