#ifndef QA_NATIVE_GUEST_PROFILE_ARTIFACT_H
#define QA_NATIVE_GUEST_PROFILE_ARTIFACT_H

#include "../elf.h"
#include "../pe.h"

typedef struct guest_profile_artifacts guest_profile_artifacts;
typedef enum guest_profile_image_kind {
    GUEST_PROFILE_ELF, GUEST_PROFILE_PE
} guest_profile_image_kind;
typedef struct guest_profile_image {
    uint64_t provider;
    guest_profile_image_kind kind;
    union { const guest_elf *elf; const guest_pe *pe; } owner;
} guest_profile_image;
typedef struct guest_profile_image_view {
    uint64_t provider;
    guest_profile_image_kind kind;
    union { const guest_elf_view *elf; const guest_pe_view *pe; } image;
} guest_profile_image_view;

/* Preserve the actual prepared dependency order and inert image witnesses.
 * This is artifact provenance, not instruction or syscall admission. No image
 * is mapped, bound, initialized, or declared safe by these operations. */
bool guest_profile_artifacts_create(const guest_profile_image *, size_t,
    guest_profile_artifacts **, qa_error *);
void guest_profile_artifacts_destroy(guest_profile_artifacts **);
size_t guest_profile_artifacts_count(const guest_profile_artifacts *);
bool guest_profile_artifacts_at(const guest_profile_artifacts *, size_t,
    guest_profile_image_view *, qa_error *);
/* Compare genuine inert owners before loading/IFUNC or cold publication.
 * Mutable relocated RAM belongs to the real loaded-image/lower owners. */
bool guest_profile_artifacts_match(const guest_profile_artifacts *,
    const guest_profile_image *, size_t, qa_error *);
bool guest_profile_artifacts_checkpoint(const guest_profile_artifacts *,
    qa_buffer *, qa_error *);
/* Complete detached decode revalidates original bytes through inert parsers.
 * It never opens a path, binds an import, maps RAM, or replays initialization. */
bool guest_profile_artifacts_decode(qa_bytes, size_t,
    guest_profile_artifacts **, qa_error *);

#endif
