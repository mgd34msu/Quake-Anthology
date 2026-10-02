#ifndef QA_NATIVE_GUEST_WINDOWS_KERNEL_H
#define QA_NATIVE_GUEST_WINDOWS_KERNEL_H
#include "windows_runtime.h"
typedef struct windows_reservation { uint64_t base, bytes; bool allocated; } windows_reservation;
typedef struct windows_fls { uint64_t callback, value; bool allocated; } windows_fls;
typedef struct windows_standard { int32_t id; uint64_t handle; } windows_standard;
typedef struct windows_file_handle { uint64_t handle; int32_t stream; double stream_offset; } windows_file_handle;
typedef struct windows_pending_file {
    char *path;
    uint32_t creation;
    guest_runtime_file_capability capability;
    bool opened;
} windows_pending_file;
struct guest_windows_kernel {
    uint64_t command_line_a, command_line_w, environment_a, environment_w;
    uint64_t pointer_secret, process_heap, dynamic_tls, exception_filter;
    windows_standard *standards; size_t standard_count, standard_capacity;
    windows_file_handle *handles; size_t handle_count, handle_capacity;
    windows_pending_file **pending_files; size_t pending_count, pending_capacity;
    uint64_t *heaps, *locks;
    size_t heap_count, heap_capacity, lock_count, lock_capacity;
    windows_reservation *reservations;
    size_t reservation_count, reservation_capacity;
    bool tls[1088];
    windows_fls fls[128];
};
bool windows_kernel_descriptors(guest_windows *, bool, qa_error *);
bool windows_kernel_initialize(guest_windows *, const guest_windows_options *, qa_error *);
bool windows_kernel_invoke(windows_service *, const qa_native_value *, size_t, qa_native_value *, qa_error *);
bool windows_kernel_validate(guest_windows *, qa_error *);
void windows_kernel_dispose(guest_windows_kernel *);
bool windows_kernel_close_pending(guest_windows *, qa_error *);
#endif
