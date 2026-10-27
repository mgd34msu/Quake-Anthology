/* Port of src/guest/runtime/system-v/runtime.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/system_v/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"
#include "qa/guest/runtime/system_v/cxx.h"
#include "qa/guest/runtime/system_v/libc.h"

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
static char *xstrdup(const char *text) {
  char *copy = xmalloc(strlen(text) + 1);
  strcpy(copy, text);
  return copy;
}
static void plain_error(const char *message) { qa_range_error("%s", message); }
static void type_error(const char *message) { qa_range_error("%s", message); }
static qa_guest_address off(const qa_mapped_guest_memory *m, qa_guest_address a, int64_t d) {
  return qa_memory_offset(m, a, d);
}
static bool streq_or_null(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

typedef struct symbol_entry {
  char *key; /* owned */
  char *library, *name, *version; /* owned, version NULL = TS null */
  qa_system_v_support support;
  int64_t reached;
  qa_guest_import_resolution resolution;
  bool has_byte_length;
  size_t byte_length;
  qa_guest_host_callback callback; /* owned storage for host resolutions */
  char *callback_id;               /* owned */
  qa_abi_value_layout *layouts;    /* owned */
  qa_system_v_implementation implementation;
} symbol_entry;
typedef struct lifecycle_entry {
  qa_elf_guest_image *image;
  qa_system_v_lifecycle_state state;
} lifecycle_entry;
typedef struct heap_entry { qa_guest_address address; size_t size; } heap_entry;
typedef struct tls_block {
  qa_guest_module_identity module;
  uint64_t module_id;
  qa_guest_address address;
  size_t byte_length;
} tls_block;
typedef struct destructor_entry {
  qa_guest_address target;
  bool has_argument; qa_guest_address argument;
  bool has_dso; qa_guest_address dso;
  bool called;
} destructor_entry;

#define SYMBOLS(rt) ((symbol_entry **)(rt)->symbols)
#define IMAGES(rt) ((lifecycle_entry *)(rt)->images)
#define HEAP(rt) ((heap_entry *)(rt)->heap)
#define TLS_BLOCKS(rt) ((tls_block *)(rt)->tls)
#define DESTRUCTORS(rt) ((destructor_entry *)(rt)->destructors)

static void grow(void **array, size_t *cap, size_t count, size_t element) {
  if (count < *cap) return;
  *cap = *cap == 0 ? 8 : *cap * 2;
  *array = xrealloc(*array, *cap * element);
}
static char *make_key(const char *library, const char *name, const char *version) {
  size_t len = strlen(library) + strlen(name) + (version != NULL ? strlen(version) : 0) + 3;
  char *key = xmalloc(len);
  (void)snprintf(key, len, "%s:%s@%s", library, name, version != NULL ? version : "");
  return key;
}
static symbol_entry *find_symbol(qa_system_v_guest_runtime *rt, const char *key) {
  for (size_t i = 0; i < rt->symbol_count; i++)
    if (strcmp(SYMBOLS(rt)[i]->key, key) == 0) return SYMBOLS(rt)[i];
  return NULL;
}
static qa_guest_address rt_allocate_ext(qa_system_v_guest_runtime *rt, size_t byte_length,
                                        bool has_alignment, uint64_t alignment,
                                        bool has_permissions, qa_guest_permissions permissions,
                                        const char *label) {
  return qa_gmem_allocate(rt->options.memory_ext, byte_length, has_alignment, alignment,
                          has_permissions, permissions, label);
}

void qa_system_v_guest_runtime_signature(qa_system_v_guest_runtime *runtime,
                                         const qa_abi_storage *parameters, size_t parameter_count,
                                         bool result_is_void, qa_abi_storage result,
                                         qa_abi_value_layout *layouts_out,
                                         qa_guest_call_signature *out) {
  memset(out, 0, sizeof *out);
  if (runtime->options.memory->pointer_bytes == 8) {
    out->abi.kind = "linux-x86-64"; out->abi.image = "elf64";
    out->abi.pointer_bytes = 8; out->abi.call = "system-v-x86-64";
  } else {
    out->abi.kind = "linux-i386"; out->abi.image = "elf32";
    out->abi.pointer_bytes = 4; out->abi.call = "system-v-i386";
  }
  for (size_t i = 0; i < parameter_count; i++) {
    memset(&layouts_out[i], 0, sizeof layouts_out[i]);
    layouts_out[i].is_aggregate = false;
    layouts_out[i].storage = parameters[i];
  }
  out->parameters = layouts_out;
  out->parameter_count = parameter_count;
  out->result_is_void = result_is_void;
  memset(&out->result, 0, sizeof out->result);
  out->result.is_aggregate = false;
  out->result.storage = result;
  out->variadic = false;
}

