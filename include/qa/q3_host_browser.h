#ifndef QA_Q3_HOST_BROWSER_H
#define QA_Q3_HOST_BROWSER_H

#include "qa/common.h"

/* Readers are lexical borrows of one syscall. A successful read returns owned
 * NUL-terminated bytes, released by the browser with qa_buffer_free. The name
 * reader copies at most 31 source bytes without requiring a later terminator. */
typedef struct qa_q3_host_browser_reader {
    void *context;
    bool (*read)(void *, qa_buffer *, qa_error *);
} qa_q3_host_browser_reader;

/* The writer is valid only during the service call. NULL text requests a
 * single NUL byte, independent of capacity. Text requests Q_strncpyz semantics.
 * A failed write must precede the browser's subsequent retrieval/ping effects. */
typedef struct qa_q3_host_browser_writer {
    void *context;
    bool (*write)(void *, const char *, qa_error *);
} qa_q3_host_browser_writer;

/* Borrows the actual UI browser owner. That owner retains LAN list order,
 * visibility values, ping/status requests, clocks and cache continuation.
 * Sources are literal UI integers: local=0, secondary master=1, global=2,
 * favorites=3. Invalid inputs retain the original per-operation behavior.
 * All callbacks are required when bound. An all-zero service is unbound. */
typedef struct qa_q3_host_browser_services {
    void *context;
    bool (*current)(void *, qa_error *);
    bool (*server_count)(void *, int32_t source, int32_t *, qa_error *);
    bool (*server_address)(void *, int32_t source, int32_t index, int32_t capacity,
                           const qa_q3_host_browser_writer *, qa_error *);
    bool (*server_info)(void *, int32_t source, int32_t index, int32_t capacity,
                        const qa_q3_host_browser_writer *, qa_error *);
    bool (*ping_count)(void *, int32_t *, qa_error *);
    bool (*clear_ping)(void *, int32_t index, qa_error *);
    bool (*get_ping)(void *, int32_t index, int32_t capacity,
                     const qa_q3_host_browser_writer *, int32_t *time, qa_error *);
    bool (*ping_info)(void *, int32_t index, int32_t capacity,
                      const qa_q3_host_browser_writer *, bool *present, qa_error *);
    bool (*mark_visible)(void *, int32_t source, int32_t index, int32_t value, qa_error *);
    bool (*update_pings)(void *, int32_t source, bool *active, qa_error *);
    bool (*reset_pings)(void *, int32_t source, qa_error *);
    bool (*load_cache)(void *, qa_error *);
    bool (*save_cache)(void *, qa_error *);
    /* Add guards source/capacity before reading address, then resolves and
     * rejects duplicates before reading name. Retains reached partial effects. */
    bool (*add_server)(void *, int32_t source, const qa_q3_host_browser_reader *name,
                       const qa_q3_host_browser_reader *address, int32_t *, qa_error *);
    bool (*remove_server)(void *, int32_t source,
                          const qa_q3_host_browser_reader *address, qa_error *);
    /* NULL address resets all requests. NULL writer resets that address.
     * Completed output is written before marking a request retrieved. */
    bool (*server_status)(void *, const char *address, int32_t capacity,
                          const qa_q3_host_browser_writer *, bool *present, qa_error *);
    bool (*server_ping)(void *, int32_t source, int32_t index, int32_t *, qa_error *);
    bool (*server_visible)(void *, int32_t source, int32_t index, int32_t *, qa_error *);
    bool (*compare_servers)(void *, int32_t source, int32_t key, int32_t direction,
                            int32_t first, int32_t second, int32_t *, qa_error *);
} qa_q3_host_browser_services;

/* Pure shape checks. Mask describes callback presence, never pointer identity
 * or browser state. The enclosing source owner saves that state independently. */
uint32_t qa_q3_host_browser_services_mask(const qa_q3_host_browser_services *);
bool qa_q3_host_browser_services_validate(const qa_q3_host_browser_services *, qa_error *);

#endif
