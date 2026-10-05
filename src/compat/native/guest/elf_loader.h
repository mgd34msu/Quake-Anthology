#ifndef QA_NATIVE_GUEST_ELF_LOADER_H
#define QA_NATIVE_GUEST_ELF_LOADER_H

#include "elf_memory.h"
#include "elf_publish.h"
#include "elf_unwind.h"

typedef struct guest_elf_loaded guest_elf_loaded;
typedef struct guest_elf_load_options {
    uint64_t provider, return_trap;
    /* Positive only for emulation; zero explicitly selects the actual
     * hardware owner's unbudgeted resolver/lifecycle capability. */
    size_t instruction_budget;
    guest_elf_memory_options memory;
    bool replacing;
} guest_elf_load_options;

/* Actual dependencies, runtime, stack/FP state and return trap must already
 * belong to this process. This fresh path attaches role-qualified image RAM,
 * reserves real TLS, publishes GNU unique definitions before relocation,
 * runs actual indirect resolvers, captures relocated metadata, finalizes
 * rights/RELRO and commits the provider. Lifecycle execution stays explicit.
 * Process entry/auxv/interpreter scheduling and unwind execution belong to
 * their actual process owners and must precede source calls requiring them.
 * Any reached mutation followed by failure makes the candidate terminal;
 * destroy the whole actual guest before abandoning retained loader records. */
bool guest_elf_load(const guest_elf *, guest_sysv_runtime *,
    const guest_elf_load_options *, guest_elf_loaded **, qa_error *);
bool guest_elf_loaded_initialize(guest_elf_loaded *, size_t, qa_error *);
bool guest_elf_loaded_finalize(guest_elf_loaded *, size_t, qa_error *);
bool guest_elf_loaded_unmap(guest_elf_loaded *, qa_error *);
/* Capture only the committed idle loaded owner. Cold adoption joins its real
 * decoded/attached runtime provider to its actual already-restored backing
 * inventory and retained artifact. It never loads, resolves, binds, publishes
 * TLS, changes permissions or invokes source lifecycle/IFUNC callbacks.
 * The runtime capsule owns relocated values and lifecycle progress; QALM owns
 * original image backing anchors. The unwind record preserves the full
 * relocated frame snapshots and historical indirect PC resolutions. None
 * is recreated from later mutable image RAM or fresh source execution. */
bool guest_elf_loaded_checkpoint(const guest_elf_loaded *, qa_buffer *, qa_error *);
bool guest_elf_loaded_memory_record(qa_bytes, qa_bytes *borrowed, qa_error *);
bool guest_elf_loaded_adopt(const guest_elf *, guest_sysv_runtime *, qa_bytes,
    guest_elf_memory **prepared,
    guest_elf_loaded **, qa_error *);
/* Host records only after actual guest destruction. The retained inert ELF
 * and decoded runtime outlive this owner. No source destructor is replayed. */
void guest_elf_loaded_abandon(guest_elf_loaded **);
guest_elf_memory *guest_elf_loaded_memory(guest_elf_loaded *);
uint64_t guest_elf_loaded_provider(const guest_elf_loaded *);
const guest_elf_unwind *guest_elf_loaded_unwind(const guest_elf_loaded *);

#endif
