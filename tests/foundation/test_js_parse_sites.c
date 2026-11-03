/*
 * Regression checks for ported number parsing and formatting sites that used
 * strtod/strtol/atof or printf("%.Nf") where the TypeScript calls Number(),
 * Number.parseInt, Number.parseFloat or toFixed. Every expected value was
 * printed by running the TypeScript function itself under bun (the oracle in
 * ../quake-anthology-ts/src, imported from a scratch bun script). The comment on
 * each check names the old C result that differed.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/network/recorded_source.h"
#include "qa/app/bootstrap/rerelease_presentation.h"
#include "qa/audio/output.h"
#include "qa/bots/behavior/rerelease/data/blockparse.h"
#include "qa/bots/behavior/rerelease/data/botdata.h"
#include "qa/content/q1/foundation/entity.h"

#include "qa/input/support.h"
#include "qa/network/services/admin.h"

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

static int same(double a, double b) {
  if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
  return a == b && signbit(a) == signbit(b);
}

/* audioKhzRate(value): Number() reads "0b1011" as 11; strtod stopped at 'b'. */
static void khz_checks(void) {
  int64_t rate = 0;
  CHECK(qa_audio_khz_rate("0b1011", &rate) && rate == 11025); /* old C: false */
  CHECK(qa_audio_khz_rate(" 22 ", &rate) && rate == 22050);
  CHECK(qa_audio_khz_rate("0x16", &rate) && rate == 22050);
  CHECK(qa_audio_khz_rate("44.0", &rate) && rate == 44100);
  CHECK(!qa_audio_khz_rate("inf", &rate));
  CHECK(!qa_audio_khz_rate("-0x16", &rate));
  CHECK(!qa_audio_khz_rate("", &rate));
}

/* parseVector: text.trim().split(/\s+/u).map(Number.parseFloat), each token alone. */
typedef qa_vec3 (*vector_fn)(const char *);
static void vector_checks(vector_fn parse) {
  qa_vec3 v = parse("1,2 3 4"); /* old C: {1, 0, 0} */
  CHECK(same(v.x, 1) && same(v.y, 3) && same(v.z, 4));
  v = parse("x 2 3"); /* old C: {0, 0, 0} */
  CHECK(same(v.x, 0) && same(v.y, 2) && same(v.z, 3));
  v = parse("1 0x10 1e2"); /* old C: {1, 16, 100} */
  CHECK(same(v.x, 1) && same(v.y, 0) && same(v.z, 100));
  v = parse("  -0 .5 7junk");
  CHECK(same(v.x, -0.0) && same(v.y, 0.5) && same(v.z, 7));
  v = parse("inf Infinity 3");
  CHECK(same(v.x, 0) && same(v.y, 0) && same(v.z, 3));
}

/* fieldNumber([text]): Number(), undefined unless finite. */
static void field_checks(void) {
  double out = 0;
  const char *v1[] = {"0x1p3"}; /* old C: 8 */
  CHECK(!qa_field_number(v1, 1, &out));
  const char *v2[] = {"-0x10"}; /* old C: -16 */
  CHECK(!qa_field_number(v2, 1, &out));
  const char *v3[] = {"0b11"}; /* old C: rejected */
  CHECK(qa_field_number(v3, 1, &out) && same(out, 3));
  const char *v4[] = {" 5 "}; /* old C: rejected (trailing space) */
  CHECK(qa_field_number(v4, 1, &out) && same(out, 5));
  const char *v5[] = {"inf"};
  CHECK(!qa_field_number(v5, 1, &out));
  const char *v6[] = {"12e1"};
  CHECK(qa_field_number(v6, 1, &out) && same(out, 120));
}

/* sourceIpv4Filter: Number.parseInt(digits, 10) & 255 per octet. */
static void ipv4_checks(void) {
  char text[420];
  memset(text, '0', 399);
  strcpy(text + 399, "5.1.2.3"); /* old C: the 400-digit run read as Infinity -> 0 */
  qa_ipv4_filter filter;
  CHECK(qa_source_ipv4_filter(text, &filter) == 1 && filter.mask == 4294967295u && filter.compare == 50462981u);
  CHECK(qa_source_ipv4_filter("300.256.1.0", &filter) == 1 && filter.mask == 16711935u && filter.compare == 65580u);
  CHECK(qa_source_ipv4_filter("99999999999999999999.1.2.3", &filter) == 1 && filter.mask == 4294967040u &&
        filter.compare == 50462976u);
}

