/* Port of src/app/bootstrap/simulation/q3/native-guest-source.ts. GPL-2.0-or-later.
 * C accommodations: the process lives in one heap block so the memory, CPU,
 * runner and runtimes keep stable addresses; the TS Set<WindowsFile> becomes a
 * list of owned file wrappers (released with the source); a checkpoint is owned
 * by the source until the next checkpoint or destroy. */
#include "qa/app/bootstrap/simulation/q3/native_guest_source.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q3/native/game_syscalls.h"
#include "qa/compat/q3/native/memory.h"
#include "qa/compat/q3/native/module.h"
#include "qa/compat/q3/native/syscall.h"
#include "qa/compat/qvm/abi.h"
#include "qa/foundation/support.h"
#include "qa/guest/elf.h"
#include "qa/guest/pe.h"
#include "qa/guest/runtime/common/memory.h"
#include "qa/guest/runtime/system_v/runtime.h"
#include "qa/guest/runtime/windows/runtime.h"
#include "qa/guest/x64.h"
#include "qa/guest/x86.h"

static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static char *xstrdup(const char *text) {
  size_t n = strlen(text != NULL ? text : "");
  char *p = malloc(n + 1);
  if (p == NULL) qa_range_error("Out of memory");
  memcpy(p, text != NULL ? text : "", n + 1);
  return p;
}

/* ---- q3NativeProfile / prepareQ3Native ---- */
qa_app_q3_native_profile_name qa_app_q3_native_profile(const uint8_t *bytes, size_t length) {
  if (length >= 2 && bytes[0] == 0x4d && bytes[1] == 0x5a) return "q3-pe";
  if (length >= 4 && bytes[0] == 0x7f && bytes[1] == 0x45 && bytes[2] == 0x4c && bytes[3] == 0x46) {
    qa_elf_inspection_full elf;
    qa_inspect_elf(&elf, bytes, length);
    bool vm_main = false;
    for (size_t i = 0; i < elf.symbol_count && !vm_main; i++)
      vm_main = elf.symbols[i].name != NULL && strcmp(elf.symbols[i].name, "vmMain") == 0 && elf.symbols[i].section != 0;
    qa_elf_inspection_full_free(&elf);
    return vm_main ? "q3-elf" : "quake-live-elf";
  }
  fail("Native Q3 server game must be a Windows PE or a Linux ELF image");
}

void qa_app_prepared_q3_native_destroy(qa_app_prepared_q3_native *prepared) {
  if (prepared == NULL) return;
  free(prepared->owned_bytes);
  for (int i = 0; i < 4; i++) free(prepared->owned_strings[i]);
  memset(prepared, 0, sizeof *prepared);
}

void qa_app_prepare_q3_native(const qa_resolved_execution_module *execution, qa_content_mounted *mounts,
                              qa_app_prepared_q3_native *out) {
  if (strcmp(execution->role, "server-game") != 0 || execution->api.kind != QA_GAME_API_Q3 ||
      execution->api.as.q3.kind != QA_Q3_API_QAGAME)
    fail("Native Q3 guest preparation requires a q3-qagame server-game artifact");
  const qa_resolved_resource_reference *reference = execution->artifact;
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_reference(mounts, reference, &length);
  if (bytes == NULL) fail("Selected native Q3 server artifact is unreadable");
  memset(out, 0, sizeof *out);
  out->owned_bytes = bytes;
  qa_app_q3_native_profile_name profile = qa_app_q3_native_profile(bytes, length);
  const char *kind = execution->profile.kind != NULL ? execution->profile.kind : "";
  if (strcmp(profile, "q3-pe") == 0) {
    qa_pe_file pe;
    qa_parse_pe(&pe, bytes, length);
    bool i386 = pe.abi.kind != NULL && strcmp(pe.abi.kind, "windows-i386") == 0;
    qa_pe_file_free(&pe);
    if (!i386 || strcmp(kind, "windows-i386") != 0) fail("Native Q3 1.32 modules require the Windows i386 profile");
  } else if (strcmp(kind, "windows-i386") == 0 || strcmp(kind, "windows-x86-64") == 0)
    fail("Native ELF module declared a Windows profile");
  else if (strcmp(profile, "q3-elf") == 0 && strcmp(kind, "linux-i386") != 0)
    fail("Native Q3 1.32 modules require the i386 profile");
  if (strcmp(profile, "quake-live-elf") != 0 && execution->api.as.q3.version != 8)
    fail("Native Q3 server games implement the 1.32 game API (version 8)");
  const qa_mount_identity *identity =
      reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE && reference->provenance.archive_mount
          ? &reference->provenance.archive_mount->identity
          : reference->provenance.loose_mount ? &reference->provenance.loose_mount->identity : NULL;
  char revision[512];
  snprintf(revision, sizeof revision, "%s:%lld", identity ? identity->id : "", identity ? (long long)identity->generation : 0);
  out->execution = execution;
  out->owned_strings[0] = xstrdup(execution->owner.provider);
  out->owned_strings[1] = xstrdup(reference->requested_path);
  out->owned_strings[2] = xstrdup(reference->digest);
  out->owned_strings[3] = xstrdup(revision);
  out->image.bytes = bytes;
  out->image.length = length;
  out->image.profile = profile;
  out->image.module.id = out->owned_strings[0];
  out->image.module.artifact_path = out->owned_strings[1];
  out->image.module.digest = out->owned_strings[2];
  out->image.module.revision = out->owned_strings[3];
}

/* ---- the guest process ---- */
#define STACK_LABEL "native Q3 stack"
typedef enum phase_kind { PHASE_CREATED, PHASE_RUNNING, PHASE_CLOSED } phase_kind;
typedef enum lifecycle_kind { LIFECYCLE_Q3, LIFECYCLE_QUAKE_LIVE } lifecycle_kind;

typedef struct owned_file {
  qa_windows_file file; /* handed to the runtime */
  qa_windows_file *inner;
  bool open;
  struct owned_file *next;
} owned_file;

typedef struct mapping_key {
  uint64_t base;
  size_t byte_length;
  qa_guest_permissions permissions;
  char *label;
} mapping_key;

typedef struct checkpoint_storage {
  qa_guest_checkpoint checkpoint;
  qa_guest_memory_region *regions;
  size_t region_count;
  qa_guest_mapping_record *layout;
  size_t layout_count;
  qa_native_guest_heap_image heap;
  qa_guest_allocator_image allocator;
  bool has_allocator;
  qa_native_heap_block *blocks;
  bool has_windows;
  qa_windows_heap_tables windows;
  uint8_t *processor;
  uint8_t *runtime_bytes;
} checkpoint_storage;

struct qa_app_q3_native_guest_source {
  qa_app_q3_game_engine base;
  qa_app_q3_native_guest_options options;
  const qa_app_q3_native_image *prepared;
  qa_guest_module_identity module;
  phase_kind phase;
  lifecycle_kind lifecycle;
  bool windows;
  int width;
  int64_t budget;
  /* process */
  qa_sparse_guest_memory memory;
  bool has_memory;
  qa_mapped_memory_ext ext;
  qa_guest_mapping *mapping_buffer;
  size_t mapping_capacity;
  qa_guest_callback_table callbacks;
  bool has_callbacks;
  qa_guest_processor_state state;
  qa_guest_cpu cpu;
  qa_i386_cpu x86;
  qa_x64_cpu x64;
  bool has_x64;
  qa_guest_call_runner *runner;
  bool has_runner;
  qa_native_game_memory guest;
  bool has_guest;
  qa_qvm_game_data data;
  bool has_data;
  qa_compat_q3_native_syscalls syscalls;
  bool has_syscalls;
  qa_windows_guest_runtime windows_runtime;
  bool has_windows_runtime;
  qa_pe_image pe;
  bool has_pe;
  qa_system_v_guest_runtime system_v;
  bool has_system_v;
  qa_elf_guest_image elf;
  bool has_elf;
  qa_guest_core_call_context context;
  qa_compat_q3_vm_module vm;
  qa_compat_q3_quakelive_game_imports imports;
  bool has_imports;
  qa_compat_q3_quakelive_game_module quake_live;
  qa_native_abi abi; /* the image ABI (checkpoint abi) */
  /* TS argumentsOf.current */
  const char *const *arguments;
  size_t argument_count;
  bool has_arguments;
  owned_file *files;
  mapping_key *baseline;
  size_t baseline_count;
  bool has_checkpoint;
  checkpoint_storage saved;
};