typedef struct wrap_state { symbol_entry *entry; } wrap_state;
static qa_call_result wrap_invoke(void *ctx, const qa_guest_call_context *context,
                                  const qa_call_value *args, size_t count) {
  wrap_state *state = ctx;
  state->entry->reached++;
  return state->entry->implementation.invoke(state->entry->implementation.ctx, context, args,
                                             count);
}
static symbol_entry *add_function(qa_system_v_guest_runtime *rt, const char *library,
                                  const char *name, const char *version,
                                  const qa_abi_storage *parameters, size_t parameter_count,
                                  bool result_is_void, qa_abi_storage result,
                                  qa_system_v_implementation implementation,
                                  qa_system_v_support support) {
  char *key = make_key(library, name, version);
  if (find_symbol(rt, key) != NULL) qa_range_error("Duplicate System V symbol %s", key);
  symbol_entry *entry = xmalloc(sizeof *entry);
  memset(entry, 0, sizeof *entry);
  entry->key = key;
  entry->library = xstrdup(library);
  entry->name = xstrdup(name);
  entry->version = version != NULL ? xstrdup(version) : NULL;
  entry->support = support;
  entry->implementation = implementation;
  entry->layouts = xmalloc(parameter_count * sizeof *entry->layouts);
  qa_guest_call_signature signature;
  qa_system_v_guest_runtime_signature(rt, parameters, parameter_count, result_is_void, result,
                                      entry->layouts, &signature);
  entry->callback_id = xmalloc(strlen(key) + 16);
  (void)snprintf(entry->callback_id, strlen(key) + 16, "system-v:%s", key);
  wrap_state *wrap = xmalloc(sizeof *wrap);
  wrap->entry = entry;
  entry->callback.id = entry->callback_id;
  entry->callback.signature = signature;
  entry->callback.ctx = wrap;
  entry->callback.invoke = wrap_invoke;
  qa_guest_address address = qa_guest_callback_table_bind(rt->options.callbacks,
                                                          &entry->callback);
  entry->resolution.kind = QA_RESOLUTION_HOST;
  entry->resolution.address = address;
  entry->resolution.callback = &entry->callback;
  grow(&rt->symbols, &rt->symbol_cap, rt->symbol_count, sizeof(symbol_entry *));
  SYMBOLS(rt)[rt->symbol_count++] = entry;
  return entry;
}

typedef struct unavailable_state {
  char *library, *name, *detail; /* owned */
  const char *version;           /* borrowed literal or NULL */
} unavailable_state;
static qa_call_result unavailable_impl(void *ctx, const qa_guest_call_context *context,
                                       const qa_call_value *args, size_t count) {
  unavailable_state *state = ctx;
  (void)context; (void)args; (void)count;
  qa_unsupported_system_v_service(state->library, state->name, state->version, state->detail);
  return qa_call_void();
}

