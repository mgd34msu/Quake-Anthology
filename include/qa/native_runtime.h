#ifndef QA_NATIVE_RUNTIME_H
#define QA_NATIVE_RUNTIME_H

#include "qa/native.h"

typedef struct qa_native_runtime qa_native_runtime;
struct guest_profile_guard_launch;
typedef struct qa_native_runtime_options {
    /* Actual executable directory; the build defines its install-relative
     * runtime directory. Explicit roots override that package. */
    const char *executable_directory;
    const char *root;
    const qa_native_runtime_config *overrides;
} qa_native_runtime_options;

/* Opens existing artifacts only, inspects their actual target, and retains
 * their file identities. Missing default package slots stay unavailable.
 * Explicit overrides must exist. No source or launcher executes. */
bool qa_native_runtime_create(const qa_native_runtime_options *, qa_native_runtime **, qa_error *);
void qa_native_runtime_retain(qa_native_runtime *);
void qa_native_runtime_release(qa_native_runtime *);
const qa_native_runtime_config *qa_native_runtime_configuration(const qa_native_runtime *);
const char *qa_native_runtime_bootstrap(const qa_native_runtime *);
/* Borrows the retained Linux x64 source monitor launcher/client after checking
 * their actual file identities. Installation and CPU admission remain the
 * physical child's responsibility. The runtime owner must outlive the borrow. */
bool qa_native_runtime_profile_launch(const qa_native_runtime *,
    struct guest_profile_guard_launch *, qa_error *);
/* Frontend restore retains the actual owner before constructing providers.
 * The capability continuation must validate before their private import;
 * it never discovers replacement paths or reconstructs a native singleton. */
bool qa_native_runtime_checkpoint(const qa_native_runtime *, qa_buffer *, qa_error *);
bool qa_native_runtime_validate(const qa_native_runtime *, qa_bytes, qa_error *);

#endif
