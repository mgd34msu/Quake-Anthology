#ifndef QA_NATIVE_GUEST_SYSV_PROGRAM_PRIVATE_H
#define QA_NATIVE_GUEST_SYSV_PROGRAM_PRIVATE_H

#include "qa/native_sysv_program.h"
#include "internal.h"
#include "elf_program.h"
#include "profile/artifact.h"

typedef struct program_file {
    qa_native_sysv_file capability;
    uint64_t offset;
    uint32_t flags;
    size_t references;
    bool seekable, closed, closing;
} program_file;
typedef struct program_descriptor {
    int32_t number;
    size_t file;
    bool close_on_exec;
} program_descriptor;
typedef struct program_clock {
    uint64_t seconds;
    uint32_t nanoseconds;
    int64_t baseline_seconds;
    int32_t baseline_nanoseconds;
} program_clock;
struct qa_native_sysv_program {
    qa_native_sysv_program_options options;
    qa_native_guest *guest;
    guest_elf *artifacts[2];
    guest_elf_memory *memory[2];
    guest_elf_program *startup;
    guest_profile_artifacts *provenance;
    program_file *files;
    size_t file_count, file_capacity;
    program_descriptor *descriptors;
    size_t descriptor_count, descriptor_capacity;
    uint64_t stack, returned, mapping_cursor, break_base, current_break;
    qa_native_sysv_program_status status;
    program_clock clocks[2];
    bool clock_active, complete, busy, failed, disposing, provisional;
};
bool program_current(qa_native_sysv_program *, qa_error *);
bool program_syscall(void *, qa_native_guest *, const qa_native_guest_syscall *,
    qa_native_guest_syscall_result *, qa_error *);
bool program_files_close(qa_native_sysv_program *, qa_error *);
bool program_clock_read(qa_native_sysv_program *, int32_t, int64_t *, int32_t *, qa_error *);

#endif