/* Host vtable. */
static int host_get_errno(void *ctx) {
  qa_system_v_guest_runtime *rt = ctx;
  return qa_gmem_read_i32(rt->options.memory, rt->errno_address_);
}
static void host_set_errno(void *ctx, int value) {
  qa_system_v_guest_runtime *rt = ctx;
  qa_gmem_write_i32(rt->options.memory, rt->errno_address_, value);
}
static void host_service(void *ctx, const char *library, const char *name,
                         const char *const *versions, size_t version_count,
                         const qa_abi_storage *parameters, size_t parameter_count,
                         bool result_is_void, qa_abi_storage result,
                         qa_system_v_implementation implementation) {
  qa_system_v_guest_runtime *rt = ctx;
  for (size_t i = 0; i < version_count; i++)
    (void)add_function(rt, library, name, versions[i], parameters, parameter_count,
                       result_is_void, result, implementation, QA_SYSTEM_V_IMPLEMENTED);
}
static void host_data(void *ctx, const char *library, const char *name, const char *version,
                      qa_guest_address address, size_t byte_length) {
  qa_system_v_guest_runtime *rt = ctx;
  qa_memory_check(rt->options.memory, address, byte_length, QA_ACCESS_READ);
  char *key = make_key(library, name, version);
  if (find_symbol(rt, key) != NULL) qa_range_error("Duplicate System V data %s", key);
  symbol_entry *entry = xmalloc(sizeof *entry);
  memset(entry, 0, sizeof *entry);
  entry->key = key;
  entry->library = xstrdup(library);
  entry->name = xstrdup(name);
  entry->version = version != NULL ? xstrdup(version) : NULL;
  entry->support = QA_SYSTEM_V_IMPLEMENTED;
  entry->resolution.kind = QA_RESOLUTION_GUEST;
  entry->resolution.address = address;
  entry->resolution.module = rt->options.module;
  entry->has_byte_length = true;
  entry->byte_length = byte_length;
  grow(&rt->symbols, &rt->symbol_cap, rt->symbol_count, sizeof(symbol_entry *));
  SYMBOLS(rt)[rt->symbol_count++] = entry;
}
static bool host_resolve_address(void *ctx, const char *library, const char *name,
                                 const char *version, qa_guest_address *out) {
  qa_system_v_guest_runtime *rt = ctx;
  char *key = make_key(library, name, version);
  symbol_entry *entry = find_symbol(rt, key);
  free(key);
  if (entry == NULL) return false;
  *out = entry->resolution.address;
  return true;
}
static qa_guest_address host_unavailable(void *ctx, const char *library, const char *name,
                                         const qa_abi_storage *parameters,
                                         size_t parameter_count, bool result_is_void,
                                         qa_abi_storage result, const char *detail) {
  qa_system_v_guest_runtime *rt = ctx;
  unavailable_state *state = xmalloc(sizeof *state);
  state->library = xstrdup(library); state->name = xstrdup(name);
  state->detail = xstrdup(detail); state->version = NULL;
  qa_system_v_implementation impl = {state, unavailable_impl};
  return add_function(rt, library, name, NULL, parameters, parameter_count, result_is_void,
                      result, impl, QA_SYSTEM_V_UNSUPPORTED_FUNCTION)->resolution.address;
}
static qa_guest_address host_allocate(void *ctx, size_t size) {
  qa_system_v_guest_runtime *rt = ctx;
  if (size > 0x10000000) qa_range_error("System V guest allocation exceeds supported size");
  size_t actual = size < 1 ? 1 : size;
  qa_guest_address address = rt_allocate_ext(rt, actual, true, 16, false, QA_PERM_READ_WRITE,
                                             "System V guest heap");
  for (size_t i = 0; i < rt->heap_count; i++)
    if (HEAP(rt)[i].address.byte_offset == address.byte_offset) {
      HEAP(rt)[i].size = actual;
      return address;
    }
  grow(&rt->heap, &rt->heap_cap, rt->heap_count, sizeof(heap_entry));
  HEAP(rt)[rt->heap_count].address = address;
  HEAP(rt)[rt->heap_count].size = actual;
  rt->heap_count++;
  return address;
}
static void host_free(void *ctx, bool has_address, qa_guest_address address) {
  qa_system_v_guest_runtime *rt = ctx;
  if (!has_address) return;
  (void)qa_memory_offset(rt->options.memory, address, 0);
  for (size_t i = 0; i < rt->heap_count; i++) {
    if (HEAP(rt)[i].address.byte_offset == address.byte_offset) {
      qa_gmem_unmap(rt->options.memory_ext, HEAP(rt)[i].address, HEAP(rt)[i].size);
      memmove(&HEAP(rt)[i], &HEAP(rt)[i + 1], (rt->heap_count - i - 1) * sizeof(heap_entry));
      rt->heap_count--;
      return;
    }
  }
  plain_error("System V free of a non-live allocation");
}
static bool host_allocation_size(void *ctx, qa_guest_address address, size_t *out) {
  qa_system_v_guest_runtime *rt = ctx;
  (void)qa_memory_offset(rt->options.memory, address, 0);
  for (size_t i = 0; i < rt->heap_count; i++)
    if (HEAP(rt)[i].address.byte_offset == address.byte_offset) {
      *out = HEAP(rt)[i].size;
      return true;
    }
  return false;
}
static qa_call_result host_invoke(void *ctx, const qa_guest_call_context *context,
                                  qa_guest_address target, const qa_abi_storage *parameters,
                                  size_t parameter_count, bool result_is_void,
                                  qa_abi_storage result, const qa_call_value *arguments,
                                  size_t argument_count) {
  qa_system_v_guest_runtime *rt = ctx;
  qa_abi_value_layout *layouts = xmalloc(parameter_count * sizeof *layouts);
  qa_guest_call_signature signature;
  qa_system_v_guest_runtime_signature(rt, parameters, parameter_count, result_is_void, result,
                                      layouts, &signature);
  qa_guest_call_context nested = *context;
  nested.callback_kind = QA_CALLBACK_REF_NATIVE_GUEST;
  nested.callback_module = context->module;
  nested.callback_address = target;
  nested.callback_abi = signature.abi;
  qa_guest_call_request request;
  memset(&request, 0, sizeof request);
  request.target = target;
  request.signature = signature;
  request.arguments = arguments;
  request.argument_count = argument_count;
  request.context = nested;
  request.instruction_budget = rt->budget;
  qa_call_result value = qa_guest_call_runner_invoke(
      qa_system_v_guest_runtime_runner(rt), &request);
  free(layouts);
  return value;
}
static void host_register_destructor(void *ctx, qa_guest_address target, bool has_argument,
                                     qa_guest_address argument, bool has_dso,
                                     qa_guest_address dso) {
  qa_system_v_guest_runtime *rt = ctx;
  qa_memory_check(rt->options.memory, target, 1, QA_ACCESS_EXECUTE);
  grow(&rt->destructors, &rt->destructor_cap, rt->destructor_count, sizeof(destructor_entry));
  destructor_entry *entry = &DESTRUCTORS(rt)[rt->destructor_count++];
  entry->target = target;
  entry->has_argument = has_argument; entry->argument = argument;
  entry->has_dso = has_dso; entry->dso = dso;
  entry->called = false;
}
static void host_finalize_destructors(void *ctx, const qa_guest_call_context *context,
                                      bool has_dso, qa_guest_address dso) {
  qa_system_v_guest_runtime *rt = ctx;
  for (;;) {
    destructor_entry *found = NULL;
    for (size_t i = rt->destructor_count; i-- > 0;) {
      destructor_entry *entry = &DESTRUCTORS(rt)[i];
      if (!entry->called &&
          (!has_dso || (entry->has_dso && entry->dso.byte_offset == dso.byte_offset))) {
        found = entry;
        break;
      }
    }
    if (found == NULL) return;
    found->called = true;
    qa_abi_storage params[] = {QA_STORAGE_POINTER};
    qa_call_value invoke_args[] = {qa_call_pointer(found->has_argument, found->argument)};
    qa_call_result result = host_invoke(rt, context, found->target, params, 1, true,
                                        QA_STORAGE_INT32, invoke_args, 1);
    qa_call_value_free(&result);
  }
}
static qa_guest_address host_tls_address(void *ctx, uint64_t module_id, int64_t offset) {
  return qa_system_v_guest_runtime_tls_address(ctx, module_id, offset);
}

