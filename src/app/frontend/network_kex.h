#ifndef QA_FRONTEND_NETWORK_KEX_H
#define QA_FRONTEND_NETWORK_KEX_H

#include "qa/network_kex_discovery.h"
#include "qa/server_browser_kex.h"

typedef struct frontend_kex_browser frontend_kex_browser;
typedef struct frontend_kex_browser_hooks {
    void *context;
    bool (*send)(void *, const qa_net_address *, qa_bytes, qa_error *);
} frontend_kex_browser_hooks;

/* Borrows the existing shared browser and actual sole socket sender. */
bool frontend_kex_browser_open(qa_server_browser *, const frontend_kex_browser_hooks *,
                               frontend_kex_browser **, qa_error *);
bool frontend_kex_browser_scan(frontend_kex_browser *, qa_error *);
bool frontend_kex_browser_pump(frontend_kex_browser *, uint64_t now_ns, qa_error *);
bool frontend_kex_browser_receive(frontend_kex_browser *, const qa_net_datagram *,
                                  bool *recognized, qa_error *);
bool frontend_kex_browser_idle(const frontend_kex_browser *);
void frontend_kex_browser_destroy(frontend_kex_browser *);
bool frontend_kex_browser_checkpoint(const frontend_kex_browser *, qa_buffer *, qa_error *);
bool frontend_kex_browser_restore(qa_bytes, qa_server_browser *,
    const frontend_kex_browser_hooks *, frontend_kex_browser **, qa_error *);
bool frontend_kex_browser_activate(frontend_kex_browser *, qa_error *);
bool frontend_kex_browser_publish(frontend_kex_browser *, qa_error *);

#endif
