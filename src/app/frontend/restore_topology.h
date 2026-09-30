#ifndef QA_FRONTEND_RESTORE_TOPOLOGY_H
#define QA_FRONTEND_RESTORE_TOPOLOGY_H
#include "internal.h"
#include "source_restore.h"
typedef struct frontend_restore_topology frontend_restore_topology;
/* Early constructor section of the complete frontend owner. Content graph
 * identities and foundation service keys qualify real heap admission; private
 * presentation/audio/input/media continuations remain separate sections. */
bool frontend_topology_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
bool frontend_topology_decode(qa_application *, qa_bytes, frontend_restore_topology **, qa_error *);
void frontend_topology_destroy(frontend_restore_topology *);
bool frontend_topology_prepare(qa_frontend *, const frontend_restore_topology *, qa_error *);
const bool *frontend_topology_mods(const frontend_restore_topology *);
#endif
