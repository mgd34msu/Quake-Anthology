/* Port of src/guest/runtime/common/format.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/common/format.h"

#include <ctype.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

void qa_guest_format_fortify_failure(const char *message) { qa_range_error("%s", message); }

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Guest runtime out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("Guest runtime out of memory");
  return next;
}

/* Byte strings (may contain NULs from %c). */
typedef struct bstr { char *data; size_t len, cap; } bstr;
static void bstr_init(bstr *s) { s->data = NULL; s->len = 0; s->cap = 0; }
static void bstr_reserve(bstr *s, size_t want) {
  if (want <= s->cap) return;
  size_t cap = s->cap == 0 ? 32 : s->cap;
  while (cap < want) cap *= 2;
  s->data = xrealloc(s->data, cap);
  s->cap = cap;
}
static void bstr_append(bstr *s, const char *bytes, size_t len) {
  bstr_reserve(s, s->len + len + 1);
  memcpy(s->data + s->len, bytes, len);
  s->len += len;
  s->data[s->len] = '\0';
}
static void bstr_append_str(bstr *s, const char *text) { bstr_append(s, text, strlen(text)); }
static void bstr_free(bstr *s) { free(s->data); bstr_init(s); }

/* Internal FormatFailure / BufferFull throws. */
typedef struct format_throw {
  jmp_buf buf;
  int kind; /* 1 failure, 2 full */
  int errno_value;
} format_throw;
static format_throw *g_throw;
static void format_failure(int errno_value) {
  if (g_throw == NULL) qa_range_error("Invalid guest printf conversion");
  g_throw->kind = 1;
  g_throw->errno_value = errno_value;
  longjmp(g_throw->buf, 1);
}
static void buffer_full(void) {
  if (g_throw == NULL) qa_range_error("Guest printf buffer full");
  g_throw->kind = 2;
  longjmp(g_throw->buf, 1);
}

typedef struct reference { int index; qa_format_argument_type type; } reference;
typedef struct amount { bool is_argument; int64_t value; reference ref; } amount;
typedef struct conversion {
  char code;
  char length[4];
  char flags[16];
  amount width;
  bool has_precision;
  amount precision;
  bool has_argument;
  reference argument;
} conversion;
typedef struct token {
  bool is_literal;
  bstr text;
  conversion conv;
} token;
typedef struct parsed_format {
  token *tokens; size_t token_count, token_cap;
  bool positional;
  reference *references; size_t reference_count, reference_cap;
} parsed_format;

static int integer_bits(const char *length, const qa_guest_format_request *request) {
  if (strcmp(length, "hh") == 0) return 8;
  if (strcmp(length, "h") == 0) return 16;
  if (strcmp(length, "ll") == 0 || strcmp(length, "j") == 0 || strcmp(length, "I64") == 0 || strcmp(length, "q") == 0) return 64;
  if (strcmp(length, "L") == 0) return request->dialect == QA_FORMAT_SYSTEM_V ? 64 : 32;
  if (strcmp(length, "l") == 0) return request->dialect == QA_FORMAT_WINDOWS ? 32 : request->memory->pointer_bytes * 8;
  if (strcmp(length, "z") == 0 || strcmp(length, "t") == 0 || strcmp(length, "I") == 0)
    return request->memory->pointer_bytes * 8;
  return 32;
}

typedef struct parser {
  const char *text;
  size_t length, at;
  parsed_format *parsed;
  const qa_guest_format_request *request;
  int sequence;
  bool positional, sequential;
} parser;

