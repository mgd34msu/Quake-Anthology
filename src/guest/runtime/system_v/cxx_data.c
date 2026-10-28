/* Port of src/guest/runtime/system-v/cxx-data.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/system_v/cxx_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

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
static char *concat2(const char *a, const char *b) {
  char *out = xmalloc(strlen(a) + strlen(b) + 1);
  strcpy(out, a); strcat(out, b);
  return out;
}
static void plain_error(const char *message) { qa_range_error("%s", message); }
static void write_ptr(const qa_mapped_guest_memory *m, qa_guest_core_address a, qa_guest_core_address v) {
  qa_gmem_write_pointer(m, a, true, v);
}
static const char *form_name(int form) { return form == 0 ? "class" : form == 1 ? "si" : "vmi"; }
static int form_index(const char *form) {
  return strcmp(form, "class") == 0 ? 0 : strcmp(form, "si") == 0 ? 1 : 2;
}
static bool types_get(qa_cxx_abi_data *abi, const char *name, qa_guest_core_address *out) {
  for (size_t i = 0; i < abi->type_count; i++)
    if (strcmp(abi->types[i].name, name) == 0) { *out = abi->types[i].address; return true; }
  return false;
}
static void types_set(qa_cxx_abi_data *abi, const char *name, qa_guest_core_address address) {
  for (size_t i = 0; i < abi->type_count; i++)
    if (strcmp(abi->types[i].name, name) == 0) { abi->types[i].address = address; return; }
  if (abi->type_count == abi->type_cap) {
    abi->type_cap = abi->type_cap == 0 ? 8 : abi->type_cap * 2;
    abi->types = xrealloc(abi->types, abi->type_cap * sizeof *abi->types);
  }
  abi->types[abi->type_count].name = xstrdup(name);
  abi->types[abi->type_count].address = address;
  abi->type_count++;
}

qa_guest_core_address qa_cxx_abi_slot(qa_cxx_abi_data *abi, qa_guest_core_address address, int64_t offset) {
  return qa_memory_offset(abi->host->memory, address, offset);
}
qa_guest_core_address qa_cxx_abi_bytes(qa_cxx_abi_data *abi, const char *text) {
  size_t len = 0;
  uint8_t *bytes = qa_string_bytes(text, false, &len);
  qa_guest_core_address address = qa_system_v_allocate(abi->host, len);
  qa_memory_write(abi->host->memory, address, bytes, len);
  free(bytes);
  return address;
}
qa_guest_core_address qa_cxx_abi_function(qa_cxx_abi_data *abi, const char *name,
                                     const qa_abi_storage *parameters, size_t parameter_count,
                                     bool result_is_void, qa_abi_storage result,
                                     qa_system_v_implementation invoke, const char *version) {
  const char *actual_version = version != NULL ? version : "GLIBCXX_3.4";
  const char *versions[] = {actual_version, NULL};
  qa_system_v_service(abi->host, "libstdc++.so.6", name, versions, 2, parameters, parameter_count,
                      result_is_void, result, invoke);
  qa_guest_core_address address = {0};
  if (!qa_system_v_resolve_address(abi->host, "libstdc++.so.6", name, actual_version, &address))
    qa_range_error("Unbound C++ service %s", name);
  return address;
}
qa_guest_core_address qa_cxx_abi_unsupported(qa_cxx_abi_data *abi, const char *name) {
  qa_abi_storage parameter = QA_STORAGE_POINTER;
  return qa_system_v_unavailable(abi->host, "libstdc++.so.6", name, &parameter, 1, true,
                                 QA_STORAGE_INT32,
                                 "this C++ virtual operation is not implemented");
}
qa_guest_core_address qa_cxx_abi_define_data(qa_cxx_abi_data *abi, const char *name, size_t size,
                                 const char *version) {
  qa_guest_core_address address = qa_system_v_allocate(abi->host, size);
  qa_system_v_data(abi->host, "libstdc++.so.6", name,
                   version != NULL ? version : "GLIBCXX_3.4", address, size);
  return address;
}

static qa_call_result zero_int32_impl(void *ctx, const qa_guest_core_call_context *context,
                                      const qa_call_value *args, size_t count) {
  (void)ctx; (void)context; (void)args; (void)count;
  return qa_call_int32(0);
}

void qa_cxx_abi_data_init(qa_cxx_abi_data *abi, qa_system_v_service_host *host) {
  memset(abi, 0, sizeof *abi);
  abi->host = host;
  qa_mapped_guest_memory *m = host->memory;
  int p = m->pointer_bytes;
  const char *class_name = "N10__cxxabiv117__class_type_infoE";
  const char *single_name = "N10__cxxabiv120__si_class_type_infoE";
  const char *multiple_name = "N10__cxxabiv121__vmi_class_type_infoE";
  qa_guest_core_address base = qa_cxx_abi_define_data(abi, "_ZTISt9type_info", (size_t)(2 * p), "GLIBCXX_3.4");
  char *class_ti = concat2("_ZTI", class_name);
  char *single_ti = concat2("_ZTI", single_name);
  char *multiple_ti = concat2("_ZTI", multiple_name);
  qa_guest_core_address class_info = qa_cxx_abi_define_data(abi, class_ti, (size_t)(3 * p), "CXXABI_1.3");
  qa_guest_core_address single_info = qa_cxx_abi_define_data(abi, single_ti, (size_t)(3 * p), "CXXABI_1.3");
  qa_guest_core_address multiple_info = qa_cxx_abi_define_data(abi, multiple_ti, (size_t)(3 * p), "CXXABI_1.3");
  qa_abi_storage ptr = QA_STORAGE_POINTER;
  qa_system_v_implementation zero_impl = {NULL, zero_int32_impl};
  qa_guest_core_address pointer_test = qa_cxx_abi_function(
      abi, "__guest_type_info_is_pointer", &ptr, 1, false, QA_STORAGE_INT32, zero_impl, NULL);
  qa_guest_core_address function_test = qa_cxx_abi_function(
      abi, "__guest_type_info_is_function", &ptr, 1, false, QA_STORAGE_INT32, zero_impl, NULL);
  const char *names[] = {class_name, single_name, multiple_name};
  qa_guest_core_address infos[] = {class_info, single_info, multiple_info};
  for (int form = 0; form < 3; form++) {
    char *table_name = concat2("_ZTV", names[form]);
    qa_guest_core_address table = qa_cxx_abi_define_data(abi, table_name, (size_t)(11 * p), "CXXABI_1.3");
    write_ptr(m, qa_cxx_abi_slot(abi, table, p), infos[form]);
    for (int index = 0; index < 9; index++) {
      qa_guest_core_address entry;
      if (index == 2) entry = pointer_test;
      else if (index == 3) entry = function_test;
      else {
        char buffer[96];
        (void)snprintf(buffer, sizeof buffer, "__guest_%s_type_info_virtual_%d",
                       form_name(form), index);
        entry = qa_cxx_abi_unsupported(abi, xstrdup(buffer));
      }
      write_ptr(m, qa_cxx_abi_slot(abi, table, (index + 2) * p), entry);
    }
    abi->class_info_vtables[form] = qa_cxx_abi_slot(abi, table, 2 * p);
    abi->has_class_info_vtables[form] = true;
    free(table_name);
  }
  if (!abi->has_class_info_vtables[0] || !abi->has_class_info_vtables[1])
    plain_error("Missing RTTI class tables");
  qa_guest_core_address class_vtable = abi->class_info_vtables[0];
  qa_guest_core_address single_vtable = abi->class_info_vtables[1];
  write_ptr(m, base, class_vtable);
  write_ptr(m, qa_cxx_abi_slot(abi, base, p), qa_cxx_abi_bytes(abi, "St9type_info"));
  const char *type_names[] = {class_name, single_name, multiple_name};
  qa_guest_core_address parents[] = {base, class_info, class_info};
  for (int i = 0; i < 3; i++) {
    write_ptr(m, infos[i], single_vtable);
    write_ptr(m, qa_cxx_abi_slot(abi, infos[i], p), qa_cxx_abi_bytes(abi, type_names[i]));
    write_ptr(m, qa_cxx_abi_slot(abi, infos[i], 2 * p), parents[i]);
    types_set(abi, type_names[i], infos[i]);
  }
  types_set(abi, "St9type_info", base);
  free(class_ti); free(single_ti); free(multiple_ti);
}
void qa_cxx_abi_data_destroy(qa_cxx_abi_data *abi) {
  for (size_t i = 0; i < abi->type_count; i++) free(abi->types[i].name);
  free(abi->types);
  memset(abi, 0, sizeof *abi);
}

qa_guest_core_address qa_cxx_abi_type(qa_cxx_abi_data *abi, const char *name,
                                 const qa_cxx_rtti_base *bases, size_t base_count) {
  qa_guest_core_address prior = {0};
  if (types_get(abi, name, &prior)) return prior;
  qa_mapped_guest_memory *m = abi->host->memory;
  int p = m->pointer_bytes;
  const char *form = base_count == 0 ? "class"
      : (base_count == 1 && bases[0].offset == 0 && bases[0].flags == 2) ? "si" : "vmi";
  int form_idx = form_index(form);
  if (!abi->has_class_info_vtables[form_idx]) plain_error("Missing RTTI class table");
  qa_guest_core_address vtable = abi->class_info_vtables[form_idx];
  size_t size = strcmp(form, "class") == 0 ? (size_t)(2 * p)
      : strcmp(form, "si") == 0 ? (size_t)(3 * p)
      : (size_t)(2 * p) + 8 + base_count * (size_t)(2 * p);
  char *data_name = concat2("_ZTI", name);
  qa_guest_core_address address = qa_cxx_abi_define_data(abi, data_name, size, NULL);
  free(data_name);
  qa_guest_core_address text = qa_cxx_abi_bytes(abi, name);
  write_ptr(m, address, vtable);
  write_ptr(m, qa_cxx_abi_slot(abi, address, p), text);
  if (strcmp(form, "si") == 0) {
    if (base_count == 0) plain_error("Missing single RTTI base");
    write_ptr(m, qa_cxx_abi_slot(abi, address, 2 * p), bases[0].type);
  } else if (strcmp(form, "vmi") == 0) {
    qa_memory_write_u32(m, qa_cxx_abi_slot(abi, address, 2 * p), 0);
    qa_memory_write_u32(m, qa_cxx_abi_slot(abi, address, 2 * p + 4), (uint32_t)base_count);
    for (size_t index = 0; index < base_count; index++) {
      write_ptr(m, qa_cxx_abi_slot(abi, address, 2 * p + 8 + (int64_t)index * 2 * p),
                bases[index].type);
      uint64_t encoded = (uint64_t)(bases[index].offset * 256) | (uint64_t)bases[index].flags;
      qa_write_unsigned(m, qa_cxx_abi_slot(abi, address, 3 * p + 8 + (int64_t)index * 2 * p),
                        p, encoded);
    }
  }
  types_set(abi, name, address);
  return address;
}
qa_guest_core_address qa_cxx_abi_vtable(qa_cxx_abi_data *abi, const char *name, qa_guest_core_address type,
                                   const qa_guest_core_address *entries, size_t entry_count) {
  qa_mapped_guest_memory *m = abi->host->memory;
  int p = m->pointer_bytes;
  char *data_name = concat2("_ZTV", name);
  qa_guest_core_address table = qa_cxx_abi_define_data(abi, data_name, (entry_count + 2) * (size_t)p, NULL);
  free(data_name);
  write_ptr(m, qa_cxx_abi_slot(abi, table, p), type);
  for (size_t index = 0; index < entry_count; index++)
    write_ptr(m, qa_cxx_abi_slot(abi, table, (int64_t)(index + 2) * p), entries[index]);
  return qa_cxx_abi_slot(abi, table, 2 * p);
}
