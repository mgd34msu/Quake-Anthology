/*
 * Port of src/guest/runtime/system-v/cxx-data.ts: GNU libstdc++ / Itanium
 * C++ ABI RTTI data. The TS Maps become owned name/address lists; the
 * class-info vtable map keeps its three fixed forms. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_SYSTEM_V_CXX_DATA_H
#define QA_GUEST_RUNTIME_SYSTEM_V_CXX_DATA_H

#include "qa/guest/runtime/system_v/contracts.h"

typedef struct qa_cxx_rtti_base {
  qa_guest_core_address type;
  int64_t offset;
  int64_t flags;
} qa_cxx_rtti_base;

typedef struct qa_cxx_abi_data {
  qa_system_v_service_host *host;
  struct { char *name; qa_guest_core_address address; } *types; /* names owned */
  size_t type_count, type_cap;
  qa_guest_core_address class_info_vtables[3]; /* class, si, vmi */
  bool has_class_info_vtables[3];
} qa_cxx_abi_data;

void qa_cxx_abi_data_init(qa_cxx_abi_data *abi, qa_system_v_service_host *host);
void qa_cxx_abi_data_destroy(qa_cxx_abi_data *abi);
qa_guest_core_address qa_cxx_abi_slot(qa_cxx_abi_data *abi, qa_guest_core_address address, int64_t offset);
qa_guest_core_address qa_cxx_abi_bytes(qa_cxx_abi_data *abi, const char *text);
/* version NULL selects the TS default "GLIBCXX_3.4". */
qa_guest_core_address qa_cxx_abi_function(qa_cxx_abi_data *abi, const char *name,
                                     const qa_abi_storage *parameters, size_t parameter_count,
                                     bool result_is_void, qa_abi_storage result,
                                     qa_system_v_implementation invoke, const char *version);
qa_guest_core_address qa_cxx_abi_unsupported(qa_cxx_abi_data *abi, const char *name);
qa_guest_core_address qa_cxx_abi_define_data(qa_cxx_abi_data *abi, const char *name, size_t size,
                                 const char *version);
qa_guest_core_address qa_cxx_abi_type(qa_cxx_abi_data *abi, const char *name,
                                 const qa_cxx_rtti_base *bases, size_t base_count);
qa_guest_core_address qa_cxx_abi_vtable(qa_cxx_abi_data *abi, const char *name, qa_guest_core_address type,
                                   const qa_guest_core_address *entries, size_t entry_count);

#endif
