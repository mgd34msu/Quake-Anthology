#ifndef QA_FRONTEND_DEMO_SERVICE_H
#define QA_FRONTEND_DEMO_SERVICE_H

#include "qa/console.h"
#include "qa/demo.h"
#include "qa/filesystem.h"
#include "qa/network_q1.h"

typedef enum frontend_demo_format {
    FRONTEND_DEMO_NQ, FRONTEND_DEMO_QW, FRONTEND_DEMO_Q2,
    FRONTEND_DEMO_Q3, FRONTEND_DEMO_MVD, FRONTEND_DEMO_Q2_SERVER
} frontend_demo_format;
typedef struct frontend_demo_packet {
    frontend_demo_format format;
    union {
        qa_q1_demo_record nq;
        qa_qw_demo_record qw;
        qa_bytes message;
        struct { int32_t sequence; qa_bytes message; } q3;
    } value;
} frontend_demo_packet;
typedef enum frontend_demo_end {
    FRONTEND_DEMO_RUNNING, FRONTEND_DEMO_EOF, FRONTEND_DEMO_TERMINATOR,
    FRONTEND_DEMO_DISCONNECTED, FRONTEND_DEMO_TRUNCATED, FRONTEND_DEMO_CLOSED
} frontend_demo_end;
typedef struct frontend_demo_reader frontend_demo_reader;
/* Packets borrow the admitted recording until its actual playback owner closes.
 * MVD packets borrow the canonical framer until the next read instead.
 * Q3 publishes a complete sequence word before admitting its length, including
 * terminal and truncated records. Other formats do not call sequence. */
bool frontend_demo_read_next(frontend_demo_reader *,
    bool (*sequence)(void *, int32_t, qa_error *), void *,
    frontend_demo_packet *, bool *present, frontend_demo_end *, qa_error *);
int32_t frontend_demo_forced_track(const frontend_demo_reader *);
/* The real protocol constructor may inspect its own native header without
 * advancing the framing cursor. This span lives until playback closes. */
qa_bytes frontend_demo_reader_bytes(const frontend_demo_reader *);
/* Q2 protocol admission borrows the next native payload without consuming it. */
bool frontend_demo_peek_message(const frontend_demo_reader *, qa_bytes *, qa_error *);

typedef struct frontend_demo_sink {
    void *owner;
    bool (*append)(void *, const frontend_demo_packet *, qa_error *);
} frontend_demo_sink;
typedef struct frontend_demo_record_source {
    void *owner;
    frontend_demo_format format;
    qa_net_protocol_id protocol;
    qa_fs_root *root;
    int32_t forced_track;
    bool (*current)(const void *);
    bool (*seed)(void *, const frontend_demo_sink *, qa_error *);
    /* attached reports actual sink custody, including partial failed adoption. */
    bool (*attach)(void *, const frontend_demo_sink *, bool *attached, qa_error *);
    bool (*detach)(void *, const frontend_demo_sink *, qa_error *);
    /* QW reconnect keeps this exact sink attached through the genuine connecting
     * source. It must replace its real receiver and record that receiver's signon. */
    bool (*reconnect)(void *, const qa_command_context *, const frontend_demo_sink *,
        bool *attached, qa_error *);
    bool (*release)(void **, qa_error *);
} frontend_demo_record_source;
typedef struct frontend_demo_playback_source {
    void *owner;
    bool (*current)(const void *);
    /* The existing protocol CLIENT receiver owns priming, packet-time gating,
     * command prediction and presentation clocks. The service owns file framing. */
    bool (*advance)(void *, frontend_demo_reader *, uint64_t elapsed_ns,
        uint64_t frame, bool timedemo, frontend_demo_end *, qa_error *);
    bool (*release)(void **, qa_error *);
} frontend_demo_playback_source;

typedef enum frontend_demo_action {
    FRONTEND_DEMO_PLAY, FRONTEND_DEMO_STOP_PLAY, FRONTEND_DEMO_RECORD,
    FRONTEND_DEMO_RERECORD, FRONTEND_DEMO_STOP_RECORD,
    FRONTEND_DEMO_SERVER_RECORD, FRONTEND_DEMO_SERVER_STOP,
    FRONTEND_DEMO_MVD_RECORD, FRONTEND_DEMO_MVD_STOP
} frontend_demo_action;
typedef struct frontend_demo_request {
    frontend_demo_action action;
    qa_command_context source;
    const char *name;
    frontend_demo_format fallback;
    bool timedemo, attract;
} frontend_demo_request;
typedef struct frontend_demo_service_options {
    void *context;
    /* These are real source/configuration owners, not console command strings.
     * A menu captures the actual seat context before staging; execution checks it
     * again after all entered command callbacks have returned. */
    bool (*current)(void *, const qa_command_context *, qa_error *);
    bool (*read)(void *, const qa_command_context *, const char *, qa_buffer *, bool *found, qa_error *);
    bool (*record_source)(void *, const qa_command_context *, frontend_demo_action,
        frontend_demo_record_source *, qa_error *);
    /* Nonempty out owns cleanup responsibility even after failed construction. */
    bool (*playback_source)(void *, const qa_command_context *, frontend_demo_format,
        uint32_t recorded_protocol, frontend_demo_reader *,
        frontend_demo_playback_source *, qa_error *);
    void (*print)(void *, const char *);
    /* Read completion commands while their genuine CLIENT still exists. The
     * returned notification follows its checked receiver/file release. */
    bool (*completing)(void *, const qa_command_context *, frontend_demo_format,
        frontend_demo_end, bool attract, qa_error *);
    bool (*completed)(void *, const qa_command_context *, frontend_demo_format,
        frontend_demo_end, bool attract, qa_error *);
} frontend_demo_service_options;
typedef struct frontend_demo_service frontend_demo_service;
bool frontend_demo_service_create(const frontend_demo_service_options *, frontend_demo_service **, qa_error *);
/* Copies names and script provenance. This never opens a file, retires a source,
 * reconnects a channel or enters another command while its caller is entered. */
bool frontend_demo_stage(frontend_demo_service *, const frontend_demo_request *, qa_error *);
bool frontend_demo_execute(frontend_demo_service *, qa_error *);
bool frontend_demo_advance(frontend_demo_service *, uint64_t elapsed_ns, uint64_t frame, qa_error *);
/* Detach returned retired/faulted feeds without advancing a playback clock. */
bool frontend_demo_sources_returned(frontend_demo_service *,qa_error *);
bool frontend_demo_service_idle(const frontend_demo_service *);
bool frontend_demo_service_pending(const frontend_demo_service *);
bool frontend_demo_service_active(const frontend_demo_service *);
bool frontend_demo_playback_attract(const frontend_demo_service *);
/* Cancels only the real queued attract request; manual Library requests stay. */
void frontend_demo_cancel_attract(frontend_demo_service *);
const char *frontend_demo_recording_path(const frontend_demo_service *);
const char *frontend_demo_server_recording_path(const frontend_demo_service *);
const char *frontend_demo_playback_path(const frontend_demo_service *);
/* Detaches actual feeds before closing their file or source. Checked refusal
 * retains the owner for a later returned cleanup attempt. No asset bytes save. */
bool frontend_demo_service_stop(frontend_demo_service *, qa_error *);
bool frontend_demo_service_destroy(frontend_demo_service **, qa_error *);

#endif
