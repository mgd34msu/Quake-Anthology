/*
 * Port of the TypeScript cases covering the core leaf modules:
 * tests/core/binary.test.ts, tests/core/omnitimer.test.ts (timer case),
 * tests/core/resource-name-index.test.ts, tests/core/parse.test.ts
 * (CommonError code), tests/app/q3-product-policy.test.ts,
 * tests/network/q3/client-authorization.test.ts (CD-key owner case),
 * and the SharedCvarMirror scenario of tests/core/commands.test.ts
 * against an in-test registry (CvarRegistry itself is not ported yet).
 * game-numeric, cvars/numbers, md4, commands/filter and info-string
 * have no dedicated TS test files; their expectations are bit patterns
 * and strings produced by running the TS code under bun.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/core/binary/index.h"
#include "qa/core/common_error.h"
#include "qa/core/commands/filter.h"
#include "qa/core/cvars/mirror.h"
#include "qa/core/cvars/numbers.h"
#include "qa/core/game_numeric.h"
#include "qa/core/info_string.h"
#include "qa/core/md4.h"
#include "qa/core/omnitimer.h"
#include "qa/core/q3_cd_key.h"
#include "qa/core/q3_product_policy.h"
#include "qa/core/resource_name_index.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static uint32_t fbits(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  return bits;
}

static int text_is(const char *got, const char *want) {
  return got != NULL && strcmp(got, want) == 0;
}

static void test_common_error(void) {
  CHECK(text_is(qa_common_error_code_name(QA_COMMON_ERROR_SERVER_DISCONNECT),
                "server-disconnect"));
  CHECK(text_is(qa_common_error_code_name(QA_COMMON_ERROR_DROP), "drop"));
  CHECK(text_is(qa_common_error_code_name(QA_COMMON_ERROR_DISCONNECT),
                "disconnect"));
  CHECK(text_is(qa_common_error_code_name(QA_COMMON_ERROR_NEED_CD), "need-cd"));
  CHECK(text_is(qa_common_error_code_name(QA_COMMON_ERROR_FATAL), "fatal"));
  qa_common_error_code code;
  /* tests/core/parse.test.ts: new CommonError("drop", "fixture").code */
  CHECK(qa_common_error_code_parse("drop", &code) && code == QA_COMMON_ERROR_DROP);
  CHECK(!qa_common_error_code_parse("nope", &code));
  CHECK_ABORTS(qa_common_error(QA_COMMON_ERROR_DROP, "fixture"));
}

static void digest_hex(const uint8_t digest[16], char out[33]) {
  for (int i = 0; i < 16; i++) sprintf(out + 2 * i, "%02x", digest[i]);
}

static void test_md4(void) {
  static const struct {
    const char *text;
    const char *hex;
  } cases[] = {
      {"", "31d6cfe0d16ae931b73c59d7e0c089c0"},
      {"a", "bde52cb31de33e46245e05fbdbd6fb24"},
      {"abc", "a448017aaf21d8525fc10ae87aa6729d"},
      {"message digest", "d9130a8164549fe818874806e1c7014b"},
      {"abcdefghijklmnopqrstuvwxyz", "d79e1c308aa5bbcdeea8ed63df412da9"},
      {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
       "043f8582f241db351ce627e153e7f0e4"},
      {"1234567890123456789012345678901234567890123456789012345678901234567890"
       "1234567890",
       "e33b4ddc9c38f2199c3e7b164fcc0536"},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    uint8_t digest[16];
    char hex[33];
    qa_md4((const uint8_t *)cases[i].text, strlen(cases[i].text), digest);
    digest_hex(digest, hex);
    CHECK(text_is(hex, cases[i].hex));
    /* Chunked updates digest identically. */
    qa_md4_context context;
    qa_md4_context_initialize(&context);
    size_t length = strlen(cases[i].text);
    qa_md4_context_update(&context, (const uint8_t *)cases[i].text, length / 2);
    qa_md4_context_update(&context,
                          (const uint8_t *)cases[i].text + length / 2,
                          length - length / 2);
    uint8_t chunked[16];
    qa_md4_context_final(&context, chunked);
    CHECK(memcmp(digest, chunked, 16) == 0);
    CHECK(context.state_a == 0 && context.count[0] == 0);
  }
  CHECK(qa_block_checksum((const uint8_t *)"quake", 5) == 0x8a7528c3u);
  CHECK(qa_block_checksum((const uint8_t *)"", 0) == 0xc6f640b7u);
  CHECK(qa_block_checksum_key((const uint8_t *)"quake", 5, 12345) == 0xa6e37c6eu);
  CHECK(qa_block_checksum_key((const uint8_t *)"quake", 5, -1) == 0xc2532ad7u);
  CHECK_ABORTS((void)qa_block_checksum_key((const uint8_t *)"q", 1, 0x100000000LL));
  CHECK_ABORTS((void)qa_block_checksum_key((const uint8_t *)"q", 1, -0x80000001LL));
}