static void push_reference(parser *p, reference ref) {
  parsed_format *parsed = p->parsed;
  if (parsed->reference_count == parsed->reference_cap) {
    parsed->reference_cap = parsed->reference_cap == 0 ? 8 : parsed->reference_cap * 2;
    parsed->references = xrealloc(parsed->references, parsed->reference_cap * sizeof *parsed->references);
  }
  parsed->references[parsed->reference_count++] = ref;
}
static void push_token(parser *p, token tok) {
  parsed_format *parsed = p->parsed;
  if (parsed->token_count == parsed->token_cap) {
    parsed->token_cap = parsed->token_cap == 0 ? 8 : parsed->token_cap * 2;
    parsed->tokens = xrealloc(parsed->tokens, parsed->token_cap * sizeof *parsed->tokens);
  }
  parsed->tokens[parsed->token_count++] = tok;
}
static int64_t parse_number(parser *p) {
  int64_t value = 0;
  while (p->at < p->length && isdigit((unsigned char)p->text[p->at])) {
    value = value * 10 + (p->text[p->at] - '0');
    p->at++;
    if (value > 0x7fffffffLL)
      format_failure(p->request->dialect == QA_FORMAT_WINDOWS ? 132 : 75);
  }
  return value;
}
static bool parse_position(parser *p, int *out) {
  size_t start = p->at;
  int64_t value = parse_number(p);
  if (p->at >= p->length || p->text[p->at] != '$') { p->at = start; return false; }
  if (p->request->dialect == QA_FORMAT_WINDOWS || value < 1 || value > 4096) format_failure(22);
  p->at++;
  *out = (int)value - 1;
  return true;
}
static reference make_reference(parser *p, qa_format_argument_type type, bool has_index, int index) {
  if (!has_index) p->sequential = true; else p->positional = true;
  if (p->positional && p->sequential) format_failure(22);
  reference ref = {has_index ? index : p->sequence++, type};
  push_reference(p, ref);
  return ref;
}
static amount parse_amount(parser *p) {
  amount result; memset(&result, 0, sizeof result);
  if (p->at >= p->length || p->text[p->at] != '*') {
    result.value = parse_number(p);
    return result;
  }
  p->at++;
  int index = 0;
  bool has_index = parse_position(p, &index);
  result.is_argument = true;
  result.ref = make_reference(p, QA_FORMAT_ARG_INT, has_index, index);
  return result;
}

static void parse_format(parsed_format *parsed, const char *text, const qa_guest_format_request *request) {
  memset(parsed, 0, sizeof *parsed);
  parser p; memset(&p, 0, sizeof p);
  p.text = text; p.length = strlen(text); p.parsed = parsed; p.request = request;
  static const char *const candidates[] = {"I64", "I32", "hh", "ll", "h", "l", "L", "j", "z", "t", "I", "w", "q"};
  while (p.at < p.length) {
    size_t start = p.at;
    while (p.at < p.length && p.text[p.at] != '%') p.at++;
    if (p.at > start) {
      token tok; memset(&tok, 0, sizeof tok);
      tok.is_literal = true;
      bstr_init(&tok.text);
      bstr_append(&tok.text, p.text + start, p.at - start);
      push_token(&p, tok);
    }
    if (p.at == p.length) break;
    p.at++;
    if (p.at < p.length && p.text[p.at] == '%') {
      token tok; memset(&tok, 0, sizeof tok);
      tok.is_literal = true;
      bstr_init(&tok.text);
      bstr_append(&tok.text, "%", 1);
      push_token(&p, tok);
      p.at++;
      continue;
    }
    int position = 0;
    bool has_position = parse_position(&p, &position);
    conversion conv; memset(&conv, 0, sizeof conv);
    size_t flag_len = 0;
    while (p.at < p.length && strchr("-+ #0'", p.text[p.at]) != NULL) {
      if (flag_len < sizeof conv.flags - 1) conv.flags[flag_len++] = p.text[p.at];
      p.at++;
    }
    conv.flags[flag_len] = '\0';
    conv.width = parse_amount(&p);
    if (p.at < p.length && p.text[p.at] == '.') {
      p.at++;
      conv.has_precision = true;
      conv.precision = parse_amount(&p);
    }
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++) {
      size_t len = strlen(candidates[i]);
      if (p.at + len <= p.length && strncmp(p.text + p.at, candidates[i], len) == 0) {
        memcpy(conv.length, candidates[i], len + 1);
        p.at += len;
        break;
      }
    }
    char code = p.at < p.length ? p.text[p.at++] : '\0';
    if (code == '\0' || strchr("diouxXfFeEgGaAcCsSpn", code) == NULL) format_failure(22);
    if (request->dialect == QA_FORMAT_SYSTEM_V
        && (strcmp(conv.length, "I") == 0 || strcmp(conv.length, "I32") == 0
            || strcmp(conv.length, "I64") == 0 || strcmp(conv.length, "w") == 0))
      format_failure(22);
    qa_format_argument_type type;
    if (strchr("fFeEgGaA", code) != NULL)
      type = strcmp(conv.length, "L") == 0 ? QA_FORMAT_ARG_LONG_DOUBLE : QA_FORMAT_ARG_DOUBLE;
    else if (strchr("sSpn", code) != NULL) type = QA_FORMAT_ARG_POINTER;
    else if (strchr("cC", code) != NULL) type = QA_FORMAT_ARG_INT;
    else type = integer_bits(conv.length, request) == 64 ? QA_FORMAT_ARG_INT64 : QA_FORMAT_ARG_INT;
    conv.code = code;
    conv.has_argument = true;
    conv.argument = make_reference(&p, type, has_position, position);
    token tok; memset(&tok, 0, sizeof tok);
    tok.conv = conv;
    push_token(&p, tok);
  }
  parsed->positional = p.positional;
}
static void parsed_format_free(parsed_format *parsed) {
  for (size_t i = 0; i < parsed->token_count; i++) bstr_free(&parsed->tokens[i].text);
  free(parsed->tokens);
  free(parsed->references);
  memset(parsed, 0, sizeof *parsed);
}

