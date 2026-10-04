#ifndef QA_FRONTEND_NATIVE_Q3_TOPOLOGY_H
#define QA_FRONTEND_NATIVE_Q3_TOPOLOGY_H
#include "native_q3_client.h"

typedef struct frontend_native_q3_topology frontend_native_q3_topology;
/* The actual frontend/content/assets capture leases precede the native child
 * records. Physical rows preserve their retained GAME descriptor recipe. */
bool frontend_native_q3_topology_checkpoint(qa_frontend *,const qa_audio_checkpoint_refs *,qa_buffer *,qa_error *);
/* Decode all metadata and resource ordinals before provider construction.
 * Borrowed child bytes and graph holders outlive the decoded topology. */
bool frontend_native_q3_topology_decode(qa_frontend *,qa_bytes,
    frontend_native_q3_topology **,qa_error *);
/* Actual provider routing precedes callback and detached media preparation.
 * Saved COMMANDS bindings exist before core COMMANDS import, without CG_Init.
 * The genuine row owns each claimed view and every partial media factory root. */
bool frontend_native_q3_topology_prepare(frontend_native_q3_topology *,
    const qa_launch_snapshot *,qa_error *);
size_t frontend_native_q3_topology_count(const frontend_native_q3_topology *);
bool frontend_native_q3_topology_read(frontend_native_q3_topology *,size_t,
    frontend_native_q3 **,frontend_native_q3_import **,q3n_client_refs *,qa_error *);
void frontend_native_q3_topology_destroy(frontend_native_q3_topology *);
#endif
