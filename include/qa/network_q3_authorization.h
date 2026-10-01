#ifndef QA_NETWORK_Q3_AUTHORIZATION_H
#define QA_NETWORK_Q3_AUTHORIZATION_H
#include "qa/network_q3.h"
#include "qa/console.h"

typedef struct qa_q3_client_authorization qa_q3_client_authorization;
typedef struct qa_q3_client_authorization_bindings {
    void *context;
    qa_cvars *cvars;
    /* Pure qualification of the retained published profile and registry. */
    bool (*current)(void *, qa_error *);
    /* The published key profile owns these bytes and immutable demo policy.
     * The destination is the actual 32-byte authorization span plus NUL. */
    bool (*read_profile)(void *, uint8_t destination[33], bool *demo, qa_error *);
    void (*print)(void *, const char *);
    /* Optional actual resolver; otherwise the native DNS owner is used. */
    bool (*resolve)(void *, qa_net_address *, qa_error *);
} qa_q3_client_authorization_bindings;

bool qa_q3_client_authorization_create(const qa_q3_client_authorization_bindings *,
    qa_q3_client_authorization **, qa_error *);
void qa_q3_client_authorization_destroy(qa_q3_client_authorization *);
bool qa_q3_client_authorization_idle(const qa_q3_client_authorization *);
/* The client-static address survives individual connection attempts. Each
 * request independently qualifies the actual current connection and socket.
 * connection_current is a pure identity/lifetime predicate. */
bool qa_q3_client_authorization_request(qa_q3_client_authorization *,
    bool (*connection_current)(void *, qa_error *), qa_q3_send_fn, void *, qa_error *);
bool qa_q3_client_authorization_checkpoint(const qa_q3_client_authorization *, qa_buffer *, qa_error *);
bool qa_q3_client_authorization_restore(qa_bytes, const qa_q3_client_authorization_bindings *,
    qa_q3_client_authorization **, qa_error *);
/* Called only after the enclosing actual profile/connection qualification. */
void qa_q3_client_authorization_rebind(qa_q3_client_authorization *, void *);
#endif