typedef struct output {
  const qa_guest_format_request *request;
  int64_t total, used;
} output;

static void output_check_total(output *out) {
  if (out->total > 0x7fffffffLL)
    format_failure(out->request->dialect == QA_FORMAT_WINDOWS ? 132 : 75);
}
static void output_append(output *out, const char *text, size_t length) {
  if (length == 0) return;
  const qa_guest_format_request *r = out->request;
  int64_t available = !r->has_buffer ? 0 : (r->capacity > out->used ? r->capacity - out->used : 0);
  size_t copied = (size_t)available < length ? (size_t)available : length;
  if (r->has_buffer && copied > 0) {
    qa_memory_write(r->memory, qa_memory_offset(r->memory, r->buffer, out->used),
                    (const uint8_t *)text, copied);
    out->used += (int64_t)copied;
  }
  if (r->has_buffer && r->termination != QA_TERMINATION_C99 && !r->continue_count && copied < length)
    buffer_full();
  out->total += (int64_t)length;
  output_check_total(out);
}
static void output_repeat(output *out, char character, int64_t count) {
  if (count <= 0) return;
  const qa_guest_format_request *r = out->request;
  char chunk[4096];
  memset(chunk, character, sizeof chunk);
  if (r->termination == QA_TERMINATION_C99 || r->continue_count || !r->has_buffer) {
    int64_t available = !r->has_buffer ? 0 : r->capacity - out->used;
    if (available < 0) available = 0;
    int64_t copied = count < available ? count : available;
    for (int64_t at = 0; at < copied; at += 4096) {
      int64_t piece = copied - at < 4096 ? copied - at : 4096;
      output_append(out, chunk, (size_t)piece);
    }
    out->total += count - copied;
    output_check_total(out);
  } else {
    for (int64_t at = 0; at < count; at += 4096) {
      int64_t piece = count - at < 4096 ? count - at : 4096;
      output_append(out, chunk, (size_t)piece);
    }
  }
}
static qa_guest_format_result output_finish(output *out, bool has_error, int error, bool full) {
  const qa_guest_format_request *r = out->request;
  qa_guest_format_result result;
  result.has_errno = has_error;
  result.errno_value = error;
  if (!r->has_buffer) {
    result.result = !has_error ? (int)out->total : -1;
    return result;
  }
  int64_t capacity = r->capacity;
  if (r->termination == QA_TERMINATION_UCRT_LEGACY) {
    if (out->used < capacity)
      qa_memory_write_u8(r->memory, qa_memory_offset(r->memory, r->buffer, out->used), 0);
    result.result = (has_error || full || out->total > capacity) ? -1 : (int)out->total;
    return result;
  }
  if (capacity > 0) {
    int64_t at = (has_error && r->dialect == QA_FORMAT_WINDOWS && r->termination == QA_TERMINATION_C99) ? 0
        : out->used >= capacity ? capacity - 1 : out->used;
    qa_memory_write_u8(r->memory, qa_memory_offset(r->memory, r->buffer, at), 0);
  }
  if (r->termination == QA_TERMINATION_UCRT && (capacity == 0 || out->used == capacity)) {
    result.result = capacity == 0 ? -1 : -2;
    return result;
  }
  result.result = !has_error ? (int)out->total : -1;
  return result;
}