static void test_game_numeric(void) {
  CHECK(fbits(qa_game_atof("0")) == 0x00000000u);
  CHECK(fbits(qa_game_atof("1.5")) == 0x3fc00000u);
  CHECK(fbits(qa_game_atof("-273.15")) == 0xc3889333u);
  CHECK(fbits(qa_game_atof("  42")) == 0x42280000u);
  CHECK(fbits(qa_game_atof("+7")) == 0x40e00000u);
  CHECK(fbits(qa_game_atof("-0")) == 0x80000000u);
  CHECK(fbits(qa_game_atof(".5")) == 0x3f000000u);
  CHECK(fbits(qa_game_atof("-.5")) == 0xbf000000u);
  CHECK(fbits(qa_game_atof("3.")) == 0x40400000u);
  CHECK(fbits(qa_game_atof("12abc")) == 0x41400000u);
  CHECK(fbits(qa_game_atof("")) == 0x00000000u);
  CHECK(fbits(qa_game_atof("999999999999999999999")) == 0x6258d727u);
  CHECK(fbits(qa_game_atof("0.1")) == 0x3dcccccdu);

  CHECK(fbits(qa_game_atof("123456789.123456789")) == 0x4ceb79a3u);
  CHECK(fbits(qa_game_atof("1e10")) == 0x3f800000u);
  CHECK(fbits(qa_game_atof("\t-2.25x")) == 0xc0100000u);
  CHECK(fbits(qa_game_atof("999999999999999999999999999999999999999")) == 0x7f800000u);

  CHECK(qa_game_atoi("42") == 42);
  CHECK(qa_game_atoi("-42") == -42);
  CHECK(qa_game_atoi("  +12x") == 12);
  CHECK(qa_game_atoi("2147483647") == 2147483647);
  CHECK(qa_game_atoi("2147483648") == INT32_MIN);
  CHECK(qa_game_atoi("-2147483648") == INT32_MIN);
  CHECK(qa_game_atoi("99999999999999999999") == 1661992959);
  CHECK(qa_game_atoi("") == 0);
  CHECK(qa_game_atoi("abc") == 0);
  CHECK(qa_game_atoi("  - 12") == 0);

  qa_game_float_scan scan = qa_scan_game_float("1.5 2.5", 0);
  CHECK(fbits(scan.value) == 0x3fc00000u && scan.next_offset == 4);
  scan = qa_scan_game_float(".5", 0); /* scan leaves a leading dot */
  CHECK(fbits(scan.value) == 0x00000000u && scan.next_offset == 0);
  scan = qa_scan_game_float("-.5", 0);
  CHECK(fbits(scan.value) == 0x80000000u && scan.next_offset == 1);
  scan = qa_scan_game_float("5.x", 0);
  CHECK(fbits(scan.value) == 0x40a00000u && scan.next_offset == 3);
  scan = qa_scan_game_float("12,34", 0);
  CHECK(fbits(scan.value) == 0x41400000u && scan.next_offset == 3);
  scan = qa_scan_game_float("x", 0);
  CHECK(fbits(scan.value) == 0x00000000u && scan.next_offset == 1);
  scan = qa_scan_game_float("1.5", 2);
  CHECK(fbits(scan.value) == 0x40a00000u && scan.next_offset == 4);
  scan = qa_scan_game_float("-", 0);
  CHECK(fbits(scan.value) == 0x80000000u && scan.next_offset == 2);
  /* TS clamps the offset (Math.min(offset, text.length)); a scan past the
   * end reads zero (tests/network/remote-fault-containment.test.ts). */
  scan = qa_scan_game_float("1.5", 4);
  CHECK(fbits(scan.value) == 0 && scan.next_offset == 3);

  qa_game_vec3 vec = qa_scan_game_vector("1 2 3");
  CHECK(fbits(vec.x) == 0x3f800000u && fbits(vec.y) == 0x40000000u &&
        fbits(vec.z) == 0x40400000u);
  vec = qa_scan_game_vector("1.5 -2.5 .25");
  CHECK(fbits(vec.x) == 0x3fc00000u && fbits(vec.y) == 0xc0200000u &&
        fbits(vec.z) == 0x00000000u);
  vec = qa_scan_game_vector("");
  CHECK(fbits(vec.x) == 0 && fbits(vec.y) == 0 && fbits(vec.z) == 0);
  vec = qa_scan_game_vector("1,2,3");
  CHECK(fbits(vec.x) == 0x3f800000u && fbits(vec.z) == 0x40400000u);
  /* TS reads missing components as zero instead of throwing. */
  vec = qa_scan_game_vector("7");
  CHECK(fbits(vec.x) == 0x40e00000u && fbits(vec.y) == 0 && fbits(vec.z) == 0);
  vec = qa_scan_game_vector("1 2");
  CHECK(fbits(vec.x) == 0x3f800000u && fbits(vec.y) == 0x40000000u && fbits(vec.z) == 0);

  qa_game_random random;
  qa_game_random_init(&random, 0);
  static const int32_t want_rand[] = {1, 3534, 1015, 14284, 2653, 1402};
  for (int i = 0; i < 6; i++) CHECK(qa_game_random_rand(&random) == want_rand[i]);
  CHECK(random.seed == -417135238);
  qa_game_random_init(&random, 22222);
  static const uint32_t want_random[] = {0x3f71efe4u, 0x3f5199a3u, 0x3f0ebb1du,
                                         0x3f3cf57au};
  for (int i = 0; i < 4; i++) {
    CHECK(fbits(qa_game_random_random(&random)) == want_random[i]);
  }
  static const uint32_t want_crandom[] = {0x3ecd9d9cu, 0x3f2d635au, 0x3f35e76cu,
                                          0x3e6e69e0u};
  for (int i = 0; i < 4; i++) {
    CHECK(fbits(qa_game_random_crandom(&random)) == want_crandom[i]);
  }
  CHECK(random.seed == -1624912154);
  qa_game_random_reset(&random, -1);
  CHECK(random.seed == -1);
  CHECK(qa_game_random_rand(&random) == 29236);
  qa_game_random_init(&random, 0x100000001LL);
  CHECK(random.seed == 1);
  CHECK_ABORTS(qa_game_random_reset(&random, 9007199254740992LL));
}