static const qa_app_q3_game_engine_ops native_ops;
static qa_app_q3_native_guest_source *self_of(qa_app_q3_game_engine *engine) { return (qa_app_q3_native_guest_source *)engine; }

/* ---- the SparseGuestMemory extension the loaders and runtimes use ---- */
static qa_guest_core_address e_map(void *ctx, uint64_t base, size_t length, qa_guest_permissions permissions, const char *label,
                                   const uint8_t *bytes, size_t bytes_len) {
  qa_app_q3_native_guest_source *s = ctx;
  qa_guest_map_options o;
  memset(&o, 0, sizeof o);
  o.base = base; o.byte_length = length; o.permissions = permissions; o.label = label; o.bytes = bytes; o.bytes_length = bytes_len;
  return qa_sparse_map(&s->memory, &o);
}
static qa_guest_core_address e_allocate(void *ctx, size_t length, bool has_alignment, uint64_t alignment, bool has_permissions,
                                        qa_guest_permissions permissions, const char *label) {
  qa_app_q3_native_guest_source *s = ctx;
  qa_guest_allocation_options o;
  memset(&o, 0, sizeof o);
  o.byte_length = length; o.alignment = has_alignment ? alignment : 0; o.permissions = permissions;
  o.has_permissions = has_permissions; o.label = label;
  return qa_sparse_allocate(&s->memory, &o);
}
static void e_unmap(void *ctx, qa_guest_core_address a, size_t length) { qa_sparse_unmap(&((qa_app_q3_native_guest_source *)ctx)->memory, a, length); }
static void e_protect(void *ctx, qa_guest_core_address a, size_t length, qa_guest_permissions p) {
  qa_sparse_protect(&((qa_app_q3_native_guest_source *)ctx)->memory, a, length, p);
}
static size_t list_mappings(qa_app_q3_native_guest_source *s) {
  size_t n = qa_sparse_mappings(&s->memory, NULL, 0);
  if (n > s->mapping_capacity) {
    qa_guest_mapping *grown = realloc(s->mapping_buffer, n * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    s->mapping_buffer = grown; s->mapping_capacity = n;
  }
  return qa_sparse_mappings(&s->memory, s->mapping_buffer, s->mapping_capacity);
}
static const qa_guest_mapping *e_mappings(void *ctx, size_t *count) {
  qa_app_q3_native_guest_source *s = ctx;
  *count = list_mappings(s);
  return s->mapping_buffer;
}
static void e_move(void *ctx, qa_guest_core_address d, qa_guest_core_address src, size_t n) {
  qa_sparse_move(&((qa_app_q3_native_guest_source *)ctx)->memory, d, src, n);
}
static void e_fill(void *ctx, qa_guest_core_address a, size_t n, int v) { qa_sparse_fill(&((qa_app_q3_native_guest_source *)ctx)->memory, a, n, (uint32_t)v); }
static int64_t e_find_zero(void *ctx, qa_guest_core_address a, size_t m) { return qa_sparse_find_zero(&((qa_app_q3_native_guest_source *)ctx)->memory, a, m); }

static bool i386_host_call(void *ctx, qa_guest_core_address address) { return qa_guest_callback_table_enter(ctx, address); }
static bool x64_host_call(void *ctx, qa_guest_core_address address) { return qa_guest_callback_table_resolve(ctx, address) != NULL; }
static qa_guest_execution_stop run_x86(void *ctx, int64_t budget, bool has_return, qa_guest_core_address ret) {
  return qa_i386_cpu_run(ctx, budget, has_return, ret);
}
static qa_guest_execution_stop run_x64(void *ctx, int64_t budget, bool has_return, qa_guest_core_address ret) {
  return qa_x64_cpu_run(ctx, budget, has_return, ret);
}
static bool variadic_layouts(void *ctx, const qa_guest_host_callback *callback, const qa_call_value *fixed, size_t fixed_count,
                             const qa_guest_core_call_context *context, qa_abi_value_layout **out, size_t *out_count) {
  qa_app_q3_native_guest_source *s = ctx;
  (void)context;
  *out = NULL; *out_count = 0;
  if (!s->has_syscalls || !qa_compat_q3_native_syscalls_variadic_layouts(&s->syscalls, callback, fixed, fixed_count, out, out_count))
    fail("Native Q3 variadic callback has no declared layout");
  return true;
}

/* ---- the unlocated Quake Live game memory: any access is a defect ---- */
static noreturn void unlocated(void) { fail("The Quake Live table interface locates no Q3 game data"); }
static bool u_pointer(void *c, int64_t p, qa_qvm_game_region *o) { (void)c; (void)p; (void)o; unlocated(); }
static qa_qvm_game_region u_span(void *c, int64_t p, int64_t l, int64_t r) { (void)c; (void)p; (void)l; (void)r; unlocated(); }
static void u_view(void *c, int64_t p, int64_t l, int64_t r, qa_qvm_writable_view *o) { (void)c; (void)p; (void)l; (void)r; (void)o; unlocated(); }
static char *u_read_string(void *c, int64_t p) { (void)c; (void)p; unlocated(); }
static void u_write_string(void *c, int64_t p, const char *t, int64_t n) { (void)c; (void)p; (void)t; (void)n; unlocated(); }
static bool u_address(void *c, int64_t p, int64_t *o) { (void)c; (void)p; (void)o; unlocated(); }
static int64_t u_pointer_word(void *c, bool h, int64_t a) { (void)c; (void)h; (void)a; unlocated(); }
static void u_view_at(void *c, int64_t a, int64_t l, qa_qvm_writable_view *o) { (void)c; (void)a; (void)l; (void)o; unlocated(); }
static void u_region_view(void *c, const qa_qvm_game_region *r, int64_t o, int64_t l, qa_qvm_writable_view *v) {
  (void)c; (void)r; (void)o; (void)l; (void)v; unlocated();
}
static void u_region_write(void *c, const qa_qvm_game_region *r, const uint8_t *b, size_t l) { (void)c; (void)r; (void)b; (void)l; unlocated(); }
static void u_region_fill(void *c, const qa_qvm_game_region *r, int64_t s, int64_t l, int32_t v) {
  (void)c; (void)r; (void)s; (void)l; (void)v; unlocated();
}
static const qa_qvm_game_memory_ops unlocated_ops = {u_pointer, u_span, u_view, u_read_string, u_write_string, u_address,
                                                      u_pointer_word, u_view_at, u_region_view, u_region_write, u_region_fill};

/* ---- SV_GameSystemCalls: libc and math traps, then G_LOCATE_GAME_DATA, then the server game's table ---- */
static int32_t word_at(const uint8_t *words, size_t offset) {
  return (int32_t)((uint32_t)words[offset] | (uint32_t)words[offset + 1] << 8 | (uint32_t)words[offset + 2] << 16 |
                   (uint32_t)words[offset + 3] << 24);
}
static double trap(void *ctx, const uint8_t *words, size_t words_length) {
  qa_app_q3_native_guest_source *s = ctx;
  if (!s->has_guest || s->options.traps == NULL) fail("A Q3 1.32 native module needs the server trap table");
  qa_qvm_game_memory guest = qa_native_game_memory_game_memory(&s->guest);
  qa_qvm_host_result intrinsic = 0;
  if (qa_qvm_intrinsic_syscall("qagame", "q3-modern", words, words_length, &guest, &intrinsic)) return intrinsic;
  qa_qvm_host_call call;
  qa_native_q3_game_call(words, words_length, &guest, s->arguments, s->argument_count, s->has_arguments, &call);
  if (strcmp(call.kind, "engine") == 0 && call.code == QA_QVM_GAME_IMPORT_G_LOCATE_GAME_DATA) {
    qa_qvm_game_data_locate(&s->data, word_at(words, 4), word_at(words, 8), word_at(words, 12), word_at(words, 16), word_at(words, 20));
    return 0;
  }
  return s->options.traps(s->options.traps_ctx, &call);
}

/* ---- the Quake Live import table's engine services, with commandArguments from the current call ---- */
static const qa_compat_q3_native_engine_services *ql(void *ctx) { return &((qa_app_q3_native_guest_source *)ctx)->options.services->engine; }
static void ql_append(void *c, const char *t) { ql(c)->append_console_command(ql(c)->ctx, t); }
static void ql_print(void *c, const char *t) { ql(c)->print(ql(c)->ctx, t); }
static void ql_drop(void *c, double n, const char *r) { ql(c)->drop_client(ql(c)->ctx, n, r); }
static void ql_send(void *c, double n, const char *t) { ql(c)->send_server_command(ql(c)->ctx, n, t); }
static const char *ql_get_userinfo(void *c, double n) { return ql(c)->get_userinfo(ql(c)->ctx, n); }
static void ql_set_userinfo(void *c, double n, const char *v) { ql(c)->set_userinfo(ql(c)->ctx, n, v); }
static double ql_value(void *c, const char *n) { return ql(c)->cvar_variable_value(ql(c)->ctx, n); }
static char *ql_string(void *c, const char *n) { return ql(c)->cvar_variable_string(ql(c)->ctx, n); }
static void ql_set(void *c, const char *n, const char *v, bool f) { ql(c)->cvar_set(ql(c)->ctx, n, v, f); }
static int64_t ql_bind(void *c, const char *n, const char *d, double f) { return ql(c)->cvar_bind_vm(ql(c)->ctx, n, d, f); }
static bool ql_read(void *c, int64_t h, double *m, char **v, double *n, double *i) { return ql(c)->cvar_read_vm(ql(c)->ctx, h, m, v, n, i); }
static void ql_configstring_set(void *c, double i, const char *v) { ql(c)->configstring_set(ql(c)->ctx, i, v); }
static const char *ql_configstring_get(void *c, double i) { return ql(c)->configstring_get(ql(c)->ctx, i); }
static const char *const *ql_arguments(void *c, size_t *count) {
  qa_app_q3_native_guest_source *s = c;
  if (s->has_arguments) { *count = s->argument_count; return s->arguments; }
  return ql(c)->command_arguments(ql(c)->ctx, count);
}

/* ---- Windows files the module opened, closed with the process ---- */
static owned_file *owned(void *ctx) { return ctx; }
static size_t f_read(void *c, size_t offset, uint8_t *out, size_t length) { return owned(c)->inner->read(owned(c)->inner->ctx, offset, out, length); }
static int64_t f_write(void *c, size_t offset, const uint8_t *bytes, size_t length) { return owned(c)->inner->write(owned(c)->inner->ctx, offset, bytes, length); }
static int64_t f_size(void *c) { return owned(c)->inner->size(owned(c)->inner->ctx); }
static void f_truncate(void *c, size_t length) { owned(c)->inner->truncate(owned(c)->inner->ctx, length); }
static void f_flush(void *c) { owned(c)->inner->flush(owned(c)->inner->ctx); }
static void f_close(void *c) {
  owned_file *file = c;
  if (!file->open) return;
  file->open = false;
  file->inner->close(file->inner->ctx);
}
static qa_windows_file *open_file(void *ctx, const char *path, bool read, bool write, int creation) {
  qa_app_q3_native_guest_source *s = ctx;
  const qa_windows_capabilities *windows = s->options.windows;
  qa_windows_file *file = windows != NULL && windows->open_file != NULL ? windows->open_file(windows->ctx, path, read, write, creation) : NULL;
  if (file == NULL) return NULL;
  owned_file *entry = xcalloc(1, sizeof *entry);
  entry->inner = file;
  entry->open = true;
  entry->file.ctx = entry;
  entry->file.read = f_read; entry->file.write = f_write; entry->file.size = f_size;
  entry->file.truncate = f_truncate; entry->file.flush = f_flush; entry->file.close = f_close;
  entry->next = s->files;
  s->files = entry;
  return &entry->file;
}
/* closeFiles: every open file closes; the first failure raises after the rest ran. */
typedef struct close_step { owned_file *file; } close_step;
static void close_one(void *arg) { f_close(((close_step *)arg)->file); }
static bool close_files(qa_app_q3_native_guest_source *s, qa_caught_error *first) {
  bool ok = true;
  for (owned_file *file = s->files; file != NULL; file = file->next) {
    if (!file->open) continue;
    close_step step = {file};
    qa_caught_error caught;
    if (!qa_try(close_one, &step, &caught) && ok) { ok = false; *first = caught; }
  }
  return ok;
}
/* { ...options.windows, openFile }: the capability table shares one ctx, so the caller's members forward with theirs. */
static const qa_windows_capabilities *caller_windows(void *ctx) { return ((qa_app_q3_native_guest_source *)ctx)->options.windows; }
static bool w_now(void *c, double *out) { return caller_windows(c)->now_milliseconds(caller_windows(c)->ctx, out); }
static bool w_counter(void *c, int64_t *out) { return caller_windows(c)->performance_counter(caller_windows(c)->ctx, out); }
static void w_output(void *c, qa_windows_stream stream, const uint8_t *bytes, size_t length) {
  caller_windows(c)->standard_output(caller_windows(c)->ctx, stream, bytes, length);
}
static size_t w_input(void *c, size_t length, uint8_t *out) { return caller_windows(c)->standard_input(caller_windows(c)->ctx, length, out); }
static qa_windows_capabilities windows_capabilities(qa_app_q3_native_guest_source *s) {
  qa_windows_capabilities caps;
  memset(&caps, 0, sizeof caps);
  const qa_windows_capabilities *w = s->options.windows;
  if (w != NULL) {
    caps = *w;
    caps.now_milliseconds = w->now_milliseconds != NULL ? w_now : NULL;
    caps.performance_counter = w->performance_counter != NULL ? w_counter : NULL;
    caps.standard_output = w->standard_output != NULL ? w_output : NULL;
    caps.standard_input = w->standard_input != NULL ? w_input : NULL;
  }
  caps.ctx = s;
  caps.open_file = open_file;
  return caps;
}
static bool system_v_now(void *ctx, int64_t *out) {
  qa_app_q3_native_guest_source *s = ctx;
  const qa_app_q3_native_game_services *services = s->options.services;
  double milliseconds = services != NULL ? services->milliseconds(services->engine.ctx) : 0;
  *out = (int64_t)trunc(milliseconds / 1000);
  return true;
}

/* ---- mappings ---- */
static bool writable(qa_guest_permissions permissions) {
  return permissions == QA_PERM_READ_WRITE || permissions == QA_PERM_READ_WRITE_EXECUTE;
}
/* describedMappings: every mapping except the call stack, which holds nothing between calls. Malloc'd copy; labels borrowed. */
static qa_guest_mapping *described_mappings(qa_app_q3_native_guest_source *s, size_t *count) {
  size_t n = list_mappings(s), kept = 0;
  qa_guest_mapping *out = xcalloc(n, sizeof *out);
  for (size_t i = 0; i < n; i++)
    if (s->mapping_buffer[i].label == NULL || strcmp(s->mapping_buffer[i].label, STACK_LABEL) != 0) out[kept++] = s->mapping_buffer[i];
  *count = kept;
  return out;
}
static const char *label_of(const qa_guest_mapping *m) { return m->label != NULL ? m->label : ""; }
static bool in_baseline(const qa_app_q3_native_guest_source *s, const qa_guest_mapping *m) {
  for (size_t i = 0; i < s->baseline_count; i++) {
    const mapping_key *k = &s->baseline[i];
    if (k->base == m->base && k->byte_length == m->byte_length && k->permissions == m->permissions && strcmp(k->label, label_of(m)) == 0)
      return true;
  }
  return false;
}
static qa_guest_core_address address_at(qa_app_q3_native_guest_source *s, uint64_t base) {
  qa_guest_core_address address;
  if (!qa_sparse_pointer(&s->memory, base, &address)) fail("Native Q3 mapping has no address");
  return address;
}
/* readable(address, mapping, access): the mapping is temporarily read-write, then returned to its own permissions. */
typedef struct protect_frame { qa_app_q3_native_guest_source *s; qa_guest_core_address address; size_t length; qa_guest_permissions permissions; } protect_frame;
static void protect_back(void *arg) { protect_frame *f = arg; qa_sparse_protect(&f->s->memory, f->address, f->length, f->permissions); }
static void readable_copy(qa_app_q3_native_guest_source *s, qa_guest_core_address address, qa_guest_permissions permissions, uint8_t *dest,
                          size_t length, const uint8_t *source) {
  bool open = writable(permissions);
  protect_frame frame = {s, address, length, permissions};
  qa_cleanup cleanup;
  if (!open) {
    qa_sparse_protect(&s->memory, address, length, QA_PERM_READ_WRITE);
    qa_cleanup_push(&cleanup, protect_back, &frame);
  }
  if (dest != NULL) qa_sparse_copy(&s->memory, address, dest, length);
  else qa_sparse_write(&s->memory, address, source, length);
  if (!open) {
    qa_cleanup_pop(&cleanup);
    protect_back(&frame);
  }
}
static void release_memory(qa_app_q3_native_guest_source *s) {
  if (!s->has_memory) return;
  size_t n = list_mappings(s);
  qa_guest_mapping *copy = xcalloc(n, sizeof *copy);
  memcpy(copy, s->mapping_buffer, n * sizeof *copy);
  for (size_t i = 0; i < n; i++) {
    qa_guest_core_address address;
    if (qa_sparse_pointer(&s->memory, copy[i].base, &address)) qa_sparse_unmap(&s->memory, address, copy[i].byte_length);
  }
  free(copy);
}

static void release_memory_step(void *arg) { release_memory(arg); }

/* ---- destruction ---- */
static void free_checkpoint(qa_app_q3_native_guest_source *s) {
  if (!s->has_checkpoint) return;
  checkpoint_storage *c = &s->saved;
  for (size_t i = 0; i < c->region_count; i++) free((void *)c->regions[i].bytes.bytes);
  free(c->regions);
  for (size_t i = 0; i < c->layout_count; i++) free((void *)c->layout[i].label);
  free(c->layout);
  if (c->has_allocator) qa_guest_allocator_image_free(&c->allocator);
  free(c->blocks);
  if (c->has_windows) qa_windows_heap_tables_free(&c->windows);
  free(c->processor);
  free(c->runtime_bytes);
  memset(c, 0, sizeof *c);
  s->has_checkpoint = false;
}
static void destroy_source(qa_app_q3_native_guest_source *s) {
  if (s == NULL) return;
  free_checkpoint(s);
  if (s->has_imports) qa_compat_q3_quakelive_game_imports_destroy(&s->imports);
  if (s->has_syscalls) qa_compat_q3_native_syscalls_destroy(&s->syscalls);
  if (s->has_windows_runtime) qa_windows_guest_runtime_destroy(&s->windows_runtime);
  if (s->has_system_v) qa_system_v_guest_runtime_destroy(&s->system_v);
  if (s->has_runner) qa_guest_call_runner_destroy(s->runner);
  free(s->runner);
  if (s->has_x64) qa_x64_cpu_destroy(&s->x64);
  if (s->has_data) qa_qvm_game_data_destroy(&s->data);
  if (s->has_guest) qa_native_game_memory_destroy(&s->guest);
  if (s->has_callbacks) qa_guest_callback_table_destroy(&s->callbacks);
  if (s->has_memory) qa_sparse_guest_memory_destroy(&s->memory);
  if (s->has_pe) qa_pe_image_free(&s->pe);
  if (s->has_elf) qa_elf_guest_image_free(&s->elf);
  for (owned_file *file = s->files; file != NULL;) {
    owned_file *next = file->next;
    free(file);
    file = next;
  }
  for (size_t i = 0; i < s->baseline_count; i++) free(s->baseline[i].label);
  free(s->baseline);
  free(s->mapping_buffer);
  free(s);
}

/* ---- construction ---- */
static qa_guest_core_call_context context_for(qa_app_q3_native_guest_source *s, qa_guest_core_address address, qa_guest_native_abi abi) {
  qa_guest_core_call_context context;
  memset(&context, 0, sizeof context);
  context.module = s->module;
  context.callback_kind = QA_CALLBACK_REF_NATIVE_GUEST;
  context.callback_module = s->module;
  context.callback_address = address;
  context.callback_abi = abi;
  return context;
}
static qa_native_abi contract_abi(qa_guest_native_abi abi) {
  qa_native_abi out = {abi.kind, abi.image, abi.pointer_bytes, abi.call};
  return out;
}
static void build(void *arg) {
  qa_app_q3_native_guest_source *s = arg;
  const qa_app_q3_native_image *prepared = s->prepared;
  const uint8_t *bytes = prepared->bytes;
  s->windows = strcmp(prepared->profile, "q3-pe") == 0;
  bool wide = !s->windows && prepared->length > 4 && bytes[4] == 2;
  s->width = s->windows || !wide ? 4 : 8;
  qa_sparse_guest_memory_options memory_options;
  memset(&memory_options, 0, sizeof memory_options);
  memory_options.module = s->module;
  memory_options.pointer_bytes = s->width;
  if (!s->windows) { memory_options.has_allocation_base = true; memory_options.allocation_base = 0x50000000u; }
  qa_sparse_guest_memory_init(&s->memory, &memory_options);
  s->has_memory = true;
  s->ext.ctx = s; s->ext.map = e_map; s->ext.allocate = e_allocate; s->ext.unmap = e_unmap; s->ext.protect = e_protect;
  s->ext.mappings = e_mappings; s->ext.move = e_move; s->ext.fill = e_fill; s->ext.find_zero = e_find_zero;
  qa_guest_callback_table_init(&s->callbacks, &s->memory);
  s->has_callbacks = true;
  const size_t stack_bytes = 1048576;
  /* Variadic syscall arguments are decoded from the stack top; keep a mapped guard above the first frame. */
  qa_guest_allocation_options stack;
  memset(&stack, 0, sizeof stack);
  stack.byte_length = stack_bytes + 4096; stack.alignment = 16; stack.label = STACK_LABEL;
  qa_guest_core_address stack_address = qa_sparse_allocate(&s->memory, &stack);
  qa_guest_allocation_options returned;
  memset(&returned, 0, sizeof returned);
  returned.byte_length = 16; returned.permissions = QA_PERM_READ_EXECUTE; returned.has_permissions = true; returned.label = "native Q3 return";
  qa_guest_core_address returned_address = qa_sparse_allocate(&s->memory, &returned);
  qa_guest_processor_initial_state initial = {s->width == 8 ? QA_GUEST_X86_64 : QA_GUEST_I386, 0,
                                              stack_address.byte_offset + stack_bytes - 16, 2, 0x37f, 0x1f80, 0xffff};
  qa_create_guest_processor_state(&s->state, &initial);
  s->cpu.state = &s->state; s->cpu.memory = &s->memory.base; s->cpu.memory_ext = &s->ext; s->cpu.module = s->module;
  if (s->width == 8) {
    qa_x64_cpu_init(&s->x64, &s->state, &s->memory, x64_host_call, &s->callbacks, &s->callbacks);
    s->has_x64 = true;
    s->cpu.run_ctx = &s->x64; s->cpu.run = run_x64;
  } else {
    qa_i386_cpu_init(&s->x86, &s->state, &s->memory, i386_host_call, &s->callbacks, NULL, NULL);
    s->cpu.run_ctx = &s->x86; s->cpu.run = run_x86;
  }
  s->runner = xcalloc(1, qa_guest_call_runner_size());
  qa_guest_call_runner_options runner;
  memset(&runner, 0, sizeof runner);
  runner.cpu = &s->cpu; runner.callbacks = &s->callbacks; runner.return_address = returned_address;
  runner.variadic_ctx = s; runner.variadic_layouts = variadic_layouts;
  if (!qa_guest_call_runner_init(s->runner, runner)) qa_range_error("Out of memory");
  s->has_runner = true;
  qa_qvm_game_memory data_memory;
  if (s->width == 4) {
    qa_native_game_memory_init(&s->guest, &s->memory);
    s->has_guest = true;
    data_memory = qa_native_game_memory_game_memory(&s->guest);
  } else {
    data_memory.ops = &unlocated_ops; data_memory.ctx = NULL;
  }
  qa_qvm_game_data_init(&s->data, &data_memory, "q3-modern");
  s->has_data = true;
  qa_compat_q3_native_module_options module_options;
  memset(&module_options, 0, sizeof module_options);
  module_options.runner = s->runner;
  module_options.instruction_budget = s->budget;
  if (s->windows) {
    qa_map_pe_image_options map;
    memset(&map, 0, sizeof map);
    map.bytes = bytes; map.bytes_len = prepared->length; map.memory = &s->memory.base; map.memory_ext = &s->ext; map.memory_module = s->module;
    qa_map_pe_image(&s->pe, &map);
    s->has_pe = true;
    qa_windows_runtime_options windows;
    memset(&windows, 0, sizeof windows);
    windows.memory = &s->memory.base; windows.memory_ext = &s->ext; windows.callbacks = &s->callbacks; windows.module = s->module;
    windows.has_capabilities = true;
    windows.capabilities = windows_capabilities(s);
    qa_windows_guest_runtime_init(&s->windows_runtime, &windows);
    s->has_windows_runtime = true;
    qa_windows_guest_runtime_attach_runner(&s->windows_runtime, s->runner);
    qa_guest_core_address vm_main;
    if (!qa_compat_q3_native_export(&s->pe.image, "vmMain", &vm_main)) fail("Native Q3 module has no vmMain export");
    s->context = context_for(s, vm_main, s->pe.image.abi);
    qa_windows_initialize_options init = {s->context, s->budget};
    qa_windows_guest_runtime_initialize(&s->windows_runtime, &s->pe, &init);
    if (!qa_compat_q3_native_syscalls_init(&s->syscalls, &s->callbacks, s->pe.image.abi, trap, s)) qa_range_error("Out of memory");
    s->has_syscalls = true;
    module_options.image = s->pe.image; module_options.context = s->context;
    qa_compat_q3_vm_module_init(&s->vm, module_options, s->syscalls.address);
    s->abi = contract_abi(s->pe.image.abi);
    s->lifecycle = LIFECYCLE_Q3;
    return;
  }
  qa_guest_map_options sentinel;
  memset(&sentinel, 0, sizeof sentinel);
  static const uint8_t breakpoint[1] = {0xcc};
  sentinel.base = 0x30000; sentinel.byte_length = 4096; sentinel.permissions = QA_PERM_READ_EXECUTE;
  sentinel.bytes = breakpoint; sentinel.bytes_length = 1;
  qa_guest_core_address sentinel_address = qa_sparse_map(&s->memory, &sentinel);
  qa_system_v_runtime_options system_v;
  memset(&system_v, 0, sizeof system_v);
  system_v.memory = &s->memory.base; system_v.memory_ext = &s->ext; system_v.callbacks = &s->callbacks; system_v.module = s->module;
  system_v.has_capabilities = true;
  system_v.capabilities.ctx = s; system_v.capabilities.now_seconds = system_v_now;
  qa_system_v_guest_runtime_init(&s->system_v, &system_v);
  s->has_system_v = true;
  qa_system_v_guest_runtime_attach_runner(&s->system_v, s->runner);
  qa_abi_value_layout no_layouts[1];
  qa_guest_call_signature signature;
  qa_system_v_guest_runtime_signature(&s->system_v, NULL, 0, true, QA_STORAGE_INT32, no_layouts, &signature);
  s->context = context_for(s, sentinel_address, signature.abi);
  qa_system_v_load_options load;
  memset(&load, 0, sizeof load);
  load.bytes = bytes; load.bytes_len = prepared->length; load.module = s->module;
  load.load_bias = s->width == 4 ? 0x10000000u : 0x100000000u;
  qa_system_v_guest_runtime_load(&s->system_v, &load, &s->elf);
  s->has_elf = true;
  qa_system_v_initialize_options init = {s->context, s->budget};
  qa_system_v_guest_runtime_initialize(&s->system_v, &s->elf, &init);
  s->abi = contract_abi(s->elf.image.abi);
  module_options.image = s->elf.image; module_options.context = s->context;
  if (strcmp(prepared->profile, "q3-elf") == 0) {
    if (!qa_compat_q3_native_syscalls_init(&s->syscalls, &s->callbacks, s->elf.image.abi, trap, s)) qa_range_error("Out of memory");
    s->has_syscalls = true;
    qa_compat_q3_vm_module_init(&s->vm, module_options, s->syscalls.address);
    s->lifecycle = LIFECYCLE_Q3;
    return;
  }
  if (s->options.services == NULL) fail("A Quake Live native module needs the engine services behind its import table");
  qa_compat_q3_native_engine_services services = {s, ql_append, ql_print, ql_drop, ql_send, ql_get_userinfo, ql_set_userinfo,
                                                  ql_value, ql_string, ql_set, ql_bind, ql_read, ql_configstring_set,
                                                  ql_configstring_get, ql_arguments};
  if (!qa_compat_q3_quakelive_game_imports_init(&s->imports, &s->memory, &s->callbacks, s->elf.image.abi, services))
    qa_range_error("Out of memory");
  s->has_imports = true;
  qa_compat_q3_quakelive_game_module_init(&s->quake_live, module_options, s->imports.address);
  s->lifecycle = LIFECYCLE_QUAKE_LIVE;
}

static void record_baseline(qa_app_q3_native_guest_source *s) {
  size_t count = 0;
  qa_guest_mapping *mappings = described_mappings(s, &count);
  s->baseline = xcalloc(count, sizeof *s->baseline);
  for (size_t i = 0; i < count; i++) {
    s->baseline[i].base = mappings[i].base; s->baseline[i].byte_length = mappings[i].byte_length;
    s->baseline[i].permissions = mappings[i].permissions; s->baseline[i].label = xstrdup(label_of(&mappings[i]));
  }
  s->baseline_count = count;
  free(mappings);
}

qa_app_q3_native_guest_source *qa_app_q3_native_guest_source_create(const qa_app_q3_native_guest_options *options) {
  qa_app_q3_native_guest_source *s = calloc(1, sizeof *s);
  if (s == NULL) return NULL;
  s->options = *options;
  s->prepared = options->prepared;
  s->budget = options->has_instruction_budget ? options->instruction_budget : 5000000;
  const qa_module_identity *m = &options->prepared->module;
  s->module.id = m->id; s->module.artifact_path = m->artifact_path; s->module.digest = m->digest; s->module.revision = m->revision;
  qa_caught_error error;
  if (!qa_try(build, s, &error)) {
    qa_caught_error cleanup;
    bool released = true;
    if (!qa_try(release_memory_step, s, &cleanup)) released = false;
    if (released && !close_files(s, &cleanup)) released = false;
    destroy_source(s);
    if (!released)
      qa_raise("AggregateError", 0, 0, "Native Q3 guest construction and cleanup failed: %s; %s", error.message, cleanup.message);
    qa_rethrow(&error);
  }
  record_baseline(s);
  s->phase = PHASE_CREATED;
  s->base.ops = &native_ops;
  s->base.kind = "native";
  s->base.module = *m;
  s->base.data = &s->data;
  return s;
}
qa_app_q3_game_engine *qa_app_q3_native_guest_source_engine(qa_app_q3_native_guest_source *source) { return &source->base; }
qa_app_q3_native_guest_source *qa_app_q3_native_guest_source_of(qa_app_q3_game_engine *engine) {
  return engine != NULL && engine->ops == &native_ops ? self_of(engine) : NULL;
}
qa_app_q3_native_profile_name qa_app_q3_native_guest_source_profile(const qa_app_q3_native_guest_source *source) {
  return source->prepared->profile;
}
qa_sparse_guest_memory *qa_app_q3_native_guest_source_memory(qa_app_q3_native_guest_source *source) { return &source->memory; }
qa_guest_call_runner *qa_app_q3_native_guest_source_runner(qa_app_q3_native_guest_source *source) { return source->runner; }
size_t qa_app_q3_native_guest_source_coverage(const qa_app_q3_native_guest_source *source, qa_compat_q3_native_service_coverage *out,
                                              size_t capacity) {
  if (!source->has_imports) return 0;
  size_t total = qa_compat_q3_quakelive_game_imports_coverage(&source->imports, NULL, 0);
  qa_compat_q3_native_service_coverage *all = xcalloc(total, sizeof *all);
  qa_compat_q3_quakelive_game_imports_coverage(&source->imports, all, total);
  size_t count = 0;
  for (size_t i = 0; i < total; i++) {
    if (all[i].reached <= 0) continue;
    if (count < capacity) out[count] = all[i];
    count++;
  }
  free(all);
  return count;
}

/* ---- the lifecycle ---- */
static double call_q3(qa_app_q3_native_guest_source *s, int command, const double *values, size_t count) {
  return qa_compat_q3_vm_module_call(&s->vm, command, values, count);
}
typedef struct arguments_frame { qa_app_q3_native_guest_source *s; const char *const *arguments; size_t count; bool has; } arguments_frame;
static void arguments_restore(void *arg) {
  arguments_frame *f = arg;
  f->s->arguments = f->arguments; f->s->argument_count = f->count; f->s->has_arguments = f->has;
}
static void with_arguments_begin(qa_app_q3_native_guest_source *s, arguments_frame *frame, qa_cleanup *cleanup, const char *const *argv,
                                 size_t argc) {
  frame->s = s; frame->arguments = s->arguments; frame->count = s->argument_count; frame->has = s->has_arguments;
  qa_cleanup_push(cleanup, arguments_restore, frame);
  s->arguments = argv; s->argument_count = argc; s->has_arguments = true;
}
static void with_arguments_end(arguments_frame *frame, qa_cleanup *cleanup) {
  qa_cleanup_pop(cleanup);
  arguments_restore(frame);
}
static void live(qa_app_q3_native_guest_source *s) {
  if (s->phase == PHASE_CLOSED) fail("Native Q3 guest is closed");
  if (s->phase != PHASE_RUNNING) fail("Native Q3 guest has not run GAME_INIT");
}
static void lifecycle_shutdown(qa_app_q3_native_guest_source *s, bool restart) {
  if (s->lifecycle == LIFECYCLE_QUAKE_LIVE) { qa_compat_q3_quakelive_game_module_shutdown(&s->quake_live, restart); return; }
  double values[1] = {restart ? 1 : 0};
  call_q3(s, QA_QVM_GAME_EXPORT_GAME_SHUTDOWN, values, 1);
}

static bool n_active(qa_app_q3_game_engine *base) { return qa_guest_call_runner_depth(self_of(base)->runner) != 0; }
static void n_initialize(qa_app_q3_game_engine *base, double level_time, double seed, bool restart) {
  qa_app_q3_native_guest_source *s = self_of(base);
  if (s->phase == PHASE_CLOSED) fail("Native Q3 guest is closed");
  if (s->phase != PHASE_CREATED && !restart) fail("Native Q3 GAME_INIT requires a fresh game or a restart");
  if (qa_guest_call_runner_depth(s->runner) != 0) fail("Native Q3 guest has an active call");
  if (s->lifecycle == LIFECYCLE_QUAKE_LIVE) {
    qa_compat_q3_quakelive_game_module_register_cvars(&s->quake_live);
    qa_compat_q3_quakelive_game_module_initialize(&s->quake_live, level_time, seed, restart);
  } else {
    double values[3] = {level_time, seed, restart ? 1 : 0};
    call_q3(s, QA_QVM_GAME_EXPORT_GAME_INIT, values, 3);
  }
  s->phase = PHASE_RUNNING;
}
static void n_shutdown(qa_app_q3_game_engine *base, bool restart) {
  qa_app_q3_native_guest_source *s = self_of(base);
  live(s);
  lifecycle_shutdown(s, restart);
}
static char *n_client_connect(qa_app_q3_game_engine *base, double client, bool first_time, bool is_bot) {
  qa_app_q3_native_guest_source *s = self_of(base);
  live(s);
  qa_guest_core_address address;
  bool has_address;
  if (s->lifecycle == LIFECYCLE_QUAKE_LIVE)
    has_address = qa_compat_q3_quakelive_game_module_client_connect(&s->quake_live, client, first_time, is_bot, &address);
  else {
    double values[3] = {client, first_time ? 1 : 0, is_bot ? 1 : 0};
    double denied = call_q3(s, QA_QVM_GAME_EXPORT_GAME_CLIENT_CONNECT, values, 3);
    has_address = denied != 0 && qa_sparse_pointer(&s->memory, (uint64_t)(uint32_t)(int32_t)denied, &address);
  }
  return has_address ? qa_read_string(&s->memory.base, &s->ext, address, 1048576) : NULL;
}
static void client_entry(qa_app_q3_native_guest_source *s, int command, double client) {
  live(s);
  if (s->lifecycle == LIFECYCLE_QUAKE_LIVE) {
    switch (command) {
    case QA_QVM_GAME_EXPORT_GAME_CLIENT_BEGIN: qa_compat_q3_quakelive_game_module_client_begin(&s->quake_live, client); break;
    case QA_QVM_GAME_EXPORT_GAME_CLIENT_USERINFO_CHANGED: qa_compat_q3_quakelive_game_module_client_userinfo_changed(&s->quake_live, client); break;
    case QA_QVM_GAME_EXPORT_GAME_CLIENT_DISCONNECT: qa_compat_q3_quakelive_game_module_client_disconnect(&s->quake_live, client); break;
    case QA_QVM_GAME_EXPORT_GAME_CLIENT_THINK: qa_compat_q3_quakelive_game_module_client_think(&s->quake_live, client); break;
    default: qa_compat_q3_quakelive_game_module_run_frame(&s->quake_live, client); break;
    }
    return;
  }
  double values[1] = {client};
  call_q3(s, command, values, 1);
}
static void n_client_begin(qa_app_q3_game_engine *base, double client) { client_entry(self_of(base), QA_QVM_GAME_EXPORT_GAME_CLIENT_BEGIN, client); }
static void n_client_userinfo_changed(qa_app_q3_game_engine *base, double client) {
  client_entry(self_of(base), QA_QVM_GAME_EXPORT_GAME_CLIENT_USERINFO_CHANGED, client);
}
static void n_client_disconnect(qa_app_q3_game_engine *base, double client) {
  client_entry(self_of(base), QA_QVM_GAME_EXPORT_GAME_CLIENT_DISCONNECT, client);
}
static void client_command_now(qa_app_q3_native_guest_source *s, double client, const char *const *argv, size_t argc) {
  arguments_frame frame;
  qa_cleanup cleanup;
  with_arguments_begin(s, &frame, &cleanup, argv, argc);
  if (s->lifecycle == LIFECYCLE_QUAKE_LIVE) qa_compat_q3_quakelive_game_module_client_command(&s->quake_live, client);
  else {
    double values[1] = {client};
    call_q3(s, QA_QVM_GAME_EXPORT_GAME_CLIENT_COMMAND, values, 1);
  }
  with_arguments_end(&frame, &cleanup);
}
static void n_client_command_async(qa_app_q3_game_engine *base, double client, const char *const *argv, size_t argc) {
  qa_app_q3_native_guest_source *s = self_of(base);
  live(s);
  client_command_now(s, client, argv, argc);
}
static void n_client_command(qa_app_q3_game_engine *base, double client, const char *const *argv, size_t argc) {
  qa_app_q3_native_guest_source *s = self_of(base);
  if (s->phase != PHASE_RUNNING) fail(s->phase == PHASE_CLOSED ? "Native Q3 guest is closed" : "Native Q3 guest has not run GAME_INIT");
  client_command_now(s, client, argv, argc);
}
static void n_client_think(qa_app_q3_game_engine *base, double client) { client_entry(self_of(base), QA_QVM_GAME_EXPORT_GAME_CLIENT_THINK, client); }
static void n_run_frame(qa_app_q3_game_engine *base, double time) { client_entry(self_of(base), QA_QVM_GAME_EXPORT_GAME_RUN_FRAME, time); }
static bool n_console_command(qa_app_q3_game_engine *base, const char *const *argv, size_t argc) {
  qa_app_q3_native_guest_source *s = self_of(base);
  live(s);
  arguments_frame frame;
  qa_cleanup cleanup;
  with_arguments_begin(s, &frame, &cleanup, argv, argc);
  bool handled = s->lifecycle == LIFECYCLE_QUAKE_LIVE ? qa_compat_q3_quakelive_game_module_console_command(&s->quake_live)
                                                       : call_q3(s, QA_QVM_GAME_EXPORT_GAME_CONSOLE_COMMAND, NULL, 0) != 0;
  with_arguments_end(&frame, &cleanup);
  return handled;
}
static void n_bot_frame(qa_app_q3_game_engine *base, double time) {
  qa_app_q3_native_guest_source *s = self_of(base);
  live(s);
  /* The API 10 table has no bot frame export. */
  if (s->lifecycle == LIFECYCLE_QUAKE_LIVE) return;
  double values[1] = {time};
  call_q3(s, QA_QVM_GAME_EXPORT_BOTAI_START_FRAME, values, 1);
}

/* ---- checkpoint: the process image between calls ---- */
static const qa_guest_checkpoint *n_checkpoint(qa_app_q3_game_engine *base) {
  qa_app_q3_native_guest_source *s = self_of(base);
  if (s->phase != PHASE_RUNNING) fail("Native Q3 checkpoint requires an initialized game");
  if (qa_guest_call_runner_depth(s->runner) != 0) fail("Native Q3 checkpoint requires a completed game call");
  if (s->options.host_state == NULL) fail("Native Q3 host has not bound checkpoint services");
  const qa_qvm_host_state *host_state = s->options.host_state;
  qa_qvm_host_checkpoint host = host_state->checkpoint(host_state->ctx);
  free_checkpoint(s);
  s->has_checkpoint = true;
  checkpoint_storage *c = &s->saved;
  size_t count = 0;
  qa_guest_mapping *layout = described_mappings(s, &count);
  c->layout = xcalloc(count, sizeof *c->layout);
  c->regions = xcalloc(count, sizeof *c->regions);
  for (size_t i = 0; i < count; i++) {
    const qa_guest_mapping *mapping = &layout[i];
    c->layout[i].base = (int64_t)mapping->base;
    c->layout[i].byte_length = (double)mapping->byte_length;
    c->layout[i].permissions = qa_guest_permissions_name(mapping->permissions);
    c->layout[i].label = xstrdup(label_of(mapping));
    c->layout_count = i + 1;
  }
  for (size_t i = 0; i < count; i++) {
    const qa_guest_mapping *mapping = &layout[i];
    if (!writable(mapping->permissions) && in_baseline(s, mapping)) continue;
    qa_guest_core_address address = address_at(s, mapping->base);
    uint8_t *bytes = xcalloc(mapping->byte_length, 1);
    qa_guest_memory_region *region = &c->regions[c->region_count++];
    region->base = (int64_t)mapping->base;
    region->permissions = qa_guest_permissions_name(mapping->permissions);
    region->bytes.bytes = bytes;
    region->bytes.length = mapping->byte_length;
    readable_copy(s, address, mapping->permissions, bytes, mapping->byte_length, NULL);
  }
  free(layout);
  qa_sparse_allocator_image(&s->memory, &c->allocator);
  c->has_allocator = true;
  size_t block_count = 0;
  if (s->windows) {
    qa_windows_guest_runtime_heap_state(&s->windows_runtime, &c->blocks, &block_count, &c->windows);
    c->has_windows = true;
  } else
    c->blocks = qa_system_v_guest_runtime_heap_state(&s->system_v, &block_count);
  c->heap.layout = c->layout; c->heap.layout_count = c->layout_count;
  c->heap.allocator = c->allocator;
  c->heap.blocks = c->blocks; c->heap.block_count = block_count;
  c->heap.windows = c->has_windows ? &c->windows : NULL;
  size_t processor_length = qa_integer_register_file_checkpoint(&s->state.registers, NULL, 0);
  c->processor = xcalloc(processor_length, 1);
  qa_integer_register_file_checkpoint(&s->state.registers, c->processor, processor_length);
  c->runtime_bytes = xcalloc(host.state.bytes.length, 1);
  if (host.state.bytes.length != 0) memcpy(c->runtime_bytes, host.state.bytes.bytes, host.state.bytes.length);
  qa_guest_checkpoint *out = &c->checkpoint;
  memset(out, 0, sizeof *out);
  out->kind = QA_GUEST_CHECKPOINT_NATIVE_GUEST;
  out->native_version = 2;
  out->module = s->prepared->module;
  out->random = host.random; out->random_count = host.random_count;
  out->callbacks = host.callbacks; out->callback_count = host.callback_count;
  out->abi = s->abi;
  out->regions = c->regions; out->region_count = c->region_count;
  out->heap = &c->heap;
  out->processor_layout.id = "q3:native-processor";
  out->processor_layout.byte_length = 0;
  out->processor_layout.alignment = 1;
  out->processor_layout.pointer_bytes = s->width;
  out->processor_state.bytes = c->processor; out->processor_state.length = processor_length;
  out->runtime_state.module = host.state.module;
  out->runtime_state.format = host.state.format;
  out->runtime_state.bytes.bytes = c->runtime_bytes; out->runtime_state.bytes.length = host.state.bytes.length;
  return out;
}

/* ---- restore: overlays a saved process image on this freshly started, identical process ---- */
static bool same_text(const char *a, const char *b) { return strcmp(a != NULL ? a : "", b != NULL ? b : "") == 0; }
static noreturn void layout_error(void) { fail("Native Q3 checkpoint memory layout differs from the started module"); }
typedef struct shape { uint64_t base; size_t byte_length; const char *label; qa_guest_permissions permissions; } shape;
static const shape *find_shape(const shape *shapes, size_t count, uint64_t base, size_t byte_length, const char *label) {
  for (size_t i = 0; i < count; i++)
    if (shapes[i].base == base && shapes[i].byte_length == byte_length && same_text(shapes[i].label, label)) return &shapes[i];
  return NULL;
}
static void n_restore(qa_app_q3_game_engine *base, const qa_guest_checkpoint *checkpoint) {
  qa_app_q3_native_guest_source *s = self_of(base);
  if (checkpoint->kind != QA_GUEST_CHECKPOINT_NATIVE_GUEST) fail("Native Q3 restore requires a native-guest checkpoint");
  if (s->phase != PHASE_CREATED) fail("Native Q3 restore requires a freshly started module");
  if (s->options.host_state == NULL) fail("Native Q3 host has not bound checkpoint services");
  const qa_module_identity *saved = &checkpoint->module, *module = &s->prepared->module;
  if (!same_text(saved->id, module->id) || !same_text(saved->digest, module->digest) || !same_text(saved->revision, module->revision) ||
      !same_text(saved->artifact_path, module->artifact_path) || !same_text(checkpoint->abi.kind, s->abi.kind) ||
      !same_text(checkpoint->runtime_state.module.digest, module->digest))
    fail("Native Q3 checkpoint belongs to a different module");
  size_t current_count = 0;
  qa_guest_mapping *current = described_mappings(s, &current_count);
  /* A checkpoint without a heap image (version 1) records a process whose layout is the one startup produces. */
  const qa_native_guest_heap_image *image = checkpoint->heap;
  shape *have = xcalloc(current_count, sizeof *have);
  for (size_t i = 0; i < current_count; i++) {
    have[i].base = current[i].base; have[i].byte_length = current[i].byte_length;
    have[i].label = label_of(&current[i]); have[i].permissions = current[i].permissions;
  }
  size_t wanted_count = image == NULL ? current_count : image->layout_count;
  shape *wanted = xcalloc(wanted_count, sizeof *wanted);
  for (size_t i = 0; i < wanted_count; i++) {
    if (image == NULL) { wanted[i] = have[i]; continue; }
    const qa_guest_mapping_record *record = &image->layout[i];
    wanted[i].base = (uint64_t)record->base; wanted[i].byte_length = (size_t)record->byte_length; wanted[i].label = record->label;
    if (!qa_guest_permissions_parse(record->permissions, &wanted[i].permissions)) layout_error();
  }
  if (image == NULL) {
    size_t writable_count = 0;
    for (size_t i = 0; i < current_count; i++) writable_count += writable(current[i].permissions);
    if (writable_count != checkpoint->region_count) layout_error();
  }
  for (size_t i = 0; i < checkpoint->region_count; i++)
    for (size_t j = 0; j < i; j++)
      if (checkpoint->regions[i].base == checkpoint->regions[j].base) layout_error();
  /* TS Map construction keeps the last entry for a repeated shape; shapes are unique per mapping base. */
  for (size_t i = 0; i < current_count; i++)
    if (find_shape(wanted, wanted_count, have[i].base, have[i].byte_length, have[i].label) == NULL)
      qa_sparse_unmap(&s->memory, address_at(s, have[i].base), have[i].byte_length);
  bool *created = xcalloc(wanted_count, sizeof *created);
  for (size_t i = 0; i < wanted_count; i++) {
    const shape *found = find_shape(have, current_count, wanted[i].base, wanted[i].byte_length, wanted[i].label);
    created[i] = found == NULL;
    if (found == NULL) {
      qa_guest_map_options map;
      memset(&map, 0, sizeof map);
      map.base = wanted[i].base; map.byte_length = wanted[i].byte_length; map.permissions = QA_PERM_READ_WRITE; map.label = wanted[i].label;
      qa_sparse_map(&s->memory, &map);
    } else if (found->permissions != wanted[i].permissions)
      qa_sparse_protect(&s->memory, address_at(s, wanted[i].base), wanted[i].byte_length, wanted[i].permissions);
  }
  for (size_t r = 0; r < checkpoint->region_count; r++) {
    const qa_guest_memory_region *region = &checkpoint->regions[r];
    const shape *target = NULL;
    for (size_t i = 0; i < wanted_count; i++) if (wanted[i].base == (uint64_t)region->base) target = &wanted[i];
    qa_guest_permissions permissions;
    if (target == NULL || !qa_guest_permissions_parse(region->permissions, &permissions) || target->permissions != permissions ||
        target->byte_length != region->bytes.length)
      layout_error();
  }
  for (size_t i = 0; i < wanted_count; i++) {
    const qa_guest_memory_region *region = NULL;
    for (size_t r = 0; r < checkpoint->region_count; r++)
      if ((uint64_t)checkpoint->regions[r].base == wanted[i].base) region = &checkpoint->regions[r];
    if (region == NULL) {
      if (created[i] || (writable(wanted[i].permissions) && image == NULL)) layout_error();
      continue;
    }
    qa_guest_core_address address = address_at(s, wanted[i].base);
    if (created[i]) {
      qa_sparse_write(&s->memory, address, region->bytes.bytes, region->bytes.length);
      if (wanted[i].permissions != QA_PERM_READ_WRITE) qa_sparse_protect(&s->memory, address, wanted[i].byte_length, wanted[i].permissions);
    } else
      readable_copy(s, address, wanted[i].permissions, NULL, region->bytes.length, region->bytes.bytes);
  }
  free(created); free(wanted); free(have); free(current);
  if (image != NULL) {
    qa_sparse_restore_allocator(&s->memory, &image->allocator);
    if (s->windows) {
      if (image->windows == NULL) fail("Native Q3 checkpoint carries no Windows heap tables");
      qa_windows_guest_runtime_restore_heap_state(&s->windows_runtime, image->blocks, image->block_count, image->windows);
    } else
      qa_system_v_guest_runtime_restore_heap_state(&s->system_v, image->blocks, image->block_count);
  }
  qa_integer_register_file_restore(&s->state.registers, checkpoint->processor_state.bytes, checkpoint->processor_state.length);
  s->phase = PHASE_RUNNING;
  qa_qvm_host_checkpoint host;
  memset(&host, 0, sizeof host);
  host.state = checkpoint->runtime_state;
  host.random = checkpoint->random; host.random_count = checkpoint->random_count;
  host.callbacks = checkpoint->callbacks; host.callback_count = checkpoint->callback_count;
  s->options.host_state->restore(s->options.host_state->ctx, &host);
}

/* ---- retire / close ---- */
static void detach(void *arg) {
  qa_app_q3_native_guest_source *s = arg;
  if (s->windows) {
    qa_windows_initialize_options options = {s->context, s->budget};
    qa_windows_guest_runtime_detach(&s->windows_runtime, &s->pe, &options);
  } else {
    qa_system_v_initialize_options options = {s->context, s->budget};
    qa_system_v_guest_runtime_finalize(&s->system_v, &s->elf, &options);
  }
}
static void retire(qa_app_q3_native_guest_source *s) {
  if (s->phase == PHASE_CLOSED) return;
  if (qa_guest_call_runner_depth(s->runner) != 0) fail("Native Q3 guest retirement requires a completed game call");
  qa_caught_error first, caught;
  bool failed = false;
  qa_guest_call_runner_recover(s->runner);
  s->phase = PHASE_CLOSED;
  if (!qa_try(detach, s, &caught)) { failed = true; first = caught; }
  if (!close_files(s, &caught) && !failed) { failed = true; first = caught; }
  if (!qa_try(release_memory_step, s, &caught) && !failed) { failed = true; first = caught; }
  if (failed) qa_raise("AggregateError", 0, 0, "Native Q3 guest retirement failed: %s", first.message);
}
static void n_retire(qa_app_q3_game_engine *base) { retire(self_of(base)); }
static void shutdown_step(void *arg) {
  qa_app_q3_native_guest_source *s = arg;
  qa_guest_call_runner_recover(s->runner);
  lifecycle_shutdown(s, false);
}
static void retire_step(void *arg) { retire(arg); }
void qa_app_q3_native_guest_source_close(qa_app_q3_native_guest_source *s) {
  if (s->phase == PHASE_CLOSED) return;
  bool initialized = s->phase == PHASE_RUNNING, failed = false;
  qa_caught_error first, caught;
  if (initialized && !qa_try(shutdown_step, s, &caught)) { failed = true; first = caught; }
  if (!qa_try(retire_step, s, &caught) && !failed) { failed = true; first = caught; }
  if (failed) qa_raise("AggregateError", 0, 0, "Native Q3 guest shutdown failed: %s", first.message);
}
static void n_destroy(qa_app_q3_game_engine *base) { destroy_source(self_of(base)); }

static const qa_app_q3_game_engine_ops native_ops = {
  n_active, n_initialize, n_shutdown, n_client_connect, n_client_begin, n_client_userinfo_changed, n_client_disconnect,
  n_client_command_async, n_client_command, n_client_think, n_run_frame, n_console_command, n_bot_frame,
  n_checkpoint, n_restore, n_retire, n_destroy,
};
