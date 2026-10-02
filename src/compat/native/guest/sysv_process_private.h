#ifndef QA_NATIVE_SYSV_PROCESS_PRIVATE_H
#define QA_NATIVE_SYSV_PROCESS_PRIVATE_H

#include "qa/native_sysv_process.h"
#include "internal.h"
#include "elf_loader.h"
#include "profile/artifact.h"

typedef struct sysv_process_image {
    guest_elf *artifact;
    guest_elf_loaded *loaded;
    uint64_t provider;
} sysv_process_image;
typedef struct sysv_process_stream {
    struct qa_native_sysv_process *process;
    uint64_t handle;
} sysv_process_stream;
typedef struct sysv_process_opened_file {
    qa_native_sysv_file file;
    bool opened;
    struct sysv_process_opened_file *next;
} sysv_process_opened_file;
struct qa_native_sysv_process {
    qa_native_sysv_process_options options;
    qa_native_guest *guest;
    guest_sysv_runtime *runtime;
    guest_runtime_resources *resources;
    guest_profile_artifacts *profile;
    sysv_process_image *images;
    size_t image_count;
    uint64_t stack, returned;
    sysv_process_stream streams[3];
    sysv_process_opened_file *pending_files;
    bool complete, busy, failed, disposing, provisional;
};

bool sysv_process_current(qa_native_sysv_process *, qa_error *);
guest_sysv_bindings sysv_process_bindings(qa_native_sysv_process *);
bool sysv_process_profile(qa_native_sysv_process *, bool, qa_error *);

#endif