static void test_cvar_numbers(void) {
  CHECK(qa_quake_atof("0") == 0);
  CHECK(qa_quake_atof("1.5") == 1.5);
  CHECK(qa_quake_atof("-1.5") == -1.5);
  CHECK(qa_quake_atof("0x1f") == 31);
  CHECK(qa_quake_atof("-0XFF") == -255);
  CHECK(qa_quake_atof("0x") == 0);
  CHECK(qa_quake_atof("'a") == 97);
  CHECK(qa_quake_atof("-'a") == -97);
  CHECK(qa_quake_atof("'") == 0);
  CHECK(qa_quake_atof("12.34.56") == 1234.56);
  CHECK(qa_quake_atof("1.25") == 1.25);
  CHECK(qa_quake_atof(".5") == 0.5);
  CHECK(qa_quake_atof("5.") == 5);
  CHECK(qa_quake_atof("123abc") == 123);
  CHECK(qa_quake_atof("") == 0);
  CHECK(qa_quake_atof("-") == 0);
  CHECK(qa_quake_atof("00.500") == 0.5);
  CHECK(qa_quake_atof("0xg") == 0);
  CHECK(qa_quake_atof("0xabcdef") == 11259375);
  CHECK(qa_quake_atof("99999999999999999999999999") == 1.0000000000000002e26);
  CHECK(qa_quake_atof("1.5e3") == 1.5);
  CHECK(qa_quake_atof(" 1") == 0);

  static const struct {
    double input;
    bool shortcut;
    const char *want;
  } cases[] = {
      {0.0, true, "0"},
      {-0.0, true, "0"},
      {-0.0, false, "-0.000000"},
      {1.0, true, "1"},
      {-2147483648.0, true, "-2147483648"},
      {2147483647.0, true, "2147483648.000000"},
      {2147483648.0, true, "2147483648.000000"},
      {1.5, true, "1.500000"},
      {0.1, false, "0.100000"},
      {-273.15, false, "-273.149994"},
      {1.0 / 3.0, false, "0.333333"},
      {123456.789, false, "123456.789062"},
      {1e20, false, "100000002004087734272.000000"},
      {3.4028235e38, false, "340282346638528859811704183484516925440.000000"},
      {1e-40, false, "0.000000"},
      {0.0000004, false, "0.000000"},
      {0.0000006, false, "0.000001"},
      {2.5, false, "2.500000"},
      {8388607.0, true, "8388607"},
      {16777217.0, true, "16777216"},
      {0.49999998, false, "0.500000"},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char *got = qa_cvar_value_text(cases[i].input, cases[i].shortcut);
    CHECK(text_is(got, cases[i].want));
    free(got);
  }
  CHECK_ABORTS(free(qa_cvar_value_text(INFINITY, false)));
  CHECK_ABORTS(free(qa_cvar_value_text(NAN, false)));
  CHECK_ABORTS(free(qa_cvar_value_text(1e300, false)));
}

static void test_binary(void) {
  qa_binary_writer writer;
  CHECK(qa_binary_writer_init(&writer, 12));
  qa_binary_writer_u16(&writer, 0x1234);
  qa_binary_writer_i16(&writer, -2);
  qa_binary_writer_f32(&writer, -0.0);
  qa_binary_writer_u32(&writer, 0x89abcdef);
  size_t written = 0;
  uint8_t *bytes = qa_binary_writer_finish(&writer, &written);
  static const uint8_t want[12] = {0x34, 0x12, 0xfe, 0xff, 0, 0,
                                   0,    0x80, 0xef, 0xcd, 0xab, 0x89};
  CHECK(written == 12 && bytes != NULL && memcmp(bytes, want, 12) == 0);
  CHECK(qa_binary_writer_offset(&writer) == 12);
  qa_binary_writer_destroy(&writer);

  uint8_t container[20] = {0};
  memcpy(container + 4, bytes, 12);
  free(bytes);
  qa_binary_reader reader;
  CHECK(qa_binary_reader_init(&reader, container + 4, 12, "fixture"));
  CHECK(qa_binary_reader_u16(&reader) == 0x1234);
  CHECK(qa_binary_reader_i16(&reader) == -2);
  float negative_zero = qa_binary_reader_f32(&reader);
  CHECK(negative_zero == 0.0f && signbit(negative_zero));
  CHECK(qa_binary_reader_u32(&reader) == 0x89abcdefu);
  CHECK_ABORTS((void)qa_binary_reader_u8(&reader));
  CHECK(qa_binary_reader_offset(&reader) == 12);
  CHECK(qa_binary_reader_remaining(&reader) == 0);
  CHECK_ABORTS((void)qa_binary_reader_data_view(&reader, 12, 1));
  CHECK_ABORTS({
    qa_binary_reader section;
    qa_binary_reader_section(&reader, -1, 1, &section);
  });
  CHECK_ABORTS({
    qa_binary_reader records;
    qa_binary_reader_records(&reader, 0, 11, 4, &records);
  });
  qa_binary_reader_destroy(&reader);

  uint8_t storage[5] = {9, 65, 255, 0, 8};
  CHECK(qa_binary_reader_init(&reader, storage + 1, 3, NULL));
  qa_binary_reader section;
  CHECK(qa_binary_reader_section(&reader, 0, 3, &section));
  uint8_t *copied = qa_binary_reader_bytes(&reader, 3);
  qa_binary_reader_data_view(&section, 0, 1)[0] = 66;
  static const uint8_t want_copied[3] = {65, 255, 0};
  CHECK(copied != NULL && memcmp(copied, want_copied, 3) == 0);
  free(copied);
  char *byte_string = qa_binary_reader_fixed_byte_string(&section, 3);
  CHECK(byte_string != NULL && (unsigned char)byte_string[0] == 'B' &&
        (unsigned char)byte_string[1] == 0xff && byte_string[2] == '\0');
  free(byte_string);
  CHECK(qa_binary_reader_offset(&reader) == 3);
  static const uint8_t want_storage[5] = {9, 66, 255, 0, 8};
  CHECK(memcmp(storage, want_storage, 5) == 0);
  qa_binary_reader_destroy(&section);
  qa_binary_reader_destroy(&reader);

  /* Magic, UTF-8 strings, seek/skip and the remaining scalar widths. */
  CHECK(qa_binary_writer_init(&writer, 64));
  qa_binary_writer_bytes(&writer, (const uint8_t *)"IDP2", 4);
  qa_binary_writer_i8(&writer, -5);
  qa_binary_writer_i32(&writer, -70000);
  qa_binary_writer_f64(&writer, 2.5);
  qa_binary_writer_bytes(&writer, (const uint8_t *)"h\xc3\xa9", 3);
  written = 0;
  bytes = qa_binary_writer_finish(&writer, &written);
  qa_binary_writer_destroy(&writer);
  CHECK(qa_binary_reader_init(&reader, bytes, written, "magic-fixture"));
  qa_binary_reader_expect_magic(&reader, "IDP2");
  CHECK(qa_binary_reader_i8(&reader) == -5);
  CHECK(qa_binary_reader_i32(&reader) == -70000);
  CHECK(qa_binary_reader_f64(&reader) == 2.5);
  char *utf8 = qa_binary_reader_fixed_string(&reader, 3);
  CHECK(text_is(utf8, "h\xc3\xa9"));
  free(utf8);
  qa_binary_reader_seek(&reader, 4);
  CHECK(qa_binary_reader_offset(&reader) == 4);
  qa_binary_reader_skip(&reader, 1);
  CHECK(qa_binary_reader_i32(&reader) == -70000);
  CHECK_ABORTS(qa_binary_reader_expect_magic(&reader, "ZZ"));
  CHECK_ABORTS(qa_binary_reader_seek(&reader, 9999));
  qa_binary_reader_destroy(&reader);
  free(bytes);

  uint8_t invalid[2] = {0xff, 0x41};
  CHECK(qa_binary_reader_init(&reader, invalid, 2, NULL));
  CHECK_ABORTS(free(qa_binary_reader_fixed_string(&reader, 2)));
  qa_binary_reader_destroy(&reader);

  uint8_t infinite[4] = {0, 0, 0x80, 0x7f};
  CHECK(qa_binary_reader_init(&reader, infinite, 4, NULL));
  CHECK_ABORTS((void)qa_binary_reader_finite_f32(&reader));
  qa_binary_reader_destroy(&reader);

  CHECK(qa_binary_writer_init(&writer, 1));
  CHECK_ABORTS(qa_binary_writer_u16(&writer, 1));
  CHECK_ABORTS(qa_binary_writer_u8(&writer, 256));
  CHECK_ABORTS(qa_binary_writer_i32(&writer, 0x80000000LL));
  qa_binary_writer_destroy(&writer);
  CHECK_ABORTS({ qa_binary_writer bad; qa_binary_writer_init(&bad, -1); });
}

