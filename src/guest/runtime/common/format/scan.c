/* Port of src/guest/runtime/common/format/scan.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/common/format/scan.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/floating_point/binary.h"
#include "qa/guest/runtime/common/format/arguments.h"
#include "qa/guest/runtime/common/memory.h"

static qa_guest_scan_catch *g_catch;
static void unsupported(const char *format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  if (g_catch == NULL) qa_range_error("%s", buffer);
  (void)snprintf(g_catch->message, sizeof g_catch->message, "%s", buffer);
  longjmp(g_catch->buf, 1);
}
static void type_error(const char *message) { qa_range_error("%s", message); }
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

typedef enum directive_kind { DIR_SPACE, DIR_LITERAL, DIR_NUMBER } directive_kind;
typedef struct directive {
  directive_kind kind;
  char value; /* literal */
  char code; /* d|i|f */
  int64_t width;
  bool suppressed;
  int bytes; /* 4|8 */
} directive;

static bool is_space_char(char c) {
  return c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r' || c == ' ';
}

static directive *parse_directives(const char *format, size_t *out_count) {
  directive *result = NULL; size_t count = 0, cap = 0;
  size_t length = strlen(format);
#define PUSH(d) do { \
    if (count == cap) { cap = cap == 0 ? 8 : cap * 2; result = xrealloc(result, cap * sizeof *result); } \
    result[count++] = (d); \
  } while (0)
  for (size_t index = 0; index < length;) {
    char value = format[index++];
    if (is_space_char(value)) { directive d = {DIR_SPACE, 0, 0, 0, false, 0}; PUSH(d); continue; }
    if (value != '%') { directive d = {DIR_LITERAL, value, 0, 0, false, 0}; PUSH(d); continue; }
    if (index < length && format[index] == '%') {
      index++;
      directive d = {DIR_LITERAL, '%', 0, 0, false, 0}; PUSH(d);
      continue;
    }
    bool suppressed = index < length && format[index] == '*';
    if (suppressed) index++;
    int64_t width = 0;
    bool has_digits = false;
    while (index < length && isdigit((unsigned char)format[index])) {
      has_digits = true;
      width = width * 10 + (format[index++] - '0');
      if (width > QA_MAX_SAFE_INTEGER) unsupported("invalid numeric scanf field width");
    }
    if (!has_digits) width = QA_MAX_SAFE_INTEGER;
    if (width == 0) unsupported("invalid numeric scanf field width");
    bool is_long = index < length && format[index] == 'l';
    if (is_long) index++;
    char code = index < length ? format[index++] : '\0';
    directive d; memset(&d, 0, sizeof d);
    d.kind = DIR_NUMBER; d.width = width; d.suppressed = suppressed;
    if (code != '\0' && strchr("fFeEgG", code) != NULL) {
      d.code = 'f'; d.bytes = is_long ? 8 : 4; PUSH(d);
    } else if ((code == 'd' || code == 'i') && !is_long) {
      d.code = code; d.bytes = 4; PUSH(d);
    } else {
      unsupported("unsupported scanf conversion in %s", format);
    }
  }
#undef PUSH
  *out_count = count;
  return result;
}

static void bigint_from_decimal(qa_bigint *out, const char *digits) {
  qa_bigint_init(out);
  qa_bigint ten; qa_bigint_init(&ten); qa_bigint_set_u64(&ten, 10);
  for (const char *p = digits; *p != '\0'; p++) {
    qa_bigint product; qa_bigint_init(&product);
    qa_bigint_mul(&product, out, &ten);
    qa_bigint digit; qa_bigint_init(&digit); qa_bigint_set_u64(&digit, (uint64_t)(*p - '0'));
    qa_bigint sum; qa_bigint_init(&sum);
    qa_bigint_add(&sum, &product, &digit);
    qa_bigint_free(&product); qa_bigint_free(&digit); qa_bigint_free(out);
    *out = sum;
  }
  qa_bigint_free(&ten);
}

