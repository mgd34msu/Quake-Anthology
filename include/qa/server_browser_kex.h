#ifndef QA_SERVER_BROWSER_KEX_H
#define QA_SERVER_BROWSER_KEX_H

#include "qa/server_browser.h"
#include "qa/network_kex_status.h"

/* Shared list projection; complete counted retail text remains in the raw
 * retained status and is available through the typed getter below. */
bool qa_browser_kex_decode(qa_bytes, qa_server_entry *, qa_error *);
bool qa_server_browser_kex_status(const qa_server_browser *, const qa_net_address *,
    qa_kex_status_view *, qa_error *);

/* Retail DNS-SD queries have their own retained send receipts, independent
 * of legacy connectionless query timeouts. Only a pending retail-query owner
 * supplies sent_ns. Malformed responses leave shared entries unchanged. */
bool qa_server_browser_receive_kex_discovery(qa_server_browser *,
    const qa_net_datagram *, uint64_t sent_ns, bool *recognized, qa_error *);

#endif