const char *qa_system_v_lifecycle_state_name(qa_system_v_lifecycle_state state) {
  switch (state) {
    case QA_SV_LOADED: return "loaded";
    case QA_SV_INITIALIZING: return "initializing";
    case QA_SV_INITIALIZED: return "initialized";
    case QA_SV_FINALIZING: return "finalizing";
    case QA_SV_FINALIZED: return "finalized";
    case QA_SV_FAILED: return "failed";
  }
  return "";
}
qa_guest_call_runner *qa_system_v_guest_runtime_runner(qa_system_v_guest_runtime *runtime) {
  if (runtime->runner_or_null == NULL)
    plain_error("Attach the shared GuestCallRunner before System V execution");
  return runtime->runner_or_null;
}
size_t qa_system_v_guest_runtime_image_count(const qa_system_v_guest_runtime *runtime) {
  return runtime->image_count;
}
qa_elf_guest_image *qa_system_v_guest_runtime_image(const qa_system_v_guest_runtime *runtime,
                                                    size_t index) {
  return ((lifecycle_entry *)runtime->images)[index].image;
}
size_t qa_system_v_guest_runtime_coverage_count(const qa_system_v_guest_runtime *runtime) {
  return runtime->symbol_count;
}
qa_system_v_import_coverage qa_system_v_guest_runtime_coverage(
    const qa_system_v_guest_runtime *runtime, size_t index) {
  symbol_entry *entry = ((symbol_entry **)runtime->symbols)[index];
  qa_system_v_import_coverage coverage;
  coverage.library = entry->library;
  coverage.name = entry->name;
  coverage.version = entry->version;
  coverage.support = entry->support;
  coverage.reached = entry->reached;
  return coverage;
}
static const char *const libraries[] = {"libc.so.6", "libm.so.6", "libstdc++.so.6",
                                        "libgcc_s.so.1", "ld-linux.so.2", "ld-linux-x86-64.so.2"};
const char **qa_system_v_guest_runtime_dependencies(const qa_system_v_guest_runtime *runtime,
                                                    size_t *out_count) {
  size_t capacity = 6 + runtime->image_count;
  const char **out = xmalloc(capacity * sizeof *out);
  size_t count = 0;
  for (size_t i = 0; i < 6; i++) out[count++] = libraries[i];
  for (size_t i = 0; i < runtime->image_count; i++) {
    const char *soname = ((lifecycle_entry *)runtime->images)[i].image->elf.soname;
    if (soname == NULL) continue;
    bool present = false;
    for (size_t j = 0; j < count; j++) if (strcmp(out[j], soname) == 0) present = true;
    if (!present) out[count++] = soname;
  }
  *out_count = count;
  return out;
}
void qa_system_v_guest_runtime_attach_runner(qa_system_v_guest_runtime *runtime,
                                             qa_guest_call_runner *runner) {
  qa_guest_call_runner_options options = qa_guest_call_runner_options_of(runner);
  if (options.cpu->memory != runtime->options.memory ||
      options.callbacks != runtime->options.callbacks)
    type_error("System V runner belongs to another process");
  runtime->runner_or_null = runner;
  runtime->host.runner = runner;
  if (runtime->options.memory->pointer_bytes == 8)
    options.cpu->state->segments[QA_SEG_FS].base = runtime->thread_pointer.byte_offset;
  else
    options.cpu->state->segments[QA_SEG_GS].base = runtime->thread_pointer.byte_offset;
}

