/* Port of src/guest/runtime/windows/msvc/locale.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/windows/msvc/locale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

static const char *const library = "msvcp140.dll";

static void plain_error(const char *message) { qa_range_error("%s", message); }
static qa_guest_core_address at(const qa_mapped_guest_memory *m, qa_guest_core_address a, int64_t o) {
  return qa_memory_offset(m, a, o);
}
static void write_ptr(const qa_mapped_guest_memory *m, qa_guest_core_address a, bool has,
                      qa_guest_core_address v) {
  qa_gmem_write_pointer(m, a, has, v);
}
static bool read_ptr(const qa_mapped_guest_memory *m, qa_guest_core_address a, qa_guest_core_address *out) {
  return qa_gmem_read_pointer(m, a, out);
}

qa_guest_core_address qa_msvc_locale_allocate(qa_msvc_locale *locale, size_t size) {
  qa_guest_core_address result = {0};
  if (!qa_windows_allocate(locale->host, size, 0, &result))
    qa_range_error("MSVC allocation failed");
  return result;
}
void qa_msvc_locale_incref(qa_msvc_locale *locale, qa_guest_core_address facet) {
  qa_mapped_guest_memory *memory = locale->host->memory;
  qa_guest_core_address slot = at(memory, facet, 8);
  qa_memory_write_u32(memory, slot, qa_memory_read_u32(memory, slot) + 1);
}
bool qa_msvc_locale_decref(qa_msvc_locale *locale, qa_guest_core_address facet, qa_guest_core_address *out) {
  qa_mapped_guest_memory *memory = locale->host->memory;
  qa_guest_core_address slot = at(memory, facet, 8);
  uint32_t refs = qa_memory_read_u32(memory, slot);
  if (refs == 0) plain_error("MSVC facet reference underflow");
  qa_memory_write_u32(memory, slot, refs - 1);
  if (refs == 1) { *out = facet; return true; }
  return false;
}
qa_guest_core_address qa_msvc_locale_create(qa_msvc_locale *locale) {
  qa_guest_core_address result = qa_msvc_locale_allocate(locale, 8);
  qa_msvc_locale_incref(locale, locale->global);
  write_ptr(locale->host->memory, result, true, locale->global);
  return result;
}
void qa_msvc_locale_destroy(qa_msvc_locale *locale, const qa_guest_core_call_context *context,
                            bool has_locale, qa_guest_core_address locale_address) {
  (void)context;
  if (!has_locale) return;
  qa_guest_core_address facet = {0};
  if (!read_ptr(locale->host->memory, locale_address, &facet))
    plain_error("Missing locale implementation");
  /* The process C locale has the same reference protocol as guest-created facets. */
  if (facet.byte_offset != locale->global.byte_offset)
    qa_unsupported_windows_import(library, "locale destructor",
                                  "non-C locale implementation destructor is not implemented");
  qa_guest_core_address decref_result = {0};
  if (qa_msvc_locale_decref(locale, facet, &decref_result))
    plain_error("MSVC process locale lost its owner references");
  if (!qa_windows_free(locale->host, true, locale_address, 0))
    plain_error("Invalid MSVC locale allocation");
}
void qa_msvc_locale_lock(qa_msvc_locale *locale, qa_guest_core_address object, int kind) {
  qa_mapped_guest_memory *memory = locale->host->memory;
  qa_gmem_write_i32(memory, object, kind);
  if (kind < 0 || kind >= 8) return; /* _Lockit deliberately ignores out-of-range categories. */
  qa_guest_core_address slot = at(memory, locale->locks, (int64_t)kind * 8);
  uint32_t owner = qa_memory_read_u32(memory, slot);
  qa_guest_core_address depth_slot = at(memory, slot, 4);
  if (owner != 0 && owner != (uint32_t)locale->host->thread_id)
    plain_error("MSVC lock contention requires thread scheduling");
  qa_memory_write_u32(memory, slot, (uint32_t)locale->host->thread_id);
  qa_memory_write_u32(memory, depth_slot, qa_memory_read_u32(memory, depth_slot) + 1);
}
void qa_msvc_locale_unlock(qa_msvc_locale *locale, qa_guest_core_address object) {
  qa_mapped_guest_memory *memory = locale->host->memory;
  int32_t kind = qa_gmem_read_i32(memory, object);
  if (kind < 0 || kind >= 8) return;
  qa_guest_core_address slot = at(memory, locale->locks, (int64_t)kind * 8);
  qa_guest_core_address depth_slot = at(memory, slot, 4);
  uint32_t depth = qa_memory_read_u32(memory, depth_slot);
  if (qa_memory_read_u32(memory, slot) != (uint32_t)locale->host->thread_id || depth == 0)
    plain_error("Unowned MSVC lock release");
  qa_memory_write_u32(memory, depth_slot, depth - 1);
  if (depth == 1) qa_memory_write_u32(memory, slot, 0);
}

