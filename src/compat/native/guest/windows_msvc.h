#ifndef QA_NATIVE_GUEST_WINDOWS_MSVC_H
#define QA_NATIVE_GUEST_WINDOWS_MSVC_H
#include "windows_runtime.h"
struct guest_windows_msvc {
    uint64_t locks, facet_vtable, global_locale, true_text, false_text;
    uint64_t ios_vtable, ostream_vtable, iostream_vtable, buffer_vtable;
    uint64_t ostream_vbase, iostream_vbase, locale_id_count, numpunct_id;
};
bool windows_msvc_descriptors(guest_windows *, bool, qa_error *);
bool windows_msvc_initialize(guest_windows *, qa_error *);
bool windows_msvc_invoke(windows_service *, const qa_native_value *, size_t, qa_native_value *, qa_error *);
bool windows_msvc_validate(guest_windows *, qa_error *);
#endif