/* demoTimingText: `${frames} frames, ${seconds.toFixed(3)} seconds: ${fps.toFixed(1)} fps\n`. */
static void timing_checks(void) {
  char *text = qa_app_demo_timing_text((qa_app_demo_timing){3, 62.5}); /* old C: "0.062" */
  CHECK(text != NULL && strcmp(text, "3 frames, 0.063 seconds: 48.0 fps\n") == 0);
  free(text);
  text = qa_app_demo_timing_text((qa_app_demo_timing){1, 2500});
  CHECK(text != NULL && strcmp(text, "1 frames, 2.500 seconds: 0.4 fps\n") == 0);
  free(text);
}

/* q2PlayerNameTokens: /##P([0-9]+)/ then names.get(Number(digits)). */
static void token_checks(void) {
  const double slots[] = {1, 12, 1000};
  const char *const names[] = {"one", "twelve", "thousand"};
  char *text = qa_app_q2_player_name_tokens("a##P1.5 b##P1e3 c##P12", slots, names, 3);
  /* old C: strtod read "1.5" and "1e3": "a b thousand ctwelve" */
  CHECK(text != NULL && strcmp(text, "aone.5 bonee3 ctwelve") == 0);
  free(text);
}

/* Number(text) through the input helper (shared by bindings, seat, settings). */
static void input_number_checks(void) {
  CHECK(isnan(qa_input_js_parse_number("-0x1F"))); /* old C: -31 */
  CHECK(isnan(qa_input_js_parse_number("inf")));   /* old C: Infinity */
  CHECK(same(qa_input_js_parse_number("0b101"), 5));
  CHECK(same(qa_input_js_parse_number(""), 0));
  CHECK(same(qa_input_js_parse_number("\t\n 42 "), 42));
  CHECK(same(qa_input_js_parse_int10("  12abc"), 12));
  CHECK(same(qa_input_js_parse_int10("9007199254740993"), 9007199254740992.0));
}

/* botdata parseItems / parseInteractables: Number() then Number.isInteger / isFinite. */
static void botdata_checks(void) {
  qa_items_result items;
  CHECK(qa_parse_items(&items, "{\n\"name\" \"a\"\n\"spawnflags\" inf = x\n}\n{\n\"name\" \"b\"\n\"spawnflags\" 0b11 = y\n}\n"));
  /* bun: errors ["line 3: \"spawnflags\" bit \"inf\" is not an integer"], spawnflags [[], [{bit 3, y}]];
   * old C: "inf" was accepted as the integer Infinity and "0b11" rejected. */
  CHECK(items.error_count == 1 && strcmp(items.errors[0], "line 3: \"spawnflags\" bit \"inf\" is not an integer") == 0);
  CHECK(items.entry_count == 2 && items.entries[0].spawnflag_count == 0 && items.entries[1].spawnflag_count == 1 &&
        same(items.entries[1].spawnflags[0].bit, 3) && strcmp(items.entries[1].spawnflags[0].name, "y") == 0);
  qa_items_result_destroy(&items);
  qa_interactables_result inter;
  CHECK(qa_parse_interactables(&inter, "{\n\"name\" \"door\"\n\"bounds\" { 0x1p3 1 2 } { 3 4 5 }\n}\n"
                                       "{\n\"name\" \"lift\"\n\"bounds\" { 0b11 1 2 } { 3 4 5 }\n}\n"));
  /* bun: errors ["line 3: \"bounds\" expects { x y z } { x y z }"], bounds [null, {mins [3,1,2] maxs [3,4,5]}];
   * old C: 0x1p3 read as 8, 0b11 rejected. */
  CHECK(inter.error_count == 1 && strcmp(inter.errors[0], "line 3: \"bounds\" expects { x y z } { x y z }") == 0);
  CHECK(inter.entry_count == 2 && !inter.entries[0].has_bounds && inter.entries[1].has_bounds &&
        same(inter.entries[1].bounds.mins[0], 3) && same(inter.entries[1].bounds.mins[1], 1) &&
        same(inter.entries[1].bounds.maxs[2], 5));
  qa_interactables_result_destroy(&inter);
}

int main(void) {
  botdata_checks();
  khz_checks();
  vector_checks(qa_q1_parse_vector);

  field_checks();
  ipv4_checks();
  timing_checks();
  token_checks();
  input_number_checks();
  printf("test_js_parse_sites: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
