/* Port of src/compat/q2/native-primary-reader.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_reader.h"
#include <stdlib.h>
#include <string.h>
static const qa_save_reader *fld(const qa_save_reader *r, const char *n, qa_save_reader *o) { qa_save_reader_field(r, n, o); return o; }
double qa_compat_q2_native_offset(const qa_save_reader *reader) {
  double v = qa_save_reader_integer_min(reader, 0);
  if (v > 4294967295.0) qa_save_reader_fail(reader, "native offset exceeds PE address space");
  return v;
}
qa_compat_q2_native_region_span qa_compat_q2_native_region(const qa_save_reader *reader) {
  qa_save_reader e, j; double entry = qa_compat_q2_native_offset(fld(reader, "entry", &e));
  double join = qa_compat_q2_native_offset(fld(reader, "join", &j));
  qa_save_reader_destroy(&e); qa_save_reader_destroy(&j);
  if (entry == join) qa_save_reader_fail(reader, "source region must contain instructions");
  qa_compat_q2_native_region_span s = {entry, join}; return s;
}
qa_native_mod_scalar qa_compat_q2_native_scalar(const qa_save_reader *reader) {
  static const char *const c[] = {"int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
  return qa_save_reader_choice_string(reader, c, 10);
}
const char *qa_compat_q2_native_register(const qa_save_reader *reader) {
  static const char *const c[] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15"};
  return qa_save_reader_choice_string(reader, c, 16);
}
qa_native_item_field qa_compat_q2_native_field(const qa_save_reader *reader) {
  static const char *const rec[] = {"entity","client","image"};
  qa_save_reader a, b, c; qa_native_item_field f;
  f.record = qa_save_reader_choice_string(fld(reader, "record", &a), rec, 3);
  f.field.offset = qa_compat_q2_native_offset(fld(reader, "offset", &b));
  f.field.encoding = qa_compat_q2_native_scalar(fld(reader, "encoding", &c));
  qa_save_reader_destroy(&a); qa_save_reader_destroy(&b); qa_save_reader_destroy(&c); return f;
}
bool qa_compat_q2_native_test(const qa_save_reader *reader, qa_native_item_test *out, qa_native_mod_address *value_storage) {
  static const char *const kinds[] = {"scalar","pointer"};
  static const char *const rec[] = {"entity","client","image"};
  static const char *const cmp[] = {"equals","at-most"};
  memset(out, 0, sizeof *out);
  qa_save_reader k; const char *kind = qa_save_reader_choice_string(fld(reader, "kind", &k), kinds, 2); qa_save_reader_destroy(&k);
  out->kind = kind;
  if (strcmp(kind, "pointer") == 0) {
    qa_save_reader f, fr, fo, v;
    fld(reader, "field", &f);
    out->pointer_field.record = qa_save_reader_choice_string(fld(&f, "record", &fr), rec, 3);
    out->pointer_field.offset = qa_compat_q2_native_offset(fld(&f, "offset", &fo));
    qa_save_reader_destroy(&fr); qa_save_reader_destroy(&fo); qa_save_reader_destroy(&f);
    fld(reader, "value", &v);
    if (qa_save_reader_is_null(&v)) { out->pointer_value = NULL; qa_save_reader_destroy(&v); return true; }
    qa_save_reader rv, iv; value_storage->rva = qa_compat_q2_native_offset(fld(&v, "rva", &rv)); qa_save_reader_destroy(&rv);
    fld(&v, "indirections", &iv); size_t n = qa_save_reader_list_count(&iv);
    double *arr = NULL;
    if (n) { arr = malloc(n * sizeof *arr); if (!arr) { qa_save_reader_destroy(&iv); qa_save_reader_destroy(&v); return false; }
      for (size_t i = 0; i < n; i++) { qa_save_reader it; qa_save_reader_item(&iv, i, &it); arr[i] = qa_compat_q2_native_offset(&it); qa_save_reader_destroy(&it); } }
    qa_save_reader_destroy(&iv); qa_save_reader_destroy(&v);
    value_storage->indirections.values = arr; value_storage->indirections.count = n; out->pointer_value = value_storage; return true;
  }
  qa_save_reader f, m, c, v; fld(reader, "field", &f); out->field = qa_compat_q2_native_field(&f); qa_save_reader_destroy(&f);
  fld(reader, "mask", &m); out->has_mask = !qa_save_reader_is_null(&m); out->mask = out->has_mask ? qa_compat_q2_native_offset(&m) : 0; qa_save_reader_destroy(&m);
  out->comparison = qa_save_reader_choice_string(fld(reader, "comparison", &c), cmp, 2); qa_save_reader_destroy(&c);
  out->scalar_value = qa_save_reader_finite(fld(reader, "value", &v)); qa_save_reader_destroy(&v); return true;
}
qa_native_abi qa_compat_q2_native_abi(const qa_save_reader *reader, qa_native_abi expected) {
  qa_save_reader a, b, c, d;
  qa_save_reader_literal_string(fld(reader, "kind", &a), expected.kind);
  qa_save_reader_literal_string(fld(reader, "image", &b), expected.image);
  qa_save_reader_literal_string(fld(reader, "call", &c), expected.call);
  qa_save_reader_literal_number(fld(reader, "pointerBytes", &d), (double)expected.pointer_bytes);
  qa_save_reader_destroy(&a); qa_save_reader_destroy(&b); qa_save_reader_destroy(&c); qa_save_reader_destroy(&d); return expected;
}
static qa_guest_value_layout sig_value(const qa_save_reader *reader) {
  qa_save_reader k, s; qa_save_reader_literal_string(fld(reader, "kind", &k), "scalar"); qa_save_reader_destroy(&k);
  fld(reader, "storage", &s); qa_guest_value_layout v; memset(&v, 0, sizeof v);
  const char *st = (!qa_save_reader_is_undefined(&s) && s.value && s.value->kind == QA_SAVE_STRING) ? s.value->text : NULL;
  if (st && strcmp(st, "pointer") == 0) v.storage = "pointer";
  else v.storage = qa_compat_q2_native_scalar(&s);
  qa_save_reader_destroy(&s); return v;
}
bool qa_compat_q2_native_signature(const qa_save_reader *reader, qa_native_abi abi, qa_compat_q2_guest_call_signature *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader a, p, r, v;
  qa_native_abi got = qa_compat_q2_native_abi(fld(reader, "abi", &a), abi); qa_save_reader_destroy(&a);
  out->abi = got;
  fld(reader, "parameters", &p); size_t n = qa_save_reader_list_count(&p);
  qa_guest_value_layout *params = NULL;
  if (n) { params = malloc(n * sizeof *params); if (!params) { qa_save_reader_destroy(&p); return false; }
    for (size_t i = 0; i < n; i++) { qa_save_reader it; qa_save_reader_item(&p, i, &it); params[i] = sig_value(&it); qa_save_reader_destroy(&it); } }
  qa_save_reader_destroy(&p); out->parameters = params; out->parameter_count = n;
  fld(reader, "result", &r);
  if (r.value && r.value->kind == QA_SAVE_STRING && strcmp(r.value->text, "void") == 0) out->result_is_void = true;
  else { out->result_is_void = false; out->result = sig_value(&r); }
  qa_save_reader_destroy(&r);
  qa_save_reader_literal_boolean(fld(reader, "variadic", &v), false); qa_save_reader_destroy(&v); out->variadic = false; return true;
}
