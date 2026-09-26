#ifndef QA_NETWORK_Q1_SESSION_H
#define QA_NETWORK_Q1_SESSION_H
#include "qa/network_q1.h"

typedef struct qa_nq_signon {
    uint8_t stage;
    const char *name, *spawn_parameters;
    uint8_t color;
    bool has_extension_flags;
    uint32_t extension_flags;
} qa_nq_signon;
bool qa_nq_signon_receive(qa_nq_signon *, uint8_t stage, qa_net_writer *);
void qa_nq_signon_first_entity(qa_nq_signon *);

/* emit borrows its bytes only for the call. A false return means the message
 * was not queued. The owner closes a failed connection before retrying a
 * multi-message command whose earlier emissions may already be queued. */
typedef bool (*qa_q1_emit_fn)(void *, qa_bytes, qa_error *);
typedef struct qa_qw_download {
    void *state;
    uint64_t size;
    bool (*read)(void *, uint64_t offset, uint8_t *, size_t count, qa_error *);
    void (*close)(void *);
} qa_qw_download;
typedef struct qa_qw_signon_host {
    void *user;
    bool (*server_data)(void *, qa_qw_serverdata *, qa_error *);
    bool (*names)(void *, bool models, const char *const **, size_t *, qa_error *);
    bool (*buffers)(void *, const qa_bytes **, size_t *, qa_error *);
    bool (*accepts_checksum)(void *, uint32_t);
    bool (*spawn)(void *, uint8_t start_client, qa_q1_emit_fn, void *, qa_error *);
    bool (*begin)(void *, qa_error *);
    void (*disconnect)(void *, const char *reason);
    /* Missing is successful with found=false. Successful found transfers the
     * download to this signon instance until completion, replacement or close.
     * The storage owner enforces mount permissions and contained resolution. */
    bool (*open_download)(void *, const char *, bool *found, qa_qw_download *, qa_error *);
} qa_qw_signon_host;
typedef struct qa_qw_signon qa_qw_signon;
bool qa_qw_signon_create(const qa_qw_signon_host *, bool donor_wide, qa_qw_signon **, qa_error *);
void qa_qw_signon_destroy(qa_qw_signon *);
bool qa_qw_signon_command(qa_qw_signon *, const char *, qa_q1_emit_fn, void *,
                          bool *handled, qa_error *);
bool qa_qw_signon_spawned(const qa_qw_signon *);
void qa_qw_signon_close_download(qa_qw_signon *);

typedef struct qa_qw_precache qa_qw_precache;
bool qa_qw_precache_create(qa_net_protocol_id, qa_qw_precache **, qa_error *);
void qa_qw_precache_destroy(qa_qw_precache *);
bool qa_qw_precache_reset(qa_qw_precache *, qa_net_protocol_id, int32_t server_count,
                          qa_net_writer *client_command);
bool qa_qw_precache_list(qa_qw_precache *, bool models, uint32_t first,
                         const char *const *names, size_t count, uint32_t next,
                         qa_net_writer *client_command);
const char *qa_qw_precache_name(const qa_qw_precache *, bool models, size_t index);
size_t qa_qw_precache_count(const qa_qw_precache *, bool models);
bool qa_qw_precache_sounds_ready(const qa_qw_precache *, qa_net_writer *);
bool qa_qw_precache_models_ready(const qa_qw_precache *, uint32_t checksum, qa_net_writer *);
bool qa_qw_precache_skins_ready(const qa_qw_precache *, qa_net_writer *);
bool qa_qw_choose_protocol(uint32_t requested_version, bool needs_wide, qa_net_protocol_id *, qa_error *);

#endif
