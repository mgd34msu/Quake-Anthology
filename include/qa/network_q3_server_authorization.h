#ifndef QA_NETWORK_Q3_SERVER_AUTHORIZATION_H
#define QA_NETWORK_Q3_SERVER_AUTHORIZATION_H
#include "qa/network_q3.h"

typedef struct qa_q3_server_authorization qa_q3_server_authorization;
typedef struct qa_q3_server_authorization_bindings {
    void *context;
    /* Pure actual host lifetime and source-policy observations. Returned
     * strings borrow the source registry until the enclosing callback ends. */
    bool (*current)(void *, qa_error *);
    bool (*policy)(void *, bool *enabled, const char **game, const char **strict, qa_error *);
    qa_q3_send_fn send;
    void (*print)(void *, const char *);
    bool (*resolve)(void *, qa_net_address *, qa_error *);
} qa_q3_server_authorization_bindings;

bool qa_q3_server_authorization_create(const qa_q3_server_authorization_bindings *,
    qa_q3_server_authorization **, qa_error *);
void qa_q3_server_authorization_destroy(qa_q3_server_authorization *);
bool qa_q3_server_authorization_idle(const qa_q3_server_authorization *);
/* Borrowed actual resolved authority; NULL before success or after a failed
 * lookup. The source server attempts its lookup only once per owner. */
const qa_net_address *qa_q3_server_authorization_address(const qa_q3_server_authorization *);
bool qa_q3_server_authorization_request(qa_q3_server_authorization *, const qa_q3_challenge *, qa_error *);
bool qa_q3_server_authorization_checkpoint(const qa_q3_server_authorization *, qa_buffer *, qa_error *);
bool qa_q3_server_authorization_restore(qa_bytes, const qa_q3_server_authorization_bindings *,
    qa_q3_server_authorization **, qa_error *);
void qa_q3_server_authorization_rebind(qa_q3_server_authorization *, void *);
#endif