struct pieces {
  char text[8][600];
  size_t count;
};

static void collect_piece(const char *text, void *ctx) {
  struct pieces *pieces = (struct pieces *)ctx;
  strcpy(pieces->text[pieces->count++], text);
}

static void test_info_string(void) {
  struct pieces out = {{{0}}, 0};
  qa_print_info("\\name\\player\\lone", collect_piece, &out);
  CHECK(out.count == 4);
  CHECK(text_is(out.text[0], "name                "));
  CHECK(text_is(out.text[1], "player\n"));
  CHECK(text_is(out.text[2], "lone                "));
  CHECK(text_is(out.text[3], "MISSING VALUE\n"));
  out.count = 0;
  qa_print_info("k\\v", collect_piece, &out);
  CHECK(out.count == 2 && text_is(out.text[0], "k                   ") &&
        text_is(out.text[1], "v\n"));
  out.count = 0;
  qa_print_info("\\a1234567890123456789012345\\v", collect_piece, &out);
  CHECK(out.count == 2 && text_is(out.text[0], "a1234567890123456789012345"));
  out.count = 0;
  qa_print_info("", collect_piece, &out);
  qa_print_info("\\", collect_piece, &out);
  CHECK(out.count == 0);

  char *value = qa_info_value_for_key("\\name\\player\\model\\sarge", "model");
  CHECK(text_is(value, "sarge"));
  free(value);
  value = qa_info_value_for_key("\\Name\\player", "NAME");
  CHECK(text_is(value, "player"));
  free(value);
  value = qa_info_value_for_key("\\name\\player", "x");
  CHECK(text_is(value, ""));
  free(value);
  value = qa_info_value_for_key("name\\player", "name");
  CHECK(text_is(value, "player"));
  free(value);
  value = qa_info_value_for_key("\\name\\player\\lone", "lone");
  CHECK(text_is(value, ""));
  free(value);
  value = qa_info_value_for_key("\\k\\1\\k\\2", "k");
  CHECK(text_is(value, "1"));
  free(value);
  value = qa_info_value_for_key_bounded("\\k\\v", "k", 5);
  CHECK(text_is(value, "v"));
  free(value);
  CHECK_ABORTS(free(qa_info_value_for_key_bounded("\\k\\v", "k", 4)));
  CHECK_ABORTS(free(qa_info_value_for_key_bounded("\\k\\v", "k", 0)));
  CHECK_ABORTS(free(qa_info_value_for_key_bounded("\\k\\v", "k", 8193)));
}

static void test_filter(void) {
  static const struct {
    const char *filter;
    const char *name;
    bool sensitive;
    bool want;
  } cases[] = {
      {"*.bsp", "e1m1.bsp", false, true},
      {"*.bsp", "e1m1.BSP", false, true},
      {"*.bsp", "e1m1.BSP", true, false},
      {"ab", "abc", true, true},
      {"ab", "a", true, false},
      {"a?c", "abc", true, true},
      {"a?", "a", true, true},
      {"?", "", true, true},
      {"?", "a", true, true},
      {"??", "a", true, true},
      {"[a-c]x", "bx", true, true},
      {"[a-c]x", "dx", true, false},
      {"[abc]", "b", true, true},
      {"[]a]", "]", true, false},
      {"[]a]", "a", true, false},
      {"[[", "[", true, false},
      {"a*zz*b", "aXYZzzQQb", true, true},
      {"a*zz*b", "azzb", true, true},
      {"*zz*", "azzb", true, true},
      {"", "anything", true, true},
      {"*", "", true, true},
      {"*a", "", true, false},
      {"A", "a", false, true},
      {"A", "a", true, false},
      {"[A-Z]", "g", false, true},
      {"[A-Z]", "g", true, false},
      {"*.BSP", "x.bsp", false, true},
      {"a[", "a", true, false},
      {"[z-a]", "m", true, false},
      {"x?y?z", "xaybz", true, true},
      {"*a*b*c*", "xxayybzzc", true, true},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    CHECK(qa_source_filter(cases[i].filter, cases[i].name,
                           cases[i].sensitive) == cases[i].want);
  }
  CHECK_ABORTS((void)qa_source_filter("[a", "a", true));
  char *long_filter = (char *)malloc(1026);
  char *long_name = (char *)malloc(1025);
  CHECK(long_filter != NULL && long_name != NULL);
  long_filter[0] = '*';
  memset(long_filter + 1, 'a', 1024);
  long_filter[1025] = '\0';
  memset(long_name, 'a', 1024);
  long_name[1024] = '\0';
  CHECK_ABORTS((void)qa_source_filter(long_filter, long_name, true));
  long_filter[1024] = '\0'; /* star run of 1023 */
  long_name[1023] = '\0';
  CHECK(qa_source_filter(long_filter, long_name, true) == true);
  free(long_filter);
  free(long_name);
}

