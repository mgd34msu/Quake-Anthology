#ifndef QA_LAUNCH_IDENTITY_H
#define QA_LAUNCH_IDENTITY_H
#include "qa/launch.h"
/* Portable native launch identity, version 4. Every choice is explicit; actor
 * references are saved slot/generation pairs relative to the supplied registry.
 * It is distinct from original game wire schemas. Outputs transfer on success.
 * The canonical encoding preserves choice order and arbitrary option bytes. */
bool qa_launch_identity_encode(const qa_launch_snapshot *, const qa_actor_registry *,
    qa_buffer *, qa_error *);
/* Encode one selected mode with the same explicit ordered fields as the full
 * launch identity. Map transitions do not change this configuration record. */
bool qa_launch_mode_identity_encode(const qa_launch_snapshot *, size_t index,
    qa_buffer *, qa_error *);
/* Reconstructs an empty draft without preset defaults. A candidate registry is
 * required when actor scopes/seats name actors. Decode does not admit content:
 * after preparation, match the entire candidate identity before publication. */
bool qa_launch_identity_decode(qa_catalog *, const qa_actor_registry *, qa_bytes,
    qa_launch_draft **, qa_error *);
bool qa_launch_identity_match(const qa_launch_snapshot *, const qa_actor_registry *,
    qa_bytes canonical_identity, qa_error *);
#endif
