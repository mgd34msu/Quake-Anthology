#ifndef QA_NATIVE_WINDOWS_PROCESS_PRIVATE_H
#define QA_NATIVE_WINDOWS_PROCESS_PRIVATE_H

#include "qa/native_windows_process.h"
#include "internal.h"
#include "pe_memory.h"
#include "windows_runtime.h"
#include "profile/artifact.h"

typedef struct windows_process_image {
    uint64_t id;
    char *path;
    guest_pe *artifact;
    guest_pe_memory *memory;
} windows_process_image;
typedef struct windows_process_stream {
    struct qa_native_windows_process *process;
    size_t index;
} windows_process_stream;
struct qa_native_windows_process {
    qa_native_windows_process_options options;
    qa_native_guest *guest;
    guest_windows *runtime;
    guest_profile_artifacts *provenance;
    windows_process_image *images;
    size_t image_count;
    uint64_t stack;
    windows_process_stream streams[3];
    bool complete, busy, failed, disposing, provisional;
};

bool windows_process_current(qa_native_windows_process *, qa_error *);
guest_windows_capabilities windows_process_capabilities(qa_native_windows_process *);
bool windows_process_same_image(const qa_native_image_info *, const qa_native_image_info *);
bool windows_process_storage(const qa_native_windows_process *, qa_error *);
bool windows_process_artifacts(qa_native_windows_process *, bool, qa_error *);

#endif
