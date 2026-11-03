/* Rows read against src/guest/runtime/windows/{crt,kernel}.ts and
 * runtime/common/format.ts retainedFormat, on test_guest_runtime.c's fixtures.
 * GPL-2.0-or-later. */
#define main test_guest_runtime_main
#include "test_guest_runtime.c"
#undef main

static qa_call_result pow_call(win_fixture *s, double base, double exponent) {
  qa_call_value args[] = {qa_call_float64(base), qa_call_float64(exponent)};
  return win_call(s, "msvcrt.dll", "pow", args, 2);
}

static size_t multibyte(win_fixture *s, uint32_t code_page, const uint8_t *bytes, size_t length, uint16_t *out) {
  fixture *f = &s->f;
  qa_mapped_guest_memory *m = &f->mem.base;
  qa_guest_core_address source = qa_gmem_allocate(&f->ext, length, false, 0, false, QA_PERM_READ_WRITE, "mb source");
  qa_guest_core_address output = qa_gmem_allocate(&f->ext, 64, false, 0, false, QA_PERM_READ_WRITE, "mb out");
  qa_memory_write(m, source, bytes, length);
  qa_call_value args[] = {qa_call_uint32(code_page), qa_call_uint32(0), ptr_arg(true, source),
                          qa_call_int32((int32_t)length), ptr_arg(true, output), qa_call_int32(32)};
  qa_call_result r = win_call(s, "kernel32.dll", "MultiByteToWideChar", args, 6);
  size_t count = (size_t)r.number;
  for (size_t i = 0; i < count; i++)
    out[i] = (uint16_t)qa_memory_read_u32(m, A(output.byte_offset + i * 2)) & 0xffff;
  return count;
}

static void test_crt_rows(void) {
  win_fixture s;
  win_init(&s, 8);
  /* Math.pow, unlike C pow. */
  CHECK(isnan(pow_call(&s, 1, NAN).number));
  CHECK(isnan(pow_call(&s, 1, INFINITY).number));
  CHECK(isnan(pow_call(&s, -1, -INFINITY).number));
  CHECK(pow_call(&s, NAN, 0).number == 1);
  CHECK(pow_call(&s, 2, 10).number == 1024);
  CHECK(pow_call(&s, 2, INFINITY).number == INFINITY);

  /* TextDecoder('utf-8'): the BOM is dropped, one U+FFFD per maximal subpart. */
  uint16_t out[32];
  static const uint8_t bom[] = {0xef, 0xbb, 0xbf, 'A'};
  CHECK(multibyte(&s, 65001, bom, sizeof bom, out) == 1 && out[0] == 'A');
  static const uint8_t truncated[] = {'A', 0xe2, 0x82};
  CHECK(multibyte(&s, 65001, truncated, sizeof truncated, out) == 2 && out[0] == 'A' && out[1] == 0xfffd);
  static const uint8_t four[] = {0xf0, 0x90, 0x80, 'x'};
  CHECK(multibyte(&s, 65001, four, sizeof four, out) == 2 && out[0] == 0xfffd && out[1] == 'x');
  static const uint8_t surrogate[] = {0xed, 0xa0, 0x80};
  CHECK(multibyte(&s, 65001, surrogate, sizeof surrogate, out) == 3 && out[0] == 0xfffd && out[1] == 0xfffd &&
        out[2] == 0xfffd);
  static const uint8_t overlong[] = {0xc0, 0xaf};
  CHECK(multibyte(&s, 65001, overlong, sizeof overlong, out) == 2 && out[0] == 0xfffd && out[1] == 0xfffd);
  static const uint8_t astral[] = {0xf0, 0x9f, 0x98, 0x80};
  CHECK(multibyte(&s, 65001, astral, sizeof astral, out) == 2 && out[0] == 0xd83d && out[1] == 0xde00);
  qa_windows_guest_runtime_destroy(&s.rt);
  qa_x64_cpu_destroy(&s.f.x64);
  qa_sparse_guest_memory_destroy(&s.f.mem);
}

/* retainedFormat changes cost only: a retained format formats like a fresh one,
 * per dialect and pointer width, and across FIFO eviction. */
