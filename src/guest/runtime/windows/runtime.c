/* Port of src/guest/runtime/windows/runtime.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/windows/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/pe/exports.h"
#include "qa/guest/pe/loader.h"
#include "qa/guest/runtime/common/memory.h"
#include "qa/guest/runtime/windows/crt.h"
#include "qa/guest/runtime/windows/kernel.h"
#include "qa/guest/runtime/windows/msvc/streams.h"

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
static void grow(void **array, size_t *cap, size_t count, size_t element) {
  if (count < *cap) return;
  *cap = *cap == 0 ? 8 : *cap * 2;
  *array = xrealloc(*array, *cap * element);
}
/* TS canonical(): basename, lower-cased, with a .dll suffix. Malloc'd. */
static char *canonical(const char *library) {
  const char *base = library;
  for (const char *p = library; *p != '\0'; p++)
    if (*p == '\\' || *p == '/') base = p + 1;
  size_t len = strlen(base);
  char *out = xmalloc(len + 5);
  for (size_t i = 0; i < len; i++)
    out[i] = base[i] >= 'A' && base[i] <= 'Z' ? (char)(base[i] + 32) : base[i];
  out[len] = '\0';
  if (!(len >= 4 && strcmp(out + len - 4, ".dll") == 0)) strcat(out, ".dll");
  return out;
}

typedef struct import_entry {
  char *library, *name; /* owned; library normalized */
  qa_guest_host_callback callback;
  char *callback_id;             /* owned */
  qa_abi_value_layout *layouts;  /* owned */
  qa_guest_address address;
  bool supported;
  int64_t reached, failed;
  char *last_failure; /* owned, NULL = TS null */
  qa_windows_import_implementation implementation;
} import_entry;
typedef struct data_entry { char *library, *name; qa_guest_address address; } data_entry;
typedef struct requested_entry { char *library, *name; } requested_entry;
typedef struct library_entry { char *library; qa_guest_address address; } library_entry;
typedef struct library_ref { uint64_t key; int64_t count; } library_ref;
typedef struct heap_allocation { qa_guest_address address; size_t size; uint64_t heap; }
    heap_allocation;
typedef struct tls_block_entry { qa_pe_image *image; qa_guest_address address; }
    tls_block_entry;

#define IMPORTS(rt) ((import_entry **)(rt)->imports)
#define DATA(rt) ((data_entry *)(rt)->data)
#define REQUESTED(rt) ((requested_entry *)(rt)->requested)
#define LIBRARIES(rt) ((library_entry *)(rt)->libraries)
#define REFS(rt) ((library_ref *)(rt)->library_references)
#define PREPARED(rt) ((qa_pe_image **)(rt)->prepared)
#define INITIALIZED(rt) ((qa_pe_image **)(rt)->initialized)
#define ALLOCATIONS(rt) ((heap_allocation *)(rt)->allocations)
#define TLS_BLOCKS(rt) ((tls_block_entry *)(rt)->tls_blocks)

static import_entry *import_entry_of(qa_windows_guest_runtime *rt, const char *library,
                                     const char *name) {
  for (size_t i = 0; i < rt->import_count; i++)
    if (strcmp(IMPORTS(rt)[i]->library, library) == 0 &&
        strcmp(IMPORTS(rt)[i]->name, name) == 0)
      return IMPORTS(rt)[i];
  return NULL;
}
static bool data_address_of(qa_windows_guest_runtime *rt, const char *library, const char *name,
                            qa_guest_address *out) {
  for (size_t i = 0; i < rt->data_count; i++)
    if (strcmp(DATA(rt)[i].library, library) == 0 && strcmp(DATA(rt)[i].name, name) == 0) {
      *out = DATA(rt)[i].address;
      return true;
    }
  return false;
}
static bool image_list_has(qa_pe_image **list, size_t count, qa_pe_image *image) {
  for (size_t i = 0; i < count; i++) if (list[i] == image) return true;
  return false;
}
static void image_list_add(void **list, size_t *count, size_t *cap, qa_pe_image *image) {
  if (image_list_has((qa_pe_image **)*list, *count, image)) return;
  grow(list, cap, *count, sizeof(qa_pe_image *));
  ((qa_pe_image **)*list)[(*count)++] = image;
}
static void image_list_remove(qa_pe_image **list, size_t *count, qa_pe_image *image) {
  for (size_t i = 0; i < *count; i++)
    if (list[i] == image) {
      memmove(&list[i], &list[i + 1], (*count - i - 1) * sizeof(qa_pe_image *));
      (*count)--;
      return;
    }
}
static void ensure_library_handle(qa_windows_guest_runtime *rt, const char *normalized) {
  for (size_t i = 0; i < rt->library_count; i++)
    if (strcmp(LIBRARIES(rt)[i].library, normalized) == 0) return;
  char *label = xmalloc(strlen(normalized) + 32);
  (void)snprintf(label, strlen(normalized) + 32, "Windows library handle %s", normalized);
  qa_guest_address address = qa_gmem_allocate(rt->options.memory_ext, 16, false, 0, false,
                                              QA_PERM_READ_WRITE, label);
  free(label);
  grow(&rt->libraries, &rt->library_cap, rt->library_count, sizeof(library_entry));
  LIBRARIES(rt)[rt->library_count].library = xstrdup(normalized);
  LIBRARIES(rt)[rt->library_count].address = address;
  rt->library_count++;
}