static uint64_t argument_integer(const qa_format_argument *value) {
  if (value->is_float) format_failure(22);
  return value->integer;
}
static int64_t as_int_n(uint64_t value, int bits) {
  if (bits >= 64) return (int64_t)value;
  uint64_t sign = 1ULL << (bits - 1);
  return (int64_t)(((value & ((sign << 1) - 1)) ^ sign) - sign);
}
static uint64_t as_uint_n(uint64_t value, int bits) {
  return bits >= 64 ? value : value & ((1ULL << bits) - 1);
}

static int cmp_mapping(const void *a, const void *b) {
  uint64_t x = ((const qa_guest_mapping *)a)->base, y = ((const qa_guest_mapping *)b)->base;
  return x < y ? -1 : x > y ? 1 : 0;
}
static bool readonly_format(const qa_guest_format_request *request, size_t length) {
  if (!request->has_format) return false;
  size_t count = 0;
  const qa_guest_mapping *live = qa_gmem_mappings(request->memory_ext, &count);
  if (live == NULL) return false;
  qa_guest_mapping *sorted = xmalloc(count * sizeof *sorted);
  memcpy(sorted, live, count * sizeof *sorted);
  qsort(sorted, count, sizeof *sorted, cmp_mapping);
  uint64_t cursor = request->format.byte_offset;
  uint64_t end = cursor + length + 1;
  bool result = false;
  for (size_t i = 0; i < count; i++) {
    uint64_t stop = sorted[i].base + sorted[i].byte_length;
    if (stop <= cursor) continue;
    if (sorted[i].base > cursor || qa_guest_permissions_has_write(sorted[i].permissions)) break;
    cursor = stop;
    if (cursor >= end) { result = true; break; }
  }
  free(sorted);
  return result;
}

static void string_argument(bstr *out_text, const qa_guest_format_request *request,
                            bool has_address, qa_guest_core_address address,
                            bool wide, bool has_precision, int64_t precision) {
  bstr_init(out_text);
  if (!has_address) {
    if (has_precision && request->dialect == QA_FORMAT_SYSTEM_V && precision < 6) return;
    size_t take = has_precision ? (size_t)precision : 6;
    if (take > 6) take = 6;
    bstr_append(out_text, "(null)", take);
    return;
  }
  qa_mapped_guest_memory *m = request->memory;
  int width = wide ? (request->dialect == QA_FORMAT_WINDOWS ? 2 : 4) : 1;
  if (!wide && m->managed_sparse && request->memory_ext != NULL && request->memory_ext->find_zero != NULL) {
    if (has_precision && precision == 0) return;
    size_t maximum = has_precision ? (size_t)precision : 1048576;
    int64_t length = request->memory_ext->find_zero(request->memory_ext->ctx, address, maximum);
    if (length < 0 && !has_precision) qa_range_error("Guest printf string exceeds runtime string limit");
    size_t take = length < 0 ? (has_precision ? (size_t)precision : 0) : (size_t)length;
    bstr_reserve(out_text, take + 1);
    out_text->data[0] = '\0';
    if (take > 0) {
      qa_memory_copy(m, address, (uint8_t *)out_text->data, take);
      out_text->len = take;
      out_text->data[take] = '\0';
    }
    return;
  }
  for (int64_t index = 0; !has_precision || (int64_t)out_text->len < precision; index++) {
    if (index >= 1048576) qa_range_error("Guest printf string exceeds runtime string limit");
    qa_guest_core_address at = qa_memory_offset(m, address, index * width);
    uint32_t code = width == 4 ? qa_memory_read_u32(m, at) : width == 2 ? qa_memory_read_u16(m, at)
                                                                        : qa_memory_read_u8(m, at);
    if (code == 0) break;
    if (wide && code > (uint32_t)(request->dialect == QA_FORMAT_WINDOWS ? 255 : 127))
      format_failure(request->dialect == QA_FORMAT_WINDOWS ? 42 : 84);
    char ch = (char)code;
    bstr_append(out_text, &ch, 1);
  }
}