typedef struct locale_state {
  qa_msvc_locale *locale;
  int kind;
  qa_guest_core_address word_address; /* _Gettrue/_Getfalse */
} locale_state;
enum {
  MS_INCREF, MS_DECREF, MS_FACET_DTOR, MS_FACET_DELETE, MS_FACET_CTOR, MS_INIT, MS_GETGLOBAL,
  MS_LOCKIT_CTOR, MS_LOCKIT_DTOR, MS_LOCINFO_CTOR, MS_LOCINFO_DTOR, MS_GETWORD, MS_GETLCONV,
  MS_GETCVT
};
static qa_call_result locale_impl(void *ctx, const qa_guest_core_call_context *context,
                                  const qa_call_value *args, size_t count) {
  locale_state *state = ctx;
  qa_msvc_locale *locale = state->locale;
  qa_windows_service_host *host = locale->host;
  qa_mapped_guest_memory *memory = host->memory;
  qa_guest_core_address none = {0};
  switch (state->kind) {
    case MS_INCREF:
      qa_msvc_locale_incref(locale, qa_guest_required_pointer(args, count, 0));
      return qa_call_void();
    case MS_DECREF: {
      qa_guest_core_address out = {0};
      bool has = qa_msvc_locale_decref(locale, qa_guest_required_pointer(args, count, 0), &out);
      return qa_call_pointer(has, out);
    }
    case MS_FACET_DTOR:
      write_ptr(memory, qa_guest_required_pointer(args, count, 0), true, locale->facet_vtable);
      return qa_call_void();
    case MS_FACET_DELETE: {
      qa_guest_core_address facet = qa_guest_required_pointer(args, count, 0);
      if (facet.byte_offset == locale->global.byte_offset)
        qa_unsupported_windows_import(
            library, "locale deletion",
            "the process classic/global locale still owns references");
      write_ptr(memory, facet, true, locale->facet_vtable);
      if ((qa_guest_integer_u64(args, count, 1) & 1u) != 0 &&
          !qa_windows_free(host, true, facet, 0))
        plain_error("Invalid MSVC facet allocation");
      return qa_call_pointer(true, facet);
    }
    case MS_FACET_CTOR: {
      qa_guest_core_address facet = qa_guest_required_pointer(args, count, 0);
      write_ptr(memory, facet, true, locale->facet_vtable);
      qa_memory_write_u32(memory, at(memory, facet, 8),
                          (uint32_t)qa_guest_integer_u64(args, count, 1));
      return qa_call_pointer(true, facet);
    }
    case MS_INIT:
      if ((qa_guest_integer_u64(args, count, 0) & 255u) != 0)
        qa_msvc_locale_incref(locale, locale->global);
      return qa_call_pointer(true, locale->global);
    case MS_GETGLOBAL: return qa_call_pointer(true, locale->global);
    case MS_LOCKIT_CTOR: {
      qa_guest_core_address object = qa_guest_required_pointer(args, count, 0);
      qa_msvc_locale_lock(locale, object, (int)qa_guest_integer(args, count, 1));
      return qa_call_pointer(true, object);
    }
    case MS_LOCKIT_DTOR:
      qa_msvc_locale_unlock(locale, qa_guest_required_pointer(args, count, 0));
      return qa_call_void();
    case MS_LOCINFO_CTOR: {
      qa_guest_core_address object = qa_guest_required_pointer(args, count, 0);
      qa_guest_core_address supplied = {0};
      bool has_supplied = qa_guest_pointer_argument(args, count, 1, &supplied);
      char *requested = NULL;
      if (has_supplied)
        requested = qa_read_string(memory, host->memory_ext, supplied, 1048576);
      if (requested == NULL || (strcmp(requested, "C") != 0 && strcmp(requested, "") != 0)) {
        /* TS: requested is null only when supplied is null, and null !== "C"/"". */
        if (requested != NULL)
          qa_range_error(
              "Unsupported Windows guest import msvcp140.dll!_Locinfo: locale %s is not "
              "implemented", requested);
        qa_unsupported_windows_import(library, "_Locinfo",
                                      "locale null is not implemented");
      }
      free(requested);
      uint8_t zeros[104] = {0};
      qa_memory_write(memory, object, zeros, sizeof zeros);
      qa_msvc_locale_lock(locale, object, 0);
      const struct { int64_t offset; bool wide; } entries[] = {{72, true}, {88, false}};
      for (size_t i = 0; i < 2; i++) {
        size_t len = 0;
        uint8_t *bytes = qa_string_bytes("C", entries[i].wide, &len);
        qa_guest_core_address address = qa_msvc_locale_allocate(locale, len);
        qa_memory_write(memory, address, bytes, len);
        free(bytes);
        write_ptr(memory, at(memory, object, entries[i].offset), true, address);
      }
      return qa_call_pointer(true, object);
    }
    case MS_LOCINFO_DTOR: {
      qa_guest_core_address object = qa_guest_required_pointer(args, count, 0);
      for (int64_t offset = 8; offset <= 88; offset += 16) {
        qa_guest_core_address slot = at(memory, object, offset);
        qa_guest_core_address allocation = {0};
        bool has = read_ptr(memory, slot, &allocation);
        if (!qa_windows_free(host, has, allocation, 0))
          plain_error("Invalid _Locinfo yarn allocation");
        write_ptr(memory, slot, false, none);
      }
      qa_msvc_locale_unlock(locale, object);
      return qa_call_void();
    }
    case MS_GETWORD: return qa_call_pointer(true, state->word_address);
    case MS_GETLCONV: {
      qa_guest_core_address target = {0};
      if (!qa_windows_resolve_address(host, "ucrtbase.dll", "localeconv", &target))
        plain_error("CRT localeconv missing");
      return qa_windows_invoke(host, context, target, NULL, 0, false, QA_STORAGE_POINTER,
                               NULL, 0, false);
    }
    case MS_GETCVT: {
      /* _Cvtvec is returned through the member-function hidden result pointer in RDX. */
      qa_guest_core_address result = qa_guest_required_pointer(args, count, 1);
      uint8_t zeros[44] = {0};
      qa_memory_write(memory, result, zeros, sizeof zeros);
      qa_memory_write_u32(memory, at(memory, result, 4), 1);
      qa_memory_write_u32(memory, at(memory, result, 8), 1);
      return qa_call_pointer(true, result);
    }
    default: break;
  }
  return qa_call_void();
}
static void reg(qa_msvc_locale *locale, int kind, const char *name,
                const qa_abi_storage *parameters, size_t parameter_count, bool result_is_void,
                qa_abi_storage result) {
  locale_state *state = malloc(sizeof *state);
  if (state == NULL) qa_range_error("Guest runtime out of memory");
  state->locale = locale; state->kind = kind;
  memset(&state->word_address, 0, sizeof state->word_address);
  qa_windows_import_implementation impl = {state, locale_impl};
  qa_windows_service(locale->host, library, name, parameters, parameter_count, result_is_void,
                     result, impl);
}