static qa_guest_import_resolution unresolved(const qa_guest_import *import_, const char *detail) {
  qa_guest_import_resolution resolution;
  memset(&resolution, 0, sizeof resolution);
  resolution.kind = QA_RESOLUTION_UNRESOLVED;
  resolution.import_ = *import_;
  resolution.detail = detail;
  return resolution;
}
qa_guest_import_resolution qa_system_v_guest_runtime_resolve(
    qa_system_v_guest_runtime *runtime, const qa_guest_import *import_,
    const qa_guest_image *requesting) {
  if (import_->symbol.is_ordinal)
    return unresolved(import_, "ELF has no ordinal import namespace");
  const char *name = import_->symbol.name;
  const char *version = import_->symbol.version;
  for (size_t i = 0; i < runtime->image_count; i++) {
    qa_elf_guest_image *image = IMAGES(runtime)[i].image;
    if (&image->image == requesting) continue;
    if (import_->library[0] != '\0' && !streq_or_null(image->elf.soname, import_->library))
      continue;
    for (size_t e = 0; e < image->image.export_count; e++) {
      qa_guest_export *exported = &image->image.exports[e];
      if (exported->symbol.is_ordinal) continue;
      if (strcmp(exported->symbol.name, name) != 0) continue;
      if (!streq_or_null(exported->symbol.version, version)) continue;
      if (!exported->is_forward) {
        qa_guest_import_resolution resolution;
        memset(&resolution, 0, sizeof resolution);
        resolution.kind = QA_RESOLUTION_GUEST;
        resolution.address = exported->address;
        resolution.module = image->image.module;
        return resolution;
      }
    }
  }
  symbol_entry *provider = NULL;
  if (import_->library[0] == '\0') {
    for (size_t i = 0; i < runtime->symbol_count; i++)
      if (strcmp(SYMBOLS(runtime)[i]->name, name) == 0 &&
          streq_or_null(SYMBOLS(runtime)[i]->version, version)) {
        provider = SYMBOLS(runtime)[i];
        break;
      }
  } else {
    char *key = make_key(import_->library, name, version);
    provider = find_symbol(runtime, key);
    free(key);
  }
  if (provider != NULL) return provider->resolution;
  bool local = false;
  if (import_->library[0] == '\0' && requesting != NULL)
    for (size_t e = 0; e < requesting->export_count; e++)
      if (!requesting->exports[e].symbol.is_ordinal &&
          strcmp(requesting->exports[e].symbol.name, name) == 0)
        local = true;
  if (import_->weak || local)
    return unresolved(import_, "No provider in this process; preserve weak-null or local definition");
  if (strncmp(name, "_ZTV", 4) == 0 || strncmp(name, "_ZTI", 4) == 0) {
    size_t label_len = strlen(name) + (version != NULL ? strlen(version) : 0) + 32;
    char *label = xmalloc(label_len);
    (void)snprintf(label, label_len, "Unsupported System V data %s@%s", name,
                   version != NULL ? version : "");
    qa_guest_address address = rt_allocate_ext(
        runtime, 4096, true, 4096, true, runtime->loading ? QA_PERM_READ : QA_PERM_NONE, label);
    symbol_entry *entry = xmalloc(sizeof *entry);
    memset(entry, 0, sizeof *entry);
    entry->key = make_key(import_->library, name, version);
    entry->library = xstrdup(import_->library);
    entry->name = xstrdup(name);
    entry->version = version != NULL ? xstrdup(version) : NULL;
    entry->support = QA_SYSTEM_V_UNSUPPORTED_DATA;
    entry->resolution.kind = QA_RESOLUTION_GUEST;
    entry->resolution.address = address;
    entry->resolution.module = runtime->options.module;
    symbol_entry *existing = find_symbol(runtime, entry->key);
    if (existing != NULL) {
      qa_guest_import_resolution resolution = entry->resolution;
      existing->support = QA_SYSTEM_V_UNSUPPORTED_DATA;
      existing->resolution = resolution;
      existing->has_byte_length = false;
      free(entry->key); free(entry->library); free(entry->name); free(entry->version);
      free(entry);
      return resolution;
    }
    grow(&runtime->symbols, &runtime->symbol_cap, runtime->symbol_count,
         sizeof(symbol_entry *));
    SYMBOLS(runtime)[runtime->symbol_count++] = entry;
    return entry->resolution;
  }
  unavailable_state *state = xmalloc(sizeof *state);
  state->library = xstrdup(import_->library);
  state->name = xstrdup(name);
  state->detail = xstrdup("no implementation for this exact symbol version");
  state->version = version;
  /* version borrows the import's string, as the TS closure captures it. */
  qa_system_v_implementation impl = {state, unavailable_impl};
  return add_function(runtime, import_->library, name, version, NULL, 0, true,
                      QA_STORAGE_INT32, impl, QA_SYSTEM_V_UNSUPPORTED_FUNCTION)->resolution;
}
static qa_guest_import_resolution resolver_resolve(void *ctx, const qa_guest_import *import_,
                                                   const qa_guest_image *requesting) {
  return qa_system_v_guest_runtime_resolve(ctx, import_, requesting);
}
qa_guest_import_resolver qa_system_v_guest_runtime_resolver(qa_system_v_guest_runtime *runtime) {
  qa_guest_import_resolver resolver = {runtime, resolver_resolve};
  return resolver;
}
bool qa_system_v_guest_runtime_resolve_symbol_size(qa_system_v_guest_runtime *runtime,
                                                   const qa_guest_import *import_, size_t *out) {
  if (import_->symbol.is_ordinal) return false;
  char *key = make_key(import_->library, import_->symbol.name, import_->symbol.version);
  symbol_entry *entry = find_symbol(runtime, key);
  free(key);
  if (entry == NULL || !entry->has_byte_length) return false;
  *out = entry->byte_length;
  return true;
}
static bool symbol_size_adapter(void *ctx, const qa_guest_import *import_,
                                const qa_guest_image *requesting, size_t *out) {
  (void)requesting;
  return qa_system_v_guest_runtime_resolve_symbol_size(ctx, import_, out);
}
static bool tls_resolve_adapter(void *ctx, const qa_guest_import *import_,
                                const qa_guest_image *requesting,
                                qa_elf_tls_resolution *out) {
  qa_system_v_guest_runtime *rt = ctx;
  (void)requesting;
  if (import_->symbol.is_ordinal) return false;
  for (size_t i = 0; i < rt->image_count; i++) {
    qa_elf_guest_image *image = IMAGES(rt)[i].image;
    if (import_->library[0] != '\0' && !streq_or_null(image->elf.soname, import_->library))
      continue;
    for (size_t e = 0; e < image->tls_export_count; e++) {
      qa_elf_tls_export *entry = &image->tls_exports[e];
      if (entry->symbol.is_ordinal) continue;
      if (strcmp(entry->symbol.name, import_->symbol.name) != 0) continue;
      if (!streq_or_null(entry->symbol.version, import_->symbol.version)) continue;
      for (size_t t = 0; t < rt->tls_count; t++) {
        tls_block *block = &TLS_BLOCKS(rt)[t];
        if (strcmp(block->module.id, image->image.module.id) != 0) continue;
        if (!streq_or_null(block->module.digest, image->image.module.digest)) continue;
        memset(out, 0, sizeof *out);
        out->module.module_id = block->module_id;
        out->module.has_thread_pointer_offset = true;
        out->module.thread_pointer_offset =
            (int64_t)block->address.byte_offset - (int64_t)rt->thread_pointer.byte_offset;
        out->offset = (int64_t)entry->offset;
        return true;
      }
    }
  }
  return false;
}

