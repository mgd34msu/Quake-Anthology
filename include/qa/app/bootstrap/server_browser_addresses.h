/* Port of src/app/bootstrap/server-browser-addresses.ts. JSON.parse ->
 * landed qa_parse_json_value. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SERVER_BROWSER_ADDRESSES_H
#define QA_APP_BOOTSTRAP_SERVER_BROWSER_ADDRESSES_H
#include <stddef.h>
#include "qa/network/common/endpoint.h"
#include "qa/persistence/value.h"
typedef struct qa_app_direct_server_address { char *remote; qa_net_address address; } qa_app_direct_server_address;
enum { QA_APP_MAXIMUM_DIRECT_SERVERS = 16 };
/* TS directServerText(value: unknown): non-string JSON kinds abort alike. */
const char *qa_app_direct_server_text(const char *value);
const char *qa_app_direct_server_text_value(const qa_save_value *value);
/* Malloc'd array; free with qa_app_direct_servers_free. */
qa_app_direct_server_address *qa_app_read_direct_servers(const char *text, size_t *out_count);
void qa_app_direct_servers_free(qa_app_direct_server_address *servers, size_t count);
/* Shared with server-browser-cache: parseNetworkAddress over a JSON value. */
void qa_app_parse_network_address_value(const qa_save_value *value, qa_net_address *out);
#endif