static int64_t source_find(const char *values[7], const char *name) {
  if (strcmp(name, "") == 0) return 0;
  for (int64_t slot = 1; slot < 7; slot++) {
    if (slot != 2 && (strcmp(values[slot], name) == 0 ||
                      strcmp(values[slot], "") == 0)) {
      return slot;
    }
  }
  return -1;
}

static void test_resource_name_index(void) {
  qa_resource_name_index index;
  const int64_t reserved[] = {2};
  CHECK(qa_resource_name_index_init(&index, 7, reserved, 1));
  const char *values[7] = {"", "", "", "", "", "", ""};
  static const char *const names[] = {"", "model/a", "model/b", "model/c",
                                      "missing"};
  uint32_t seed = 17;
  for (int step = 0; step < 100; step++) {
    seed = seed * 1664525u + 1013904223u;
    int64_t slot = 1 + (int64_t)(seed % 6u);
    const char *name = names[(seed >> 8) % 4u];
    values[slot] = name;
    qa_resource_name_index_set(&index, slot, name);
    for (int c = 0; c < 5; c++) {
      CHECK(qa_resource_name_index_find(&index, names[c]) ==
            source_find(values, names[c]));
    }
  }
  qa_resource_name_index_clear(&index);
  CHECK(qa_resource_name_index_find(&index, "model/a") == 1);
  for (int64_t slot = 1; slot < 7; slot++) {
    qa_resource_name_index_set(&index, slot, "full");
  }
  CHECK(qa_resource_name_index_find(&index, "full") == 1);
  CHECK(qa_resource_name_index_find(&index, "missing") == -1);
  qa_resource_name_index_set(&index, 1, "");
  CHECK(qa_resource_name_index_find(&index, "full") == 1);
  qa_resource_name_index_set(&index, 1, "other");
  CHECK(qa_resource_name_index_find(&index, "full") == 3);
  CHECK(qa_resource_name_index_find(&index, "") == 0);
  qa_resource_name_index_set(&index, 0, "ignored");
  qa_resource_name_index_set(&index, 99, "ignored");
  CHECK(qa_resource_name_index_find(&index, "full") == 3);
  qa_resource_name_index_destroy(&index);
  CHECK_ABORTS({
    qa_resource_name_index bad;
    qa_resource_name_index_init(&bad, 0, NULL, 0);
  });
}

struct timer_fixture {
  qa_omni_timer *timer;
  double now;
};

static double fixture_clock(void *ctx) {
  return ((struct timer_fixture *)ctx)->now;
}

static void *render_op(void *ctx) {
  ((struct timer_fixture *)ctx)->now = 5;
  return ctx;
}

static void *frame_op(void *ctx) {
  struct timer_fixture *fixture = (struct timer_fixture *)ctx;
  fixture->now = 2;
  qa_omni_timer_measure(fixture->timer, "render", render_op, fixture);
  fixture->now = 7;
  return ctx;
}

static void test_omnitimer(void) {
  struct timer_fixture fixture = {NULL, 0};
  fixture.timer = qa_omni_timer_create(fixture_clock, &fixture, 2);
  CHECK(fixture.timer != NULL);
  CHECK(!qa_omni_timer_enabled(fixture.timer));
  /* Disabled: push/pop/stamp are no-ops, measure just runs. */
  qa_omni_timer_push(fixture.timer, "off");
  qa_omni_timer_pop(fixture.timer);
  CHECK(qa_omni_timer_measure(fixture.timer, "off", render_op, &fixture) ==
        &fixture);
  size_t count = 99;
  qa_timer_report *rows = qa_omni_timer_report(fixture.timer, &count);
  CHECK(count == 0 && rows == NULL);

  fixture.now = 0;
  qa_omni_timer_set_enabled(fixture.timer, true);
  CHECK(qa_omni_timer_enabled(fixture.timer));
  qa_omni_timer_measure(fixture.timer, "frame", frame_op, &fixture);
  rows = qa_omni_timer_report(fixture.timer, &count);
  CHECK(count == 2 && rows != NULL);
  CHECK(text_is(rows[0].name, "render") && rows[0].calls == 1 &&
        rows[0].total_milliseconds == 3 && rows[0].self_milliseconds == 3 &&
        rows[0].maximum_milliseconds == 3);
  CHECK(text_is(rows[1].name, "frame") && rows[1].calls == 1 &&
        rows[1].total_milliseconds == 7 && rows[1].self_milliseconds == 4 &&
        rows[1].maximum_milliseconds == 7);
  free(rows);
  char *formatted = qa_omni_timer_format(fixture.timer);
  CHECK(text_is(formatted, "name\tcalls\ttotal_ms\tself_ms\tmax_ms\n"
                           "render\t1\t3.000\t3.000\t3.000\n"
                           "frame\t1\t7.000\t4.000\t7.000\n"));
  free(formatted);

  fixture.now = 9;
  qa_omni_timer_stamp(fixture.timer, "first");
  qa_omni_timer_stamp(fixture.timer, "second");
  qa_omni_timer_stamp(fixture.timer, "third");
  qa_timer_stamp *stamps = qa_omni_timer_stamp_list(fixture.timer, &count);
  CHECK(count == 2 && stamps != NULL);
  CHECK(text_is(stamps[0].name, "second") && text_is(stamps[1].name, "third"));
  CHECK(stamps[0].milliseconds == 9 && stamps[1].milliseconds == 9);
  free(stamps);

  qa_omni_timer_reset(fixture.timer);
  rows = qa_omni_timer_report(fixture.timer, &count);
  CHECK(count == 0 && rows == NULL);
  CHECK_ABORTS(qa_omni_timer_pop(fixture.timer));
  CHECK_ABORTS({
    qa_omni_timer_push(fixture.timer, "scope");
    qa_omni_timer_set_enabled(fixture.timer, false);
  });
  CHECK_ABORTS({
    qa_omni_timer_push(fixture.timer, "scope");
    qa_omni_timer_reset(fixture.timer);
  });
  qa_omni_timer_destroy(fixture.timer);
  CHECK_ABORTS((void)qa_omni_timer_create(fixture_clock, &fixture, 0));
}

