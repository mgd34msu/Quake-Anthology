#ifndef QA_NATIVE_SYSV_PROGRAM_SAVE_H
#define QA_NATIVE_SYSV_PROGRAM_SAVE_H

#include "qa/native_sysv_program.h"

typedef struct qa_native_sysv_program_restore_bindings {
    qa_native_guest_backend backend;
    size_t maximum_backing_bytes, maximum_image_bytes;
    const char *host_executable;
    const guest_profile_guard_launch *profile_guard;
    qa_native_sysv_program_services services;
} qa_native_sysv_program_restore_bindings;
/* Captures actual kernel descriptor offsets/aliases/close progress/task state,
 * original program/interpreter artifacts, historical startup ownership and
 * current named CPU/RAM/backing/allocator state. External native objects are
 * held by the enclosing captured resource graph, not reopened by this codec. */
bool qa_native_sysv_program_checkpoint(qa_native_sysv_program *, qa_buffer *, qa_error *);
bool qa_native_sysv_program_restore(qa_bytes, const qa_native_sysv_program_restore_bindings *,
    qa_native_sysv_program **, qa_error *);
/* Candidate capabilities have independent actual native-object holds. Publish
 * without file I/O or source execution; previous retains its checked close work. */
bool qa_native_sysv_program_adopt_owned(qa_native_sysv_program *, qa_native_sysv_program *, qa_error *);

#endif
