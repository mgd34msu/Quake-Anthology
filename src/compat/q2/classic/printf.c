/* Port of src/compat/q2/classic/printf.ts. GPL-2.0-or-later.
 * Float text follows the JS toFixed/toExponential/toPrecision shapes
 * the TS builds (shortest round-trip via snprintf %g-family staged
 * through float/double as in TS). */
#include "qa/compat/q2/classic/printf.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/foundation/js_parse_extra.h"
#include "qa/foundation/js.h"

typedef struct { size_t start, end; char flags[16]; bool width_star; double width_num; bool has_prec, prec_star; double prec_num; char size[4]; char type; } conv_t;
static const char *const sizes[] = {"hh", "h", "ll", "l", "I64", "I32", "I", "L", "j", "z", "t", "w"};
/* TS wide(entry): 64-bit integer conversions. */
static bool wide(const conv_t *c) {
  return strchr("diuoxX", c->type) != NULL && (strcmp(c->size, "ll") == 0 || strcmp(c->size, "I64") == 0 || strcmp(c->size, "j") == 0);
}
static bool parse_conversions(const char *format, conv_t *out, size_t cap, size_t *total) {
  size_t n = 0, len = strlen(format);
  for (size_t i = 0; i < len; i++) {
    if (format[i] != '%') continue;
    size_t j = i + 1; conv_t c; memset(&c, 0, sizeof c); c.start = i; c.width_num = 0; c.prec_num = 0;
    size_t fi = 0;
    while (j < len && strchr("-+ #0", format[j])) { if (fi < 15) c.flags[fi++] = format[j]; j++; }
    c.flags[fi] = 0;
    if (j < len && format[j] == '*') { c.width_star = true; j++; }
    else { double w = 0; bool any = false; while (j < len && isdigit((unsigned char)format[j])) { w = w * 10 + (format[j] - '0'); j++; any = true; } c.width_num = any ? w : 0; }
    if (j < len && format[j] == '.') {
      c.has_prec = true; j++;
      if (j < len && format[j] == '*') { c.prec_star = true; j++; }
      else { double p = 0; bool any = false; while (j < len && isdigit((unsigned char)format[j])) { p = p * 10 + (format[j] - '0'); j++; any = true; } c.prec_num = any ? p : 0; }
    }
    /* (hh|h|ll|l|I64|I32|I|L|j|z|t|w)?: the first alternative followed by a conversion type wins, else none. */
    c.size[0] = 0;
    for (size_t k = 0; k < sizeof sizes / sizeof *sizes; k++) {
      size_t n2 = strlen(sizes[k]);
      if (j + n2 < len && strncmp(format + j, sizes[k], n2) == 0 && strchr("%diuoxXcspfeEgG", format[j + n2]) != NULL) {
        memcpy(c.size, sizes[k], n2 + 1);
        j += n2;
        break;
      }
    }
    /* The CRT prints what it cannot convert, so a stray or unsupported specifier is ordinary text.
     * TS matches within format.slice(index, index + 64). */
    if (j >= len || !strchr("%diuoxXcspfeEgG", format[j]) || j + 1 - i > 64) continue;
    c.type = format[j]; c.end = j + 1; i = j;
    if (out && n < cap) out[n] = c;
    n++;
  }
  *total = n; return true;
}
size_t qa_compat_q2_classic_printf_layouts(const char *format, qa_guest_value_layout *out, size_t capacity) {
  conv_t conv[256]; size_t total = 0; parse_conversions(format, conv, 256, &total);
  if (total > 256) qa_range_error("Unsupported API 3 printf conversion at %s", format);
  size_t n = 0;
  for (size_t i = 0; i < total; i++) {
    if (conv[i].type == '%') continue;
    qa_guest_value_layout items[3]; size_t m = 0;
    if (conv[i].width_star) items[m++] = qa_compat_q2_q2_int;
    if (conv[i].prec_star) items[m++] = qa_compat_q2_q2_int;
    static const qa_guest_value_layout q2_int64 = {false, "int64", {0}};
    items[m++] = strchr("feEgG", conv[i].type) ? qa_compat_q2_q2_double : strchr("sp", conv[i].type) ? qa_compat_q2_q2_pointer : wide(&conv[i]) ? q2_int64 : qa_compat_q2_q2_int;
    for (size_t k = 0; k < m; k++) { if (out && n < capacity) out[n] = items[k]; n++; }
  }
  return n;
}
typedef struct { char *buf; size_t len, cap; } sbuf;
static void sb_put(sbuf *s, const char *t, size_t n) {
  if (s->len + n + 1 > s->cap) { s->cap = (s->len + n + 1) * 2; s->buf = realloc(s->buf, s->cap); if (!s->buf) qa_range_error("API 3 printf out of memory"); }
  memcpy(s->buf + s->len, t, n); s->len += n; s->buf[s->len] = 0;
}
static void sb_str(sbuf *s, const char *t) { sb_put(s, t, strlen(t)); }
/* value.toFixed / toExponential / toPrecision: JS rounds exact decimal ties up and
 * prints NaN/Infinity and an unpadded exponent, where "%.*f"/"%.*e" rounded ties to even
 * and printed "nan"/"inf". The caller pads the exponent and uppercases, as the TS does. */