void qa_system_v_guest_runtime_load(qa_system_v_guest_runtime *runtime,
                                    const qa_system_v_load_options *options,
                                    qa_elf_guest_image *out_image) {
  if (runtime->loading) plain_error("System V recursive image loading is unsupported");
  qa_elf_inspection_full inspection;
  qa_inspect_elf(&inspection, options->bytes, options->bytes_len);
  qa_elf_segment_info *segment = NULL;
  for (size_t i = 0; i < inspection.segment_count; i++)
    if (inspection.segments[i].type == 7) { segment = &inspection.segments[i]; break; }
  uint64_t module_id = (uint64_t)runtime->tls_count + 1;
  if (module_id > 1024) qa_range_error("System V thread TLS module limit exceeded");
  size_t size = segment != NULL ? segment->memory_size : 0;
  uint64_t alignment = segment == NULL || segment->alignment == 0 ? 1 : segment->alignment;
  if (alignment > 4096)
    qa_range_error("System V static TLS alignment exceeds the thread allocation alignment");
  uint64_t next = (runtime->tls_used + size + alignment - 1) / alignment * alignment;
  if (next > 0x10000) qa_range_error("System V static TLS exceeds thread allocation");
  qa_guest_address block = off(runtime->options.memory, runtime->thread_pointer, -(int64_t)next);
  qa_elf_tls_bindings tls;
  memset(&tls, 0, sizeof tls);
  tls.current.module_id = module_id;
  tls.current.has_thread_pointer_offset = true;
  tls.current.thread_pointer_offset = -(int64_t)next;
  tls.ctx = runtime;
  tls.resolve = tls_resolve_adapter;
  qa_elf_inspection_full_free(&inspection);
  /* Relocation validates provider addresses. No guest code runs while
   * unresolved data reservations are readable (TS comment). */
  for (size_t i = 0; i < runtime->symbol_count; i++)
    if (SYMBOLS(runtime)[i]->support == QA_SYSTEM_V_UNSUPPORTED_DATA)
      qa_gmem_protect(runtime->options.memory_ext, SYMBOLS(runtime)[i]->resolution.address,
                      4096, QA_PERM_READ);
  size_t dependency_count = 0;
  const char **dependencies = qa_system_v_guest_runtime_dependencies(runtime, &dependency_count);
  qa_elf_load_options load_options;
  memset(&load_options, 0, sizeof load_options);
  load_options.bytes = options->bytes;
  load_options.bytes_len = options->bytes_len;
  load_options.module = options->module;
  load_options.memory = runtime->options.memory;
  load_options.memory_ext = runtime->options.memory_ext;
  load_options.load_bias = options->load_bias;
  load_options.resolver = qa_system_v_guest_runtime_resolver(runtime);
  load_options.dependencies = dependencies;
  load_options.dependency_count = dependency_count;
  load_options.tls = &tls;
  load_options.resolve_ctx = runtime;
  load_options.resolve_symbol_size = symbol_size_adapter;
  load_options.unique_symbols = &runtime->unique_symbols;
  runtime->loading = true;
  (void)qa_load_elf(out_image, &load_options);
  runtime->loading = false;
  free((void *)dependencies);
  for (size_t i = 0; i < runtime->symbol_count; i++)
    if (SYMBOLS(runtime)[i]->support == QA_SYSTEM_V_UNSUPPORTED_DATA)
      qa_gmem_protect(runtime->options.memory_ext, SYMBOLS(runtime)[i]->resolution.address,
                      4096, QA_PERM_NONE);
  if (out_image->image.has_tls) {
    qa_mapped_guest_memory *m = runtime->options.memory;
    qa_memory_write(m, block, out_image->image.tls.initialized,
                    out_image->image.tls.initialized_len);
    qa_gmem_write_pointer(m, off(m, runtime->dtv, (int64_t)(module_id * (uint64_t)(2 * m->pointer_bytes))),
                          true, block);
    qa_write_unsigned(m, runtime->dtv, m->pointer_bytes, module_id + 1);
    grow(&runtime->tls, &runtime->tls_cap, runtime->tls_count, sizeof(tls_block));
    TLS_BLOCKS(runtime)[runtime->tls_count].module = out_image->image.module;
    TLS_BLOCKS(runtime)[runtime->tls_count].module_id = module_id;
    TLS_BLOCKS(runtime)[runtime->tls_count].address = block;
    TLS_BLOCKS(runtime)[runtime->tls_count].byte_length = size;
    runtime->tls_count++;
    runtime->tls_used = next;
  }
  grow(&runtime->images, &runtime->image_cap, runtime->image_count, sizeof(lifecycle_entry));
  IMAGES(runtime)[runtime->image_count].image = out_image;
  IMAGES(runtime)[runtime->image_count].state = QA_SV_LOADED;
  runtime->image_count++;
}
qa_guest_address qa_system_v_guest_runtime_tls_address(qa_system_v_guest_runtime *runtime,
                                                       uint64_t module_id, int64_t offset) {
  tls_block *block = NULL;
  for (size_t i = 0; i < runtime->tls_count; i++)
    if (TLS_BLOCKS(runtime)[i].module_id == module_id) block = &TLS_BLOCKS(runtime)[i];
  if (block == NULL || offset < 0 || (uint64_t)offset >= block->byte_length)
    qa_range_error("System V TLS index outside loaded module block");
  qa_mapped_guest_memory *m = runtime->options.memory;
  qa_guest_address dtv = {0};
  bool has_dtv = qa_gmem_read_pointer(m, off(m, runtime->thread_pointer, m->pointer_bytes), &dtv);
  if (!has_dtv ||
      module_id > qa_read_unsigned(m, off(m, dtv, -2 * m->pointer_bytes), m->pointer_bytes))
    plain_error("System V guest DTV has no requested module");
  qa_guest_address address = {0};
  if (!qa_gmem_read_pointer(m, off(m, dtv, (int64_t)(module_id * (uint64_t)(2 * m->pointer_bytes))),
                            &address))
    plain_error("System V guest DTV module is unallocated");
  return off(m, address, offset);
}
static lifecycle_entry *find_lifecycle(qa_system_v_guest_runtime *rt, qa_elf_guest_image *image) {
  for (size_t i = 0; i < rt->image_count; i++)
    if (IMAGES(rt)[i].image == image) return &IMAGES(rt)[i];
  return NULL;
}
static void push_trace(qa_system_v_guest_runtime *rt, qa_elf_guest_image *image, bool finalize,
                       qa_guest_address target) {
  if (rt->lifecycle_trace_count == rt->lifecycle_trace_cap) {
    rt->lifecycle_trace_cap = rt->lifecycle_trace_cap == 0 ? 8 : rt->lifecycle_trace_cap * 2;
    rt->lifecycle_trace = xrealloc(rt->lifecycle_trace,
                                   rt->lifecycle_trace_cap * sizeof *rt->lifecycle_trace);
  }
  rt->lifecycle_trace[rt->lifecycle_trace_count].image = image->image.module;
  rt->lifecycle_trace[rt->lifecycle_trace_count].finalize = finalize;
  rt->lifecycle_trace[rt->lifecycle_trace_count].target = target;
  rt->lifecycle_trace_count++;
}
static void run_initializer(qa_system_v_guest_runtime *rt, qa_elf_guest_image *image,
                            qa_guest_address target, const qa_guest_call_context *context) {
  qa_abi_storage params[] = {QA_STORAGE_INT32, QA_STORAGE_POINTER, QA_STORAGE_POINTER};
  qa_call_value invoke_args[] = {
      qa_call_int32((int32_t)rt->options.argv_count),
      qa_call_pointer(true, rt->argv_address), qa_call_pointer(true, rt->envp_address)};
  qa_call_result result = host_invoke(rt, context, target, params, 3, true, QA_STORAGE_INT32,
                                      invoke_args, 3);
  qa_call_value_free(&result);
  push_trace(rt, image, false, target);
}
void qa_system_v_guest_runtime_initialize(qa_system_v_guest_runtime *runtime,
                                          qa_elf_guest_image *image,
                                          const qa_system_v_initialize_options *options) {
  lifecycle_entry *entry = find_lifecycle(runtime, image);
  if (entry == NULL) plain_error("System V image is not loaded in this process");
  if (entry->state == QA_SV_INITIALIZED || entry->state == QA_SV_INITIALIZING) return;
  if (entry->state != QA_SV_LOADED)
    qa_range_error("Cannot initialize System V image in state %s",
                   qa_system_v_lifecycle_state_name(entry->state));
  runtime->budget = options->instruction_budget;
  entry->state = QA_SV_INITIALIZING;
  for (size_t i = 0; i < image->preinitializer_count; i++)
    run_initializer(runtime, image, image->preinitializers[i], &options->context);
  for (size_t i = 0; i < image->elf.needed_library_count; i++) {
    for (size_t j = 0; j < runtime->image_count; j++) {
      qa_elf_guest_image *other = IMAGES(runtime)[j].image;
      if (other->elf.soname != NULL &&
          strcmp(other->elf.soname, image->elf.needed_libraries[i]) == 0) {
        qa_system_v_guest_runtime_initialize(runtime, other, options);
        break;
      }
    }
  }
  for (size_t i = 0; i < image->image.initializer_count; i++)
    run_initializer(runtime, image, image->image.initializers[i], &options->context);
  entry = find_lifecycle(runtime, image);
  entry->state = QA_SV_INITIALIZED;
}
void qa_system_v_guest_runtime_finalize(qa_system_v_guest_runtime *runtime,
                                        qa_elf_guest_image *image,
                                        const qa_system_v_initialize_options *options) {
  lifecycle_entry *entry = find_lifecycle(runtime, image);
  if (entry != NULL && entry->state == QA_SV_FINALIZED) return;
  if (entry == NULL || entry->state != QA_SV_INITIALIZED)
    plain_error("Only an initialized System V image can finalize");
  runtime->budget = options->instruction_budget;
  entry->state = QA_SV_FINALIZING;
  for (size_t i = 0; i < image->image.finalizer_count; i++) {
    qa_call_result result = host_invoke(runtime, &options->context,
                                        image->image.finalizers[i], NULL, 0, true,
                                        QA_STORAGE_INT32, NULL, 0);
    qa_call_value_free(&result);
    push_trace(runtime, image, true, image->image.finalizers[i]);
  }
  entry = find_lifecycle(runtime, image);
  entry->state = QA_SV_FINALIZED;
}

