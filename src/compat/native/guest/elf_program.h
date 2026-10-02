#ifndef QA_NATIVE_GUEST_ELF_PROGRAM_H
#define QA_NATIVE_GUEST_ELF_PROGRAM_H

#include "elf_memory.h"

typedef struct guest_elf_program guest_elf_program;
typedef struct guest_elf_program_image {
    uint64_t provider;
    const guest_elf *artifact;
    guest_elf_memory *memory;
} guest_elf_program_image;
typedef struct guest_elf_program_aux { uint64_t tag, value; } guest_elf_program_aux;
typedef struct guest_elf_program_images {
    guest_elf_program_image program, interpreter;
    /* Exact selected PT_INTERP path, not a guessed host linker path. */
    const char *interpreter_path;
} guest_elf_program_images;
typedef struct guest_elf_program_options {
    guest_elf_program_images images;
    /* Existing allocator-owned process stack. Its mappings and protection
     * already belong to the actual process, including any executable policy. */
    uint64_t stack;
    size_t stack_bytes;
    const char *const *argv, *const *environment;
    size_t argc, environment_count;
    const char *executed_path, *platform, *base_platform;
    qa_bytes random; /* Exactly 16 bytes from the caller's actual authority. */
    /* Actual caller/kernel scalar values. UID/EUID/GID/EGID/HWCAP/CLKTCK,
     * SECURE and FLAGS are required. Image, string and random entries are
     * derived here; architecture entries and unclassified resource entries
     * each retain caller order within the actual Linux auxiliary sequence. */
    const guest_elf_program_aux *auxiliary;
    size_t auxiliary_count;
} guest_elf_program_options;
typedef struct guest_elf_program_view {
    uint64_t program, interpreter, stack, initial_stack, entry;
    size_t stack_bytes, argc, environment_count;
} guest_elf_program_view;

/* Kernel-style fresh startup, with raw PROGRAM-role attachments for both
 * executable and selected interpreter. No guest_elf_load, relocation, IFUNC,
 * RELRO, source initializer or runtime installer may precede this operation.
 * All layout/readability checks and host allocations precede the stack write.
 * A reached mutation followed by failure retains a terminal process owner.
 * The actual process owns FP defaults, architectural selectors/personality,
 * syscall/exit services and source scheduling; this operation sets fresh GP,
 * FS/GS bases, SP and entry once, then leaves execution to that real caller. */
bool guest_elf_program_prepare(const guest_elf_program_options *,
    guest_elf_program **, qa_error *);
const guest_elf_program_view *guest_elf_program_describe(const guest_elf_program *);
bool guest_elf_program_checkpoint(const guest_elf_program *, qa_buffer *, qa_error *);
/* The process capsule retains both actual QALM attachments separately. Their
 * inert artifacts and live memory owners outlive this borrowed startup owner.
 * Pure host-record adoption against the actual restored guest, artifact graph
 * and allocator-owned stack. Saved mutable stack/RAM/CPU win; no strings,
 * random bytes, auxiliary words or initial register values are replayed. */
bool guest_elf_program_adopt(const guest_elf_program_images *, qa_bytes,
    guest_elf_program **, qa_error *);
void guest_elf_program_abandon(guest_elf_program **);

#endif
