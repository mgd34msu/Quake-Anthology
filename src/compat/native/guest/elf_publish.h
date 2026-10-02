#ifndef QA_NATIVE_GUEST_ELF_PUBLISH_H
#define QA_NATIVE_GUEST_ELF_PUBLISH_H

#include "elf_bind.h"

typedef struct guest_elf_publication guest_elf_publication;

/* Build the genuine relocated provider and TLS template for load_commit.
 * This fresh path may execute defined IFUNC exports through the actual ABI;
 * it does not invoke lifecycle arrays or publish the provider itself. Strings
 * and artifact metadata borrow the retained inert ELF until close. Runtime
 * load_commit copies them before this transient host owner can be released.
 * Cold runtime decode restores its saved provider without this fresh path. */
bool guest_elf_publication_open(const guest_elf_binding *, guest_elf_publication **, qa_error *);
void guest_elf_publication_close(guest_elf_publication **);
const guest_sysv_provider *guest_elf_publication_provider(const guest_elf_publication *);
qa_bytes guest_elf_publication_tls(const guest_elf_publication *);

#endif