static int format_into(fixture *f, qa_format_dialect dialect, qa_guest_core_address format,
                       qa_guest_core_address buffer, qa_guest_core_address args) {
  qa_guest_format_request request;
  memset(&request, 0, sizeof request);
  request.memory = &f->mem.base;
  request.memory_ext = &f->ext;
  request.dialect = dialect;
  request.has_buffer = true;
  request.buffer = buffer;
  request.capacity = 512;
  request.has_format = true;
  request.format = format;
  request.has_arguments = true;
  request.arguments = args;
  request.termination = QA_TERMINATION_C99;
  request.exponent_digits = 2;
  return (int)qa_format_guest_buffer(&request).result;
}

static void test_retained_format(void) {
  fixture f;
  fixture_init(&f, 8);
  qa_mapped_guest_memory *m = &f.mem.base;
  qa_guest_core_address buffer = qa_gmem_allocate(&f.ext, 512, false, 0, false, QA_PERM_READ_WRITE, "out");
  qa_guest_core_address args = qa_gmem_allocate(&f.ext, 64, false, 0, false, QA_PERM_READ_WRITE, "args");
  qa_memory_write_u64(m, args, 0x1122334455667788ull);
  /* System V va_list: every register slot consumed, so values come from the
   * overflow area at args. */
  qa_guest_core_address va_list = qa_gmem_allocate(&f.ext, 24, false, 0, false, QA_PERM_READ_WRITE, "va_list");
  qa_memory_write_u32(m, va_list, 48);
  qa_memory_write_u32(m, A(va_list.byte_offset + 4), 176);
  qa_memory_write_u64(m, A(va_list.byte_offset + 8), args.byte_offset);
  qa_memory_write_u64(m, A(va_list.byte_offset + 16), args.byte_offset);
  char expect[64];

  /* %ld is 32 bits on Windows and pointer width on System V: one source text,
   * two parses. */
  qa_guest_core_address ld = guest_text(&f, "%ld");
  for (int pass = 0; pass < 2; pass++) {
    (void)format_into(&f, QA_FORMAT_WINDOWS, ld, buffer, args);
    char *text = qa_read_string(m, &f.ext, buffer, 64);
    (void)snprintf(expect, sizeof expect, "%d", (int)0x55667788);
    CHECK(strcmp(text, expect) == 0);
    free(text);
    qa_memory_write_u64(m, A(va_list.byte_offset + 8), args.byte_offset); /* va_arg advanced it */
    (void)format_into(&f, QA_FORMAT_SYSTEM_V, ld, buffer, va_list);
    text = qa_read_string(m, &f.ext, buffer, 64);
    (void)snprintf(expect, sizeof expect, "%lld", (long long)0x1122334455667788ll);
    CHECK(strcmp(text, expect) == 0);
    free(text);
  }

  /* More distinct sources than the FIFO holds, then the first again. */
  for (int i = 0; i < 300; i++) {
    char source[32];
    (void)snprintf(source, sizeof source, "v%d=%%d", i);
    qa_guest_core_address format = guest_text(&f, source);
    (void)format_into(&f, QA_FORMAT_WINDOWS, format, buffer, args);
    char *text = qa_read_string(m, &f.ext, buffer, 64);
    (void)snprintf(expect, sizeof expect, "v%d=%d", i, (int)0x55667788);
    CHECK(strcmp(text, expect) == 0);
    free(text);
  }
  CHECK(format_into(&f, QA_FORMAT_WINDOWS, ld, buffer, args) > 0);

  /* A source past the retention limit still formats. */
  char *longer = malloc(5001);
  memset(longer, 'x', 5000);
  longer[5000] = '\0';
  qa_guest_format_request request;
  qa_guest_core_address long_format = guest_text(&f, longer);
  memset(&request, 0, sizeof request);
  request.memory = m;
  request.memory_ext = &f.ext;
  request.dialect = QA_FORMAT_WINDOWS;
  request.has_format = true;
  request.format = long_format;
  request.has_arguments = true;
  request.arguments = args;
  request.termination = QA_TERMINATION_C99;
  request.exponent_digits = 2;
  CHECK(qa_format_guest_buffer(&request).result == 5000);
  free(longer);
  qa_x64_cpu_destroy(&f.x64);
  qa_sparse_guest_memory_destroy(&f.mem);
}

int main(void) {
  test_crt_rows();
  test_retained_format();
  printf("test_guest_win_rows: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
