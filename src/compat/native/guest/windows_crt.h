#ifndef QA_NATIVE_GUEST_WINDOWS_CRT_H
#define QA_NATIVE_GUEST_WINDOWS_CRT_H
#include "windows_runtime.h"
#include "windows_stdio.h"
#include "qa/id_index.h"
enum { WINDOWS_FILE_ADDRESS, WINDOWS_FILE_HANDLE };
struct guest_windows_crt {
    uint64_t onexit, error_number, time_buffer, locale, empty, decimal;
    windows_stdio_file *files;
    qa_id_index file_identities;
    size_t file_count, file_capacity;
    uint32_t next_file;
    windows_stdio_pending **pending_files;
    size_t pending_file_count, pending_file_capacity;
    bool has_file_opener;
};
bool windows_crt_descriptors(guest_windows *, bool, qa_error *);
guest_readonly_kind windows_crt_readonly_kind(uint32_t);
bool windows_crt_initialize(guest_windows *, qa_error *);
bool windows_crt_invoke(windows_service *, const qa_native_value *, size_t, qa_native_value *, qa_error *);
bool windows_crt_validate(guest_windows *, qa_error *);
bool windows_crt_finalize_image(guest_windows *, uint64_t, uint64_t, qa_error *);
#endif
