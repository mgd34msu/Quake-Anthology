#ifndef QA_NETWORK_Q3_DOWNLOAD_H
#define QA_NETWORK_Q3_DOWNLOAD_H
#include "qa/network_q3.h"
#include "qa/filesystem.h"

typedef struct qa_q3_download_window qa_q3_download_window;
/* Resolve an actual immutable mounted package, without recording a guest read.
 * The window copies the span before returning to its owner. Absent names return
 * an empty span; errors describe an invalid mounted authority. */
typedef struct qa_q3_download_source {
    void *context;
    bool (*resolve)(void *, const char *, qa_bytes *, const qa_fs_identity **, qa_error *);
} qa_q3_download_source;
typedef struct qa_q3_download_offer {
    const qa_q3_download_window *owner;
    uint64_t revision;
    int32_t next_transmit, send_time;
    bool denied;
    size_t count;
} qa_q3_download_offer;
bool qa_q3_download_window_create(const qa_q3_download_source *, qa_q3_download_window **, qa_error *);
void qa_q3_download_window_destroy(qa_q3_download_window *);
/* Rebind the same installed resolver to the qualified candidate owner after
 * its enclosing graph moves. Does not invoke it or change producer state. */
void qa_q3_download_window_rebind(qa_q3_download_window *, void *source_context);
bool qa_q3_download_window_begin(qa_q3_download_window *, const char *, qa_error *);
void qa_q3_download_window_close(qa_q3_download_window *);
const char *qa_q3_download_window_name(const qa_q3_download_window *);
bool qa_q3_download_window_active(const qa_q3_download_window *);
/* A mismatched acknowledgement reports broken=true; the installed source
 * caller must retire its genuine client with the original broken-download reason. */
bool qa_q3_download_window_acknowledge(qa_q3_download_window *, int32_t block, int32_t time,
    bool *broken, qa_error *);
/* Encode through the genuine packet writer until the source rate/cursors stop
 * or that writer fails. Prefetch advances the real read cursor. Transmission
 * advances only after the caller commits a successfully encoded whole packet.
 * Draining an older fragment/FIFO packet must not invoke this producer. */
bool qa_q3_download_window_write(qa_q3_download_window *, bool enabled, bool pure, int32_t time,
    const qa_q3_server_rate *, qa_q3_writer *, qa_q3_download_offer *, qa_error *);
bool qa_q3_download_window_commit(qa_q3_download_window *, const qa_q3_download_offer *, qa_error *);
bool qa_q3_download_window_checkpoint(const qa_q3_download_window *, qa_buffer *, qa_error *);
bool qa_q3_download_window_restore(qa_bytes, const qa_q3_download_source *, qa_q3_download_window **, qa_error *);
#endif