static void encode_value_bytes(uint8_t *out, const qa_binary_value *value, int bytes) {
  qa_bigint bits; qa_bigint_init(&bits);
  qa_fp_encode_binary(&bits, value, bytes == 4 ? 32 : 64);
  qa_fp_write_bits(out, &bits, (size_t)bytes);
  qa_bigint_free(&bits);
}

static void decimal_bytes(const char *token, int bytes, uint8_t *out) {
  int sign = token[0] == '-' ? 1 : 0;
  const char *begin = token + ((token[0] == '+' || token[0] == '-') ? 1 : 0);
  /* Split on e/E (TS lowercases first; digits are unaffected). */
  const char *epos = NULL;
  for (const char *p = begin; *p != '\0'; p++)
    if (*p == 'e' || *p == 'E') { epos = p; break; }
  size_t mantissa_len = epos != NULL ? (size_t)(epos - begin) : strlen(begin);
  char *mantissa = xmalloc(mantissa_len + 1);
  memcpy(mantissa, begin, mantissa_len);
  mantissa[mantissa_len] = '\0';
  const char *dot = strchr(mantissa, '.');
  char *digits = xmalloc(mantissa_len + 1);
  size_t digit_len = 0;
  for (size_t i = 0; i < mantissa_len; i++)
    if (mantissa[i] != '.') digits[digit_len++] = mantissa[i];
  digits[digit_len] = '\0';
  while (digit_len > 0 && digits[0] == '0') { memmove(digits, digits + 1, digit_len); digit_len--; }
  long exponent_part = epos != NULL ? strtol(epos + 1, NULL, 10) : 0;
  long after_dot = dot == NULL ? 0 : (long)(mantissa_len - (size_t)(dot - mantissa) - 1);
  long exponent = exponent_part - after_dot;
  free(mantissa);
  if (digit_len == 0) {
    qa_binary_value zero = qa_fp_zero(sign);
    encode_value_bytes(out, &zero, bytes);
    qa_binary_value_free(&zero);
    free(digits);
    return;
  }
  if (exponent + (long)digit_len > 400) {
    qa_binary_value infinity; memset(&infinity, 0, sizeof infinity);
    infinity.kind = QA_BINARY_INFINITY; infinity.sign = sign;
    qa_bigint_init(&infinity.coefficient); qa_bigint_init(&infinity.payload);
    encode_value_bytes(out, &infinity, bytes);
    qa_binary_value_free(&infinity);
    free(digits);
    return;
  }
  if (exponent + (long)digit_len < -400) {
    qa_binary_value zero = qa_fp_zero(sign);
    encode_value_bytes(out, &zero, bytes);
    qa_binary_value_free(&zero);
    free(digits);
    return;
  }
  qa_bigint coefficient;
  bigint_from_decimal(&coefficient, digits);
  free(digits);
  qa_bigint power; qa_bigint_init(&power);
  qa_bigint_pow_u32(&power, 10, (uint32_t)(exponent < 0 ? -exponent : exponent));
  qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
  qa_bigint scaled; qa_bigint_init(&scaled);
  const qa_bigint *numerator, *denominator;
  if (exponent >= 0) {
    qa_bigint_mul(&scaled, &coefficient, &power);
    numerator = &scaled; denominator = &one;
  } else {
    numerator = &coefficient; denominator = &power;
  }
  qa_binary_result rounded;
  qa_fp_round_rational(&rounded, sign, numerator, denominator, 0,
                       qa_fp_format_for(bytes == 4 ? 32 : 64), QA_FP_NEAREST);
  encode_value_bytes(out, &rounded.value, bytes);
  qa_binary_result_free(&rounded);
  qa_bigint_free(&coefficient); qa_bigint_free(&power);
  qa_bigint_free(&one); qa_bigint_free(&scaled);
}

typedef struct scan_state {
  const qa_scan_windows_buffer_options *options;
  size_t cursor;
  int assigned;
  bool converted;
} scan_state;