struct key_store {
  char *text;
};

static const char *key_load(void *ctx, const char *name) {
  (void)name;
  return ((struct key_store *)ctx)->text;
}

static void key_dump(void *ctx, const char *name, const char *contents) {
  (void)name;
  struct key_store *store = (struct key_store *)ctx;
  free(store->text);
  size_t length = strlen(contents) + 1;
  store->text = (char *)malloc(length);
  memcpy(store->text, contents, length);
}

struct flag_sink {
  int64_t flags;
};

static void mark_modified(void *ctx, int64_t flags) {
  ((struct flag_sink *)ctx)->flags |= flags;
}

static void test_q3_cd_key(void) {
  CHECK(qa_validate_q3_cd_key("2222222222222222", "20"));
  CHECK(!qa_validate_q3_cd_key("2222222222222222", "21"));
  CHECK(!qa_validate_q3_cd_key("IIIIIIIIIIIIIIII", NULL));
  CHECK(qa_validate_q3_cd_key("abcdabcdabcdabcd", NULL));
  CHECK(!qa_validate_q3_cd_key("ABCDABCDABCDABCD", "x"));
  CHECK(!qa_validate_q3_cd_key("222", NULL));
  CHECK(!qa_validate_q3_cd_key("2222222222222222", "200"));
  qa_q3_key_build build;
  CHECK(qa_q3_key_build_parse("dedicated", &build) &&
        build == QA_Q3_KEY_BUILD_DEDICATED);
  CHECK(text_is(qa_q3_key_build_name(QA_Q3_KEY_BUILD_CLIENT), "client"));

  /* Dedicated constructor bytes are '1'..'9' then zeros. */
  qa_q3_cd_key_state dedicated;
  qa_q3_cd_key_state_init(&dedicated, NULL, NULL, QA_Q3_KEY_BUILD_DEDICATED);
  CHECK(memcmp(dedicated.bytes, "123456789", 9) == 0 && dedicated.bytes[9] == 0);

  struct key_store base = {NULL}, mod = {NULL};
  struct flag_sink sink = {0};
  qa_q3_key_storage base_storage = {&base, key_load, key_dump};
  qa_q3_key_storage mod_storage = {&mod, key_load, key_dump};
  key_dump(&base, "q3key", "2222222222222222\n");
  key_dump(&mod, "q3key", "3333333333333333\n");
  qa_q3_cd_key_state keys;
  qa_q3_cd_key_state_init(&keys, mark_modified, &sink, QA_Q3_KEY_BUILD_CLIENT);
  qa_q3_cd_key_state_read_file(&keys, &base_storage);
  qa_q3_cd_key_state_append_file(&keys, &mod_storage);
  uint8_t destination[33], ui[17];
  qa_q3_cd_key_state_read_authorization(&keys, destination, sizeof destination);
  CHECK(memcmp(destination, "22222222222222223333333333333333", 32) == 0);
  CHECK(destination[32] == 0);
  qa_q3_cd_key_state_read_ui(&keys, 1, "missionpack", ui, sizeof ui, NULL);
  CHECK(memcmp(ui, "3333333333333333", 16) == 0 && ui[16] == 0);
  qa_q3_cd_key_state_read_ui(&keys, 2, "missionpack", ui, sizeof ui, NULL);
  CHECK(memcmp(ui, "2222222222222222", 16) == 0);
  qa_q3_cd_key_state_read_ui(&keys, 1, "", ui, sizeof ui, NULL);
  CHECK(memcmp(ui, "2222222222222222", 16) == 0);
  qa_q3_cd_key_state_write_ui(&keys, 1, "missionpack",
                              (const uint8_t *)"2222222222222222", 16);
  CHECK((sink.flags & 1) != 0); /* CvarFlag.Archive */
  qa_q3_cd_key_state_write_file(&keys, &mod_storage, 16);
  CHECK(mod.text != NULL &&
        strncmp(mod.text, "2222222222222222\n// generated by quake", 38) == 0);
  struct key_store missing = {NULL};
  qa_q3_key_storage missing_storage = {&missing, key_load, key_dump};
  qa_q3_cd_key_state_read_file(&keys, &missing_storage);
  qa_q3_cd_key_state_read_authorization(&keys, destination, sizeof destination);
  CHECK(destination[16] == 0);
  int spaces = 1;
  for (int i = 0; i < 16; i++) spaces = spaces && destination[i] == 32;
  CHECK(spaces);
  /* An invalid stored key is not written back out. */
  qa_q3_cd_key_state_write_file(&keys, &mod_storage, 0);
  CHECK(strncmp(mod.text, "2222222222222222\n// generated by quake", 38) == 0);
  CHECK_ABORTS(qa_q3_cd_key_state_read_authorization(&keys, destination, 32));
  CHECK_ABORTS(qa_q3_cd_key_state_read_ui(&keys, 0, "", ui, 16, NULL));
  CHECK_ABORTS(qa_q3_cd_key_state_write_ui(&keys, 0, "",
                                           (const uint8_t *)"short", 5));
  free(base.text);
  free(mod.text);
}

