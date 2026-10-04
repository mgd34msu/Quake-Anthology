#ifndef QA_FRONTEND_REMOTE_Q3_GRAPH_ROSTER_H
#define QA_FRONTEND_REMOTE_Q3_GRAPH_ROSTER_H
#include "remote_q3_graph.h"
#include "remote_q3_graph_children.h"
#include "remote_q3_modules_save.h"

typedef struct frontend_remote_q3_graph_roster frontend_remote_q3_graph_roster;
/* Shared dictionaries own the parent heaps and numeric registries. The
 * physical QNRS cell owns QANCM; this envelope owns only QRGT/QRCH/QRMW. */
bool frontend_remote_q3_graph_checkpoint(qa_frontend *,const frontend_remote_q3_modules_save_refs *,
    qa_buffer *,qa_error *);
bool frontend_remote_q3_graph_decode(qa_frontend *,qa_bytes,frontend_remote_q3_graph_roster **,qa_error *);
/* Actual CONNECTIONS staging precedes this constructor prefix and COMMANDS. */
bool frontend_remote_q3_graph_prepare(frontend_remote_q3_graph_roster *,qa_error *);
/* Imported parent world/heaps precede empty role backends and QANCM RAM. */
bool frontend_remote_q3_graph_prepare_modules(frontend_remote_q3_graph_roster *,qa_error *);
/* World adoption precedes backend preparation; numeric Q3AS is still empty. */
bool frontend_remote_q3_graph_prepare_runtime(frontend_remote_q3_graph_roster *,qa_error *);
/* Actual registry capture and imported shared holders precede private children. */
bool frontend_remote_q3_graph_restore_children(frontend_remote_q3_graph_roster *,const qa_audio_checkpoint_refs *,qa_error *);
bool frontend_remote_q3_graph_restore_modules(frontend_remote_q3_graph_roster *,
    const frontend_remote_q3_modules_save_refs *,qa_error *);
/* Snapshot/PPS import precedes QRPD proof; finished frames bind only after
 * actual asset capture has returned. No frame callback or Init is invoked. */
bool frontend_remote_q3_graph_restore_frames(frontend_remote_q3_graph_roster *,qa_error *);
bool frontend_remote_q3_graph_finish_modules(frontend_remote_q3_graph_roster *,
    const frontend_remote_q3_modules_save_refs *,qa_error *);
bool frontend_remote_q3_graph_finish_sources(frontend_remote_q3_graph_roster *,qa_error *);
void frontend_remote_q3_graph_destroy(frontend_remote_q3_graph_roster *);
#endif