static int peek_byte(scan_state *state) {
  if ((int64_t)state->cursor >= state->options->capacity) return -1;
  uint8_t byte = qa_memory_read_u8(state->options->memory,
      qa_memory_offset(state->options->memory, state->options->input, (int64_t)state->cursor));
  return byte == 0 ? -1 : byte;
}
static bool peek_is_space(scan_state *state) {
  int byte = peek_byte(state);
  return byte >= 0 && is_space_char((char)byte);
}
static int failure(scan_state *state) {
  return !state->converted && peek_byte(state) < 0 ? -1 : state->assigned;
}

/* Token validation mirroring the TS regexes. */
static bool valid_float_token(const char *token) {
  const char *p = token;
  if (*p == '+' || *p == '-') p++;
  bool digits_before = false, digits_after = false;
  const char *q = p;
  while (isdigit((unsigned char)*q)) { digits_before = true; q++; }
  if (*q == '.') { q++; while (isdigit((unsigned char)*q)) { digits_after = true; q++; } }
  if (!digits_before && !digits_after) return false;
  if (*q == 'e' || *q == 'E') {
    q++;
    if (*q == '+' || *q == '-') q++;
    if (!isdigit((unsigned char)*q)) return false;
    while (isdigit((unsigned char)*q)) q++;
  }
  return *q == '\0';
}
static bool valid_int_token(const char *token) {
  const char *p = token;
  if (*p == '+' || *p == '-') p++;
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    if (!isxdigit((unsigned char)p[2])) return false;
    for (const char *q = p + 2; *q != '\0'; q++) if (!isxdigit((unsigned char)*q)) return false;
    return true;
  }
  if (!isdigit((unsigned char)*p)) return false;
  for (const char *q = p; *q != '\0'; q++) if (!isdigit((unsigned char)*q)) return false;
  return true;
}

