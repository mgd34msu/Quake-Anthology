/* Port of src/compat/q2/rerelease/imports.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/imports.h"
#include "qa/compat/q2/guest_console.h"
#include "qa/compat/q2/guest_strings.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/foundation/js.h"

static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Q2 rerelease imports out of memory"); return p; }
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = xmalloc(n); memcpy(p, s, n); return p; }
static void fmt_number(char *out, size_t cap, double v) {
  qa_net_js_number(v, out, cap);
}

char *qa_compat_q2_read_guest_string(const qa_compat_q2_memory *memory, qa_guest_address address, double maximum) {
  double limit = maximum < 0 ? 1048576.0 : maximum;
  if (memory->managed && memory->find_zero) {
    int64_t length = memory->find_zero(memory->ctx, address, limit);
    if (length < 0) qa_range_error("Q2 guest string has no terminator within its source limit");
    char *out = xmalloc((size_t)length + 1);
    if (memory->copy_into) memory->copy_into(memory->ctx, address, (uint8_t *)out, (size_t)length, (double)length);
    else memory->copy(memory->ctx, address, (uint8_t *)out, (double)length);
    out[length] = 0;
    return out;
  }
  size_t max = limit > 0 ? (size_t)limit : 0;
  char *out = xmalloc(max + 1);
  for (size_t len = 0; len < max; len++) {
    uint8_t byte = qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)len));
    if (byte == 0) {
      if (memory->copy_into) memory->copy_into(memory->ctx, address, (uint8_t *)out, len, (double)len);
      else memory->copy(memory->ctx, address, (uint8_t *)out, (double)len);
      out[len] = 0;
      return out;
    }
  }
  free(out);
  qa_range_error("Q2 guest string has no terminator within its source limit");
}

typedef struct { int64_t key; qa_guest_address address; double size, tag; } allocation_t;
typedef struct { char *key; qa_guest_address address; } string_entry_t;
typedef struct {
  char *name; qa_guest_address address;
  qa_compat_q2_reusable_guest_string text, latched_text;
  qa_guest_address f_string, f_latched, f_flags, f_modified, f_value, f_integer;
} guest_cvar_t;
struct qa_compat_q2_rerelease_core_imports {
  qa_compat_q2_memory *memory;
  qa_compat_q2_rerelease_core_services services;
  allocation_t *allocs; size_t alloc_count, alloc_cap;
  string_entry_t *strings; size_t string_count, string_cap;
  guest_cvar_t **cvars; size_t cvar_count, cvar_cap;
  bool has_last_cvar; qa_guest_address last_cvar;
  qa_compat_q2_command_guest_strings *command_strings; /* TS #commandStrings */
};
qa_compat_q2_rerelease_core_imports *qa_compat_q2_rerelease_core_imports_create(qa_compat_q2_memory *memory,
                                                                              qa_compat_q2_rerelease_core_services services) {
  qa_compat_q2_rerelease_core_imports *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->memory = memory; c->services = services;
  c->command_strings = qa_compat_q2_command_guest_strings_create(memory, "Q2 command text");
  return c;
}
void qa_compat_q2_rerelease_core_imports_destroy(qa_compat_q2_rerelease_core_imports *c) {
  if (!c) return;
  for (size_t i = 0; i < c->string_count; i++) free(c->strings[i].key);
  for (size_t i = 0; i < c->cvar_count; i++) { free(c->cvars[i]->name); qa_compat_q2_reusable_guest_string_destroy(&c->cvars[i]->text); qa_compat_q2_reusable_guest_string_destroy(&c->cvars[i]->latched_text); free(c->cvars[i]); }
  if (c->command_strings) qa_compat_q2_command_guest_strings_destroy(c->command_strings);
  free(c->allocs); free(c->strings); free(c->cvars); free(c);
}
qa_compat_q2_memory *qa_compat_q2_rerelease_core_imports_get_memory(const qa_compat_q2_rerelease_core_imports *c) { return c->memory; }
qa_guest_address qa_compat_q2_rerelease_core_imports_string(qa_compat_q2_rerelease_core_imports *c, const char *text) {
  for (size_t i = 0; i < c->string_count; i++) if (strcmp(c->strings[i].key, text) == 0) return c->strings[i].address;
  size_t len = strlen(text);
  qa_guest_address address = qa_compat_q2_allocate_native_memory(c->memory, (double)len + 1, "Q2 engine string");
  c->memory->write(c->memory->ctx, address, (const uint8_t *)text, len);
  if (c->string_count == c->string_cap) {
    c->string_cap = c->string_cap ? c->string_cap * 2 : 16;
    c->strings = realloc(c->strings, c->string_cap * sizeof *c->strings);
    if (!c->strings) qa_range_error("Q2 rerelease imports out of memory");
  }
  c->strings[c->string_count++] = (string_entry_t){xstrdup(text), address};
  return address;
}
static void write_cvar(qa_compat_q2_rerelease_core_imports *c, guest_cvar_t *record, const qa_cvar_snapshot *value) {
  const qa_compat_q2_memory *m = c->memory;
  /* Private cvars (rcon passwords) exist for the guest but never reveal their contents. */
  bool hidden = (value->flags & QA_Q2_CVAR_FLAG_PRIVATE) != 0;
  qa_compat_q2_mem_write_pointer(m, record->f_string, true, qa_compat_q2_reusable_guest_string_set_text(&record->text, hidden ? "" : value->value));
  if (value->latched_value == NULL || hidden) qa_compat_q2_mem_write_pointer(m, record->f_latched, false, record->address);
  else qa_compat_q2_mem_write_pointer(m, record->f_latched, true, qa_compat_q2_reusable_guest_string_set_text(&record->latched_text, value->latched_value));
  qa_compat_q2_mem_write_u32(m, record->f_flags, qa_js_to_uint32(value->flags));
  double modified = value->modification_count;
  qa_compat_q2_mem_write_i32(m, record->f_modified, qa_js_to_int32((modified == 0 || isnan(modified) ? 1 : modified)));
  qa_compat_q2_mem_write_f32(m, record->f_value, hidden ? 0 : value->numeric_value);
  qa_compat_q2_mem_write_i32(m, record->f_integer, hidden ? 0 : qa_js_to_int32(value->integer_value));
}
void qa_compat_q2_rerelease_core_imports_refresh_cvars(qa_compat_q2_rerelease_core_imports *c) {
  for (size_t i = 0; i < c->cvar_count; i++) {
    qa_cvar_snapshot value; memset(&value, 0, sizeof value);
    if (qa_cvar_registry_get(c->services.cvars, c->cvars[i]->name, &value)) {
      write_cvar(c, c->cvars[i], &value);
      qa_cvar_read_free(&value);
    }
  }
}
bool qa_compat_q2_rerelease_core_imports_cvar(qa_compat_q2_rerelease_core_imports *c, const qa_cvar_snapshot *value, qa_guest_address *out) {
  if (!value) return false;
  qa_compat_q2_rerelease_layouts_ensure();
  guest_cvar_t *record = NULL;
  for (size_t i = 0; i < c->cvar_count; i++) if (strcmp(c->cvars[i]->name, value->name) == 0) record = c->cvars[i];
  if (!record) {
    char label[512]; snprintf(label, sizeof label, "Q2 cvar %s", value->name);
    qa_guest_address address = c->memory->allocate(c->memory->ctx, qa_compat_q2_rerelease_cvar_layout.byte_length, true, 8, label);
    const qa_guest_layout *layout = &qa_compat_q2_rerelease_cvar_layout;
    if (c->cvar_count == c->cvar_cap) {
      c->cvar_cap = c->cvar_cap ? c->cvar_cap * 2 : 16;
      c->cvars = realloc(c->cvars, c->cvar_cap * sizeof *c->cvars);
      if (!c->cvars) qa_range_error("Q2 rerelease imports out of memory");
    }
    record = calloc(1, sizeof *record); if (!record) qa_range_error("Q2 rerelease imports out of memory");
    c->cvars[c->cvar_count++] = record;
    record->name = xstrdup(value->name); record->address = address;
    char value_label[512], latched_label[512];
    snprintf(value_label, sizeof value_label, "Q2 cvar %s value", value->name); snprintf(latched_label, sizeof latched_label, "Q2 cvar %s latched", value->name);
    qa_compat_q2_reusable_guest_string_init(&record->text, c->memory, value_label);
    qa_compat_q2_reusable_guest_string_init(&record->latched_text, c->memory, latched_label);
    record->f_string = qa_compat_q2_mem_offset(c->memory, address, (int64_t)qa_compat_q2_rerelease_field_offset(layout, "string"));
    record->f_latched = qa_compat_q2_mem_offset(c->memory, address, (int64_t)qa_compat_q2_rerelease_field_offset(layout, "latched_string"));
    record->f_flags = qa_compat_q2_mem_offset(c->memory, address, (int64_t)qa_compat_q2_rerelease_field_offset(layout, "flags"));
    record->f_modified = qa_compat_q2_mem_offset(c->memory, address, (int64_t)qa_compat_q2_rerelease_field_offset(layout, "modified_count"));
    record->f_value = qa_compat_q2_mem_offset(c->memory, address, (int64_t)qa_compat_q2_rerelease_field_offset(layout, "value"));
    record->f_integer = qa_compat_q2_mem_offset(c->memory, address, (int64_t)qa_compat_q2_rerelease_field_offset(layout, "integer"));
    qa_compat_q2_mem_write_pointer(c->memory, address, true, qa_compat_q2_rerelease_core_imports_string(c, value->name));
    qa_guest_address next = qa_compat_q2_mem_offset(c->memory, address, (int64_t)qa_compat_q2_rerelease_field_offset(layout, "next"));
    qa_compat_q2_mem_write_pointer(c->memory, next, c->has_last_cvar, c->has_last_cvar ? c->last_cvar : address);
    c->last_cvar = address; c->has_last_cvar = true;
  }
  write_cvar(c, record, value);
  if (out) *out = record->address;
  return true;
}
void qa_compat_q2_rerelease_core_imports_free(qa_compat_q2_rerelease_core_imports *c, bool has, qa_guest_address address) {
  if (!has) return;
  c->memory->check(c->memory->ctx, address, 1, "write");
  for (size_t i = 0; i < c->alloc_count; i++) {
    if (c->allocs[i].key == address.byte_offset) {
      double size = c->allocs[i].size;
      c->memory->unmap(c->memory->ctx, address, qa_compat_q2_native_allocation_bytes(size));
      /* Keep the TS Map's insertion order for FreeTags. */
      memmove(&c->allocs[i], &c->allocs[i + 1], (c->alloc_count - i - 1) * sizeof *c->allocs);
      c->alloc_count--;
      return;
    }
  }
  qa_range_error("Q2 TagFree received an unowned or already freed allocation");
}
static qa_guest_call_result result_void(void) { qa_guest_call_result r; memset(&r, 0, sizeof r); r.kind = QA_GUEST_VALUE_VOID; return r; }
static qa_guest_call_result result_u32(double v) { qa_guest_call_result r; memset(&r, 0, sizeof r); r.kind = QA_GUEST_VALUE_UINT32; r.number_value = v; return r; }
static qa_guest_call_result result_u64(uint64_t v) { qa_guest_call_result r; memset(&r, 0, sizeof r); r.kind = QA_GUEST_VALUE_UINT64; r.uint64_value = v; return r; }
static char *arg_text(qa_compat_q2_rerelease_core_imports *c, const qa_guest_call_value *args, size_t count, size_t index) {
  return qa_compat_q2_read_guest_string(c->memory, qa_compat_q2_guest_required_pointer(args, count, index), -1);
}
static qa_guest_call_result cvar_result(qa_compat_q2_rerelease_core_imports *c, bool filled, qa_cvar_snapshot *snap) {
  qa_guest_address out = {0};
  bool has = filled && qa_compat_q2_rerelease_core_imports_cvar(c, snap, &out);
  if (filled) qa_cvar_read_free(snap);
  return qa_compat_q2_rerelease_guest_pointer(has, out);
}
qa_guest_call_result qa_compat_q2_rerelease_core_imports_invoke(qa_compat_q2_rerelease_core_imports *c, qa_compat_q2_rerelease_import_call call) {
  if (c->services.invoke) {
    qa_guest_call_result supplied;
    if (c->services.invoke(c->services.ctx, call, &supplied)) return supplied;
  }
  const qa_guest_call_value *args = call.arguments;
  size_t count = call.argument_count;
  const char *name = call.name;
  qa_compat_q2_rerelease_core_services *s = &c->services;
  if (strcmp(name, "Com_Print") == 0) { char *t = arg_text(c, args, count, 0); s->print(s->ctx, t); free(t); return result_void(); }
  if (strcmp(name, "Com_Error") == 0) { char *t = arg_text(c, args, count, 0); qa_range_error("Q2 %s: %s", call.api, t); }
  if (strcmp(name, "get_configstring") == 0)
    return qa_compat_q2_rerelease_guest_pointer(true, qa_compat_q2_rerelease_core_imports_string(c, s->get_configstring(s->ctx, (double)qa_compat_q2_guest_integer(args, count, 0))));
  if (strcmp(name, "configstring") == 0) {
    qa_guest_address value; bool has = qa_compat_q2_guest_pointer(args, count, 1, &value);
    char *t = has ? qa_compat_q2_read_guest_string(c->memory, value, -1) : xstrdup("");
    s->set_configstring(s->ctx, (double)qa_compat_q2_guest_integer(args, count, 0), t);
    free(t); return result_void();
  }
  if (strcmp(name, "modelindex") == 0 || strcmp(name, "soundindex") == 0 || strcmp(name, "imageindex") == 0) {
    const char *kind = name[0] == 'm' ? "model" : name[0] == 's' ? "sound" : "image";
    char *t = arg_text(c, args, count, 0);
    qa_guest_call_result r = qa_compat_q2_rerelease_guest_int(s->resource_index(s->ctx, kind, t));
    free(t); return r;
  }
  if (strcmp(name, "ServerFrame") == 0) return result_u32(s->server_frame(s->ctx));
  if (strcmp(name, "argc") == 0) { size_t n = 0; (void)s->command_arguments(s->ctx, &n); return qa_compat_q2_rerelease_guest_int((double)n); }
  if (strcmp(name, "argv") == 0 || strcmp(name, "args") == 0) {
    size_t n = 0; const char *const *argv = s->command_arguments(s->ctx, &n);
    const char *tail = s->command_tail(s->ctx);
    /* `${command.join("\0")}\0\0${tail}` keys the reusable strings of one command. */
    size_t key_length = 2 + strlen(tail);
    for (size_t i = 0; i < n; i++) key_length += strlen(argv[i] ? argv[i] : "") + (i ? 1 : 0);
    char *key = malloc(key_length + 1);
    if (!key) qa_range_error("Q2 import allocation failed");
    size_t at = 0;
    for (size_t i = 0; i < n; i++) {
      const char *part = argv[i] ? argv[i] : "";
      if (i) key[at++] = '\0';
      memcpy(key + at, part, strlen(part)); at += strlen(part);
    }
    key[at++] = '\0'; key[at++] = '\0';
    memcpy(key + at, tail, strlen(tail)); at += strlen(tail);
    key[at] = '\0';
    const char *value = tail;
    if (strcmp(name, "argv") == 0) {
      int64_t index = qa_compat_q2_guest_integer(args, count, 0);
      value = index >= 0 && (size_t)index < n && argv[index] ? argv[index] : "";
    }
    qa_guest_address address = qa_compat_q2_command_guest_strings_get(c->command_strings, key, at, value);
    free(key);
    return qa_compat_q2_rerelease_guest_pointer(true, address);
  }
  if (strcmp(name, "AddCommandString") == 0) {
    char *t = arg_text(c, args, count, 0);
    qa_compat_q2_guest_console_filter filtered = qa_compat_q2_filter_guest_console(t);
    free(t);
    if (filtered.refused_count != 0) {
      char *list = qa_compat_q2_guest_console_refused_list(&filtered);
      size_t size = strlen(list) + 64;
      char *message = malloc(size);
      if (!message) qa_range_error("Q2 import allocation failed");
      snprintf(message, size, "game module console command refused: %s\n", list);
      s->print(s->ctx, message);
      free(message); free(list);
    }
    if (filtered.text[0] != '\0') s->add_command(s->ctx, filtered.text);
    qa_compat_q2_guest_console_filter_free(&filtered);
    return result_void();
  }
  if (strcmp(name, "GetExtension") == 0) {
    char *t = arg_text(c, args, count, 0); qa_guest_address out = {0};
    bool has = s->extension && s->extension(s->ctx, t, call.api, &out);
    free(t); return qa_compat_q2_rerelease_guest_pointer(has, out);
  }
  if (strcmp(name, "Info_ValueForKey") == 0) {
    qa_guest_address input = qa_compat_q2_guest_required_pointer(args, count, 0);
    char *key = arg_text(c, args, count, 1);
    int64_t maximum = qa_compat_q2_guest_integer(args, count, 3);
    if (maximum < 0 || maximum > 9007199254740991LL) { free(key); qa_range_error("Invalid Q2 info output size"); }
    char *value = qa_compat_q2_read_guest_string(c->memory, input, 2048);
    size_t length = strlen(value), cursor = value[0] == '\\' ? 1 : 0;
    const char *found = ""; size_t found_len = 0;
    while (cursor < length) {
      const char *sep_p = strchr(value + cursor, '\\');
      if (!sep_p) break;
      size_t sep = (size_t)(sep_p - value);
      const char *next_p = strchr(value + sep + 1, '\\');
      size_t end = next_p ? (size_t)(next_p - value) : length;
      if (strlen(key) == sep - cursor && memcmp(value + cursor, key, sep - cursor) == 0) { found = value + sep + 1; found_len = end - (sep + 1); break; }
      cursor = end + 1;
    }
    if (maximum != 0) {
      qa_guest_address output = qa_compat_q2_guest_required_pointer(args, count, 2);
      size_t n = found_len < (size_t)maximum - 1 ? found_len : (size_t)maximum - 1;
      c->memory->check(c->memory->ctx, output, (double)n + 1, "write");
      c->memory->write(c->memory->ctx, output, (const uint8_t *)found, n);
      qa_compat_q2_mem_write_u8(c->memory, qa_compat_q2_mem_offset(c->memory, output, (int64_t)n), 0);
    }
    free(key); free(value);
    return result_u64((uint64_t)found_len);
  }
  if (strcmp(name, "cvar") == 0) {
    char *n0 = arg_text(c, args, count, 0), *n1 = arg_text(c, args, count, 1);
    qa_cvar_snapshot snap; memset(&snap, 0, sizeof snap);
    bool filled = qa_cvar_registry_register(s->cvars, n0, n1, (double)qa_compat_q2_guest_integer(args, count, 2), &snap);
    free(n0); free(n1);
    return cvar_result(c, filled, &snap);
  }
  if (strcmp(name, "cvar_set") == 0 || strcmp(name, "cvar_forceset") == 0) {
    char *n0 = arg_text(c, args, count, 0), *n1 = arg_text(c, args, count, 1);
    qa_cvar_snapshot snap; memset(&snap, 0, sizeof snap);
    /* A guest may not overwrite the credentials it is not allowed to read. */
    bool filled = qa_cvar_registry_get(s->cvars, n0, &snap);
    if (!filled || (snap.flags & QA_Q2_CVAR_FLAG_PRIVATE) == 0) {
      if (filled) qa_cvar_read_free(&snap);
      memset(&snap, 0, sizeof snap);
      filled = qa_cvar_registry_set(s->cvars, n0, n1, strcmp(name, "cvar_forceset") == 0, &snap);
    }
    free(n0); free(n1);
    return cvar_result(c, filled, &snap);
  }
  if (strcmp(name, "TagMalloc") == 0) {
    int64_t raw = qa_compat_q2_guest_integer(args, count, 0);
    if (raw < 0 || raw > 0x10000000LL) qa_range_error("Q2 TagMalloc size exceeds mapped-memory allocation limit");
    double size = raw > 1 ? (double)raw : 1, tag = (double)qa_compat_q2_guest_integer(args, count, 1);
    char label[128], num[64]; fmt_number(num, sizeof num, tag); snprintf(label, sizeof label, "Q2 tag %s", num);
    qa_guest_address address = qa_compat_q2_allocate_native_memory(c->memory, size, label);
    if (c->alloc_count == c->alloc_cap) {
      c->alloc_cap = c->alloc_cap ? c->alloc_cap * 2 : 16;
      c->allocs = realloc(c->allocs, c->alloc_cap * sizeof *c->allocs);
      if (!c->allocs) qa_range_error("Q2 rerelease imports out of memory");
    }
    c->allocs[c->alloc_count++] = (allocation_t){address.byte_offset, address, size, tag};
    return qa_compat_q2_rerelease_guest_pointer(true, address);
  }
  if (strcmp(name, "TagFree") == 0) {
    qa_guest_address a; bool has = qa_compat_q2_guest_pointer(args, count, 0, &a);
    qa_compat_q2_rerelease_core_imports_free(c, has, a);
    return result_void();
  }
  if (strcmp(name, "FreeTags") == 0) {
    double tag = (double)qa_compat_q2_guest_integer(args, count, 0);
    size_t n = c->alloc_count;
    qa_guest_address *list = xmalloc((n ? n : 1) * sizeof *list); size_t m = 0;
    for (size_t i = 0; i < n; i++) if (c->allocs[i].tag == tag) list[m++] = c->allocs[i].address;
    for (size_t i = 0; i < m; i++) qa_compat_q2_rerelease_core_imports_free(c, true, list[i]);
    free(list);
    return result_void();
  }
  qa_compat_q2_rerelease_throw_missing_import(&call);
}

