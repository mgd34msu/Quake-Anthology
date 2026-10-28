/*
 * Port of src/guest/runtime/system-v/locale.ts: GNU libstdc++ classic "C"
 * locale implementation, facets and ctype tables in guest memory.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_SYSTEM_V_LOCALE_H
#define QA_GUEST_RUNTIME_SYSTEM_V_LOCALE_H

#include "qa/guest/runtime/system_v/cxx_data.h"

typedef struct qa_system_v_classic_locale {
  qa_cxx_abi_data *abi;
  qa_guest_core_address implementation;
  qa_guest_core_address c_locale;
  qa_guest_core_address ctype[2];   /* narrow, wide */
  qa_guest_core_address num_put[2];
  qa_guest_core_address num_get[2];
  qa_guest_core_address facet_base;
} qa_system_v_classic_locale;

void qa_system_v_classic_locale_init(qa_system_v_classic_locale *locale, qa_cxx_abi_data *abi);
qa_guest_core_address qa_system_v_classic_locale_retain(qa_system_v_classic_locale *locale);
void qa_system_v_classic_locale_release(qa_system_v_classic_locale *locale,
                                        qa_guest_core_address implementation);

#endif