struct fake_registrar {
  int64_t demo;
  int64_t team_arena;
  bool present;
};

static bool fake_register(void *ctx, const char *name, const char *default_value,
                          int64_t flags, int64_t *out) {
  struct fake_registrar *fake = (struct fake_registrar *)ctx;
  CHECK(text_is(default_value, "0"));
  CHECK(flags == 16); /* CvarFlag.Init */
  if (!fake->present) return false;
  *out = strcmp(name, "com_prereleaseDemo") == 0 ? fake->demo : fake->team_arena;
  return true;
}

static void test_q3_product_policy(void) {
  for (int demo = 0; demo < 2; demo++) {
    for (int ta = 0; ta < 2; ta++) {
      qa_q3_product_policy policy = qa_q3_product_policy_make(demo, ta);
      CHECK(qa_q3_prerelease_demo(policy) == (demo != 0));
      CHECK(qa_q3_team_arena_demo(policy) == (ta != 0));
      qa_q3_mount_restriction open =
          qa_q3_mount_restriction_make(policy, false);
      CHECK((open.kind == QA_Q3_MOUNT_DEMO) == (demo != 0));
      qa_q3_mount_restriction restricted =
          qa_q3_mount_restriction_make(policy, true);
      CHECK(restricted.kind == QA_Q3_MOUNT_DEMO &&
            text_is(restricted.directory, "demota") &&
            restricted.pak_checksum == 437558517);
      size_t count = 0;
      const char *const *commands =
          qa_q3_product_map_commands(policy, &count);
      if (demo) {
        CHECK(count == 1 && text_is(commands[0], "map"));
      } else {
        CHECK(count == 4 && text_is(commands[0], "map") &&
              text_is(commands[1], "devmap") && text_is(commands[2], "spmap") &&
              text_is(commands[3], "spdevmap"));
      }
      qa_q3_team_arena_catalog_policy catalog =
          qa_q3_team_arena_catalog_policy_make(policy);
      CHECK(text_is(catalog.game_info, ta ? "demogameinfo.txt" : "gameinfo.txt"));
      CHECK(text_is(catalog.team_info, ta ? "demoteaminfo.txt" : "teaminfo.txt"));
      CHECK(catalog.additional_teams == !ta && catalog.additional_arenas == !ta);
    }
  }
  qa_q3_product_policy policy = qa_q3_product_policy_make(true, true);
  CHECK(text_is(qa_q3_product_kind_name(policy.kind), "prerelease-demo"));
  CHECK(text_is(qa_q3_team_arena_ui_name(policy.team_arena_ui), "demo"));
  qa_q3_product_kind kind;
  CHECK(qa_q3_product_kind_parse("prerelease-ta-demo", &kind) &&
        kind == QA_Q3_PRODUCT_PRERELEASE_TA_DEMO);
  CHECK(!qa_q3_product_kind_parse("shareware", &kind));
  CHECK(text_is(qa_q3_mount_kind_name(QA_Q3_MOUNT_NONE), "none"));

  struct fake_registrar fake = {1, 0, true};
  qa_q3_cvar_registrar registrar = {&fake, fake_register};
  policy = qa_register_q3_product_policy(&registrar);
  CHECK(policy.kind == QA_Q3_PRODUCT_PRERELEASE_DEMO &&
        policy.team_arena_ui == QA_Q3_TEAM_ARENA_UI_RETAIL);
  fake.demo = 0;
  fake.team_arena = 3;
  policy = qa_register_q3_product_policy(&registrar);
  CHECK(policy.kind == QA_Q3_PRODUCT_PRERELEASE_TA_DEMO);
  fake.present = false;
  policy = qa_register_q3_product_policy(&registrar);
  CHECK(policy.kind == QA_Q3_PRODUCT_RETAIL);
}

/* Minimal CvarRegistry stand-in for the mirror's used surface. */
struct fake_cvar {
  char name[64];
  char *value;
  char *reset;
  int64_t flags;
  qa_cvar_mirror_binding binding;
  bool has_binding;
};

struct fake_registry {
  struct fake_cvar vars[16];
  size_t count;
};

static char *fake_dup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  memcpy(copy, text, length);
  return copy;
}

static struct fake_cvar *fake_lookup(struct fake_registry *registry,
                                     const char *name) {
  for (size_t i = 0; i < registry->count; i++) {
    if (strcmp(registry->vars[i].name, name) == 0) return &registry->vars[i];
  }
  return NULL;
}

static bool fake_find(void *ctx, const char *name, qa_cvar_mirror_read *out) {
  struct fake_cvar *var = fake_lookup((struct fake_registry *)ctx, name);
  if (var == NULL) return false;
  out->reset_value = var->reset;
  out->flags = var->flags;
  return true;
}

static void fake_register_cvar(void *ctx, const char *name,
                               const char *default_value, int64_t flags) {
  struct fake_registry *registry = (struct fake_registry *)ctx;
  if (fake_lookup(registry, name) != NULL) return;
  struct fake_cvar *var = &registry->vars[registry->count++];
  strcpy(var->name, name);
  var->value = fake_dup(default_value);
  var->reset = fake_dup(default_value);
  var->flags = flags;
  var->has_binding = false;
}

static void fake_set(void *ctx, const char *name, const char *value,
                     bool force) {
  (void)force;
  struct fake_registry *registry = (struct fake_registry *)ctx;
  struct fake_cvar *var = fake_lookup(registry, name);
  if (var == NULL) {
    fake_register_cvar(ctx, name, value, 0);
    return;
  }
  if (var->has_binding && var->binding.validate != NULL &&
      var->binding.validate(var->binding.ctx, value) != NULL) {
    return;
  }
  char *copy = fake_dup(value);
  free(var->value);
  var->value = copy;
  if (var->has_binding && var->binding.changed != NULL) {
    var->binding.changed(var->binding.ctx, var->value);
  }
}