static int scan_run(const qa_scan_windows_buffer_options *options) {
  qa_mapped_guest_memory *memory = options->memory;
  char *format = qa_read_string(memory, options->memory_ext, options->format, 1048576);
  size_t directive_count = 0;
  directive *directives = parse_directives(format, &directive_count);
  qa_format_arguments args;
  qa_format_arguments_init(&args, memory, QA_FORMAT_WINDOWS, options->has_arguments, options->arguments);
  scan_state state; memset(&state, 0, sizeof state);
  state.options = options;
  int result = 0;
  bool done = false;
  for (size_t di = 0; di < directive_count && !done; di++) {
    const directive *dir = &directives[di];
    if (dir->kind == DIR_SPACE) {
      while (peek_is_space(&state)) state.cursor++;
      continue;
    }
    if (dir->kind == DIR_LITERAL) {
      if (peek_byte(&state) != (uint8_t)dir->value) { result = failure(&state); done = true; break; }
      state.cursor++;
      continue;
    }
    while (peek_is_space(&state)) state.cursor++;
    if (peek_byte(&state) < 0) { result = failure(&state); done = true; break; }
    size_t start = state.cursor;
    size_t token_cap = 64, token_len = 0;
    char *token = xmalloc(token_cap);
#define CURRENT() (((int64_t)(state.cursor - start) < dir->width) ? peek_byte(&state) : -1)
#define TAKE() do { \
      int b_ = peek_byte(&state); \
      if (token_len + 2 > token_cap) { token_cap *= 2; token = xrealloc(token, token_cap); } \
      token[token_len++] = (char)(b_ < 0 ? 0 : b_); token[token_len] = '\0'; \
      state.cursor++; \
    } while (0)
    token[0] = '\0';
    if (CURRENT() == '+' || CURRENT() == '-') TAKE();
    bool valid = true;
    if (dir->code == 'f') {
      int current = CURRENT();
      if (current == 'i' || current == 'I' || current == 'n' || current == 'N') {
        while (isalpha(CURRENT()) && token_len < 9) TAKE();
        const char *body = token + ((token[0] == '+' || token[0] == '-') ? 1 : 0);
        bool nonfinite = false;
        if (strlen(body) >= 3) {
          char lower[4] = {(char)tolower((unsigned char)body[0]), (char)tolower((unsigned char)body[1]),
                           (char)tolower((unsigned char)body[2]), '\0'};
          nonfinite = strcmp(lower, "inf") == 0 || strcmp(lower, "nan") == 0;
        }
        if (nonfinite) unsupported("scanf nonfinite text is not implemented");
        result = state.assigned; done = true;
        free(token);
        break;
      }
      while (isdigit(CURRENT())) TAKE();
      if ((strcmp(token, "0") == 0 || strcmp(token, "+0") == 0 || strcmp(token, "-0") == 0)
          && (CURRENT() == 'x' || CURRENT() == 'X'))
        unsupported("scanf hexadecimal floating input is not implemented");
      if (CURRENT() == '.') { TAKE(); while (isdigit(CURRENT())) TAKE(); }
      if (CURRENT() == 'e' || CURRENT() == 'E') {
        TAKE();
        if (CURRENT() == '+' || CURRENT() == '-') TAKE();
        while (isdigit(CURRENT())) TAKE();
      }
      valid = valid_float_token(token);
    } else {
      int base = 10;
      if (dir->code == 'i' && CURRENT() == '0') {
        TAKE(); base = 8;
        if (CURRENT() == 'x' || CURRENT() == 'X') { TAKE(); base = 16; }
      }
      for (;;) {
        int current = CURRENT();
        bool is_digit = base == 16 ? isxdigit(current) : base == 8
            ? (current >= '0' && current <= '7') : isdigit(current);
        if (!is_digit) break;
        TAKE();
      }
      valid = valid_int_token(token);
    }
    if (!valid) { result = state.assigned; done = true; free(token); break; }
    state.converted = true;
    if (!dir->suppressed) {
      qa_format_argument argument = qa_format_arguments_next(&args, QA_FORMAT_ARG_POINTER);
      if (argument.is_float) type_error("scanf destination must be a pointer");
      qa_guest_address destination = {0};
      if (!qa_memory_pointer(memory, argument.integer, &destination))
        type_error("scanf destination is null");
      if (dir->code == 'f') {
        uint8_t bytes[8];
        decimal_bytes(token, dir->bytes, bytes);
        qa_memory_write(memory, destination, bytes, (size_t)dir->bytes);
      } else {
        bool negative = token[0] == '-';
        const char *body = token + ((token[0] == '+' || token[0] == '-') ? 1 : 0);
        uint64_t value;
        if (dir->code == 'i' && body[0] == '0' && body[1] != '\0' && body[1] != 'x' && body[1] != 'X') {
          bool all_octal = true;
          for (const char *p = body; *p != '\0'; p++) if (*p < '0' || *p > '7') all_octal = false;
          value = all_octal ? strtoull(body, NULL, 8) : strtoull(body, NULL, 10);
        } else if (body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) {
          value = strtoull(body, NULL, 16);
        } else {
          value = strtoull(body, NULL, 10);
        }
        qa_write_unsigned(memory, destination, 4, negative ? (uint64_t)(-(int64_t)value) : value);
      }
      state.assigned++;
    }
    free(token);
#undef CURRENT
#undef TAKE
  }
  if (!done) result = state.assigned;
  free(directives);
  free(format);
  return result;
}

int qa_scan_windows_buffer(const qa_scan_windows_buffer_options *options, qa_guest_scan_catch *catch_) {
  qa_guest_scan_catch *previous = g_catch;
  g_catch = catch_;
  if (catch_ != NULL && setjmp(catch_->buf) != 0) {
    g_catch = previous;
    return -2; /* unsupported: message in catch_->message; caller rethrows */
  }
  int result = scan_run(options);
  g_catch = previous;
  return result;
}
