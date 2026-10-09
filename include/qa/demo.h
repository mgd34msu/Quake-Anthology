#ifndef QA_DEMO_H
#define QA_DEMO_H

#include "qa/save.h"
#include "qa/network.h"

typedef enum qa_demo_record_kind {
    QA_DEMO_KEYFRAME = 1, QA_DEMO_ADVANCE, QA_DEMO_INPUT, QA_DEMO_NETWORK,
    QA_DEMO_JOURNAL, QA_DEMO_END
} qa_demo_record_kind;
typedef struct qa_demo_record {
    qa_demo_record_kind kind;
    uint64_t sequence, time_ns, elapsed_ns;
    qa_net_protocol_id protocol;
    qa_bytes payload;
} qa_demo_record;
typedef struct qa_demo qa_demo;
typedef struct qa_demo_recorder qa_demo_recorder;

/* Shared recordings preserve the whole selected composition through full save
 * keyframes. Source network dialects remain tagged and use their native packet
 * codecs. ADVANCE records the supplied simulation duration; input/network/
 * journal codecs must encode fields explicitly before append. Initial keyframe
 * is mandatory. A partial append faults the recorder and is never retried. */
bool qa_demo_record_begin(qa_fs_root *, const char *relative_name,
                           const qa_save_image *initial, qa_demo_recorder **, qa_error *);
bool qa_demo_record_append(qa_demo_recorder *, qa_demo_record_kind, uint64_t elapsed_ns,
                            qa_net_protocol_id, qa_bytes, qa_error *);
bool qa_demo_record_keyframe(qa_demo_recorder *, const qa_save_image *, qa_error *);
bool qa_demo_record_end(qa_demo_recorder *, qa_error *);
/* Buffered records copy into a fixed 256 KiB byte queue. A writer thread writes
 * and syncs batches of at least 64 KiB; ordinary appends perform no file I/O or
 * allocation. Queue exhaustion faults recording and keeps its valid file prefix.
 * Explicit flush, keyframes, END and destruction drain on the caller's request.
 * A crash may lose the queued tail; only complete written frames are replayed. */
bool qa_demo_record_buffered(qa_demo_recorder *, bool, qa_error *);
bool qa_demo_record_flush(qa_demo_recorder *, qa_error *);
void qa_demo_recorder_destroy(qa_demo_recorder *);

/* take transfers owned bytes on success and clears them. recover_tail admits
 * an unfinished recording only through its last complete bounded block;
 * malformed complete record fields always fail. No malformed source/shared
 * signature fallback. The returned records borrow immutable demo storage. */
bool qa_demo_take(qa_buffer *, bool recover_tail, qa_demo **, qa_error *);
bool qa_demo_read(qa_fs_root *, const char *, bool recover_tail, qa_demo **, qa_error *);
void qa_demo_destroy(qa_demo *);
size_t qa_demo_record_count(const qa_demo *);
const qa_demo_record *qa_demo_record_at(const qa_demo *, size_t);
bool qa_demo_complete(const qa_demo *);
uint64_t qa_demo_start_time(const qa_demo *);
uint64_t qa_demo_end_time(const qa_demo *);

/* Seeking is replay into an isolated candidate from the nearest preceding
 * complete keyframe. Publication follows successful replay and validation.
 * reached_ns reports the last whole simulation frame at/before the target. */
typedef struct qa_demo_seek_ops {
    bool (*create)(void *, const qa_save_image *, void **candidate, qa_error *);
    bool (*apply)(void *, void *candidate, const qa_demo_record *, qa_error *);
    bool (*finish)(void *, void *candidate, qa_error *);
    bool (*publish)(void *, void *candidate, qa_error *);
    void (*discard)(void *, void *candidate);
} qa_demo_seek_ops;
bool qa_demo_seek(const qa_demo *, uint64_t target_ns, void *, const qa_demo_seek_ops *,
                   uint64_t *reached_ns, qa_error *);

#endif