void qa_windows_guest_runtime_signature(qa_windows_guest_runtime *runtime, const char *library,
                                        const qa_abi_storage *parameters, size_t parameter_count,
                                        bool result_is_void, qa_abi_storage result,
                                        qa_abi_value_layout *layouts_out,
                                        qa_guest_call_signature *out) {
  memset(out, 0, sizeof *out);
  if (runtime->options.memory->pointer_bytes == 8) {
    out->abi.kind = "windows-x86-64"; out->abi.image = "pe32+";
    out->abi.pointer_bytes = 8; out->abi.call = "microsoft-x64";
  } else {
    char *normalized = canonical(library);
    out->abi.kind = "windows-i386"; out->abi.image = "pe32";
    out->abi.pointer_bytes = 4;
    out->abi.call = strcmp(normalized, "kernel32.dll") == 0 ? "stdcall" : "cdecl";
    free(normalized);
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

typedef struct wrap_state { import_entry *entry; } wrap_state;
static qa_call_result wrap_invoke(void *ctx, const qa_guest_call_context *context,
                                  const qa_call_value *args, size_t count) {
  wrap_state *state = ctx;
  state->entry->reached++;
  /* TS catches failures to record failed/lastFailure before rethrowing;
   * a C abort cannot be observed after the fact, so they stay unchanged. */
  return state->entry->implementation.invoke(state->entry->implementation.ctx, context, args,
                                             count);
}
static import_entry *register_import(qa_windows_guest_runtime *rt, const char *library,
                                     const char *name, qa_guest_call_signature signature,
                                     qa_windows_import_implementation implementation,
                                     bool supported) {
  char *normalized = canonical(library);
  if (import_entry_of(rt, normalized, name) != NULL)
    qa_range_error("Windows import already registered: %s!%s", normalized, name);
  import_entry *entry = xmalloc(sizeof *entry);
  memset(entry, 0, sizeof *entry);
  entry->library = normalized;
  entry->name = xstrdup(name);
  entry->supported = supported;
  entry->implementation = implementation;
  entry->layouts = xmalloc(signature.parameter_count * sizeof *entry->layouts);
  for (size_t i = 0; i < signature.parameter_count; i++)
    entry->layouts[i] = signature.parameters[i];
  signature.parameters = entry->layouts;
  entry->callback_id = xmalloc(strlen(normalized) + strlen(name) + 16);
  (void)snprintf(entry->callback_id, strlen(normalized) + strlen(name) + 16, "windows:%s!%s",
                 normalized, name);
  wrap_state *wrap = xmalloc(sizeof *wrap);
  wrap->entry = entry;
  entry->callback.id = entry->callback_id;
  entry->callback.signature = signature;
  entry->callback.ctx = wrap;
  entry->callback.invoke = wrap_invoke;
  entry->address = qa_guest_callback_table_bind(rt->options.callbacks, &entry->callback);
  grow(&rt->imports, &rt->import_cap, rt->import_count, sizeof(import_entry *));
  IMPORTS(rt)[rt->import_count++] = entry;
  ensure_library_handle(rt, normalized);
  return entry;
}

typedef struct unsupported_state { char *library, *name; } unsupported_state;
static qa_call_result unsupported_impl(void *ctx, const qa_guest_call_context *context,
                                       const qa_call_value *args, size_t count) {
  unsupported_state *state = ctx;
  (void)context; (void)args; (void)count;
  qa_unsupported_windows_import(state->library, state->name, NULL);
  return qa_call_void();
}

/* Host vtable. */
static const qa_pe_image *const *host_images(void *ctx, size_t *count) {
  qa_windows_guest_runtime *rt = ctx;
  if (count != NULL) *count = rt->image_count;
  return (const qa_pe_image *const *)rt->images;
}
static uint32_t host_get_last_error(void *ctx) {
  qa_windows_guest_runtime *rt = ctx;
  qa_mapped_guest_memory *m = rt->options.memory;
  return (uint32_t)qa_read_unsigned(m, off(m, rt->teb, m->pointer_bytes == 4 ? 0x34 : 0x68), 4);
}
static void host_set_last_error(void *ctx, uint32_t value) {
  qa_windows_guest_runtime *rt = ctx;
  qa_mapped_guest_memory *m = rt->options.memory;
  qa_write_unsigned(m, off(m, rt->teb, m->pointer_bytes == 4 ? 0x34 : 0x68), 4, value);
}
static void host_register(void *ctx, const char *library, const char *name,
                          qa_guest_call_signature signature,
                          qa_windows_import_implementation implementation) {
  (void)register_import(ctx, library, name, signature, implementation, true);
}
static void host_service(void *ctx, const char *library, const char *name,
                         const qa_abi_storage *parameters, size_t parameter_count,
                         bool result_is_void, qa_abi_storage result,
                         qa_windows_import_implementation implementation) {
  qa_windows_guest_runtime *rt = ctx;
  qa_abi_value_layout *layouts = xmalloc(parameter_count * sizeof *layouts);
  qa_guest_call_signature signature;
  qa_windows_guest_runtime_signature(rt, library, parameters, parameter_count, result_is_void,
                                     result, layouts, &signature);
  (void)register_import(rt, library, name, signature, implementation, true);
  free(layouts);
}
static bool host_allocate(void *ctx, size_t size, uint64_t heap, qa_guest_address *out) {
  return qa_windows_guest_runtime_allocate(ctx, size, heap, out);
}
static bool host_free(void *ctx, bool has_address, qa_guest_address address, uint64_t heap) {
  return qa_windows_guest_runtime_free(ctx, has_address, address, heap);
}
static bool host_allocation_size(void *ctx, qa_guest_address address, uint64_t heap,
                                 size_t *out) {
  return qa_windows_guest_runtime_allocation_size(ctx, address, heap, out);
}
static void host_destroy_heap(void *ctx, uint64_t heap) {
  qa_windows_guest_runtime *rt = ctx;
  /* Collect first: free() removes entries while iterating (TS iterates a
   * live Map view with the same net effect). */
  size_t count = 0;
  qa_guest_address *list = xmalloc(rt->allocation_count * sizeof *list);
  for (size_t i = 0; i < rt->allocation_count; i++)
    if (ALLOCATIONS(rt)[i].heap == heap) list[count++] = ALLOCATIONS(rt)[i].address;
  for (size_t i = 0; i < count; i++) (void)qa_windows_guest_runtime_free(rt, true, list[i], heap);
  free(list);
}
static qa_call_result host_invoke(void *ctx, const qa_guest_call_context *context,
                                  qa_guest_address target, const qa_abi_storage *parameters,
                                  size_t parameter_count, bool result_is_void,
                                  qa_abi_storage result, const qa_call_value *arguments,
                                  size_t argument_count, bool system) {
  qa_windows_guest_runtime *rt = ctx;
  const char *library = system ? "kernel32.dll" : "ucrtbase.dll";
  qa_abi_value_layout *layouts = xmalloc(parameter_count * sizeof *layouts);
  qa_guest_call_signature signature;
  qa_windows_guest_runtime_signature(rt, library, parameters, parameter_count, result_is_void,
                                     result, layouts, &signature);
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
  qa_call_result value = qa_guest_call_runner_invoke(qa_windows_guest_runtime_runner(rt),
                                                     &request);
  free(layouts);
  return value;
}
static bool host_resolve_address(void *ctx, const char *library, const char *name,
                                 qa_guest_address *out) {
  return qa_windows_guest_runtime_resolve_address(ctx, library, name, out);
}
static bool host_library_handle(void *ctx, const char *library, qa_guest_address *out) {
  return qa_windows_guest_runtime_library_handle(ctx, library, out);
}
static bool host_load_library(void *ctx, const char *library, qa_guest_address *out) {
  return qa_windows_guest_runtime_load_library(ctx, library, out);
}
static bool host_free_library(void *ctx, qa_guest_address handle) {
  return qa_windows_guest_runtime_free_library(ctx, handle);
}
static const char *host_library_name(void *ctx, qa_guest_address handle) {
  qa_windows_guest_runtime *rt = ctx;
  for (size_t i = 0; i < rt->library_count; i++)
    if (LIBRARIES(rt)[i].address.byte_offset == handle.byte_offset)
      return LIBRARIES(rt)[i].library;
  for (size_t i = 0; i < rt->image_count; i++)
    if (rt->images[i]->image.base.byte_offset == handle.byte_offset)
      return rt->images[i]->image.module.artifact_path;
  return NULL;
}

qa_guest_call_runner *qa_windows_guest_runtime_runner(qa_windows_guest_runtime *runtime) {
  if (runtime->runner_or_null == NULL)
    plain_error("Attach the shared GuestCallRunner before Windows execution");
  return runtime->runner_or_null;
}
static bool coverage_visible(qa_windows_guest_runtime *rt, import_entry *entry) {
  if (entry->reached > 0) return true;
  for (size_t i = 0; i < rt->requested_count; i++)
    if (strcmp(REQUESTED(rt)[i].library, entry->library) == 0 &&
        strcmp(REQUESTED(rt)[i].name, entry->name) == 0)
      return true;
  return false;
}
size_t qa_windows_guest_runtime_coverage_count(const qa_windows_guest_runtime *runtime) {
  size_t count = 0;
  for (size_t i = 0; i < runtime->import_count; i++)
    if (coverage_visible((qa_windows_guest_runtime *)runtime,
                         ((import_entry **)runtime->imports)[i]))
      count++;
  return count;
}
qa_windows_import_coverage qa_windows_guest_runtime_coverage(
    const qa_windows_guest_runtime *runtime, size_t index) {
  size_t seen = 0;
  for (size_t i = 0; i < runtime->import_count; i++) {
    import_entry *entry = ((import_entry **)runtime->imports)[i];
    if (!coverage_visible((qa_windows_guest_runtime *)runtime, entry)) continue;
    if (seen++ == index) {
      qa_windows_import_coverage coverage;
      coverage.library = entry->library;
      coverage.name = entry->name;
      coverage.supported = entry->supported;
      coverage.reached = entry->reached;
      coverage.failed = entry->failed;
      coverage.last_failure = entry->last_failure;
      return coverage;
    }
  }
  qa_windows_import_coverage empty;
  memset(&empty, 0, sizeof empty);
  return empty;
}
void qa_windows_guest_runtime_attach_runner(qa_windows_guest_runtime *runtime,
                                            qa_guest_call_runner *runner) {
  qa_guest_call_runner_options options = qa_guest_call_runner_options_of(runner);
  if (options.cpu->memory != runtime->options.memory ||
      options.callbacks != runtime->options.callbacks)
    type_error("Windows runner belongs to different guest state");
  runtime->runner_or_null = runner;
  runtime->host.runner = runner;
  qa_guest_processor_state *state = options.cpu->state;
  qa_mapped_guest_memory *m = runtime->options.memory;
  uint64_t stack = qa_integer_register_file_read(&state->registers, QA_REG_RSP,
                                                 m->pointer_bytes == 4 ? 32 : 64, false);
  size_t mapping_count = 0;
  const qa_guest_mapping *mappings = qa_gmem_mappings(runtime->options.memory_ext,
                                                      &mapping_count);
  const qa_guest_mapping *found = NULL;
  for (size_t i = 0; i < mapping_count; i++)
    if (stack > mappings[i].base && stack <= mappings[i].base + mappings[i].byte_length)
      found = &mappings[i];
  if (found == NULL) plain_error("Windows thread stack must already be mapped");
  qa_write_unsigned(m, off(m, runtime->teb, m->pointer_bytes), m->pointer_bytes,
                    found->base + found->byte_length);
  qa_write_unsigned(m, off(m, runtime->teb, m->pointer_bytes * 2), m->pointer_bytes,
                    found->base);
  if (m->pointer_bytes == 4) state->segments[QA_SEG_FS].base = runtime->teb.byte_offset;
  else state->segments[QA_SEG_GS].base = runtime->teb.byte_offset;
}
qa_guest_address qa_windows_guest_runtime_register_data(qa_windows_guest_runtime *runtime,
                                                        const char *library, const char *name,
                                                        const uint8_t *bytes, size_t len) {
  char *normalized = canonical(library);
  qa_guest_address existing = {0};
  if (data_address_of(runtime, normalized, name, &existing) ||
      import_entry_of(runtime, normalized, name) != NULL)
    qa_range_error("Duplicate Windows data export %s!%s", normalized, name);
  char *label = xmalloc(strlen(normalized) + strlen(name) + 2);
  (void)snprintf(label, strlen(normalized) + strlen(name) + 2, "%s!%s", normalized, name);
  qa_guest_address address = qa_gmem_allocate(runtime->options.memory_ext, len, true, 16,
                                              false, QA_PERM_READ_WRITE, label);
  free(label);
  qa_memory_write(runtime->options.memory, address, bytes, len);
  grow(&runtime->data, &runtime->data_cap, runtime->data_count, sizeof(data_entry));
  DATA(runtime)[runtime->data_count].library = normalized;
  DATA(runtime)[runtime->data_count].name = xstrdup(name);
  DATA(runtime)[runtime->data_count].address = address;
  runtime->data_count++;
  ensure_library_handle(runtime, normalized);
  return address;
}
static const qa_pe_image *find_image_by_library(qa_windows_guest_runtime *rt,
                                                  const char *library) {
  char *normalized = canonical(library);
  const qa_pe_image *found = NULL;
  for (size_t i = 0; i < rt->image_count; i++) {
    char *candidate = canonical(rt->images[i]->image.module.artifact_path);
    bool equal = strcmp(candidate, normalized) == 0;
    free(candidate);
    if (equal) { found = rt->images[i]; break; }
  }
  free(normalized);
  return found;
}
typedef struct lookup_ctx { qa_windows_guest_runtime *rt; } lookup_ctx;
static const qa_pe_image *pe_lookup(void *ctx, const char *library,
                                    const qa_pe_image *requesting) {
  (void)requesting;
  return find_image_by_library(((lookup_ctx *)ctx)->rt, library);
}
qa_guest_import_resolution qa_windows_guest_runtime_resolve(
    qa_windows_guest_runtime *runtime, const qa_guest_import *import_,
    const qa_guest_image *requesting) {
  char ordinal_name[16];
  const char *name;
  if (import_->symbol.is_ordinal) {
    (void)snprintf(ordinal_name, sizeof ordinal_name, "#%d", import_->symbol.ordinal);
    name = ordinal_name;
  } else name = import_->symbol.name;
  char *library = canonical(import_->library);
  bool already_requested = false;
  for (size_t i = 0; i < runtime->requested_count; i++)
    if (strcmp(REQUESTED(runtime)[i].library, library) == 0 &&
        strcmp(REQUESTED(runtime)[i].name, name) == 0)
      already_requested = true;
  if (!already_requested) {
    grow(&runtime->requested, &runtime->requested_cap, runtime->requested_count,
         sizeof(requested_entry));
    REQUESTED(runtime)[runtime->requested_count].library = xstrdup(library);
    REQUESTED(runtime)[runtime->requested_count].name = xstrdup(name);
    runtime->requested_count++;
  }
  qa_guest_import_resolution resolution;
  memset(&resolution, 0, sizeof resolution);
  qa_guest_address data = {0};
  if (data_address_of(runtime, library, name, &data)) {
    resolution.kind = QA_RESOLUTION_GUEST;
    resolution.address = data;
    resolution.module = runtime->options.module;
    free(library);
    return resolution;
  }
  const qa_pe_image *guest = find_image_by_library(runtime, library);
  /* TS compares the PeImage identity; requesting is its guest image. */
  if (guest != NULL && &guest->image != requesting) {
    lookup_ctx lookup = {runtime};
    qa_pe_resolved_export target = qa_resolve_pe_export(guest, import_->symbol, pe_lookup,
                                                        &lookup);
    resolution.kind = QA_RESOLUTION_GUEST;
    resolution.address = target.address;
    resolution.module = target.image->image.module;
    free(library);
    return resolution;
  }
  import_entry *entry = import_entry_of(runtime, library, name);
  if (entry == NULL) {
    unsupported_state *state = xmalloc(sizeof *state);
    state->library = xstrdup(library);
    state->name = xstrdup(name);
    qa_windows_import_implementation impl = {state, unsupported_impl};
    qa_abi_value_layout *layouts = xmalloc(sizeof *layouts);
    qa_guest_call_signature signature;
    qa_windows_guest_runtime_signature(runtime, library, NULL, 0, true, QA_STORAGE_INT32,
                                       layouts, &signature);
    entry = register_import(runtime, library, name, signature, impl, false);
    free(layouts);
  }
  resolution.kind = QA_RESOLUTION_HOST;
  resolution.address = entry->address;
  resolution.callback = &entry->callback;
  free(library);
  return resolution;
}
static qa_guest_import_resolution resolver_resolve(void *ctx, const qa_guest_import *import_,
                                                   const qa_guest_image *requesting) {
  return qa_windows_guest_runtime_resolve(ctx, import_, requesting);
}
qa_guest_import_resolver qa_windows_guest_runtime_resolver(qa_windows_guest_runtime *runtime) {
  qa_guest_import_resolver resolver = {runtime, resolver_resolve};
  return resolver;
}
bool qa_windows_guest_runtime_resolve_address(qa_windows_guest_runtime *runtime,
                                              const char *library, const char *name,
                                              qa_guest_address *out) {
  char *current_library = xstrdup(library);
  char *current_name = xstrdup(name);
  requested_entry *seen = NULL;
  size_t seen_count = 0, seen_cap = 0;
  bool found = false;
  for (int depth = 0; depth < 128 && !found; depth++) {
    char *normalized = canonical(current_library);
    bool already = false;
    for (size_t i = 0; i < seen_count; i++)
      if (strcmp(seen[i].library, normalized) == 0 &&
          strcmp(seen[i].name, current_name) == 0)
        already = true;
    if (already) { free(normalized); break; }
    grow((void **)&seen, &seen_cap, seen_count, sizeof(requested_entry));
    seen[seen_count].library = xstrdup(normalized);
    seen[seen_count].name = xstrdup(current_name);
    seen_count++;
    const qa_pe_image *image = find_image_by_library(runtime, normalized);
    if (image == NULL) {
      import_entry *entry = import_entry_of(runtime, normalized, current_name);
      if (entry != NULL && entry->supported) {
        *out = entry->address;
        found = true;
      } else {
        found = data_address_of(runtime, normalized, current_name, out);
      }
      free(normalized);
      break;
    }
    free(normalized);
    const qa_guest_export *exported = NULL;
    for (size_t i = 0; i < image->image.export_count; i++) {
      const qa_guest_export *candidate = &image->image.exports[i];
      char candidate_name[16];
      const char *candidate_text;
      if (candidate->symbol.is_ordinal) {
        (void)snprintf(candidate_name, sizeof candidate_name, "#%d",
                       candidate->symbol.ordinal);
        candidate_text = candidate_name;
      } else candidate_text = candidate->symbol.name;
      if (strcmp(candidate_text, current_name) == 0) { exported = candidate; break; }
    }
    if (exported == NULL) break;
    if (!exported->is_forward) {
      *out = exported->address;
      found = true;
      break;
    }
    free(current_library);
    current_library = xstrdup(exported->forward_library);
    free(current_name);
    if (exported->forward_symbol.is_ordinal) {
      char ordinal[16];
      (void)snprintf(ordinal, sizeof ordinal, "#%d", exported->forward_symbol.ordinal);
      current_name = xstrdup(ordinal);
    } else current_name = xstrdup(exported->forward_symbol.name);
  }
  for (size_t i = 0; i < seen_count; i++) { free(seen[i].library); free(seen[i].name); }
  free(seen);
  free(current_library);
  free(current_name);
  return found;
}
bool qa_windows_guest_runtime_library_handle(qa_windows_guest_runtime *runtime,
                                             const char *library, qa_guest_address *out) {
  char *normalized = canonical(library);
  const qa_pe_image *image = find_image_by_library(runtime, normalized);
  if (image != NULL) {
    *out = image->image.base;
    free(normalized);
    return true;
  }
  bool any_supported = false;
  for (size_t i = 0; i < runtime->import_count; i++)
    if (strcmp(IMPORTS(runtime)[i]->library, normalized) == 0 && IMPORTS(runtime)[i]->supported)
      any_supported = true;
  size_t data_count = 0;
  for (size_t i = 0; i < runtime->data_count; i++)
    if (strcmp(DATA(runtime)[i].library, normalized) == 0) data_count++;
  bool result = false;
  if (any_supported || data_count > 0)
    for (size_t i = 0; i < runtime->library_count; i++)
      if (strcmp(LIBRARIES(runtime)[i].library, normalized) == 0) {
        *out = LIBRARIES(runtime)[i].address;
        result = true;
      }
  free(normalized);
  return result;
}
bool qa_windows_guest_runtime_load_library(qa_windows_guest_runtime *runtime,
                                           const char *library, qa_guest_address *out) {
  if (!qa_windows_guest_runtime_library_handle(runtime, library, out)) {
    host_set_last_error(runtime, 126);
    return false;
  }
  for (size_t i = 0; i < runtime->library_reference_count; i++)
    if (REFS(runtime)[i].key == out->byte_offset) {
      REFS(runtime)[i].count++;
      return true;
    }
  grow(&runtime->library_references, &runtime->library_reference_cap,
       runtime->library_reference_count, sizeof(library_ref));
  REFS(runtime)[runtime->library_reference_count].key = out->byte_offset;
  REFS(runtime)[runtime->library_reference_count].count = 1;
  runtime->library_reference_count++;
  return true;
}
bool qa_windows_guest_runtime_free_library(qa_windows_guest_runtime *runtime,
                                           qa_guest_address handle) {
  for (size_t i = 0; i < runtime->library_reference_count; i++) {
    if (REFS(runtime)[i].key != handle.byte_offset) continue;
    if (REFS(runtime)[i].count == 1) {
      memmove(&REFS(runtime)[i], &REFS(runtime)[i + 1],
              (runtime->library_reference_count - i - 1) * sizeof(library_ref));
      runtime->library_reference_count--;
    } else REFS(runtime)[i].count--;
    /* Prepared images and built-in services retain their owner's initial reference. */
    return true;
  }
  host_set_last_error(runtime, 6);
  return false;
}
bool qa_windows_guest_runtime_allocate(qa_windows_guest_runtime *runtime, size_t size,
                                       uint64_t heap, qa_guest_address *out) {
  if (size > 0x10000000) {
    host_set_last_error(runtime, 8);
    return false;
  }
  *out = qa_allocate_native_memory(runtime->options.memory_ext, size, "Windows guest heap");
  grow(&runtime->allocations, &runtime->allocation_cap, runtime->allocation_count,
       sizeof(heap_allocation));
  ALLOCATIONS(runtime)[runtime->allocation_count].address = *out;
  ALLOCATIONS(runtime)[runtime->allocation_count].size = size < 1 ? 1 : size;
  ALLOCATIONS(runtime)[runtime->allocation_count].heap = heap;
  runtime->allocation_count++;
  return true;
}
bool qa_windows_guest_runtime_free(qa_windows_guest_runtime *runtime, bool has_address,
                                   qa_guest_address address, uint64_t heap) {
  if (!has_address) return true;
  for (size_t i = 0; i < runtime->allocation_count; i++) {
    if (ALLOCATIONS(runtime)[i].address.byte_offset != address.byte_offset) continue;
    if (ALLOCATIONS(runtime)[i].heap != heap) break;
    heap_allocation allocation = ALLOCATIONS(runtime)[i];
    qa_gmem_unmap(runtime->options.memory_ext, allocation.address,
                  qa_native_allocation_bytes(allocation.size));
    memmove(&ALLOCATIONS(runtime)[i], &ALLOCATIONS(runtime)[i + 1],
            (runtime->allocation_count - i - 1) * sizeof(heap_allocation));
    runtime->allocation_count--;
    return true;
  }
  host_set_last_error(runtime, 87);
  return false;
}
bool qa_windows_guest_runtime_allocation_size(qa_windows_guest_runtime *runtime,
                                              qa_guest_address address, uint64_t heap,
                                              size_t *out) {
  for (size_t i = 0; i < runtime->allocation_count; i++)
    if (ALLOCATIONS(runtime)[i].address.byte_offset == address.byte_offset &&
        ALLOCATIONS(runtime)[i].heap == heap) {
      *out = ALLOCATIONS(runtime)[i].size;
      return true;
    }
  return false;
}

typedef struct cfg_state { qa_windows_guest_runtime *rt; int kind; } cfg_state;
static void validate_cfg(qa_windows_guest_runtime *rt, uint64_t target,
                         const qa_guest_call_context *context) {
  (void)context;
  bool valid = false;
  for (size_t i = 0; i < rt->cfg_target_count; i++)
    if (rt->cfg_targets[i] == target) valid = true;
  if (qa_guest_callback_table_has_bound_trap(rt->options.callbacks, target)) valid = true;
  qa_guest_address address = {0};
  bool has_address = qa_memory_pointer(rt->options.memory, target, &address);
  if (!valid || !has_address) {
    char detail[64];
    (void)snprintf(detail, sizeof detail, "invalid indirect target 0x%llx",
                   (unsigned long long)target);
    qa_unsupported_windows_import("quake-runtime.dll", "control-flow-guard", detail);
  }
  qa_memory_check(rt->options.memory, address, 1, QA_ACCESS_EXECUTE);
}
static qa_call_result cfg_impl(void *ctx, const qa_guest_call_context *context,
                               const qa_call_value *args, size_t count) {
  cfg_state *state = ctx;
  qa_windows_guest_runtime *rt = state->rt;
  int width = rt->options.memory->pointer_bytes;
  if (state->kind == 0) {
    qa_guest_call_runner_options options = qa_guest_call_runner_options_of(
        qa_windows_guest_runtime_runner(rt));
    uint64_t target = qa_integer_register_file_read(
        &options.cpu->state->registers, width == 4 ? QA_REG_RCX : QA_REG_RAX,
        width == 4 ? 32 : 64, false);
    validate_cfg(rt, target, context);
    return qa_call_void();
  }
  if (state->kind == 1) {
    const qa_call_value *arg = count > 0 ? &args[0] : NULL;
    if (arg == NULL || arg->kind != QA_CALL_POINTER)
      type_error("CFG dispatch target missing");
    validate_cfg(rt, arg->pointer_has ? arg->pointer.byte_offset : 0, context);
    return qa_call_void();
  }
  qa_unsupported_windows_import("quake-runtime.dll", "guard-dispatch-check",
                                "i386 CFG dispatch convention is not implemented");
  return qa_call_void();
}
static void prepare_cfg(qa_windows_guest_runtime *rt, qa_pe_image *image) {
  if (!image->has_load_configuration) return;
  qa_pe_load_configuration *config = &image->load_configuration;
  if ((image->pe.dll_characteristics & 0x4000) == 0 || (config->guard_flags & 0x100) == 0)
    return;
  qa_mapped_guest_memory *m = rt->options.memory;
  int width = m->pointer_bytes;
  int table_offset = width == 4 ? 80 : 128;
  if (config->bytes_len < (size_t)(table_offset + width * 2))
    plain_error("Truncated CFG load configuration");
  uint64_t table = qa_read_unsigned(m, off(m, config->address, table_offset), width);
  uint64_t count = qa_read_unsigned(m, off(m, config->address, table_offset + width), width);
  uint32_t extra = config->guard_flags >> 28;
  uint32_t stride = extra + 4;
  if (count > image->image.byte_length / stride) plain_error("Invalid CFG target count");
  qa_guest_address base = {0};
  bool has_base = qa_memory_pointer(m, table, &base);
  if (count != 0 && !has_base) plain_error("Null CFG target table");
  if (has_base) {
    size_t entries_len = (size_t)count * stride;
    uint8_t *entries = xmalloc(entries_len);
    qa_memory_copy(m, base, entries, entries_len);
    int64_t previous = -1;
    for (uint64_t index = 0; index < count; index++) {
      size_t at = (size_t)index * stride;
      uint32_t rva = (uint32_t)entries[at] | ((uint32_t)entries[at + 1] << 8) |
                     ((uint32_t)entries[at + 2] << 16) | ((uint32_t)entries[at + 3] << 24);
      uint8_t flags = extra == 0 ? 0 : entries[at + 4];
      bool tail_nonzero = false;
      for (size_t t = at + 5; t < at + stride; t++) if (entries[t] != 0) tail_nonzero = true;
      if ((int64_t)rva <= previous || (uint64_t)rva >= image->image.byte_length ||
          (flags & ~3) != 0 || tail_nonzero)
        plain_error("Invalid/unsupported CFG function metadata");
      previous = rva;
      if ((flags & 1) != 0 || ((flags & 2) != 0 && (config->guard_flags & 0x8000) != 0))
        continue;
      qa_guest_address address = off(m, image->image.base, rva);
      qa_memory_check(m, address, 1, QA_ACCESS_EXECUTE);
      bool present = false;
      for (size_t t = 0; t < rt->cfg_target_count; t++)
        if (rt->cfg_targets[t] == address.byte_offset) present = true;
      if (!present) {
        grow((void **)&rt->cfg_targets, &rt->cfg_target_cap, rt->cfg_target_count,
             sizeof(uint64_t));
        rt->cfg_targets[rt->cfg_target_count++] = address.byte_offset;
      }
    }
    free(entries);
  }
  if (!rt->has_cfg_check) {
    cfg_state *check_state = xmalloc(sizeof *check_state);
    check_state->rt = rt; check_state->kind = 0;
    qa_windows_import_implementation check_impl = {check_state, cfg_impl};
    qa_windows_service(&rt->host, "quake-runtime.dll", "guard-check", NULL, 0, true,
                       QA_STORAGE_INT32, check_impl);
    rt->has_cfg_check = qa_windows_guest_runtime_resolve_address(
        rt, "quake-runtime.dll", "guard-check", &rt->cfg_check);
    if (width == 8) {
      cfg_state *dispatch_state = xmalloc(sizeof *dispatch_state);
      dispatch_state->rt = rt; dispatch_state->kind = 1;
      qa_windows_import_implementation dispatch_impl = {dispatch_state, cfg_impl};
      qa_abi_storage ptr = QA_STORAGE_POINTER;
      qa_windows_service(&rt->host, "quake-runtime.dll", "guard-dispatch-check", &ptr, 1,
                         true, QA_STORAGE_INT32, dispatch_impl);
      qa_guest_address check = {0};
      if (!qa_windows_guest_runtime_resolve_address(rt, "quake-runtime.dll",
                                                    "guard-dispatch-check", &check))
        plain_error("CFG checker binding missing");
      uint8_t code[] = {0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x48, 0x83, 0xec, 0x20,
                        0x48, 0x89, 0xc1, 0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,
                        0xff, 0xd0, 0x48, 0x83, 0xc4, 0x20, 0x41, 0x59, 0x41, 0x58, 0x5a,
                        0x59, 0x58, 0xff, 0xe0};
      for (int i = 0; i < 8; i++) code[16 + i] = (uint8_t)(check.byte_offset >> (i * 8));
      qa_guest_address address = qa_gmem_allocate(rt->options.memory_ext, sizeof code, true,
                                                  16, false, QA_PERM_READ_WRITE,
                                                  "Guest CFG dispatch instructions");
      qa_memory_write(m, address, code, sizeof code);
      qa_gmem_protect(rt->options.memory_ext, address, sizeof code, QA_PERM_READ_EXECUTE);
      rt->cfg_dispatch = address;
      rt->has_cfg_dispatch = true;
    } else {
      cfg_state *dispatch_state = xmalloc(sizeof *dispatch_state);
      dispatch_state->rt = rt; dispatch_state->kind = 2;
      qa_windows_import_implementation dispatch_impl = {dispatch_state, cfg_impl};
      qa_windows_service(&rt->host, "quake-runtime.dll", "guard-dispatch-check", NULL, 0,
                         true, QA_STORAGE_INT32, dispatch_impl);
      rt->has_cfg_dispatch = qa_windows_guest_runtime_resolve_address(
          rt, "quake-runtime.dll", "guard-dispatch-check", &rt->cfg_dispatch);
    }
  }
  struct { bool has; qa_guest_address slot; bool has_value; qa_guest_address value; } patches[] = {
      {config->has_guard_check, config->guard_check_slot, rt->has_cfg_check, rt->cfg_check},
      {config->has_guard_dispatch, config->guard_dispatch_slot, rt->has_cfg_dispatch,
       rt->cfg_dispatch}};
  for (size_t i = 0; i < 2; i++) {
    if (!patches[i].has || !patches[i].has_value) continue;
    qa_guest_address slot = patches[i].slot;
    size_t mapping_count = 0;
    const qa_guest_mapping *mappings = qa_gmem_mappings(rt->options.memory_ext,
                                                        &mapping_count);
    const qa_guest_mapping *region = NULL;
    for (size_t j = 0; j < mapping_count; j++)
      if (slot.byte_offset >= mappings[j].base &&
          slot.byte_offset + (uint64_t)width <= mappings[j].base + mappings[j].byte_length)
        region = &mappings[j];
    if (region == NULL) plain_error("Unmapped CFG pointer slot");
    qa_guest_permissions original = region->permissions;
    qa_gmem_protect(rt->options.memory_ext, slot, (size_t)width, QA_PERM_READ_WRITE);
    qa_write_pointer(m, slot, true, patches[i].value);
    qa_gmem_protect(rt->options.memory_ext, slot, (size_t)width, original);
  }
}
void qa_windows_guest_runtime_prepare_image(qa_windows_guest_runtime *runtime,
                                            qa_pe_image *image) {
  if (image_list_has(PREPARED(runtime), runtime->prepared_count, image)) return;
  /* The TS address-space identity check has no C counterpart. */
  if (!image_list_has(runtime->images, runtime->image_count, image)) {
    grow((void **)&runtime->images, &runtime->image_cap, runtime->image_count,
         sizeof(qa_pe_image *));
    runtime->images[runtime->image_count++] = image;
  }
  qa_guest_import_resolution *resolutions = NULL;
  size_t resolution_count = 0;
  qa_bind_pe_imports(image, runtime->options.memory, runtime->options.memory_ext,
                     qa_windows_guest_runtime_resolver(runtime), &resolutions,
                     &resolution_count);
  free(resolutions);
  if (image->image.has_tls) {
    if (!image->has_tls_index || runtime->tls_block_count >= 1024)
      plain_error("Invalid Windows static TLS index");
    size_t index = runtime->tls_block_count;
    size_t byte_length = image->image.tls.initialized_len + image->image.tls.zero_fill_bytes;
    if (byte_length < 1) byte_length = 1;
    char *label = xmalloc(strlen(image->image.module.id) + 8);
    (void)snprintf(label, strlen(image->image.module.id) + 8, "%s: TLS",
                   image->image.module.id);
    qa_guest_address block = qa_gmem_allocate(
        runtime->options.memory_ext, byte_length, true,
        image->image.tls.alignment < 16 ? 16 : image->image.tls.alignment, false,
        QA_PERM_READ_WRITE, label);
    free(label);
    qa_memory_write(runtime->options.memory, block, image->image.tls.initialized,
                    image->image.tls.initialized_len);
    qa_write_unsigned(runtime->options.memory, image->tls_index_address, 4, index);
    qa_write_pointer(runtime->options.memory,
                     off(runtime->options.memory, runtime->static_tls,
                         (int64_t)(index * (size_t)runtime->options.memory->pointer_bytes)),
                     true, block);
    grow(&runtime->tls_blocks, &runtime->tls_block_cap, runtime->tls_block_count,
         sizeof(tls_block_entry));
    TLS_BLOCKS(runtime)[runtime->tls_block_count].image = image;
    TLS_BLOCKS(runtime)[runtime->tls_block_count].address = block;
    runtime->tls_block_count++;
  }
  /* The CRT entry initializes its own cookie/complement. The guest loader
   * owns CFG dispatch (TS comment). */
  prepare_cfg(runtime, image);
  image_list_add(&runtime->prepared, &runtime->prepared_count, &runtime->prepared_cap, image);
}
static qa_call_result notification(qa_windows_guest_runtime *rt, qa_pe_image *image,
                                   qa_guest_address target, uint32_t reason,
                                   const qa_windows_initialize_options *options, bool is_void) {
  qa_abi_storage storages[] = {QA_STORAGE_POINTER, QA_STORAGE_UINT32, QA_STORAGE_POINTER};
  qa_abi_value_layout *layouts = xmalloc(3 * sizeof *layouts);
  qa_guest_call_signature signature;
  qa_windows_guest_runtime_signature(rt, "kernel32.dll", storages, 3, is_void,
                                     QA_STORAGE_INT32, layouts, &signature);
  qa_guest_address none = {0};
  qa_call_value invoke_args[] = {qa_call_pointer(true, image->image.base),
                                 qa_call_uint32(reason), qa_call_pointer(false, none)};
  qa_guest_call_context context = options->context;
  context.callback_kind = QA_CALLBACK_REF_NATIVE_GUEST;
  context.callback_module = options->context.module;
  context.callback_address = target;
  context.callback_abi = signature.abi;
  qa_guest_call_request request;
  memset(&request, 0, sizeof request);
  request.target = target;
  request.signature = signature;
  request.arguments = invoke_args;
  request.argument_count = 3;
  request.context = context;
  request.instruction_budget = options->instruction_budget;
  qa_call_result value = qa_guest_call_runner_invoke(qa_windows_guest_runtime_runner(rt),
                                                     &request);
  free(layouts);
  return value;
}
void qa_windows_guest_runtime_initialize(qa_windows_guest_runtime *runtime, qa_pe_image *image,
                                         const qa_windows_initialize_options *options) {
  if (image_list_has(INITIALIZED(runtime), runtime->initialized_count, image)) return;
  qa_windows_guest_runtime_prepare_image(runtime, image);
  runtime->budget = options->instruction_budget;
  if (image->image.has_tls)
    for (size_t i = 0; i < image->image.tls.callback_count; i++) {
      qa_call_result result = notification(runtime, image, image->image.tls.callbacks[i], 1,
                                           options, true);
      qa_call_value_free(&result);
    }
  if (image->image.has_entry_point) {
    qa_call_result result = notification(runtime, image, image->image.entry_point, 1,
                                         options, false);
    bool failed = result.kind != QA_CALL_INT32 || result.number == 0;
    qa_call_value_free(&result);
    if (failed)
      qa_range_error("Windows DLL_PROCESS_ATTACH failed: %s", image->image.module.id);
  }
  image_list_add(&runtime->initialized, &runtime->initialized_count,
                 &runtime->initialized_cap, image);
}
void qa_windows_guest_runtime_detach(qa_windows_guest_runtime *runtime, qa_pe_image *image,
                                     const qa_windows_initialize_options *options) {
  if (!image_list_has(INITIALIZED(runtime), runtime->initialized_count, image)) return;
  runtime->budget = options->instruction_budget;
  if (image->image.has_entry_point) {
    qa_call_result result = notification(runtime, image, image->image.entry_point, 0,
                                         options, false);
    qa_call_value_free(&result);
  }
  if (image->image.has_tls)
    for (size_t i = 0; i < image->image.tls.callback_count; i++) {
      qa_call_result result = notification(runtime, image, image->image.tls.callbacks[i], 0,
                                           options, true);
      qa_call_value_free(&result);
    }
  image_list_remove(INITIALIZED(runtime), &runtime->initialized_count, image);
}

void qa_windows_guest_runtime_init(qa_windows_guest_runtime *runtime,
                                   const qa_windows_runtime_options *options) {
  memset(runtime, 0, sizeof *runtime);
  runtime->options = *options;
  runtime->budget = 1000000;
  qa_mapped_guest_memory *m = options->memory;
  if (&options->callbacks->memory->base != m)
    type_error("Windows callbacks require the same guest memory");
  qa_windows_service_host *host = &runtime->host;
  memset(host, 0, sizeof *host);
  host->ctx = runtime;
  host->memory = m;
  host->memory_ext = options->memory_ext;
  if (options->has_capabilities) host->capabilities = options->capabilities;
  host->pointer_storage = m->pointer_bytes == 4 ? QA_STORAGE_UINT32 : QA_STORAGE_UINT64;
  host->process_id = options->has_process_id ? options->process_id : 1;
  host->thread_id = options->has_thread_id ? options->thread_id : 1;
  host->images = host_images;
  host->get_last_error = host_get_last_error;
  host->set_last_error = host_set_last_error;
  host->register_ = host_register;
  host->service = host_service;
  host->allocate = host_allocate;
  host->free = host_free;
  host->allocation_size = host_allocation_size;
  host->destroy_heap = host_destroy_heap;
  host->invoke = host_invoke;
  host->resolve_address = host_resolve_address;
  host->library_handle = host_library_handle;
  host->load_library = host_load_library;
  host->free_library = host_free_library;
  host->library_name = host_library_name;
  runtime->teb = qa_gmem_allocate(options->memory_ext, 0x2000, true, 4096, false,
                                  QA_PERM_READ_WRITE, "Windows TEB");
  runtime->peb = qa_gmem_allocate(options->memory_ext, 0x1000, true, 4096, false,
                                  QA_PERM_READ_WRITE, "Windows PEB");
  runtime->static_tls = qa_gmem_allocate(options->memory_ext,
                                         (size_t)(m->pointer_bytes * 1024), true, 16, false,
                                         QA_PERM_READ_WRITE, "Windows static TLS vector");
  host->teb = runtime->teb;
  int width = m->pointer_bytes;
  qa_write_pointer(m, off(m, runtime->teb, width == 4 ? 0x18 : 0x30), true, runtime->teb);
  qa_write_pointer(m, off(m, runtime->teb, width == 4 ? 0x30 : 0x60), true, runtime->peb);
  qa_write_pointer(m, off(m, runtime->teb, width == 4 ? 0x2c : 0x58), true,
                   runtime->static_tls);
  qa_write_unsigned(m, runtime->teb, width, width == 4 ? 0xffffffffULL : UINT64_MAX);
  qa_write_unsigned(m, off(m, runtime->teb, width == 4 ? 0x20 : 0x40), width,
                    (uint64_t)host->process_id);
  qa_write_unsigned(m, off(m, runtime->teb, width == 4 ? 0x24 : 0x48), width,
                    (uint64_t)host->thread_id);
  qa_install_kernel(host);
  qa_install_crt(host);
  qa_install_msvc_streams(host);
  /* Microsoft STL xlocale: locale::id contains size_t _Id; its shared counter is int. */
  const uint8_t zeros[8] = {0};
  (void)qa_windows_guest_runtime_register_data(runtime, "msvcp140.dll",
                                               "?_Id_cnt@id@locale@std@@0HA", zeros, 4);
  (void)qa_windows_guest_runtime_register_data(
      runtime, "msvcp140.dll", "?id@?$numpunct@D@std@@2V0locale@2@A", zeros, (size_t)width);
}
void qa_windows_guest_runtime_destroy(qa_windows_guest_runtime *runtime) {
  for (size_t i = 0; i < runtime->import_count; i++) {
    import_entry *entry = IMPORTS(runtime)[i];
    free(entry->library); free(entry->name); free(entry->callback_id);
    free(entry->layouts); free(entry->last_failure);
    free(entry->callback.ctx);
    free(entry);
  }
  free(runtime->imports);
  for (size_t i = 0; i < runtime->data_count; i++) {
    free(DATA(runtime)[i].library); free(DATA(runtime)[i].name);
  }
  free(runtime->data);
  for (size_t i = 0; i < runtime->requested_count; i++) {
    free(REQUESTED(runtime)[i].library); free(REQUESTED(runtime)[i].name);
  }
  free(runtime->requested);
  for (size_t i = 0; i < runtime->library_count; i++) free(LIBRARIES(runtime)[i].library);
  free(runtime->libraries);
  free(runtime->images);
  free(runtime->library_references);
  free(runtime->prepared);
  free(runtime->initialized);
  free(runtime->allocations);
  free(runtime->tls_blocks);
  free(runtime->cfg_targets);
  memset(runtime, 0, sizeof *runtime);
}