void qa_msvc_locale_init(qa_msvc_locale *locale, qa_windows_service_host *host) {
  memset(locale, 0, sizeof *locale);
  locale->host = host;
  qa_mapped_guest_memory *memory = host->memory;
  locale->locks = qa_gmem_allocate(host->memory_ext, 64, false, 0, false, QA_PERM_READ_WRITE,
                                   "MSVC recursive library locks");
  qa_abi_storage ptr = QA_STORAGE_POINTER, u32 = QA_STORAGE_UINT32, u64 = QA_STORAGE_UINT64,
                 i32 = QA_STORAGE_INT32;
  reg(locale, MS_INCREF, "?_Incref@facet@locale@std@@UEAAXXZ", &ptr, 1, true, i32);
  reg(locale, MS_DECREF, "?_Decref@facet@locale@std@@UEAAPEAV_Facet_base@3@XZ", &ptr, 1,
      false, ptr);
  reg(locale, MS_FACET_DTOR, "??1facet@locale@std@@MEAA@XZ", &ptr, 1, true, i32);
  qa_abi_storage delete_params[] = {ptr, u32};
  reg(locale, MS_FACET_DELETE, "runtime:facet-delete", delete_params, 2, false, ptr);
  locale->facet_vtable = qa_gmem_allocate(host->memory_ext, 24, false, 0, false,
                                          QA_PERM_READ_WRITE, "MSVC facet vftable");
  const char *methods[] = {"runtime:facet-delete", "?_Incref@facet@locale@std@@UEAAXXZ",
                           "?_Decref@facet@locale@std@@UEAAPEAV_Facet_base@3@XZ"};
  for (size_t i = 0; i < 3; i++) {
    qa_guest_core_address address = {0};
    if (!qa_windows_resolve_address(host, library, methods[i], &address))
      plain_error("Missing MSVC facet method");
    write_ptr(memory, at(memory, locale->facet_vtable, (int64_t)i * 8), true, address);
  }
  qa_gmem_protect(host->memory_ext, locale->facet_vtable, 24, QA_PERM_READ);
  locale->global = qa_gmem_allocate(host->memory_ext, 56, false, 0, false, QA_PERM_READ_WRITE,
                                    "MSVC C locale implementation");
  write_ptr(memory, locale->global, true, locale->facet_vtable);
  qa_memory_write_u32(memory, at(memory, locale->global, 8), 2);
  qa_memory_write_u32(memory, at(memory, locale->global, 32), 63);
  qa_guest_core_address name = qa_gmem_allocate(host->memory_ext, 2, false, 0, false,
                                           QA_PERM_READ_WRITE, "MSVC locale name");
  size_t name_len = 0;
  uint8_t *name_bytes = qa_string_bytes("C", false, &name_len);
  qa_memory_write(memory, name, name_bytes, name_len);
  free(name_bytes);
  write_ptr(memory, at(memory, locale->global, 40), true, name);
  qa_abi_storage ctor_params[] = {ptr, u64};
  reg(locale, MS_FACET_CTOR, "??0facet@locale@std@@IEAA@_K@Z", ctor_params, 2, false, ptr);
  reg(locale, MS_INIT, "?_Init@locale@std@@CAPEAV_Locimp@12@_N@Z", &u32, 1, false, ptr);
  reg(locale, MS_GETGLOBAL, "?_Getgloballocale@locale@std@@CAPEAV_Locimp@12@XZ", NULL, 0,
      false, ptr);
  qa_abi_storage lock_params[] = {ptr, i32};
  reg(locale, MS_LOCKIT_CTOR, "??0_Lockit@std@@QEAA@H@Z", lock_params, 2, false, ptr);
  reg(locale, MS_LOCKIT_DTOR, "??1_Lockit@std@@QEAA@XZ", &ptr, 1, true, i32);
  qa_abi_storage locinfo_params[] = {ptr, ptr};
  reg(locale, MS_LOCINFO_CTOR, "??0_Locinfo@std@@QEAA@PEBD@Z", locinfo_params, 2, false, ptr);
  reg(locale, MS_LOCINFO_DTOR, "??1_Locinfo@std@@QEAA@XZ", &ptr, 1, true, i32);
  const char *words[] = {"true", "false"};
  for (size_t i = 0; i < 2; i++) {
    size_t len = 0;
    uint8_t *bytes = qa_string_bytes(words[i], false, &len);
    char label[32];
    (void)snprintf(label, sizeof label, "MSVC %s name", words[i]);
    qa_guest_core_address address = qa_gmem_allocate(host->memory_ext, len, false, 0, false,
                                                QA_PERM_READ_WRITE, label);
    qa_memory_write(memory, address, bytes, len);
    free(bytes);
    locale_state *state = malloc(sizeof *state);
    if (state == NULL) qa_range_error("Guest runtime out of memory");
    state->locale = locale; state->kind = MS_GETWORD; state->word_address = address;
    qa_windows_import_implementation impl = {state, locale_impl};
    char service_name[64];
    (void)snprintf(service_name, sizeof service_name, "?_Get%s@_Locinfo@std@@QEBAPEBDXZ",
                   words[i]);
    qa_windows_service(host, library, service_name, &ptr, 1, false, ptr, impl);
  }
  reg(locale, MS_GETLCONV, "?_Getlconv@_Locinfo@std@@QEBAPEBUlconv@@XZ", &ptr, 1, false, ptr);
  reg(locale, MS_GETCVT, "?_Getcvt@_Locinfo@std@@QEBA?AU_Cvtvec@@XZ", locinfo_params, 2,
      false, ptr);
}