static void uint_to_radix(bstr *out_text, uint64_t value, int radix, bool upper) {
  char reversed[64];
  size_t n = 0;
  if (value == 0) reversed[n++] = '0';
  while (value != 0) {
    unsigned digit = (unsigned)(value % (uint64_t)radix);
    reversed[n++] = (char)(digit < 10 ? '0' + (int)digit : (upper ? 'A' : 'a') + (int)digit - 10);
    value /= (uint64_t)radix;
  }
  bstr_init(out_text);
  for (size_t i = n; i-- > 0;) bstr_append(out_text, &reversed[i], 1);
}

typedef struct format_run {
  const qa_guest_format_request *request;
  qa_format_arguments *reader;
  bool positional;
  qa_format_argument *values;
  size_t value_count;
} format_run;

static qa_format_argument get_argument(format_run *run, reference ref) {
  if (!run->positional) {
    if (run->reader == NULL) qa_range_error("Missing printf argument reader");
    return qa_format_arguments_next(run->reader, ref.type);
  }
  if (ref.index < 0 || (size_t)ref.index >= run->value_count) format_failure(22);
  qa_format_argument value = run->values[ref.index];
  /* Borrow: clear ownership in the stored copy so frees stay single. The
   * caller of get_argument does not free positional values. */
  return value;
}
static int64_t amount_value(format_run *run, amount value) {
  if (!value.is_argument) return value.value;
  qa_format_argument argument = get_argument(run, value.ref);
  int64_t result = as_int_n(argument_integer(&argument), 32);
  if (!run->positional) qa_format_argument_free(&argument);
  return result;
}

