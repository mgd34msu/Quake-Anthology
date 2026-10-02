#ifndef QA_NATIVE_GUEST_ELF_BIND_H
#define QA_NATIVE_GUEST_ELF_BIND_H

#include "elf.h"
#include "sysv_runtime.h"

typedef struct guest_elf_binding {
    const guest_elf *image;
    qa_native_guest *guest;
    guest_sysv_runtime *runtime;
    uint64_t provider, return_trap;
    size_t instruction_budget; /* Positive EMU budget, explicitly zero HOST. */
    guest_sysv_tls tls;
} guest_elf_binding;

/* Fresh loading only. The actual image pages, relocation write permissions,
 * stack, return trap and runtime must already belong to this process. Ordinary
 * relocations precede actual IFUNC/IRELATIVE execution. Reached writes and
 * resolver effects remain authoritative; failure makes the candidate terminal
 * for whole-process teardown. This never maps an image or installs services.
 * Cold adoption preserves relocated RAM and never calls these functions. */
bool guest_elf_bind_relocations(const guest_elf_binding *, qa_error *);
/* Resolve a real defined export. Ordinary definitions use their image value;
 * GNU-unique data consults the process registry and IFUNC executes its genuine
 * resolver ABI. Export size retains this symbol's actual declaration. */
bool guest_elf_bind_export(const guest_elf_binding *, size_t,
    guest_sysv_symbol *, qa_error *);

#endif