static char *js_fixed(double v, int prec) {
  char *b = malloc(512);
  if (b == NULL) qa_range_error("API 3 printf out of memory");
  qa_js_to_fixed(v, prec, b, 512);
  return b;
}
static char *js_exp(double v, int prec, char type) {
  (void)type;
  char *b = malloc(512);
  if (b == NULL) qa_range_error("API 3 printf out of memory");
  qa_js_to_exponential(v, prec, b, 512);
  return b;
}
static char *js_precision(double v, int prec) {
  char *b = malloc(512);
  if (b == NULL) qa_range_error("API 3 printf out of memory");
  qa_js_to_precision(v, prec, b, 512);
  return b;
}
/* JS /e([+-])(\d)$/ -> e$10$2 (pad single-digit exponent). */
static void fix_exp(char *t) {
  size_t n = strlen(t);
  if (n >= 3 && (t[n - 3] == 'e' || t[n - 3] == 'E') && (t[n - 2] == '+' || t[n - 2] == '-') && isdigit((unsigned char)t[n - 1])) {
    memmove(t + n - 1, t + n - 2, 2); t[n - 2] = '0'; t[n + 1] = 0;
  }
}
char *qa_compat_q2_classic_printf(const qa_compat_q2_memory *memory, const char *format, const qa_guest_call_value *arguments, size_t count) {
  conv_t conv[256]; size_t total = 0; parse_conversions(format, conv, 256, &total);
  if (total > 256) qa_range_error("Unsupported API 3 printf conversion at %s", format);
  sbuf out = {0}; size_t next = 0, position = 0;
  for (size_t ci = 0; ci < total; ci++) {
    conv_t *e = &conv[ci];
    sb_put(&out, format + position, e->start - position); position = e->end;
    if (e->type == '%') { sb_str(&out, "%"); continue; }
    /* value()/integer() helpers inline */
    double width, prec_in; bool has_prec;
    if (e->width_star) {
      if (next >= count) qa_range_error("API 3 printf argument missing");
      const qa_guest_call_value *it = &arguments[next++];
      if (it->kind != QA_GUEST_VALUE_INT32 && it->kind != QA_GUEST_VALUE_UINT32) qa_range_error("API 3 printf integer required");
      width = it->number_value;
    } else width = e->width_num;
    if (e->prec_star) {
      if (next >= count) qa_range_error("API 3 printf argument missing");
      const qa_guest_call_value *it = &arguments[next++];
      if (it->kind != QA_GUEST_VALUE_INT32 && it->kind != QA_GUEST_VALUE_UINT32) qa_range_error("API 3 printf integer required");
      prec_in = it->number_value; has_prec = true;
    } else if (e->has_prec) { prec_in = e->prec_num; has_prec = true; } else { prec_in = 0; has_prec = false; }
    bool has_precision = has_prec && prec_in >= 0; double precision = prec_in;
    if (fabs(width) > 65536 || (has_precision && precision > 100)) qa_range_error("API 3 printf width or precision exceeds supported buffer");
    if (next >= count) qa_range_error("API 3 printf argument missing");
    const qa_guest_call_value *item = &arguments[next++];
    char *text = NULL;
    if (e->type == 's' || e->type == 'p') {
      if (item->kind != QA_GUEST_VALUE_POINTER) qa_range_error("API 3 printf pointer required");
      if (e->type == 's') {
        char *full = qa_compat_q2_read_classic_string(memory, item->pointer_value.has_value, item->pointer_value.value, 65536);
        if (has_precision && strlen(full) > (size_t)precision) full[(size_t)precision] = 0;
        text = full;
      } else { text = malloc(32); snprintf(text, 32, "%08llx", (unsigned long long)(item->pointer_value.has_value ? item->pointer_value.value.byte_offset : 0)); }
    } else if (strchr("feEgG", e->type)) {
      if (item->kind != QA_GUEST_VALUE_FLOAT64) qa_range_error("API 3 printf promoted double required");
      double v = item->number_value; int p = has_precision ? (int)precision : 6;
      if (e->type == 'f' || e->type == 'F') text = js_fixed(v, p);
      else if (e->type == 'e' || e->type == 'E') text = js_exp(v, p, e->type);
      else {
        text = js_precision(v, p < 1 ? 1 : p);
        /* strip trailing zeros in fraction: /(\.\d*?)0+(e|$)/ and /\.(e|$)/ */
        char *dot = strchr(text, '.');
        if (dot) {
          char *end = text + strlen(text); char *ex = strchr(text, 'e'); if (!ex) ex = strchr(text, 'E'); char *stop = ex ? ex : end;
          char *q = stop;
          while (q > dot + 1 && q[-1] == '0') q--;
          if (q == dot + 1 && q[-1] == '.') q--;
          size_t kept = (size_t)(q - text), rest = strlen(stop);
          memmove(text + kept, stop, rest + 1);
        }
      }
      if (v == 0 && signbit(v) && text[0] != '-') { size_t L = strlen(text); memmove(text + 1, text, L + 1); text[0] = '-'; }
      fix_exp(text);
      if (e->type == 'F' || e->type == 'E' || e->type == 'G') for (char *p2 = text; *p2; p2++) *p2 = (char)toupper((unsigned char)*p2);
    } else {
      const int bits = wide(e) ? 64 : strcmp(e->size, "h") == 0 ? 16 : strcmp(e->size, "hh") == 0 ? 8 : 32;
      int64_t sv; uint64_t uv;
      if (bits == 64) {
        if (item->kind != QA_GUEST_VALUE_INT64 && item->kind != QA_GUEST_VALUE_UINT64) qa_range_error("API 3 printf 64-bit integer required");
        uv = item->kind == QA_GUEST_VALUE_INT64 ? (uint64_t)item->int64_value : item->uint64_value;
        sv = (int64_t)uv;
      } else {
        if (item->kind != QA_GUEST_VALUE_INT32 && item->kind != QA_GUEST_VALUE_UINT32) qa_range_error("API 3 printf integer required");
        /* BigInt.asIntN/asUintN(bits, BigInt(value)) */
        const uint64_t raw = (uint64_t)(int64_t)item->number_value;
        const uint64_t mask = bits == 32 ? 0xffffffffull : bits == 16 ? 0xffffull : 0xffull;
        uv = raw & mask;
        sv = (uv & ((mask >> 1) + 1)) != 0 ? (int64_t)uv - (int64_t)mask - 1 : (int64_t)uv;
      }
      text = malloc(64);
      if (e->type == 'c') { text[0] = (char)(uv & 255); text[1] = 0; }
      else if (e->type == 'd' || e->type == 'i') snprintf(text, 64, "%lld", (long long)sv);
      else if (e->type == 'o') snprintf(text, 64, "%llo", (unsigned long long)uv);
      else if (e->type == 'u') snprintf(text, 64, "%llu", (unsigned long long)uv);
      else snprintf(text, 64, "%llx", (unsigned long long)uv);
      if (e->type == 'X') for (char *p2 = text; *p2; p2++) *p2 = (char)toupper((unsigned char)*p2);
      if (has_precision && precision == 0 && uv == 0 && e->type != 'c') strcpy(text, (e->type == 'o' && strchr(e->flags, '#')) ? "0" : "");
      if (has_precision && e->type != 'c') {
        /* padStart(precision,"0") preserving sign */
        bool neg = text[0] == '-'; const char *digits = text + (neg ? 1 : 0); size_t dl = strlen(digits);
        if ((double)dl < precision) {
          char *nb = malloc((size_t)precision + 2); size_t pos = 0;
          if (neg) nb[pos++] = '-';
          for (size_t k = dl; k < (size_t)precision; k++) nb[pos++] = '0';
          strcpy(nb + pos, digits); free(text); text = nb;
        }
      }
      if (strchr(e->flags, '#') && uv != 0) {
        const char *pre = e->type == 'o' ? "0" : e->type == 'x' ? "0x" : e->type == 'X' ? "0X" : "";
        if (pre[0]) { char *nb = malloc(strlen(text) + 3); strcpy(nb, pre); strcat(nb, text); free(text); text = nb; }
      }
    }
    if (strchr("difeEgG", e->type) && text[0] != '-') {
      char sign = strchr(e->flags, '+') ? '+' : strchr(e->flags, ' ') ? ' ' : 0;
      if (sign) { size_t L = strlen(text); char *nb = malloc(L + 2); nb[0] = sign; strcpy(nb + 1, text); free(text); text = nb; }
    }
    /* width padding */
    bool left = width < 0 || strchr(e->flags, '-') != NULL;
    char fill = (!left && strchr(e->flags, '0') && !has_precision && e->type != 's' && e->type != 'c') ? '0' : ' ';
    double aw = fabs(width); size_t tl = strlen(text);
    if ((double)tl < aw) {
      size_t want = (size_t)aw; char *nb = malloc(want + 1);
      if (left) { memcpy(nb, text, tl); memset(nb + tl, ' ', want - tl); nb[want] = 0; }
      else if (fill == '0' && (text[0] == '+' || text[0] == '-')) { nb[0] = text[0]; memset(nb + 1, '0', want - tl); memcpy(nb + 1 + (want - tl), text + 1, tl); nb[want] = 0; }
      else if (fill == '0' && tl >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { nb[0] = '0'; nb[1] = text[1]; memset(nb + 2, '0', want - tl); memcpy(nb + 2 + (want - tl), text + 2, tl - 1); nb[want] = 0; }
      else { memset(nb, fill, want - tl); memcpy(nb + (want - tl), text, tl + 1); }
      free(text); text = nb;
    }
    sb_str(&out, text); free(text);
  }
  sb_str(&out, format + position);
  if (!out.buf) { out.buf = malloc(1); out.buf[0] = 0; }
  return out.buf;
}
