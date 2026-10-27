/*
 * Port of src/guest/runtime/windows/msvc/locale.ts: MSVC x64 xlocale/
 * xfacet layout; facet counts and ownership live in guest memory.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_WINDOWS_MSVC_LOCALE_H
#define QA_GUEST_RUNTIME_WINDOWS_MSVC_LOCALE_H

#include "qa/guest/runtime/windows/contracts.h"

typedef struct qa_msvc_locale {
  qa_windows_service_host *host;
  qa_guest_address global;
  qa_guest_address facet_vtable;
  qa_guest_address locks;
} qa_msvc_locale;

void qa_msvc_locale_init(qa_msvc_locale *locale, qa_windows_service_host *host);
qa_guest_address qa_msvc_locale_allocate(qa_msvc_locale *locale, size_t size);
void qa_msvc_locale_incref(qa_msvc_locale *locale, qa_guest_address facet);
/* false: TS null. */
bool qa_msvc_locale_decref(qa_msvc_locale *locale, qa_guest_address facet, qa_guest_address *out);
qa_guest_address qa_msvc_locale_create(qa_msvc_locale *locale);
void qa_msvc_locale_destroy(qa_msvc_locale *locale, const qa_guest_call_context *context,
                            bool has_locale, qa_guest_address locale_address);
void qa_msvc_locale_lock(qa_msvc_locale *locale, qa_guest_address object, int kind);
void qa_msvc_locale_unlock(qa_msvc_locale *locale, qa_guest_address object);

#endif