static qa_guest_format_result format_run_body(const qa_guest_format_request *request, output *out) {
  char *text = qa_read_string(request->memory, request->memory_ext, request->format, 1048576);
  parsed_format parsed;
  parse_format(&parsed, text, request);
  qa_format_arguments reader;
  bool has_reader = parsed.reference_count > 0;
  if (has_reader)
    qa_format_arguments_init(&reader, request->memory, request->dialect,
                             request->has_arguments, request->arguments);
  format_run run; memset(&run, 0, sizeof run);
  run.request = request;
  run.reader = has_reader ? &reader : NULL;
  run.positional = parsed.positional;
  if (parsed.positional) {
    /* Collect types by index; require contiguity from 0. */
    size_t max_index = 0;
    for (size_t i = 0; i < parsed.reference_count; i++)
      if (parsed.references[i].index + 1 > (int)max_index) max_index = (size_t)parsed.references[i].index + 1;
    qa_format_argument_type *types = xmalloc(max_index * sizeof *types);
    bool *seen = xmalloc(max_index * sizeof *seen);
    memset(seen, 0, max_index * sizeof *seen);
    for (size_t i = 0; i < parsed.reference_count; i++) {
      reference ref = parsed.references[i];
      if (seen[ref.index] && types[ref.index] != ref.type) format_failure(22);
      seen[ref.index] = true;
      types[ref.index] = ref.type;
    }
    size_t type_count = 0;
    for (size_t i = 0; i < max_index; i++) if (seen[i]) type_count++;
    run.values = xmalloc(type_count * sizeof *run.values);
    run.value_count = type_count;
    for (size_t index = 0; index < type_count; index++) {
      if (index >= max_index || !seen[index] || !has_reader) format_failure(22);
      run.values[index] = qa_format_arguments_next(&reader, types[index]);
    }
    free(types); free(seen);
  }

  qa_guest_format_result final;
  for (size_t ti = 0; ti < parsed.token_count; ti++) {
    token *tok = &parsed.tokens[ti];
    if (tok->is_literal) { output_append(out, tok->text.data, tok->text.len); continue; }
    conversion *conv = &tok->conv;
    int64_t width = amount_value(&run, conv->width);
    bool has_precision = conv->has_precision;
    int64_t precision = has_precision ? amount_value(&run, conv->precision) : 0;
    bool left = strchr(conv->flags, '-') != NULL || width < 0;
    if (width < 0) width = -width;
    if (has_precision && precision < 0) has_precision = false;
    if (has_precision && precision > 1048576)
      qa_range_error("Guest printf precision exceeds runtime conversion limit");
    bool alternate = strchr(conv->flags, '#') != NULL;
    bool plus = strchr(conv->flags, '+') != NULL;
    bool blank = strchr(conv->flags, ' ') != NULL;
    if (!conv->has_argument) qa_range_error("Missing printf conversion argument");
    qa_format_argument value = get_argument(&run, conv->argument);
    char code = conv->code;
    bstr body, prefix;
    bstr_init(&body); bstr_init(&prefix);
    bool zero = strchr(conv->flags, '0') != NULL && !left;
    bool skip_output = false;
    if (strchr("diouxX", code) != NULL) {
      bool signed_ = code == 'd' || code == 'i';
      int bits = integer_bits(conv->length, request);
      int64_t number = signed_ ? as_int_n(argument_integer(&value), bits) : 0;
      uint64_t magnitude = signed_ ? (number < 0 ? (uint64_t)(-(number + 1)) + 1 : (uint64_t)number)
                                   : as_uint_n(argument_integer(&value), bits);
      int radix = code == 'o' ? 8 : (code == 'x' || code == 'X') ? 16 : 10;
      if (!(has_precision && precision == 0 && magnitude == 0))
        uint_to_radix(&body, magnitude, radix, code == 'X');
      int64_t wanted = has_precision ? precision : 1;
      if ((int64_t)body.len < wanted) {
        bstr padded; bstr_init(&padded);
        for (int64_t i = 0; i < wanted - (int64_t)body.len; i++) bstr_append(&padded, "0", 1);
        bstr_append(&padded, body.data, body.len);
        bstr_free(&body);
        body = padded;
      }
      if (signed_) {
        if (number < 0) bstr_append_str(&prefix, "-");
        else if (plus) bstr_append_str(&prefix, "+");
        else if (blank) bstr_append_str(&prefix, " ");
      } else if (alternate && radix == 16 && magnitude != 0) {
        bstr_append_str(&prefix, code == 'X' ? "0X" : "0x");
      } else if (alternate && radix == 8 && !(body.len > 0 && body.data[0] == '0')) {
        bstr_append_str(&prefix, "0");
      }
      if (has_precision) zero = false;
    } else if (strchr("fFeEgGaA", code) != NULL) {
      if (!value.is_float) format_failure(22);
      char *formatted = qa_format_float(&value.float_value, code, has_precision, (int)precision,
                                        alternate, request->rounding,
                                        request->dialect == QA_FORMAT_WINDOWS,
                                        strcmp(conv->length, "L") == 0 && request->dialect == QA_FORMAT_SYSTEM_V,
                                        request->exponent_digits);
      if (formatted == NULL) qa_range_error("Guest runtime out of memory");
      bstr_append_str(&body, formatted);
      free(formatted);
      if (value.float_value.sign == 1) bstr_append_str(&prefix, "-");
      else if (plus) bstr_append_str(&prefix, "+");
      else if (blank) bstr_append_str(&prefix, " ");
      if (value.float_value.kind != QA_BINARY_FINITE) zero = false;
      if (body.len >= 2 && body.data[0] == '0' && (body.data[1] == 'x' || body.data[1] == 'X')) {
        bstr_append(&prefix, body.data, 2);
        memmove(body.data, body.data + 2, body.len - 2);
        body.len -= 2;
        body.data[body.len] = '\0';
      }
    } else if (code == 'p') {
      uint64_t pointer = as_uint_n(argument_integer(&value), request->memory->pointer_bytes * 8);
      if (request->dialect == QA_FORMAT_WINDOWS) {
        uint_to_radix(&body, pointer, 16, true);
        int64_t wanted = request->memory->pointer_bytes * 2;
        if ((int64_t)body.len < wanted) {
          bstr padded; bstr_init(&padded);
          for (int64_t i = 0; i < wanted - (int64_t)body.len; i++) bstr_append(&padded, "0", 1);
          bstr_append(&padded, body.data, body.len);
          bstr_free(&body); body = padded;
        }
      } else if (pointer == 0) {
        bstr_append_str(&body, "(nil)");
        zero = false;
      } else {
        bstr_append_str(&prefix, "0x");
        uint_to_radix(&body, pointer, 16, false);
        int64_t wanted = has_precision ? precision : 1;
        if ((int64_t)body.len < wanted) {
          bstr padded; bstr_init(&padded);
          for (int64_t i = 0; i < wanted - (int64_t)body.len; i++) bstr_append(&padded, "0", 1);
          bstr_append(&padded, body.data, body.len);
          bstr_free(&body); body = padded;
        }
      }
    } else if (code == 's' || code == 'S') {
      bool wide = strcmp(conv->length, "l") == 0 || strcmp(conv->length, "w") == 0
          || (code == 'S' && strcmp(conv->length, "h") != 0);
      qa_guest_core_address address = {0};
      uint64_t raw = argument_integer(&value);
      bool has_address = qa_memory_pointer(request->memory, raw, &address);
      bstr_free(&body);
      string_argument(&body, request, has_address, address, wide, has_precision, precision);
      zero = false;
    } else if (code == 'c' || code == 'C') {
      bool wide = strcmp(conv->length, "l") == 0 || strcmp(conv->length, "w") == 0
          || (code == 'C' && strcmp(conv->length, "h") != 0);
      int bits = wide ? (request->dialect == QA_FORMAT_WINDOWS ? 16 : 32) : 8;
      uint64_t number = as_uint_n(argument_integer(&value), bits);
      if (wide && number > (uint64_t)(request->dialect == QA_FORMAT_WINDOWS ? 255 : 127))
        format_failure(request->dialect == QA_FORMAT_WINDOWS ? 42 : 84);
      char ch = (char)number;
      bstr_append(&body, &ch, 1);
      zero = false;
    } else if (code == 'n') {
      if (request->dialect == QA_FORMAT_WINDOWS) format_failure(22);
      if (request->fortify && !readonly_format(request, strlen(text)))
        qa_guest_format_fortify_failure("glibc %n in writable format storage");
      qa_guest_core_address address = {0};
      if (!qa_memory_pointer(request->memory, argument_integer(&value), &address))
        qa_range_error("Null printf count pointer");
      qa_write_unsigned(request->memory, address, integer_bits(conv->length, request) / 8,
                        (uint64_t)out->total);
      skip_output = true;
    }
    if (!run.positional) qa_format_argument_free(&value);
    if (skip_output) { bstr_free(&body); bstr_free(&prefix); continue; }
    int64_t padding = width - (int64_t)prefix.len - (int64_t)body.len;
    if (padding < 0) padding = 0;
    if (!left && !zero) output_repeat(out, ' ', padding);
    output_append(out, prefix.data, prefix.len);
    if (zero) output_repeat(out, '0', padding);
    output_append(out, body.data, body.len);
    if (left) output_repeat(out, ' ', padding);
    bstr_free(&body);
    bstr_free(&prefix);
  }
  final = output_finish(out, false, 0, false);
  for (size_t i = 0; i < run.value_count; i++) qa_format_argument_free(&run.values[i]);
  free(run.values);
  parsed_format_free(&parsed);
  free(text);
  return final;
}

qa_guest_format_result qa_format_guest_buffer(const qa_guest_format_request *request) {
  int64_t limit = request->memory->pointer_bytes == 4 ? 0xffffffffLL : INT64_MAX;
  if (request->capacity < 0 || request->capacity > limit)
    qa_range_error("Guest printf buffer count is not size_t");
  if (!request->has_format || (!request->has_buffer && request->capacity != 0)) {
    qa_guest_format_result result = {-1, true, 22};
    return result;
  }
  output out; memset(&out, 0, sizeof out);
  out.request = request;
  format_throw catcher; memset(&catcher, 0, sizeof catcher);
  format_throw *previous = g_throw;
  g_throw = &catcher;
  if (setjmp(catcher.buf) != 0) {
    g_throw = previous;
    if (catcher.kind == 2) return output_finish(&out, false, 0, true);
    return output_finish(&out, true, catcher.errno_value, false);
  }
  qa_guest_format_result result = format_run_body(request, &out);
  g_throw = previous;
  return result;
}
