/* Port of src/compat/qc/program.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/program.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

noreturn void qa_compat_qc_program_error(const char *source, const char *format, ...) {
  char msg[QA_CAUGHT_MESSAGE_CAPACITY]; va_list ap; va_start(ap, format); vsnprintf(msg, sizeof msg, format, ap); va_end(ap);
  qa_raise("QcProgramError", 0, 0, "%s: %s", source ? source : "progs.dat", msg);
}
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *r = malloc(n); if (r) memcpy(r, s, n); return r; }
char *qa_compat_qc_byte_string(const uint8_t *bytes, size_t length, int64_t offset) {
  if (offset < 0 || (uint64_t)offset >= length) qa_compat_qc_program_error("progs.dat", "invalid string offset %lld", (long long)offset);
  for (size_t i = (size_t)offset; i < length; i++) if (bytes[i] == 0) {
    size_t n = i - (size_t)offset; char *r = malloc(n + 1); if (!r) return NULL; memcpy(r, bytes + offset, n); r[n] = 0; return r;
  }
  qa_compat_qc_program_error("progs.dat", "unterminated string at %lld", (long long)offset);
}
int64_t qa_compat_qc_signed_branch(int64_t word) { return (int64_t)(int32_t)((uint32_t)(word & 0xffffu) << 16) >> 16; }
const qa_compat_qc_definition *qa_compat_qc_program_global_named(const qa_compat_qc_program *p, const char *name) {
  for (size_t i = 0; i < p->global_count; i++) { if (strcmp(p->globals[i].name, name) == 0) return &p->globals[i]; } return NULL;
}
const qa_compat_qc_definition *qa_compat_qc_program_field_named(const qa_compat_qc_program *p, const char *name) {
  for (size_t i = 0; i < p->field_count; i++) { if (strcmp(p->fields[i].name, name) == 0) return &p->fields[i]; } return NULL;
}
const qa_compat_qc_function *qa_compat_qc_program_function_named_map(const qa_compat_qc_program *p, const char *name) {
  for (size_t i = 0; i < p->function_count; i++) { if (strcmp(p->functions[i].name, name) == 0) return &p->functions[i]; } return NULL;
}
const qa_compat_qc_function *qa_compat_qc_program_function_at(const qa_compat_qc_program *p, int64_t index) {
  if (index <= 0 || (uint64_t)index >= p->function_count) qa_compat_qc_program_error(p->source, "invalid function %lld", (long long)index);
  return &p->functions[index];
}
const qa_compat_qc_function *qa_compat_qc_program_function_named(const qa_compat_qc_program *p, const char *name) {
  const qa_compat_qc_function *fn = qa_compat_qc_program_function_named_map(p, name);
  if (!fn || fn->index == 0) qa_compat_qc_program_error(p->source, "missing function %s", name);
  return fn;
}
static int64_t crc16(const uint8_t *b, size_t n) {
  uint32_t crc = 0xffff;
  for (size_t i = 0; i < n; i++) { crc ^= (uint32_t)b[i] << 8; for (int bit = 0; bit < 8; bit++) crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff; }
  return (int64_t)crc;
}
static const char *const value_types[8] = {"void","string","float","vector","entity","field","function","pointer"};
void qa_compat_qc_program_destroy(qa_compat_qc_program *p) {
  if (!p) return;
  for (size_t i = 0; i < p->global_count; i++) free(p->globals[i].name);
  for (size_t i = 0; i < p->field_count; i++) free(p->fields[i].name);
  for (size_t i = 0; i < p->function_count; i++) { free(p->functions[i].name); free(p->functions[i].file); free(p->functions[i].parameter_sizes); }
  free(p->statements); free(p->globals); free(p->fields); free(p->functions);
  free(p->strings); free(p->initial_globals); free(p->source); free(p->digest); memset(p, 0, sizeof *p);
}
/* A throw abandons the partial program, which TS leaves to the GC. */
typedef struct {
  qa_binary_reader reader, secs[6]; qa_compat_qc_program *out;
  char *name, *file; uint8_t *sizes; int64_t *parameter_sizes;
} LoadScope;
static void load_release(LoadScope *l) {
  free(l->name); free(l->file); free(l->sizes); free(l->parameter_sizes);
  for (int i = 0; i < 6; i++) qa_binary_reader_destroy(&l->secs[i]);
  qa_binary_reader_destroy(&l->reader);
}
static void load_scope_free(void *arg) { LoadScope *l = arg; load_release(l); qa_compat_qc_program_destroy(l->out); }
static void *grown(void *items, size_t count, size_t *capacity, size_t size, size_t first) {
  if (count < *capacity) return items;
  size_t next = *capacity ? *capacity * 2 : first;
  void *p = realloc(items, next * size);
  if (!p) qa_range_error("QC program allocation failed");
  *capacity = next;
  return p;
}
bool qa_compat_qc_load_program(const uint8_t *bytes, size_t length, const qa_quakec_api_identity *expected, const char *source, qa_compat_qc_program *out) {
  memset(out, 0, sizeof *out);
  const char *src = source ? source : "progs.dat";
  LoadScope l; memset(&l, 0, sizeof l); l.out = out;
  if (!qa_binary_reader_init(&l.reader, (uint8_t *)bytes, length, src)) return false;
  qa_cleanup scope; qa_cleanup_push(&scope, load_scope_free, &l);
  int32_t version = qa_binary_reader_i32(&l.reader), crc = qa_binary_reader_i32(&l.reader);
  if (version != 6) qa_compat_qc_program_error(src, "unsupported program version %d", version);
  qa_quakec_api_identity api;
  if (crc == 5927) api = (qa_quakec_api_identity){QA_QUAKEC_API_Q1_NETQUAKE, 6, 5927};
  else if (crc == 54730) api = (qa_quakec_api_identity){QA_QUAKEC_API_Q1_QUAKEWORLD, 6, 54730};
  else qa_compat_qc_program_error(src, "unknown system layout CRC %d", crc);
  if (expected && expected->kind != api.kind)
    qa_compat_qc_program_error(src, "expected %s, found %s", expected->kind == QA_QUAKEC_API_Q1_QUAKEWORLD ? "q1-quakeworld" : "q1-netquake", api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD ? "q1-quakeworld" : "q1-netquake");
  static const int strides[6] = {8, 8, 8, 36, 1, 4};
  for (int i = 0; i < 6; i++) {
    int32_t off = qa_binary_reader_i32(&l.reader), cnt = qa_binary_reader_i32(&l.reader);
    if (cnt < 0) qa_compat_qc_program_error(src, "negative section count");
    if (!qa_binary_reader_records(&l.reader, off, (int64_t)cnt * strides[i], strides[i], &l.secs[i])) goto oom;
  }
  int32_t efw = qa_binary_reader_i32(&l.reader);
  if (efw <= 0 || efw > 65536) qa_compat_qc_program_error(src, "invalid entity field count %d", efw);
  out->string_length = qa_binary_reader_length(&l.secs[4]);
  out->strings = qa_binary_reader_bytes(&l.secs[4], (int64_t)out->string_length);
  if (!out->strings && out->string_length) goto oom;
  if (out->string_length == 0 || out->strings[0] != 0) qa_compat_qc_program_error(src, "string zero must be empty");
  out->initial_globals_length = qa_binary_reader_length(&l.secs[5]);
  out->initial_globals = qa_binary_reader_bytes(&l.secs[5], (int64_t)out->initial_globals_length);
  if (!out->initial_globals && out->initial_globals_length) goto oom;
  if (out->initial_globals_length < 28 * 4) qa_compat_qc_program_error(src, "missing reserved global words");
  out->source = xstrdup(src); if (!out->source) goto oom;
  out->api = api; out->entity_field_words = efw;
  /* definitions: secs[1]=globals, secs[2]=fields */
  for (int which = 0; which < 2; which++) {
    qa_binary_reader *in = &l.secs[which ? 2 : 1]; bool field = which == 1;
    qa_compat_qc_definition **arr = field ? &out->fields : &out->globals;
    size_t *count = field ? &out->field_count : &out->global_count, cap = 0;
    while (qa_binary_reader_remaining(in) > 0) {
      uint16_t raw = qa_binary_reader_u16(in), off = qa_binary_reader_u16(in);
      l.name = qa_compat_qc_byte_string(out->strings, out->string_length, qa_binary_reader_i32(in));
      if (!l.name) goto oom;
      const char *type = (raw & 0x7fff) < 8 ? value_types[raw & 0x7fff] : "opaque";
      if (field && (raw & 0x8000)) qa_compat_qc_program_error(src, "invalid definition %s", l.name);
      int64_t words = strcmp(type, "vector") == 0 ? 3 : 1;
      int64_t limit = field ? efw : (int64_t)(out->initial_globals_length / 4);
      if (off + words > limit) qa_compat_qc_program_error(src, "definition %s exceeds its memory", l.name);
      *arr = grown(*arr, *count, &cap, sizeof **arr, 16);
      (*arr)[(*count)++] = (qa_compat_qc_definition){type, raw & 0x7fff, (raw & 0x8000) != 0, off, l.name};
      l.name = NULL;
    }
  }
  { size_t cap = 0;
    while (qa_binary_reader_remaining(&l.secs[0]) > 0) {
      uint16_t op = qa_binary_reader_u16(&l.secs[0]);
      if (op > QA_QC_OP_BITOR) qa_compat_qc_program_error(src, "unsupported opcode %u", op);
      out->statements = grown(out->statements, out->statement_count, &cap, sizeof *out->statements, 64);
      /* Initializer lists are indeterminately sequenced in C: read in file order first. */
      uint16_t a = qa_binary_reader_u16(&l.secs[0]), b = qa_binary_reader_u16(&l.secs[0]), c = qa_binary_reader_u16(&l.secs[0]);
      out->statements[out->statement_count++] = (qa_compat_qc_statement){(qa_compat_qc_opcode)op, a, b, c};
    }
  }
  { size_t cap = 0; qa_binary_reader *in = &l.secs[3];
    while (qa_binary_reader_remaining(in) > 0) {
      int32_t first = qa_binary_reader_i32(in), pstart = qa_binary_reader_i32(in), locals = qa_binary_reader_i32(in);
      (void)qa_binary_reader_i32(in); /* Runtime profile counters start at zero. */
      l.name = qa_compat_qc_byte_string(out->strings, out->string_length, qa_binary_reader_i32(in));
      if (!l.name) goto oom;
      l.file = qa_compat_qc_byte_string(out->strings, out->string_length, qa_binary_reader_i32(in));
      if (!l.file) goto oom;
      int32_t count = qa_binary_reader_i32(in);
      l.sizes = qa_binary_reader_bytes(in, 8);
      if (!l.sizes) goto oom;
      if (count < 0 || count > 8 || locals < 0 || pstart < 0 || pstart + (int64_t)locals > (int64_t)(out->initial_globals_length / 4))
        qa_compat_qc_program_error(src, "invalid function locals/parameters for %s", l.name);
      if (count) { l.parameter_sizes = malloc((size_t)count * sizeof *l.parameter_sizes); if (!l.parameter_sizes) goto oom; }
      int64_t sum = 0;
      for (int32_t i = 0; i < count; i++) { if (first > 0 && l.sizes[i] != 1 && l.sizes[i] != 3) qa_compat_qc_program_error(src, "invalid parameter size for %s", l.name); l.parameter_sizes[i] = l.sizes[i]; sum += l.sizes[i]; }
      free(l.sizes); l.sizes = NULL;
      if (first >= (int64_t)out->statement_count || (first > 0 && pstart + sum > (int64_t)(out->initial_globals_length / 4)))
        qa_compat_qc_program_error(src, "invalid entry point for %s", l.name);
      size_t n = out->function_count;
      out->functions = grown(out->functions, n, &cap, sizeof *out->functions, 32);
      out->functions[n] = (qa_compat_qc_function){(int64_t)n, first, pstart, locals, l.name, l.file, l.parameter_sizes, (size_t)count, n > 0 && first == 0 && pstart == 0 && locals == 0};
      out->function_count = n + 1;
      l.name = NULL; l.file = NULL; l.parameter_sizes = NULL;
    }
  }
  if (out->function_count == 0 || out->statement_count == 0) qa_compat_qc_program_error(src, "empty function/statement table");
  out->checksum = crc16(bytes, length);
  { uint8_t dig[SHA256_DIGEST_LENGTH]; SHA256(bytes, length, dig); char hex[65];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) sprintf(hex + i * 2, "%02x", dig[i]);
    hex[64] = 0; out->digest = qa_create_content_digest(hex); if (!out->digest) goto oom; }
  qa_cleanup_pop(&scope);
  load_release(&l);
  return true;
oom:
  qa_cleanup_pop(&scope);
  load_scope_free(&l);
  return false;
}

int64_t qa_compat_qc_program_function_end(const qa_compat_qc_program *p, int64_t first_statement) {
  bool found = false;
  int64_t end = 0;
  for (size_t i = 0; i < p->function_count; i++) {
    const int64_t start = p->functions[i].first_statement;
    if (start > first_statement && (!found || start < end)) {
      end = start;
      found = true;
    }
  }
  return found ? end : (int64_t)p->statement_count;
}
