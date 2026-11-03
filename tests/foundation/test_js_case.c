/*
 * JavaScript case mapping and localeCompare helpers (include/qa/foundation/js_case.h).
 * Every expected value below was printed by the JS expression under bun:
 * `bun tools/js_case_reference.js` emits the generated block verbatim.
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/js_case.h"

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

static void text_check(char *got, const char *want, const char *what, int line) {
  checks++;
  if (got == NULL || strcmp(got, want) != 0) {
    failures++;
    fprintf(stderr, "FAIL %s:%d: %s = \"%s\", want \"%s\"\n", __FILE__, line, what, got ? got : "(null)", want);
  }
  free(got);
}
#define LOWER(in, want) text_check(qa_js_to_lower_case(in), (want), "toLowerCase(" #in ")", __LINE__)
#define UPPER(in, want) text_check(qa_js_to_upper_case(in), (want), "toUpperCase(" #in ")", __LINE__)
#define LATIN1(in, want) text_check(qa_js_latin1_to_lower_case(in), (want), "latin1 toLowerCase(" #in ")", __LINE__)

static int sign(int v) { return v < 0 ? -1 : v > 0; }
static void lc(const char *a, const char *b, int def, int base, int numeric, int line) {
  int got[3] = {sign(qa_js_locale_compare(a, b)), sign(qa_js_locale_compare_base(a, b)),
                sign(qa_js_locale_compare_numeric(a, b))};
  int want[3] = {def, base, numeric};
  for (int i = 0; i < 3; i++) {
    checks++;
    if (got[i] != want[i]) {
      failures++;
      fprintf(stderr, "FAIL %s:%d: localeCompare#%d(\"%s\", \"%s\") = %d, want %d\n", __FILE__, line, i, a, b,
              got[i], want[i]);
    }
  }
}
#define LC(a, b, def, base, numeric) lc((a), (b), (def), (base), (numeric), __LINE__)

int main(void) {
  /* BEGIN bun tools/js_case_reference.js */
  LOWER("", "");
  UPPER("", "");
  LOWER("ABC def", "abc def");
  UPPER("ABC def", "ABC DEF");
  LOWER("\xc3\x80""\xc3\x89""\xc3\x8e""\xc3\x95""\xc3\x9c"" \xc3\xbf"" \xc3\x9f""", "\xc3\xa0""\xc3\xa9""\xc3\xae""\xc3\xb5""\xc3\xbc"" \xc3\xbf"" \xc3\x9f""");
  UPPER("\xc3\x80""\xc3\x89""\xc3\x8e""\xc3\x95""\xc3\x9c"" \xc3\xbf"" \xc3\x9f""", "\xc3\x80""\xc3\x89""\xc3\x8e""\xc3\x95""\xc3\x9c"" \xc5\xb8"" SS");
  LOWER("\xc4\xb0""stanbul", "i\xcc\x87""stanbul");
  UPPER("\xc4\xb0""stanbul", "\xc4\xb0""STANBUL");
  LOWER("\xce\x91""\xce\xa3""", "\xce\xb1""\xcf\x82""");
  UPPER("\xce\x91""\xce\xa3""", "\xce\x91""\xce\xa3""");
  LOWER("\xce\xa3""", "\xcf\x83""");
  UPPER("\xce\xa3""", "\xce\xa3""");
  LOWER("\xce\x91""\xce\xa3"" \xce\x91""", "\xce\xb1""\xcf\x82"" \xce\xb1""");
  UPPER("\xce\x91""\xce\xa3"" \xce\x91""", "\xce\x91""\xce\xa3"" \xce\x91""");
  LOWER("\xce\x91""\xce\xa3"".", "\xce\xb1""\xcf\x82"".");
  UPPER("\xce\x91""\xce\xa3"".", "\xce\x91""\xce\xa3"".");
  LOWER("\xce\x91""\xce\xa3""\xce\x91""", "\xce\xb1""\xcf\x83""\xce\xb1""");
  UPPER("\xce\x91""\xce\xa3""\xce\x91""", "\xce\x91""\xce\xa3""\xce\x91""");
  LOWER("\xce\xa3"".\xce\x91""", "\xcf\x83"".\xce\xb1""");
  UPPER("\xce\xa3"".\xce\x91""", "\xce\xa3"".\xce\x91""");
  LOWER("a'\xce\xa3""", "a'\xcf\x82""");
  UPPER("a'\xce\xa3""", "A'\xce\xa3""");
  LOWER("\xef\xac\x83"" \xc5\x89"" \xc7\xb0"" \xce\x90""", "\xef\xac\x83"" \xc5\x89"" \xc7\xb0"" \xce\x90""");
  UPPER("\xef\xac\x83"" \xc5\x89"" \xc7\xb0"" \xce\x90""", "FFI \xca\xbc""N J\xcc\x8c"" \xce\x99""\xcc\x88""\xcc\x81""");
  LOWER("\xd0\x9f""\xd0\xa0""\xd0\x98""\xd0\x92""\xd0\x95""\xd0\xa2"" \xd0\xbc""\xd0\xb8""\xd1\x80""", "\xd0\xbf""\xd1\x80""\xd0\xb8""\xd0\xb2""\xd0\xb5""\xd1\x82"" \xd0\xbc""\xd0\xb8""\xd1\x80""");
  UPPER("\xd0\x9f""\xd0\xa0""\xd0\x98""\xd0\x92""\xd0\x95""\xd0\xa2"" \xd0\xbc""\xd0\xb8""\xd1\x80""", "\xd0\x9f""\xd0\xa0""\xd0\x98""\xd0\x92""\xd0\x95""\xd0\xa2"" \xd0\x9c""\xd0\x98""\xd0\xa0""");
  LOWER("\xc7\x85""\xc7\x84""\xc7\x86""", "\xc7\x86""\xc7\x86""\xc7\x86""");
  UPPER("\xc7\x85""\xc7\x84""\xc7\x86""", "\xc7\x84""\xc7\x84""\xc7\x84""");
  LOWER("\xe2\x84\xaa""\xe2\x84\xab""", "k\xc3\xa5""");
  UPPER("\xe2\x84\xaa""\xe2\x84\xab""", "\xe2\x84\xaa""\xe2\x84\xab""");
  LOWER("\xe1\xba\x9e""", "\xc3\x9f""");
  UPPER("\xe1\xba\x9e""", "\xe1\xba\x9e""");
  LOWER("\xce\xa9""\xcd\x85""", "\xcf\x89""\xcd\x85""");
  UPPER("\xce\xa9""\xcd\x85""", "\xce\xa9""\xce\x99""");
  LOWER("maps/Q2DM1.BSP", "maps/q2dm1.bsp");
  UPPER("maps/Q2DM1.BSP", "MAPS/Q2DM1.BSP");
  LOWER("\xce\x8c""\xce\xa3""\xce\x9f""\xce\xa3"" \xce\x86""\xce\xa3"".", "\xcf\x8c""\xcf\x83""\xce\xbf""\xcf\x82"" \xce\xac""\xcf\x82"".");
  UPPER("\xce\x8c""\xce\xa3""\xce\x9f""\xce\xa3"" \xce\x86""\xce\xa3"".", "\xce\x8c""\xce\xa3""\xce\x9f""\xce\xa3"" \xce\x86""\xce\xa3"".");
  LOWER("x\xcc\x81""\xce\xa3""", "x\xcc\x81""\xcf\x82""");
  UPPER("x\xcc\x81""\xce\xa3""", "X\xcc\x81""\xce\xa3""");
  CHECK(qa_js_equals_ignore_case("PAK0.PK3", "pak0.pk3") == true);
  CHECK(qa_js_equals_ignore_case("\xc4\xb0""", "i\xcc\x87""") == true);
  CHECK(qa_js_equals_ignore_case("\xe2\x84\xaa""", "k") == true);
  CHECK(qa_js_equals_ignore_case("\xc3\x89""", "\xc3\xa9""") == true);
  CHECK(qa_js_equals_ignore_case("\xc3\x9f""", "SS") == false);
  CHECK(qa_js_equals_ignore_case("abc", "abd") == false);
  CHECK(qa_js_ends_with_ignore_case("maps/x.\xe2\x84\xaa""", ".k") == true);
  CHECK(qa_js_ends_with_ignore_case("FOO.PK3", ".pk3") == true);
  CHECK(qa_js_ends_with_ignore_case("a", "ab") == false);
  CHECK(qa_js_starts_with_ignore_case("HTTP://x", "http://") == true);
  CHECK(qa_js_starts_with_ignore_case("\xc4\xb0""x", "i") == true);
  CHECK(qa_js_starts_with_ignore_case("ab", "abc") == false);
  LATIN1("\x41\x42\x43\xc0\xd7\xde\xdf\xe0\xff\x80\x9f", "\x61\x62\x63\xe0\xd7\xfe\xdf\xe0\xff\x80\x9f");
  LATIN1("\x51\x32\x44\x4d\x31", "\x71\x32\x64\x6d\x31");
  CHECK(qa_js_latin1_equals_ignore_case("\xc0X", "\xe0x") == true);
  CHECK(qa_js_latin1_equals_ignore_case("\xd7", "\xf7") == false);
  LC("", "b", -1, -1, -1);
  LC("a", "a b", -1, -1, -1);
  LC("A", "01.2", 1, 1, 1);
  LC("b", "q2dm1", -1, -1, -1);
  LC("B", "$", 1, 1, 1);
  LC("ab", "zz\x01""A", -1, -1, -1);
  LC("aB", "\x01"".!~", 1, 1, 1);
  LC("Ab", "", 1, 1, 1);
  LC("a-b", "~A~,", 1, 1, 1);
  LC("a_b", "!,Bb", 1, 1, 1);
  LC("a b", "Ab", -1, -1, -1);
  LC("a1", "0!99", 1, 1, 1);
  LC("a10", "_Az-", 1, 1, 1);
  LC("a9", "", 1, 1, 1);
  LC("1.10", "0~", 1, 1, 1);
  LC("1.9", "0Az#", 1, 1, 1);
  LC("1.2", "", 1, 1, 1);
  LC("01.2", "", 1, 1, 1);
  LC("1.02", "Ab", -1, -1, -1);
  LC("1.2a", "", 1, 1, 1);
  LC("v2.0.10", "~B", 1, 1, 1);
  LC("v2.0.9", "#zb\x01""", 1, 1, 1);
  LC("Zed", "b-9,", 1, 1, 1);
  LC("zed", "0,$ ", 1, 1, 1);
  LC("q2dm1", "9#", 1, 1, 1);
  LC("Q2DM1", "", 1, 1, 1);
  LC("a\x01""", "#\t", 1, 1, 1);
  LC("\x01""a", "B!", -1, -1, -1);
  LC("\t", "$aB\x01""", -1, -1, -1);
  LC(" ", "\x01""9", -1, -1, -1);
  LC("~", ",$B#", 1, 1, 1);
  LC("$", "\t\x01""A ", 1, 1, 1);
  LC("e", "9Z", 1, 1, 1);
  LC("f", "", 1, 1, 1);
  LC("007", "", 1, 1, 1);
  LC("7", "#A", 1, 1, 1);
  LC(".", "", 1, 1, 1);
  LC("zz\x01""A", "", 1, 1, 1);
  LC("9 ", "90B9", -1, -1, -1);
  LC("AZA#", "-Z", 1, 1, 1);
  LC("", "B~a9", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("$b", ",9,a", 1, 1, 1);
  LC("", "~!", -1, -1, -1);
  LC("\x01"".!~", "~a  ", -1, -1, -1);
  LC(".Za\x01""", " 9,-", 1, 1, 1);
  LC("_B #", "~\x01""", -1, -1, -1);
  LC("", "A", -1, -1, -1);
  LC("B\x01""", "a_b", 1, 1, 1);
  LC("", "1.2", -1, -1, -1);
  LC("", "zed", -1, -1, -1);
  LC("", "~", -1, -1, -1);
  LC("$a", ".", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("", "", 0, 0, 0);
  LC(",z", "Zb", -1, -1, -1);
  LC("", ".B_\x01""", -1, -1, -1);
  LC("Zb", " 0", 1, 1, 1);
  LC("~A~,", "", 1, 1, 1);
  LC("", "_ -0", -1, -1, -1);
  LC("", "-$_!", -1, -1, -1);
  LC("0.B#", "\x01""!", 1, 1, 1);
  LC("0.", "!~", 1, 1, 1);
  LC("\t ", "\x01""Z,,", -1, -1, -1);
  LC(".B_\x01""", "", 1, 1, 1);
  LC("!,Bb", "Z9", -1, -1, -1);
  LC("", "Z\x01""", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("AZ", "a0", 1, 1, 1);
  LC("bb", "", 1, 1, 1);
  LC("\x01"".$.", "0~# ", -1, -1, -1);
  LC(" 0", "A.", -1, -1, -1);
  LC("Ab", "", 1, 1, 1);
  LC("-A0-", "", 1, 1, 1);
  LC("!#0\t", ",\x01""\x01""_", 1, 1, 1);
  LC("z-ZZ", "", 1, 1, 1);
  LC("zz", "BB", 1, 1, 1);
  LC("_A .", "!!b ", -1, -1, -1);
  LC("", "bb_B", -1, -1, -1);
  LC("0!99", "Z!", -1, -1, -1);
  LC("_~a\x01""", "A_0~", -1, -1, -1);
  LC(" a", ",a$.", -1, -1, -1);
  LC("9$", "\x01"" ", 1, 1, 1);
  LC("b!z_", "aa", 1, 1, 1);
  LC("", "b\t", -1, -1, -1);
  LC("_ -0", "_#", -1, -1, -1);
  LC("_Az-", "9Z", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("", "", 0, 0, 0);
  LC("", "#00 ", -1, -1, -1);
  LC(".!9b", "0-", -1, -1, -1);
  LC("", "~_-b", -1, -1, -1);
  LC("-$_!", "", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC(",0", " b", 1, 1, 1);
  LC("z9", "a", 1, 1, 1);
  LC(",~$\t", "a-b", -1, -1, -1);
  LC("$a!A", "1.9", -1, -1, -1);
  LC("", "Zed", -1, -1, -1);
  LC("\x01""!", " ", 1, 1, 1);
  LC("0~", "7", -1, -1, -1);
  LC("", "$b", -1, -1, -1);
  LC("~~#a", "", 1, 1, 1);
  LC("!z", "", 1, 1, 1);
  LC("0z ,", "\t ", 1, 1, 1);
  LC("", "\x01"".$.", -1, -1, -1);
  LC("!~", "_A .", 1, 1, 1);
  LC("0Az#", "", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("", "", 0, 0, 0);
  LC("", "", 0, 0, 0);
  LC("", "~bZ\x01""", -1, -1, -1);
  LC("~bZ\x01""", "0a", -1, -1, -1);
  LC("\x01""Z,,", "", 1, 1, 1);
  LC("", "~0!b", -1, -1, -1);
  LC(" z", "!~A#", -1, -1, -1);
  LC("z9", "a_$!", 1, 1, 1);
  LC("Z\tA.", "BzAB", 1, 1, 1);
  LC("\tZ", "_.z.", -1, -1, -1);
  LC("0a", " bZ-", 1, 1, 1);
  LC("", "$ ", -1, -1, -1);
  LC("", "z#Ba", -1, -1, -1);
  LC("\x01""~- ", "AA\x01"" ", -1, -1, -1);
  LC("!#", "\t$", 1, 1, 1);
  LC("_\x01""b-", "\t\x01""Z\x01""", 1, 1, 1);
  LC("9!\t!", "", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("Z9", "\x01""Z", 1, 1, 1);
  LC("Ab", "$AZb", 1, 1, 1);
  LC("-,,,", ".#\tZ", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("B\x01""", "a ", 1, 1, 1);
  LC("", "0.Az", -1, -1, -1);
  LC("~0!b", "", 1, 1, 1);
  LC("Z\x01""", "B,", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("", "Z0", -1, -1, -1);
  LC("z-_#", "", 1, 1, 1);
  LC("A ,!", "", 1, 1, 1);
  LC("-ZB-", "", 1, 1, 1);
  LC("!~A#", "", 1, 1, 1);
  LC("", ".._b", -1, -1, -1);
  LC("~B", "", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("0 ", "Ab", -1, -1, -1);
  LC(".\t", "1.10", -1, -1, -1);
  LC("0\x01""-!", "v2.0.9", -1, -1, -1);
  LC("a_$!", "\t", 1, 1, 1);
  LC("a0", "007", 1, 1, 1);
  LC("#zb\x01""", "", 1, 1, 1);
  LC("", "B\x01""", -1, -1, -1);
  LC("a0", ",z", 1, 1, 1);
  LC("\tz", "0.", -1, -1, -1);
  LC("zZb\x01""", "bb", 1, 1, 1);
  LC("BzAB", "zz", -1, -1, -1);
  LC("", "b!z_", -1, -1, -1);
  LC("b-9,", ".!9b", 1, 1, 1);
  LC("BAB.", "$a!A", 1, 1, 1);
  LC("ab", "0z ,", 1, 1, 1);
  LC("Z~a,", "", 1, 1, 1);
  LC("", "\tZ", -1, -1, -1);
  LC("_.z.", "9!\t!", -1, -1, -1);
  LC("0~# ", "", 1, 1, 1);
  LC("0,$ ", "-ZB-", 1, 1, 1);
  LC("9z", "0\x01""-!", 1, 1, 1);
  LC("!,", "zZb\x01""", -1, -1, -1);
  LC(" _\ta", "", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC(" bZ-", ",z", -1, -1, -1);
  LC("A.", "\x01""#", 1, 1, 1);
  LC("9#", "", 1, 1, 1);
  LC("bZA#", "0\x01""9Z", 1, 1, 1);
  LC("~B", "#.#a", 1, 1, 1);
  LC("\tb", "", 1, 1, 1);
  LC(",z", "~\t", -1, -1, -1);
  LC("$ ", "B0B,", -1, -1, -1);
  LC("", "B#0-", -1, -1, -1);
  LC("", " \x01""", -1, -1, -1);
  LC("", "a-bb", -1, -1, -1);
  LC(".Z\tA", "", 1, 1, 1);
  LC("#\x01""", "0bbb", -1, -1, -1);
  LC("\x01""#", "", 1, 1, 1);
  LC("z#Ba", "Bb", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("#\t", "$!_#", -1, -1, -1);
  LC("", "b!9,", -1, -1, -1);
  LC("a ", "", 1, 1, 1);
  LC("-a", "0.~ ", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("AA\x01"" ", "", 1, 1, 1);
  LC(",\x01""\x01""_", "", 1, 1, 1);
  LC("B!", "~B-,", 1, 1, 1);
  LC("", "aB", -1, -1, -1);
  LC("b $9", "a9", 1, 1, 1);
  LC("", "v2.0.10", -1, -1, -1);
  LC("0\x01""9Z", "\x01""a", -1, -1, -1);
  LC("\t$", "f", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("$aB\x01""", "", 1, 1, 1);
  LC("-aAz", "", 1, 1, 1);
  LC("", "0.B#", -1, -1, -1);
  LC("-0", "AZ", -1, -1, -1);
  LC("#.#a", "z-ZZ", -1, -1, -1);
  LC("\t\x01""Z\x01""", "9$", -1, -1, -1);
  LC("BB", "", 1, 1, 1);
  LC("\x01""9", ",~$\t", 1, 1, 1);
  LC("a-", "!z", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("", "Z\tA.", -1, -1, -1);
  LC("", "_\x01""b-", -1, -1, -1);
  LC("", "B\x01""", -1, -1, -1);
  LC("!!b ", "A ,!", -1, -1, -1);
  LC(",$B#", ".\t", -1, -1, -1);
  LC("zaZB", "\tz", 1, 1, 1);
  LC(",Az.", "Z~a,", -1, -1, -1);
  LC("0_-,", " _\ta", 1, 1, 1);
  LC("~\t", "\tb", 1, 1, 1);
  LC("", "#\x01""", -1, -1, -1);
  LC("bb_B", "-a", 1, 1, 1);
  LC("\t\x01""A ", "", 1, 1, 1);
  LC("zz", "-0", 1, 1, 1);
  LC(",\t", "", 1, 1, 1);
  LC("\t~a_", "0_-,", -1, -1, -1);
  LC("B0B,", "\t~a_", 1, 1, 1);
  LC("\x01""Z", " a", 1, 1, 1);
  LC("Z!", "9#", 1, 1, 1);
  LC("aA", "", 1, 1, 1);
  LC("", "#_", -1, -1, -1);
  LC("0z", "", 1, 1, 1);
  LC(" a", "", 1, 1, 1);
  LC("B#0-", ",\x01""-B", 1, 1, 1);
  LC("$AZb", "", 1, 1, 1);
  LC("A_0~", "!#,0", 1, 1, 1);
  LC("9Z", "$!0A", 1, 1, 1);
  LC(".Z.\t", "", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("9#", "\t9", 1, 1, 1);
  LC(" \x01""", "", 1, 1, 1);
  LC(".#\tZ", "", 1, 1, 1);
  LC(",a$.", "\x01""Zb9", -1, -1, -1);
  LC("", "ab", -1, -1, -1);
  LC(",_", "a10", -1, -1, -1);
  LC("#9", "1.2a", -1, -1, -1);
  LC("", "a\x01""", -1, -1, -1);
  LC("a-bb", "e", -1, -1, -1);
  LC("", "AZA#", -1, -1, -1);
  LC("\x01"" ", "_B #", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("", "", 0, 0, 0);
  LC("", "", 0, 0, 0);
  LC("#_", "!#0\t", 1, 1, 1);
  LC("", " a", -1, -1, -1);
  LC("a ", "", 1, 1, 1);
  LC("aa", "z9", -1, -1, -1);
  LC("#A", "~~#a", -1, -1, -1);
  LC("\x01"".", "", 1, 1, 1);
  LC("", "z9", -1, -1, -1);
  LC("", "!#", -1, -1, -1);
  LC("0bbb", "", 1, 1, 1);
  LC("0.Az", "z-_#", -1, -1, -1);
  LC("b\t", "0 ", 1, 1, 1);
  LC("", "a0", -1, -1, -1);
  LC("a-__", "ab", -1, -1, -1);
  LC("$9", "!,", 1, 1, 1);
  LC("", "~B", -1, -1, -1);
  LC("", ".Z\tA", -1, -1, -1);
  LC("", "a ", -1, -1, -1);
  LC("_#", "b $9", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("!A!-", "", 1, 1, 1);
  LC("$#AA", ",Az.", 1, 1, 1);
  LC(",\x01""-B", ",\t", 1, 1, 1);
  LC("Bb", "0z", 1, 1, 1);
  LC("B,", "", 1, 1, 1);
  LC("9Z", "#9", 1, 1, 1);
  LC("90B9", "", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("", "$9", -1, -1, -1);
  LC("", "$#AA", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("", "~A_~", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("-Z", "", 1, 1, 1);
  LC("_0", "", 1, 1, 1);
  LC("~A_~", "", 1, 1, 1);
  LC("!#,0", "", 1, 1, 1);
  LC("$!_#", "ZA", -1, -1, -1);
  LC("Z0", "#$", 1, 1, 1);
  LC("", "B", -1, -1, -1);
  LC("B~a9", "a1", 1, 1, 1);
  LC("", "1.02", -1, -1, -1);
  LC("", "Q2DM1", -1, -1, -1);
  LC("b!9,", "9 ", 1, 1, 1);
  LC("", ".Za\x01""", -1, -1, -1);
  LC("#00 ", "$a", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("A-", "", 1, 1, 1);
  LC("", "-A0-", -1, -1, -1);
  LC("", "_~a\x01""", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("", ",0", -1, -1, -1);
  LC("0-", "", 1, 1, 1);
  LC(",9,a", "", 1, 1, 1);
  LC("\x01""a", " z", 1, 1, 1);
  LC("", "\x01""~- ", -1, -1, -1);
  LC("", "-,,,", -1, -1, -1);
  LC("0.~ ", "", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("~_-b", "", 1, 1, 1);
  LC("~!", "BAB.", -1, -1, -1);
  LC("Z~#~", "9z", 1, 1, 1);
  LC("", "bZA#", -1, -1, -1);
  LC("\t9", "", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("", "", 0, 0, 0);
  LC("", "-aAz", -1, -1, -1);
  LC("~a  ", "a-", -1, -1, -1);
  LC("a ", "zaZB", -1, -1, -1);
  LC("", "zz", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC("", ".Z.\t", -1, -1, -1);
  LC(".._b", ",_", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC(" 9,-", "\x01"".", -1, -1, -1);
  LC(". ", "a-__", -1, -1, -1);
  LC("ZA", "!A!-", 1, 1, 1);
  LC("", "", 0, 0, 0);
  LC("", "_0", -1, -1, -1);
  LC("", "", 0, 0, 0);
  LC(" b", "A-", -1, -1, -1);
  LC("~\x01""", "\x01""a", -1, -1, -1);
  LC("\x01""Z", "Z~#~", -1, -1, -1);
  LC("#$", "a ", -1, -1, -1);
  LC("\x01""Zb9", ". ", 1, 1, 1);
  /* END bun tools/js_case_reference.js */
  printf("test_js_case: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