static qa_guest_address make_strings(qa_system_v_guest_runtime *rt, const char *const *values,
                                     size_t count) {
  qa_mapped_guest_memory *m = rt->options.memory;
  qa_guest_address vector = rt_allocate_ext(rt, (count + 1) * (size_t)m->pointer_bytes, true, 16,
                                            false, QA_PERM_READ_WRITE,
                                            "System V argument vector");
  for (size_t i = 0; i < count; i++) {
    size_t len = 0;
    uint8_t *bytes = qa_string_bytes(values[i], false, &len);
    qa_guest_address address = rt_allocate_ext(rt, len, false, 0, false, QA_PERM_READ_WRITE,
                                               "System V argument string");
    qa_memory_write(m, address, bytes, len);
    free(bytes);
    qa_gmem_write_pointer(m, off(m, vector, (int64_t)i * m->pointer_bytes), true, address);
  }
  return vector;
}
void qa_system_v_guest_runtime_init(qa_system_v_guest_runtime *runtime,
                                    const qa_system_v_runtime_options *options) {
  memset(runtime, 0, sizeof *runtime);
  runtime->options = *options;
  runtime->budget = 100000;
  qa_mapped_guest_memory *m = options->memory;
  if (&options->callbacks->memory->base != m)
    type_error("System V callback table belongs to another memory");
  /* Host vtable first: installers below register through it. */
  qa_system_v_service_host *host = &runtime->host;
  memset(host, 0, sizeof *host);
  host->ctx = runtime;
  host->memory = m;
  host->memory_ext = options->memory_ext;
  if (options->has_capabilities) host->capabilities = options->capabilities;
  host->pointer_storage = m->pointer_bytes == 4 ? QA_STORAGE_UINT32 : QA_STORAGE_UINT64;
  host->signed_pointer_storage = m->pointer_bytes == 4 ? QA_STORAGE_INT32 : QA_STORAGE_INT64;
  host->get_errno = host_get_errno;
  host->set_errno = host_set_errno;
  host->service = host_service;
  host->data = host_data;
  host->resolve_address = host_resolve_address;
  host->unavailable = host_unavailable;
  host->allocate = host_allocate;
  host->free = host_free;
  host->allocation_size = host_allocation_size;
  host->invoke = host_invoke;
  host->register_destructor = host_register_destructor;
  host->finalize_destructors = host_finalize_destructors;
  host->tls_address = host_tls_address;
  qa_elf_unique_symbols_init(&runtime->unique_symbols);
  qa_guest_address tls_area = rt_allocate_ext(runtime, 0x11000, true, 4096, false,
                                              QA_PERM_READ_WRITE,
                                              "System V static TLS and TCB");
  runtime->thread_pointer = off(m, tls_area, 0x10000);
  qa_gmem_write_pointer(m, runtime->thread_pointer, true, runtime->thread_pointer);
  qa_gmem_write_pointer(m, off(m, runtime->thread_pointer, 2 * m->pointer_bytes), true,
                        runtime->thread_pointer);
  qa_guest_address dtv_allocation = rt_allocate_ext(
      runtime, (size_t)(1026 * 2 * m->pointer_bytes), true, 16, false, QA_PERM_READ_WRITE,
      "System V thread dynamic vector");
  runtime->dtv = off(m, dtv_allocation, 2 * m->pointer_bytes);
  qa_write_unsigned(m, dtv_allocation, m->pointer_bytes, 1024);
  qa_write_unsigned(m, runtime->dtv, m->pointer_bytes, 1);
  qa_gmem_write_pointer(m, off(m, runtime->thread_pointer, m->pointer_bytes), true,
                        runtime->dtv);
  runtime->errno_address_ = rt_allocate_ext(runtime, 4, true, 4, false, QA_PERM_READ_WRITE,
                                            "System V thread errno");
  runtime->argv_address = make_strings(runtime, options->argv, options->argv_count);
  runtime->envp_address = make_strings(runtime, options->environment,
                                       options->environment_count);
  runtime->empty_string_representation = rt_allocate_ext(
      runtime, (size_t)(m->pointer_bytes * 4), true, 16, false, QA_PERM_READ_WRITE,
      "GLIBCXX_3.4 shared empty string representation");
  host->empty_string_representation = runtime->empty_string_representation;
  host->thread_pointer = runtime->thread_pointer;
  host->errno_address = runtime->errno_address_;
  qa_system_v_data(host, "libstdc++.so.6", "_ZNSs4_Rep20_S_empty_rep_storageE", "GLIBCXX_3.4",
                   runtime->empty_string_representation, (size_t)(m->pointer_bytes * 4));
  qa_install_libc(host);
  qa_install_cxx(host);
  runtime->iostreams = xmalloc(sizeof *runtime->iostreams);
  qa_system_v_iostreams_init(runtime->iostreams, host);
}
void qa_system_v_guest_runtime_destroy(qa_system_v_guest_runtime *runtime) {
  if (runtime->iostreams != NULL) {
    qa_system_v_iostreams_destroy(runtime->iostreams);
    free(runtime->iostreams);
  }
  qa_elf_unique_symbols_free(&runtime->unique_symbols);
  for (size_t i = 0; i < runtime->symbol_count; i++) {
    symbol_entry *entry = SYMBOLS(runtime)[i];
    free(entry->key); free(entry->library); free(entry->name); free(entry->version);
    free(entry->callback_id); free(entry->layouts);
    free(entry->callback.ctx);
    free(entry);
  }
  free(runtime->symbols);
  free(runtime->images);
  free(runtime->heap);
  free(runtime->tls);
  free(runtime->destructors);
  free(runtime->lifecycle_trace);
  memset(runtime, 0, sizeof *runtime);
}