static const char *fake_variable_string(void *ctx, const char *name) {
  struct fake_cvar *var = fake_lookup((struct fake_registry *)ctx, name);
  return var != NULL ? var->value : "";
}

struct fake_release {
  struct fake_registry *registry;
  char name[64];
  void *binding_ctx;
};

static void fake_release_run(void *ctx) {
  struct fake_release *release = (struct fake_release *)ctx;
  struct fake_cvar *var = fake_lookup(release->registry, release->name);
  if (var != NULL && var->has_binding &&
      var->binding.ctx == release->binding_ctx) {
    var->has_binding = false;
  }
  free(release);
}

static qa_cvar_mirror_release fake_bind(void *ctx, const char *name,
                                        qa_cvar_mirror_binding binding) {
  struct fake_registry *registry = (struct fake_registry *)ctx;
  struct fake_cvar *var = fake_lookup(registry, name);
  if (var == NULL || var->has_binding) abort(); /* TS throws Error here */
  var->binding = binding;
  var->has_binding = true;
  struct fake_release *release =
      (struct fake_release *)malloc(sizeof(struct fake_release));
  release->registry = registry;
  strcpy(release->name, name);
  release->binding_ctx = binding.ctx;
  qa_cvar_mirror_release result = {release, fake_release_run};
  return result;
}

static void fake_registry_clear(struct fake_registry *registry) {
  for (size_t i = 0; i < registry->count; i++) {
    free(registry->vars[i].value);
    free(registry->vars[i].reset);
  }
  registry->count = 0;
}

struct assert_counter {
  int calls;
};

static void count_assert(void *ctx) { ((struct assert_counter *)ctx)->calls++; }

static void test_cvar_mirror(void) {
  static int session_a, session_b;
  struct fake_registry server, seat0, seat1, foreign;
  memset(&server, 0, sizeof server);
  memset(&seat0, 0, sizeof seat0);
  memset(&seat1, 0, sizeof seat1);
  memset(&foreign, 0, sizeof foreign);
  qa_cvar_mirror_registry server_iface = {&server, &session_a, "q3", fake_find,
                                          fake_register_cvar, fake_set,
                                          fake_variable_string, fake_bind};
  qa_cvar_mirror_registry seat0_iface = {&seat0, &session_a, "q3", fake_find,
                                         fake_register_cvar, fake_set,
                                         fake_variable_string, fake_bind};
  qa_cvar_mirror_registry seat1_iface = {&seat1, &session_a, "q3", fake_find,
                                         fake_register_cvar, fake_set,
                                         fake_variable_string, fake_bind};
  qa_cvar_mirror_registry foreign_iface = {
      &foreign, &session_b, "q3", fake_find, fake_register_cvar, fake_set,
      fake_variable_string, fake_bind};
  static const char *const names[] = {"cm_noCurves", "cm_playerCurveClip",
                                      "cm_noAreas"};
  for (int i = 0; i < 3; i++) fake_register_cvar(&server, names[i], "0", 0);
  struct assert_counter counter = {0};
  qa_shared_cvar_mirror *mirror0 = qa_shared_cvar_mirror_create(
      &server_iface, &seat0_iface, names, 3, count_assert, &counter);
  qa_shared_cvar_mirror *mirror1 = qa_shared_cvar_mirror_create(
      &server_iface, &seat1_iface, names, 3, count_assert, &counter);
  CHECK(mirror0 != NULL && mirror1 != NULL);
  CHECK(text_is(fake_variable_string(&seat0, "cm_noCurves"), "0"));
  fake_register_cvar(&seat0, "cg_fov", "90", 0);
  fake_register_cvar(&seat1, "cg_fov", "100", 0);

  fake_set(&server, "cm_noCurves", "1", true);
  fake_set(&server, "cm_noAreas", "1", true);
  CHECK(text_is(fake_variable_string(&seat0, "cm_noCurves"), "1"));
  CHECK(text_is(fake_variable_string(&seat1, "cm_noCurves"), "1"));
  CHECK(text_is(fake_variable_string(&seat1, "cm_noAreas"), "1"));
  CHECK(counter.calls == 0); /* refresh never asserts */
  CHECK(text_is(fake_variable_string(&seat0, "cg_fov"), "90"));
  CHECK(text_is(fake_variable_string(&seat1, "cg_fov"), "100"));

  fake_set(&seat0, "cm_noCurves", "0", false);
  CHECK(text_is(fake_variable_string(&server, "cm_noCurves"), "0"));
  CHECK(text_is(fake_variable_string(&seat1, "cm_noCurves"), "0"));
  CHECK(counter.calls == 1);

  CHECK_ABORTS((void)qa_shared_cvar_mirror_create(
      &server_iface, &foreign_iface, names, 3, count_assert, &counter));
  CHECK_ABORTS((void)qa_shared_cvar_mirror_create(
      &server_iface, &server_iface, names, 3, count_assert, &counter));
  static const char *const undeclared[] = {"not_declared"};
  CHECK_ABORTS((void)qa_shared_cvar_mirror_create(
      &server_iface, &seat0_iface, undeclared, 1, count_assert, &counter));

  qa_shared_cvar_mirror_destroy(mirror0);
  qa_shared_cvar_mirror_destroy(mirror1);
  fake_set(&server, "cm_noCurves", "1", true);
  CHECK(text_is(fake_variable_string(&seat0, "cm_noCurves"), "0"));
  CHECK(text_is(fake_variable_string(&seat1, "cm_noCurves"), "0"));
  fake_registry_clear(&server);
  fake_registry_clear(&seat0);
  fake_registry_clear(&seat1);
  fake_registry_clear(&foreign);
}

int main(void) {
  test_common_error();
  test_md4();
  test_game_numeric();
  test_cvar_numbers();
  test_binary();
  test_info_string();
  test_filter();
  test_resource_name_index();
  test_omnitimer();
  test_q3_cd_key();
  test_q3_product_policy();
  test_cvar_mirror();
  fprintf(stderr, "%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}



