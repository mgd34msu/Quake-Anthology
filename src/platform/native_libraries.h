#ifndef QA_PLATFORM_NATIVE_LIBRARIES_H
#define QA_PLATFORM_NATIVE_LIBRARIES_H

#include "qa/common.h"

typedef enum qa_platform_library_kind {
    QA_PLATFORM_LIBRARY_VORBISFILE,
    QA_PLATFORM_LIBRARY_THEORADEC,
    QA_PLATFORM_LIBRARY_FREETYPE
} qa_platform_library_kind;

typedef struct qa_platform_library qa_platform_library;

/* Binding succeeds only after every symbol needed by this native ABI is found.
 * A failed candidate is closed before trying the next one. An explicit override
 * is the sole candidate. The successful handle must outlive all native objects. */
bool qa_platform_library_open(qa_platform_library_kind,
    bool (*bind)(qa_platform_library *, void *, qa_error *), void *api,
    qa_platform_library **out, qa_error *);
void qa_platform_library_close(qa_platform_library *);
bool qa_platform_library_bind_symbol(qa_platform_library *, const char *name,
                                     void *function, qa_error *);

#define QA_PLATFORM_LIBRARY_BIND(library, api, name, error) do { \
    _Static_assert(sizeof((api)->name) == sizeof(void *), "Native function pointer size"); \
    if (!qa_platform_library_bind_symbol((library), #name, &(api)->name, (error))) \
        return false; \
} while (0)

#endif
